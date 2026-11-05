# APK de prova — Etapa P (P1, P2, P3)

APK separado (`com.gtavport.probe`), mesmos min/targetSdk do APK do upstream (30/34), sem nada do jogo.

| Teste | O que prova | Passa se |
|---|---|---|
| **P1** | `libclang.so` e `libld.lld.so` (LLVM 19.1.5 do `build_llvm.sh` do upstream) executam de `nativeLibraryDir` | `--version` dos dois com exit 0 |
| **W^X** (controle) | a mesma ferramenta copiada para `getFilesDir()` **não** executa | `posix_spawn` falha com EACCES |
| **P2** | compilar (C e C++23 freestanding, flags do upstream) → linkar (opções de link do `libgtav_game.so`) → `dlopen` → chamar | retorna 42 nos dois casos |
| **S** | carga sintética (~400 funções longas): RSS e tempo do clang no aparelho | só referência; **não substitui P3** |
| **P4a** | DXC no aparelho: HLSL próprio → SPIR-V (flags do upstream) → `lower_int64` | SPIR-V válido nos dois passos |
| **P4b** | cadeia do `make_spirv.sh` em shaders capturados (`files/p4/*.bin`) | SPIR-V ok; idêntico a `files/p4/ref/*.spv` do PC quando fornecido |
| **P3** | unidades reais do jogo do usuário, maiores primeiro, N jobs nos núcleos grandes | gera `p3_result.csv` (RSS máx, tempo, temperatura por unidade) |

Medição por processo: `posix_spawn` + `wait4` (RSS máximo, tempo de parede, CPU user/sys), afinidade de CPU
herdada da thread que cria o processo.

## Requisito descoberto: headers internos do clang
O código do jogo compila com `-nostdlibinc`, mas usa os headers internos do clang (`stdint.h`, `arm_neon.h`).
No aparelho o clang fica em `nativeLibraryDir` e não os encontra sozinho. O APK leva esses headers como asset
(`clang-resource.zip`), extrai para o armazenamento interno e passa `-resource-dir`. **O APK do jogo (Fase 1A)
precisa do mesmo.**

## Uso
```sh
work/llvm/tools/android/build_llvm.sh 3                       # clang+lld para o aparelho (horas)
android-ondevice/probe/build.sh install                      # -> build/probe/gtavport-probe.apk
# abrir "Gtavport Probe"; relatório na tela e em:
adb pull /sdcard/Android/data/com.gtavport.probe/files/probe_report.txt
# P3 (opcional, com a sua instalação do PC):
tools/p3/make_bundle.sh /caminho/do/gtav-recomp-no-pc /tmp/p3bundle 4 4 push
```
`make_bundle.sh` recusa destino dentro do repositório, e o APK de prova apaga a cópia interna das unidades
quando termina: código gerado do jogo não fica aqui.
