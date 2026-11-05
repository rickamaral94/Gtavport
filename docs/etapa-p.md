# Etapa P — provas de viabilidade: estado

Atualizado: 2026-11-05. Nada aqui foi executado num aparelho ainda: esta sessão roda em nuvem, sem o Odin 2.

| Prova | Ferramenta | Estado | Falta |
|---|---|---|---|
| P1 exec clang/lld | `android-ondevice/probe` | **APK gerado** (127 MB: clang 80 MB + lld 43 MB, dependem só de libc/libm/libdl, alinhados a 16 KB) | rodar no Odin 2 |
| P2 compilar → dlopen | idem | pronto (C e C++23 freestanding, flags e link do upstream) | rodar no Odin 2 |
| P3 unidades reais | `tools/p3/make_bundle.sh` + probe | pronto | sua instalação do PC + aparelho |
| P4 shader no aparelho | `android-ondevice/shaders/` + probe | **no APK de prova** (155 MB): DXC `416fab6b` (libdxcompiler 25 MB), XenosRecomp, `shader_fix`; P4a compara byte a byte com o SPIR-V do DXC do PC | rodar no Odin 2; shaders capturados para P4b |
| P5 contagem de shaders | cvar do upstream `gtav_native_save_missing_shaders` | instruções abaixo | aparelho + jogo |

## P4: cadeia que precisa rodar no aparelho
Lida em `tools/android/make_spirv.sh` (upstream):
`shader_cache/*.bin` → **XenosRecomp** → HLSL → `ubo_constants` → **DXC** (`-spirv -fvk-use-dx-layout`) →
`lower_int64` → SPIR-V, mais a variante `_tiles2` de um shader e duas trocas de texto no `shader_common.h`.
- `ubo_constants` e `lower_int64` **já têm versão C++** no upstream (`gtav/src/native/adreno_shader_fix.cpp`,
  CLI `tools/android/shader_fix`): não precisa de Python no aparelho.
- Os binários "linux arm64" do `dxc-bin` são glibc: não servem no Android. Daí a build cruzada do DXC.
- Entrada: os `.bin` que o próprio jogo grava em `shader_cache/` no aparelho (P5).
- Todas as quatro ferramentas compilam para aarch64-android: XenosRecomp (948 KB), `shader_fix` (1 MB),
  DXC `416fab6b` = v1.8.2407 (o mesmo commit dos binários do `dxc-bin`, para o SPIR-V sair igual ao do PC).
- No APK de prova: **P4a** roda o DXC num shader HLSL nosso (com int64, para exercitar o `lower_int64`) e compara
  o resultado **byte a byte** com o SPIR-V que o DXC do PC (dxc-bin, mesmo commit) gera do mesmo arquivo; e
  **P4b** roda a cadeia inteira nos `.bin` em `files/p4/`, comparando byte a byte com `files/p4/ref/*.spv` do PC.
  HLSL e SPIR-V gerados são apagados ao fim do teste; fica só o CSV com tempos e resultado.

## Achado para a Fase 1B (varredura)
No modo diretório, o XenosRecomp já varre arquivos arbitrários procurando contêineres de shader (assinatura
`0x102A1100` + tamanhos consistentes). É a base da "varredura dos arquivos do jogo" deixada como melhoria opcional.
**Não verificado:** se os RPF do 360 guardam esses contêineres sem compressão/criptografia. Se não guardarem,
seria preciso extraí-los antes, e o plano continua sendo captura → tradução → cache → faltantes.

## P5: como medir
Com o jogo no aparelho (APK do upstream), `tools/android/set_args.sh --gtav_native_save_missing_shaders=true`,
iniciar um jogo novo e carregar o modo história; depois contar `files/shader_cache/*.bin` e anotar o tempo de
carregamento. Repetir dirigindo 10 min e contar quantos apareceram **depois** do carregamento.
