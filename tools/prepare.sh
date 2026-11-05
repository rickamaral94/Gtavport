#!/bin/bash
# Prepara work/ a partir das versões fixadas: upstream + dependências nos pins, patches do
# upstream e depois a série do Gtavport. Idempotente: recria a árvore do zero a cada execução
# (use KEEP_DEPS=1 para reaproveitar os clones das dependências já baixados).
#   tools/prepare.sh            -> work/gtav (pronto para compilar)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/work
pin() { awk -v n="$1" '$1==n{print $3}' "$ROOT/versions.lock"; }
url() { awk -v n="$1" '$1==n{print $2}' "$ROOT/versions.lock"; }

git -C "$ROOT" submodule update --init upstream
[ "$(git -C "$ROOT/upstream" rev-parse HEAD)" = "$(pin gtav-recomp)" ] || {
  echo "upstream/ não está no pin de versions.lock" >&2; exit 1; }

mkdir -p "$W/deps"
fetch_dep() {  # nome -> work/deps/<nome> limpo no pin
  local n=$1 d=$W/deps/$1
  if [ ! -d "$d/.git" ]; then git clone -q --recursive "$(url "$n")" "$d"; fi
  git -C "$d" fetch -q origin 2>/dev/null || true
  git -C "$d" checkout -q -f "$(pin "$n")"
  git -C "$d" submodule update -q --init --recursive
  git -C "$d" clean -qfdx
}
[ "${KEEP_DEPS:-0}" = 1 ] || rm -rf "$W/deps"
for n in rexglue-sdk plume XenosRecomp; do fetch_dep "$n"; done

rm -rf "$W/gtav"
git -C "$ROOT/upstream" archive --format=tar --prefix=gtav/ HEAD | tar -C "$W" -x
mkdir -p "$W/gtav/thirdparty"
cp -a "$W/deps/rexglue-sdk" "$W/gtav/rexglue-sdk"
cp -a "$W/deps/plume" "$W/gtav/thirdparty/plume"
cp -a "$W/deps/XenosRecomp" "$W/gtav/thirdparty/XenosRecomp"

dir_of() { case $1 in gtav) echo "$W/gtav";; rexglue-sdk) echo "$W/gtav/rexglue-sdk";;
  plume|XenosRecomp) echo "$W/gtav/thirdparty/$1";; *) echo "alvo desconhecido: $1" >&2; exit 1;; esac; }
apply() {  # alvo patch
  local d; d=$(dir_of "$1")
  if ! git -C "$d" apply --whitespace=nowarn "$2" 2>/dev/null; then
    git -C "$d" apply --3way --whitespace=nowarn "$2" || { echo "FALHOU: $2 em $1" >&2; exit 1; }
  fi
  echo "  ok $1 <- $(basename "$2")"
}
echo "patches do upstream:"
apply rexglue-sdk "$W/gtav/rexglue-sdk-gtav-linux.patch"
apply plume "$W/gtav/plume-gtav.patch"
apply XenosRecomp "$W/gtav/xenosrecomp-gtav.patch"
echo "série do Gtavport:"
grep -v '^\s*#' "$ROOT/odin2/patches/series" | while read -r tgt p; do
  [ -n "${tgt:-}" ] && apply "$tgt" "$ROOT/odin2/patches/$p"
done
echo "pronto: $W/gtav"
