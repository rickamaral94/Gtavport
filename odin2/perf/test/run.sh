#!/bin/bash
# Testes no host do "Medir desempenho": contas, /proc e símbolos ELF (com ASan/UBSan).
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/odin2_diag_test
${CXX:-c++} -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -rdynamic \
  -I "$D/.." "$D/diag_test.cpp" -ldl -o "$OUT"
"$OUT"
