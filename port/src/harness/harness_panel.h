#pragma once

// The drawing kit the fly view's harness panels share: the entity tree, the
// key help, the menu bar and its panels. PC-only. Everything is laid out in
// units of the game debug font's 10x20 cell and scaled to the window by one
// factor, so hit tests and drawing agree by construction.

#include "harness/debug_text.h"

#include <string>
#include <vector>

namespace orphen::harness
{

  struct DebugFont
  {
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
  };

  namespace panel
  {
    inline constexpr int kAdvance = orphen::ported::debug::text::kGlyphCellWidth;
    inline constexpr int kRowPitch = orphen::ported::debug::text::kGlyphCellHeight;
    inline constexpr int kPadding = 6;
    inline constexpr int kMargin = 8;
    // The strip along the top of the fly view the menu bar takes. Panels that
    // hang from the top of the window start below it.
    inline constexpr int kMenuBarHeight = kRowPitch + 4;

    // Pixels per layout unit for a framebuffer this tall.
    float scaleFor(int framebufferHeight);

    // One glyph per non-space character, `columns` at most, from (x, y).
    void appendText(std::vector<orphen::ported::debug::DebugGlyph> &glyphs, const std::string &text, int x, int y,
                    int columns);

    // Glyphs are in layout units; this is the one place they become pixels.
    void drawGlyphs(const DebugTextRenderer &text,
                    const DebugFont &font,
                    int framebufferWidth,
                    int framebufferHeight,
                    float scale,
                    const std::vector<orphen::ported::debug::DebugGlyph> &glyphs,
                    float red,
                    float green,
                    float blue);

    // A flat rectangle in layout units, over whatever is there. Only inside an
    // OverlayState.
    void fillRect(float scale, float left, float top, float width, float height, float red, float green,
                  float blue, float alpha);
    // The same rectangle's one-pixel outline.
    void strokeRect(float scale, float left, float top, float width, float height, float red, float green,
                    float blue, float alpha);

    // The panels' background colour and the heading yellow, so every panel
    // reads as one family.
    void fillPanel(float scale, float left, float top, float width, float height);
    inline constexpr float kHeadingRed = 1.0f;
    inline constexpr float kHeadingGreen = 0.85f;
    inline constexpr float kHeadingBlue = 0.2f;

    // The 2D state the panels draw in, and back.
    class OverlayState
    {
    public:
      OverlayState(int framebufferWidth, int framebufferHeight);
      ~OverlayState();
      OverlayState(const OverlayState &) = delete;
      OverlayState &operator=(const OverlayState &) = delete;

    private:
      unsigned char depth_, texture_, fog_, blend_, cull_, lighting_, alphaTest_;
    };

    // An axis-aligned box in layout units.
    struct Rect
    {
      float left = 0.0f;
      float top = 0.0f;
      float width = 0.0f;
      float height = 0.0f;

      bool contains(float x, float y) const { return x >= left && x < left + width && y >= top && y < top + height; }
    };
  } // namespace panel

} // namespace orphen::harness
