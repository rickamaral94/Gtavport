# Fase 1 — instalar e compilar o jogo no próprio aparelho (sem PC)

O app abre direto na tela **instalar e compilar neste aparelho** (como os ports de BOTW/TOTK) → escolher Disc 1, Disc 2 e TU27 → **Instalar e compilar** → **Jogar**. Usar um pack feito no PC fica como opção avançada no fim da tela.

## O que roda no aparelho
| Etapa | Código | Ferramenta (nativeLibraryDir) |
|---|---|---|
| Copiar e conferir os dados (SHA-256 de cada arquivo) | `install.cpp` (upstream), modo `RunDevice` (patch 0010) | — |
| Recompilar PowerPC → C++ (811 arquivos) | codegen do ReXGlue | `librexglue.so` |
| Conferir cada `.cpp` contra a referência (`expected.txt`) | `CheckGenerated` (upstream) | — |
| Compilar (jobs pela RAM livre, flags do upstream) | `Compile` (upstream) + `-resource-dir` | `libclang.so` |
| Linkar `libgtav_game.so` contra as libs do próprio APK | `LinkAndroid` (upstream) | `libld.lld.so` |
| Shaders, durante o jogo: XenosRecomp → ubo → DXC → int64 | `shader_compiler.cpp` (patch 0011) | `libxenosrecomp.so`, `libshader_fix.so`, `libdxc.so` |

O resultado (`files/libgtav_game.so`, `files/pack/{expected,pack}.txt`) fica onde a tela de setup põe um pack
importado do PC: o resto do app não muda. A build roda num serviço em primeiro plano (`dataSync`) com wake lock,
progresso na notificação e retomada (dados por `install_state.txt`, codegen pulado se a saída já confere,
objetos já compilados mantidos). A pasta de build fica no armazenamento interno (mais rápido que o externo) e é
apagada no fim.

## Peças
- `android-ondevice/device/make_device_bundle.sh`: libs (`build/device/libs`) e o asset `gtavport-toolroot.zip`
  (manifest e partição do codegen, `expected.txt`, headers freestanding, interface de hooks, simde, headers do
  clang, `shader_common.h` com as trocas do Adreno). Nada do jogo; pureza verificada.
- `odin2/android/.../Odin2DeviceBuild.java` (extrai o toolroot, chama o nativo, configura os shaders do jogo),
  `Odin2BuildService.java` (serviço), `Odin2DeviceBuildActivity.java` (tela).
- Patches: 0010 (instalador + processos no Android via fork/exec), 0011 (shaders no aparelho), 0012 (botão,
  manifesto, assets no build_apk.sh).

## Riscos a medir no Odin 2
1. SELinux deixar o app executar as ferramentas de `nativeLibraryDir` (o APK de prova testa: P1).
2. Codegen no Android gerar exatamente os mesmos 811 `.cpp` que no PC (a conferência pega qualquer diferença).
3. Tempo total e RAM por job do clang (o upstream anota até ~1 GB na maior unidade; P3 mede).
4. Shaders: tempo da tradução no aparelho nos primeiros minutos (P5).
