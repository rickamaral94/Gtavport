# odin2/driver — drivers Vulkan: sistema ou Turnip

## Para o jogador: tela "Driver gráfico"
Abre pelo botão **Graphics driver (Turnip / system)…** na tela de setup ou pelo atalho **Driver gráfico**
(toque longo no ícone do app; o APK pula o setup quando o jogo já está instalado).

- **Importar pacote (.zip)**: pacotes Turnip no formato adrenotools (`meta.json` com `libraryName` + o `.so`),
  como os distribuídos para emuladores Android. Recusado se faltar `meta.json`/`libraryName`, se o `.so` não for
  ELF ARM64 ou se o `minApi` for maior que o Android do aparelho. Instalado em `files/drivers/<nome>-<versão>/`
  (armazenamento interno; o externo não permite `dlopen`).
- **Selecionar**: vale no próximo início do jogo. A escolha fica salva no app.
- **Testar**: carrega o driver num **processo separado** (`:drivertest`) e mostra GPU, versão Vulkan,
  `driverName`/`driverInfo` e recursos que o renderer do upstream contorna no driver proprietário
  (`shaderInt64`, `geometryShader`, `VK_EXT_graphics_pipeline_library`). Se o driver derrubar o processo de teste,
  a tela informa e o jogo não é afetado.
- **Variáveis**: `NOME=VALOR` aplicadas só com aquele driver (ex.: `TU_DEBUG=sysmem` / `TU_DEBUG=gmem` para a
  Fase 3). `LD_*` e `ODIN2_*` são ignoradas.
- **Status**: "última execução: …" lê `files/logs/driver_status.txt`, escrito pelo lado nativo com o driver
  pedido, o efetivo e o motivo de uma eventual volta ao driver do sistema.
- Prioridade: `env:ODIN2_VK_DRIVER` no `args.txt` (benchmark) > escolha da tela > sistema. A tela avisa quando o
  `args.txt` está sobrepondo a escolha.

Nenhum driver vem dentro do APK: o usuário importa o pacote que quiser (Turnip do Mr. Purple, do
Amaral-Adreno-Tools etc.). Bugs de driver vão para o repositório do driver, não viram contorno no jogo.

## Para benchmark (Etapa 0.5)

## Peças
| Peça | O que faz |
|---|---|
| `libodin2driver.so` (`odin2_driver.cpp`) | abre o driver escolhido com `adrenotools_open_libvulkan` **uma vez por processo** |
| hooks do libadrenotools (`libmain_hook.so`, `libhook_impl.so`, `libfile_redirect_hook.so`, `libgsl_alloc_hook.so`) | precisam estar em `nativeLibraryDir` (o manifesto do upstream já tem `extractNativeLibs="true"`) |
| patch 0001 (rexglue-sdk) / 0002 (plume) | runtime e renderer pedem o handle via `dlopen("libodin2driver.so")` e usam o **mesmo** driver; sem a lib, comportamento idêntico ao upstream |
| patch 0003 (gtav) | cache de pipeline por driver: `shader_cache/vk_pipelines.<driver>.bin` (sistema: nome original) |
| patch 0004 (gtav) | `GameActivity` chama `Odin2Driver.configure` antes de carregar as libs; `build_apk.sh` aceita `ODIN2_JAVA` / `ODIN2_LIBS` |
| `odin2/android/.../Odin2Driver.java` | instalação validada, lista, escolha salva, variáveis por driver, status; importa zips de `files/drivers_import/` (adb) e define `ODIN2_VK_*` |
| `Odin2DriverActivity` / `Odin2DriverTestActivity` | tela "Driver gráfico" e teste em processo separado |
| `driver_test.cpp` | JNI do teste: instância Vulkan pelo driver, propriedades e recursos |
| patch 0005 (gtav) | botão no setup, atalho sem apagar o do upstream, activities no manifesto |

## Uso (com o aparelho no adb)
```sh
odin2/driver/build.sh                                         # -> build/odin2-driver/out/*.so
ODIN2_JAVA=$PWD/odin2/android/java ODIN2_LIBS=$PWD/build/odin2-driver/out \
  work/gtav/tools/android/build_apk.sh install                # APK do Gtavport
tools/driver/push_driver.sh turnip-x.zip                      # importado no próximo início
FAN_MODE=... PERF_MODE=... tools/bench/matrix.sh cena2 120 60 system turnip-x turnip-y
```
O driver **efetivo** vai para `meta.txt` (`driver_effective`), lido do log `Odin2Driver`: se o driver
pedido falhar, o jogo cai no driver do sistema e isso fica registrado, nunca mascarado.

## Validação exigida pelo plano (antes da matriz)
1. APK do upstream, driver do sistema (Etapa 0).
2. APK do Gtavport **com** `libodin2driver.so`, `ODIN2_VK_DRIVER=system` → mesmo resultado dentro do ruído da Etapa 0.
   Esse caminho não chama o adrenotools: valida os patches 0001–0004 e o empacotamento.
3. Só então os Turnip.

## Estado
- Compilado: `libodin2driver.so` + hooks (NDK r29); `vulkan_instance.cpp` do SDK com o patch 0001 (alvo Android);
  `plume_vulkan.cpp` com o patch 0002 (verificação de sintaxe para Android); Java + dex.
- Tela de driver: Java + manifesto compilados e validados (`aapt2 link`); filtro de variáveis e checagem ELF testados na JVM.
- **Não verificado:** build completa do jogo com o patch 0003 (exige o código gerado dos discos) e qualquer execução no aparelho.
