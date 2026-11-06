#!/bin/bash
# Usage: scripts/build.sh [JUCE checkout]   (default: ../JUCE next to this repository)
# Configures with Xcode, builds every app in Release, and builds the two helper tools.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
JUCE=${1:-$R/../JUCE}
cmake -G Xcode -S "$R" -B "$R/build" -DJUCE_DIR="$JUCE"
for t in RepaintRepro_CG RepaintRepro_MetalLayer RepaintRepro_JuceMetal RepaintRepro_GL JuceMetalCoverage; do
  xcodebuild -project "$R/build/JuceRendererBench.xcodeproj" -target $t -configuration Release build -quiet
done
mkdir -p "$R/build/tools"
swiftc -O "$R/tools/ax.swift" -o "$R/build/tools/ax"
swiftc -O "$R/tools/ocr.swift" -o "$R/build/tools/ocr"
echo "Built into $R/build"
