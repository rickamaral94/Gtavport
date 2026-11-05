# Plano — Caminho A (Xbox 360 TU27 → Odin 2 Portal Pro)

Status: **proposta para avaliação**. Caminho A aprovado em 2026-11-05; Caminho B congelado.
Base: `andrewnakas/gtav-recomp` @ `2a76184`. Cada fase abaixo tem objetivo, arquivos, riscos e métrica;
nenhuma começa sem o seu OK explícito naquela fase.

## 0. Ajustes que proponho ao roteiro original

Depois de ler o upstream, três mudanças de ordem/escopo:

1. **adrenotools vem antes do baseline (Fase 0.5).** O APK upstream não carrega driver customizado; sem isso a matriz
   sistema × Mr. Purple × Amaral-Adreno-Tools da Fase 0 não existe. É um patch pequeno e isolado.
2. **Fase 2 encolhe.** O upstream já compila com `armv8.2-a+lse`, já tem byteswap NEON em massa nos vértices e já faz downmix
   para estéreo. Vira validação + o bug do canal central, não reescrita.
3. **Fase 1 é dividida em 1A (código) e 1B (shaders).** O código já tem caminho pronto (`build_llvm.sh`); os shaders
   não têm (falta XenosRecomp + dxc em Android). Separar permite entregar 1A mesmo se 1B atrasar.

Ponto de atenção que muda a Fase 4/5: **a física quebra acima de 30 fps** (upstream #12). Proponho que o "60 fps" venha
de frame generation (lsfg-vk, 30→60) e não de destravar o jogo, até provarmos 40 fps seguro.

## Estrutura do repositório

```
Gtavport/
  upstream/            submódulo pinado no gtav-recomp (nunca editado à mão)
  odin2/patches/       série de patches sobre upstream (quilt-style, numerados, 1 assunto cada)
  odin2/               código novo nosso: adrenotools, ADPF/térmica, Swappy, configurações, controles
  android-ondevice/    pipeline de compilação no aparelho (serviço, checkpoints, toolchain)
  tools/               scripts: aplicar patches, rebase semanal, build do APK, verificação de pureza
  docs/                decisões, baseline, relatórios A/B do ADL
```
Rebase semanal: avançar o submódulo, reaplicar a série, rodar build + verificação; conflito vira item no relatório semanal.

**CI (GitHub Actions)**: build do APK *shell*, verificação de pureza estrutural (sem `.so` de jogo, sem SPIR-V, sem
`expected` de conteúdo além dos hashes já públicos do upstream, limite de tamanho por lib) e licenças. **Limite honesto:** a
taint scan completa do upstream compara byte a byte contra o código gerado do jogo, então só roda numa máquina que tem os
discos — fica como passo obrigatório de release local, não no CI.

---

## Fase 0 — Baseline (sem alterar código)
- **Objetivo:** números de referência do upstream intocado no Odin 2 e gargalo dominante.
- **Arquivos:** só `docs/baseline.md` (protocolo já escrito).
- **Execução:** depende de você ou de uma sessão com o aparelho via adb. Eu preparo um script `tools/bench/run_scene.sh`
  (captura 120 s, `dumpsys gfxinfo`/Perfetto, temperaturas de `/sys/class/thermal`, clocks) — isso é ferramenta, não altera o jogo.
- **Risco:** o Odin 2 tem ventoinha; resultados frios vs. quentes divergem menos que no S23 — bom, mas registrar perfil da ventoinha.
- **Métrica de pronto:** 5 cenas × driver do sistema × 3 execuções preenchidas + perfil `simpleperf` de uma cena dizendo CPU/GPU/shader/térmico.
- **Estimativa:** 1–2 dias de aparelho.

## Fase 0.5 — Troca de driver via adrenotools
- **Objetivo:** escolher driver no APK (sistema / Turnip X / Turnip Y) para completar a matriz da Fase 0.
- **Arquivos:** `odin2/driver/` (integração libadrenotools, como LibertyRecompAndroid), patch no ponto onde o SDK carrega
  `libvulkan` (`odin2/patches/0001-vulkan-loader-hook.patch`), seletor no `SetupActivity`.
- **Riscos:** o código de contorno do driver proprietário (lowering de int64, waits em fatias, rect lists na CPU) pode
  ser desnecessário ou até prejudicial no Turnip — manter por cvar e medir. Bugs de Turnip vão para o repositório do driver.
- **Métrica:** os 3 drivers iniciam e completam a cena 1; matriz da Fase 0 completa.
- **Estimativa:** 3–5 dias.

## Fase 1A — Compilar o código do jogo no aparelho
- **Objetivo:** o usuário escolhe Disc 1 + Disc 2 + TU27 no Odin 2 e sai com `libgtav_game.so` sem PC.
- **O que embarcar no APK** (em `lib/arm64-v8a/`, único local executável):
  `libclang.so` e `libld.lld.so` (LLVM 19.1.5, de `build_llvm.sh`), `librexcodegen.so` (codegen do ReXGlue cross-compilado),
  stubs de link de `android/link/`. Sem sysroot do NDK: o upstream já compila *freestanding*.
- **Fluxo:** SAF → extração XISO/STFS → SHA-256 dos arquivos (`expected.txt` existente) → codegen (811 `.cpp`) → **hash de
  cada `.cpp` contra a referência** → compilação paralela → link `-z now` → `getFilesDir()/game/libgtav_game.so` → `dlopen`.
- **Arquivos:** `android-ondevice/service/` (Foreground Service tipo `dataSync`, notificação com progresso, WakeLock parcial),
  `android-ondevice/jobs/` (fila com checkpoint por arquivo: `.o` + hash da entrada; retomar pula os prontos),
  patch no `SetupActivity`/`android_setup_jni.cpp` para o novo modo "Compilar aqui".
- **Agendamento:** jobs fixados nos núcleos grandes (X3 + 4 gold), `-j` decidido por RAM livre (~1–1,5 GB/job);
  começar `-j4`; unidades acima de um limiar de tamanho em `-O2`, demais iguais ao upstream (para manter hashes de saída comparáveis com o PC).
- **Riscos:**
  - OOM/LMK matando o serviço → limitar jobs por `ActivityManager.MemoryInfo`, checkpoint resolve o retrabalho.
  - Android 14+ restringe Foreground Service de longa duração (`dataSync` tem teto de 6 h/dia) — suficiente, mas registrar.
  - Tamanho do APK sobe ~150–250 MB (clang+lld+codegen). Alternativa: baixar a toolchain como pacote separado assinado — decidir com você.
  - Térmica: compilação de 30+ min a 100 % — mostrar temperatura e permitir pausar.
- **Métrica:** build completo < 40 min no Odin 2 com ventoinha; `libgtav_game.so` funcionalmente idêntico ao do PC
  (mesmo hash de todos os `.cpp`, jogo inicia a cena 1); retomar após `kill` sem recomeçar.
- **Estimativa:** 2–3 semanas.

## Fase 1B — Shaders no aparelho
- **Objetivo:** acabar com o round-trip de `shader_cache` pelo PC (resolve também upstream #7).
- **Proposta:** portar XenosRecomp + dxc para arm64-android e usar dois mecanismos:
  1. **Na instalação:** varrer os arquivos de shader do próprio jogo do usuário e traduzir tudo para SPIR-V em lote
     (com `adreno_shader_fix`, `lower_int64`, `ubo_constants` já existentes em C++/Python → portar os de Python para C++).
  2. **Em jogo:** tradução sob demanda de qualquer shader faltante, em thread de baixa prioridade, gravando no cache — igual ao desktop.
- **Arquivos:** `android-ondevice/shaders/` (build do dxc e XenosRecomp com o NDK, CLI único `libshadertool.so`), patch no renderer para
  chamar o tradutor local quando o SPIR-V não existe.
- **Riscos:** dxc é grande (~40–60 MB) e sua build para Android não é oficial; plano B é traduzir HLSL→SPIR-V com glslang (menos fiel — só se dxc falhar).
  Se a varredura completa não cobrir variantes geradas em tempo de execução, o item 2 cobre.
- **Métrica:** zero objetos faltando nas 5 cenas num aparelho que nunca viu o PC; tempo de tradução em lote informado (meta: < 15 min).
- **Estimativa:** 2–4 semanas (maior incerteza do plano).

## Fase 2 — Corretude ARM64 (escopo reduzido)
- **Objetivo:** validar o que o upstream já fez e corrigir o que falta.
- **Itens:** auditoria de `lwarx/stwcx.` → LSE (teste de estresse multi-thread no aparelho); conferir que o caminho NEON de byteswap cobre índices
  e não só vértices; mover byteswap/destiling de texturas para compute onde o perfil mostrar custo; **áudio 5.1 sem canal central**: reproduzir
  numa cutscene fixa, verificar mapeamento de canais do XMA e o downmix (C × 0,707 em L e R; LFE opcional).
- **Arquivos:** patches em `odin2/patches/02xx-*`; testes em `odin2/tests/`.
- **Métrica:** 0 regressões nas 5 cenas; diálogo audível nas cutscenes 1–3 com save fixo; stress de atomics 1 h sem divergência.
- **Estimativa:** 1–2 semanas.

## Fase 3 — Vulkan no Adreno 740
- **Objetivo:** reduzir tempo de GPU e eliminar stutter.
- **Itens, nesta ordem (cada um com A/B no ADL):**
  1. `loadOp/storeOp` corretos por passe (CLEAR/DONT_CARE onde o passe começa com clear completo) — já listado pelo upstream como candidato.
  2. Juntar passes e cortar resolves desnecessários (~175–190 passes/frame medidos no Mac: ~9 ms de overhead).
  3. GMEM vs sysmem por passe (Turnip: `TU_DEBUG`/heurísticas; proprietário: não controlável — medir).
  4. Pipeline cache persistente + pré-compilação de pipelines após a Fase 1B; `VK_EXT_graphics_pipeline_library` se o driver expuser.
  5. Render scale 50–100 % (upstream já tem 0,75) + FSR1 e UI em resolução nativa (exige o fator de escala nos shaders que calculam tiles por FragCoord — upstream já apontou).
  6. Opções de LOD, sombras, partículas, reflexos — só as que tiverem cvar/hook viável sem tocar conteúdo.
- **Arquivos:** `odin2/patches/03xx-*` (renderer), `odin2/settings/`.
- **Métrica:** GPU ms/frame −25 % na cena 2 em relação ao baseline com o mesmo driver; stutter > 50 ms ausente após 2ª sessão.
- **Estimativa:** 3–4 semanas.

## Fase 4 — CPU, threads e térmica
- **Objetivo:** 30 fps estáveis com 1 % low > 25 após 30 min (perfil equilibrado).
- **Itens:** afinidade: principal e render no X3/gold (upstream já fixa o principal), streaming e áudio nos gold restantes, nunca nos silver;
  ADPF (`APerformanceHint`) com alvo de 33,3 ms; Thermal API (`AThermal_getThermalHeadroom`) reduzindo render scale antes do throttling;
  Swappy para pacing; travas 30/40/45/60 **somente depois** de um teste de física em 40 fps (ver #12) — se falhar, só 30 + FG.
  CPU de gravação (o gargalo do S23): paralelizar com secondary command buffers e reduzir hashing XXH3 (estratégia de dirty-tracking).
- **Arquivos:** `odin2/perf/`, patches `04xx-*`.
- **Métrica:** teste de 30 min na cena 2 + rodovia, 3 execuções, 1 % low > 25 fps.
- **Estimativa:** 3–4 semanas.

## Fase 5 — Frame generation e extras
- **Itens:** lsfg-vk como camada Vulkan (base: `FrankBarretta/lsfg-vk-android` + fluxo do BOTW fork), usuário seleciona o próprio `Lossless.dll`
  via SAF, nada embarcado; mapeamento nativo do Odin 2 (gatilhos analógicos, botões traseiros), rumble, gyro opcional para mira;
  menu de configurações por jogo; exportar logs + métricas para o ADL (sem memória do jogo nos logs).
- **Métrica:** 30→60 apresentados com latência adicional medida e aceitável para você; controles 100 % mapeados.
- **Estimativa:** 2–3 semanas.

---

## Resumo de cronograma (estimativa, sujeito ao baseline)
| Fase | Duração | Depende de |
|---|---|---|
| 0 + 0.5 | ~1,5 semana | aparelho + dumps |
| 1A | 2–3 sem | 0 |
| 1B | 2–4 sem | 1A (paralelizável parcialmente) |
| 2 | 1–2 sem | 0 |
| 3 | 3–4 sem | 1B |
| 4 | 3–4 sem | 3 |
| 5 | 2–3 sem | 4 |
Total: ~4–5 meses de trabalho focado.

## O que eu preciso de você
1. **Acesso ao aparelho:** quem executa as medições? Uma sessão Claude Code local com o Odin 2 via adb seria o ideal; esta sessão em nuvem não alcança o aparelho.
2. **Specs do seu Odin 2 Portal Pro:** RAM (12 ou 16 GB), versão do Android, versão do driver Adreno de fábrica.
3. **Versões de Turnip** a usar na matriz (Mr. Purple e Amaral-Adreno-Tools — links/tags).
4. **Toolchain no APK ou separada?** (APK único ~+200 MB vs. download à parte).
5. **Aprovação da Fase 0 + 0.5** para eu começar: script de benchmark e integração do adrenotools.
