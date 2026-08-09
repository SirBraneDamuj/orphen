#pragma once

// The debug overlay's text pipeline.
//
//   src/FUN_002681c0.c  the printf: format into a scratch buffer, append to
//                       DAT_00572c38, gated on DAT_003555dc and DAT_003555da
//   src/FUN_00268270.c  the layout pass: walk that buffer once a frame,
//                       placing one glyph per printable character, then reset
//                       the length
//   src/FUN_00268410.c  one glyph, as a GS sprite through FUN_00207938
//   src/FUN_00268558.c  the strcat the printf ends with
//   src/FUN_002685e8.c  the strlen the '~' escape measures with
//
// The port stops at "character C belongs at (x, y) and samples this texel
// window". FUN_00268410 hands FUN_00207938 a 10x20 quad textured from a 7x15
// window of texture slot 0x30 -- an 8x16 cell grid, 32 columns wide, three
// rows covering 0x20..0x7F, so a 256x48 band. FUN_00221fd8 binds slot 0x30 to
// texture 0x179 at boot, which the EE dump confirms (DAT_003429a8[0x30] ==
// 0x179), and the port already loads it: it is one of
// EntityModelStore::FUN_00221fd8_bind_boot_textures' seven fixed binds,
// resolved out of the s00_e000 boot bundle.
//
// The atlas is a 256x256 sheet shared with the chest and title art. **Its font
// band is at the bottom in storage order** -- v = 0 is the last stored row,
// which is exactly the flip decodeBmpaTexture already applies, so the window
// coordinates below index the decoded image directly.
//
// == Coordinate space ==
//
// FUN_00268270 works in the units it hands FUN_00268410, which negates the
// vertical: x runs -0x130..+0x130 about the screen centre, y runs +0xD8 (top)
// downward in steps of 0x14. FUN_00207938 then writes x at <<4 and y at <<3,
// so with the shipped GS geometry (SCISSOR_1 640x224, XYOFFSET_1 centred on
// 320 x 112) one x unit is one framebuffer pixel and one y unit is half of
// one -- the framebuffer is a field, displayed at 448 lines.
//
// Everything here is therefore expressed on a 640x448 virtual screen, which is
// what those units measure and what the glyph is square in:
//
//   screenX = 320 + x      screenY = 224 - y
//
// giving a left margin of 16, a first line at y = 8, a 10x20 glyph cell, a
// 12-pixel advance and a 20-pixel line pitch.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orphen::ported::debug
{

  // The virtual screen the layout below is measured on, and the glyph metrics
  // read off FUN_00268270 and FUN_00268410.
  namespace text
  {
    inline constexpr int kScreenWidth = 640;
    inline constexpr int kScreenHeight = 448;

    // FUN_00268410's param_5 / param_6: the sprite quad, in screen units.
    inline constexpr int kGlyphCellWidth = 10;
    inline constexpr int kGlyphCellHeight = 20;

    // FUN_00221fd8's slot for the atlas, and the cell grid inside it.
    inline constexpr int kFontTextureSlot = 0x30;
    inline constexpr int kAtlasCellWidth = 8;
    inline constexpr int kAtlasCellHeight = 16;
    inline constexpr int kAtlasColumns = 32;
    inline constexpr int kWindowWidth = 7;  // FUN_00268410's stacked uw
    inline constexpr int kWindowHeight = 15; // and vh
    inline constexpr int kWindowInsetU = 1;  // the "| 1" and "+ 1" in the call
    inline constexpr int kWindowInsetV = 1;

    // FUN_00268270's own constants.
    inline constexpr int kLeftMargin = -0x130;  // iVar3's reset value
    inline constexpr int kFirstLine = 0xd8;     // iVar5's initial value
    inline constexpr int kAdvance = 0xc;        // per printable character
    inline constexpr int kLineStep = 0x14;      // per newline
    inline constexpr int kRightLimit = 0x130;   // wrap once x exceeds this
    inline constexpr int kEscapeLine = -0xbe;   // the '~' escape's line
    inline constexpr int kEscapeRight = 0x140;  // and its right edge

    // DAT_00572c38's cap. FUN_002681c0 drops an append that would cross it.
    inline constexpr std::size_t kBufferSize = 0x800;

    // FUN_002681c0's own stack scratch, which caps one formatted append.
    inline constexpr std::size_t kFormatScratchSize = 4096;
  } // namespace text

  // One placed character, on the 640x448 virtual screen described above.
  // (x, y) is the top-left of the 10x20 cell.
  struct DebugGlyph
  {
    char character = ' ';
    int x = 0;
    int y = 0;
  };

  // FUN_00268410's texel window for a character, in the decoded atlas. The
  // original derives it as
  //   cell = (c & 0xFF) - 0x20
  //   u0 = ((cell & 0x1F) << 3) | 1        v0 = (cell >> 5) * 16 + 1
  // over a 7x15 extent. Characters below 0x20 reuse the arithmetic on c - 1,
  // which the layout pass never lets through.
  struct GlyphWindow
  {
    int u = 0;
    int v = 0;
    int width = text::kWindowWidth;
    int height = text::kWindowHeight;
  };

  GlyphWindow FUN_00268410_glyphWindow(char character);

  class DebugTextBuffer
  {
  public:
    // DAT_003555da: the debug-active byte. FUN_002681c0 clears the output gate
    // when this is off, so a printf with output on but debug off both drops the
    // text *and* latches the gate off.
    void setDAT_003555da_debugActive(bool active) { DAT_003555da_debugActive_ = active; }
    bool DAT_003555da_debugActive() const { return DAT_003555da_debugActive_; }

    // DAT_003555dc / cGpffffb66c: the output gate.
    void setDAT_003555dc_outputEnabled(bool enabled) { DAT_003555dc_outputEnabled_ = enabled; }
    bool DAT_003555dc_outputEnabled() const { return DAT_003555dc_outputEnabled_; }

    // FUN_002681c0. Appends, silently, when either gate is closed or when the
    // append would reach DAT_00572c38's 0x800 cap.
    void FUN_002681c0_printf(const char *format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    // FUN_00268270. Places every printable character in the buffer, then
    // clears DAT_003551dc -- so this both reads and drains, once a frame,
    // exactly as the original does.
    std::vector<DebugGlyph> FUN_00268270_layoutAndDrain();

    // DAT_003551dc = 0 without laying anything out, which is what
    // FUN_0022a418 does across a map load.
    void clear() { DAT_003551dc_length_ = 0; }

    bool empty() const { return DAT_003551dc_length_ == 0; }

  private:
    // DAT_00572c38 and DAT_003551dc. One extra byte so the buffer is always
    // terminated even when a full 0x7FF characters are in it.
    std::array<char, text::kBufferSize + 1> DAT_00572c38_text_{};
    std::size_t DAT_003551dc_length_ = 0;
    bool DAT_003555da_debugActive_ = false;
    bool DAT_003555dc_outputEnabled_ = false;
  };

} // namespace orphen::ported::debug
