#!/bin/bash
# XenosRecomp para aarch64-android (P4).  Pré-requisitos: tools/prepare.sh e build_dxc.sh.
#   android-ondevice/shaders/build_xenosrecomp.sh  -> work/shadertools/out/XenosRecomp
# DXC_LIB=caminho/para/libdxcompiler.so sobrescreve o do build_dxc.sh (ex.: um stub para testar só
# a compilação).
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
: "${ANDROID_NDK_HOME:?defina ANDROID_NDK_HOME (NDK r29)}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
XENOS=$ROOT/work/gtav/thirdparty/XenosRecomp
DXC_LIB=${DXC_LIB:-$ROOT/work/dxc/out/libdxcompiler.so}
DXC_INC=${DXC_INC:-$XENOS/thirdparty/dxc-bin/inc}
B=$ROOT/work/shadertools/build
[ -d "$XENOS" ] || { echo "rode tools/prepare.sh" >&2; exit 1; }
[ -f "$DXC_LIB" ] || { echo "falta $DXC_LIB: rode build_dxc.sh" >&2; exit 1; }
cmake -S "$ROOT/android-ondevice/shaders" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DXENOS_DIR="$XENOS" -DDXC_LIB="$DXC_LIB" -DDXC_INC="$DXC_INC" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384" >/dev/null
nice cmake --build "$B" --target XenosRecomp -j"${JOBS:-2}"
mkdir -p "$ROOT/work/shadertools/out"
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" -o "$ROOT/work/shadertools/out/XenosRecomp" \
  "$B/xenosrecomp/XenosRecomp/XenosRecomp"
ls -la "$ROOT/work/shadertools/out"
