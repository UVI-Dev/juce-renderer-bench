#!/bin/bash
# Usage: sudo scripts/power_log.sh [log file]   -> CPU, GPU and combined power every second,
# with the time, until Ctrl-C (to line up with the timestamps of demorunner_sweep.sh).
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$R/results/power_log.txt}
mkdir -p "$(dirname "$OUT")"; : > "$OUT"; chmod 644 "$OUT"
powermetrics --samplers cpu_power,gpu_power -i 1000 2>/dev/null | while IFS= read -r line; do
  case "$line" in
    "CPU Power"*)      c=${line#*: }; c=${c% mW} ;;
    "GPU Power"*)      g=${line#*: }; g=${g% mW} ;;
    "Combined Power"*) t=${line#*: }; t=${t% mW}
                       echo "$(date +%H:%M:%S) cpu $c gpu $g total $t mW" | tee -a "$OUT" ;;
  esac
done
