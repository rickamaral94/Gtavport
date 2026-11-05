#!/bin/bash
# Envia um pacote de driver (zip no formato Turnip: meta.json + .so) para o app importar no
# próximo início (odin2/android Odin2Driver). O nome do zip vira o nome do driver.
#   tools/driver/push_driver.sh turnip-amaral-v1.zip      -> env:ODIN2_VK_DRIVER=turnip-amaral-v1
set -euo pipefail
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
PKG=${PKG:-com.gtavrecomp.app}
zip=${1:?uso: push_driver.sh PACOTE.zip}
unzip -l "$zip" | grep -q "meta.json" || { echo "sem meta.json: não é um pacote de driver adrenotools" >&2; exit 1; }
D=/sdcard/Android/data/$PKG/files/drivers_import
adb shell mkdir -p "$D"
adb push "$zip" "$D/" >/dev/null
adb shell chmod -R a+rwX "$D"
name=$(basename "$zip" .zip | sed 's/[^A-Za-z0-9._-]/_/g')
echo "enviado. Será importado no próximo início do jogo. Para usar: env:ODIN2_VK_DRIVER=$name"
