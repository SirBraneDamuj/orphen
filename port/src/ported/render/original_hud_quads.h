#pragma once

// One screen-space quad on the `FUN_00207de8` UI path -- the packet builder at
// puGpffffb7b4, submitted straight into a display-list bucket rather than
// projected from a world position.
//
//   src/FUN_0022eb00.c  the pentagon pip: four free corners, one fixed UV rect
//   src/FUN_00207de8.c  the packet, and where the texture halfword is split
//   src/FUN_00207938.c  the GS coordinates the corners are already in
//
// == Coordinates ==
//
// FUN_00207938 writes `x << 4 - 0x8000` and `y << 3 - 0x8000`, and
// FUN_0022eb00's caller has already added the GS origins 0x6C00 and 0x7900.
// Undo both and the corners are plain pixels of the original's 640x448 virtual
// screen -- the same space the dialogue sprites land in, with y growing down.
// Sub-pixel precision survives because FUN_0022EC30 truncates in GS units, so
// the corners are floats here rather than ints.
//
// == The texture halfword ==
//
// `*(short *)(puGpffffb7b4 + 6)` is `(bank << 8) | slot`. FUN_00207de8 copies
// the low byte into the packet's texture field and, for a slot at or above
// 0x18, writes `bank + 1` beside it -- zero meaning "8-bit page, no bank". The
// consumer turns that back into a TEX2_1 write with PSM PSMT4 and CSA = bank,
// which the s01_e012 GS dump shows plainly: 28 draws at CSA 9 (the character
// shadow, FUN_0020DDC8's 0x092A) and a handful more at 10, 12 and 13, against
// 783 ordinary PSMT8 draws at CSA 0.
//
// So `clutBank` is not decoration: slot 0x2A's sheet stores every one of these
// sprites in the same 16 nibble values, and the bank is the only thing that
// makes a pentagon arm red rather than green.

#include <cstdint>

namespace orphen::ported::render
{

  struct HudQuad
  {
    // Four corners in the original's 640x448 virtual screen, in the packet's
    // own vertex order (a triangle fan, so corner 0 is the pivot).
    float x[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float y[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Texels of the 256x256 sheet, matched to the same corner index.
    float u[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    int textureSlot = -1;
    // -1 for an ordinary 8-bit page; 0..15 selects a 16-entry CLUT window and
    // reads the sheet's low nibbles instead.
    int clutBank = -1;

    // The per-vertex colour as the *caller* wrote it, before FUN_00207de8's
    // fold. That fold (:130-141) halves all four channels of a textured
    // packet's RGBAQ and only the alpha of an untextured one, and 0x80 is 1.0
    // on the GS -- so FUN_0022EB00's 0xFF alpha lands at 0.99 and its 0x40 at
    // 0.25, not 0.5. Whoever submits the quad applies it.
    // FUN_0022EB00 gives all four corners the same value, so one is enough.
    std::uint32_t color = 0x80808080u;
    // FUN_00207de8's `& 0x1C000` ladder: 0 none, 1 alpha, 2 additive, 3 the
    // third mode. FUN_0022EB00's mode word is 0x4180, so bit 0x4000 -- 1.
    int blendMode = 1;
  };

} // namespace orphen::ported::render
