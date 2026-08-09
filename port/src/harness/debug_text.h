#pragma once

#include "ported/debug/original_debug_text.h"

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
  // lowercase is drawn as a small capital, at kSmallCapHeight of the cap box,
  // so "tPOS>" and "cPOS>" still read as themselves.
  inline constexpr float kSmallCapHeight = 0.7f;

  // How the stroke font sits inside the original's 10x20 glyph cell, for the
  // fallback path where slot 0x30's atlas is not resident.
  //
  // The atlas window is 7x15 texels of an 8x16 cell, so the *sprite's* ink
  // covers 8.75 x 18.75 of it -- but that window is a bitmap face, and most of
  // its 15 rows go to the space a capital does not occupy. A stroke glyph has
  // no such space: drawn to the full window it fills the cell edge to edge and
  // consecutive lines, which the original packs at exactly one cell pitch,
  // collide. These put a capital in the middle of the cell instead, which is
  // where the bitmap's is.
  inline constexpr float kCapWidthFraction = 7.0f / 8.0f;  // the atlas window
  inline constexpr float kCapHeightFraction = 0.72f;       // cap height
  inline constexpr float kBaselineFraction = 0.14f;        // above the cell floor

  class DebugTextRenderer
  {
  public:
    // Draws left-aligned lines from the top-left of a framebufferWidth x
    // framebufferHeight orthographic overlay. Assumes an already-current GL
    // context; saves and restores the matrices and the state it touches.
    //
    // topOffsetPixels pushes the block down, so the harness HUD can sit below
    // the ported debug overlay rather than through it.
    void draw(int framebufferWidth,
              int framebufferHeight,
              const std::vector<std::string> &lines,
              float pixelsPerGlyph = 11.0f,
              float topOffsetPixels = 0.0f) const;

    // The ported debug overlay (FUN_00268270's output). Glyphs arrive already
    // placed on the original's 640x448 virtual screen, so this only has to fit
    // that screen into the framebuffer -- uniformly, centred, the way a 4:3
    // picture would sit in a wider window -- and stamp one glyph per cell.
    //
    // fontAtlasTexture is the GL object holding texture slot 0x30, the sheet
    // FUN_00268410 samples. Zero falls back to the stroke font above, which is
    // what a run with no boot bundle gets.
    //
    // Returns the framebuffer y just below the lowest glyph drawn, or 0 when
    // there were none, so a caller can stack under it.
    float drawOriginalOverlay(int framebufferWidth,
                              int framebufferHeight,
                              const std::vector<orphen::ported::debug::DebugGlyph> &glyphs,
                              unsigned int fontAtlasTexture,
                              int fontAtlasWidth,
                              int fontAtlasHeight) const;

  private:
    void drawGlyph(char character, float originX, float originY, float scale) const;
    // originY is the cell's baseline; the glyph box grows upward from it.
    void drawGlyph(char character, float originX, float originY, float scaleX, float scaleY) const;
  };

} // namespace orphen::harness
