#!/bin/bash
# DXC (DirectXShaderCompiler) para aarch64-android: libdxcompiler.so + dxc, a mesma versão que o
# XenosRecomp do upstream usa (dxc-bin, v1.7.2403). Os binários "linux arm64" do dxc-bin são glibc e
# não carregam no bionic, por isso a build cruzada (P4).
#   android-ondevice/shaders/build_dxc.sh [JOBS]  -> work/dxc/out/{libdxcompiler.so,dxc}
set -euo pipefail
source ~/android-tools/env.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=v1.7.2403
JOBS=${1:-3}
W=$ROOT/work/dxc
SRC=$W/DirectXShaderCompiler
mkdir -p "$W"
if [ ! -d "$SRC/.git" ]; then
  git clone -q --depth 1 --branch "$VER" --recurse-submodules --shallow-submodules \
    https://github.com/microsoft/DirectXShaderCompiler "$SRC"
fi
# 1) tablegen do host (a build cruzada executa llvm-tblgen/clang-tblgen aqui).
if [ ! -x "$W/host/bin/clang-tblgen" ]; then
  cmake -S "$SRC" -B "$W/host" -G Ninja -C "$SRC/cmake/caches/PredefinedParams.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DSPIRV_BUILD_TESTS=OFF >/dev/null
  nice cmake --build "$W/host" -j"$JOBS" --target llvm-tblgen clang-tblgen
fi
# 2) build cruzada só do que o XenosRecomp usa: a biblioteca e a CLI, com o backend SPIR-V.
cmake -S "$SRC" -B "$W/android" -G Ninja -C "$SRC/cmake/caches/PredefinedParams.cmake" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_SPIRV_CODEGEN=ON -DSPIRV_BUILD_TESTS=OFF \
  -DLLVM_TABLEGEN="$W/host/bin/llvm-tblgen" -DCLANG_TABLEGEN="$W/host/bin/clang-tblgen" \
  -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF -DLLVM_BUILD_TOOLS=OFF \
  -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384" >/dev/null
nice cmake --build "$W/android" -j"$JOBS" --target dxcompiler dxc
mkdir -p "$W/out"
STRIP=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
"$STRIP" -o "$W/out/libdxcompiler.so" "$W/android/lib/libdxcompiler.so"
"$STRIP" -o "$W/out/dxc" "$W/android/bin/dxc"
ls -la "$W/out"
