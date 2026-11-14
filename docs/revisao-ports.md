# Revisão dos ports e recomps públicos de GTA V para Android (13/11/2026)

Objetivo: achar o que melhora o Gtavport no AYN Odin 2 Portal Pro (Snapdragon 8 Gen 2, Adreno 740, 12 GB,
sem root). Prioridade: 30 fps sustentados no Modo História, menos travadas, iluminação correta e saves
funcionando; 60 fps é a segunda meta.

Ponto de partida histórico: **v0.2.12-pre1** (commit `acb273d`, upstream `2a76184`). Os números citados
abaixo vêm dos relatórios "Medir desempenho" enviados do Odin 2: `logs_5` (`perf-diag-20261107-204636`,
v0.2.11, A/B 10 min) e `logs_6` (`perf-diag-20261108-024109`, v0.2.12, A/B 10 min), ambos na sala com a
árvore de Natal piscando (prólogo, cenário fechado).

## 1. Estado confirmado

| Item | Valor |
|---|---|
| Branch de trabalho | `claude/ports-review-odin2`, a partir de `claude/new-session-crtrp6` (`acb273d`, v0.2.12-pre1) |
| Upstream fixado antes | `andrewnakas/gtav-recomp` `2a76184` (= `main` do upstream hoje) |
| Upstream fixado agora | `e0999ca` (branch `issue-fixes`, v0.4.0) |
| Arquitetura preservada | recompilação estática do Xbox 360 TU27, compilação no aparelho, Turnip via AdrenoTools, saves, fluxo de instalação, assinatura fixa do APK |

### Contradição encontrada nas notas da v0.2.12-pre1

A nota da release dizia que as luzes a 2x2 levaram a GPU de ~61 para ~30 ms e as travadas >100 ms de 370
para 21. Os relatórios mostram outra coisa:

| Relatório | Variante (10 s cada, em rodízio) | fps | GPU média | quadros > 100 ms |
|---|---|---|---|---|
| logs_5 (v0.2.11) | como está | 15,4 | 61,1 ms | 370 |
| logs_5 | **tudo a 2x2 (`coarse_shading=2`, todos os passes 3D)** | **28,9** | **30,5 ms** | **21** |
| logs_6 (v0.2.12) | como está (luzes a 2x2) | 13,9 | 68,3 ms | 451 |
| logs_6 | luzes por pixel (`light_coarse=0`) | 13,7 | 68,8 ms | 465 |
| logs_6 | teste depois do shader (`early_tests=false`) | 10,2 | 95,1 ms | 490 |
| logs_6 | tudo a 2x2 | 27,8 | 31,8 ms | 31 |

O ganho de 61 → 30 ms veio de 2x2 em **todos** os passes 3D, não do 2x2 só nas luzes que foi publicado. As
luzes a 2x2 não mudaram nada mensurável (13,7 → 13,9 fps). O teste de profundidade antes do shader
(patch 0028) é o que ajuda de fato (95 → 68 ms). Corrigido nesta branch (ver seção 3).

### Bug do `tools/prepare.sh` encontrado durante a revisão

`work/gtav` vem de um `git archive` (sem `.git`) e fica dentro do repositório do Gtavport. O `git apply`
rodado ali achava o repositório de cima, lia os caminhos do patch a partir da raiz dele e **pulava** todos os
patches do alvo `gtav` com código 0 ("Skipped patch"). Reproduzido nesta sessão: a `libgtav_fast.so`
compilada depois do rebase não tinha nenhum código do Gtavport. Os relatórios do aparelho mostram que as
builds de release tinham os patches, então o ambiente do CI não caiu nesse caso, mas nada impedia. Agora o
`prepare.sh` exporta `GIT_CEILING_DIRECTORIES=work/` e confere, para cada patch, que todo arquivo que ele
cria ou altera existe na árvore.

## 2. Comparação dos projetos

Licenças: os forks do gtav-recomp herdam GPL-3.0 (compatível; trechos incorporados com atribuição). Projetos
sem LICENSE no repositório servem só como ideia; nada foi copiado deles. Nenhum código, shader ou dado do
jogo de nenhum projeto entrou no Git ou nos artefatos.

| Projeto | Commit/release | Arquivo / símbolo | Mecanismo | Licença | Aplicável ao Odin 2? | Evidência | Decisão |
|---|---|---|---|---|---|---|---|
| andrewnakas/gtav-recomp | `issue-fixes` `e0999ca` (v0.4.0, +43 commits sobre `main`) | `shader_compiler.cpp` `WaitForShader`, `menu.cpp` idioma, `backend.cpp` escala até 2.0 | espera o shader que falta (até 20 s) em vez de pular o draw; menu de idioma; correções de issues | GPL-3.0 | sim, mesma base | corrige iluminação preta (#4/#10 do upstream) | **incorporado** (rebase da série; 0022/0023 de idioma removidos, o upstream já cobre) |
| andrewnakas/gtav-recomp | `perf-linux` `7cd394a` | SDK `XThread::Delay`, `XObject::GetNativeObject`/`StashHandle` | `Sleep(0)` virava `nanosleep` de 100 µs (prioridade do convidado comparada com `kBelowNormal`=8 do POSIX); busca de objeto sem o mutex global | GPL-3.0 | sim (POSIX, mesmo SDK) | nosso perfil da thread principal: 6,7 % em `nanosleep < XThread::Delay < KeDelayExecutionThread`; o upstream não mediu | **incorporado** como `0031-sleep0-cede-a-cpu.patch` |
| andrewnakas/gtav-recomp | `perf-linux` `29cb2e4`, `8a47b12`/`e0855db`, `90484b8`, `2913c9c` | renderer / push constants / listagem de diretório / afinidade | menos paradas da thread de render, memo de push constants, cache de arquivos ausentes, afinidade (padrão desligado) | GPL-3.0 | provável, mas o gargalo medido é GPU | nenhuma medição (nem no upstream) | **não incorporado**: medir CPU antes (regra 6) |
| Ravi9733/gtav-recomp | `fix/adreno-constant-descriptor-guard` `0ce1eda`, `c896b30`, `4d93a49` | `backend.cpp`, plume descriptor sets, upload buffers | guardas contra descritor de constantes nulo, validação de descriptor sets, pula draws com BDA inválido | GPL-3.0 (herdada) | defensivo; nenhum crash nosso bate com esses casos | nenhum log nosso com descritor nulo | **não incorporado** (candidato se aparecer crash desse tipo) |
| Ravi9733/gtav-recomp | `turnip-port-0.3.0-safe` `9132b85` | `args.txt` | `--gtav_fps_limit=30 --vsync=true`, cache de texturas limitado | GPL-3.0 | já temos 30 fps + vsync como padrão | — | já coberto |
| nullbr97/gtav-recomp-mali | — | workflows de CI | só edição de CI; nenhum código Mali | GPL-3.0 | não | — | nada a incorporar |
| XHYN-PH/XHYN-V-reborn | — | — | port com motor de PC | sem LICENSE | só ideias | — | nada copiado |
| rushilvishwakarma/Rush5, port chinês (PC Legacy ARM64 + DXVK) | — | — | outro motor (versão de PC) + DXVK | sem LICENSE / não verificável | arquitetura diferente | — | só ideias |
| mempage4k/glue5 | `bbab319` e vizinhos | camada wgpu | stipple fades (adicionados e revertidos), política de command pool no Adreno | LICENSE presente | motor diferente (wgpu) | — | não transferível |
| c22dev/muguet | #66, #67, #75, #76 | aquecimento de pipelines, cloud shadows, chave de SRV, áudio | aquecimento de pipelines; fallback de taxa nativa de áudio | LICENSE presente | já temos aquecimento de pipelines (upstream) | — | ideia registrada (áudio) |
| lilblock1/gta5mobile-android-test-improvements | `bba27f8` (#10), #9 | `src/gpu.rs` | log de esperas longas pelo slot de GPU; guarda de overrun do anel de áudio; baseline Galaxy S25+ (oscilação térmica, 10,6 GiB PSS) | LICENSE presente | ideia já coberta pelo nosso diag (esperas por balde, GPU por passe) | — | já coberto |
| AlgoArtist06/Vulkan-GTA-V, WALID1618Y, CurelME, mohitky2018 | — | docs de release, launcher (MIT) | importação de save do Modo História (mohitky2018) | variadas | saves já funcionam no Gtavport | — | nada a incorporar agora |
| bytarch/gta5mobile-android | — | — | — | — | inacessível nesta sessão | — | não avaliado |

**Soluções compartilhadas:** limite de 30 fps + vsync (Ravi9733, nós), aquecimento de pipelines (muguet,
upstream), logs de esperas de GPU (lilblock1, nosso diag). Ninguém publicou correção para o custo por pixel do
passe HDR no Adreno; é o nosso gargalo principal e continua sem causa raiz.

## 3. O que mudou nesta branch

| Mudança | Arquivo | De onde veio | O que sustenta | Reverter |
|---|---|---|---|---|
| Rebase para o upstream v0.4.0 | `versions.lock`, `upstream`, série | `issue-fixes` `e0999ca` | correções de iluminação/shader do upstream | voltar o pin para `2a76184` |
| `Sleep(0)` cede a CPU; busca de objeto sem lock global | `odin2/patches/0031-sleep0-cede-a-cpu.patch` (SDK) | upstream `7cd394a` | causa raiz documentada + 6,7 % do perfil da thread principal no `nanosleep` | tirar a linha da `series` |
| Luzes a 2x2 desligadas por padrão; textos corrigidos | 0029, 0030, `Odin2SettingsActivity.java`, `odin2_diag.cpp` | logs_6 | 13,7 → 13,9 fps (sem efeito) | opção "Iluminação das luzes" continua no menu |
| Modo "Isolar o passe HDR" no diagnóstico | `0032-censo-do-passe-hdr.patch`, `odin2_diag.cpp`, `Odin2MenuActivity.java` | próprio | GPU p95 ~273 ms com quadros lentos em degraus de ~250 ms, GPU 99 % ocupada; 2x2 em tudo divide o custo (= custo por pixel num passe) | opção fica desligada; o censo só liga com o pedido |
| `prepare.sh` não pula mais patches em silêncio | `tools/prepare.sh` | próprio | reproduzido nesta sessão | — |

Adiado com motivo: **R1 1080p** (escalas 1,25/1,5). A GPU já fica 99 % ocupada a 720p × 0,75 na cena
medida; subir a resolução agora só piora a meta de 30 fps, e o caminho de upscaler (FSR/SGSR) foi feito para
ampliar, não reduzir. Volta depois que o passe HDR estiver resolvido.

## 4. O que ainda precisa ser medido (no Odin 2)

1. **Isolar o passe HDR** (Menu > Logs de desempenho > "Isolar o passe HDR (10 min)") parado na sala da árvore
   de Natal. O relatório traz o censo de pixel shaders do alvo HDR e, para cada um desligado, fps/GPU/travadas.
   O shader cujo desligamento derruba a GPU de ~68 ms para perto de ~30 ms é o alvo da próxima correção
   (laço longo no shader traduzido, constantes de laço depois da correção de UBO do Adreno, ou custo real).
2. **Sleep(0)**: "Logs de desempenho (10 min)" no mesmo lugar e numa rua com trânsito; comparar CPU da thread
   principal e a fatia de `nanosleep` com o relatório da v0.2.12.
3. **Driver no passe HDR** (sugestões do agente Turnip, 14/11; nenhuma testada no Odin ainda). Mesmo APK,
   mesma posição na sala da árvore, "Logs de desempenho (60 s)" em cada caso, com as variáveis definidas em
   Driver gráfico > Variáveis, que só valem com aquele driver:
   - driver do sistema contra Turnip sem variáveis. Se a Qualcomm tiver o mesmo custo, o problema está no
     conteúdo do shader; se só o Turnip for lento, acionar a campainha;
   - Turnip com `IR3_SHADER_DEBUG=nouboopt`: se o passe HDR mudar muito, ganha peso a hipótese de laço com
     limite lido de UBO;
   - Turnip com `TU_DEBUG=sysmem` contra `TU_DEBUG=gmem`. O alvo tem ~11 MB, maior que a GMEM: ganhar com
     sysmem indica custo de binning, ganhar com gmem indica custo de banda;
   - `IR3_SHADER_DEBUG=nofp16` serve só de controle de precisão: anotar se a imagem mudar;
   - `TU_DEBUG=perf` mostra no logcat por que GMEM ou LRZ foram desligados em cada passe.
   Conferir também, na linha do tempo do relatório, se os degraus de ~250 ms coincidem com queda de clock
   da GPU ou subida de temperatura. Se coincidirem, a causa é DCVS ou térmica, não o shader.
4. Depois que o "Isolar o passe HDR" apontar o shader: tirar as estatísticas dele no driver
   (`VK_KHR_pipeline_executable_properties`: waves, registradores, laços, spill e o assembly IR3, para ver se
   há `ldc` dentro do laço). Isso exige ligar a extensão e a flag de captura no plume; fica para depois de
   conhecido o shader, para não mexer no caminho de criação de todos os pipelines.
5. Protocolo completo abaixo.

## 5. Protocolo de teste

Comparar três builds no mesmo aparelho: **upstream** (APK público do gtav-recomp v0.4.0, se disponível),
**atual** (v0.2.12-pre1) e **candidata** (esta branch). Para cada uma:

| Cena | Como chegar | Duração |
|---|---|---|
| Prólogo / sala com a árvore de Natal (Bobcat) | início do Modo História | 10 min parado + andando |
| Los Santos com trânsito | centro, cruzamento movimentado | 10 min |
| Rodovia | dirigir em velocidade | 10 min |
| Noite com muitas luzes | Vinewood/centro à noite | 10 min |

- Cache frio (logo após recompilar/limpar o cache de pipelines) e cache quente (segunda sessão).
- Uma sessão contínua de ≥ 10 min para cada build (temperatura e clocks).
- Métricas do relatório "Medir desempenho": fps médio, 1 % low, p99, quadros > 50 ms e > 100 ms, tempo de
  CPU por thread e de GPU (média/p95, por passe), clocks de CPU/GPU, temperatura, RAM (PSS).
- Verificar à mão: imagem (luzes, sombras, partículas e transparências — o teste antes do shader e o 2x2 não
  podem cortar fumaça/vidro), áudio, controle, salvar e carregar, sair e voltar ao app (resume).

### Drivers, isolados

Mesmo APK, um driver por vez, cache de pipelines separado (limpar entre trocas): driver do sistema
(Qualcomm), Turnip de referência do projeto e Turnip do Amaral Adreno Tools (v5.8.17.1, pacote Standard
para o A740). Anotar o driver carregado (o relatório traz a linha `driver`).

**Campainha do agente Turnip** (combinado com o Rick em 14/11: acionar sempre que algo precisar de ajuste
ou análise no lado do driver, e discutir com ele o que ajustar). Ela fica na PR #2 deste repositório
("turnip: campainha do agente Turnip", rascunho, não mesclar); detalhes em `docs/campainha-turnip.md` no
branch `turnip/campainha`. O comentário começa com `[gtav]` e traz:
- sintoma e rótulo (validado no Odin / verificado localmente / pendente de hardware);
- build do Gtavport e driver (`driverName` e `driverInfo` do relatório);
- o A/B contra o driver do sistema, no mesmo APK e com cache separado;
- onde estão os relatórios;
- o comportamento esperado.

Quando acionar: crash, device lost ou corrupção que só acontecem no Turnip; um passe mais lento só no
Turnip; dúvida sobre variável de ambiente ou recurso do driver; pedido de driver de laboratório. Antes de
relatar corrupção só no Turnip, repetir com `TU_DEBUG=flushall` e com `TU_DEBUG=syncdraw` (mesa#15792:
corrupção por desenho no A725/A750 que some com esses flags).

### Rótulos dos resultados

Cada número no PR/release leva um destes rótulos: **validado no Odin** (relatório do aparelho anexado),
**verificado localmente** (build, testes, pureza) ou **pendente de hardware**.
