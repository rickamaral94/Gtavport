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

## Resposta do agente do Turnip (18/11, Amaral commit `da0ccbd`, `docs/audits/2026-11-18-gtav-pipeline-compile.md`)

Medido no código do Mesa `a6aacf6` e numa reprodução com o par de shaders do caso extremo:

- **Muda a premissa:** no Turnip, formato do alvo, blend, layout de vértice, depth/stencil e topologia **não** entram
  na chave dos shaders. O mesmo VS+PS com outro estado custa 0,09–0,15 ms. O que recompila tudo é: **par VS/PS novo**,
  **outra spec constant** ou outro layout. O nosso renderer põe spec constants por draw (o bit de alvo escalado da
  escala 1,5x e outros): o mesmo par em alvo escalado e não escalado já são duas compilações.
- **GPL:** anunciada sem condição; a correção de junção de set layouts já está na 5.9.18.1; sem bug conhecido, mas o CTS
  de GPL não foi rodado. Usar o mesmo `VkPipelineLayout` em todas as bibliotecas, sem `INDEPENDENT_SETS`; manter a
  volta ao monolítico e a opção de desligar.
- **Custos (host, proporções):** bibliotecas de vertex input/fragment output ~0,05 ms; pré-raster (VS) 7–9 ms; fragment
  shader até 0,9 s no caso extremo (o ir3 compila na criação da biblioteca); **fast-link 0,08 ms**; link com LTO ≈
  monolítico. Recomendado: fast-link na hora e LTO/monolítico em segundo plano, trocando quando ficar pronto; medir antes
  o custo de GPU do fast-link.
- **Não recomendado:** opções do ir3 para compilar mais rápido (não existem sem perda), cache de disco do Mesa (não evita
  a recompilação por troca de parceiro) e restrição de energia do KGSL (o governador está certo a 30 fps).

**Próximo passo:** contar no log, por pipeline novo, se é PS novo, PS já visto com outro VS, ou o mesmo par com outra
spec constant. Com isso decidir entre tirar a spec do caminho (push constant) e a GPL.

Complemento do agente do Turnip: a spec constant também entra na chave da biblioteca de FS na GPL, então tirá-la do
caminho ajuda nos dois caminhos (não são excludentes). Passar para push constant custa um pouco de GPU (o ramo morto
deixa de ser eliminado): desprezível para o bit de "alvo escalado" (só escala uma coordenada); manter a spec nos bits
que ligam ou desligam blocos grandes. Ele mede instruções/registradores/waves dos dois jeitos com pares reais.
