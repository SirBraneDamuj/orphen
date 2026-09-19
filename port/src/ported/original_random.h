#pragma once

// The game's **random number generator**, which is not an LCG.
//
// Source map:
//   FUN_00216708  0x00216708  seed the table (called once, from FUN_002000C0)
//   FUN_00216800  0x00216800  stir the whole table
//   FUN_00216868  0x00216868  draw one 32-bit word
//
// It is a lagged-Fibonacci shift register over a 521-word table at
// DAT_0055F0A0, with lags (521, 32) and XOR as the combiner -- the classic
// R521. The index lives at DAT_00355A60 (gp-0x4510) and runs 0..0x208.
//
// **Ghidra decompiles the draw as returning void.** It does not: the
// disassembly at 0x002168B8 leaves the fresh word in `$v0` and the `sw` that
// follows is in the delay slot, so `v0 = x[i] ^ x[j]` is the return value.
// `FUN_00212DB0` reads it as one -- `uVar2 = FUN_00216868()` -- and so do the
// other call sites. Trusting the void signature is how this ended up stubbed.
//
// ---- why a stand-in was not good enough ----------------------------------
//
// The port ran a plain LCG here, returning `(state >> 16) & 0x7FFF`. Fifteen
// bits. Most callers only want a small range and never noticed, but the smoke
// cloud seeds a **16-bit torus angle** per axis straight from a draw
// (FUN_00212DB0:49, `*puVar5 = FUN_00216868()`), so half of every axis was
// unreachable: the 3x3x3 box only ever filled about 1.8 units of its 3, the
// cloud stopped dead partway across the screen, and what did fit was denser
// than hardware because the same particle count was packed into 60% of the
// volume. Measured against a GS dump of s01_e014, hardware's 2236 quads spread
// smoothly across the whole frame and past both edges; the port's stopped at
// x = 526 with nothing beyond it.
//
// The seed is the literal 0x12345678 that FUN_002000C0:158 passes, so this is
// as reproducible as the stand-in was.

#include <array>
#include <cstdint>

namespace orphen::ported
{

  class OriginalRandom
  {
  public:
    // FUN_002000C0:158 seeds once at boot with this.
    static constexpr std::uint32_t kBootSeed = 0x12345678u;

    OriginalRandom() { FUN_00216708_seed(kBootSeed); }

    // FUN_00216708.
    void FUN_00216708_seed(std::uint32_t seed)
    {
      // :9-19. Seventeen words, each built one bit at a time from the top bit
      // of an LCG -- 32 steps per word, shifting right so the first bit drawn
      // ends up in bit 0.
      std::uint32_t lcg = seed;
      for (int word = 0; word < 17; ++word)
      {
        std::uint32_t accumulator = 0;
        for (int bit = 0; bit < 32; ++bit)
        {
          lcg = lcg * 0x5D588B65u + 1u;
          accumulator = (accumulator >> 1) | (lcg & 0x80000000u);
        }
        table_[static_cast<std::size_t>(word)] = accumulator;
      }

      // :21. The first step of the spread is special-cased: where the loop
      // below would read x[i - 17], this reads x[16] itself.
      table_[16] = (table_[16] << 23) ^ (table_[0] >> 9) ^ table_[15];
      // :22-27. The remaining 504 words.
      for (int i = 17; i < kTableWords; ++i)
      {
        const auto at = static_cast<std::size_t>(i);
        table_[at] = (table_[at - 17] << 23) ^ (table_[at - 16] >> 9) ^ table_[at - 1];
      }

      FUN_00216800_stir();
      FUN_00216800_stir();
      // :30. The next draw increments past 0x208 and wraps to 0.
      DAT_00355a60_index_ = 0x208;
    }

    // FUN_00216800. One pass of the recurrence over the whole table, in the
    // two halves the wrap splits it into.
    void FUN_00216800_stir()
    {
      for (int i = 0; i < 32; ++i)
      {
        table_[static_cast<std::size_t>(i)] ^= table_[static_cast<std::size_t>(i + 0x1E9)];
      }
      for (int i = 32; i < kTableWords; ++i)
      {
        table_[static_cast<std::size_t>(i)] ^= table_[static_cast<std::size_t>(i - 0x20)];
      }
    }

    // FUN_00216868. One draw: advance the cursor, fold the word 32 behind it
    // in, and hand back the result.
    std::uint32_t FUN_00216868_draw()
    {
      ++DAT_00355a60_index_;
      if (DAT_00355a60_index_ > 0x208)
      {
        DAT_00355a60_index_ = 0;
      }
      int lag = DAT_00355a60_index_ - 0x20;
      if (lag < 0)
      {
        lag = DAT_00355a60_index_ + 0x1E9;
      }
      const auto at = static_cast<std::size_t>(DAT_00355a60_index_);
      table_[at] ^= table_[static_cast<std::size_t>(lag)];
      return table_[at];
    }

  private:
    // 0x209 words at DAT_0055F0A0.
    static constexpr int kTableWords = 0x209;
    std::array<std::uint32_t, kTableWords> table_{};
    // DAT_00355A60, gp-0x4510.
    int DAT_00355a60_index_ = 0;
  };

} // namespace orphen::ported
