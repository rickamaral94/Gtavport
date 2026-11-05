# Etapa 0 — Baseline (plano v2; resultados pendentes)

Objetivo: medir o `gtav-recomp` upstream **sem alterações** no AYN Odin 2 Portal Pro e identificar o gargalo principal.
Nada aqui altera código. Os números só podem ser preenchidos no aparelho.

## Build sob teste
- Upstream: `andrewnakas/gtav-recomp` @ `2a76184` (registrar commit exato usado).
- Gerado pelo fluxo oficial do PC ("Build for Android"), mesmo pack para todas as execuções.
- `shader_cache` aquecido com o round-trip do upstream antes de medir (senão a medição é de stutter de tradução).

## Protocolo (regra 4.6)
- **Etapa 0 = upstream intacto, só driver do sistema.** Turnip só na Etapa 0.5, depois que o APK com adrenotools
  reproduzir os números do sistema dentro do ruído.
- Mesma cena, mesmo save, 3 execuções por célula; temperatura inicial do SoC e da bateria registrada (alvo ≤ 32 °C).
- Ventoinha e modo de desempenho do Odin 2 **fixos e anotados** (mesmos em todas as execuções), desconectado do carregador, brilho 50 %.
- Três estados de cache por cena:
  - **frio**: cache de pipeline apagado, primeira execução
  - **aquecido**: 2ª execução em diante
  - **sustentado**: 30 min contínuos (cenas 2 e 4)
- Ruído: a variação entre as 3 execuções define o limiar abaixo do qual uma diferença A/B não conta.
- Matriz de driver (Etapa 0.5): ordem **A/B/BA**, cache de pipeline separado por driver e versão.
- Captura de 120 s por cena via ADL; Perfetto + `simpleperf` em uma das 3 execuções.

## Fontes de tempo
- `dumpsys gfxinfo` **não basta** para o renderer Vulkan nativo. Usar:
  - tempos internos de CPU: `--gtav_wait_profile` (thread principal, gravação, espera)
  - tempo de GPU por frame: timestamps do renderer
  - frametime apresentado: ADL/Perfetto `SurfaceFlinger`
- Telemetria indisponível (sensor ou clock sem permissão sem root, contador ausente no driver) é registrada **explicitamente**
  como "indisponível", nunca deixada em branco nem estimada.

## Cenas
| # | Cena | Como reproduzir |
|---|---|---|
| 1 | Prólogo (Ludendorff) | início do jogo novo |
| 2 | Downtown Los Santos, 12:00 | save próprio, a pé, câmera fixa 60 s + caminhada 60 s |
| 3 | Vinewood, noite com chuva | mesmo save + clima/hora fixos |
| 4 | Rodovia em alta velocidade | Del Perro Fwy, carro esportivo, 120 s |
| 5 | Interior com muitas luzes | ex.: casa do Michael à noite |

## Métricas por execução
fps médio · 1 % low · frametime p50/p99 · temperatura início/fim · clock CPU (prime/gold/silver) e GPU · RAM do processo · crashes · artefatos (com print).
Upstream já expõe `--gtav_wait_profile` e `tools/android/profile.sh` para separar CPU (gravação) de GPU.

## Resultados

| Cena | Driver | Cache | fps méd | 1 % low | p99 ms | T ini/fim °C | GPU MHz | RAM | Notas |
|---|---|---|---|---|---|---|---|---|---|
| 1 | sistema | frio | | | | | | | |

## Gargalo principal
_A preencher: CPU-bound / GPU-bound / shader stutter / térmico, com evidência (perfil). Esse resultado ordena as trilhas de CPU e GPU do plano._
Hipótese a confirmar (do S23 FE): CPU-bound na gravação de comandos do render thread.
