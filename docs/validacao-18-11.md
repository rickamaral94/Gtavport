# Validação de 18/11: o que medimos e o que falta para chegar perto do Xbox 360

Base: sessão de 10 min com a v0.5.1 (frame gen 2x, LSFG 3.1P, fluxo 50%, 1080p, VSync, 30 fps reais, Turnip
Amaral 27.0.0-devel v5.9.18.1), comparada com a sessão de 17/11 da v0.4.9 (sem frame gen, com os testes A/B/C ligados).

## Estado de cada ponto

| Ponto | Medido | Situação |
|---|---|---|
| Fluidez na tela | 60 imagens/s (30 reais + 30 geradas, ~100% dos gerados mostrados); p95 34,4 ms, p99 35,2 ms | **Resolvido** (frame gen 2x) |
| Quadros reais | 29,9 fps; >50 ms: 32 em 10 min (antes 236) | Bom; restam as travadas abaixo |
| Texturas listradas | normal HEND3N 11:11:10 corrigida (v0.4.7) | **Resolvido** |
| Carros batidos | formatos de vértice com sinal (v0.4.7) | **Resolvido** |
| Chão piscando | viés de profundidade na escala + atraso da GPU 6 ms (v0.4.9) | A confirmar pelo Rick |
| Start/pausa fechando o jogo | contorno da página zero (v0.4.6) | **Resolvido** |
| Tela cheia após print | v0.4.6 | **Resolvido** |
| Thread principal no núcleo prime | 98% do tempo no prime (antes 24%) | **Resolvido** (v0.4.8) |
| Texto em PT-BR | menus e textos em português com o idioma 9 | **Resolvido** |
| Legendas das falas | sem legenda nas falas | A confirmar: opção do jogo (Configurações > Exibição > Legendas) |
| Temperatura | 43–74 °C, GPU 401–680 MHz | Bom |
| Áudio | sem cortes (0 de 1875 quadros em silêncio) | Bom |
| Leitura de arquivos | síncrona do armazenamento do aparelho; 0% de espera de disco nas 14 threads mais ativas | Não limita (o carregamento tardio é do streaming do jogo) |

## As 49 travadas (> 50 ms) restantes, por causa

| Causa | Travadas | Tempo total | Lado |
|---|---|---|---|
| Telas de carregamento (< 50 draws) | 9 | 1611 ms | Jogo (normal) |
| Efeitos (pipelines) e texturas novos | 11 | 1325 ms | **Port + driver**: compilação de pipeline de 4 a 11 ms cada |
| O jogo atrasou o quadro (CPU do jogo) | 16 | 903 ms | Jogo/CPU |
| Gravação longa (cena pesada) | 7 | 604 ms | Port (thread de render) |
| Espera da GPU | 6 | 343 ms | GPU (clock subindo) |

O clock da GPU não é o gargalo: com 401 MHz a GPU fica em ~73% de uso e ~19 ms por quadro real; sobe a 550–680 MHz
só nos trechos pesados.

## Oportunidades, por ordem

1. **Pipeline library (`VK_EXT_graphics_pipeline_library`) no plume.** Shaders compilados em segundo plano quando o jogo
   os cria; no draw, só o fast-link. Tira o custo de 4–11 ms por efeito novo (as travadas ao entrar em área nova).
   Pergunta enviada ao agente do Turnip (18/11): estabilidade da GPL no A740, custo do fast-link, opções do ir3.
2. **Hint de desempenho do Android (ADPF, `APerformanceHint`, API 33).** Informa ao sistema que a thread principal e a
   de render têm 33 ms por quadro; o escalonador ajusta o clock dentro dos limites normais (não é overclock). Alvo:
   as 16 travadas de CPU do jogo.
3. **Máscara do HUD no frame gen** (como no TOTK): o HUD do próprio jogo (radar, textos) é interpolado hoje; nas bordas
   em movimento pode deformar.
4. **Consultas de oclusão das luzes (coronas):** todas respondidas com resultado de alguns quadros atrás (2100 por 300
   quadros). Pode fazer reflexos de faróis/postes piscarem ao virar a câmera. Investigar se aparecer.
5. **Streaming (coisas aparecendo tarde):** limite de memória do jogo (512 MB do 360). Só com alteração no código do
   jogo (distância de LOD ou orçamento de streaming); experimental e desligado por padrão, se for feito.
