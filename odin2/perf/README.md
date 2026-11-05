# odin2/perf — seção "Performance" e logs de desempenho

## No jogo: menu (RB + Back) > Performance
- **Driver em uso:** sistema ou Turnip, com `driverName`, `driverInfo` (versão do Mesa/Turnip ou do driver da
  Qualcomm) e versão Vulkan, além das variáveis do driver (`TU_DEBUG` etc.). O driver mostrado é o que carregou de
  fato, não o pedido.
- **Ao vivo** (janela de ~120 quadros): fps, 1 % low, p99, engasgos > 50 ms (e quantos com shader/pipeline novo),
  gráficos de frametime e de tempo de GPU.
- **Por quadro:** gravação de comandos na CPU (thread de render), tempo de GPU (timestamps), espera pela GPU (fence)
  e espera pelo jogo (thread de render ociosa).
- **Gargalo provável**, com a fração dos quadros em cada classe:

  | classe | regra (por quadro) |
  |---|---|
  | limite de fps/vsync | intervalo a ±5 % do alvo do `gtav_fps_limit` |
  | GPU | GPU ≥ 85 % do intervalo, ou espera pela fence ≥ 25 % |
  | CPU (thread de render) | gravação ≥ 75 % do intervalo |
  | CPU (threads do jogo) | render esperando o jogo ≥ 40 % do intervalo |
  | misto | nenhum dominante |

  Avisos à parte: **térmico** (margem térmica ≥ 0,85 ou clock da GPU < 70 % do maior visto) e **stutter de shader**.
- **Sensores** (1×/s, thread própria): clock e uso da GPU (kgsl), temperatura da GPU, clocks das CPUs prime,
  performance e eficiência, margem e status térmico (Thermal API do Android) e RAM do jogo. O que o SELinux
  bloquear aparece como **indisp.**, nunca como zero.
- **GPU por tipo de passe** (opcional): liga o `gtav_native_pass_gpu_times` do upstream e mostra os 8 passes mais caros.
- **Gravar sessão:** CSV por quadro + resumo `.txt` em `files/logs/perf/`. A gravação em disco é feita pela thread
  de sensores, não pela de render.

## No app: "Logs de desempenho"
Abre pelo atalho do ícone, pela tela de setup ou pela tela de driver. Lista as sessões (resumo completo na tela)
e **exporta para `Download/GtavPort/`** um zip com o resumo, o CSV, o `gtav.log`, o status do driver e o
`gtav_settings.ini`. Também apaga sessões.

## No PC
```sh
python3 tools/bench/perf_report.py perf_sistema.csv perf_turnip.csv   # tabela comparativa (markdown)
python3 tools/bench/perf_report.py --json perf_turnip.csv
```
Usa as mesmas regras de classificação do jogo (testes em `tools/bench/tests/test_perf_report.py`). Para comparar
drivers ou opções, siga o protocolo de `docs/baseline.md`: mesma cena, mesma temperatura inicial, 3 gravações,
ordem A/B/BA.

## Estado
Compilado (`libgtav_fast.so` com os patches 0001–0008, NDK r29; Java/dex/manifesto). **Não executado no
aparelho:** quais sensores o SELinux libera no Odin 2 e o custo da coleta ainda precisam ser vistos lá.
