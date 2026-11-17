# Investigação: listras em superfícies perto da câmera (17/11)

Projeto: Gtavport, recompilação estática do GTA V de Xbox 360 (TU27) com ReXGlue, renderer nativo Vulkan
(plume), shaders traduzidos pelo XenosRecomp para HLSL, depois dxc para SPIR-V e passadas Adreno no aparelho.
Alvo: AYN Odin 2 Portal Pro (Adreno 740), sem root.

## Sintoma

- Aparece em troncos, na parede do trailer, na neve do chão e num pilar do cemitério (prólogo, North Yankton).
- É uma textura "esticada": a cor fica constante ao longo de um eixo da textura e varia só no outro. No tronco as
  listras são horizontais; no pilar, verticais; na estrada, radiais.
- Só em superfícies a poucos metros da câmera. Os objetos distantes ficam corretos. Na mesma foto, um tronco pode
  sair certo e o vizinho listrado.

## O que já foi descartado (medido no Odin)

| Hipótese | Teste | Resultado |
|---|---|---|
| Driver (Turnip) | Driver do sistema Qualcomm, mesma cena (15/11) | listras nos dois: **não é driver** |
| Tradução sem máquina de estados (0041) | `XENOSRECOMP_PC_LOOP=1`, 283 shaders retraduzidos (15/11) | listras continuam |
| Resolução 1080p | Rick: "já existia antes", nas versões 0.2.x a 720p/0.75 | **não é escala** |
| Filtro anisotrópico | anisotropia 1 (rodada de 16/11, 05:11) | listras continuam |
| UBO lido fora do bloco (a0 negativo) | v0.4.2 com o clamp 0050, 2670 shaders retraduzidos (17/11) | listras continuam |

## Hipóteses que restam e o teste que separa cada uma

1. **A, mip 0 corrompido** (dados ou decodificação do nível de maior resolução, o único amostrado de perto).
   Teste: menu Start > Performance > Caçar shader > "Teste A: pular o mip 0" (`--gtav_native_texture_min_lod=1`,
   vale na hora). Se as listras somem, a causa está em `textures.cpp` (DecodeGuestTexture, mip 0) ou no
   streaming do nível alto.
2. **B, dados desatualizados** (textura ou buffer de vértices reescrito pelo streaming e conferido só por
   amostragem; o Android não tem write-watch). Teste: "Teste B: conferir texturas e buffers inteiros todo
   quadro" (`--gtav_native_exact_cache`). Pouco provável: uma cópia velha se corrige em até 4 quadros (texturas)
   ou 8 quadros (buffers), e as listras ficam.
3. **C, coordenada de textura ou shader** (um componente de UV degenerado: formato de vértice do modelo de perto,
   ou tradução de um tfetch). Teste: Caçar shader > G-buffer > Próximo até a superfície listrada mudar. Com o hash
   em mãos, ler o HLSL e o SPIR-V daquele shader (`GTAVPORT_PIPELINE_SPIRV=<hash>`).

O teste A decide entre A e C. O B fecha a possibilidade de dados velhos. Os dois levam segundos na cena.

## Melhorias propostas (seção 4 do pedido)

| Ideia | Resolve | Onde entra | Como medir | Complexidade |
|---|---|---|---|---|
| Captura de um quadro (draws do G-buffer com hash do ps/vs, formatos de textura, dimensões e mip) sob demanda | Localiza o primeiro draw incorreto sem ir shader a shader | `backend.cpp` (já existe `gtav_native_msaa_draws` como modelo) + botão no menu | Um quadro com o tronco listrado deve listar o draw e a textura | média |
| Replay determinístico de entradas | Repetir a mesma rota (cemitério, trem) entre builds e comparar imagem e tempo de quadro | Camada de controles (SDL) gravando e reproduzindo eventos com o tempo do jogo | Mesmo trecho com p95 e captura de tela iguais em duas execuções da mesma build | alta (o jogo tem RNG e streaming assíncronos) |
| Retradução de shaders em segundo plano antes de jogar | Elimina os ~3 min de quadros travados após uma atualização do tradutor (v0.4.2: 2670 shaders) | Tela do app após instalar a atualização: rodar o shader_compiler sobre o cache antes de abrir o jogo | Primeiro boot pós-atualização: quadros > 100 ms (hoje 535 em 10 min) | média |
| Limite térmico automático em 1080p | 85 °C e queda de clock aos ~7 min sem limite (17/11) | Configurações: sugerir 30/45 fps em 150% | Sessão de 10 min: clock da GPU e p95 estáveis | baixa |
| Janela de instrumentação curta ao redor de um spike | Capturar CPU/GPU/esperas só nos 2 s em volta de um quadro > 100 ms | odin2_perf (o stall probe já existe) | Custo fora da captura ~0 e o relatório traz o spike | baixa/média |

As duas de baixo custo que ajudam a validar esta rodada (testes A/B ao vivo e Caçar shader) já entraram
(commits `8acd2c7`, `8931e4c`).
