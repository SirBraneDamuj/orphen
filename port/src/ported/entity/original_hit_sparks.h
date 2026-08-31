#pragma once

// Native counterpart of the hit sparks -- the streaks that fly off whatever a
// weapon just connected with:
//
//   src/FUN_002205d0.c  0x002205d0  carve the pool out of the heap cursor
//   src/FUN_002206a8.c  0x002206a8  one burst, fired from FUN_00216140
//   src/FUN_00220910.c  0x00220910  walk the ten groups once a frame
//   src/FUN_00220c00.c  0x00220c00  one spark: step it, then build its quad
//
// This is the **third** particle system in the game and it shares nothing with
// the other two. DAT_00355620 (ported in original_particles.h) is 1536 entries
// with one installed behaviour and screen-space square sprites; this one is a
// thousand entries in ten fixed groups, no behaviour pointer, and a quad that
// is oriented in world space.
//
// ---- the pool ------------------------------------------------------------
//
// FUN_002205d0 takes 56000 bytes -- a thousand entries of 0x38 -- plus a ten
// entry group table, out of the heap cursor at DAT_0035572c. Every entry is
// marked dead with +0x28 = 0xFFFF and +0x2A = 0xFF, and group i is given
// buffer index i, so the groups never move.
//
// **A burst takes the first group whose count is not positive.** Ten hits can
// be showing at once and an eleventh silently shows nothing -- FUN_002206a8
// falls out of its search loop and returns without spawning or reporting.
//
// ---- what a burst looks like ---------------------------------------------
//
// The count is `min(100, damage * 10)`, and that arithmetic is what pins the
// damage numbers down from outside: the flies_hit save state holds two flyers
// struck for one point each and two groups holding ten sparks each.
//
// Every spark in a burst starts at the same point -- the victim's position at
// three quarters of its +0x58 body height -- and differs in two angles:
//
//   +0x1C  a random yaw over the full circle, `(rand % 360) * 2pi / 360`
//   +0x20  the fan angle, starting at 30 degrees and stepping
//          `(360 / count) * 2pi / 360` per spark, with the division on the
//          left an **integer** one
//
// Both are consumed by the matrices in FUN_00220c00_build_quad below. The
// lifetime is 0x780 ticks (about 60 frames at 32 ticks a frame), the speed a
// flat 100, and the scale 0.3 -- or 0.6 for reactions 0x1B and 0x1D, which
// also picks a different LOD.
//
// ---- the geometry --------------------------------------------------------
//
// A spark is a **streak**, not a billboard: four corners at (-1, 0.01),
// (-1, -0.01), (1, -0.01) and (1, 0.01) in the local x/z plane, with y flat
// zero and **only the long axis scaled** by +0x2C. So it is 0.6 units long and
// 0.02 wide however far away it is, which is what makes it read as a spark
// rather than a dot.
//
// Two matrices place it, and they are separate because the streak has to point
// along its own travel while the burst as a whole is oriented off the camera:
//
//   spin   Z(-fanAngle). Turns the long axis to lie along the direction the
//          spark is sliding, which FUN_00220c00 steps as
//          `cos(fan), sin(fan)` in the same local plane.
//   world  Y(yaw) then Z(-cameraYaw - pi/2) then T(spawn position). The Z term
//          is the exact inverse of the view matrix's own yaw
//          (FUN_0020bec8 uses `+ (cameraYaw + pi/2)`), so the burst's plane
//          faces the camera in yaw and the fan opens across the screen.
//
// The travelled offset is added **between** them: it accumulates in the same
// local frame the spin matrix works in, and the world matrix carries it out to
// where the victim was standing.
//
// ---- what the port does differently --------------------------------------
//
// The original steps and draws in one walk. Here FUN_00220910_step runs in the
// simulation and the quads are built at publish time, exactly as the
// DAT_00355620 pool is split -- the same sparks in the same order in the same
// display list bucket.
//
// The original also projects each corner to integer GS coordinates through
// FUN_0020b6a0 before submitting. This port hands the four world-space corners
// to the scene's own projection instead, which is the faithful analogue here:
// unlike the sprite pass, whose corners are *built* in GS integer units around
// a truncated origin, these corners are built in world space and only meet the
// projection at the end.

#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <functional>

namespace orphen::ported::entity
{

  // One entry of DAT_00355b74. Offsets are the entry's own; +0x18 is zeroed on
  // spawn and never read again, so it is left out.
  struct HitSpark
  {
    // +0x00. Where the burst started, in world space. Never changes.
    float x00 = 0.0f;
    float y04 = 0.0f;
    float z08 = 0.0f;

    // +0x0C. How far the spark has travelled, in the burst's own local frame
    // rather than in world space. FUN_00220c00 only ever moves the first two:
    // +0x14 is loaded and stored back untouched every frame.
    float offsetX0c = 0.0f;
    float offsetY10 = 0.0f;
    float offsetZ14 = 0.0f;

    // +0x1C. A random yaw over the full circle, the same for the spark's whole
    // life.
    float yaw1c = 0.0f;
    // +0x20. Where this spark sits in the burst's fan, and the direction it
    // slides in. Also fixed for life.
    float fanAngle20 = 0.0f;

    // +0x24. Ticks left. Counted down by the frame's tick count and tested as a
    // signed 16-bit value, so it dies on the frame it would go negative.
    std::uint16_t lifetime24 = 0;
    // +0x26. Written with the same 0x780 and never read.
    std::uint16_t lifetime26 = 0;

    // +0x28. FUN_00216140's second argument: 1 when the victim is on the
    // player's side. It picks the texture rectangle, and **0xFFFF in it is what
    // marks the entry dead** -- the walk stops at the first negative one.
    std::int16_t sourceSide28 = -1;
    // +0x2A. Which group owns this entry, so FUN_00220ba8 can find the count to
    // decrement. 0xFF when dead.
    std::uint8_t group2a = 0xFF;

    // +0x2C. Half the streak's length: DAT_003523F8 (0.3), or DAT_003523FC
    // (0.6) for reactions 0x1B and 0x1D.
    float scale2c = 0.0f;
    // +0x30. A flat 100.0 for every spark the game spawns.
    float speed30 = 0.0f;
    // +0x34. Set for reactions 0x1B and 0x1D. Only chooses a mip bias in the
    // GS packet, which this port does not reproduce.
    std::uint8_t bigReaction34 = 0;

    bool alive() const { return sourceSide28 >= 0; }
  };

  // One entry of DAT_00355b78. Four bytes: the buffer index, then the live
  // count as a halfword at +0x02.
  struct HitSparkGroup
  {
    std::uint8_t buffer = 0;
    std::int16_t count = 0;
  };

  // --- the constants FUN_002206a8 and FUN_00220c00 read ---------------------

  // DAT_003523F8 and DAT_003523FC.
  inline constexpr float kDAT_003523f8_scale = 0.300000012f;
  inline constexpr float kDAT_003523fc_bigScale = 0.600000024f;
  // DAT_00352400 / DAT_00352408, both 2pi to the same seven digits the ELF
  // holds -- and neither is the pi the camera code uses.
  inline constexpr float kDAT_00352400_twoPi = 6.28318405f;
  // DAT_00352404, thirty degrees. Where the fan starts.
  inline constexpr float kDAT_00352404_fanStart = 0.523598671f;
  // fGpffff849c (0x0035240C), pi/2. Added to the camera yaw before the burst's
  // plane is turned by its negation.
  inline constexpr float kfGpffff849c_halfPi = 1.57079601f;
  // FUN_002206a8:0x00220768. A flat 0x780 ticks, sixty frames.
  inline constexpr std::uint16_t kSparkLifetime = 0x780;
  // FUN_002206a8:0x002207c8, the literal 0x42C80000.
  inline constexpr float kSparkSpeed = 100.0f;
  // FUN_00220c00:0x00220c50. The step is `speed * frameTicks / 32000`.
  inline constexpr float kSparkSpeedDivisor = 32000.0f;

  // DAT_0034B988..DAT_0034B9A0, four (x, z) pairs. Only x is scaled.
  inline constexpr std::array<std::array<float, 2>, 4> kDAT_0034b988_corners{{
      {-1.0f, 0.00999999978f},
      {-1.0f, -0.00999999978f},
      {1.0f, -0.00999999978f},
      {1.0f, 0.00999999978f},
  }};

  // The two texture rectangles FUN_00220c00 chooses between, at 0x003159B8 and
  // 0x003159D8. Stored in the ELF as ST over 256 and scaled by 4096 into the
  // GS's 1/16-texel units on the way out, so these are plain texels -- the same
  // form the sprite pass's quads carry.
  //
  // Index 0 is the one a **party-side** victim gets. It is 55 texels long and
  // 16 tall; index 1, for everything else, is 63 by 15.
  inline constexpr std::array<std::array<std::array<float, 2>, 4>, 2> kHitSparkTexels{{
      {{{0.400000006f, 63.4000015f},
        {0.400000006f, 79.4000015f},
        {55.4000015f, 79.4000015f},
        {55.4000015f, 63.4000015f}}},
      {{{0.400000006f, 80.4000015f},
        {0.400000006f, 95.4000015f},
        {63.4000015f, 95.4000015f},
        {63.4000015f, 80.4000015f}}},
  }};

  // The low byte of FUN_002190f8's last argument -- 0x22 in all three branches --
  // is the texture slot. FUN_00207de8 copies it into the packet's texture field
  // and tests it against 0x18 the same way FUN_0020f510 tests a slot index.
  //
  // The **sheet** is what settles it, not the encoding. Slot 0x22 holds texture
  // 0x19A, and at exactly the two rectangles below it carries two lens-shaped
  // streaks, blue at v 63..79 and gold at v 80..95, each the exact size of its
  // rectangle. Slot 0x21's texture 0x19B has a smoke puff across both and
  // nothing streak-shaped anywhere on it. The flies_hit save state's own
  // screenshot shows gold streaks for a hit on an enemy, which is the second
  // rectangle.
  //
  // Worth flagging: FUN_0020f510, which fills the same packet field for the
  // sprite pass, writes `slot + 1`. The two producers disagree by one and only
  // the sheets say which the consumer honours. This port never reads that field
  // for sprites -- they carry the slot the cache handed them -- so the
  // disagreement only decides the two effect paths that go through
  // FUN_00207de8, and both of those read correctly as the slot itself.
  //
  // 0x19A is not one of FUN_00221fd8's seven fixed binds; it arrives through
  // the same pass's static model records, and the port loads it into slot 0x22
  // as well.
  inline constexpr int kHitSparkTextureSlot = 0x22;
  // FUN_002190f8's param_3 is 0x10008580, whose bit 0x8000 picks blend mode 2 --
  // additive -- in FUN_00207de8's `& 0x1C000` ladder.
  inline constexpr int kHitSparkBlendMode = 2;
  // FUN_00220c00's tail: FUN_00207de8(0x1000), the same bucket the DAT_00355620
  // particles land in.
  inline constexpr int kHitSparkDisplayListBucket = 0x1000;
  // param_4, 0xF0F0F0F0, on all four vertices. 0x80 is 1.0, so a spark is
  // 1.875x white before the additive blend.
  inline constexpr std::uint32_t kHitSparkColour = 0xF0F0F0F0u;

  // FUN_00218ee0, the guard FUN_002190f8 checks before it submits. Lane W of
  // FUN_0020b6a0's output is Q = 1 / max(viewZ, eps), and the quad is dropped
  // unless **all four** corners have Q <= fGpffff8350 (0.7). Expressed as the
  // depth that is equivalent to; a corner behind the eye is clamped to a huge Q
  // and fails it the same way.
  inline constexpr float kfGpffff8350_maxQ = 0.699999988f;
  inline constexpr float kHitSparkMinViewDepth = 1.0f / kfGpffff8350_maxQ;

  // FUN_00220c00's geometry half: the four corners of one spark's streak, in
  // world space, and which texture rectangle they take.
  struct HitSparkQuad
  {
    std::array<orphen::ported::psm2::Vec3, 4> corners{};
    int texelRectangle = 1;
  };

  HitSparkQuad FUN_00220c00_build_quad(const HitSpark &spark, float fGpffffb6d4_cameraYaw);

  class HitSparkPool
  {
  public:
    static constexpr std::size_t kGroupCount = 10;
    static constexpr std::size_t kGroupCapacity = 100;
    static constexpr std::size_t kCount = kGroupCount * kGroupCapacity; // 1000

    // FUN_002205d0. Every entry dead, every group empty, every group pointed at
    // its own slice.
    void FUN_002205d0_reset();

    // FUN_002206a8. `sourceSide` is FUN_00216140's uVar12 -- 1 for a party-side
    // victim, 0 otherwise. Reads the victim's +0xBE for the burst size and its
    // +0xBC for the reaction, so it must be called **after** the damage has been
    // added. Returns how many sparks it seeded, which is zero when the victim
    // has no pending damage or when all ten groups are busy.
    std::size_t FUN_002206a8_spawn(const OriginalEntity &victim,
                                   std::int16_t sourceSide,
                                   const std::function<std::uint32_t()> &random);

    // FUN_00220910 plus FUN_00220c00's first half. Walks all ten groups, ages
    // every live spark and slides the survivors outward.
    void FUN_00220910_step(std::uint32_t frameTicks);

    const std::array<HitSpark, kCount> &sparks() const { return sparks_; }
    // cGpffffbc0c. FUN_00220910 returns immediately when this is not positive,
    // so it is both a fast path and the pool's "anything showing" flag.
    std::int8_t DAT_00355b7c_activeGroups() const { return activeGroups_; }
    std::size_t aliveCount() const;

  private:
    // FUN_00220ba8. The only thing that retires a spark: decrement the owning
    // group's count, and when that reaches zero drop the active-group count too.
    // Both clamp at zero rather than wrapping.
    void FUN_00220ba8_retire(HitSpark &spark);

    std::array<HitSpark, kCount> sparks_{};
    std::array<HitSparkGroup, kGroupCount> groups_{};
    std::int8_t activeGroups_ = 0;
  };

} // namespace orphen::ported::entity
