cmake_minimum_required(VERSION 3.22)
project(JuceRendererBench VERSION 1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)

# A JUCE checkout to test. The JuceMetal targets need one carrying the JUCE_METAL_RENDERER engine
# (https://github.com/UVI-Dev/JUCE/tree/metal-renderer); the others build with any JUCE 8/9.
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../JUCE" CACHE PATH "JUCE checkout to test")
add_subdirectory(${JUCE_DIR} juce)

set(bench_common_definitions
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_DISABLE_COREGRAPHICS_FONT_SMOOTHING=1) # the same glyph coverage on every engine

function(bench_link target)
  target_link_libraries(${target} PRIVATE ${ARGN} juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()

# RepaintRepro: a grid of 1,200 widgets with N small areas repainting every frame (or the whole
# window with --full), painted by different engines. Same source for every variant.
#   RepaintRepro_CG         : JUCE's default CoreGraphics path (layer-backed NSView, AppKit coalesces
#                             the dirty rects into their bounding box)
#   RepaintRepro_MetalLayer : JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1 (CoreGraphics
#                             into a CAMetalLayer, one paint per dirty rect)
#   RepaintRepro_JuceMetal  : the JUCE_METAL_RENDERER engine
#   RepaintRepro_GL         : JUCE's OpenGL renderer attached to the content component
foreach(variant CG MetalLayer JuceMetal GL)
  set(target RepaintRepro_${variant})
  juce_add_gui_app(${target} PRODUCT_NAME ${target})
  target_sources(${target} PRIVATE repaint_repro/Main.cpp)
  target_compile_definitions(${target} PRIVATE ${bench_common_definitions}
    JUCE_APPLICATION_NAME_STRING="${target}"
    JUCE_APPLICATION_VERSION_STRING="1.0")

  if(variant STREQUAL "MetalLayer")
    target_compile_definitions(${target} PRIVATE JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=1)
  elseif(variant STREQUAL "JuceMetal")
    target_compile_definitions(${target} PRIVATE JUCE_METAL_RENDERER=1)
  endif()

  if(variant STREQUAL "GL")
    target_compile_definitions(${target} PRIVATE REPRO_GL=1 JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)
    bench_link(${target} juce::juce_opengl)
  else()
    bench_link(${target} juce::juce_gui_basics)
  endif()
endforeach()

# JuceMetalCoverage: JUCE's own GraphicsDemo (and a few extra scenes) rendered by the CoreGraphics,
# Software and Metal engines of the same window, captured as WindowServer shows them and compared.
juce_add_gui_app(JuceMetalCoverage PRODUCT_NAME JuceMetalCoverage)
target_sources(JuceMetalCoverage PRIVATE coverage/Coverage.cpp coverage/WindowCapture.cpp)
target_include_directories(JuceMetalCoverage PRIVATE coverage "${JUCE_DIR}/examples")
target_compile_definitions(JuceMetalCoverage PRIVATE ${bench_common_definitions}
  JUCE_METAL_RENDERER=1
  JUCE_STRICT_REFCOUNTEDPOINTER=1
  PIP_JUCE_EXAMPLES_DIRECTORY_STRING="${JUCE_DIR}/examples"
  JUCE_APPLICATION_NAME_STRING="JuceMetalCoverage"
  JUCE_APPLICATION_VERSION_STRING="1.0")
bench_link(JuceMetalCoverage juce::juce_gui_extra)
