#pragma once

// The converging streaks opcode 0x114 throws and opcode 0x115 puts away -- an
// eighth particle system, whose globals sit immediately after the hit sparks'.
//
// Source map:
//   src/FUN_00220f70.c  0x00220F70  spawn a group -- opcode 0x114's target
//   src/FUN_002218f0.c  0x002218F0  release one group, or all of them: 0x115
//   src/FUN_00221398.c  0x00221398  walk the ten groups once a frame
//   src/FUN_00221608.c  0x00221608  one entry: step it, then build its quad
//   src/FUN_00262dd8.c  0x00262DD8  opcode 0x114
//   src/FUN_00262f10.c  0x00262F10  opcode 0x115
//
// DAT_00355B80 is the base, DAT_00355B84 the ten-entry group table,
// DAT_00355B88 the count of groups in use and DAT_00354CC8 the gate. Three
// thousand entries of 0x84 in ten fixed buffers of 300, which is a different
// shape from the hit sparks' ten buffers of 100 at DAT_00355B74 even though the
// group table is byte-for-byte the same four-byte record.
//
// ---- what a streak does --------------------------------------------------
//
// It **converges**. Each entry is born on the local +x axis at a random radius
// and walks in toward zero at a fixed speed; its life is set to exactly the
// number of ticks that walk takes, and when the life runs out the entry is put
// back at its starting radius rather than freed. So a group runs forever, at a
// steady rate, until opcode 0x115 releases it -- there is no self-expiry
// anywhere in the pool.
//
// The direction is per entry: a yaw and a pitch, each the burst's own base
// angle plus or minus a random share of the spread, with the sign of the jitter
// its own coin flip. Those two turns plus the burst's origin become a matrix
// that is built **once** and cached in the entry at +0x44, which is what makes
// the record 0x84 bytes rather than 0x44.
//
// The alpha runs the other way from every other pool here: `age * 255 / life`,
// so a streak fades **in** as it closes on the centre and then snaps back to
// nothing when it wraps.
//
// ---- the quad ------------------------------------------------------------
//
// A screen-aligned rectangle in GS units around the projected point, 32 by 16
// units of +0x1C and scaled by `400 * q` -- the same construction the fountain
// pool uses, off the same sprite (DAT_00315A18 is a third copy of
// DAT_00315858's ST pairs). The blend word is 0x10008080, so bit 0x8000 picks
// mode 2, additive. A zero colour means 0xF0F0F0 and CLUT bank 3.
//
// Skipped while the projected q is above DAT_00352418, which is the same 0.7
// near cutoff the dust and fountain pools have.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"
#include "ported/render/original_view_projection.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // One entry of the pool at DAT_00355B80. Offsets are the entry's own.
  struct GatherStreak
  {
    float originX00 = 0.0f; // +0x00, the burst's origin, shared by the group
    float originY04 = 0.0f; // +0x04
    float originZ08 = 0.0f; // +0x08
    // +0x0C. The live position along the local +x axis, walked down to zero.
    float localX0c = 0.0f;
    float localY10 = 0.0f; // +0x10, always zero
    float localZ14 = 0.0f; // +0x14, always zero
    std::uint32_t colour18 = 0; // +0x18, RGB; zero means 0xF0F0F0 and bank 3
    float size1c = 0.0f;        // +0x1C, the quad's half extent
    float yaw20 = 0.0f;         // +0x20, turned about Y and **negated**
    float pitch28 = 0.0f;       // +0x28, turned about Z and negated
    std::int16_t age34 = 0;     // +0x34
    // +0x36. `trunc(radius / speed) * 32` -- exactly the walk, in ticks.
    std::int16_t life36 = 0;
    float radius38 = 0.0f; // +0x38, where a wrap puts localX0c back
    float speed3c = 0.0f;  // +0x3C, per tick before the frame delta
    // +0x40. Negative is free; the group walk stops at the first free entry.
    std::int8_t alive40 = -1;
    // +0x41. Set once the matrix at +0x44 has been built.
    std::uint8_t matrixCached41 = 0;
    std::int8_t group42 = 0; // +0x42, the buffer index it came from
    // +0x44..+0x84. The entry's own yaw-pitch-translate, cached.
    orphen::ported::render::Matrix4 matrix44{};

    bool alive() const { return alive40 >= 0; }
  };

  // One entry of DAT_00355B84. The same four-byte record the hit sparks use.
  struct GatherGroup
  {
    std::int8_t buffer = 0;  // +0x00, which 300-entry slice this group owns
    std::int16_t count = 0;  // +0x02, how many of it are in use
  };

  struct GatherStreakDraw
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float size = 0.0f;
    std::uint32_t colour = 0;
    int alpha = 0;
  };

  class GatherParticlePool
  {
  public:
    static constexpr std::size_t kGroupCount = 10;
    static constexpr std::size_t kGroupCapacity = 300;
    static constexpr std::size_t kCount = kGroupCount * kGroupCapacity; // 3000

    void reset();

    // FUN_00220F70(x, y, z, speed, radiusRange, yaw, pitch, spread, count,
    //              colour, size). Returns the group's buffer index, or -1 when
    // the guards reject the call and 0 when all ten groups are busy -- the
    // original's own three return values, and 0 is ambiguous with buffer 0.
    std::int8_t FUN_00220f70_spawn(float x, float y, float z,
                                   float speed,
                                   float radiusRange,
                                   float yaw,
                                   float pitch,
                                   float spread,
                                   std::int16_t count,
                                   std::uint32_t colour,
                                   float size,
                                   const std::function<std::uint32_t()> &random);

    // FUN_002218F0. A negative group clears every group and every entry; a
    // non-negative one clears just that group's 300.
    //
    // `hitSparkActiveGroups` is not a typo. The per-group branch decrements
    // **DAT_00355B7C**, which is the *hit spark* pool's active-group count --
    // gp-0x43F4, one word below this pool's own DAT_00355B88 at gp-0x43E8.
    // Reproduced, because the hit spark draw is gated on that word and this
    // really does turn it off.
    void FUN_002218f0_release(std::int8_t group, std::int8_t *hitSparkActiveGroups);

    // FUN_00221398 and FUN_00221608's step half.
    void FUN_00221398_step(std::uint32_t frameTicks);

    const std::vector<GatherStreakDraw> &drawList() const { return draws_; }
    // Emptied on a frame FUN_002192C0 never runs, so the pool emits nothing
    // while its records stand still. See PortRuntime's gate on DAT_00354D2C.
    void clearFrameDraws() { draws_.clear(); }
    int DAT_00355b88_activeGroups() const { return activeGroups_; }
    bool DAT_00354cc8_gate() const { return gate_; }

  private:
    std::array<GatherStreak, kCount> streaks_{};
    std::array<GatherGroup, kGroupCount> groups_{};
    std::vector<GatherStreakDraw> draws_;
    int activeGroups_ = 0;
    bool gate_ = false;
  };

  orphen::ported::render::SpriteQuad
  FUN_00221608_build_gather_quad(const GatherStreakDraw &streak,
                                 std::int32_t gsOriginX, std::int32_t gsOriginY,
                                 float viewZ,
                                 float projectionScaleX, float projectionScaleY,
                                 float screenCentreX, float screenCentreY);

  // DAT_00352418, the same 0.7 near cutoff the other two screen-space pools
  // read out of their own copy of it.
  inline constexpr float kDAT_00352418_gatherNearCutoff = 0.699999988079071f;

} // namespace orphen::ported::entity
