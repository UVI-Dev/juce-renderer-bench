#!/bin/bash
set -m  # background jobs keep SIGINT, so that "kill -INT" stops xctrace right away
# Usage: scripts/diagnostics_sweep.sh <path to DemoRunner.app> [engines...]
# Opens ComponentDiagnosticsDemo (JUCE 9) in DemoRunner and, for each rendering engine, starts a
# fresh DemoRunner, records one Instruments trace (Metal Application + GPU, all processes) and goes
# through every example of the demo: per example, 2 s to settle, a 4 s window (CPU time of
# DemoRunner and WindowServer), then the demo's own timings read from a window capture (its
# "total (median)" is the paint time on the message thread). Drives the UI through accessibility
# only (the terminal needs the Accessibility and Screen Recording permissions). Takes ~9 min per
# engine; leave the machine alone meanwhile. MAX_EXAMPLES=n only does the first n examples (a quick
# check). Then: scripts/diagnostics_analyze.py <output directory>
R=$(cd "$(dirname "$0")/.." && pwd)
APP=$1; shift
ENGINES=("$@")
[ ${#ENGINES[@]} -eq 0 ] && ENGINES=("CoreGraphics Renderer" "Metal Renderer")
OUT=$R/results/diagnostics_$(date +%Y%m%d_%H%M%S); mkdir -p "$OUT"
AX=$R/build/tools/ax; OCR=$R/build/tools/ocr
WS=$(pgrep -x WindowServer)
cpu() { ps -o time= -p $1 | awk -F'[:.]' '{ if (NF==3) print $1*60+$2+$3/100; else print $1*3600+$2*60+$3+$4/100 }'; }
now() { python3 -c 'import time; print("%.3f" % time.time())'; }
current() { $AX dump $P 12 2>/dev/null | grep -A1 "value='Renderer:'" | tail -1 | sed "s/.*value='\(.*\)'/\1/"; }

i=0
for engine in "${ENGINES[@]}"; do
  i=$((i+1))
  pkill -x DemoRunner; sleep 1
  "$APP/Contents/MacOS/DemoRunner" > "$OUT/engine$i.log" 2>&1 &
  P=$!; sleep 4
  $AX activate $P > /dev/null
  $AX press $P "Settings" AXButton > /dev/null; sleep 1
  [ "$(current)" != "$engine" ] && { $AX press $P "$(current)" AXPopUpButton > /dev/null; sleep 1
                                     $AX press $P "$engine" AXMenuItem > /dev/null || echo "!! could not select $engine"; sleep 1; }
  selected=$(current)
  $AX press $P "Browse Demos" AXButton > /dev/null; sleep 1
  $AX press $P "GUI" AXRow > /dev/null; sleep 1
  $AX press $P "ComponentDiagnosticsDemo.h" AXRow > /dev/null; sleep 2

  # The examples, as listed by the demo's menu (the demo may take a moment to appear).
  items=()
  for attempt in 1 2 3 4 5; do
    $AX press $P "1: Cheap" AXPopUpButton > /dev/null; sleep 1
    while IFS= read -r line; do items+=("$line"); done < <($AX dump $P 20 2>/dev/null | grep "AXMenuItem" | sed -n "s/.*title='\([0-9][0-9]*: [^']*\)'.*/\1/p")
    [ ${#items[@]} -gt 0 ] && break
    sleep 2
  done
  $AX press $P "1: Cheap" AXMenuItem > /dev/null; sleep 1
  [ ${#items[@]} -eq 0 ] && { echo "!! no examples found"; kill $P; exit 1; }
  [ -n "$MAX_EXAMPLES" ] && items=("${items[@]:0:$MAX_EXAMPLES}")

  xcrun xctrace record --instrument "Metal Application" --instrument "GPU" --all-processes \
        --time-limit 1800s --output "$OUT/engine$i.trace" > "$OUT/engine$i.record.txt" 2>&1 &
  XP=$!; sleep 4
  shown="1: Cheap"
  for item in "${items[@]}"; do
    $AX press $P "$shown" AXPopUpButton > /dev/null; sleep 0.7
    $AX press $P "$item" AXMenuItem > /dev/null; shown="$item"; sleep 2
    a=$(cpu $P); w=$(cpu $WS); t0=$(now); sleep 4; t1=$(now); b=$(cpu $P); x=$(cpu $WS)
    W=$($AX winid $P | tail -1); screencapture -x -o -l $W "$OUT/capture.png"
    timings=$($OCR "$OUT/capture.png" | grep -E "^[0-9]+\.[0-9]+ ms$" | sed 's/ ms//' | tr '\n' ',')
    echo "$i|$selected|$item|$t0|$t1|$(python3 -c "print(($b-$a)*1000)")|$(python3 -c "print(($x-$w)*1000)")|$timings|$engine" | tee -a "$OUT/windows.txt"
  done
  kill -INT $XP; wait $XP
  kill $P
done
echo "Results in $OUT"
