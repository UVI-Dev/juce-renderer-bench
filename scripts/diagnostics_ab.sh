#!/bin/bash
# Usage: scripts/diagnostics_ab.sh <path to DemoRunner.app> "<example>" [rounds] [engine A] [engine B]
# For examples whose timings move a lot from one moment to the next: alternates two rendering
# engines on one ComponentDiagnosticsDemo example ("54: Widgets demo" for instance), starting a
# fresh DemoRunner each time, and prints per run six readings of the demo's "total (median)" one
# second apart and DemoRunner's CPU time (ms per second). Defaults: 4 rounds, CoreGraphics
# Renderer vs Metal Renderer. Drives the UI through accessibility only.
R=$(cd "$(dirname "$0")/.." && pwd)
APP=$1; ITEM=$2; ROUNDS=${3:-4}
A=${4:-CoreGraphics Renderer}; B=${5:-Metal Renderer}
AX=$R/build/tools/ax; OCR=$R/build/tools/ocr
CAPTURE=$(mktemp -t diagnostics_ab).png
cpu() { ps -o time= -p $1 | awk -F'[:.]' '{ if (NF==3) print $1*60+$2+$3/100; else print $1*3600+$2*60+$3+$4/100 }'; }
current() { $AX dump $P 12 2>/dev/null | grep -A1 "value='Renderer:'" | tail -1 | sed "s/.*value='\(.*\)'/\1/"; }

for r in $(seq $ROUNDS); do
  for engine in "$A" "$B"; do
    pkill -x DemoRunner; sleep 1
    "$APP/Contents/MacOS/DemoRunner" > /dev/null 2>&1 &
    P=$!; sleep 4
    $AX activate $P > /dev/null
    $AX press $P "Settings" AXButton > /dev/null; sleep 1
    [ "$(current)" != "$engine" ] && { $AX press $P "$(current)" AXPopUpButton > /dev/null; sleep 1
                                       $AX press $P "$engine" AXMenuItem > /dev/null; sleep 1; }
    selected=$(current)
    $AX press $P "Browse Demos" AXButton > /dev/null; sleep 1
    $AX press $P "GUI" AXRow > /dev/null; sleep 1
    $AX press $P "ComponentDiagnosticsDemo.h" AXRow > /dev/null; sleep 2
    $AX press $P "1: Cheap" AXPopUpButton > /dev/null; sleep 0.7
    $AX press $P "$ITEM" AXMenuItem > /dev/null; sleep 3
    a=$(cpu $P); t0=$(python3 -c 'import time; print(time.time())'); readings=""
    for k in 1 2 3 4 5 6; do
      W=$($AX winid $P | tail -1); screencapture -x -o -l $W "$CAPTURE"
      readings="$readings $($OCR "$CAPTURE" | grep -E '^[0-9]+\.[0-9]+ ms$' | sed -n 2p | sed 's/ ms//')"
      sleep 1
    done
    b=$(cpu $P)
    t1=$(python3 -c 'import time; print(time.time())')
    echo "$selected |$readings | CPU $(python3 -c "print(round(($b-$a)*1000/($t1-$t0)))") ms/s"
    kill $P 2>/dev/null; wait $P 2>/dev/null
  done
done
rm -f "$CAPTURE"
