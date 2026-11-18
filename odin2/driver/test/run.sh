#!/bin/bash
# Testes no host do SHA-256 usado no relatório do driver (ASan/UBSan).
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/odin2_sha256_test
${CXX:-c++} -std=c++17 -O1 -g -fsanitize=address,undefined "$D/sha256_test.cpp" -o "$OUT"
args=()
for f in "$D/sha256_test.cpp" "$OUT"; do args+=("$f" "$(sha256sum "$f" | cut -d' ' -f1)"); done
"$OUT" "${args[@]}"
