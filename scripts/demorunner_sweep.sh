#!/bin/bash
# Usage: scripts/demorunner_sweep.sh <path to DemoRunner.app> [engines...]
# Opens GraphicsDemo in DemoRunner and, for each rendering engine, records one Instruments trace
# (Metal Application + GPU, all processes) while it goes through the 14 GraphicsDemo tests: per
# test, 2 s to settle, a 6 s window (CPU time of DemoRunner and WindowServer), then the demo's
# "Actual FPS" read from a window capture. Drives the UI through accessibility only (the terminal
# needs the Accessibility and Screen Recording permissions). Takes ~3 min per engine; leave the
# machine alone meanwhile. Then: scripts/demorunner_analyze.py <output directory>
R=$(cd "$(dirname "$0")/.." && pwd)
APP=$1; shift
ENGINES=("$@")
[ ${#ENGINES[@]} -eq 0 ] && ENGINES=("CoreGraphics Renderer" "Metal Renderer" "OpenGL Renderer" "Software Renderer" "CoreGraphics Metal Layer Renderer")
OUT=$R/results/demorunner_$(date +%Y%m%d_%H%M%S); mkdir -p "$OUT"
AX=$R/build/tools/ax; OCR=$R/build/tools/ocr
"$APP/Contents/MacOS/DemoRunner" > "$OUT/demorunner.log" 2>&1 &
P=$!; sleep 4
cpu() { ps -o time= -p $1 | awk -F'[:.]' '{ if (NF==3) print $1*60+$2+$3/100; else print $1*3600+$2*60+$3+$4/100 }'; }
current() { $AX dump $P 12 2>/dev/null | grep -A1 "value='Renderer:'" | tail -1 | sed "s/.*value='\(.*\)'/\1/"; }
now() { python3 -c 'import time; print("%.3f" % time.time())'; }
$AX activate $P > /dev/null
$AX press $P "Browse Demos" AXButton > /dev/null; sleep 1
$AX press $P "GUI" AXRow > /dev/null; sleep 1
$AX press $P "GraphicsDemo.h" AXRow > /dev/null; sleep 2
WS=$(pgrep -x WindowServer)
tests=("Paths: Radial Gradients" "Paths: Linear Gradients" "Paths: Solid" "Fill Types: Rectangles" "Paths: Stroked" "Images: RGB" "Images: RGB Tiled" "Images: ARGB" "Images: ARGB Tiled" "Blur" "Glyphs" "SVG" "Lines" "Shapes")
i=0
for engine in "${ENGINES[@]}"; do
  i=$((i+1))
  $AX activate $P > /dev/null
  $AX press $P "Settings" AXButton > /dev/null; sleep 1
  $AX press $P "$(current)" AXPopUpButton > /dev/null; sleep 1
  $AX press $P "$engine" AXMenuItem > /dev/null || echo "!! could not select $engine"; sleep 1
  selected=$(current)
  $AX press $P "Demo" AXButton > /dev/null; sleep 1
  xcrun xctrace record --instrument "Metal Application" --instrument "GPU" --all-processes \
        --time-limit 175s --output "$OUT/engine$i.trace" > "$OUT/engine$i.record.txt" 2>&1 &
  XP=$!; sleep 4
  for t in "${tests[@]}"; do
    $AX press $P "$t" AXRow > /dev/null; sleep 2
    a=$(cpu $P); w=$(cpu $WS); t0=$(now); sleep 6; t1=$(now); b=$(cpu $P); x=$(cpu $WS)
    W=$($AX winid $P | tail -1); screencapture -x -o -l $W "$OUT/capture.png"
    fps=$($OCR "$OUT/capture.png" | grep -i "actual fps" | sed 's/.*: *//')
    echo "$i|$selected|$t|$t0|$t1|$(python3 -c "print(($b-$a)*1000)")|$(python3 -c "print(($x-$w)*1000)")|$fps|$engine" | tee -a "$OUT/windows.txt"
  done
  wait $XP
done
kill $P
echo "Results in $OUT"
