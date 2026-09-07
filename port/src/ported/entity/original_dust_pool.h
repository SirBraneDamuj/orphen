#pragma once

// The fourth particle system: the dust puffs an impact kicks up.
//
// Source map:
//   src/FUN_0021a698.c  0x0021A698  carve the pool out of the heap cursor
//   src/FUN_0021a730.c  0x0021A730  find a free entry
//   src/FUN_0021a4f8.c  0x0021A4F8  spawn a ring of them
//   src/FUN_0021a170.c  0x0021A170  spawn one, with the jitter rolled for you
//   src/FUN_00219af0.c  0x00219AF0  spawn a ring with the jitter rolled per
//                                   particle -- what the impact helpers call
//   src/FUN_0021a760.c  0x0021A760  walk the pool once a frame
//   src/FUN_0021a820.c  0x0021A820  one entry: step it, then build its quad
//
// It shares nothing with the other three. DAT_00355620 is 1536 entries with an
// installed behaviour; DAT_00355B74 is a thousand sparks in ten groups drawn as
// world-space streaks; this one is **a thousand entries of 0x28** with no
// behaviour pointer at all -- FUN_0021A820 is the only thing that ever steps
// one -- and it draws a screen-aligned rectangle whose corners are built in GS
// units around a projected origin, the way FUN_002D3058's are.
//
// ---- what a puff does ----------------------------------------------------
//
// Two lines of motion, and that is all: it rises by 0.003 a tick, and if it has
// a heading it slides along it by 0.01 a tick. Everything else is the fade --
// the alpha is `(remaining << 7) / total`, so it runs 0x80 down to 0 over the
// entry's own life, and the entry frees itself when the timer expires.
//
// ---- the quad ------------------------------------------------------------
//
// `+0x21` picks between two rectangles, both sixteen units of `+0x18` wide:
//
//   0  y from -16*size to 0      -- the puff hangs below its anchor
//   1  y from -8*size to +8*size -- centred
//
// and both are scaled by `400 * q` where q is the projected 1/w. The colour is
// `+0x24`, or 0xF0F0F0 when that is zero, and a zero colour also selects a
// different CLUT bank in the packet -- the one visible difference between the
// two kinds of puff.
//
// A puff is skipped, not drawn, while `q > 0.7`: it has a **near** cutoff, not
// a far one, so a puff closer than about a unit and a half is invisible.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

#include <array>
#include <cstdint>
#include <functional>

namespace orphen::ported::entity
{

  // One entry of the pool at DAT_00355A9C. Offsets are the entry's own; +0x10
  // and +0x14 are the jitter ranges the spawner already applied and nothing
  // reads back, kept because the original stores them.
  struct DustPuff
  {
    float x00 = 0.0f; // +0x00, world position
    float y04 = 0.0f; // +0x04
    float z08 = 0.0f; // +0x08
    // +0x0C. The heading it slides along. **Exactly zero means it does not
    // slide at all** -- the step tests the float against zero, not a flag.
    float heading0c = 0.0f;
    float jitterX10 = 0.0f; // +0x10
    float jitterY14 = 0.0f; // +0x14
    float size18 = 0.0f;    // +0x18, half the quad in units of sixteen
    std::int16_t remaining1c = 0; // +0x1C, ticks left
    std::int16_t total1e = 0;     // +0x1E, ticks it started with
    // +0x20. 0xFF is free, and the spawn walk tests nothing else.
    std::int8_t alive20 = -1;
    // +0x21. 0 is the rectangle that hangs below the anchor, 1 the centred one.
    std::uint8_t shape21 = 0;
    std::uint32_t colour24 = 0; // +0x24, RGB; zero means 0xF0F0F0 and bank 2

    bool alive() const { return alive20 >= 0; }
  };

  class DustPool
  {
  public:
    static constexpr std::size_t kCount = 1000;

    // FUN_0021A698: 40000 bytes off the heap cursor, every entry marked free.
    void FUN_0021a698_reset();

    // FUN_0021A730: the first free entry, or none.
    DustPuff *FUN_0021a730_allocate();

    // FUN_0021A760 / FUN_0021A820's step half. The draw half is collected at
    // publish time, the way both other pools are split.
    void FUN_0021a760_step(std::uint32_t frameTicks);

    // FUN_0021A4F8(x, y, z, size, jitterX, jitterY, radius, lifeSpread, count,
    //              colour, shape). A ring of `count` puffs at `radius`, each
    // with its own random offset inside the two jitter ranges and its own life
    // of `5 .. 5 + lifeSpread` frames.
    void FUN_0021a4f8_spawn_ring(float x, float y, float z,
                                 float size,
                                 float jitterX,
                                 float jitterY,
                                 float radius,
                                 std::int16_t lifeSpread,
                                 int count,
                                 std::uint32_t colour,
                                 std::uint8_t shape,
                                 const std::function<std::uint32_t()> &random);

    // FUN_00219AF0. Two nested rings: the outer one is walked here, rolling
    // both jitters per step and folding the first into the position, and each
    // step hands a whole inner ring to FUN_0021A4F8. `lit` picks 0xFFFFFF
    // over 0.
    void FUN_00219af0_spawn_impact(float x, float y, float z,
                                   float size,
                                   float jitterX,
                                   float jitterY,
                                   float radius,
                                   std::int16_t lifeSpread,
                                   int outerCount,
                                   int innerCount,
                                   std::uint8_t shape,
                                   bool lit,
                                   const std::function<std::uint32_t()> &random);

    // FUN_0021A170: one puff, jitter rolled, no ring.
    void FUN_0021a170_spawn_one(float x, float y, float z,
                                float size,
                                float jitterX,
                                float jitterY,
                                std::int16_t lifeSpread,
                                int count,
                                std::uint8_t shape,
                                bool lit,
                                const std::function<std::uint32_t()> &random);

    const std::array<DustPuff, kCount> &puffs() const { return puffs_; }
    std::size_t aliveCount() const;

  private:
    std::array<DustPuff, kCount> puffs_{};
    // DAT_00355A98. Incremented on spawn, decremented when an entry ages out,
    // and the walk's own guard -- FUN_0021A760 does nothing while it is zero.
    std::int32_t live_ = 0;
  };

  // What FUN_0021A820 needs from the projection to place one puff.
  struct DustQuadInputs
  {
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    float size = 0.0f;
    std::uint8_t shape = 0;
    std::uint32_t colour = 0;
    int alpha = 0x80; // 0..0x80, from (remaining << 7) / total
  };

  orphen::ported::render::SpriteQuad FUN_0021a820_build_dust_quad(const DustQuadInputs &inputs);

  // fGpffff839c. A puff whose projected q is above this is skipped.
  inline constexpr float kFGpffff839c_dustNearCutoff = 0.699999988079071f;

} // namespace orphen::ported::entity
