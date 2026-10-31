#pragma once

// Eight full-screen texture layers at DAT_00356100, each a sheet stretched
// over the whole picture with an optional UV scroll.
//
//   src/FUN_00202f00.c  fill a slot -- opcode 0x138 (FUN_002652D0)
//   src/FUN_00202f88.c  empty a slot -- opcode 0x139 (FUN_00265350)
//   src/FUN_00202ff8.c  scroll and submit every live slot
//
// FUN_002000c0:224 calls FUN_00202ff8 once per frame while the live count
// cGpffffba90 (DAT_00355A00) is non-zero, inside the same `DAT_00354d2c != 0xc`
// test as the smear, and after FUN_002239c8 has already submitted the fade and
// the letterbox bars.
//
// == The record (stride 0x18) ==
//
//   +0x00 byte   flags: 0x80 live, bits 0-1 the blend, bit 2 the UV table
//   +0x02 short  texture slot (FUN_00207de8's halfword, slot itself)
//   +0x04 word   RGBA, the same for all four corners
//   +0x08 float  U offset   -- the int argument as a float, *not* scaled
//   +0x0C float  V offset
//   +0x10 float  U per frame -- the argument * 1/65536
//   +0x14 float  V per frame
//
// == The quad ==
//
// The corners come from DAT_003149A8 and are raw GS coordinates: the mode word
// carries 0x10000000, which makes FUN_00207de8 copy +0x20/+0x24 into XYZ2 as
// integers rather than truncating floats. 0x6C00..0x9400 by 0x7900..0x8700 is
// the whole 640x448 screen. The UVs are DAT_003149C8 (0..1 square) or, with
// flag bit 2, DAT_003149D8, plus the scroll; FUN_00207de8 emits them as
// `uv * 4096` in 4.12, i.e. in units of a 256-texel sheet.
//
// The blend is `flags & 3` through the mode words 0x10000780 / 0x10004780 /
// 0x10008780 / 0x10010780, which FUN_00207de8's `& 0x1C000` ladder reads as
// none, alpha, additive and the third mode. Bucket 0x1007, submitted after the
// fade and the bars, and a bucket draws LIFO -- so this lands under both of
// them and over the 0x1006 smear and pips.
//
// Nothing clears a slot on a scene load: only 0x139 does, and no retail script
// calls 0x139. A layer left up rides into the next scene.

#include "ported/render/original_hud_quads.h"

#include <array>
#include <cstdint>
#include <vector>

namespace orphen::ported::render
{

  class ScreenLayers
  {
  public:
    static constexpr int kSlotCount = 8;

    void FUN_00202f00_set(std::int32_t slot,
                          std::uint8_t flags,
                          std::uint16_t texture,
                          std::uint32_t colour,
                          std::int32_t uOffset,
                          std::int32_t vOffset,
                          std::int32_t uRate,
                          std::int32_t vRate);
    void FUN_00202f88_clear(std::int32_t slot);

    // DAT_00355A00. FUN_002000c0 skips the whole pass while it is zero.
    std::uint8_t liveCount() const { return cGpffffba90_liveCount_; }

    // One frame of FUN_00202ff8: advance every live slot's scroll, wrap it,
    // and return its quad in slot order.
    std::vector<HudQuad> FUN_00202ff8_step();

  private:
    struct Slot
    {
      std::uint8_t flags = 0;
      std::uint16_t texture = 0;
      std::uint32_t colour = 0;
      float uOffset = 0.0f;
      float vOffset = 0.0f;
      float uRate = 0.0f;
      float vRate = 0.0f;
    };

    std::array<Slot, kSlotCount> DAT_00356100_slots_{};
    std::uint8_t cGpffffba90_liveCount_ = 0;
  };

} // namespace orphen::ported::render
