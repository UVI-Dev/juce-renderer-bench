// Repro: several small, far-apart regions of one window request a repaint every frame.
// Measures what JUCE actually repaints on macOS for that pattern.
//
// Window = opaque background + a grid of (non-opaque) "cells" that never change, plus N small
// "spots" scattered over the window that call repaint() on every vblank. Ideally only the
// spots (and the background under them) are painted; every cell paint is pure overdraw.
//
// Usage: RepaintRepro --spots N [--seconds S] [--lattice] [--full] [--textcache] [--software] [--background] [--label text]
//   default layout: 4 corners first, then random; --lattice: non-touching spots (N separate rects)
// RepaintRepro --polyline N [--lines K] [--fill] [--stroke W] [--joint mitered|curved|beveled]
//   [--cap butt|square|rounded] [--closed] [--curves] [--rotate]: a scope (1320 x 400, opaque)
//   that strokes K polylines of N points each on every vblank, like a waveform, spectrum or
//   modulation history view (--fill: also fills the area under each one); no spots unless --spots.
// Prints one line of stats, then checks that the on-screen pixels match a full repaint
// (catches a paint path that leaves stale or cleared pixels outside the dirty rects), and quits.
// That check needs --background (a foreground app can't capture its window); REPRO_DUMP_DIR=dir
// also saves both captures as PNGs.

#include <juce_gui_basics/juce_gui_basics.h>
#if REPRO_GL
 #include <juce_opengl/juce_opengl.h>
#endif

#include <CoreGraphics/CoreGraphics.h>
#include <mach/mach.h>
#include <sys/resource.h>

#include <algorithm>
#include <optional>
#include <cmath>
#include <vector>

namespace
{
using juce::int64;

constexpr int windowW = 1400, windowH = 900;
constexpr int cellW = 35, cellH = 30;
constexpr int spotSize = 24;

[[nodiscard]] double nowSeconds() noexcept
{
  return juce::Time::getMillisecondCounterHiRes() * 1.0e-3;
}

[[nodiscard]] double processCpuSeconds() noexcept
{
  rusage ru {};
  getrusage (RUSAGE_SELF, &ru);
  return (double) ru.ru_utime.tv_sec + (double) ru.ru_utime.tv_usec * 1.0e-6
       + (double) ru.ru_stime.tv_sec + (double) ru.ru_stime.tv_usec * 1.0e-6;
}

// Must be called on the thread to measure (message thread here).
[[nodiscard]] double threadCpuSeconds() noexcept
{
  thread_basic_info_data_t info {};
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  const auto self = mach_thread_self();
  const auto kr = thread_info (self, THREAD_BASIC_INFO, (thread_info_t) &info, &count);
  mach_port_deallocate (mach_task_self(), self);

  if (kr != KERN_SUCCESS)
    return 0.0;

  return (double) info.user_time.seconds + (double) info.user_time.microseconds * 1.0e-6
       + (double) info.system_time.seconds + (double) info.system_time.microseconds * 1.0e-6;
}

struct Stats
{
  bool measuring = false;
  int64 frames = 0;       // vblanks during which repaints were requested
  int64 passes = 0;       // top-level paint traversals (one per drawRect, or one per rect with the metal renderer)
  int64 cellPaints = 0;   // cells never change: each paint is overdraw
  int64 spotPaints = 0;
  int64 scopePaints = 0;
  double paintedArea = 0; // sum of clip-bounds area over passes, in points^2
  double dirtyArea = 0;   // area actually requested through repaint()
  double paintSeconds = 0;
  double frameAccum = 0;
  bool inContentPass = false; // Content's paint pass is timing its children (Scope included)
  std::vector<double> perFramePaintMs;
};

Stats stats;

// --textcache: each cell keeps its laid-out text instead of going through Graphics::drawText's
// global 128-entry LRU (which the 1200 distinct labels thrash, so every paint reshapes).
bool cacheTextPerCell = false;

class Cell final : public juce::Component
{
public:
  // Not opaque, like most real widgets: the parent background is painted under it.
  explicit Cell (int i) : index (i) {}

  void paint (juce::Graphics& g) override
  {
    if (stats.measuring)
      ++stats.cellPaints;

    // Representative widget cost: filled + stroked rounded rect and a label.
    const auto r = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff20242a));
    g.setColour (juce::Colour::fromHSV ((float) (index % 37) / 37.0f, 0.5f, 0.6f, 1.0f));
    g.fillRoundedRectangle (r.reduced (3.0f), 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (3.5f), 4.0f, 1.0f);
    g.setFont (11.0f);

    if (! cacheTextPerCell)
    {
      g.drawText (juce::String (index), getLocalBounds(), juce::Justification::centred, false);
      return;
    }

    // Same layout as Graphics::drawText (addCurtailedLineOfText + justifyGlyphs), done once.
    // Real code would rebuild on text/font/size/scale change (resized, lookAndFeelChanged...).
    if (! label.has_value())
    {
      label.emplace();
      label->addCurtailedLineOfText (g.getCurrentFont(), juce::String (index), 0.0f, 0.0f, (float) getWidth(), false);
      label->justifyGlyphs (0, label->getNumGlyphs(), 0.0f, 0.0f, (float) getWidth(), (float) getHeight(),
                            juce::Justification::centred);
    }

    label->draw (g);
  }

private:
  int index;
  std::optional<juce::GlyphArrangement> label;
};

class Spot final : public juce::Component
{
public:

  void tick() noexcept { phase = (phase + 1) & 255; }

  void paint (juce::Graphics& g) override
  {
    if (stats.measuring)
      ++stats.spotPaints;

    g.fillAll (juce::Colour::fromHSV ((float) phase / 256.0f, 0.9f, 1.0f, 1.0f));
  }

private:
  int phase = 0;
};

// --polyline: K polylines of N points across the scope, moving every frame. The stroke style
// options (--joint, --cap, --closed, --curves, --rotate) exercise the other outline cases.
struct ScopeStyle
{
  int points = 0, lines = 1;
  bool fill = false;
  float width = 1.5f;
  juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::mitered;
  juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::butt;
  bool closed = false, curves = false, rotate = false;
};

class Scope final : public juce::Component
{
public:
  explicit Scope (const ScopeStyle& s) : style (s)
  {
    setOpaque (true);

    // The wave's phases, so that a frame costs no sin(): sin (b + d) from sin b and cos b.
    for (int l = 0; l < style.lines; ++l)
      for (int i = 0; i < style.points; ++i)
      {
        const auto t = (float) i / (float) (style.points - 1);
        const auto a = t * 40.0f + (float) l;
        const auto fast = a * 13.7f + (float) i;
        waves.push_back ({ std::sin (a), std::cos (a), std::sin (fast), std::cos (fast) });
      }

    paths.resize ((size_t) style.lines);
    areas.resize ((size_t) style.lines);
  }

  void tick() noexcept { ++phase; }

  void paint (juce::Graphics& g) override
  {
    if (stats.measuring)
      ++stats.scopePaints;

    // The scope is opaque, so its repaints don't go through Content's timed pass: timed here.
    const auto start = nowSeconds();
    paintScope (g);

    if (stats.measuring && ! stats.inContentPass)
    {
      const auto dt = nowSeconds() - start;
      stats.paintSeconds += dt;
      stats.frameAccum += dt;
    }
  }

private:
  void paintScope (juce::Graphics& g)
  {
    g.fillAll (juce::Colour (0xff181c20));
    const auto w = (float) getWidth(), h = (float) getHeight();
    const auto band = h / (float) style.lines;
    const auto d = (float) phase * 0.07f;
    const auto sinD = std::sin (d), cosD = std::cos (d), sinFastD = std::sin (d * 13.7f), cosFastD = std::cos (d * 13.7f);
    const auto transform = style.rotate ? juce::AffineTransform::rotation (0.05f, w * 0.5f, h * 0.5f) : juce::AffineTransform();

    for (int l = 0; l < style.lines; ++l)
    {
      auto& p = paths[(size_t) l];
      p.clear();
      p.preallocateSpace (3 * style.points + 8);
      const auto mid = band * ((float) l + 0.5f);
      const auto amp = band * 0.4f;
      const auto* wave = waves.data() + (size_t) (l * style.points);

      for (int i = 0; i < style.points; ++i)
      {
        // A smooth wave plus a fast wiggle: dense, irregular, like a waveform or a spectrum.
        const auto& v = wave[i];
        const auto y = mid + amp * (0.7f * (v.sinA * cosD + v.cosA * sinD) + 0.3f * (v.sinFast * cosFastD + v.cosFast * sinFastD));
        const auto x = (float) i / (float) (style.points - 1) * w;

        if (i == 0)
          p.startNewSubPath (x, y);
        else if (style.curves && (i & 1) == 0)
          p.quadraticTo (p.getCurrentPosition().x, mid, x, y);
        else
          p.lineTo (x, y);
      }

      const auto colour = juce::Colour::fromHSV ((float) l / (float) style.lines, 0.6f, 1.0f, 1.0f);

      if (style.fill)
      {
        // The area under the open wave, built alongside it rather than copied.
        auto& area = areas[(size_t) l];
        area.clear();
        area.preallocateSpace (3 * style.points + 16);
        area.addPath (p);
        area.lineTo (w, mid + amp);
        area.lineTo (0.0f, mid + amp);
        area.closeSubPath();
        g.setColour (colour.withAlpha (0.25f));
        g.fillPath (area, transform);
      }

      if (style.closed)
        p.closeSubPath();

      g.setColour (colour);
      g.strokePath (p, juce::PathStrokeType (style.width, style.joint, style.cap), transform);
    }
  }

  struct Wave { float sinA, cosA, sinFast, cosFast; };

  const ScopeStyle style;
  std::vector<Wave> waves;
  std::vector<juce::Path> paths, areas;
  int phase = 0;
};

class Content final : public juce::Component
{
public:
  Content (int numSpots, bool lattice)
  {
    setOpaque (true);
    setSize (windowW, windowH);

    for (int y = 0, i = 0; y + cellH <= windowH; y += cellH)
      for (int x = 0; x + cellW <= windowW; x += cellW, ++i)
      {
        auto& c = cells.emplace_back (std::make_unique<Cell> (i));
        c->setBounds (x, y, cellW, cellH);
        addAndMakeVisible (*c);
      }

    // Corners first (worst case for a bounding-box union), then deterministic scatter.
    const juce::Point<int> corners[] { { 8, 8 }, { windowW - spotSize - 8, windowH - spotSize - 8 },
                                       { windowW - spotSize - 8, 8 }, { 8, windowH - spotSize - 8 } };
    juce::Random rng (1234);

    for (int i = 0; i < numSpots; ++i)
    {
      // Lattice: every spot is its own rect, never touching another (exact rect count).
      const auto p = lattice ? juce::Point<int> (20 + (i * 7 % 25) * 54, 20 + (i * 11 % 16) * 54)
                   : i < 4   ? corners[i]
                             : juce::Point<int> (rng.nextInt (windowW - spotSize), rng.nextInt (windowH - spotSize));
      auto& s = spots.emplace_back (std::make_unique<Spot>());
      s->setBounds (p.x, p.y, spotSize, spotSize);
      addAndMakeVisible (*s);
    }
  }

  void paint (juce::Graphics& g) override
  {
    passStart = nowSeconds();
    stats.inContentPass = true;

    if (stats.measuring)
    {
      ++stats.passes;
      stats.paintedArea += (double) g.getClipBounds().getWidth() * (double) g.getClipBounds().getHeight();
    }

    g.fillAll (juce::Colour (0xff101214));
  }

  void paintOverChildren (juce::Graphics&) override
  {
    if (stats.measuring && passStart > 0)
    {
      const auto dt = nowSeconds() - passStart;
      stats.paintSeconds += dt;
      stats.frameAccum += dt;
    }

    passStart = 0;
    stats.inContentPass = false;
  }

  bool animate = true;
  bool recordDirty = false;            // GPU target: no peer, the renderer consumes this list
  juce::RectangleList<int> pendingDirty;
  bool fullRepaint = false; // --full: whole window every frame (page switch / resize cost)
  std::unique_ptr<Scope> scope; // --polyline

  void addScope (const ScopeStyle& style)
  {
    scope = std::make_unique<Scope> (style);
    scope->setBounds (40, 40, windowW - 80, 400);
    addAndMakeVisible (*scope);
  }

  void onVBlank()
  {
    if (! animate)
      return;

    if (stats.measuring)
    {
      // Paint work triggered by the previous vblank's requests lands before this one.
      if (stats.frames > 0)
        stats.perFramePaintMs.push_back (stats.frameAccum * 1000.0);

      stats.frameAccum = 0;
      ++stats.frames;
    }

    if (scope != nullptr)
      scope->tick();

    if (fullRepaint)
    {
      repaint();

      if (recordDirty)
        pendingDirty.add (getLocalBounds());

      if (stats.measuring)
        stats.dirtyArea += (double) windowW * windowH;

      return;
    }

    if (scope != nullptr)
    {
      scope->repaint();

      if (recordDirty)
        pendingDirty.add (scope->getBounds());

      if (stats.measuring)
        stats.dirtyArea += (double) scope->getWidth() * scope->getHeight();
    }

    for (auto& s : spots)
    {
      s->tick();
      s->repaint();

      if (recordDirty)
        pendingDirty.add (s->getBounds());

      if (stats.measuring)
        stats.dirtyArea += (double) spotSize * spotSize;
    }
  }

private:
  std::vector<std::unique_ptr<Cell>> cells;
  std::vector<std::unique_ptr<Spot>> spots;
  double passStart = 0;
};

class MainWindow final : public juce::DocumentWindow
{
public:
  MainWindow (int numSpots, bool software, bool lattice, bool foreground)
    : DocumentWindow ("RepaintRepro", juce::Colours::black, 0)
  {
    setUsingNativeTitleBar (true);
    setContentOwned (content = new Content (numSpots, lattice), true);
    setTopLeftPosition (60, 60);
    setVisible (true);

    // Measure in the foreground: background apps get App Nap, timer coalescing and lower QoS.
    // But a process made a foreground app loses the window-capture access it inherits from the
    // shell, so the pixel check needs --background (and its timings don't count).
    if (foreground)
    {
      juce::Process::makeForegroundProcess();
      toFront (true);
    }

    if (software)
      if (auto* peer = getPeer())
        peer->setCurrentRenderingEngine (0);

   #if REPRO_GL
    // JUCE's OpenGL renderer: retained FBO + validArea, one traversal clipped to the exact
    // invalid RectangleList, edge tables on the CPU and shader fills on the GPU.
    glContext.attachTo (*content);
   #endif

    vblank = std::make_unique<juce::VBlankAttachment> (content, [this] { content->onVBlank(); });
  }

 #if REPRO_GL
  ~MainWindow() override { glContext.detach(); }
 #endif

  void closeButtonPressed() override { juce::JUCEApplication::quit(); }
  void repaintAll() { content->repaint(); }

  Content* content = nullptr;

private:
  std::unique_ptr<juce::VBlankAttachment> vblank;
 #if REPRO_GL
  juce::OpenGLContext glContext;
 #endif
};

// Composited pixels of our own window, as the window server shows them.
[[nodiscard]] std::vector<uint32_t> captureOwnWindow (int& w, int& h)
{
  CGWindowID windowId = 0;

  if (auto info = CGWindowListCopyWindowInfo (kCGWindowListOptionAll, kCGNullWindowID))
  {
    const auto pid = (int) getpid();

    for (CFIndex i = 0; i < CFArrayGetCount (info) && windowId == 0; ++i)
    {
      auto dict = (CFDictionaryRef) CFArrayGetValueAtIndex (info, i);
      int owner = 0, number = 0, layer = -1;
      CFNumberGetValue ((CFNumberRef) CFDictionaryGetValue (dict, kCGWindowOwnerPID), kCFNumberIntType, &owner);
      CFNumberGetValue ((CFNumberRef) CFDictionaryGetValue (dict, kCGWindowNumber), kCFNumberIntType, &number);
      CFNumberGetValue ((CFNumberRef) CFDictionaryGetValue (dict, kCGWindowLayer), kCFNumberIntType, &layer);

      if (owner == pid && layer == 0)
        windowId = (CGWindowID) number;
    }

    CFRelease (info);
  }

  std::vector<uint32_t> pixels;
  w = h = 0;

  if (windowId == 0)
    return pixels;

  JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
  auto image = CGWindowListCreateImage (CGRectNull, kCGWindowListOptionIncludingWindow, windowId,
                                        kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
  JUCE_END_IGNORE_DEPRECATION_WARNINGS

  if (image == nullptr)
    return pixels;

  w = (int) CGImageGetWidth (image);
  h = (int) CGImageGetHeight (image);
  pixels.resize ((size_t) w * (size_t) h);
  auto space = CGColorSpaceCreateDeviceRGB();
  auto ctx = CGBitmapContextCreate (pixels.data(), (size_t) w, (size_t) h, 8, (size_t) w * 4, space,
                                    (uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Host);
  CGContextDrawImage (ctx, CGRectMake (0, 0, w, h), image);
  CGContextRelease (ctx);
  CGColorSpaceRelease (space);
  CGImageRelease (image);
  return pixels;
}

class App final : public juce::JUCEApplication, private juce::Timer
{
public:
  const juce::String getApplicationName() override    { return "RepaintRepro"; }
  const juce::String getApplicationVersion() override { return "1.0"; }

  void initialise (const juce::String&) override
  {
    const auto args = getCommandLineParameterArray();
    auto argValue = [&] (const char* name, const juce::String& def)
    {
      const auto i = args.indexOf (name);
      return i >= 0 && i + 1 < args.size() ? args[i + 1] : def;
    };

    // --polyline: the scope alone, unless --spots asks for spots too.
    const auto polyline = args.contains ("--polyline");
    numSpots = argValue ("--spots", polyline ? "0" : "2").getIntValue();
    seconds = argValue ("--seconds", "5").getDoubleValue();
    label = argValue ("--label", "");
    software = args.contains ("--software");
    const auto lattice = args.contains ("--lattice");
    cacheTextPerCell = args.contains ("--textcache");

    window = std::make_unique<MainWindow> (numSpots, software, lattice, ! args.contains ("--background"));
    window->content->fullRepaint = args.contains ("--full");

    if (polyline)
    {
      const auto joint = argValue ("--joint", "mitered"), cap = argValue ("--cap", "butt");
      ScopeStyle style;
      style.points = argValue ("--polyline", "0").getIntValue();
      style.lines = argValue ("--lines", "1").getIntValue();
      style.width = argValue ("--stroke", "1.5").getFloatValue();
      style.fill = args.contains ("--fill");
      style.closed = args.contains ("--closed");
      style.curves = args.contains ("--curves");
      style.rotate = args.contains ("--rotate");
      style.joint = joint == "curved" ? juce::PathStrokeType::curved
                  : joint == "beveled" ? juce::PathStrokeType::beveled : juce::PathStrokeType::mitered;
      style.cap = cap == "square" ? juce::PathStrokeType::square
                : cap == "rounded" ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt;

      const auto fail = [this] (const char* message)
      {
        std::fprintf (stderr, "RepaintRepro: %s\n", message);
        setApplicationReturnValue (1);
        quit();
      };

      if (style.points < 2)
        return fail ("--polyline needs at least 2 points");
      if (style.lines < 1)
        return fail ("--lines needs at least 1 line");
      if (! (style.width > 0.0f))
        return fail ("--stroke needs a width > 0");
      if (! juce::StringArray { "mitered", "curved", "beveled" }.contains (joint))
        return fail ("--joint: mitered, curved or beveled");
      if (! juce::StringArray { "butt", "square", "rounded" }.contains (cap))
        return fail ("--cap: butt, square or rounded");

      scopeDescription = juce::String::formatted (" scope=%dx%d stroke=%.2f %s/%s%s%s%s%s", style.points, style.lines,
                                                  (double) style.width, joint.toRawUTF8(), cap.toRawUTF8(),
                                                  style.fill ? " fill" : "", style.closed ? " closed" : "",
                                                  style.curves ? " curves" : "", style.rotate ? " rotate" : "");
      window->content->addScope (style);
    }

    startTimer (1000); // warm-up: first frames repaint everything
  }

  void shutdown() override { window.reset(); }

private:
  void timerCallback() override
  {
    stopTimer();

    if (! stats.measuring)
    {
      stats.measuring = true;
      t0 = nowSeconds();
      cpu0 = processCpuSeconds();
      mainCpu0 = threadCpuSeconds();
      startTimer ((int) (seconds * 1000.0));
      return;
    }

    switch (phase++)
    {
      case 0:
        stats.measuring = false;
        report (nowSeconds() - t0, processCpuSeconds() - cpu0, threadCpuSeconds() - mainCpu0);
        window->content->animate = false; // freeze, let the last partial repaints land
        startTimer (300);
        return;

      case 1:
        partial = captureOwnWindow (capW, capH);
        window->repaintAll();             // reference: everything repainted
        startTimer (300);
        return;

      default:
      {
        int w = 0, h = 0;
        const auto full = captureOwnWindow (w, h);
        int64 diff = 0;

        juce::Rectangle<int> diffBounds;

        // Stale or cleared pixels differ by far more than the few LSBs of rasterisation and
        // compositing noise seen between two full repaints; the title bar (vibrancy) is skipped.
        constexpr int tolerance = 16;
        const auto channelDelta = [] (uint32_t p, uint32_t q)
        {
          int m = 0;
          for (int shift = 0; shift < 32; shift += 8)
            m = std::max (m, std::abs ((int) ((p >> shift) & 0xff) - (int) ((q >> shift) & 0xff)));
          return m;
        };
        const auto top = std::max (0, h - windowH);

        if (w == capW && h == capH)
          for (int y = top; y < h; ++y)
            for (int x = 0; x < w; ++x)
              if (channelDelta (full[(size_t) (y * w + x)], partial[(size_t) (y * w + x)]) > tolerance)
              {
                ++diff;
                diffBounds = diffBounds.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                                  : diffBounds.getUnion ({ x, y, 1, 1 });
              }

        if (diff > 0)
          std::printf ("%-14s verify: diff bounds %s\n", label.toRawUTF8(), diffBounds.toString().toRawUTF8());

        if (const auto dumpDir = juce::SystemStats::getEnvironmentVariable ("REPRO_DUMP_DIR", {}); dumpDir.isNotEmpty())
        {
          auto save = [&] (const std::vector<uint32_t>& px, const char* name)
          {
            juce::Image img (juce::Image::ARGB, w, h, false);
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
              std::memcpy (bd.getLinePointer (y), px.data() + (size_t) (y * w), (size_t) w * 4);
            juce::File f (dumpDir + "/" + label + "_" + name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
          };
          save (partial, "partial");
          save (full, "full");
        }

        if (full.empty() || w != capW || h != capH)
          std::printf ("%-14s verify: capture failed\n", label.toRawUTF8());
        else
          std::printf ("%-14s verify: %s (%lld of %d content px differ from a full repaint by > %d)\n", label.toRawUTF8(),
                       diff == 0 ? "OK" : "MISMATCH", (long long) diff, w * windowH, tolerance);

        std::fflush (stdout);
        quit();
      }
    }
  }

  void report (double wall, double cpu, double mainCpu) const
  {
    const auto frames = (double) std::max<int64> (1, stats.frames);
    auto ms = stats.perFramePaintMs;
    std::sort (ms.begin(), ms.end());
    const auto pct = [&] (double q) { return ms.empty() ? 0.0 : ms[(size_t) std::min ((double) ms.size() - 1, q * (double) ms.size())]; };
    const auto mean = ms.empty() ? 0.0 : stats.paintSeconds * 1000.0 / (double) ms.size();

    auto line = juce::String::formatted (
      "%-14s spots=%3d fps=%5.1f passes/frame=%5.2f cellPaints/frame=%7.1f spotPaints/frame=%5.1f scopePaints/frame=%5.1f "
      "paintedArea/dirtyArea=%7.1fx paintMs/frame mean=%6.3f p50=%6.3f p95=%6.3f max=%6.3f "
      "mainCPU=%5.1f%% procCPU=%5.1f%%%s",
      label.toRawUTF8(), numSpots, (double) stats.frames / wall,
      (double) stats.passes / frames, (double) stats.cellPaints / frames, (double) stats.spotPaints / frames,
      (double) stats.scopePaints / frames,
      stats.dirtyArea > 0 ? stats.paintedArea / stats.dirtyArea : 0.0,
      mean, pct (0.5), pct (0.95), ms.empty() ? 0.0 : ms.back(),
      100.0 * mainCpu / wall, 100.0 * cpu / wall, scopeDescription.toRawUTF8());


    std::printf ("%s\n", line.toRawUTF8());
    std::fflush (stdout);
  }

  std::unique_ptr<MainWindow> window;
  int numSpots = 2;
  double seconds = 5.0;
  bool software = false;
  juce::String label, scopeDescription;
  double t0 = 0, cpu0 = 0, mainCpu0 = 0;
  int phase = 0, capW = 0, capH = 0;
  std::vector<uint32_t> partial;
};

} // namespace

START_JUCE_APPLICATION (App)
