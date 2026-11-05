#!/bin/bash
# Compila os passes de odin2_post.hlsl com o DXC do upstream e roda post_test.c no lavapipe (Mesa).
# Requisitos: tools/prepare.sh, libvulkan-dev, mesa-vulkan-drivers (lavapipe).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
P=$ROOT/odin2/post
D=$ROOT/work/gtav/thirdparty/XenosRecomp/thirdparty/dxc-bin
O=$ROOT/build/post-test
mkdir -p "$O"
for e in vs_6_0:VSPost ps_6_0:PSFxaa ps_6_0:PSSmaaEdge ps_6_0:PSSmaaWeight ps_6_0:PSSmaaBlend ps_6_0:PSEasu ps_6_0:PSRcas ps_6_0:PSSgsr1; do
  LD_LIBRARY_PATH=$D/lib/x64 "$D/bin/x64/dxc-linux" -T "${e%%:*}" -E "${e##*:}" -spirv -fspv-target-env=vulkan1.1 \
    -I "$P" -Fo "$O/${e##*:}.spv" "$P/odin2_post.hlsl"
  spirv-val --target-env vulkan1.1 "$O/${e##*:}.spv"
done
cc -O1 -o "$O/post_test" "$P/test/post_test.c" -lvulkan -lm
ICD=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json | head -1)
# Opcional: shaders GLSL do BOTWRecompAndroid para comparar o port (BOTW_SRC=caminho do clone).
REF=
if [ -n "${BOTW_SRC:-}" ] && command -v glslangValidator >/dev/null; then
  REF=$O/botw
  mkdir -p "$REF"
  python3 -I "$P/test/extract_botw.py" "$BOTW_SRC/runtime/nvn/vulkan/shaders.c" "$REF"
  for f in post.vert fxaa.frag easu.frag rcas.frag sgsr1.frag; do
    glslangValidator -V --target-env vulkan1.1 -o "$REF/$f.spv" "$REF/$f" >/dev/null
  done
fi
VK_ICD_FILENAMES=$ICD VK_DRIVER_FILES=$ICD "$O/post_test" "$O" $REF
