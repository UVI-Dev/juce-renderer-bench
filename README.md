# juce-renderer-bench

Tools to compare JUCE's rendering engines on macOS: pixel output and CPU/GPU cost per frame.
They were written while developing the Metal renderer
(https://github.com/UVI-Dev/JUCE/tree/metal-renderer), but they measure any engine the window
offers (CoreGraphics, CoreGraphics Metal Layer, Software, OpenGL, Metal).

## Build

Needs Xcode, CMake 3.22+ and a JUCE checkout. The targets that use the Metal renderer need the
`metal-renderer` branch; the others build with any JUCE 8/9.

```
git clone -b metal-renderer https://github.com/UVI-Dev/JUCE.git ../JUCE
scripts/build.sh ../JUCE
```

Everything lands in `build/` (Release). The scripts that drive apps through accessibility or
capture windows need the terminal to have the Accessibility and Screen Recording permissions.

## What's in it

### RepaintRepro (`repaint_repro/Main.cpp`)

A window with a grid of 1,200 widgets (a rounded rect, an outline and a label each) and N small
areas that repaint every frame, or the whole window with `--full`. It prints the frame rate, how
much was painted compared to what was asked, paint time per frame and CPU use, then checks that the
pixels on screen match a full repaint (stale or cleared pixels outside the dirty rects fail it).

```
build/RepaintRepro_<variant>_artefacts/Release/RepaintRepro_<variant>.app/Contents/MacOS/RepaintRepro_<variant> --spots 2 [--full] [--textcache] [--seconds 6]
```

| Variant | Paint path |
|---|---|
| `CG` | JUCE's default CoreGraphics path (AppKit merges the dirty rects into one bounding box) |
| `MetalLayer` | `JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1` (CoreGraphics into a CAMetalLayer) |
| `JuceMetal` | `JUCE_METAL_RENDERER=1` |
| `GL` | JUCE's OpenGL renderer attached to the content component |

`--textcache` makes each widget keep its laid-out label. Without it, more than 128 different labels
on screen make `Graphics::drawText`'s arrangement cache miss on every frame, and text layout
dominates the frame whatever the engine.

Set `JUCE_METAL_RENDERER_STATS=1` to get the Metal renderer's per-second statistics (CPU paint and
encode time, GPU time, draw calls, fallbacks) on stderr.

### JuceMetalCoverage (`coverage/`)

Renders JUCE's own GraphicsDemo (every test, every clip mode) with the CoreGraphics, Software and
Metal engines of the same window, captures each as WindowServer shows it, and compares them. The
demo's clock is frozen so the three captures show the same frame.

```
build/JuceMetalCoverage_artefacts/Release/JuceMetalCoverage.app/Contents/MacOS/JuceMetalCoverage <output dir> [--rows 0,3,11]
```

It writes `<scenario>_{cg,sw,metal}.png` and a heat map of the differences, and prints the share of
pixels that differ by more than the threshold. Extra scenes, instead of GraphicsDemo:

| Flag | Scene |
|---|---|
| `--parity` | text at a fractional scale, unrounded strokes, blend modes and replace in a layer, many gradients |
| `--parity2` | non-uniform scales and shears, rotated and single-channel images, stretched text |
| `--parity3` | circles, rounded rects, short gradients, radial gradients, oblique lines, rotated rects |
| `--parity4` | text at every 1/20 px offset (glyph placement) |
| `--imagestream` | images rewritten every frame next to the same picture drawn as vectors: `scripts/check_imagestream.py` |
| `--partialstream` | one of 16 cells repainted per frame, its paint number in its colour: `scripts/check_partialstream.py` |
| `--perf <engine>` | runs the GraphicsDemo animation on one engine and prints paint time, fps and CPU per test |

`scripts/compare_runs.py <dir A> <dir B>` diffs two runs made with the same flags (for example
before and after a change).

### DemoRunner sweep

`scripts/demorunner_sweep.sh <DemoRunner.app>` opens GraphicsDemo in DemoRunner and, for each
rendering engine, goes through the 14 tests with an Instruments trace running. It only uses
accessibility (no mouse), and reads the demo's own "Actual FPS" with OCR. Then
`scripts/demorunner_analyze.py <its output dir>` gives, per test and engine, the frame rate and
CPU/GPU milliseconds per frame for the app and for WindowServer (GPU time is the union of each
process's GPU intervals in the trace).

### Power

- `sudo scripts/power_compare.sh [--textcache]`: CPU, GPU and combined power of the machine while
  each RepaintRepro variant runs in front (2 areas, 8 areas, whole window), interleaved twice.
  Compare per frame: CoreGraphics can't hold 60 fps on some of these.
- `sudo scripts/power_log.sh`: one line per second with the time, to run next to anything else.

### Helpers

- `tools/ax.swift`: dumps an app's accessibility tree, presses elements by title, activates or
  resizes its window.
- `tools/ocr.swift`: prints the text found in an image (Vision).

## Results

`results/2026-11-06-M1.md` has the numbers we got on an M1 MacBook Pro.

## License

MIT (see LICENSE). The apps link JUCE, which has its own license.
