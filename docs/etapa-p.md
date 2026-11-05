# Etapa P — provas de viabilidade: estado

Atualizado: 2026-11-05. Nada aqui foi executado num aparelho ainda: esta sessão roda em nuvem, sem o Odin 2.

| Prova | Ferramenta | Estado | Falta |
|---|---|---|---|
| P1 exec clang/lld | `android-ondevice/probe` | **APK gerado** (127 MB: clang 80 MB + lld 43 MB, dependem só de libc/libm/libdl, alinhados a 16 KB) | rodar no Odin 2 |
| P2 compilar → dlopen | idem | pronto (C e C++23 freestanding, flags e link do upstream) | rodar no Odin 2 |
| P3 unidades reais | `tools/p3/make_bundle.sh` + probe | pronto | sua instalação do PC + aparelho |
| P4 shader no aparelho | `android-ondevice/shaders/` | XenosRecomp compila/linka para Android (verificado com DXC stub); DXC `416fab6b` (mesmo do dxc-bin) compilando | DXC real, CLI de shader no APK de prova, shader capturado |
| P5 contagem de shaders | cvar do upstream `gtav_native_save_missing_shaders` | instruções abaixo | aparelho + jogo |

## P4: cadeia que precisa rodar no aparelho
Lida em `tools/android/make_spirv.sh` (upstream):
`shader_cache/*.bin` → **XenosRecomp** → HLSL → `ubo_constants` → **DXC** (`-spirv -fvk-use-dx-layout`) →
`lower_int64` → SPIR-V, mais a variante `_tiles2` de um shader e duas trocas de texto no `shader_common.h`.
- `ubo_constants` e `lower_int64` **já têm versão C++** no upstream (`gtav/src/native/adreno_shader_fix.cpp`,
  CLI `tools/android/shader_fix`): não precisa de Python no aparelho.
- Os binários "linux arm64" do `dxc-bin` são glibc: não servem no Android. Daí a build cruzada do DXC.
- Entrada: os `.bin` que o próprio jogo grava em `shader_cache/` no aparelho (P5).

## P5: como medir
Com o jogo no aparelho (APK do upstream), `tools/android/set_args.sh --gtav_native_save_missing_shaders=true`,
iniciar um jogo novo e carregar o modo história; depois contar `files/shader_cache/*.bin` e anotar o tempo de
carregamento. Repetir dirigindo 10 min e contar quantos apareceram **depois** do carregamento.
