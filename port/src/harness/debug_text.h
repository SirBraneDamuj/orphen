#pragma once

#include <string>
#include <vector>

namespace orphen::harness
{

  // One line segment of a glyph, on a 0..1 box with +Y up.
  struct StrokeSegment
  {
    float x0, y0, x1, y1;
  };

  // The glyph table, shared so world-space labels can use the same font as the
  // 2D overlay rather than carrying a second one. Returns nullptr and a zero
  // count for characters the font does not cover; lowercase is not handled here,
  // so upper-case before calling.
  const StrokeSegment *glyphStrokeSegments(char character, int &segmentCount);

  // A minimal stroke font for the debug overlay. PC-only diagnostics; nothing
  // here corresponds to the original's text renderer (FUN_002681c0 and the
  // glyph tables), which draws through the GS.
  //
  // Glyphs are line segments on a 0..1 box with +Y up, scaled at draw time.
  // Covers digits, uppercase letters and the punctuation the HUD uses;
  // lowercase input is upper-cased.
  class DebugTextRenderer
  {
  public:
    // Draws left-aligned lines from the top-left of a framebufferWidth x
    // framebufferHeight orthographic overlay. Assumes an already-current GL
    // context; saves and restores the matrices and the state it touches.
    void draw(int framebufferWidth,
              int framebufferHeight,
              const std::vector<std::string> &lines,
              float pixelsPerGlyph = 11.0f) const;

  private:
    void drawGlyph(char character, float originX, float originY, float scale) const;
  };

} // namespace orphen::harness
