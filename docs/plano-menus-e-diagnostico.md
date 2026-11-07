# Plano: menus, logs e diagnóstico a partir do BOTW/TOTK

Pedido do Amaral (06/11): partir do que já existe nos ports de BOTW (`rickamaral94/Botwnspxci`) e TOTK
(`rickamaral94/Totknspcxi`) — menus, logs, diagnóstico de desempenho — em vez de reinventar no GTA V.
Os dois são MIT (LAS Recomp, Peiikai): o código pode vir para cá com o aviso de copyright, como o FSR/SGSR/SMAA.

## O que existe lá e o que serve aqui

| No TOTK/BOTW | Onde | Aqui |
|---|---|---|
| Menu em ImGui com abas e ícones, linhas de opção em dois painéis (rótulo/valor à esquerda, descrição à direita), navegação por controle (D-pad, A/B, LB/RB), toque com rolagem, fonte própria (Baloo 2), visual "Zonai" | `runtime/ui/menu.mm`, `option_rows.mm` | **Sim.** O GTA V já desenha um menu ImGui no jogo (`menu.cpp`); troca-se o miolo pelas linhas de opção e abas do TOTK, com um visual próprio (escuro, verde/branco, sem arte da Rockstar). |
| Tela inicial no próprio app nativo, com as mesmas abas do menu do jogo | `runtime/ui/launcher.mm` | **Em parte.** No GTA o jogo começa a carregar assim que o processo sobe; a tela inicial continua em Java (já tem o visual GTA V), mas com as mesmas abas, textos e ordem do menu do jogo. |
| Aba **Sair** (voltar à tela inicial / fechar o app) | `menu.mm tab_quit` | **Feito** na v0.2.4 (Voltar > Sair do jogo; jogo no processo `:game`). |
| i18n (pt-BR completo) | `runtime/ui/i18n*.{mm,inc}` | Sim: o menu do jogo em português (hoje está em inglês). |
| **Diagnóstico "Medir desempenho (60 s / 10 min)"**: linhas `run:`/`summary:` (fps, p50/p95/p99, GPU por quadro, MHz por cluster, temperatura, status térmico), tempo de parede de cada thread em categorias exclusivas, CPU própria por thread, amostrador de PC por thread com símbolos | `runtime/diag.c`, `docs/DESEMPENHO-DIAG.md` | **Sim, é o principal.** O nosso problema hoje (~12 fps com GPU e renderer ociosos) é de CPU do jogo; o amostrador de PC diz *em que funções do jogo* (`sub_XXXXXXXX`) o tempo vai. As categorias mudam: em vez de svc do Switch, as esperas do kernel emulado do ReXGlue (`KeWaitForSingleObject`, locks, `gtavfs_global_lock`), o renderer e o GPU emulado. |
| **ADPF** (dicas de desempenho do Android) na thread que apresenta, na de render e nas threads do jogo mais ocupadas | `platform/android.c`, `settings.c` | **Sim, com A/B.** Sem dica, o Android pode deixar as threads do jogo em clocks baixos. No BOTW não houve ganho com as threads do jogo; no GTA o perfil é outro (jogo preso na CPU). |
| **Comparar A/B** (alterna uma opção a cada 10 s e o relatório mostra os dois lados) | `diag.c` | Sim: é a regra 6 (medir antes/depois) feita pelo próprio app. |
| Fila de shaders prioritários, workers de compilação, LRZ do Turnip, memória do jogo na GPU | `exec.c`, `device.c` | A fila prioritária já fizemos (0014). LRZ/GPU heap são do NVN; só o LRZ (variável do Turnip) pode valer aqui, com A/B. |
| Overlay de desempenho no canto | `perf_overlay.mm` | Já temos a seção Performance; ganha o overlay compacto do TOTK. |

## Fases propostas (cada uma termina numa release testável)

**D1 — Diagnóstico (primeiro: é o que destrava as otimizações).**
`odin2/diag/`, portado de `runtime/diag.c` (MIT): botão "Medir desempenho (60 s / 10 min)" no menu do jogo e na
tela inicial; começa após 5 s de jogo fora de menus/loading; relatório em `[diag]` no log e em
`logs/perf/perf-diag-<data>.txt` (a tela Logs de desempenho mostra). Conteúdo: `run:`/`summary:`, percentis do
intervalo entre presents, GPU por quadro, MHz/temperatura/térmico, CPU própria e tempo de parede por thread do jogo
(com os nomes das threads do kernel emulado), amostrador de PC nas 4 threads mais ocupadas, com os endereços do jogo
resolvidos para `sub_XXXXXXXX` pela tabela da `libgtav_game.so` e o resto por módulo+offset. Substitui a sonda
`[stall]` da v0.2.4 (que fica como modo leve). Teste no host como o `test_diag.c`.

**D2 — Primeiras otimizações guiadas pelo D1.** Candidatas, cada uma com A/B no próprio app: ADPF (render +
threads do jogo), afinidade das threads do jogo (hoje só a principal vai para o prime), prioridade das threads de
streaming/IO, o que o amostrador apontar nas funções do jogo. Nada entra sem o A/B mostrar ganho.

**U1 — Menu do jogo no padrão TOTK.** `option_rows` + abas com ícones (Vídeo, Desempenho, Controles, Sobre, Sair),
pt-BR, descrição no painel direito em vez de tooltip, toque com rolagem, fonte própria (OFL), visual próprio do
Gtavport. As opções atuais todas vão para lá (nada some).

**U2 — Tela inicial alinhada.** Mesmas abas e textos do U1 na tela Java (ou, se o Amaral preferir o visual idêntico,
uma tela inicial nativa em ImGui antes do boot do jogo — mais trabalho, decidir depois do U1).

**L1 — Tradução pt-BR (pedido do Amaral, 06/11).** O GTA V do Xbox 360 escolhe os textos pelo idioma que o
console informa (`XGetLanguage`); o runtime respondia sempre inglês. Patch 0022 (o idioma vem de `user_language`) e
a opção *Idioma do jogo* (Configurações > Exibição e menu do jogo; 9 = português do Brasil). Feito na v0.2.6;
falta confirmar no aparelho que os textos em português estão nos discos (se não estiverem, o jogo volta ao inglês ou
mostra textos faltando: aí a opção sai). Depois: o menu do jogo (U1) também em pt-BR.

**R1 — 1080p (pedido do Amaral, 06/11).** Hoje a cena 3D é desenhada em 1280x720 (o 720p do Xbox) vezes a
"Resolução interna" (50-100%) e ampliada para a tela 1920x1080. O renderer já desenha os alvos de largura total numa
escala (constante de especialização 1/escala nos shaders); 1080p é a escala 1,5. Passos: liberar a escala acima de 1
(hoje limitada a 1), conferir resolves/cópias/tiles do EDRAM e a UI do jogo nessa escala, medir no aparelho. Custo
de GPU ~2,25x a 100% (hoje ~10 ms por quadro): faz sentido depois do D2, quando a CPU deixar de ser o limite; com
FSR/SGSR dá para usar 1,25 (1600x900) como meio-termo.

Ordem atualizada: D1 → D2 → L1 (já entra junto, é pequena) → R1 → U1 → U2.

### O que o primeiro log com a sonda mostrou (v0.2.4, 06/11)
Durante o jogo (~13-16 fps), por 10 s: `SDLThread` (a thread principal do SDL, que roda o laço de eventos) usou
~9,5 s de CPU e `GPU Commands` (o processador de comandos da GPU emulada) ~8,5-9,4 s, enquanto a thread principal do
jogo usou só ~1-4 s. Ou seja: duas threads do runtime quase no máximo e o jogo esperando. O `GPU Commands` gira
esperando comandos (`gpu_idle_spins`) por desenho; o `SDLThread` deveria dormir em `SDL_WaitEvent`. São os primeiros
alvos do D2, com o relatório do D1 (amostras de PC) para saber onde o tempo vai dentro delas.

Ordem sugerida antes: D1 → D2 → U1 → U2 (o stutter é o que mais incomoda; os menus vêm logo depois e já usam o D1 para
mostrar o relatório).

## Fora deste plano
DLCs (combinado: depois do jogo rodar bem), geração de quadros (lsfg do TOTK: avaliar depois do D2).

### D2, primeira rodada (relatório perf-diag de 06/11 21:48, Turnip, 60 s de jogo)
11,8 fps; renderer esperando o jogo 89% do quadro; GPU 15,7 ms. Achados:
1. **O jogo inteiro num núcleo só.** As threads do jogo (a principal e as 5 de trabalho) rodaram 100% no núcleo prime e
   52-88% do tempo prontas sem núcleo: a principal é fixada no prime (android_threads.cpp) ~1,5 s depois do início e
   as threads que ela cria herdam a máscara. Patch 0026: as que herdaram "só o prime" ganham os outros núcleos.
2. **A espera de 1 ms virava giro.** `GPU Commands` usou 93% de um núcleo, 96% em `clock_gettime`: a espera alertável
   do runtime arredondava o tempo restante para baixo (1 ms -> 0) e girava lendo o relógio. Patch 0025 (ceil). Vale
   para toda espera alertável curta do jogo.
3. **Travas que giram.** As threads de trabalho passam 77% em syscall e 18% em `atomic_cas` dentro de
   `RtlEnterCriticalSection` (giro de spin count * 256 e depois espera): com o item 1 corrigido o dono da trava deixa
   de disputar o mesmo núcleo. Reavaliar na próxima rodada.
4. **SDLThread a 99%** (63% em `clock_gettime`, átomos/mutex do SDL): sem pilha ainda. O amostrador agora tira a
   assinatura PAC dos endereços e segue a cadeia de frames (leitura segura via process_vm_readv): o próximo
   relatório traz as pilhas.

### D2, segunda rodada (perf-diag de 07/11 05:00, v0.2.7, mesmas condições)
**11,8 -> 31,1 fps** (p50 22 ms, p95 98 ms; nenhum quadro > 400 ms, eram 42). `GPU Commands` 93% -> 4,6% de um núcleo;
a thread principal do jogo 8% -> 41,6% (ainda 47% na fila); as de trabalho saíram do prime. Renderer esperando o
jogo: 89% -> 42% do quadro. GPU agora pesa: 22,8 ms/quadro (p95 97 ms), esperando a GPU 11,9 ms.
- **SDLThread 83% (72% no prime, disputando com a thread principal):** as pilhas mostram
  `SDL_WaitEventTimeoutNS > SDL_PumpEventsInternal > SDL_UpdateJoysticks > ... > clock_gettime`. Bug do SDL3 no
  Android: cada volta da espera põe um POLL_SENTINEL na fila, pôr um evento acorda quem espera, e a espera nunca
  dorme. Patch 0027: sentinela desligada (o laço usa SDL_WaitEvent).
- Próximos alvos: a thread principal em `sub_8380CEF8` / `sub_83807DC8` (22% das amostras dela, possível espera
  ativa do jogo), `NtWaitForSingleObjectEx` com `sched_yield`, e os picos de GPU (p95 97 ms).

### D2, terceira rodada (v0.2.8, dois relatórios de 07/11)
- 06:31: **59,8 fps** (o limite de 60), p95 22 ms, GPU 11,1 ms; a SDLThread sumiu da lista.
- 06:41: **27,4 fps**, p95 178 ms, gargalo **GPU**: 31,7 ms/quadro (p95 177). No gtav.log das duas sessões a GPU
  começa a ~11 ms e entra em fases de 26-61 ms por quadro com os mesmos ~180 passes e ~1000 draws, e o tempo de envio
  (submit) sobe junto (3 -> 50 ms); memória estável (caches iguais). A primeira sessão também teve isso, depois dos
  60 s medidos. Causas candidatas: clock da GPU (temperatura 53-63 °C, sem status térmico do Android), um tipo de
  passe que explode em certas cenas, algo do Turnip (envio/BOs). O relatório ganha a linha do tempo a cada 2 s (fps,
  GPU, submit, MHz e temperatura) e o GPU por tipo de passe da janela mais rápida x mais lenta; o menu ganha a
  gravação de 10 min. Próximo passo: comparar com o driver da Qualcomm na mesma cena.
