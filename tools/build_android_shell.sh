#!/bin/bash
# libgtav_fast.so + libmain.so (shim) + libgtav_setup.so sem código do jogo (GTAV_GAME_DLL=ON),
# o layout do APK público do upstream. Precisa de tools/prepare.sh e do SDK para Android
# (work/gtav/tools/android/build_sdk.sh).   -> work/gtav/build/android-dll
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
: "${ANDROID_NDK_HOME:?defina ANDROID_NDK_HOME (NDK r29)}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=$ROOT/work/gtav
SDK=$G/rexglue-sdk/out/install/android-arm64
[ -d "$SDK/lib/cmake/rexglue" ] || { echo "SDK para Android não instalado: rode work/gtav/tools/android/build_sdk.sh" >&2; exit 1; }
B=$G/build/android-dll
cmake -S "$G/gtav" -B "$B" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release -DGTAV_GAME_DLL=ON \
  -DCMAKE_FIND_ROOT_PATH="$SDK" -Drexglue_DIR="$SDK/lib/cmake/rexglue" -DSDL3_DIR="$SDK/lib/cmake/SDL3" \
  -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON >/dev/null
nice cmake --build "$B" -j"${JOBS:-3}"
ls -la "$B"/*.so
