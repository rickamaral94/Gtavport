#!/bin/bash
# Uma execução de benchmark no aparelho (Etapa 0 / 0.5). Não altera o jogo: só liga cvars de
# diagnóstico que o upstream já tem e amostra telemetria pelo adb.
#
#   FAN_MODE=smart PERF_MODE=balanced tools/bench/run_scene.sh LABEL [SECS] [WARMUP] [flags...]
#
# LABEL  ex.: e0_cena2_sistema_aquecido_r1  (cena, driver, estado do cache e número da execução)
# SECS   duração da captura (padrão 120); WARMUP: espera antes de capturar (padrão 60, carregar o save)
# Variáveis: FAN_MODE e PERF_MODE (obrigatórias, registradas no resultado), MAX_TEMP_C (espera esfriar
# até essa temperatura da bateria, padrão 32), COLD_CACHE=1 (apaga o cache de pipeline antes),
# PKG (padrão com.gtavrecomp.app).
# Saída: bench/results/LABEL/{meta.txt,raw/} → depois tools/bench/summarize.py bench/results/LABEL
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -f ~/android-tools/env.sh ] && source ~/android-tools/env.sh
PKG=${PKG:-com.gtavrecomp.app}
D=/sdcard/Android/data/$PKG/files
label=${1:?uso: run_scene.sh LABEL [SECS] [WARMUP] [flags...]}; secs=${2:-120}; warm=${3:-60}
shift $(( $# < 3 ? $# : 3 ))
: "${FAN_MODE:?defina FAN_MODE (modo da ventoinha do Odin 2, fixo em todas as execuções)}"
: "${PERF_MODE:?defina PERF_MODE (modo de desempenho do Odin 2)}"
OUT=$ROOT/bench/results/$label
[ -e "$OUT" ] && { echo "já existe: $OUT" >&2; exit 1; }
mkdir -p "$OUT/raw"
sh() { adb shell "$@" 2>/dev/null | tr -d '\r'; }

adb wait-for-device
bat_c() { sh dumpsys battery | awk '/^  temperature:/ {printf "%.1f", $2/10; exit}'; }
max=${MAX_TEMP_C:-32}
for i in $(seq 120); do
  t=$(bat_c); [ -n "$t" ] || { echo "aparelho inacessível" >&2; exit 2; }
  awk -v t="$t" -v m="$max" 'BEGIN{exit !(t<=m)}' && break
  [ "$i" = 1 ] && echo "esfriando: bateria $t °C (alvo ≤ $max)"
  sleep 10
done

{
  echo "label=$label"; echo "date=$(date -Iseconds)"; echo "secs=$secs"; echo "warmup=$warm"
  echo "fan_mode=$FAN_MODE"; echo "perf_mode=$PERF_MODE"; echo "cold_cache=${COLD_CACHE:-0}"
  echo "device=$(sh getprop ro.product.model)"; echo "soc=$(sh getprop ro.soc.model)"
  echo "android=$(sh getprop ro.build.version.release) sdk=$(sh getprop ro.build.version.sdk)"
  echo "build=$(sh getprop ro.build.fingerprint)"
  echo "app_version=$(sh dumpsys package $PKG | awk -F= '/versionName/{print $2; exit}')"
  echo "vulkan_driver=$(sh dumpsys gpu 2>/dev/null | grep -m1 -i 'driver' || echo indisponível)"
  echo "charging=$(sh dumpsys battery | awk '/AC powered|USB powered/{printf "%s ", $0}')"
  echo "flags=$*"
  echo "battery_start_c=$(bat_c)"
} > "$OUT/meta.txt"

# Cvars de diagnóstico do upstream (nenhuma muda o comportamento do jogo, só registra).
DIAG=(--gtav_native_stats=true --gtav_native_pass_gpu_times=true --gtav_wait_profile=true)
"$ROOT/upstream/tools/android/set_args.sh" "${DIAG[@]}" "$@" >/dev/null
if [ "${COLD_CACHE:-0}" = 1 ]; then
  sh "rm -rf $D/pipeline_cache* $D/cache/pipeline* 2>/dev/null"; echo "cache de pipeline apagado" >> "$OUT/meta.txt"
fi

sh am force-stop $PKG; adb logcat -c
sh "mv -f $D/logs/gtav.log $D/logs/prev.log"
sh am start -n $PKG/.GameActivity >/dev/null
echo "aquecendo ${warm}s"; sleep "$warm"
pid=$(sh pidof $PKG); [ -n "$pid" ] || { echo "jogo não está rodando (crash no carregamento?)" | tee -a "$OUT/meta.txt"; adb logcat -d > "$OUT/raw/logcat.txt"; exit 3; }
layer=$(sh dumpsys SurfaceFlinger --list | grep -m1 "$PKG.*GameActivity" || sh dumpsys SurfaceFlinger --list | grep -m1 "$PKG")
echo "pid=$pid" >> "$OUT/meta.txt"; echo "sf_layer=${layer:-indisponível}" >> "$OUT/meta.txt"

# Amostragem a cada 2 s: o SurfaceFlinger guarda só os últimos 128 quadros (~4 s a 30 fps).
echo "capturando ${secs}s"
end=$(( $(date +%s) + secs ))
: > "$OUT/raw/sf_latency.txt"; : > "$OUT/raw/telemetry.txt"
while [ "$(date +%s)" -lt "$end" ]; do
  now=$(date +%s.%N)
  [ -n "$layer" ] && { echo "@ $now"; sh dumpsys SurfaceFlinger --latency "'$layer'"; } >> "$OUT/raw/sf_latency.txt"
  sh "
    echo '@ $now'
    for z in /sys/class/thermal/thermal_zone*; do echo \"therm \$(cat \$z/type) \$(cat \$z/temp 2>/dev/null || echo NA)\"; done
    for p in /sys/devices/system/cpu/cpufreq/policy*; do echo \"cpufreq \${p##*/} \$(cat \$p/scaling_cur_freq 2>/dev/null || echo NA)\"; done
    echo \"gpu_mhz \$(cat /sys/class/kgsl/kgsl-3d0/clock_mhz 2>/dev/null || echo NA)\"
    echo \"gpu_busy \$(cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage 2>/dev/null || echo NA)\"
    echo \"gpu_temp \$(cat /sys/class/kgsl/kgsl-3d0/temp 2>/dev/null || echo NA)\"
    echo \"rss_kb \$(awk '/VmRSS/{print \$2}' /proc/$pid/status 2>/dev/null || echo NA)\"
    echo \"battery_dc \$(dumpsys battery | awk '/^  temperature:/{print \$2; exit}')\"
  " >> "$OUT/raw/telemetry.txt"
  pidof_now=$(sh pidof $PKG); [ "$pidof_now" = "$pid" ] || { echo "crash=durante a captura" >> "$OUT/meta.txt"; break; }
  sleep 2
done
echo "battery_end_c=$(bat_c)" >> "$OUT/meta.txt"
sh "dumpsys meminfo $PKG" > "$OUT/raw/meminfo.txt"
sh am force-stop $PKG
adb pull "$D/logs/gtav.log" "$OUT/raw/gtav.log" >/dev/null 2>&1 || echo "gtav.log indisponível" >> "$OUT/meta.txt"
adb logcat -d > "$OUT/raw/logcat.txt"
grep -E "F DEBUG|F libc" "$OUT/raw/logcat.txt" | head -5 | sed 's/^/crash_log=/' >> "$OUT/meta.txt"
python3 "$ROOT/tools/bench/summarize.py" "$OUT"
