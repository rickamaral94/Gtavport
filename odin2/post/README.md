# odin2/post — anti-aliasing, upscaler e resolução interna (menu "Video")

Mesmo modelo de opções do BOTWRecompAndroid, no menu do jogo (RB + Back no controle, ou Esc):

| Opção | Valores | Vale |
|---|---|---|
| **Anti-aliasing** (`odin2_antialias`) | Off · FXAA · SMAA 1x | na hora |
| **Upscaler** (`odin2_upscaler`) | Bilinear (original) · AMD FSR 1 (EASU + RCAS) · Snapdragon GSR 1 (edge direction) | na hora |
| **FSR sharpness** (`odin2_fsr_sharpness`) | 0–2 stops (0,2 = padrão da AMD) | na hora |
| **Internal resolution** (`gtav_native_render_scale`, do upstream) | 50 · 60 · 67 · 75 · 85 · 100 % de 1280×720 | no próximo início |

Tudo fica salvo no `gtav_settings.ini`, como as demais opções do menu.

## Como funciona
O renderer do upstream apresenta o front buffer do jogo (cena + HUD, 1280×720 × escala interna) com um blit
bilinear até o tamanho na tela (no Odin 2 Portal, 1920×1080). Antes desse blit, `Post::Run` roda:
1. **AA** no tamanho da imagem do jogo: FXAA (1 passe) ou SMAA 1x (bordas → pesos → mistura, preset High);
2. **upscaler** até o tamanho na tela, só quando é ampliação: SGSR 1 (1 passe) ou FSR 1 (EASU + RCAS).

O blit final do upstream apresenta o resultado, agora 1:1. Com **Off + Bilinear**, que é o padrão, `Run` devolve o
próprio front buffer e o caminho é exatamente o do upstream. Assim o baseline e a validação da Etapa 0.5
continuam comparáveis.

**Limite desta versão:** o HUD faz parte da imagem do jogo, então AA e upscaler passam por ele também. HUD em
resolução nativa (upscale só da cena, como o `scene_upscale` do BOTW) exige achar no renderer o passe onde a cena
termina, e isso é trabalho da Fase 3.

**Sem 2×:** a escala interna vai até 100 %. O upstream limita a 0,5–1,0, e acima disso não foi validado (shaders de
tiles por `FragCoord`). Além disso, 2× são 4× os pixels numa GPU que já é o gargalo. Se fizer sentido depois do
baseline, entra como experimental e medido.

## Fontes e licenças
| Passe | Origem | Licença |
|---|---|---|
| FXAA | port do `fxaa.frag` do BOTWRecompAndroid (no estilo do FXAA 3.11 de Timothy Lottes) | MIT (BOTWRecompAndroid) |
| SMAA 1x | `third_party/smaa/SMAA.hlsl` de referência + `AreaTex.h`/`SearchTex.h` (Jimenez et al.) | MIT (`third_party/smaa/LICENSE.txt`) |
| FSR 1 EASU/RCAS | port do `easu.frag`/`rcas.frag` do BOTWRecompAndroid (AMD FidelityFX Super Resolution 1) | MIT |
| SGSR 1 | port do `sgsr1.frag` do BOTWRecompAndroid (`sgsr1_shader_mobile_edge_direction`, Qualcomm) | BSD-3-Clause |

## Testes (`odin2/post/test/run.sh`)
Rodam os passes numa GPU Vulkan de software (lavapipe) com o mesmo contrato do jogo (heaps bindless, push de 64 bytes):
- imagem lisa → identidade (0/255) em todos os passes;
- gradiente suave → upscalers a menos de 1/255 (média) do blit bilinear do upstream;
- degrau → borda exatamente onde o bilinear põe (sem meio pixel de deslocamento);
- borda serrilhada → FXAA/SMAA mudam só pixels a até 3 px da borda;
- com `BOTW_SRC=<clone do BOTWRecompAndroid>`: os shaders GLSL do BOTW rodam lado a lado → **FXAA, FSR 1 e SGSR 1
  idênticos bit a bit** nas quatro imagens.

Ainda não medido: custo em ms e qualidade no Adreno 740 (ADL, com o aparelho).
