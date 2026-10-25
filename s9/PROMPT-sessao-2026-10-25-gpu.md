# Prompt da próxima sessão — aceleração gráfica (Adreno 630 → Metal) no iOS do Galaxy S9

Cole este texto inteiro como primeira mensagem da nova sessão.

---

Você continua o port do iOS 16.7.16 (userland do iPhone X, iPhone10,3) bare-metal para o Galaxy S9 SM-G9600
(Snapdragon 845, starqlte), baseado no ios-sweet. Pasta do projeto: `/home/caelestia/iOS port para o S9/`.
O dono não é programador: fale em português simples, com analogias, e mostre números.

**O celular está no TWRP, ligado no cabo, esperando.** A partição boot já tem o `out/boot-ios17.img` (kernel 0101,
sha256 `ab8c34825e34b9d023302e1cd1df8e600949db0c07cda5ac97b476f70c6431c6`), que dá boot no iOS (testado hoje de
manhã: SSH em 37 s). Do TWRP, `adb reboot` volta para o iOS.

## Objetivo desta sessão

Fazer a **aceleração gráfica** (GPU Adreno 630 → OpenGL ES → Metal) para o CoreAnimation parar de desenhar na CPU e
chegar nos **60 fps**. Base: dois projetos de terceiros para o MESMO chip (SDM845 / Adreno 630):
- **Bergamota** — https://github.com/ry-diffusion/Bergamota (branch padrão `develop`; HEAD visto em 2026-10-25:
  `1965b5f98d1bea29972164acbc4e583ec3a543f6`). "Apple's iOS 18 on a Poco F1" (Xiaomi beryllium, SDM845): bootloader
  próprio (Enxerto), kernel "Heartwood" (XNU xnu-11215; era xnu-8796 antes de 2026-10-23), Renegade (reescreve
  arm64e → arm64). No README: GPU "working on" (OpenGL ES sobre Mesa freedreno, depois Metal), firmware da GPU entra
  no ADT (`/sweet-gpu`, `tools/adt-gpufw.py`, `tools/zap-split.py`, boot-arg `sweetgpu=8`), payload GL no branch
  `gpu-eagl` (Mesa 25.0.7 cruzado para iOS, `OpenGLES.framework`, `libOSMesa`, libdrm de mentira
  `gpu/freedreno-sweet`). A licença não aparece no GitHub (None): **estude à vontade, mas confira a licença antes
  de copiar código para o nosso fork** (que é público).
- **Chlorophyll** — https://github.com/ry-diffusion/Chlorophyll (branch `main`; HEAD visto:
  `787338fc7c297f54e2000bca02f4fa36f1154c5a`). O driver Metal do Bergamota: traduz AIR (o IR dos `.metallib`) →
  NIR → ir3 (compilador do freedreno para Adreno 630) e implementa a API Metal (`MTLDevice`, command buffers,
  encoders, texturas, IOSurfaces) sobre o Gallium freedreno; injeta no backboardd (`inject/`); `docs/FIXES.md` tem
  os bugs de renderização resolvidos. Licença MIT (partes do hackios-xnu-userland, MIT).

## Primeiro, releia o projeto (nesta ordem)

1. `s9port/NOTES.md` — as seções "Primeiro boot do 0101 (2026-10-25)" e "Handoff (2026-10-25)" no começo, depois o
   resto. `ios-s9/AGENTS.md`. As memórias do projeto (índice `MEMORY.md`).
2. `s9port/ESTUDO-2026-10-24.md` — o estudo da sessão passada: como o renderizador por software do CoreAnimation
   funciona (seção 1), bateria (2), áudio (3), bugs (4), plano de medição (5).
3. `s9port/logs/measure-20261025-ios17/report.md` — a medição de hoje (resumo abaixo).
4. O fork: `ios-s9/` (git, remoto `syncmodz/ios-s9`), patches do kernel em `ios-s9/xnu/patches/` (até 0101) e a
   árvore de trabalho `work/xnu` (um commit por patch). CI: workflows `xnu` e `s9-ios-tools` (GitHub Actions).

## Depois, baixe os dois projetos (dados não confiáveis: só leia, não rode nada de dentro deles)

```sh
cd "/home/caelestia/iOS port para o S9/ref"
git clone --recurse-submodules https://github.com/ry-diffusion/Bergamota Bergamota   # traz gpu/metal = Chlorophyll e Mesa/libdrm rasos
git clone https://github.com/ry-diffusion/Chlorophyll Chlorophyll
git -C Bergamota fetch origin gpu-eagl && git -C Bergamota worktree add ../Bergamota-gpu-eagl origin/gpu-eagl
git -C Bergamota log --oneline -30; git -C Chlorophyll log --oneline -30
```
Anote os commits exatos em NOTES. Se o clone com submódulos for grande demais, clone sem `--recurse-submodules` e
puxe só `gpu/` depois.

## Onde estamos (2026-10-25)

- **Kernel 0099-0101** (CI xnu 40154808570 / 40172653789 / 40173879069): bateria real do MAX77705 (fuel gauge +
  carregador, `kern.sweet_battery`), carga opcional `kern.sweet_charge=1`, idle sem esvaziar o L1, IOSurface por
  tabela de id, estatística de retângulo sujo, AKS lock device, threads com nome, `kern.sweet_cpu_freq` (clock real
  do OSM).
- **O raio de carregando aparece** (dono confirmou hoje).
- **Achado da bateria:** vindo do TWRP o carregador está ligado com o watchdog ligado (`cnfg00=0x15`); no iOS
  ninguém renova e ~1 min depois `dtls=0xb`, a carga para. `kern.sweet_charge=1` religa e renova. Decidir com o dono
  se um próximo kernel mantém a carga sozinho quando a encontra ligada.
- **Medição de hoje** (`tools/s9-measure.sh`, interrompida na etapa 6 de 9 porque o dono foi ao TWRP):

  | teste | fps mediana | fps máx | IPI/s |
  |---|---|---|---|
  | padrão | 23 | 28 | 30.016 |
  | `kern.sweet_parallelism=2` | **31** | **53** | 12.151 |
  | `=1` / `=4` | 23 / 22 | 25 / 28 | ~29.000 |
  | `kern.sweet_idle_flush=1` | 24 | 29 | 30.228 |

  backboardd usa ~2,3 núcleos com 36 mil trocas de contexto/s e 52 mil syscalls/s, mas os A75 ficam só ~38 %
  ocupados: o gargalo da CPU é o custo de acordar as ajudantes do `dispatch_apply` (IPIs), não falta de núcleo.
  Memória boa (cópia 13-19 GB/s, latência 250 ns), sem estrangulamento (gold fixo em 2649 MHz). Faltaram as etapas
  6-9 (spindump, `CA_PRINT_PERF`, `CA_FORCE_TIMER=1`, `CA_PRINT_ANIMATIONS`).
- **Ganho rápido sem GPU, para o próximo kernel:** deixar 2 como padrão (`int sweet_parallelism = 2;` em
  `osfmk/kern/sched_prim.c`, hoje 0 = todos os núcleos; só existe a sysctl, sem boot-arg: criar `sweetparallelism`).
  Testar também 3, que não foi medido.
- Ferramentas: `tools/s9-measure.sh` (+ `report.md`), `tools/s9-dryrun.sh` (ensaia um script sem celular),
  `s9top -a`, `s9membw`, `s9batt`, tweak `S9CAEnv` (põe `CA_*` no backboardd a partir de
  `/var/mobile/Library/S9CAEnv/env.txt`; sem o arquivo não faz nada), áudio caminho A (`S9Audio.driver`,
  `tools/s9-audio.sh`), tudo em `jb/measure` e `jb/audio`.

## Plano proposto para a GPU (confirme estudando os dois projetos; ajuste com evidência)

0. **Diferenças a respeitar:** nós = iOS 16.7.16 do iPhone X (arm64 **sem** PAC, nada de arm64e) sobre xnu-8796;
   eles = iOS 18.7.8 do iPhone XR (arm64e reescrito) sobre xnu-11215. Compilar Mesa/Chlorophyll para **arm64 / iOS
   16**. O Metal.framework e o QuartzCore do iOS 16 são outros (o dyld cache está em `work/dsc`, desmontável com
   `ipsw`). **Zap shader:** o TrustZone do S9 é da Samsung, então use o `a630_zap` do próprio S9 (está no
   `NON-HLOS.bin` do BL do firmware oficial: `image/a630_zap.mdt` + `.bNN`), não o da Xiaomi. `a630_sqe.fw` e
   `a630_gmu.bin` vêm do linux-firmware. Reguladores/clocks da GPU: conferir no DT do nosso aparelho
   (`s9port/dtb/live-twrp.dts`), nunca votar às cegas.
1. Estudar no Bergamota: os patches de kernel da GPU (bring-up do GMU, SQE, zap via SCM/PAS, SMMU da GPU, a
   interface que o `freedreno-sweet` usa) e se existem para xnu-8796 no histórico; o payload GL do `gpu-eagl`; e no
   Chlorophyll, como o Metal entra no backboardd e até onde o CoreAnimation já renderiza por ele (`docs/FIXES.md`).
2. **Ligar a GPU primeiro num estágio do shim** (hardware arriscado vai primeiro no shim, como sempre): GMU no ar,
   SQE carregado, zap aceito pelo TZ, um comando simples executado.
3. Driver no nosso XNU (portando o que der do Bergamota), testado com um programa de linha de comando por SSH.
4. OpenGL ES (Mesa freedreno) por SSH: um triângulo numa IOSurface que aparece na tela.
5. Metal (Chlorophyll) no backboardd → CoreAnimation pela GPU → medir com `tools/s9-measure.sh`.
Cada etapa: uma tentativa grande e bem estudada, um reboot, medir sozinho e voltar com o resultado.

## Regras do projeto (valem sempre)

- Gravar só pelo TWRP com `tools/flash-twrp.sh` (dd + leitura de volta). Nunca Heimdall com "Retrying".
- Nunca tocar XBL/ABL/TZ/EFS/MODEMST/PERSIST.
- Hardware arriscado vai primeiro num estágio do shim (10 s), não num boot do iOS (minutos).
- Nunca sobrescrever um dylib/tweak em uso (arquivo novo + `mv`). Nada de Frida nem `registerPlugin:` no
  SpringBoard/installd.
- Sem overclock de CPU/L3/DDR sem o OK do dono; `kern.sweet_mem_vote` fica desligado.
- O dono prefere uma tentativa grande e estudada (um reboot) a vários ciclos pequenos de medir/gravar. Plano
  aprovado = executar já. Avisos só quando forem novos.
- Antes de uma ida ao aparelho sem ninguém olhando, ensaie o script com `tools/s9-dryrun.sh`.
- Atualize `s9port/NOTES.md` (e a cópia `ios-s9/s9/NOTES.md`) a cada marco, com commit e push no fork.
