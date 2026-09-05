#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  struct BmpaTexture
  {
    static constexpr std::uint16_t kWidth = 256;
    static constexpr std::uint16_t kHeight = 256;

    std::uint16_t width = kWidth;
    std::uint16_t height = kHeight;
    std::vector<std::uint8_t> rgbaPixels;

    // The sheet before the palette lookup, kept because a texture slot at or
    // above 0x18 is not an 8-bit page on the GS at all.
    //
    // FUN_002103d0:66-80 packs those slots as **PSMT4**: it walks the BMPA
    // index array two bytes at a time and writes `(a & 0xF) | ((b & 0xF) << 4)`,
    // so the texel is the low nibble of the index and the high nibble is
    // discarded. The 16-entry CLUT then comes from CSA, and one 256-entry
    // palette therefore holds sixteen independent 16-colour ramps. That is how
    // a single sheet draws the character shadow, the five pentagon elements and
    // the rest of the UI from the same pixels.
    //
    // The upload swizzles the palette into the GS's 8-bit CLUT layout
    // (FUN_002103d0:110-131, the classic "swap the middle two groups of eight
    // in every pair of rows"), and a 4-bit CSA window reads an 8x2 block back
    // out of that buffer. The two cancel exactly, so bank `b` is palette
    // entries `b * 16 .. b * 16 + 15` in source order, which is what
    // clutBankPixels returns.
    std::vector<std::uint8_t> indices;  // one byte per texel, already in GS v order
    std::vector<std::uint8_t> palette;  // 1024 bytes, source order, BGRA

    // The RGBA8 page this sheet reads as when it is bound 4-bit with CSA
    // `bank`. Empty when the sheet was never decoded or `bank` is out of range.
    std::vector<std::uint8_t> clutBankPixels(int bank) const;
  };

  inline constexpr int kClutBankCount = 16;
  inline constexpr int kClutBankEntries = 16;
  // FUN_002103d0:36. At and above this a slot is uploaded 4-bit; below it the
  // page is a plain 8-bit index into all 256 entries.
  inline constexpr int kFirstFourBitTextureSlot = 0x18;

  // BMPA texture resource decoder. No single original FUN_* counterpart has been isolated yet.
  BmpaTexture decodeBmpaTexture(std::span<const std::uint8_t> bmpaBytes);

} // namespace orphen::ported::resource
