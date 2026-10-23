# iOS no Galaxy S9 (SM-G9600, starqlte, SDM845): notas de trabalho

Referência: ios-sweet (`../ref/sweet/ios-sweet-main`, MIT): XNU + userland do iPhone X 16.7.16 no Redmi Note 10 Pro (SM7150).
Kernel Samsung/LineageOS 20 do S9: `../ref/kernel/android_kernel_samsung_sdm845-lineage-20`.
Firmware stock: `../ZTO-G9600ZHU9FVF2` (G9600ZHU9FVF2, Android 10).

## Estado (2026-10-22)

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
