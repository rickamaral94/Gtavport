# Campainha do agente Turnip (Amaral Adreno Tools)

Pedido do Rick em 14/11/2026: o agente que trabalha no Gtavport pode acionar o agente do driver
**Amaral Adreno Tools** (Turnip/Mesa para Adreno, repositório `rickamaral94/Amaral-Adreno-Tools`) sempre
que precisar de um ajuste ou de uma análise do lado do driver.

Este arquivo diz quando acionar, como acionar e o que esperar de volta.

## Quando acionar

Acione quando o problema parecer ser do **driver**, e não do jogo:

- **Falha só no Turnip.** Crash, `VK_ERROR_DEVICE_LOST`, travamento da GPU ou corrupção de imagem que
  aparece com Turnip e não aparece com o driver do sistema (Qualcomm), no mesmo APK e na mesma cena.
- **Desempenho pior só no Turnip.** Um passe, shader ou recurso fica bem mais lento no Turnip que no
  driver do sistema, com A/B.
- **Dúvida de driver.** Qual Turnip usar; se uma variável (`TU_DEBUG=...`, driconf) ajuda; se um recurso
  Vulkan é confiável no Turnip (`shaderInt64`, GPL, sparse, VRS/`coarse_shading`, feedback loop, LRZ).
- **Pedido de build.** Testar um Turnip de laboratório com uma correção específica.

Não acione para bugs do próprio jogo ou do renderer que também acontecem no driver do sistema. Isso
continua sendo do Gtavport. A regra do projeto vale: bug de driver vai para o repositório do driver, não vira
contorno no jogo.

## Como acionar

Comente no **PR de campainha** deste repositório (título `turnip: campainha do agente Turnip (NÃO
MESCLAR)`). O agente Turnip está inscrito nele e é acordado por cada comentário novo.

Comece o comentário com `[gtav]` e traga:

1. **Sintoma**, em uma frase, e o rótulo: validado no Odin, verificado localmente ou pendente de hardware.
2. **Build e driver.** Versão do Gtavport e commit; driver pelo nome e versão que o relatório mostra
   (`driverName` e `driverInfo`). Para pacote Amaral, o nome do ZIP ou o SHA-256. Junte as variáveis do
   driver (`TU_DEBUG` etc.).
3. **A/B contra o driver do sistema**, na mesma cena e com cache de pipelines separado: fps, GPU média e p95,
   quadros acima de 100 ms. Se não houver A/B, diga.
4. **Onde estão os dados.** Caminho no repositório ou anexo do relatório "Medir desempenho"
   (`perf-diag-*.txt` e o zip de `Download/GtavPort/`). Sem logs brutos com dados pessoais.
5. **O que você espera.** Diagnóstico, uma variável para testar, um driver de laboratório ou uma correção.

## O que o agente Turnip devolve

- Resposta no próprio PR de campainha, com análise feita no código do Turnip/Mesa e citando arquivo e commit.
- Quando der para testar sem build, uma variável ou driconf. Exemplo: `TU_DEBUG=sysmem`, `gmem`, `nolrz`,
  `flushall` ou `syncdraw`. Os três últimos são diagnósticos, não ajustes para deixar ligados.
- Quando precisar de driver novo, um **ZIP de laboratório** no formato adrenotools, com SHA-256, identificando
  o que muda em relação à versão publicada. Ele não substitui a release até ser testado no Odin.
- Correção definitiva no repositório Amaral Adreno Tools e, quando couber, enviada ao Mesa upstream.

O agente Turnip não altera código do Gtavport sem pedido. Merge, release e Latest continuam sendo decisão
do Rick nos dois repositórios.

## Contexto que o agente Turnip já levantou (14/11/2026)

- **Driver publicado:** Amaral `mesa-27.0.0-devel-v5.8.17.1` (Latest), Mesa `fd8ef2b`. ELF Standard
  `e64f5306c5c8f52f013f01f3d2627a81704f07ba71ab9518c27be2b209863a96`. Para o A740 do Odin, use o pacote
  Standard. O OneUI é só para aparelhos Samsung.
- **Relevante para o renderer plume.** O mesmo renderer do UnleashedRecomp tem um relato aberto no Mesa,
  [mesa#15792](https://gitlab.freedesktop.org/mesa/mesa/-/issues/15792): corrupção transitória por desenho
  em A725/A750 que some com `TU_DEBUG=flushall` ou `syncdraw`. Ainda não há correção upstream. Se o Gtavport
  vir objetos "piscando" ou se desfazendo por um quadro, teste esses dois valores e acione a campainha com o
  resultado.
- **Coarse shading.** O ganho de "tudo a 2x2" (`coarse_shading=2`) indica custo por pixel. Antes de concluir
  que o passe HDR é caro por natureza, vale o censo de shaders que já está na branch de trabalho. Com o
  shader identificado, o agente Turnip pode olhar o código gerado pelo ir3: laços, spill de registradores,
  `fullsync`.
