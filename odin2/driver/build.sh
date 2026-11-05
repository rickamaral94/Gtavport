#!/bin/bash
# Compila libodin2driver.so e os hooks do adrenotools para arm64-v8a.
#   odin2/driver/build.sh  -> build/odin2-driver/out/*.so
set -euo pipefail
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build/odin2-driver
cmake -S "$ROOT/odin2/driver" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DADRENOTOOLS_DIR="$ROOT/work/deps/libadrenotools" >/dev/null
cmake --build "$B" --target odin2driver main_hook file_redirect_hook gsl_alloc_hook hook_impl
mkdir -p "$B/out"
find "$B" -name '*.so' -not -path "$B/out/*" -exec cp {} "$B/out/" \;
ls -la "$B/out"
