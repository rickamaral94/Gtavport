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
# A libgtav_fast.so só pode importar da libgtav_game.so as funções do jogo (sub_*, gtavfs_*, rex_*). Um símbolo
# gtav::native indefinido (ex.: função nossa dentro de namespace anônimo) passa no link de .so, mas o dlopen no
# aparelho falha ("cannot locate symbol"), porque a libgtav_game.so de lá foi compilada sem ele.
NM="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm"
bad=$("$NM" -D --undefined-only "$B/libgtav_fast.so" | awk '{print $NF}' | grep -E '^_ZN?K?4gtav6native' || true)
if [ -n "$bad" ]; then
  echo "ERRO: libgtav_fast.so importa símbolos gtav::native (o dlopen falharia no aparelho):" >&2
  echo "$bad" >&2
  exit 1
fi
# Todo símbolo que a libgtav_fast.so importa (fora os do jogo) precisa existir nas bibliotecas que ela declara, na API
# mínima do app (30). Um que não existe passa no link (stub de outra lib ou API mais nova) e o dlopen falha no aparelho:
# v0.5.0, ANativeWindow_setFrameRate (fica na libnativewindow.so, não na libandroid.so).
T=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64 SR=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android
DEFS=$(mktemp)
for f in "$SR"/30/{libandroid,liblog,libm,libdl,libc,libOpenSLES}.so "$SR/libc++_shared.so" "$SDK/lib/librexruntime.so" \
         "$SDK/lib/librexgpu-xenos.so"; do "$NM" -D --defined-only "$f" | awk '{print $NF}'; done | sed 's/@.*//' | sort -u > "$DEFS"
missing=$("$NM" -D --undefined-only "$B/libgtav_fast.so" | awk '$1=="U"{print $2}' | sed 's/@.*//' | sort -u |
          grep -vE '^(sub_|__imp__|gtavfs_|rex_|gtav_game_)' | comm -23 - "$DEFS" || true)
rm -f "$DEFS"
if [ -n "$missing" ]; then
  echo "ERRO: libgtav_fast.so importa símbolos que as bibliotecas dela (API 30) não têm (o dlopen falharia no aparelho):" >&2
  echo "$missing" >&2
  exit 1
fi
ls -la "$B"/*.so
