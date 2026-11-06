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

Ordem sugerida: D1 → D2 → U1 → U2 (o stutter é o que mais incomoda; os menus vêm logo depois e já usam o D1 para
mostrar o relatório).

## Fora deste plano
DLCs (combinado: depois do jogo rodar bem), geração de quadros (lsfg do TOTK: avaliar depois do D2).
