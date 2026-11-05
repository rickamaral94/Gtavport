#!/bin/bash
# Matriz de drivers da Etapa 0.5, em ordem palindrômica (A B C C B A A B C): cada driver é medido
# antes e depois dos outros, 3 vezes, para que aquecimento e deriva não favoreçam ninguém.
#   FAN_MODE=... PERF_MODE=... tools/bench/matrix.sh CENA SECS WARMUP system turnip-x turnip-y
# Cada execução vira bench/results/e05_<CENA>_<driver>_r<N>. Cache de pipeline já é separado por
# driver (patch 0003); para cache frio use COLD_CACHE=1 (apaga só o arquivo do driver da vez).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
scene=${1:?cena}; secs=${2:?secs}; warm=${3:?warmup}; shift 3
drivers=("$@"); [ ${#drivers[@]} -ge 1 ] || { echo "liste os drivers" >&2; exit 1; }
order=("${drivers[@]}")
for ((i=${#drivers[@]}-1; i>=0; i--)); do order+=("${drivers[i]}"); done
order+=("${drivers[@]}")
declare -A n
for d in "${order[@]}"; do
  n[$d]=$(( ${n[$d]:-0} + 1 ))
  label="e05_${scene}_${d}_r${n[$d]}"
  echo "== $label"
  "$ROOT/tools/bench/run_scene.sh" "$label" "$secs" "$warm" "env:ODIN2_VK_DRIVER=$d" || echo "falhou: $label"
done
echo; echo "| execução | fps méd | 1 % low | p99 ms | bateria °C ini/fim | GPU MHz | RSS MB | notas |"
echo "|---|---|---|---|---|---|---|---|"
for d in "${drivers[@]}"; do cat "$ROOT"/bench/results/e05_"${scene}"_"${d}"_r*/summary.md 2>/dev/null; done
