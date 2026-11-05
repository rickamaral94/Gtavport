# odin2/driver — troca de driver Vulkan (Etapa 0.5)

## Peças
| Peça | O que faz |
|---|---|
| `libodin2driver.so` (`odin2_driver.cpp`) | abre o driver escolhido com `adrenotools_open_libvulkan` **uma vez por processo** |
| hooks do libadrenotools (`libmain_hook.so`, `libhook_impl.so`, `libfile_redirect_hook.so`, `libgsl_alloc_hook.so`) | precisam estar em `nativeLibraryDir` (o manifesto do upstream já tem `extractNativeLibs="true"`) |
| patch 0001 (rexglue-sdk) / 0002 (plume) | runtime e renderer pedem o handle via `dlopen("libodin2driver.so")` e usam o **mesmo** driver; sem a lib, comportamento idêntico ao upstream |
| patch 0003 (gtav) | cache de pipeline por driver: `shader_cache/vk_pipelines.<driver>.bin` (sistema: nome original) |
| patch 0004 (gtav) | `GameActivity` chama `Odin2Driver.configure` antes de carregar as libs; `build_apk.sh` aceita `ODIN2_JAVA` / `ODIN2_LIBS` |
| `odin2/android/.../Odin2Driver.java` | importa zips de `files/drivers_import/` para o armazenamento interno e define `ODIN2_VK_*` |

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
- **Não verificado:** build completa do jogo com o patch 0003 (exige o código gerado dos discos) e qualquer execução no aparelho.
