# Plano — Caminho A (Xbox 360 TU27 → Odin 2 Portal Pro) — v2

Status: **v2 para avaliação**. Caminho A aprovado em 2026-11-05; Caminho B congelado.
Base: `andrewnakas/gtav-recomp` @ `2a76184` (main). Branch `issue-fixes` @ `9fdbbdc` acompanhado (ver Política de FPS e Fase 2).
Nenhuma etapa começa sem OK explícito. **Prazos não são compromisso até o portão G1** (fim das provas de viabilidade).

## Mudanças da v1 → v2

| # | v1 | v2 | Motivo |
|---|---|---|---|
| 1 | adrenotools antes do baseline | **baseline do upstream intacto primeiro**, depois adrenotools e repetição com o driver do sistema | detecta regressão da própria integração |
| 2 | Fase 1A com prazo, `-j4`, < 40 min | **provas de viabilidade (P1–P5) antes**; `-j` e tempo derivados de medição | os ~1,5 GB/job do `build_llvm.sh` são da build do próprio LLVM, não dos `.cpp` do jogo — erro meu na v1 |
| 3 | shaders depois de 1A | **prova de shader em paralelo, desde o início** | maior risco do plano |
| 4 | "traduzir todos os shaders na instalação" por varredura | **captura dos shaders criados → tradução local → cache persistente → faltantes**; varredura vira melhoria opcional | ainda não sabemos localizar/extrair todos dos arquivos; segundo o mantenedor (issue #7), quase todos os ~3.600 são criados no carregamento do modo história |
| 5 | glslang como plano B do DXC | **não é substituto**; só com validação de compatibilidade shader a shader | semântica HLSL diferente |
| 6 | "física quebra acima de 30 fps" | problema relatado é **principalmente condução**; upstream já tem 30 fps em veículos (`fab5c28`); demais sistemas não validados | conclusão era forte demais |
| 7 | CPU (Fase 4) depois de GPU (Fase 3) | **duas trilhas ordenadas pelo gargalo medido** | o próprio upstream aponta gravação de comandos na CPU |
| 8 | ~9 ms de overhead de passes como previsão | **pista**, medida em M1/MoltenVK | outra GPU, outro driver |
| 9 | LSE = atomics corretos | **validar semântica** `lwarx/stwcx.` e `ldarx/stdcx.` | flag de compilação não prova semântica |
| 10 | toolchain no APK ou separada | **no APK** na 1ª versão | arquivo extraído em diretório gravável não pode ser `execve()` |
| 11 | "teto de 6 h do dataSync no Android 14+" | vale para apps com **targetSdk 35+** nas condições documentadas; tratar `Service.onTimeout()` | correção factual |
| 12 | metas fixas (−25 % GPU, < 40 min, sem stutter > 50 ms) | **metas provisórias**, revistas após o baseline | ainda não medidas |

## Estrutura do repositório (sem mudança de conceito)

```
upstream/            submódulo pinado (nunca editado à mão)
odin2/patches/       série numerada sobre upstream, 1 assunto por patch
odin2/               código novo: driver, perf, settings, controles
android-ondevice/    compilação e shaders no aparelho
tools/               apply_patches.sh, rebase semanal, build reproduzível, bench
docs/
```
Requisitos para essa estrutura funcionar:
- `tools/apply_patches.sh` idempotente (aplica a série sobre um submódulo limpo; falha alto em conflito).
- Versões fixadas: commit do upstream e de rexglue-sdk/XenosRecomp/plume/SDL, NDK (r29), LLVM 19.1.5, DXC (tag), Turnip (tags).
- Build reproduzível em container (Dockerfile com toolchain pinada); o hash do APK shell é registrado por release.
- CI: build do shell + verificação estrutural de pureza + licenças. A taint scan completa continua como passo **local** de release (precisa dos discos).

---

## Etapa 0 — Baseline do upstream intacto
- **Objetivo:** números do upstream sem qualquer patch, **driver do sistema**, no Odin 2.
- **Build:** fluxo oficial do PC (Build for Android), um pack único; commit e versão do pack registrados.
- **Protocolo:** `docs/baseline.md` (revisado nesta v2): cache frio / aquecido / sessão sustentada (30 min), tempos internos de CPU e GPU do renderer (`--gtav_wait_profile`, timestamps de GPU), telemetria indisponível registrada como tal, ventoinha e modo de desempenho fixos.
- **Entregável:** tabela + gargalo dominante **com evidência de perfil**. É ela que ordena as trilhas de CPU e GPU.

## Etapa 0.5 — adrenotools
- **Objetivo:** trocar o driver no APK, sem regredir.
- **Arquivos:** `odin2/driver/`, `odin2/patches/0001-vulkan-loader-adrenotools.patch`, seletor no SetupActivity.
- **Sequência de validação:**
  1. APK com adrenotools **usando o driver do sistema** → comparar com a Etapa 0. Diferença acima do ruído (definido pela variação das 3 execuções da Etapa 0) = regressão a corrigir antes de seguir.
  2. Matriz sistema / Turnip Mr. Purple / Turnip Amaral-Adreno-Tools em ordem **A/B/BA** (cada driver medido antes e depois dos outros), **cache de pipeline separado por driver e versão**.
- Contornos do driver proprietário que o upstream fez (lowering de int64, waits em fatias, rect lists na CPU) continuam ligados por cvar. Desligar algum só com A/B por driver.

## Etapa P — Provas de viabilidade (em paralelo com 0/0.5)
Sem serviço, interface ou checkpoints, só a demonstração mínima no Odin 2. Cada prova tem saída binária: passou ou não passou, com números.

| Prova | Demonstração | O que medir |
|---|---|---|
| **P1** | `libclang.so` e `libld.lld.so` empacotados no APK executam via `execve()` a partir de `nativeLibraryDir` (com `extractNativeLibs=true` / `useLegacyPackaging`) | funciona sob SELinux `untrusted_app` no targetSdk escolhido |
| **P2** | compilar e linkar uma `.so` pequena no aparelho, colocá-la em `getFilesDir()` e carregá-la com `dlopen` | carregamento OK com `-z now` |
| **P3** | rodar o codegen do ReXGlue no aparelho e compilar unidades representativas, **incluindo as maiores** das 811, com as flags do upstream | RSS máximo por job, tempo por unidade, temperatura → `-j` seguro e tempo total projetado |
| **P4** | XenosRecomp + DXC compilados para arm64-android traduzem **um shader capturado** (com adreno_shader_fix, lowering de int64 e UBO), e o renderer o usa no lugar do SPIR-V vindo do PC | saída igual à do PC (diff de SPIR-V) ou diferença explicada; render correto |
| **P5** | instrumentar a criação de shaders (o upstream já tem `gtav_native_save_missing_shaders`) num carregamento do modo história no aparelho | quantos shaders, quanto tempo leva traduzir todos no aparelho, quantos ainda aparecem depois em jogo |

**Portão G1 (decisão sua):** com P1–P5 em mãos eu apresento prazo e metas revisados para 1A/1B. Se P4 falhar, o plano volta para "shaders via PC" na 1ª versão, e o resto continua igual.

## Fase 1A — Compilação do código no aparelho (produto)
- Foreground Service `dataSync`: progresso, pausa por temperatura, **checkpoint por arquivo** (`.o` + hash da entrada). Se o targetSdk for 35+, implementar `onTimeout()`: salvar estado, parar e retomar na próxima abertura.
- Jobs nos núcleos X3 e gold, com `-j` vindo de P3 e limitado pela RAM livre em tempo real.
- Verificação: SHA-256 dos discos (`expected.txt`), hash de cada `.cpp` gerado contra a referência, mesmas flags do upstream.
- Toolchain dentro do APK (~+150–250 MB, número exato após P1).
- **Meta provisória:** < 40 min, a revisar com P3.

## Fase 1B — Shaders no aparelho (produto)
Ordem: **captura → tradução local → cache persistente → faltantes**.
1. Na 1ª execução, a tela de carregamento do modo história captura os shaders criados e traduz em lote, com progresso.
2. O cache SPIR-V e o cache de pipeline persistem, versionados por build e driver.
3. Shader faltante em jogo: tradução em segundo plano, com registro no log. Política para o frame em que ele falta (pular o draw ou esperar) definida por medição.
4. Varredura completa dos arquivos do jogo: só se P5/1B mostrarem faltantes relevantes.

## Fase 2 — Corretude ARM64
- **Atomics:** testes de estresse multi-thread para a semântica de reserva PowerPC no ARM64: falha espúria de `stwcx.`, perda de reserva por escrita de outra thread, `ldarx/stdcx.` de 64 bits, granularidade da reserva, ordenação com `sync/lwsync/isync`. Rodar 1 h no Odin 2 sem divergência, comparando com uma referência sequencial.
- Cobertura NEON do byteswap (índices além de vértices). Mover para compute só onde o perfil mostrar custo.
- **Áudio do canal central em cutscenes:** o upstream está investigando o XMA de cutscenes no `issue-fixes` (`a67fdad`, `3af0c1c`). Primeiro reproduzir com save fixo e trazer o trabalho deles; patch próprio só para o que sobrar (mapeamento de canais e downmix com C × 0,707). O que for geral vira contribuição upstream.

## Trilhas de desempenho (CPU e GPU), ordenadas pelo gargalo da Etapa 0

Regra: cada experimento tem **hipótese concreta + previsão + A/B no ADL** (3 execuções, mesma temperatura inicial). Experimentos que o upstream já mediu sem ganho só voltam com hipótese de por que o Adreno 740 / 8 Gen 2 mudaria o resultado:
- hash workers em threads (não ganhou)
- conversão de vértices em workers (1–6 ms pior)
- write-watch por proteção de páginas (2× pior, lock global por falta de página)
- XMA flush em vez de reopen (menos CPU, mais falhas de áudio)

**Trilha CPU:** afinidade (render e streaming fora dos núcleos silver), redução do hashing XXH3 (dirty-tracking com custo medido), secondary command buffers para gravação paralela, ADPF (`APerformanceHint`) e Thermal API (headroom → ajuste de render scale), Swappy.

**Trilha GPU:** `loadOp/storeOp` por passe, fusão de passes e corte de resolves, GMEM vs sysmem (onde o driver permitir), pipeline cache + GPL se disponível, render scale 50–100 % + FSR1 com UI nativa (exige o fator de escala nos shaders que usam FragCoord para tiles), opções de LOD/sombras/partículas/reflexos onde houver hook viável.

**Metas provisórias** (revistas após a Etapa 0): 30 fps com 1 % low > 25 após 30 min no perfil equilibrado; ganho de GPU e limite de stutter definidos a partir do baseline.

## Política de FPS
- **30 fps é o padrão** em todo o jogo.
- Avaliar a solução do upstream (`fab5c28`: 30 fps em veículos, FPS escolhido a pé).
- Antes de liberar qualquer trava acima de 30 (40/45/60), validar **separadamente** em cada valor: condução, movimento a pé, colisões e física de objetos, missões com cronômetro/scripts sensíveis a tempo, cutscenes (sincronia de áudio). Passar a 40 não libera 45 nem 60.
- **Frame generation (lsfg-vk)** é experimental: entra só se houver sobra real de GPU, e com latência medida e aceita por você. O ADL registra **fps reais e fps apresentados** separadamente. O `Lossless.dll` é sempre do usuário; nada é embarcado.

## Desde o primeiro APK de teste
- Controles básicos do Odin 2 funcionando (gamepad interno via SDL).
- Exportação de logs e métricas para o ADL sem memória do jogo: o upstream já tem relatório de bug com um clique no `issue-fixes` (`de53c76`), que vamos aproveitar.

## Extras (depois da trilha de desempenho)
Mapeamento completo do Odin 2 (botões traseiros, gatilhos analógicos), rumble, gyro opcional, configurações por jogo.

---

## Estimativa
Os 4–5 meses da v1 ficam como **estimativa inicial, não compromisso**. Prazo revisado sai no portão G1, depois de P1–P5.

## O que preciso de você
1. **Quem executa no aparelho:** o ideal é uma sessão local do Claude Code com o Odin 2 via adb, porque esta sessão em nuvem não alcança o aparelho.
2. Specs do Odin 2 Portal Pro: RAM, versão do Android, versão do driver Adreno de fábrica, perfis de ventoinha disponíveis.
3. Tags das versões de Turnip (Mr. Purple, Amaral-Adreno-Tools).
4. targetSdk desejado (afeta `onTimeout` do dataSync e restrições de execução). Proposta: o mesmo do upstream na 1ª versão.
5. **Aprovação para começar:** Etapa 0 (scripts de benchmark), Etapa 0.5 e as provas P1–P5.
