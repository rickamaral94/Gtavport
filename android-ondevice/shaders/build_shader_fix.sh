#!/bin/bash
# shader_fix do upstream (tools/android/shader_fix: passes ubo_constants e lower_int64 em C++)
# para aarch64-android, para a cadeia de shaders rodar no aparelho sem Python (P4).
#   android-ondevice/shaders/build_shader_fix.sh  -> work/shadertools/out/shader_fix
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
: "${ANDROID_NDK_HOME:?defina ANDROID_NDK_HOME (NDK r29)}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
G=$ROOT/work/gtav
B=$ROOT/work/shadertools/shader_fix
[ -d "$G/tools/android/shader_fix" ] || { echo "rode tools/prepare.sh" >&2; exit 1; }
cmake -S "$G/tools/android/shader_fix" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384" >/dev/null
nice cmake --build "$B" --target shader_fix -j"${JOBS:-2}"
mkdir -p "$ROOT/work/shadertools/out"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" -o "$ROOT/work/shadertools/out/shader_fix" "$B/shader_fix"
ls -la "$ROOT/work/shadertools/out/shader_fix"
