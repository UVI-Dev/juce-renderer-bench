// Kept out of the demo translation unit: CoreGraphics' MacTypes 'Point' clashes with
// 'using namespace juce' in JUCE's example demos.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Composited pixels of this process' main window at backing resolution (premultiplied BGRA words).
std::vector<uint32_t> captureOwnWindowPixels (int& width, int& height);

// CPU time consumed so far by the calling thread / the whole process, in seconds.
double callingThreadCpuSeconds();
double processCpuSeconds();

// Class name of an NSView's backing layer ("nil" when it has none).
std::string layerClassName (void* nsView);
