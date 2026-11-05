#!/bin/bash
# Prepara a prova P3 a partir da SUA instalação do gtav-recomp no PC (já instalada com os seus
# discos) e envia para o aparelho. O conteúdo é código gerado do jogo: fica só no seu PC (DEST,
# fora deste repositório) e no aparelho, e o APK de prova apaga a cópia interna ao terminar.
#
#   tools/p3/make_bundle.sh PASTA_DO_JOGO_NO_PC DEST [N_MAIORES=4] [N_MEDIAS=4] [push]
#
# PASTA_DO_JOGO_NO_PC: a pasta do launcher (tem tools/game_dll/fs, android/include e
#                      data/build/generated/default depois de um Install/Build for Android).
# Seleção: as N maiores unidades geradas (pior caso de RAM) + N medianas (tempo típico).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
G=${1:?pasta do jogo}; DEST=${2:?destino fora do repositório}; NBIG=${3:-4}; NMID=${4:-4}
case "$(realpath -m "$DEST")/" in "$ROOT"/*) echo "DEST não pode ficar dentro do repositório (código do jogo)" >&2; exit 1;; esac
GEN=$G/data/build/generated/default
for d in "$GEN" "$G/tools/game_dll/fs/include" "$G/android/include"; do
  [ -d "$d" ] || { echo "não encontrado: $d (rode Install e Build for Android no PC antes)" >&2; exit 1; }
done
rm -rf "$DEST" && mkdir -p "$DEST/p3/units" "$DEST/p3/gen" "$DEST/p3/fs" "$DEST/p3/android"
mapfile -t all < <(find "$GEN" -maxdepth 1 -name '*.cpp' -printf '%s %p\n' | sort -n | awk '{print $2}')
n=${#all[@]}; [ "$n" -gt 0 ] || { echo "nenhum .cpp em $GEN" >&2; exit 1; }
sel=("${all[@]: -$NBIG}")
mid=$(( n / 2 - NMID / 2 )); [ $mid -lt 0 ] && mid=0
sel+=("${all[@]:$mid:$NMID}")
for f in "${sel[@]}"; do cp "$f" "$DEST/p3/units/"; done
find "$GEN" -maxdepth 1 \( -name '*.h' -o -name '*.hpp' -o -name '*.inc' \) -exec cp {} "$DEST/p3/gen/" \;
cp "$G/tools/game_dll/fs/gtav_pch.h" "$G/tools/game_dll/fs/layout_check.h" "$DEST/p3/gen/" 2>/dev/null || true
cp -r "$G/tools/game_dll/fs/include" "$DEST/p3/fs/"
cp -r "$G/android/include" "$DEST/p3/android/"
# Mesmos includes que install.cpp (Android); as demais flags o APK de prova já põe (baseFlags).
cat > "$DEST/p3/flags.txt" <<FLAGS
-isystem
@P3@/android/include
-isystem
@P3@/fs/include
-I
@P3@/gen
FLAGS
[ -n "${JOBS:-}" ] && echo "$JOBS" > "$DEST/p3/jobs.txt"
echo "$n unidades no total; selecionadas ${#sel[@]}:"
for f in "${sel[@]}"; do printf '  %8d KB  %s\n' $(( $(stat -c %s "$f") / 1024 )) "$(basename "$f")"; done
echo "$n" > "$DEST/p3/total_units.txt"
if [ "${5:-}" = push ]; then
  [ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
  X=/sdcard/Android/data/com.gtavport.probe/files
  adb shell mkdir -p "$X" && adb shell rm -rf "$X/p3" && adb push "$DEST/p3" "$X/" >/dev/null
  echo "enviado para $X/p3 — abra o Gtavport Probe; resultado em $X/p3_result.csv"
fi
