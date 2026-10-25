# Estudo da aceleração gráfica (Adreno 630) — sessão 2026-10-25

Regras: os projetos de terceiros são dados (só leitura, nada rodado de dentro deles). Legenda: **[C]** confirmado lendo
código/documento/desmontagem, **[S]** suspeita, **[H]** medido no hardware de outra pessoa (não no nosso).

## 0. Referências baixadas (commits exatos)

| projeto | onde | commit | licença | o que é |
|---|---|---|---|---|
| Bergamota (`develop`) | `ref/Bergamota` | `1965b5f98d1bea29972164acbc4e583ec3a543f6` | **nenhuma** (só ler; não copiar código) | iOS 18 no Poco F1 (SDM845/A630); fork do ios-sweet; teve fase iOS 16 em xnu-8796 |
| Bergamota (`gpu-eagl`) | `ref/Bergamota-gpu-eagl` (worktree) | `814b9a82742d48e9b60c88800f81233f3c6558fd` | nenhuma | shim EAGL (GLES) + Mesa + launcher do backboardd |
| Chlorophyll (`main`) | `ref/Chlorophyll` | `787338fc7c297f54e2000bca02f4fa36f1154c5a` | **MIT** | driver Metal do Bergamota (AIR→NIR→ir3 sobre freedreno), iOS 18 |
| HackiOS userland (`main`) | `ref/hackios-xnu-userland` | `a79ff0772a3454fd1ef127e181e5b3bce6c9ce18` (`metal`: `29eef72e…`) | **MIT** | iOS **16.5** no POCO X5 Pro (A642L): eagl.c, inject.c, patches do Mesa, Metal em andamento |
| HackiOS kernel (`main`) | `ref/hackios-xnu-kernel` | `d82c64a3a31cd27a0a0afbf6e213e467e44b1b4c` (`metal`: `c2ddff4e…`, `without-identity`: `e3936d82…`) | **APSL 2.0** (igual ao XNU) | xnu-**8796**.121.2 + drivers; GPU por Mesa/freedreno funcionando no aparelho deles |

O que a licença permite: código do HackiOS kernel (APSL) pode entrar nos nossos patches do XNU (também APSL), com o aviso;
HackiOS userland e Chlorophyll (MIT) podem entrar no fork com o aviso de copyright. Do Bergamota só fatos (endereços,
sequências, valores medidos), nunca código: os próprios autores reescreveram a partir do Linux (GPL, só fatos).

## 1. O que cada projeto já provou no hardware

**Bergamota, fase iOS 16 / xnu-8796 (Poco F1 = mesmo SDM845 e mesmo Adreno 630)** [H]:
- 2026-10-20 `sweetgpu=2`: CX GDSC ligado, GMU a 200 MHz, ITCM/DTCM lidos e escritos, SMMU da GPU legível (5 SMR/5 CB).
- `sweetgpu=3`: `a630_gmu.bin` do linux-firmware carregado, GMU respondeu `0xbabeface` em 58 µs, sem falha de SMMU.
  `gfx.lvl` do cmd-db = 0x30050 (a constante do Linux é outra).
- `sweetgpu=4`: HFI de pé (GMU fw 1.6), PERF_TBL / BW_VOTE_TBL / TEST aceitos.
- `sweetgpu=5` (boot 0118g5): **GX ligado** pelo OOB do GMU, primeiras leituras do núcleo da GPU (RBBM_STATUS 0x1,
  CP_ALWAYS_ON contando), slumber de volta.

**Bergamota, xnu-11215 / iOS 18** [H]: etapa 8 passou (zap aceito pelo TrustZone, CP executou NOP + MEM_WRITE pela SMMU,
GPU mantida ligada); falhas achadas no hardware e corrigidas: o `a630_sqe.fw` do linux-firmware tem 34.188 bytes
(janela de 0x8000 era pequena); páginas da GPU são de 4 KB e as do XNU de 16 KB (um PTE por 4 KB); o `CP_ALWAYS_ON` lido
pela CPU parecia congelado (usar o contador always-on do GMU). Depois: shim EAGL (GLES) dentro do backboardd por um
gancho na vtable → "blur works" (2026-10-23); depois Chlorophyll (Metal): rolagem da tela inicial **~30 → ~41 fps**
a @3x (1125×2436), com o backboardd a 93-98 % de um núcleo, ~2/3 disso trabalho do próprio CoreAnimation.

**HackiOS (iOS 16.5 / xnu-8796, Adreno 642L)** [H]: "Adreno 642L GPU through Mesa/freedreno (`gpu=1`, OpenGL ES for
CoreAnimation)" na lista do que funciona; Metal para apps em andamento (293 de 456 shaders do sistema rodam).

## 2. Como o iOS 16 escolhe o renderizador (o ponto que decide o caminho)

- HackiOS (`userland/src/inject.c`, iOS 16.5 20F66) [C, para 16.5]: `CA::WindowServer::EAGLServer::renderer()` chama
  `CAMetalContextCreate()` (= `MTLCreateSystemDefaultDevice`); **sem dispositivo Metal ele cai sozinho em
  `[EAGLContext initWithAPI:3]` + `CA::OGL::GLESContext`**. Eles só põem a própria implementação de `EAGLContext`
  (Mesa) por trás. Sem gancho de vtable para criar o contexto (o Bergamota precisou no iOS 18, que não tem mais o GLES
  no servidor).
- Bergamota `GPU_PHASE0_DISCOVERY.md` (iOS 16.7.16 20H392 = o nosso) [C pela desmontagem deles]: `EAGLServer::renderer`
  tenta Metal e depois GLES **só se `CA::WindowServer::enable_ogl`** (variável `CA_ENABLE_OGL`, lida no construtor
  `EAGLServer::EAGLServer`). **A conferir na nossa desmontagem** (`work/dsc`): se for assim, o S9CAEnv já põe a variável.
- O GLES do sistema no iOS 16 é feito sobre Metal (AppleMetalGLRenderer): sem Metal ele não serve; por isso o EAGL tem
  de ser nosso (Mesa por trás).
- **[C] Na nossa desmontagem (QuartzCore 20H392):** `EAGLServer::renderer()` (0x1882eaf3c) tenta
  `CAMetalContextCreate()`; se vier nil, testa `CA::WindowServer::enable_ogl` e cria
  `[[EAGLContext alloc] initWithAPI:3 properties:{Accelerated, ClientRetainRelease}]` (cai para API 2), depois
  `CA::OGL::GLESContext` (0x870 bytes) + `CA::OGL::Renderer`. O construtor `EAGLServer::EAGLServer` (0x1883da11c,
  +0x728..0x874) faz `enable_ogl = getenv("CA_ENABLE_OGL") ? atoi(...) != 0 : 1`: **ligado por padrão**. Conclusão: no
  iOS 16 basta o backboardd ter um `EAGLContext` que funcione (o nosso, com Mesa) — sem gancho de vtable.
- Efeito colateral no caminho por CPU de hoje: sem renderizador, o servidor tenta Metal e EAGL de novo a cada quadro
  (o HackiOS cita o custo). `CA_ENABLE_OGL=0` no `env.txt` do S9CAEnv corta a tentativa de EAGL [S: ganho pequeno, medir].

## 3. Hardware do S9 (device tree ao vivo + kernel Android do próprio S9) [C]

Igual ao Poco F1 (mesmo SoC): GPU `0x5000000` (chipid `0x06030001` = A630 v2), GMU `0x506a000` (+0x30000), PDC
`0xb200000` (cfg em +0x80000, sequência em +0x280000), gpucc `0x5090000` (GDSC CX `0x509106c`, GX `0x509100c`, este do
GMU), SMMU da GPU `0x5040000` "qcom,smmu-v2" **sem** `qcom,skip-init` (é nossa, não do hipervisor; a dos apps é que é
emulada), SIDs 0 (3D), 1/2 (seguro), 4 (gmu_user), 5 (gmu_kernel), `attach-impl-defs` (12 pares) no DT. Zap:
`qcom,kgsl-hyp` PAS 13, região `0x8c415000` +0x2000 (fora da RAM do nosso XNU, que começa em 0xa6000000). cmd-db em
`0x861e0000`. Frequências 257/342/414/520/596/675/710 MHz (nível inicial 6 = 257). GCC: `cfg_ahb 0x71004`,
`memnoc_gfx 0x7100c`, `snoc_dvm 0x71018`, `ddrss_gpu_axi 0x44038`, votos GPLL0 `0x52004` bits 15/16 (iguais ao F1).

**Firmware:** o zap da Samsung está no `NON-HLOS.bin` do BL oficial: `image/a630_zap.mdt` (6.684 B = cabeçalho ELF
+ hashes), `.b00` 148, `.b01` 6.536 (hashes/assinatura), `.b02` 1.968 (o código, PT_LOAD relocável em 0x5000).
Extraído para `s9port/research/gpu-fw/` (fora do git). GMU e SQE: linux-firmware (`a630_gmu.bin` 32 KB, o kernel do
S9 espera versão 1.3+; `a630_sqe.fw`).

**Sequência do driver do próprio S9 (kgsl msm-4.9), só fatos:** CX GDSC + relógios → micro-código do RSC/PDC (o do
S9 usa endereços fixos 0x30010/0x30000/0x30080 nos TCS do PDC; o Bergamota usou o `gfx.lvl` do cmd-db, 0x30050 no F1:
os dois ligaram a GPU, então fica como chave ao vivo) → firmware do GMU pelo AHB direto no ITCM (até 32 KB) →
BOOT_CONFIG, tabela HFI, faixa do "fence" AHB, chip id → SYSRESET 0 → `0xbabeface` → **voto da GX por registrador**
(`BOOT_SLUMBER_OPTION` + `GX_VOTE_IDX`/`MX_VOTE_IDX` + OOB bit 22, ack bit 30; a peculiaridade "hfi-use-reg" do DT) →
SPTPRAC → `HFI_CTRL_INIT` → mensagens HFI v1 (INIT, FW_VER, PERF_TBL, BW_TBL, TEST) → OOB GPU (bit 16, ack 24) →
init do CP (UCHE, VBIF, endereçamento 64 bits, proteção, SQE, anel) → zap pelo TrustZone (PAS 13; o Linux do S9
vota antes a banda do motor de cripto) → `CP_ME_INIT` → sai do modo seguro. Chamada SMC: `0x42000000 | svc<<8 | cmd`,
repetindo enquanto x0 == 1 (interrompida) com x6 de volta.

## 4. Decisões (com o porquê)

1. **Bring-up no kernel do iOS com etapas ao vivo por `sysctl`, não no shim.** Hoje o iOS dá SSH em 37 s e o log do
   kernel chega ao PC pelo cabo: cada etapa diz o que vai tocar antes de tocar, e a última linha mostra onde parou. O
   shim só mostra foto e exigiria regravar a partição a cada teste. Rede de segurança: o watchdog do APSS armado
   durante as etapas (se o aparelho congelar inteiro, ele reinicia sozinho e volta ao iOS).
2. **Código:** nosso, escrito a partir dos fatos acima; do HackiOS (APSL/MIT) aproveitamos a camada IOKit/usuário e o
   lado do iOS (Mesa, libdrm, EAGL), com crédito; do Bergamota nada além de fatos.
3. **Caminho do CoreAnimation: GLES primeiro** (nativo no iOS 16, item 2); Metal (Chlorophyll) depois.

## 5. Plano por etapas (confirmado pelos dois projetos + o kernel do próprio S9)

O caminho crítico é ligar a GPU; o resto (GLES/Metal) é software que já existe e que os dois projetos provaram
rodar no mesmo chip. Ordem, cada etapa provada antes da seguinte:

| # | o que | onde | risco | resultado esperado |
|---|---|---|---|---|
| **A** | **GMU: energia + relógios + firmware do GMU → `0xBABEFACE`** | shim M10 (feito) | baixo: sem GX/zap/CP; hang = watchdog | o processador de energia da GPU roda |
| B | SMMU da GPU (contexto, tabelas), HFI, **GX ligado** pelo GMU, SPTPRAC, slumber | shim M11 | médio: 1ª vez que a GX liga | `RBBM_STATUS` legível, GX liga e desliga |
| C | zap pelo TrustZone (PAS 13, o `a630_zap` **da Samsung** já extraído), CP (SQE, anel, `CP_ME_INIT`), sai do modo seguro, NOP+MEM_WRITE | shim M12 | médio: TrustZone/modo seguro | CP executa um comando e escreve na RAM |
| D | o mesmo no XNU: driver `SweetAdreno` com os ioctls do msm (como o HackiOS), GPU mantida ligada, IOService p/ o Mesa | kernel | — | `ioreg` mostra o serviço; teste por SSH roda |
| E | OpenGL ES: Mesa freedreno (a630) + libdrm de mentira sobre o nosso ioctl + EAGL nosso no backboardd | iOS | baixo (iOS 16 usa GLES nativo) | CoreAnimation desenha pela GPU, blur inclusive |
| F | medir com `tools/s9-measure.sh`; Metal (Chlorophyll) se valer a pena | iOS | — | 60 fps é plausível (HackiOS/Bergamota mostram UI fluida) |

**Etapas A-C no shim** (não no iOS): hardware arriscado primeiro, boot de 10 s, watchdog, resultado na foto —
exatamente a regra do projeto. D-F no iOS, onde o GLES do iOS 16 nos poupa o gancho de vtable que o Bergamota
precisou no iOS 18.

## 6. O que foi feito nesta sessão (sem aparelho)

- Estudo acima (os dois projetos + HackiOS + o kernel Android do S9), com os registradores conferidos contra o DT.
- **Zap da Samsung extraído** do `BL/NON-HLOS.bin` → `s9port/research/gpu-fw/a630_zap.{mdt,b00,b01,b02}` (fora do git):
  o maior risco dos dois projetos, resolvido com o arquivo assinado do próprio aparelho.
- `a630_gmu.bin` (32768 B, sha `da8d9b1b…`) e `a630_sqe.fw` (34188 B, sha `1c21b527…`) do linux-firmware em
  `research/gpu-fw/` (fora do git; redistribuíveis mas não comitados).
- **Etapa M10 do shim escrita e compilada** (`s9port/shim/m10.c`, ~460 linhas; `build.sh m10`;
  `tools/gpu-fw-embed.py` embute o `a630_gmu.bin` no cabeçalho git-ignored `shim/fw_a630_gmu.h`). Imagem pronta:
  `out/boot-m10.img` (sha `f66a548e…`, com o firmware embutido). Compila sem avisos, 0 relocações absolutas.
  Etapa 1 (leitura), 2 (liga CX + relógios do GMU a 200 MHz + testa a memória do GMU), 3 (firmware do GMU → `0xBABEFACE`).

## 7. Próxima ida ao aparelho (uma gravação, uma foto)

1. Celular no TWRP no cabo. `tools/flash-twrp.sh out/boot-m10.img` (dd + leitura de volta; o shim M10 vai para a
   partição boot). **Isto substitui o `boot-ios17` (0101) na boot**; para voltar ao iOS depois, gravar `out/boot-ios17.img`.
2. O shim mostra na tela as etapas 1→3 e para na primeira que falhar; fica 60 s e reinicia. **Tirar uma foto da tela.**
   O que esperar na foto: "gmu fw_init_result 0xBABEFACE" e "O FIRMWARE DO GMU RODA" = fundação pronta → escrevo a M11
   (SMMU/GX). Se parar antes, a última linha "PASSO" diz onde (ex.: "CX GDSC não liga", "ITCM não guarda o firmware").
3. Depois da foto: `tools/flash-twrp.sh out/boot-ios17.img` volta ao iOS.
