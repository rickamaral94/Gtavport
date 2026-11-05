# Decisão de arquitetura: Caminho A (Xbox 360 TU27) vs Caminho B (PC x86-64)

Status: **aguardando aprovação do Amaral**. Nenhum código é escrito antes da resposta.
Data: 2026-11-05. Upstream analisado: `andrewnakas/gtav-recomp` @ `2a76184` (2026-11-03, GPL-3.0, ReXGlue v0.10.0).

## O que o upstream já resolve (lido no código, não suposto)

- Fluxo PC → APK funcionando: launcher faz codegen (811 `.cpp`), confere hashes de referência, compila
  `libgtav_game.so` com LLVM 19 *freestanding* `aarch64-linux-android30`, linka com lld e gera
  `gtav-android-pack.zip` + `GTAV.apk` vazio (sem código do jogo). Taint scan já existe (`tools/release/scan_release*.sh`).
- `tools/android/build_llvm.sh` **já cross-compila clang+lld para rodar no aparelho** (empacotados como
  `libclang.so`/`libld.lld.so` em `nativeLibraryDir`, único lugar executável). Não foi testado no telefone ("Not done yet").
- Medido no S23 FE (Adreno 730): 28–29 fps frio, 14–17 fps quente. Gargalo declarado: **gravação de comandos na CPU**
  (25–36 ms; XXH3 14 %, conversão de vértices 12 % do render thread) e GPU 20–29 ms. Prime core já fixado, render scale 0,75.
- **Lacuna real para a Fase 1:** shaders. O telefone hoje só recebe SPIR-V de shaders que o PC *já viu* (round-trip de
  `shader_cache`). Compilar no aparelho exige **XenosRecomp + dxc para arm64-android**, que não existem no upstream (issue #7 pede justamente isso).

## Comparação

| | **A — Xbox 360 TU27 (recomp estática)** | **B — PC x86-64 (FEX/Box64 + Wine + DXVK/VKD3D)** |
|---|---|---|
| Entrada | Disc 1 + Disc 2 + TU27 do usuário | Pasta do GTA5 PC + launcher Rockstar online |
| Base existente | Fork fino do gtav-recomp (já roda em Android) | Winlator/GameHub genéricos; nada específico |
| Esforço até "pronto" | Médio: Fase 1 = portar dxc+XenosRecomp, foreground service, checkpoints; resto é tuning | Alto e aberto: launcher/Social Club no Wine-ARM, anti-tamper sob tradução dinâmica, VKD3D no Turnip |
| Performance esperada no 740 | ~30–40 fps frio (8 Gen 2 ≈ +15–25 % CPU e +25 % GPU sobre o 8 Gen 1; **estimativa a medir na Fase 0**) | ~10–20 fps estimados: JIT x86→ARM + DX11/12→Vulkan sobre um jogo que pede 4 núcleos rápidos; cache AOT reduz stutter, não o teto |
| Fidelidade | Conteúdo do 360 (720p nativo, LOD de console) | Conteúdo do PC (mas escalado para baixo para rodar) |
| Riscos técnicos | RAM do clang no aparelho (~1,5 GB/job; 811 unidades); W^X/SELinux no `dlopen` (upstream já resolve via `getFilesDir()`); dxc no Android; térmica | Launcher pode recusar Wine/ARM; atualizações do jogo quebram o perfil; tradução do anti-tamper pode ser lentíssima |
| Riscos legais/regras | Baixos se o APK continuar vazio + taint scan | Médios: qualquer travamento do anti-tamper/launcher **não** será contornado (regra 4.2) — se não rodar com o launcher original, o caminho morre |
| Online | Inexistente (360 offline) | Precisa bloquear GTA Online explicitamente |

## Recomendação

**Seguir com A como trilha única por agora; B fica congelado** até A atingir a definição de pronto.
Justificativa: A reaproveita uma cadeia que já roda no Android, o gargalo é conhecido e mensurável, e
o "APK que recompila" é majoritariamente empacotamento + portar dxc/XenosRecomp. B depende de um
launcher/anti-tamper que não controlamos e não podemos contornar, com teto de performance pior.

## Riscos a avisar já

1. **Física em >30 fps** (upstream issue #12): travas de 40/45/60 fps da Fase 4 podem quebrar a física — validar antes de prometer.
2. **Fase 0 exige o Odin 2 físico.** Esta sessão roda em nuvem, sem aparelho e sem os seus dumps: o
   baseline (`docs/baseline.md`) precisa ser executado por você, ou numa sessão com o aparelho via adb.
3. Meta de **< 40 min de build no aparelho**: plausível (PC leva 15–30 min), mas o clang no aparelho pode estourar RAM com 8 jobs — provável `-j4` nos núcleos grandes.

## Decisões pedidas ao Amaral

- [ ] Aprovar Caminho A como principal e congelar B.
- [ ] Aprovar o plano da Fase 0 (`docs/baseline.md`) e confirmar quem roda no aparelho.
- [ ] Confirmar o fork: `rickamaral94/Gtavport` como fork fino com upstream em submódulo/subtree (proposta: **submódulo pinado** + patches em `odin2/patches/`).
