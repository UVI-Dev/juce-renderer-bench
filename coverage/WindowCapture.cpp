#include "WindowCapture.h"

#include <cstdio>
#include <cstdlib>

#include <CoreGraphics/CoreGraphics.h>
#include <unistd.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <objc/message.h>
#include <objc/runtime.h>

std::vector<uint32_t> captureOwnWindowPixels (int& width, int& height)
{
  width = height = 0;
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

      if (owner == pid && getenv ("CAPTURE_DEBUG") != nullptr)
        fprintf (stderr, "capture: window %d layer %d\n", number, layer);

      if (owner == pid && layer == 0)
        windowId = (CGWindowID) number;
    }

    CFRelease (info);
  }

  std::vector<uint32_t> pixels;

  if (windowId == 0)
    return pixels;

 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wdeprecated-declarations"
  auto image = CGWindowListCreateImage (CGRectNull, kCGWindowListOptionIncludingWindow, windowId,
                                        kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution);
 #pragma clang diagnostic pop

  if (getenv ("CAPTURE_DEBUG") != nullptr)
    fprintf (stderr, "capture: window %u -> image %s\n", windowId, image != nullptr ? "ok" : "null");

  if (image == nullptr)
    return pixels;

  width = (int) CGImageGetWidth (image);
  height = (int) CGImageGetHeight (image);
  pixels.resize ((size_t) width * (size_t) height);
  auto space = CGColorSpaceCreateDeviceRGB();
  auto ctx = CGBitmapContextCreate (pixels.data(), (size_t) width, (size_t) height, 8, (size_t) width * 4, space,
                                    (uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Host);
  CGContextDrawImage (ctx, CGRectMake (0, 0, width, height), image);
  CGContextRelease (ctx);
  CGColorSpaceRelease (space);
  CGImageRelease (image);
  return pixels;
}

double callingThreadCpuSeconds()
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

double processCpuSeconds()
{
  rusage ru {};
  getrusage (RUSAGE_SELF, &ru);
  return (double) ru.ru_utime.tv_sec + (double) ru.ru_utime.tv_usec * 1.0e-6
       + (double) ru.ru_stime.tv_sec + (double) ru.ru_stime.tv_usec * 1.0e-6;
}

std::string layerClassName (void* nsView)
{
  if (nsView == nullptr)
    return "nil";

  auto* layer = reinterpret_cast<id (*) (id, SEL)> (objc_msgSend) (static_cast<id> (nsView), sel_registerName ("layer"));
  return layer != nullptr ? object_getClassName (layer) : "nil";
}
