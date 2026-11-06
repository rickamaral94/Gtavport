#!/bin/bash
# Chave de compatibilidade da libgtav_game.so (a biblioteca do jogo compilada no aparelho).
#   odin2/abi/game_abi.sh <toolroot.version> <libgtav_fast.so> <librexruntime.so>
# Uma libgtav_game.so já compilada continua valendo enquanto não mudarem:
#   - o toolroot (manifest, partição, hooks, headers com que o código do jogo é compilado);
#   - os nomes da interface que ela importa (gen_interface.py do upstream): da libgtav_fast.so os
#     hooks (sub_<endereço>, __imp__sub_*), a ponte gtavfs_* e os rex_* em C; da librexruntime.so
#     todos os exportados (o runtime só muda com o pin do SDK).
# O resto dos exportados da libgtav_fast.so (templates do spdlog/fmt, código do renderer) muda a cada
# versão e o jogo não usa; o compilador também não entra (não muda uma biblioteca já compilada).
# Antes o app exigia o mesmo hash da libgtav_fast.so: toda versão nova pedia outra compilação.
set -euo pipefail
[ $# -eq 3 ] || { echo "uso: $0 <toolroot.version> <libgtav_fast.so> <librexruntime.so>" >&2; exit 2; }
NM=${LLVM_NM:-}
if [ -z "$NM" ]; then
  for c in "${ANDROID_NDK_HOME:-/nonexistent}"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm \
           ~/android-tools/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm llvm-nm; do
    if command -v "$c" >/dev/null 2>&1; then NM=$c; break; fi
  done
fi
[ -n "$NM" ] || { echo "llvm-nm não encontrado" >&2; exit 1; }
{
  echo "gtavport game abi 3"
  tr -d '[:space:]' < "$1"; echo
  "$NM" -D --defined-only "$2" | awk '{print $NF}' | grep -E '^(__imp__)?sub_[0-9A-F]{8}$|^gtavfs_[a-z_]+$|^rex_[a-z_]+$' | LC_ALL=C sort
  "$NM" -D --defined-only "$3" | awk '{print $NF}' | LC_ALL=C sort
} | sha256sum | cut -c1-16
