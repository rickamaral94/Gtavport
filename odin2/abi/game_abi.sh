#!/bin/bash
# Chave de compatibilidade da libgtav_game.so (a biblioteca do jogo compilada no aparelho).
#   odin2/abi/game_abi.sh <toolroot.version> <librexglue.so> <libclang.so> <libld.lld.so> <libgtav_fast.so> <librexruntime.so>
# A libgtav_game.so depende só do que entra na compilação dela (toolroot: manifest, hooks, headers;
# codegen, clang e lld) e dos símbolos que ela importa da libgtav_fast.so e da librexruntime.so.
# Uma versão nova do app que só mexe no renderer/menu/app tem a mesma chave: o jogo já compilado
# continua valendo e o app não pede outra compilação (antes ele exigia o mesmo hash da libgtav_fast.so).
set -euo pipefail
[ $# -eq 6 ] || { echo "uso: $0 <toolroot.version> <librexglue.so> <libclang.so> <libld.lld.so> <libgtav_fast.so> <librexruntime.so>" >&2; exit 2; }
NM=${LLVM_NM:-}
if [ -z "$NM" ]; then
  for c in "${ANDROID_NDK_HOME:-/nonexistent}"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm \
           ~/android-tools/android-ndk-r29/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm llvm-nm; do
    if command -v "$c" >/dev/null 2>&1; then NM=$c; break; fi
  done
fi
[ -n "$NM" ] || { echo "llvm-nm não encontrado" >&2; exit 1; }
{
  echo "gtavport game abi 1"
  tr -d '[:space:]' < "$1"; echo
  for f in "$2" "$3" "$4"; do sha256sum "$f" | cut -c1-64; done
  for f in "$5" "$6"; do "$NM" -D --defined-only "$f" | awk '{print $NF}' | LC_ALL=C sort; done
} | sha256sum | cut -c1-16
