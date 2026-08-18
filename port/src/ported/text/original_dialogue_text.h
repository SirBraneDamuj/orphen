#pragma once

// The dialogue system's glyph sheet, and the sprite list a line of text turns
// into.
//
//   src/FUN_00221fd8.c  binds texture 0x173 into slot 0x2E and 0x172 into 0x2F
//   src/FUN_00238c90.c  measures every cell of one of those sheets and writes
//                       the proportional width table at 0x0031C518
//   src/FUN_00238e50.c  reads one width out of it
//   src/FUN_00238a08.c  one glyph of a dialogue line: slot, cell, advance
//   src/FUN_00238608.c  a whole string in one call, used for menu captions
//   src/FUN_002391d0.c  the "waiting for input" prompt sprite
//   src/FUN_00237fc0.c  steps the prompt's four-frame animation
//   src/FUN_00239020.c  turns one entry into a GS sprite
//   src/FUN_00207938.c  the GS packet, and where the screen units come from
//
// == Screen units ==
//
// FUN_00207938 writes x at `<< 4` and y at `<< 3` about the 2048-pixel GS
// centre, so a horizontal unit is one output pixel and a vertical unit is half
// of one -- the same 640x448 virtual screen the debug overlay lands on, which
// is what the 640x224 field looked like once the display stretched it back to
// 4:3. FUN_00239020 negates y on the way in, so a *larger* entry y is further
// up the screen.
//
//   screenX = entryX + 320
//   screenY = 224 - entryY
//
// FUN_00237b38 starts a window at entry (-0x130, -0x78), which is x = 16 and
// y = 344: the bottom-left, one 22-unit line clear of the bottom edge.
//
// == The glyph sheet ==
//
// Eleven columns of 22x22 cells, indexed by `character - 0x20`, in slot 0x2E
// until the index runs past the sheet at character 0x99 and continues in slot
// 0x2F. The cells are a fixed pitch but the font is proportional: FUN_00238c90
// walks each cell at boot looking for the rightmost column with ink in it and
// stores `column + 2` (or 6 for a blank) as the character's width. The port
// does the same scan over the decoded texture -- see FUN_00238c90_measure --
// rather than shipping a copy of the table, because the table is not in the
// executable's data. The values it computes match the live table in the EE
// dump for all 121 cells of slot 0x2E.
//
// Text is drawn at 90% of the measured width: FUN_00238a08 advances by
// `(width * 0x5A) / 100`, which is FUN_00238608's `(cellWidth * 100) / 22`
// with the shipped 20-wide cell.

#include "ported/resource/bmpa_texture_decoder.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace orphen::ported::text
{

  // Texture slots, from FUN_00221fd8's boot binds.
  inline constexpr int kFontSlotLow = 0x2E;   // texture 0x173, characters 0x20..0x98
  inline constexpr int kFontSlotHigh = 0x2F;  // texture 0x172, characters 0x99..0xFB
  inline constexpr int kPromptSlot = 0x2A;    // texture 0x178, the book
  inline constexpr int kButtonIconSlot = 0x2C; // texture 0x176, the four face buttons

  inline constexpr int kCellSize = 0x16;   // 22, both axes
  inline constexpr int kColumns = 0x0B;    // 11
  inline constexpr int kFirstCharacter = 0x20;
  // FUN_00238a08:52. Past this the cell has run off the bottom of the low sheet.
  inline constexpr int kLowSheetLastV = 0xF1;

  // FUN_00238a08:62-64. The shipped dialogue cell is 20x22 and the advance is
  // the measured width at 90%.
  inline constexpr int kDrawnCellWidth = 0x14;
  inline constexpr int kDrawnCellHeight = 0x16;
  inline constexpr int kAdvancePercent = 0x5A; // 90

  // FUN_00237b38:14-15, the window origin, and the two offsets the glyph and
  // prompt builders add to it.
  inline constexpr int kWindowOriginX = -0x130;
  inline constexpr int kWindowOriginY = -0x78;
  inline constexpr int kGlyphOriginBias = 8;   // FUN_00238a08:34
  inline constexpr int kPromptOriginBias = 0x10; // FUN_002391d0:37
  inline constexpr int kLineStep = -kCellSize;

  // FUN_002391d0:51-56 and the table at 0x0031C630. Four cells in a 2x2 block
  // of texture 0x178, sampled 15x15 out of 16 and drawn at 20x22. The frames
  // run round the block rather than across it: top-left, bottom-left,
  // bottom-right, top-right.
  struct PromptFrame
  {
    int u = 0;
    int v = 0;
  };
  inline constexpr std::array<PromptFrame, 4> kPromptFrames{{{96, 32}, {96, 48}, {112, 48}, {112, 32}}};
  inline constexpr int kPromptSourceSize = 0x0F;
  inline constexpr int kPromptDrawWidth = 0x14;
  inline constexpr int kPromptDrawHeight = 0x16;
  // FUN_00237fc0:77-91. The timer runs on frame ticks, each frame holds for
  // 0x80 of them, and the cycle wraps past 0x200.
  inline constexpr int kPromptTicksPerFrame = 0x80;
  inline constexpr int kPromptCycleTicks = 0x200;

  // The entry colour at +0x30, which FUN_00207938 writes straight into the GS
  // RGBAQ register: R in bits 0-7, then G, B, A, with 0x80 standing for 1.0
  // through the texture function's (Ct * Cv) >> 7. FUN_00237b38 opens a window
  // at 0x80808080, plain white.
  inline constexpr std::uint32_t kColorDefault = 0x80808080;
  // FUN_00239760:13. The speaker's name is drawn in this instead -- R 0x00,
  // G 0x60, B 0x60, so a dark cyan against the white of the line.
  inline constexpr std::uint32_t kColorSpeaker = 0x80606000;

  // One FUN_00239020 entry, reduced to what a 2D blit needs. Screen coordinates
  // are already in the 640x448 top-left space; source coordinates are texels of
  // the 256x256 slot.
  struct DialogueSprite
  {
    int textureSlot = kFontSlotLow;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int u = 0;
    int v = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::uint32_t color = kColorDefault;
  };

  // The proportional width table FUN_00238c90 builds, at 0x0031C518.
  class DialogueFont
  {
  public:
    // FUN_00238c90(buffer, bank). `bank` is 0 for slot 0x2E and 1 for 0x2F,
    // which is where in the table the 121 measured widths land.
    void FUN_00238c90_measure(int bank, const orphen::ported::resource::BmpaTexture &sheet);
    bool measured() const { return measured_; }

    // FUN_00238e50.
    std::uint8_t FUN_00238e50_width(unsigned char character) const { return width_[character]; }
    // The drawn advance, `(width * 0x5A) / 100`.
    int advance(unsigned char character) const;

  private:
    std::array<std::uint8_t, 256> width_{};
    bool measured_ = false;
  };

  // FUN_00238a08 in a loop: one line of text starting at `originX`/`originY` in
  // *entry* coordinates, returning sprites in screen coordinates. `penX` is
  // sGpffffbcdc, the running offset, and comes back advanced so a caller can
  // put a prompt after the last glyph.
  std::vector<DialogueSprite> FUN_00238a08_layoutLine(std::string_view line,
                                                      const DialogueFont &font,
                                                      int originX,
                                                      int originY,
                                                      int &penX);

  // FUN_002391d0 plus FUN_00237fc0's animation step.
  DialogueSprite promptSprite(int originX, int originY, int penX, int animationTicks);

} // namespace orphen::ported::text
