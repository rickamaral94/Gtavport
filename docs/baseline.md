# Fase 0 — Baseline (plano; resultados pendentes)

Objetivo: medir o `gtav-recomp` upstream **sem alterações** no AYN Odin 2 Portal Pro e identificar o gargalo principal.
Nada aqui altera código. Os números só podem ser preenchidos no aparelho.

## Build sob teste
- Upstream: `andrewnakas/gtav-recomp` @ `2a76184` (registrar commit exato usado).
- Gerado pelo fluxo oficial do PC ("Build for Android"), mesmo pack para todas as execuções.
- `shader_cache` aquecido com o round-trip do upstream antes de medir (senão a medição é de stutter de tradução).

## Protocolo (regra 4.6)
- Mesma cena, mesmo save, 3 execuções por célula, temperatura inicial de bateria/SoC registrada (alvo ≤ 32 °C).
- Ventoinha do Odin 2 no modo **Smart/Sport** fixo, desconectado do carregador, brilho 50 %.
- Captura de 120 s por cena via ADL; Perfetto + `simpleperf` numa das 3 execuções.
- Matriz de driver: sistema (anotar versão) · Turnip Mr. Purple (versão) · Turnip Amaral-Adreno-Tools (versão).
  Observação: o APK upstream ainda **não** carrega driver via adrenotools; se não houver forma de trocar o driver sem patch, medir só o do sistema e registrar como limitação.

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

| Cena | Driver | fps méd | 1 % low | p99 ms | T ini/fim °C | GPU MHz | RAM | Notas |
|---|---|---|---|---|---|---|---|---|
| 1 | sistema | | | | | | | |

## Gargalo principal
_A preencher: CPU-bound / GPU-bound / shader stutter / térmico, com evidência (perfil)._
Hipótese a confirmar (do S23 FE): CPU-bound na gravação de comandos do render thread.
