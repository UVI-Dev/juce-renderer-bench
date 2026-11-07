// Coverage check for the JUCE_METAL_RENDERER engine, using JUCE's own GraphicsDemo.
//
// For every GraphicsDemo test (paths, gradients, strokes, images, tiled images, blur, glyphs, SVG,
// lines, shapes) and every clip mode of the demo (none, rectangle, path, image), the same window is
// rendered with the CoreGraphics, Software and Metal engines of the peer, captured at backing
// resolution and compared. Animations are switched off so the three captures show the same frame.
//
// Usage: JuceMetalCoverage <outputDir>
// Prints one line per scenario; writes <scenario>_{cg,sw,metal}.png plus a diff heat map.
// Run with JUCE_METAL_RENDERER_STATS=1 to get the per-second fallback statistics on stderr.

#include <JuceHeader.h>

#include "WindowCapture.h"

// The demo animates from the wall clock (BouncingNumber, the SVG reload timer, the fps text).
// Its view of the clock is frozen so that the three engines render the very same frame; the
// harness moves it forward between scenarios.
// In --perf mode the clock runs normally.
namespace { double frozenMs = 1.0e6; bool clockFrozen = true; }

struct FrozenTime : public juce::Time
{
  using juce::Time::Time;
  FrozenTime() = default;
  FrozenTime (const juce::Time& t) : juce::Time (t) {}

  static double getMillisecondCounterHiRes() noexcept
  {
    return clockFrozen ? frozenMs : juce::Time::getMillisecondCounterHiRes();
  }

  static FrozenTime getCurrentTime() noexcept
  {
    return clockFrozen ? FrozenTime ((juce::int64) frozenMs) : FrozenTime (juce::Time::getCurrentTime());
  }
};

#define Time FrozenTime
// Test-only: --perf reads the demo's own paint-time average (a private member).
#define private public
#include "GUI/GraphicsDemo.h"
#undef private
#undef Time

namespace
{
struct Capture
{
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;
};

Capture captureOwnWindow()
{
  Capture c;
  c.pixels = captureOwnWindowPixels (c.width, c.height);
  return c;
}

Image toImage (const Capture& c)
{
  Image img (Image::ARGB, c.width, c.height, false, SoftwareImageType());
  Image::BitmapData bd (img, Image::BitmapData::writeOnly);

  for (int y = 0; y < c.height; ++y)
    std::memcpy (bd.getLinePointer (y), c.pixels.data() + (size_t) (y * c.width), (size_t) c.width * 4);

  return img;
}

void savePng (const Image& img, const File& f)
{
  f.deleteFile();
  FileOutputStream os (f);
  PNGImageFormat().writeImageToStream (img, os);
}

int maxChannelDelta (uint32_t a, uint32_t b) noexcept
{
  int m = 0;

  for (int shift = 0; shift < 24; shift += 8)
    m = std::max (m, std::abs ((int) ((a >> shift) & 0xff) - (int) ((b >> shift) & 0xff)));

  return m;
}

// Share of pixels (title bar excluded) differing by more than 'threshold' on any channel.
struct DiffResult { double percentOver = 0; int maxDelta = 0; Image heatMap; };

DiffResult compare (const Capture& a, const Capture& b, int skipTop, int threshold)
{
  DiffResult r;

  if (a.width != b.width || a.height != b.height || a.pixels.empty())
  {
    r.percentOver = 100.0;
    return r;
  }

  r.heatMap = Image (Image::RGB, a.width, a.height, true, SoftwareImageType());
  Image::BitmapData bd (r.heatMap, Image::BitmapData::writeOnly);
  int64 over = 0, total = 0;

  for (int y = skipTop; y < a.height; ++y)
    for (int x = 0; x < a.width; ++x)
    {
      const auto d = maxChannelDelta (a.pixels[(size_t) (y * a.width + x)], b.pixels[(size_t) (y * a.width + x)]);
      r.maxDelta = std::max (r.maxDelta, d);
      ++total;

      if (d > threshold)
        ++over;

      const auto v = (uint8) std::min (255, d * 4);
      bd.setPixelColour (x, y, Colour (v, (uint8) (d > threshold ? 0 : v / 2), (uint8) 0));
    }

  r.percentOver = total > 0 ? 100.0 * (double) over / (double) total : 0.0;
  return r;
}

// --twopoint: two-point conical (SVG focal) radial gradients, one per cell, covering the branches
// of juce::detail::TwoPointConicalGradient: focal inside, start radius > 0, start circle leaving
// the end circle (transparent outside the cone), end radius 0 (swapped), focal on the end circle
// (close to singular); with pad / reflect / repeat. Each cell fills a rectangle and an ellipse.
struct TwoPointGradientTest final : public Component
{
  TwoPointGradientTest() { setOpaque (true); setSize (750, 750); }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);

    struct Case { Point<float> c0; float r0; Point<float> c1; float r1; ColourGradient::SpreadMethod spread; };
    using S = ColourGradient::SpreadMethod;
    const Case cases[]
    {
      { { 0.40f, 0.45f }, 0.00f, { 0.5f, 0.5f }, 0.40f, S::pad },      // focal inside
      { { 0.35f, 0.40f }, 0.10f, { 0.5f, 0.5f }, 0.40f, S::pad },      // start radius > 0
      { { 0.15f, 0.50f }, 0.20f, { 0.6f, 0.5f }, 0.25f, S::pad },      // start circle leaves the end circle
      { { 0.50f, 0.50f }, 0.40f, { 0.7f, 0.6f }, 0.00f, S::pad },      // end radius 0 (swapped)
      { { 0.10f, 0.50f }, 0.00f, { 0.5f, 0.5f }, 0.40f, S::pad },      // focal on the end circle
      { { 0.40f, 0.45f }, 0.05f, { 0.5f, 0.5f }, 0.15f, S::reflect },
      { { 0.40f, 0.45f }, 0.05f, { 0.5f, 0.5f }, 0.15f, S::repeat },
      { { 0.15f, 0.50f }, 0.20f, { 0.6f, 0.5f }, 0.25f, S::reflect },
      { { 0.30f, 0.30f }, 0.30f, { 0.6f, 0.6f }, 0.10f, S::pad },      // start larger than end
    };

    const auto cell = 250.0f;

    for (int i = 0; i < 9; ++i)
    {
      const auto& k = cases[i];
      const Rectangle<float> area ((float) (i % 3) * cell, (float) (i / 3) * cell, cell, cell);
      const auto at = [&] (Point<float> p) { return area.getPosition() + p * cell; };

      ColourGradient gradient (Colours::red, at (k.c0), Colours::blue, at (k.c1), true);
      gradient.addColour (0.5, Colours::yellow.withAlpha (0.8f));
      gradient.startRadius = k.r0 * cell;
      gradient.endRadius = k.r1 * cell;
      gradient.spreadMethod = k.spread;
      g.setGradientFill (gradient);
      g.fillRect (area.reduced (8.0f).withHeight (cell * 0.45f));

      Path ellipse;
      ellipse.addEllipse (area.reduced (8.0f).withTrimmedTop (cell * 0.5f));
      g.fillPath (ellipse);
    }
  }
};

// --xsnap: where CoreGraphics puts a glyph whose x is a hair below a 1/16 px step: rows of "H"
// at device x = 400 - d, plus references at 400 and 400 - 1/16.
struct XSnapTest final : public Component
{
  XSnapTest() { setOpaque (true); setSize (750, 750); }

  static constexpr float deltas[] = { 0.0f, 0.0005f, 0.0006f, 0.0007f, 0.0008f, 0.00085f, 0.0009f, 0.00095f, 0.00097f,
                                      0.00099f, 0.001f, 0.0011f, 0.0013f, 0.0015f, 0.0019f, 0.0625f };

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);
    g.setColour (Colours::black);
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();

    for (int i = 0; i < (int) std::size (deltas); ++i)
    {
      GlyphArrangement ga;
      ga.addLineOfText (FontOptions (13.0f), "H", (400.0f - deltas[i]) / scale, (40.0f + 36.0f * (float) i) / scale);
      ga.draw (g);
    }
  }
};

// --parity4: how CoreGraphics places a text baseline that falls between device pixels: 20 lines
// whose baseline advances by 1/20 of a device pixel more than a whole number of pixels per line.
struct ParityTest4 final : public Component
{
  ParityTest4() { setOpaque (true); setSize (750, 750); }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);
    g.setColour (Colours::black);
    const auto scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();

    for (int i = 0; i < 20; ++i)
    {
      // Device baseline: 40 + 36 i + i / 20 px.
      const auto baseline = (40.0f + 36.0f * (float) i + (float) i / 20.0f) / scale;
      GlyphArrangement ga;
      ga.addLineOfText (FontOptions (13.0f), "Baseline HHH xxx 0123", 10.0f, baseline);
      ga.draw (g);

      // Right column: whole-pixel baselines, x advancing by 1/20 of a device pixel per line.
      GlyphArrangement gx;
      gx.addLineOfText (FontOptions (13.0f), "HHHHHHHH", (400.0f + (float) i / 20.0f) / scale, (40.0f + 36.0f * (float) i) / scale);
      gx.draw (g);
    }
  }
};

// --parity3: the cases where an analytic GPU renderer can't match the software renderer to 1/255
// without care (renderer review): circles and rounded corners (JUCE draws them as cubic paths),
// short gradients (JUCE's lookup table and indexing), oblique edges (area vs distance ramps).
// Laid out on a grid of 250 x 250 px cells; see coverage/parity3_regions.py.
struct ParityTest3 final : public Component
{
  ParityTest3() { setOpaque (true); setSize (750, 750); }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);

    // Row 0: circles r = 20 and 50 px (device px at 2x: r = 10 and 25 pt), filled and stroked.
    g.setColour (Colours::black);
    g.fillEllipse (20.0f, 20.0f, 20.0f, 20.0f);
    g.fillEllipse (60.3f, 20.0f, 50.0f, 50.0f);
    g.drawEllipse (130.0f, 20.0f, 50.0f, 50.0f, 1.5f);
    g.drawEllipse (190.0f, 20.0f, 20.0f, 20.0f, 1.0f);

    // Row 0, middle: rounded rectangles, corner 5 pt (10 px), filled and stroked.
    g.fillRoundedRectangle (270.0f, 20.0f, 80.0f, 50.0f, 5.0f);
    g.drawRoundedRectangle (370.5f, 20.5f, 80.0f, 50.0f, 5.0f, 1.0f);

    // Row 0, right: a pill (corner = half the height).
    g.fillRoundedRectangle (520.0f, 20.0f, 150.0f, 30.0f, 15.0f);

    // Row 1: short gradients (20 px and 60 px at 1x), linear 2-stop, linear 3-stop, radial.
    for (int i = 0; i < 3; ++i)
    {
      const auto x = 20.0f + (float) i * 80.0f;
      ColourGradient lin (Colours::black, x, 260.0f, Colours::white, x + (i == 0 ? 10.0f : 30.0f), 260.0f, false);

      if (i == 2)
        lin.addColour (0.5, Colours::red);

      g.setGradientFill (lin);
      g.fillRect (x, 260.0f, 60.0f, 30.0f);
    }

    g.setGradientFill (ColourGradient (Colours::yellow, 400.0f, 300.0f, Colours::darkblue, 440.0f, 300.0f, true));
    g.fillRect (360.0f, 260.0f, 80.0f, 80.0f);
    g.setGradientFill (ColourGradient (Colours::white, 520.0f, 260.0f, Colours::black, 520.0f, 340.0f, false));
    g.fillRoundedRectangle (500.0f, 260.0f, 60.0f, 80.0f, 6.0f);

    // Row 2: oblique edges: 1 px lines at 45 and 30 degrees, a rotated square, a rotated stroke.
    g.setColour (Colours::black);
    g.drawLine (20.0f, 520.0f, 120.0f, 620.0f, 1.0f);
    g.drawLine (140.0f, 520.0f, 240.0f, 578.0f, 1.0f);
    {
      Graphics::ScopedSaveState s (g);
      g.addTransform (AffineTransform::rotation (0.5f, 330.0f, 570.0f));
      g.fillRect (300.0f, 540.0f, 60.0f, 60.0f);
      g.drawRect (400.0f, 540.0f, 60.0f, 60.0f, 1.5f);
    }
  }
};

// --parity2: transforms the anti-aliasing must follow (non-uniform scale, shear, rotation), a
// rotated image, a single-channel image drawn as an image, and horizontally stretched text.
struct ParityTest2 final : public Component
{
  ParityTest2() { setOpaque (true); setSize (750, 750); }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);

    // Shapes under scale (3, 1), (1, 0.2) and a shear: filled, stroked, rounded, ellipses.
    const AffineTransform transforms[] { AffineTransform::scale (3.0f, 1.0f).translated (10.0f, 10.0f),
                                         AffineTransform::scale (1.0f, 0.25f).translated (10.0f, 120.0f),
                                         AffineTransform::shear (0.6f, 0.0f).translated (10.0f, 170.0f) };

    for (const auto& t : transforms)
    {
      Graphics::ScopedSaveState s (g);
      g.addTransform (t);
      g.setColour (Colours::darkred);
      g.fillRect (Rectangle<float> (2.3f, 3.6f, 40.0f, 30.0f));
      g.drawRect (Rectangle<float> (50.3f, 3.6f, 40.0f, 30.0f), 2.0f);
      g.fillRoundedRectangle (Rectangle<float> (100.0f, 3.0f, 40.0f, 30.0f), 8.0f);
      g.drawRoundedRectangle (Rectangle<float> (150.0f, 3.0f, 40.0f, 30.0f), 8.0f, 2.5f);
      g.fillEllipse (Rectangle<float> (200.0f, 3.0f, 30.0f, 30.0f));
      g.drawEllipse (Rectangle<float> (235.5f, 3.5f, 30.0f, 30.0f), 2.0f);
    }

    // Rotated RGBA image, and a single-channel image drawn with drawImage (white with the alpha).
    {
      Image argb (Image::ARGB, 80, 50, true);
      {
        Graphics gi (argb);
        gi.setGradientFill (ColourGradient (Colours::orange, 0, 0, Colours::purple, 80, 50, false));
        gi.fillAll();
      }

      g.drawImageTransformed (argb, AffineTransform::rotation (0.35f).translated (60.0f, 290.0f));

      Image mono (Image::SingleChannel, 80, 50, true);
      {
        Graphics gi (mono);
        gi.setColour (Colours::white);
        gi.fillEllipse (2, 2, 76, 46);
      }

      g.setColour (Colours::black);
      g.fillRect (200, 280, 120, 70);
      g.setColour (Colours::blue.withAlpha (0.8f));
      g.drawImageAt (mono, 220, 290);
      g.drawImageAt (mono, 340, 290, true); // alpha channel filled with the current colour
    }

    // Horizontally stretched text.
    g.setColour (Colours::black);
    g.setFont (FontOptions (22.0f).withHorizontalScale (2.0f));
    g.drawSingleLineText ("Wide WAVM", 20, 420);
    g.setFont (FontOptions (16.0f).withHorizontalScale (0.6f));
    g.drawSingleLineText ("Narrow text 0123456789", 20, 460);
  }
};

// --parity: cases GraphicsDemo doesn't cover, from the renderer reviews. Text at a fractional
// scale and baseline, unrounded stroked rectangles (mitred corners), the image blend modes and
// replace fills (inside a layer, so destination alpha is composited the same way everywhere),
// more distinct gradients than the GPU gradient atlas holds, a dense waveform (hundreds of edge
// crossings per row) and a path with a high winding number.
struct ParityTest final : public Component
{
  ParityTest() { setOpaque (true); setSize (750, 750); }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::white);

    // Text, 1.25x and fractional baselines.
    {
      Graphics::ScopedSaveState s (g);
      g.addTransform (AffineTransform::scale (1.25f).translated (3.3f, 2.7f));
      g.setColour (Colours::black);

      for (int i = 0; i < 6; ++i)
      {
        g.setFont (FontOptions (11.0f + (float) i * 1.7f));
        g.drawSingleLineText ("Cutoff 1.25 kHz  Resonance -3.7 dB  Mix 42%", 4 + i % 3, 18 + (int) ((float) i * 19.3f));
      }
    }

    // Unrounded and slightly rounded stroked rectangles.
    g.setColour (Colours::darkblue);

    for (int i = 0; i < 4; ++i)
    {
      const Rectangle<float> r (20.0f + (float) i * 85.0f, 160.5f, 60.0f, 40.25f);
      g.drawRoundedRectangle (r, i == 3 ? 1.0f : 0.0f, 1.0f + (float) i * 2.3f);
    }

    // Blend modes and replace fills over stripes, in a layer.
    {
      Image src (Image::ARGB, 60, 60, true);
      {
        Graphics gi (src);
        gi.setGradientFill (ColourGradient (Colours::red.withAlpha (0.9f), 0, 0, Colours::blue.withAlpha (0.2f), 60, 60, false));
        gi.fillEllipse (2, 2, 56, 56);
      }

      g.beginTransparencyLayer (1.0f);

      for (int x = 0; x < 360; x += 12)
      {
        g.setColour (Colour::fromHSV ((float) x / 360.0f, 0.6f, 0.9f, 0.85f));
        g.fillRect (380 + x, 150, 12, 70);
      }

      const BlendMode modes[] { BlendMode::sourceOver, BlendMode::source, BlendMode::destinationIn, BlendMode::destinationOut };

      for (int i = 0; i < 4; ++i)
      {
        Graphics::ScopedSaveState s (g);
        g.setImageBlendMode (modes[i]);
        g.drawImageTransformed (src, AffineTransform::translation (385.0f + (float) i * 88.0f, 155.0f));
      }

      g.getInternalContext().setFill (Colours::green.withAlpha (0.35f));
      g.getInternalContext().fillRect ({ 390, 225, 300, 10 }, true);
      g.endTransparencyLayer();
    }

    // More distinct gradients than the atlas rows.
    for (int i = 0; i < 300; ++i)
    {
      const auto x = 20.0f + (float) (i % 30) * 23.0f, y = 250.0f + (float) (i / 30) * 14.0f;
      g.setGradientFill (ColourGradient (Colour::fromHSV ((float) i / 300.0f, 0.8f, 0.9f, 1.0f), x, y,
                                         Colour::fromHSV (1.0f - (float) i / 300.0f, 0.5f, 0.4f, 1.0f), x + 20.0f, y, false));
      g.fillRect (x, y, 20.0f, 11.0f);
    }

    // Dense waveform: filled and stroked.
    {
      Path wave;
      Random rng (1234);
      wave.startNewSubPath (20.0f, 520.0f);

      for (int i = 0; i <= 2400; ++i)
      {
        const auto x = 20.0f + (float) i * 0.29f;
        const auto y = 470.0f + 45.0f * std::sin ((float) i * 0.37f) * (0.4f + 0.6f * rng.nextFloat());
        wave.lineTo (x, y);
      }

      wave.lineTo (20.0f + 2400.0f * 0.29f, 520.0f);
      wave.closeSubPath();
      g.setColour (Colours::teal.withAlpha (0.8f));
      g.fillPath (wave);
      g.setColour (Colours::black);
      g.strokePath (wave, PathStrokeType (0.75f));
    }

    // High winding number (stacked loops), non-zero and even-odd.
    for (int rule = 0; rule < 2; ++rule)
    {
      Path loops;

      for (int i = 0; i < 40; ++i)
        loops.addEllipse (40.0f + (float) rule * 350.0f + (float) i * 3.0f, 560.0f + (float) i * 2.0f, 220.0f, 150.0f);

      loops.setUsingNonZeroWinding (rule == 0);
      g.setColour (Colours::purple.withAlpha (0.7f));
      g.fillPath (loops);
    }
  }
};

// Counts the frames that actually reach the demo area: a 1 px child of the demo holder, which
// repaints its whole area on every display refresh.
struct FrameCounter final : public Component
{
  FrameCounter() { setInterceptsMouseClicks (false, false); }
  void paint (Graphics&) override { ++frames; }
  int64 frames = 0;
};

template <typename T>
T* findDescendant (Component& root, std::function<bool (T&)> matches)
{
  for (auto* child : root.getChildren())
  {
    if (auto* t = dynamic_cast<T*> (child); t != nullptr && matches (*t))
      return t;

    if (auto* found = findDescendant<T> (*child, matches))
      return found;
  }

  return nullptr;
}

// --imagestream: persistent images rewritten on every paint (meters, scopes), each drawn next to
// the same picture drawn as vectors. A stale or overwritten texture shows as a difference between
// the two columns. The second row rewrites the image between two draws of one frame: the first
// draw must keep the first contents.
struct ImageStreamTest final : public Component, private Timer
{
  ImageStreamTest() { setSize (420, 330); startTimerHz (60); }

  static void picture (Graphics& g, int n, int x, int y, bool mono)
  {
    // Single channel: only the bars, as alpha.
    if (! mono)
    {
      g.setColour (Colour::fromHSV ((float) (n % 97) / 97.0f, 0.7f, 0.9f, 1.0f));
      g.fillRect (x, y, 96, 64);
    }

    g.setColour (mono ? Colours::white : Colours::black);
    g.fillRect (x + n % 88, y, 8, 64);
    g.fillRect (x, y + (n / 3) % 56, 96, 8);
  }

  static void write (Image& image, int n)
  {
    image.clear (image.getBounds());
    Graphics gi (image);
    picture (gi, n, 0, 0, image.isSingleChannel());
  }

  void paint (Graphics& g) override
  {
    g.fillAll (Colours::grey);
    g.setImageResamplingQuality (Graphics::lowResamplingQuality);
    const auto n = counter++;

    write (argb, n);
    g.drawImageAt (argb, 20, 20);
    picture (g, n, 140, 20, false);

    write (argb, n + 41);
    g.drawImageAt (argb, 20, 120);
    picture (g, n + 41, 140, 120, false);

    // Single channel, drawn with the alpha filled by the current colour (white on grey).
    write (mono, n + 13);
    g.setColour (Colours::white);
    g.drawImageAt (mono, 20, 220, true);
    g.setColour (Colours::grey);
    g.fillRect (140, 220, 96, 64);
    picture (g, n + 13, 140, 220, true);
  }

  void timerCallback() override { repaint(); }

  Image argb { Image::ARGB, 96, 64, true };
  Image mono { Image::SingleChannel, 96, 64, true };
  int counter = 0;
};

// --partialstream: 16 cells, one repainted per tick (only its own area), its paint number encoded
// in its colour (value mod 512, 3 bits per channel). Whatever frame a capture shows, the cells hold
// 16 consecutive numbers, cell k % 16 holding k: a cell drawn from a buffer that missed updates
// shows an older number (partial repaints into several buffers, see buffer age).
struct PartialStreamTest final : public Component, private Timer
{
  struct Cell final : public Component
  {
    // 3 bits per channel, levels 32 apart: survives the colour conversion of window captures.
    void paint (Graphics& g) override
    {
      const auto level = [this] (int shift) { return (uint8) (((value >> shift) & 7) * 32 + 16); };
      g.fillAll (Colour (level (0), level (3), level (6)));
    }
    int value = 0;
  };

  PartialStreamTest()
  {
    for (int i = 0; i < 16; ++i)
    {
      cells[i].value = i - 16 + 65536;
      cells[i].setBounds (20 + (i % 4) * 70, 20 + (i / 4) * 70, 60, 60);
      addAndMakeVisible (cells[i]);
    }

    setSize (320, 320);
    startTimerHz (60);
  }

  void paint (Graphics& g) override { g.fillAll (Colours::grey); }

  void timerCallback() override
  {
    ++counter;
    auto& cell = cells[counter % 16];
    cell.value = counter;
    cell.repaint();
  }

  Cell cells[16];
  int counter = 65535;
};

class CoverageApp final : public JUCEApplication, private Timer
{
public:
  const String getApplicationName() override    { return "JuceMetalCoverage"; }
  const String getApplicationVersion() override { return "1.0"; }

  void initialise (const String& commandLine) override
  {
    auto args = StringArray::fromTokens (commandLine, true);
    outDir = File (args[0].unquoted());

    if (const auto i = args.indexOf ("--from"); i >= 0)
      scenario = args[i + 1].getIntValue() * numClipModes;

    if (const auto i = args.indexOf ("--perf"); i >= 0)
    {
      // An index, or sw / cg / cgmetal / metal.
      perfEngineArg = args[i + 1];
      perfEngine = 0;
      clockFrozen = false;
    }

    outDir.createDirectory();

    if (commandLine.contains ("--cgmetal"))
      secondEngine = "CoreGraphics Metal Layer Renderer";

    window = std::make_unique<DocumentWindow> ("JuceMetalCoverage", Colours::black, 0);
    window->setUsingNativeTitleBar (true);
    if (commandLine.contains ("--partialstream"))
      window->setContentOwned (demo = new PartialStreamTest(), true);
    else if (commandLine.contains ("--imagestream"))
      window->setContentOwned (demo = new ImageStreamTest(), true);
    else if (commandLine.contains ("--xsnap"))
      window->setContentOwned (demo = new XSnapTest(), true);
    else if (commandLine.contains ("--parity4"))
      window->setContentOwned (demo = new ParityTest4(), true);
    else if (commandLine.contains ("--parity3"))
      window->setContentOwned (demo = new ParityTest3(), true);
    else if (commandLine.contains ("--parity2"))
      window->setContentOwned (demo = new ParityTest2(), true);
    else if (commandLine.contains ("--parity"))
      window->setContentOwned (demo = new ParityTest(), true);
    else if (commandLine.contains ("--twopoint"))
      window->setContentOwned (demo = new TwoPointGradientTest(), true);
    else
      window->setContentOwned (demo = new GraphicsDemo(), true);
    window->setTopLeftPosition (60, 60);
    // --resizable: lets a test resize the window (through accessibility) while it renders.
    if (commandLine.contains ("--resizable"))
      window->setResizable (true, false);
    window->setVisible (true);

    // Benchmarks must not run in the background: App Nap, timer coalescing and lower QoS make
    // background windows slower, and not by a constant amount.
    // Only for --perf: once made a foreground app, the process loses the window-capture access it
    // inherits from the shell (CGWindowListCreateImage returns null), which the pixel checks need.
    if (perfEngine >= 0)
    {
      Process::makeForegroundProcess();
      window->toFront (true);
    }

    const auto engines = window->getPeer()->getAvailableRenderingEngines();
    std::printf ("engines: %s\n", engines.joinIntoString (", ").toRawUTF8());
    listBox = findDescendant<ListBox> (*demo, [] (ListBox&) { return true; });
    numRows = listBox != nullptr ? listBox->getListBoxModel()->getNumRows() : 0;

    // --rows 0,1,3: only these GraphicsDemo tests (default: all, from --from on).
    if (const auto i = commandLine.indexOf ("--rows"); i >= 0)
      for (const auto& r : StringArray::fromTokens (commandLine.substring (i + 6).trim().upToFirstOccurrenceOf (" ", false, false), ",", {}))
        rows.push_back (r.getIntValue());

    if (rows.empty())
      for (int r = scenario / numClipModes; r < jmax (1, numRows); ++r)
        rows.push_back (r);

    scenario = 0;

    if (perfEngine >= 0)
    {
      // Default demo animation (position + rotation), real clock, one engine.
      if (auto* holder = findDescendant<DemoHolderComponent> (*demo, [] (DemoHolderComponent&) { return true; }))
      {
        holder->addAndMakeVisible (frameCounter);
        frameCounter.setBounds (0, 0, 1, 1);
      }

      perfEngine = perfEngineArg.containsOnly ("0123456789") ? perfEngineArg.getIntValue()
                                                             : engineIndex (aliasToEngineName (perfEngineArg));
      window->getPeer()->setCurrentRenderingEngine (perfEngine);
      std::printf ("perf engine %d: %s (layer %s)\n", perfEngine, engines[perfEngine].toRawUTF8(),
                   layerClassName (window->getPeer()->getNativeHandle()).c_str());
      perfTimer = std::make_unique<PerfStepper> (*this);
      return;
    }

    for (auto* name : { "Animate Position", "Animate Rotation", "Animate Size", "Animate Shearing", "Animate Alpha" })
      setToggle (name, false);

    if (! engines.contains ("Metal Renderer"))
    {
      std::printf ("no Metal engine: build with JUCE_METAL_RENDERER=1 against the metal-renderer branch\n");
      quit();
      return;
    }

    startTimer (250);
  }

  void shutdown() override
  {
    perfTimer.reset();
    frameCounter.setVisible (false);

    if (auto* parent = frameCounter.getParentComponent())
      parent->removeChildComponent (&frameCounter);

    window.reset();
  }

private:
  // --perf: per scenario, 1 s warm-up then 3 s of measurement.
  struct PerfStepper final : private Timer
  {
    explicit PerfStepper (CoverageApp& o) : owner (o) { startTimer (10); }

    void timerCallback() override
    {
      stopTimer();
      auto& o = owner;

      if (measuring)
      {
        const auto wall = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        auto* graphicsDemo = findDescendant<GraphicsDemoBase> (*o.demo, [] (GraphicsDemoBase&) { return true; });
        std::printf ("%-28s paint %6.3f ms   fps %5.1f   mainCPU %5.1f%%   procCPU %5.1f%%\n", o.scenarioName().toRawUTF8(),
                     graphicsDemo != nullptr ? graphicsDemo->averageTimeMs : -1.0,
                     (double) (o.frameCounter.frames - frames0) / wall,
                     100.0 * (callingThreadCpuSeconds() - main0) / wall,
                     100.0 * (processCpuSeconds() - proc0) / wall);
        std::fflush (stdout);
        measuring = false;
        ++o.scenario;
      }

      if (o.scenario >= (int) o.rows.size() * numClipModes)
      {
        JUCEApplication::quit();
        return;
      }

      if (! warmedUp)
      {
        if (o.listBox != nullptr)
          o.listBox->selectRow (o.currentRow());

        for (int i = 1; i < numClipModes; ++i)
          o.setToggle (clipModes[i], i == o.scenario % numClipModes);

        std::fprintf (stderr, "== %s\n", o.scenarioName().toRawUTF8());
        warmedUp = true;
        startTimer (1000);
        return;
      }

      warmedUp = false;
      measuring = true;
      frames0 = o.frameCounter.frames;
      main0 = callingThreadCpuSeconds();
      proc0 = processCpuSeconds();
      t0 = Time::getMillisecondCounterHiRes();
      startTimer (3000);
    }

    CoverageApp& owner;
    bool warmedUp = false, measuring = false;
    int64 frames0 = 0;
    double main0 = 0, proc0 = 0, t0 = 0;
  };

  static constexpr const char* clipModes[] { "none", "Clip to Rectangle", "Clip to Path", "Clip to Image" };
  static constexpr int numClipModes = 4;
  static constexpr int threshold = 48; // above anti-aliasing differences between rasterisers

  void setToggle (const String& name, bool on)
  {
    if (auto* b = findDescendant<ToggleButton> (*demo, [&] (ToggleButton& t) { return t.getButtonText() == name; }))
      b->setToggleState (on, sendNotification);
  }

  static String aliasToEngineName (const String& alias)
  {
    if (alias == "sw")      return "Software Renderer";
    if (alias == "cg")      return "CoreGraphics Renderer";
    if (alias == "cgmetal") return "CoreGraphics Metal Layer Renderer";
    if (alias == "metal")   return "Metal Renderer";
    return alias;
  }

  int engineIndex (const String& name) const
  {
    const auto index = window->getPeer()->getAvailableRenderingEngines().indexOf (name);
    jassert (index >= 0);
    return jmax (0, index);
  }

  void setEngine (const String& name)
  {
    const auto index = engineIndex (name);
    window->getPeer()->setCurrentRenderingEngine (index);
    std::fprintf (stderr, "== %s engine %d (%s) current %d layer %s\n", scenarioName().toRawUTF8(), index, name.toRawUTF8(),
                  window->getPeer()->getCurrentRenderingEngine(), layerClassName (window->getPeer()->getNativeHandle()).c_str());
    demo->repaint();
  }

  String scenarioName() const
  {
    return "row" + String (currentRow()).paddedLeft ('0', 2) + "_"
           + String (clipModes[scenario % numClipModes]).removeCharacters (" ").toLowerCase();
  }

  void timerCallback() override
  {
    // Steps per scenario: 0 configure + CG, 1 capture CG + Software, 2 capture SW + Metal, 3 capture Metal.
    // A capture only counts once two successive grabs agree (the new engine's frame is on screen)
    // and aren't empty (an off-screen window, e.g. on another Space, can't be captured).
    if (step > 0)
    {
      auto grab = captureOwnWindow();
      const auto stable = ! grab.pixels.empty() && grab.pixels == pending.pixels;
      pending = std::move (grab);

      if (! stable && ++attempts < 20)
        return;

      attempts = 0;

      if (! stable)
        std::printf ("%-28s capture unstable or empty, engine step %d\n", scenarioName().toRawUTF8(), step);
    }

    switch (step++)
    {
      case 0:
      {
        if (scenario >= (int) rows.size() * numClipModes)
        {
          quit();
          return;
        }

        frozenMs += 1234.5; // a different (still frozen) frame for each scenario
        if (listBox != nullptr)
          listBox->selectRow (currentRow());

        for (int i = 1; i < numClipModes; ++i)
          setToggle (clipModes[i], i == scenario % numClipModes);

        setEngine ("CoreGraphics Renderer");
        return;
      }

      case 1: captures[0] = std::exchange (pending, {}); setEngine (secondEngine); return;
      case 2: captures[1] = std::exchange (pending, {}); setEngine ("Metal Renderer"); return;

      default:
      {
        captures[2] = std::exchange (pending, {});
        // Skip the title bar: compare the content area only.
        const auto backingScale = (double) captures[2].width / (double) std::max (1, window->getWidth());
        const auto skipTop = captures[2].height - (int) std::lround (demo->getHeight() * backingScale);
        const auto name = scenarioName();
        const auto vsCg = compare (captures[0], captures[2], std::max (0, skipTop), threshold);
        const auto vsSw = compare (captures[1], captures[2], std::max (0, skipTop), threshold);
        const auto cgVsSw = compare (captures[0], captures[1], std::max (0, skipTop), threshold);

        const auto second = secondEngine == "Software Renderer" ? "sw" : "cgmetal";
        std::printf ("%-28s metal-vs-cg %6.2f%% (max %3d)   metal-vs-%s %6.2f%% (max %3d)   cg-vs-%s %6.2f%%\n",
                     name.toRawUTF8(), vsCg.percentOver, vsCg.maxDelta, second, vsSw.percentOver, vsSw.maxDelta, second, cgVsSw.percentOver);
        std::fflush (stdout);

        savePng (toImage (captures[0]), outDir.getChildFile (name + "_cg.png"));
        savePng (toImage (captures[1]), outDir.getChildFile (name + (secondEngine == "Software Renderer" ? "_sw.png" : "_cgmetal.png")));
        savePng (toImage (captures[2]), outDir.getChildFile (name + "_metal.png"));

        if (vsCg.heatMap.isValid())
          savePng (vsCg.heatMap, outDir.getChildFile (name + "_diff_cg.png"));

        ++scenario;
        step = 0;
      }
    }
  }

  File outDir;
  std::unique_ptr<DocumentWindow> window;
  Component* demo = nullptr;
  ListBox* listBox = nullptr;
  int currentRow() const { return rows[(size_t) (scenario / numClipModes)]; }

  std::vector<int> rows;
  int numRows = 0, scenario = 0, step = 0, attempts = 0, perfEngine = -1;
  String perfEngineArg;
  String secondEngine { "Software Renderer" }; // --cgmetal: CoreGraphics Metal Layer Renderer instead
  FrameCounter frameCounter;
  std::unique_ptr<PerfStepper> perfTimer;
  Capture captures[3], pending;
};
} // namespace

START_JUCE_APPLICATION (CoverageApp)
