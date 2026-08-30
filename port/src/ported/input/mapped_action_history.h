#pragma once

// Native counterpart of the tail of src/FUN_0023b5d8.c (0x0023b5d8) and of
// src/FUN_0023b890.c (0x0023b890): the mapped-action ring at DAT_00342a70 and
// the "was this action asked for in the last N frames" query over it.
//
// FUN_0023b5d8 pushes one word per frame:
//
//     DAT_00355614 = (DAT_00355614 + 1) & 0x3F;
//     DAT_00342a70[DAT_00355614] = (held << 16) | pressed;
//
// where `held` is DAT_003555F8 and `pressed` is DAT_003555FA -- the raw pad's
// low byte run through the remap table at DAT_00571A50, with the high byte
// (d-pad and system buttons) passed straight through. In the EE dump that
// remap is the identity, so a mapped action bit is the raw pad bit:
//
//     0x01 L2   0x02 R2   0x04 L1     0x08 R1
//     0x10 Triangle       0x20 Circle 0x40 Cross   0x80 Square
//
// FUN_0023b890(n) ORs the last n entries together and returns the 32-bit word,
// so a caller testing `& 0x20` is really asking "was Circle newly pressed at
// any point in the last n frames". FUN_00256bb8 asks for 8. That window is the
// game's input buffer: an attack pressed during the tail of a jump still fires
// on the frame the player lands, and the port used to drop it.

#include <array>
#include <cstddef>
#include <cstdint>

namespace orphen::ported::input
{

  class MappedActionHistory
  {
  public:
    // DAT_00342a70's length, and the mask FUN_0023b5d8 advances the cursor by.
    static constexpr std::size_t kSlotCount = 0x40;

    // One frame's worth. `held` is the mapped held set, `pressed` the mapped
    // newly-pressed set; both are 16-bit in the original.
    void FUN_0023b5d8_push(std::uint16_t held, std::uint16_t pressed)
    {
      cursor_ = (cursor_ + 1) & (kSlotCount - 1);
      slots_[cursor_] = (static_cast<std::uint32_t>(held) << 16) |
                        static_cast<std::uint32_t>(pressed);
      // DAT_00355618, clamped to 0x40. FUN_0023b890 will not read further back
      // than the number of frames actually recorded, so a fresh scene cannot
      // see a button press from before it loaded.
      if (recorded_ < kSlotCount)
      {
        ++recorded_;
      }
    }

    // FUN_0023b890. Walks backwards from the newest entry, wrapping at 0x3F.
    std::uint32_t FUN_0023b890_recent(std::size_t frames) const
    {
      std::size_t count = frames < recorded_ ? frames : recorded_;
      std::uint32_t result = 0;
      std::size_t index = cursor_;
      while (count-- != 0)
      {
        result |= slots_[index];
        index = (index - 1) & (kSlotCount - 1);
      }
      return result;
    }

    void reset()
    {
      slots_.fill(0);
      cursor_ = 0;
      recorded_ = 0;
    }

  private:
    std::array<std::uint32_t, kSlotCount> slots_{};
    // DAT_00355614.
    std::size_t cursor_ = 0;
    // DAT_00355618.
    std::size_t recorded_ = 0;
  };

} // namespace orphen::ported::input
