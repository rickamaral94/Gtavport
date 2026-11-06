#!/bin/bash
# Fase 1 (compilar no aparelho): o que o APK do Gtavport leva a mais para instalar e compilar o jogo sem PC.
#   android-ondevice/device/make_device_bundle.sh   -> build/device/{libs/*.so, assets/gtavport-toolroot.zip}
# libs/   executáveis e bibliotecas para lib/arm64-v8a (Android só executa de nativeLibraryDir):
#         librexglue.so (codegen do ReXGlue), libclang.so, libld.lld.so, libxenosrecomp.so, libdxc.so,
#         libdxcompiler.so, libshader_fix.so, mais libodin2driver.so e os hooks do adrenotools.
# assets/ gtavport-toolroot.zip, extraído no armazenamento interno: tools/game_dll (manifest, partição,
#         expected.txt, headers freestanding), android/{interface,include/simde}, clang-resource,
#         shader_common.android.h. Só nomes, endereços, hashes e código nosso/de terceiros: nada do jogo.
# Pré-requisitos: tools/prepare.sh, SDK Android, tools/build_android_shell.sh, odin2/driver/build.sh,
# work/llvm (clang/lld), work/dxc, android-ondevice/shaders/build_{xenosrecomp,shader_fix}.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
G=$ROOT/work/gtav
OUT=$ROOT/build/device
LLVM=$ROOT/work/llvm/build/android-llvm
need() { [ -e "$1" ] || { echo "falta $1 ($2)" >&2; exit 1; }; }
need "$G/rexglue-sdk/out/install/android-arm64/bin/rexglue" "SDK Android"
need "$G/build/android-dll/game_interface/gtav_game.exports" "tools/build_android_shell.sh"
need "$LLVM/out/clang" "work/llvm"
need "$ROOT/work/dxc/out/libdxcompiler.so" "android-ondevice/shaders/build_dxc.sh"
need "$ROOT/work/shadertools/out/XenosRecomp" "build_xenosrecomp.sh"
need "$ROOT/work/shadertools/out/shader_fix" "build_shader_fix.sh"
need "$ROOT/build/odin2-driver/out/libodin2driver.so" "odin2/driver/build.sh"
rm -rf "$OUT" && mkdir -p "$OUT/libs" "$OUT/assets" "$OUT/toolroot"

L=$OUT/libs
cp "$ROOT"/build/odin2-driver/out/*.so "$L/"
cp "$G/rexglue-sdk/out/install/android-arm64/bin/rexglue" "$L/librexglue.so"
cp "$LLVM/out/clang" "$L/libclang.so"
cp "$LLVM/out/lld" "$L/libld.lld.so"
cp "$ROOT/work/shadertools/out/XenosRecomp" "$L/libxenosrecomp.so"
cp "$ROOT/work/dxc/out/dxc" "$L/libdxc.so"
cp "$ROOT/work/dxc/out/libdxcompiler.so" "$L/"
cp "$ROOT/work/shadertools/out/shader_fix" "$L/libshader_fix.so"
chmod 755 "$L"/*.so

T=$OUT/toolroot
mkdir -p "$T/tools/game_dll/fs/include" "$T/android/interface" "$T/android/include" "$T/clang-resource"
cp "$G/tools/release/expected.txt" "$T/tools/game_dll/expected.txt"
cp "$G/gtav/gtav_manifest.toml" "$T/tools/game_dll/"
cp "$G/tools/game_dll/codegen.partition.json" "$T/tools/game_dll/"
cp "$G/gtav/fs/gtav_pch.h" "$G/gtav/fs/layout_check.h" "$G/gtav/fs/game_dll.cpp" "$T/tools/game_dll/fs/"
cp "$G/gtav/fs/include/stdlib.h" "$G/gtav/fs/include/malloc.h" "$T/tools/game_dll/fs/include/"
cp "$G/build/android-dll/game_interface/gtav_hooks.h" "$G/build/android-dll/game_interface/gtav_game.exports" \
   "$T/android/interface/"
rsync -a --exclude='*.md' --exclude='*.txt' --exclude='.*' "$G/rexglue-sdk/thirdparty/simde/simde/" "$T/android/include/simde/"
cp -r "$(ls -d "$LLVM"/android/lib/clang/*/include | head -1)" "$T/clang-resource/include"
# shader_common.h com as duas trocas do make_spirv.sh (Adreno), como o APK de prova.
sed -e 's/(1ull << semanticIndex)/(1u << semanticIndex)/' \
    -e 's/vk::RawBufferLoad<float2>(\(g_PushConstants\.SharedConstants + \)\([0-9]*\))/float2(vk::RawBufferLoad<float>(\1\2), vk::RawBufferLoad<float>(\1\2 + 4))/' \
    "$G/thirdparty/XenosRecomp/XenosRecomp/shader_common.h" > "$T/shader_common.android.h"
grep -q '1u << semanticIndex' "$T/shader_common.android.h" && ! grep -q 'RawBufferLoad<float2>' "$T/shader_common.android.h"
# Versão: o app re-extrai quando muda.
(cd "$T" && find . -type f | sort | xargs sha256sum | sha256sum | cut -c1-16) > "$T/toolroot.version"
(cd "$T" && zip -q -r -X "$OUT/assets/gtavport-toolroot.zip" .)
cp "$T/toolroot.version" "$OUT/assets/gtavport-toolroot.version"  # o app compara antes de extrair
# Chave da biblioteca do jogo (odin2/abi): o app só pede outra compilação quando ela muda.
SDK_LIB=$G/rexglue-sdk/out/install/android-arm64/lib
ABI=$("$ROOT/odin2/abi/game_abi.sh" "$T/toolroot.version" "$L/librexglue.so" "$L/libclang.so" "$L/libld.lld.so" \
      "$G/build/android-dll/libgtav_fast.so" "$SDK_LIB/librexruntime.so")
{ echo "abi $ABI"; awk -v k="$ABI" '$1 == k { print "compat " $2 }' "$ROOT/odin2/abi/compat-shells.txt"; } \
  > "$OUT/assets/gtavport-game-abi.txt"

# Todo DT_NEEDED das libs novas está no APK ou é do sistema.
R=$(ls "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf 2>/dev/null || ls ~/android-tools/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf)
APKLIBS="libgtav_fast.so librexruntime.so librexgpu-xenos.so libc++_shared.so libmain.so libgtav_setup.so"
PUBLIC="libc.so libm.so libdl.so liblog.so libz.so libandroid.so libvulkan.so libOpenSLES.so libaaudio.so libEGL.so libGLESv2.so libGLESv3.so"
for so in "$L"/*.so; do
  for n in $("$R" -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    [ -f "$L/$n" ] || [[ " $APKLIBS $PUBLIC " == *" $n "* ]] || { echo "$(basename "$so") precisa de $n" >&2; exit 1; }
  done
done
du -sh "$L" "$OUT/assets/gtavport-toolroot.zip"
echo "toolroot $(cat "$T/toolroot.version")"
echo "game abi: $(tr '\n' ' ' < "$OUT/assets/gtavport-game-abi.txt")"
