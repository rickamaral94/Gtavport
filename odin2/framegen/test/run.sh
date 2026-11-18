#!/bin/bash
# Teste no PC da extração do Lossless.dll (lsfg_extract.cpp + dxbc + pe-parse) com entradas inválidas.
# Precisa de work/deps/lsfg-vk-android (tools/prepare.sh) e do plume em work/deps (cabeçalhos Vulkan).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
L=$ROOT/work/deps/lsfg-vk-android P=$L/thirdparty/pe-parse/pe-parser-library/src F=$ROOT/odin2/framegen
OUT=$(mktemp -d)
${CXX:-g++} -std=c++17 -O1 -w -I "$F" -I "$ROOT/odin2/driver" -I "$L/thirdparty/pe-parse/pe-parser-library/include" \
  -I "$L/thirdparty/dxbc/include/dxbc" -I "$L/thirdparty/dxbc/include/spirv" -I "$L/thirdparty/dxbc/include/util" \
  -I "$L/thirdparty/dxbc/include/dxvk" -I "$ROOT/work/deps/plume/contrib/Vulkan-Headers/include" \
  -DLSFG_VK_COMMIT='"test"' -DLSFG_DXBC_COMMIT='"test"' "$F/test/extract_test.cpp" "$F/lsfg_extract.cpp" "$F/lsfg_translate.cpp" \
  "$P/buffer.cpp" "$P/parse.cpp" "$P/unicode_codecvt.cpp" "$L"/thirdparty/dxbc/src/{dxbc,spirv,util}/*.cpp -o "$OUT/t"
mkdir -p "$OUT/root"
"$OUT/t" "$OUT/root"
