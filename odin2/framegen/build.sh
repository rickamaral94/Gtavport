#!/bin/bash
# Compila liblsfg_framegen.so e liblsfg_extract.so para arm64-v8a (odin2/framegen/README.md).
#   odin2/framegen/build.sh  -> build/odin2-framegen/out/*.so
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
: "${ANDROID_NDK_HOME:?defina ANDROID_NDK_HOME (NDK r29)}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
L=$ROOT/work/deps/lsfg-vk-android
B=$ROOT/build/odin2-framegen
cmake -S "$ROOT/odin2/framegen" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DLSFG_DIR="$L" -DVULKAN_HEADERS_DIR="$ROOT/work/deps/plume/contrib/Vulkan-Headers/include" \
  -DLSFG_VK_COMMIT="$(git -C "$L" rev-parse --short=12 HEAD)" \
  -DLSFG_DXBC_COMMIT="$(git -C "$L/thirdparty/dxbc" rev-parse --short=12 HEAD)" >/dev/null
nice cmake --build "$B" -j"${JOBS:-4}" --target lsfg_framegen lsfg_extract
mkdir -p "$B/out"
cp "$B/liblsfg_framegen.so" "$B/liblsfg_extract.so" "$B/out/"
# liblsfg_framegen.so exporta só lsfgfg_* (o volk dele fica escondido do plume).
NM="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
extra=$("$NM" -D --defined-only "$B/out/liblsfg_framegen.so" | awk '{print $NF}' | grep -v '^lsfgfg_' || true)
[ -z "$extra" ] || { echo "ERRO: liblsfg_framegen.so exporta mais que lsfgfg_*:" >&2; echo "$extra" >&2; exit 1; }
ls -la "$B/out"
