#!/bin/bash
# APK de prova (P1/P2/P3) sem Gradle: JNI + clang/lld do aparelho + headers internos do clang.
#   android-ondevice/probe/build.sh [install]   -> build/probe/gtavport-probe.apk
# Pré-requisito: work/llvm (upstream tools/android/build_llvm.sh, LLVM 19.1.5 para aarch64-android).
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
: "${ANDROID_NDK_HOME:?defina ANDROID_NDK_HOME (NDK r29)}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
P=$ROOT/android-ondevice/probe
OUT=$ROOT/build/probe
LLVM=$ROOT/work/llvm/build/android-llvm
BT=$ANDROID_HOME/build-tools/36.0.0
JAR=$ANDROID_HOME/platforms/android-36/android.jar
NDKBIN=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
KEY=$HOME/android-tools/debug.keystore

for f in "$LLVM/out/clang" "$LLVM/out/lld"; do
  [ -f "$f" ] || { echo "falta $f: rode work/llvm/tools/android/build_llvm.sh" >&2; exit 1; }
done
RES=$(ls -d "$LLVM"/android/lib/clang/*/include 2>/dev/null | head -1)
[ -n "$RES" ] && [ -f "$RES/arm_neon.h" ] || { echo "headers internos do clang (com arm_neon.h) não encontrados em $LLVM/android/lib/clang" >&2; exit 1; }

rm -rf "$OUT" && mkdir -p "$OUT"/{classes,stage/lib/arm64-v8a,assets}
cd "$OUT"
"$NDKBIN/aarch64-linux-android30-clang++" -shared -fPIC -O2 -std=c++17 -static-libstdc++ \
  -Wl,-z,max-page-size=16384 -o stage/lib/arm64-v8a/libodin2probe.so "$P/jni/probe_jni.cpp"
# Android só executa arquivos de nativeLibraryDir, e só extrai para lá o que se chama lib*.so.
cp "$LLVM/out/clang" stage/lib/arm64-v8a/libclang.so
cp "$LLVM/out/lld" stage/lib/arm64-v8a/libld.lld.so
(cd "$(dirname "$RES")" && zip -q -r -X "$OUT/assets/clang-resource.zip" include)

javac --release 17 -nowarn -classpath "$JAR" -d classes $(find "$P/java" -name '*.java') 2>&1 | grep -v "^Note:\|JAVA_TOOL_OPTIONS" || true
"$BT/d8" --release --min-api 30 --lib "$JAR" --output stage $(find classes -name '*.class')
"$BT/aapt2" link -o base.apk --manifest "$P/AndroidManifest.xml" -I "$JAR" -A assets \
  --min-sdk-version 30 --target-sdk-version 34
cp base.apk unaligned.apk
(cd stage && zip -q -X ../unaligned.apk classes.dex && zip -q -X -0 -r ../unaligned.apk lib)
"$BT/zipalign" -f -P 16 4 unaligned.apk aligned.apk
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out gtavport-probe.apk aligned.apk
rm -f unaligned.apk aligned.apk base.apk
ls -la gtavport-probe.apk
[ "${1:-}" = install ] && adb install -r gtavport-probe.apk || true
