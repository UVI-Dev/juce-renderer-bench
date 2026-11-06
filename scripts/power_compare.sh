#!/bin/bash
# Usage: sudo scripts/power_compare.sh [extra RepaintRepro flags, e.g. --textcache]
# System-wide CPU, GPU and combined power (powermetrics, 1 s samples) while each RepaintRepro
# variant runs in the foreground: 2 small repainting areas, 8, and the whole window, interleaved
# twice, with idle before and after. Don't use the machine while it runs (~5 min).
source "$(dirname "$0")/app.sh"
OUT=$R/results/power_$(date +%Y%m%d_%H%M%S).txt
mkdir -p "$R/results"; : > "$OUT"
U=${SUDO_USER:-$USER}
EXTRA="$*"
measure() { powermetrics --samplers cpu_power,gpu_power -i 1000 -n 5 2>/dev/null \
              | awk -v n="$1" '/^CPU Power/ {c=$3} /^GPU Power/ {g=$3} /^Combined Power/ {print n, c, g, $(NF-1)}' >> "$OUT"; }
run() { # name app args...
  local name=$1 bin; bin=$(app "$2"); shift 2
  sudo -u "$U" "$bin" "$@" $EXTRA --seconds 10 > /dev/null 2>&1 &
  sleep 1
  local pid; pid=$(pgrep -n -f "$bin --")
  [ -n "$pid" ] && sudo -u "$U" "$R/build/tools/ax" activate "$pid" > /dev/null 2>&1
  sleep 2; measure "$name"; wait; sleep 2
}
sleep 3; measure idle
for round in 1 2; do
  for load in "2:--spots 2" "8:--spots 8" "full:--spots 2 --full"; do
    tag=${load%%:*}; args=${load#*:}
    for v in JuceMetal CG GL MetalLayer; do run "${v}_$tag" RepaintRepro_$v $args; done
  done
done
measure idle
chown "$U" "$OUT"
echo "== $OUT (mW, mean of the samples; compare per frame with each app's fps line)"
awk '{c[$1]+=$2; g[$1]+=$3; t[$1]+=$4; n[$1]++}
     END {for (k in n) printf "%-16s cpu %5.0f  gpu %5.0f  total %5.0f  (n=%d)\n", k, c[k]/n[k], g[k]/n[k], t[k]/n[k], n[k]}' "$OUT" | sort
