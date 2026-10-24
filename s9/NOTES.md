# iOS no Galaxy S9 (SM-G9600, starqlte, SDM845): notas de trabalho

Referência: ios-sweet (`../ref/sweet/ios-sweet-main`, MIT): XNU + userland do iPhone X 16.7.16 no Redmi Note 10 Pro (SM7150).
Kernel Samsung/LineageOS 20 do S9: `../ref/kernel/android_kernel_samsung_sdm845-lineage-20`.
Firmware stock: `../ZTO-G9600ZHU9FVF2` (G9600ZHU9FVF2, Android 10).

## Estado (2026-10-23)

- M0: arquivos descompactados, boot.img stock desmontado, fatos de hardware conferidos no DTB stock (abaixo).
  Empacotamento testado com Image falso: header igual ao stock, DTBs intactos.
- Toolchain: gcc 16.1.0 / binutils 2.47 cross do Arch, **desempacotados em `toolchain/root`** (sem root; sha256 conferidos
  contra /var/lib/pacman/sync/extra.db). `build.sh` usa sozinho. A senha de sudo do roteiro era marcador: comandos de
  root, o dono roda com `! sudo ...`.
- M1: compilado sem warnings (Image 84 KB, header ok, sem SIMD/memset, pintura antes de qualquer MMIO).
  **Flash 1** (2026-10-22 ~22:05): `heimdall flash --BOOT out/boot.img` (cópia: `out/boot-m1.img`, sha256 6c04d7df...),
  "upload ok" após 1 retry. Resultado: **logo da Samsung parado por horas**.
  Causa provável: bug do retry do Heimdall (BridgeManager.cpp: o retry cria outro SendFilePartPacket, que lê o
  PRÓXIMO pedaço do arquivo; o aparelho aceitou como parte 0, ou seja, a imagem gravou deslocada, sem cabeçalho).
  **Confirmado por readback (2026-10-23, flash 2 = M1.1 também com retry no 0%):** partição MiB 0 = MiB 1 do
  arquivo, MiB 1 = zeros, MiB 2-10 certos. Sem "ANDROID!" o ABL mostra o logo com o quadro de status (RPMB,
  CURRENT BINARY...) e para. O shim nunca rodou nos flashes 1 e 2. TWRP: `omni_starqltechn`, boot = sda20 em
  /dev/block/platform/soc/1d84000.ufshc/by-name/boot; pstore ativo (ramoops 0x100000@0xa1300000, ecc 0).
  ABL: msm_drm.dsi_display0=ss_dsi_panel_S6E3HA8_AMB577PX01_WQHD:, lcd_id=0x910443, revision=14, debug_level LOW.
  Regra: flash com "Retrying" = flash inválido. Conferir sempre por readback no TWRP (`tools/twrp-check.sh`).

- **M1.1** (pronto, sha256 f1c5dc86...): M1 + caixa-preta na RAM (ramlog.c, zona console do ramoops 0xa1340000,
  formato persistent_ram_buffer, sig 0x43474244, runs fazem append) + watchdog armado (bark 15 s, bite 18 s) +
  fim em reboot pedindo recovery (IMEM 0x77665502; o ABL Samsung talvez só olhe o PMIC: teste grátis).
  Procedimento: TWRP -> `tools/twrp-check.sh out/boot-m1.img` (confirma o flash 1) -> `tools/flash-twrp.sh`
  (dd + readback) -> observar -> voltar ao TWRP -> `tools/twrp-check.sh` (lê a caixa-preta).
  Reset forçado Power+Vol- = s2-type 8 (DVDD hard reset): a RAM provavelmente se perde. Por isso o watchdog.

- **M1 CONCLUÍDO (2026-10-23, M1.1 gravado por TWRP+dd com readback):** tela do shim aparece, barra anda,
  reinicia e roda de novo (o cookie de recovery no IMEM é ignorado: o ABL olha o PMIC). Foto: cores certas.
  Valores lidos no hardware: shim carregado em **0x800FC000**, DTB em **0x85500000**, EL1, MIDR 0x517F803C (Kryo
  385 prata), 16K sim, PSCI 1.0 (0x00010000), watchdog desligado na entrada, MDSS 0x40000001.
  Scanout: SSPP0 (VIG0) addr 0x9D400000, src 720x2960 (meia largura, split em 2 LM), stride 5760,
  fmt 0x000237FF, unpack 0x03020001 (B,G,R,A = a8r8g8b8). CTL0 TOP 0x00020020 (INTF_1/DSI0, modo comando).
  **PP0 AUTOREFRESH = 0x80000002**: o ABL deixa o autorefresh ligado (a cada 2 TE, ~30 Hz) -> escrever na RAM
  basta, igual ao painel de vídeo do sweet. Não precisa de kick.
- pstore NÃO sobrevive a reboot neste Samsung: PON tem `qcom,store-hard-reset-reason` -> reboots viram hard
  reset (testado com `adb reboot recovery`: pstore vazio). Log de verdade = tela; log persistente = UFS (depois).
- DTB vivo (TWRP): `dtb/live-twrp.dts`. CPUs reg 0x000..0x700 (Aff1 = núcleo, 0-3 prata A55, 4-7 ouro A75),
  psci 1.0 smc, timer PPI 1/2/3/0 (vtimer INTID 19), GICD 0x17a00000, GICR 0x17a60000 stride 0x20000,
  memória 0x80000000 + 0xFE1C0000.
- **M2** (`./build.sh m2`, sha256 d26038d8...): features, PSCI_FEATURES, CPU_ON nos 7 secundários (MPIDR+MIDR),
  GIC Group 1: vtimer (espera ~30 IRQs em 300 ms) e SGI 1 (espera 10). Fica 90 s na tela e reinicia.

- **M2 CONCLUÍDO (2026-10-23):** CPU0 MPIDR 0x81000000. PSCI CPU_ON ret=0 nos 7: CPU1-3 MIDR 0x517F803C (A55),
  CPU4-7 0x516F802D (A75). EL1, 16K sim, PARange 2 (40 bits), GICsys 1, ICC_SRE 1, CNTFRQ 19,2 MHz.
  PSCI 1.0, SMCCC_VERSION ausente (FFFFFFFF). FEATURES: CPU_ON 0, OFF 0, SUSPEND 3, MIGRATE_INFO -1,
  SYSTEM_OFF 0, RESET 0, RESET2 -1, SYSTEM_SUSPEND -1. GICD_CTLR 0x10 -> 0x12, TYPER ~0x007B0418; GICR do
  CPU0 = 0x17A60000, waker 0; ICC_CTLR 0x8440. vtimer 30/30, SGI1 10/10. INTID 28 sempre pendente (igual sweet).
- **Painel com defeito (tela trincada):** abaixo de ~90 % de brilho fica quase todo preto. Shim/XNU não mexem
  no brilho (o nível do ABL aparece inteiro). Se um dia controlarmos brilho (DCS 0x51), piso de ~90 %.
  TWRP: /sys/class/backlight/panel0-backlight (max 50000); os scripts põem 50000 ao conectar. O TWRP não
  guarda config (/data criptografada, não montada). Não editar a imagem do TWRP: é a rede de segurança.
- **M3** (`./build.sh m3`, sha256 3a655c7a...): sondagem UFS só leitura (TUR, READ CAPACITY, GPT, partições
  >= 64 MiB, superbloco de userdata/cache) com MMU desligada; depois MMU 16K (mmu.c do sweet: imagem WB,
  bloco 0x9C000000 NC, resto Device; o bloco seguinte tem TIMA e fica Device) + PAR + tempo de memória.

- **M3 CONCLUÍDO (2026-10-23):** UFSHCI 2.1 (cap 0x1587031F, 32 slots) ligado pelo ABL (hce 1, hcs 0x10F, lista
  do ABL em 0x9BAB3000); TUR/READ CAPACITY/READ ok, LUN0 lastlba 0x1BC73FF (4K, ~119 GB), GPT 36 entradas.
  boot lba 0x16E0B (64 MiB), recovery 0x1AE0B, system 0x1F30B (4435), vendor 0x13460B, odm, cache 0x17940B (ext4),
  omr, userdata **0x19340B, 115519 MiB** (superbloco ilegível = metadados criptografados). MMU 16K ok: SCTLR
  0x00C00800 -> 0x00C01805, TCR 0x1409CB51C (igual sweet), PAR img/fb/gic certos, 12G = falta L2. Memória 127x
  mais rápida com cache. O RKP não bloqueia a MMU da EL1. O ABL carregou o shim em 0x8015C000 (varia com o tamanho).
- **M4 (XNU) em preparo:** árvore local `../work/xnu` = xnu-8796.141.3 + 0001-0081 do sweet (0031/0041 não aplicam
  sem o HFS preparado: os scripts de stage usam xcrun / bearssl.org). Nosso patch
  `xnu-patches/0082-s9-galaxy-s9-bringup.patch`: console de emergência no splash do S9 (start.s, sweet_early.c com
  2184 linhas = só o bloco de 32 MB mapeado, arm_vm_init com offset 0x1400000) e touch/teclas/USB do Redmi
  DESLIGADOS por padrão (o touch vota RPMh ldoc11 = 3,2 V!). `tools/mkadt.py` S9: RAM 0x100000000 + 0x7e1c0000,
  FB 0x9d400000 1440x2960, A75 >= núcleo 4; 'qcom,kryo470' fica (KernelConfigTables.cpp casa o driver nele).
  Shim m4 = loader.c do sweet com boot_args.Video do fb.c. CI: fork de dipa-studio/ios-sweet (precisa de login).
  Boot-args do 1º boot: sem ufsrw/rootrw (UFS só leitura, sem log persistente), sweettouch=0 sweetkeys=0 sweetusb=0.

- **M4: XNU RODOU NO S9 (2026-10-23)**, com o kernel do CI do sweet + ADT do S9: IOKit, autotestes, UFS LUN0
  (userdata 115519 MiB read-only), e depois loop de "unknown Volume Signature c449" (userdata ainda da LineageOS).
- Fork **syncmodz/ios-s9** (CI ok em ~6 min). Kernel 1 = 0082; kernel 2 (run 39946975709, sha a93c6d7b) = 0082 + **0083**
  (canvas do iPhone X 1125x2436 centralizado no painel 1440x2960; sem isso o sweet cai no tamanho nativo).
- Ferramentas compiladas sem root em `toolchain/bin` (apfs-fuse com CMAKE_POLICY_VERSION_MINIMUM=3.5 e
  `-include cstdint`; hfs do glaubitz, branch linux, compilado fora do caminho com espaços; lzfse).
  O fsck.hfsplus não aceita arquivo comum: rodar via `losetup`. `research/ipsw-x/d22ap.adt` vem da IPSW (IM4P LZFSE).
- **Root v1:** `../work/iosroot/ios16-root-v1.img` (mkiosroot.sh do sweet como root + sweet-root-tweaks.py; 12 GB
  usados de 16), gravado na **userdata** com `tools/flash-userdata.sh` (gzip + adb shell -T, readback sha256 421fccb4).
  Boot `out/boot-ios1.img` (sha 54cf699e): kernel 2 + boot-args do sweet com cpus=4, ufsrw=1 rootrw=1, console
  visível, touch/teclas/USB desligados. Log persistente: `tools/sweet-log-pull.sh logs/X.txt` (TWRP).

- **🎉 TELA DE BLOQUEIO DO iOS 16.7.16 NO S9 (2026-10-23, boot-ios1):** logo da Apple, depois o SpringBoard com a
  tela de bloqueio (relógio em 31/dez porque não há RTC, bateria 100 % carregando do SweetBattery), centralizada
  no painel. Boot de ~15 min com cpus=4 (A55). Console por cima piscando (sem noconsole). Sem touch/botões.
- **0084:** volume+ do S9 = GPIO6 do PM8998 (SID 0, PPID 0x0c5, regs 0xc5xx); power/volume- = PON 0x810 como no
  Redmi; teclas ligadas por padrão. Com sweetfb_noconsole=1, limpa as bordas do painel no 1º quadro da GUI.
- **0085:** touch S6SY761 (sec_ts) no SE11 0xa8c000 (GPIO31/32 qup11, voto GCC 0x5200c bits 18-21 + 25, RCG
  0x183a8, CBCR 0x183a4). Sem reset/RPMh: S2DOS05 LDO2/LDO3 são `regulator_boot_on`. Sequência: CLEAR_EVENT_STACK
  0x62, SET_TOUCHFUNCTION 0x30 [0x61 0x00], SENSE_ON 0x10; poll a cada 10 ms com 0x60 (8 B) + 0x61 (restantes).
  Coordenadas 0..4095 -> painel 1440x2960 -> SweetHID (offset -157/-262, toque na borda gruda na beira).
  O touch fica desligado por padrão (0082): usar `sweettouch=1` (+ `sweettouchdelay=20`).
- **boot-ios2 (cpus=8):** boot em ~7 min, botões OK, console sem piscar. Touch: "se7 fw=0x301 proto=3 ... need
  ABL-loaded I2C firmware and 32-bit FIFOs": o firmware do SE11 tem FIFO desligado (GENI_IF_DISABLE_RO bit 0).
- **0086/0087:** canvas do iPhone X escalado até a altura, depois esticado até 1440x2960 por padrão
  (`sweetfb_aspect=1` mantém a proporção, `sweetfb_noscale=1` desliga). boot-ios3 = kernel 5 (run 39959555728).
- **Touch, testado no shim M5 (2026-10-23)**, ciclos de 1 min em vez de boots de 7 min:
  - **Energia:** o S6SY761 fica SEM energia depois do ABL. LDO2 (dvdd 1,8 V) e LDO3 (avdd 3,0 V) do S2DOS05 têm
    `regulator-boot-on`, mas quem liga é o regulator core do Linux. S2DOS05 em 0x60 num i2c-gpio (SDA GPIO127,
    SCL GPIO128). Lido: id=00 en=0x19 ldo2=0x8c ldo3=0x8c (tensões já certas, vsel 12). Escrevendo EN|2, 1 ms,
    EN|6 → en=0x1f, as linhas do touch sobem (GENI_IOS 0 → 3) e o IRQ (GPIO120) desce.
  - **GSI/GPI não dá sem votos:** a ordem certa é EV ALLOC → contexto → doorbell (eu tinha invertido; RP virava
    lixo 0x75170000). Corrigida, os RPs batem com as bases, mas o 1º DMA trava a CPU. A tbu2 (SID 0x6d6,
    GDSC 0x17d038) já está ligada (0x80000001); ler a SMMU (0x15000000) trava o barramento: faltam os votos de
    barramento do Linux (msm-bus apps_smmu/QUP). Abandonado.
  - **Bit-bang nos pinos do touch (GPIO31/32 como GPIO, dreno aberto por OE), como o i2c-gpio:** funcionou de
    primeira, boot status + id + 5 dedos ao mesmo tempo (M5.4).
- **0088:** o driver do touch no XNU virou isso: liga LDO2/LDO3 pelo S2DOS05, I²C bit-bang a ~150 kHz em
  GPIO31/32, lê eventos só com o IRQ (GPIO120) em 0, poll de 5 ms. Corrige também o 0085, que só mandava o
  toque inicial ao SweetHID (sects_event escrevia em st.x, então touch_report nunca via movimento).

- **🎉 boot-ios4 = TOUCH FUNCIONANDO NO iOS (2026-10-23):** kernel 6 (run 39991709646, 0082-0088) + boot-args abaixo,
  gravado com `tools/flash-twrp.sh` (readback ok, sha f382e672...). Vídeo do dono (37 s): desbloqueia arrastando,
  tela inicial, Ajustes > Acessibilidade > AssistiveTouch, Geral > Sobre (iOS 16.7.16, iPhone X, 17 GB / 3,96 GB
  livres), Siri pelo botão lateral. Volume+/- e power OK.
  Boot-args: `debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0
  unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 ufswait=1
  launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30 sweettouch=1 sweettouchdelay=20 sweetusb=0
  sweetsample=MSUEarlyBootTask,launchd,SpringBoard,backboardd`. Receita: payload com `tools/mkpayload.py`, depois
  `PAYLOAD=out/payload-ios4.bin ./build.sh m4` (o kernel do CI vai em `../work/s9-kernel-6`).
  **Problemas vistos:** interface lenta (render só na CPU, sem GPU) e apagar a tela pelo power demora muito.

- **M6 (fluidez, 2026-10-23)** `./build.sh m6` (sha 98539530...): nada no XNU/sweet ajusta o clock das CPUs
  (ficam no que o ABL deixou). M6 lê as tabelas do OSM v2 (L3 0x17d41000, prata 0x17d43000, ouro 0x17d45800;
  FREQ_REG 0x110+32*i, PSTATE 0x700, DESIRED 0x920+4*core), mede MHz (cadeia de adds x contador) na CPU0 e
  na CPU4, cópia 4 MB e escrita/leitura do framebuffer NC; depois sobe (L3 <= 1478, prata <= 1689 sem voto MX,
  ouro no máximo com 4 núcleos) e mede de novo. start.S: `adr` -> `adrp+add` (buffers de 8 MB na .bss).
  Gravado com readback ok; aguardando a foto.

- **M6 resultado:** ABL deixa o ouro em 825 MHz (linha 0) e o prata em 1612; com OSM: ouro 2706 MHz medido.
  Framebuffer write-combined: 200 MB/s escrita, 22 MB/s leitura (85 ms/quadro). **0089** (boot-ios5): clocks no
  SweetCPU (ouro 2649, prata 1689, L3 1478; `sweetcpu=0`, `sweetcpu_mhz=N`) + painel com cache e `dc cvac` por
  quadro (`sweetfb_wc=1` volta). Dono: "mais fluido do que eu esperava".
- **🎉 USB DO ZERO (shim M7.11, 2026-10-23):** o PC enumerou "Galaxy S9 iOS (USB Ethernet)". Receita (cada passo
  provado no M7.x, ver shim/m7.c):
  1. RPMh DRV2 (RSC 0x179e0000, TCS +0xd00, layout igual ao sweet): ldoa5 800 mV, ldoa12 1800, ldoa24 3075 (VRM:
     tensão, modo 7, enable). BCMs (cmd-db slave 5): SH3 SN11 SN0 SN6 CN0 SN12 — AMC de BCM pode levar >100 ms
     (timeout de 2 s; antes falhavam em cascata). No kernel o voto é 0x3fff (nunca rebaixa); MC0/SH0 intocados.
  2. **SMMU atrás do hypervisor**: ler sCR0 TRAVA; só ID0/ID1/SMR/S2CR/CBAR são emulados. Receita do U-Boot
     qcom-hyp-smmu.c: contexto livre como S1-translate/S2-bypass com tradução desligada + SMR 0x740 → S2CR TRANSLATE
     (BYPASS vira FAULT). Sem isso o 1º DMA (TRB) trava a CPU — era também o problema do GPI do touch.
  3. GCC: GDSC 0xf004; BCR 0xf000 com os clocks DESLIGADOS (com clocks ligados a 1ª leitura do DWC3 trava);
     RCG 0xf018 cfg 0x108 (133 MHz); CBCR master 0xf00c, sleep 0xf010, utmi 0xf014, cfg-noc 0x502c, aggre 0x8201c,
     ahb2phy 0x6a004 (offsets do sweet/SM7150 são outros).
  4. QUSB2: BCR 0x12000, ahb2phy 0x88ee010=0x11, init-seq do DT, PLL 0x1a0 bit0.
  5. **MAX77705**: o Linux da Samsung ABRE a chave D+/D− em todo shutdown (max77705_muic_shutdown → com_to_open),
     então depois do TWRP os fios ficam desligados. I²C bit-bang GPIO33 (SDA)/GPIO34 (SCL), slave 0x25: escrever
     [0x21, 0x06 (CONTROL1_WRITE), 0x09 (COM_USB)] e [0x41, 0x00].
  6. DWC3: QSCRATCH GENERAL_CFG PIPE_UTMI_CLK_SEL|PIPE3_PHYSTATUS_SW (sem PHY SS), GCTL device + DSBLCLKGTNG,
     GUSB3PIPECTL SUSPHY, GUCTL/GFLADJ p/ 19,2 MHz; VBUS por override no QSCRATCH.
- **0090** (CI 40054981141, kernel `../work/s9-kernel-8`): a receita acima no sweet_usb.c (`sweetusb=2`), replug
  (reset depois de configurado → reinicia o controlador; link cai quando o barramento suspende = cabo fora),
  política "AMFI" do dyld (__mac_syscall 90 → 0xff: DYLD_INSERT_LIBRARIES liberado) e `sysctl kern.sweet_next_boot`
  (1 = recovery: PON SOFT_RB_SPARE 0x88F bits 7:1 via canal SPMI 0xc600000+0x10000*APID + cookie IMEM).
- **Root v2** (`tools/prep-ios6.sh`, sudo): v1 + bootstrap Procursus rootful (palera1n bootstrap-1800) + jb/debs
  (ElleKit 1.2, Sileo 2.5.1, NewTerm 3, OpenSSH 9.7, network-cmds) instalados no 1º boot pelo job
  com.ios-s9.jbsetup (log /var/ios-s9/jbsetup.log), s9net (s9/ios-tools no fork, CI s9-ios-tools: publica
  IPv4/DNS do ue0 no configd), sshd inetd (22/2222, chave ~/.ssh/ios-s9 do PC p/ root), SpringBoard com
  DYLD_INSERT_LIBRARIES=/usr/lib/TweakInject.dylib (ElleKit estilo Taurine; /.installed_taurine pula o loader).
  Substitute não existe p/ iOS 16 e conflita com o ElleKit. Senha mobile/root: alpine.
  PC: perfil NetworkManager `s9-usb` (MAC 02:00:5e:77:ee:01, 192.168.7.1) + dispatcher NAT 90-ios-s9-share.
- **Controle remoto:** `tools/s9ssh.sh [cmd]`, `tools/s9-twrp.sh` (iOS → TWRP sem botões), `tools/s9-log.py ARQ`
  (log do kernel ao vivo, UDP 5140). boot-ios6 = kernel 8 + boot-args do ios4 + `sweetusb=2 cs_enforcement_disable=1`.

## Próximos passos (para a próxima conversa)

1. **Fluidez** (pedido do dono depois do vídeo). Não existe driver iOS pra Adreno: o render continua na CPU.
   Investigar nesta ordem, medindo antes e depois (`sweetsample` / fps no log):
   - se os 4 A75 (núcleos 4-7, 2,8 GHz) estão mesmo rodando o backboardd/SpringBoard, e o clock deles (o ABL
     pode deixá-los baixos; frequência via OSM/cpucp do SDM845, ver o kernel Lineage);
   - o caminho de cópia/escala do sweetfb (0086: estica 1125x2436 -> 1440x2960 na CPU a cada quadro; testar
     `sweetfb_noscale=1` / `sweetfb_aspect=1` e `sweetfb_hz`), NEON e cache na cópia;
   - reduzir efeitos (Ajustes > Acessibilidade > Reduzir movimento / transparência) como paliativo imediato.
   - **Tela apagando devagar:** provavelmente a animação de apagar renderizada na CPU e/ou o backlight/DPMS sem
     driver (o sweet faz o quê no power?). Ver no log o que o backboardd faz ao apertar o power.
2. **Internet por USB** (CDC-ECM, `sweetusb=2`), testando no shim primeiro (novo stage). Levantado:
   - DWC3 em 0xa600000, QSCRATCH 0x0a6f8800 (overrides de VBUS como no sweet_usb.c), GDSC usb30_prim 0x10f004,
     QUSB2 PHY v2 em 0x88e2000 (init-seq no DT `qcom,qusb-phy-init-seq`), SS PHY QMP 0x88e8000, ahb2phy 0x88ee000.
   - O ABL da Samsung NÃO deixa o USB ligado (o Redmi sim, por fastboot): ligar GDSC, clocks (conferir offsets
     do sweet = SM7150 no gcc-sdm845.c) e PHY do zero. PHY precisa de vdd/vdda18/vdda33 via RPMh
     (sweet_rpmh.c tem endereços do SM7150: conferir RSC/TCS no SDM845).
   - **Risco:** DMA do USB passa pela SMMU (SID 0x740, anoc_2_tbu), igual ao GPI que travou. O 1º teste no shim é
     exatamente o controlador escrever na RAM; o PC vê o dispositivo no `dmesg`. Se travar: votos BCM no RPMh.
3. Depois: Sileo/NewTerm/ElleKit, boot mais rápido (~7 min hoje), relógio (sem RTC), 4 GB de RAM.
   Wi-Fi (WCN3990 via modem, QMI, rmtfs sem escrever EFS) fica pra bem depois: semanas a meses.

## Objetivos do dono para depois do boot do iOS (2026-10-23)

Usar como um iPhone com jailbreak, publicado como fork no GitHub com créditos (sweet MIT, Apple APSL, sem binários
da Apple). Avaliação honesta de cada item:
- **Touch** (sec_ts, GENI I2C 0xa8c000, 0x48, IRQ GPIO 120): viável. Portar o protocolo de drivers/input/touchscreen/
  sec_ts como o sweet fez com o Goodix. Ver antes os reguladores do touch no DTB (nunca votar às cegas).
- **Wi-Fi** (WCN3990 do SDM845): muito difícil. Exige QMI + subir o firmware no modem/WCSS + driver tipo ath10k-snoc no
  XNU, e a interface Wi-Fi do iOS (IO80211) é fechada, então apareceria como rede cabeada. Alternativa que já existe
  no sweet: **internet pelo cabo USB** (CDC-ECM, sweetusb=2, o PC compartilha a conexão), suficiente pro Sileo.
- **Sileo + NewTerm + ElleKit** (bootstrap Procursus rootful, kernel já permissivo): viável, pré-instalado na imagem
  do root (sweet-root-tweaks). O Substitute é obsoleto; o ElleKit o substitui.
- **Fluidez**: sem GPU (não existe driver iOS para Adreno, render em CPU como no sweet). O maior ganho é usar os 4 A75
  a 2,8 GHz (o sweet só usa A55). Renderizar menor (ex.: 720p) e escalar 2x na CPU é possível, mas mexe na identidade/
  escala da interface. 20-30 fps é o alvo realista; 60 fps não, sem GPU.
- **4 GB de RAM**: o XNU quer um bloco contínuo. Os ~2 GB de cima são limpos; descer abaixo de 4 GB exige tirar as
  áreas protegidas da Qualcomm/Samsung do physmap (patch no kernel). Uns ~3 GB parecem possíveis depois.

## Fatos conferidos no boot.img/DTB stock (não no "achismo")

- boot.img: header **v0**, pagesize 4096, base 0, kernel_offset 0x8000, ramdisk_offset 0x2000000,
  tags_offset 0x1e00000, board `RILQI20A009`, os 10.0.0 / 2022-05.
- kernel = Image.gz + 15 DTBs colados (Rev02..Rev14, msm-id 0x141) + 1 blob RTIC (MP_DATA, descartado).
- Splash: `cont_splash_region@9d400000`, 0x2400000 (36 MiB). Confere.
- Painel S6E3HA8 (AMB577PX01): **modo comando**, TE pelo pino, **DSC** (2 encoders, lm-split 720+720),
  **um DSI só** (dsi-ctrl = só DSI0; topologia 2 LM / 2 DSC / 1 INTF). Não é dual-DSI.
  O sweet tem painel em modo *vídeo* ("no refresh kick needed"); aqui precisa de autorefresh ou kick.
- SDE (qcom,mdss_mdp@ae00000): CTL 0xae02000 + 0x200*n (5), SSPP vig 0xae05000/7000/9000/b000,
  dma 0xae25000/27000/29000/2b000, PP 0xae71000/71800/72000/72800/73000, INTF DSI0 0xae6b800.
- Touch: `sec,sec_ts` em I2C 0x48 no `i2c@a8c000` (QUPv3 SE11), IRQ no GPIO 120. Confere.
- IMEM 0x146bf000 (restart_reason @0x65c), PS_HOLD 0xc264000. Reboot reasons em include/linux/sec_debug.h.
- RAM reservada útil depois: `ramoops@a1300000` (1 MiB), `ss_plog@a1100000`.
- TIMA 0x9f800000 (2 MiB), RKP 0x9fa00000 (15 MiB). Confere.
- PIT: BOOT = 64 MiB, RECOVERY = ~69 MiB. Existe partição VBMETA, mas o firmware **não traz vbmeta.img**:
  o S9 não usa AVB 2.0, não há "vbmeta OFF" pra flashar.

## Instalação

- Heimdall: `heimdall flash --BOOT out/boot.img`. Odin: `out/boot.tar.md5` no slot AP.
- Loop rápido (TWRP): `tools/flash-twrp.sh`. Backup antes do 1º flash: `tools/backup-twrp.sh`.
- Nunca tocar: XBL, ABL, TZ, HYP, PIT, EFS, MODEMST*, FSG, FSC, PERSIST.

## O que o M1 faz (m1.c)

1. Pinta com o layout padrão de GOP (32 bpp, B,G,R,X) antes de mexer em qualquer registrador.
2. Alimenta o watchdog do APSS (se o ABL deixou ligado). Lê DTB (modelo, `msm_drm.dsi_display0=`, `lcd_id=`), EL, MIDR, suporte a página de 16K, versão PSCI.
3. Lê o SDE: qual CTL alimenta o DSI e em que modo, autorefresh nos PPs, qual SSPP lê a RAM (endereço, stride, formato).
4. Redesenha com o layout real, faixas R/G/B, diagnóstico; kick com CTL_FLUSH(bit 17) + CTL_START se não houver autorefresh.
5. Conta 60 s na tela (kick a cada segundo) e desliga por PSCI SYSTEM_OFF (fallback: reboot).

Leitura do resultado: tela mudou e o TEMPO anda = OK. Tela não mudou mas desligou em ~1 min = shim rodou, kick não funcionou.
Ficou preso no logo pra sempre = travou cedo (ex.: leitura de registrador) ou o ABL não aceitou a imagem.
Reinicia sozinho em ~10-15 s = watchdog (0x17980000, bark 11 s) ou o ABL recusou. Tela azul "PANICO" = exceção (foto!).

- **🎉 CAUSA RAIZ do "jailbreak quebrado" = falta de RAM, não trust/rede (2026-10-24):** Sileo não instalava
  ("restart Sileo"), NewTerm morria ("session finalized"), Safari não abria página. Diagnóstico: os crashes eram
  quase todos **JetsamEvent** (matador por pressão de memória), um a cada ~10 s. Com 2 GB e ~28 MB livres, o jetsam
  matava Sileo/NewTerm/WebContent ao nascer. `su mobile -c id` funciona (não é AMFI/trust). Fix: **remover 82 daemons
  inúteis** (sem cellular/conta/sensor/BT/Wi-Fi/GPU: vmd, locationd, cloud*, homed, health*, spotlight/search, siri*,
  identityservices, callservices, bluetoothd, fairplayd, etc.) do `/System/Library/xpc/launchd.plist`, protegendo a
  infra (SpringBoard, backboardd, mediaserverd, lsd, installd, container/usermanager, runningboard, ssh, trustd,
  amfid, nossos jobs). `tools/trim-daemons.py PLIST --apply` (ligado no `prep-ios6.sh`; aplicado ao vivo + reboot,
  backup em `launchd.plist.orig`). **Resultado: 28 MB → 163 MB livres, 533→451 daemons, processo de 15 s sobrevive,
  jetsam parou.** A internet dos apps já vinha OK com o s9net v3 (reachability=1; publicar o serviço SEM forçar a
  primária — forçar derrubava o SpringBoard por watchdog). Falta o dono confirmar Sileo/NewTerm/Safari na tela.
- **RAM: alvo 3,2 GB** (teste shim M8 pronto, `./build.sh m8`). Hoje o iOS vê só 2 GB (bloco contíguo de cima,
  0x100000000+0x7e1c0000). Os 4 GB físicos (0x80000000+0xfe1c0000) têm a metade de baixo esburacada de carveouts
  (modem, hyp, IPA 320M+85M, SPSS, fb, debug). Dois buracos livres: R1 [0xa1800000,0xc0000000) 487M e
  R2 [0xd4000000,0xff000000) 688M. 2017+688+487 = ~3,2 GB, mas exige XNU gerenciar um intervalão com 2 carveouts
  reservados (0xc0000000-0xd4000000, 0xff000000-0x100000000). M8 confirma na mão que R1/R2 são RAM livre antes de arriscar.
