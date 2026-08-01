#pragma once

#include <string>
#include <vector>

namespace orphen::harness
{

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
