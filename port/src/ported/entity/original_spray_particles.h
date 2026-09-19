#pragma once

// The particle spray opcode 0x10D throws -- a sixth pool, and not one of the
// five in the five_particle_systems note.
//
// Source map:
//   src/FUN_0021e540.c  0x0021E540  carve it: 96000 bytes, 2000 x 0x30
//   src/FUN_0021e088.c  0x0021E088  spawn a burst -- opcode 0x10D's target
//   src/FUN_0021e5e0.c  0x0021E5E0  walk the pool once a frame
//   src/FUN_0021e808.c  0x0021E808  one entry: step it, then build its quad
//   src/FUN_002629c0.c  0x002629C0  opcode 0x10D itself
//
// The globals: DAT_00355B58 is the base pointer, DAT_00355B54 the live count
// (also spelled iGpffffbbe4) and DAT_00354CBC the gate the walk checks before
// it does anything (also spelled uGpffffad4c). A spawn raises the gate; the
// last particle to die lowers it.
//
// ---- what a particle is --------------------------------------------------
//
// An **anchor and a running offset**, not a position and a velocity. The anchor
// at +0x0C is where the burst was thrown and never moves; +0x00 is the offset
// from it, and every step adds to that:
//
//   offset.z += entry +0x1C           the rise, baked at spawn
//   heading  += ticks * 1deg / 32     one degree a tick at 32 ticks a frame
//   offset.x += ticks * 0.005 / 32 * cos(heading)
//   offset.y += ticks * 0.005 / 32 * sin(heading)
//
// so a particle spirals outward and upward from a fixed point. The x/y step is
// skipped for mode 1, which is what makes that mode a straight column.
//
// The three modes differ in how they start and how they end:
//
//   0  random life 1..N times 32 ticks, random direction    dies at the end
//   1  fixed life N*32 ticks, no spiral                     dies at the end
//   2  random life like 0                                   **restarts**, with
//      a fresh direction and its offset back at the anchor -- a looping emitter
//
// Anything else is a mode too: the tail of FUN_0021E088 stores it and marks the
// entry live without setting a life or a direction, so it inherits whatever the
// previous occupant of the slot left behind. Reproduced, because a script can
// pass one.
//
// ---- the quad ------------------------------------------------------------
//
// A flat 0.03-unit square in the XZ plane (DAT_00315838's four corners, all
// +/-0.015), turned to face the camera by Rz(-yaw - pi/2) -- the same billboard
// the hit sparks build -- and placed at anchor + offset. The size never
// changes, so a particle is a fixed-size world-space dot that shrinks only with
// distance.
//
// The alpha is `255 - age/life * 255`, read from the age at the **start** of
// the frame rather than the stepped one, and the colour is the entry's own or
// 0xF0F0F0 when it is zero. A zero colour also picks the other CLUT bank --
// FUN_0021E808 passes 0x0121 instead of 0x0021 -- which is the only visible
// difference between a coloured particle and a plain one.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // One entry of the pool at DAT_00355B58. Offsets are the entry's own.
  struct SprayParticle
  {
    float offsetX00 = 0.0f; // +0x00, the running offset from the anchor
    float offsetY04 = 0.0f; // +0x04
    float offsetZ08 = 0.0f; // +0x08
    float anchorX0c = 0.0f; // +0x0C, where the burst was thrown
    float anchorY10 = 0.0f; // +0x10
    float anchorZ14 = 0.0f; // +0x14
    float heading18 = 0.0f; // +0x18, radians, turned one degree a tick
    // +0x1C. The per-tick rise, and the one field the frame delta is folded
    // into **at spawn time** rather than each step: FUN_0021E088 stores
    // `rise * DAT_003555BC / 32`. A burst thrown on a long frame therefore
    // rises faster for its whole life. That is what the original does.
    float rise1c = 0.0f;
    // +0x20. The launch speed is `rand() % this / 100`, so it is a range in
    // hundredths and never zero -- FUN_0021E088 floors the operand at 1.
    std::int16_t speedRange20 = 1;
    std::int16_t age22 = 0;     // +0x22, ticks elapsed
    std::int16_t life24 = 0;    // +0x24, ticks it gets
    std::uint32_t colour28 = 0; // +0x28, RGB; zero means 0xF0F0F0 and bank 1
    // +0x2C. Negative is free -- the spawn walk tests the sign and nothing
    // else, which is why 0xFF is the value it writes.
    std::int8_t mode2c = -1;

    bool alive() const { return mode2c >= 0; }
  };

  // One particle that survived its step, ready for the publish pass to turn
  // into a quad. The centre is anchor + offset, already summed.
  struct SprayParticleDraw
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t colour = 0; // the entry's own; zero still means 0xF0F0F0
    int alpha = 0;            // 0..255, from the age at the start of the frame
  };

  class SprayParticlePool
  {
  public:
    static constexpr std::size_t kCount = 2000;

    // FUN_0021E540. The original zeroes 96000 bytes and then marks every entry
    // free, which a default-constructed SprayParticle already is.
    void FUN_0021e540_reset();

    // FUN_0021E088(rise, x, y, z, count, speedRange, lifeUnit, mode, colour).
    // `lifeUnit` under 1 becomes 10 and `speedRange` under 1 becomes 1, both
    // before anything is rolled.
    void FUN_0021e088_spawn(float rise, float x, float y, float z,
                            int count,
                            int speedRange,
                            std::int16_t lifeUnit,
                            std::int8_t mode,
                            std::uint32_t colour,
                            std::uint32_t frameTicks,
                            const std::function<std::uint32_t()> &random);

    // FUN_0021E5E0 and FUN_0021E808's step half. The draw half is collected
    // here and built at publish time, the way the other pools are split -- see
    // FUN_0021e808_build_corners.
    void FUN_0021e5e0_step(std::uint32_t frameTicks,
                           const std::function<std::uint32_t()> &random);

    const std::vector<SprayParticleDraw> &drawList() const { return draws_; }
    // Emptied on a frame FUN_002192C0 never runs, so the pool emits nothing
    // while its records stand still. See PortRuntime's gate on DAT_00354D2C.
    void clearFrameDraws() { draws_.clear(); }
    const std::array<SprayParticle, kCount> &particles() const { return particles_; }
    int DAT_00355b54_aliveCount() const { return live_; }
    bool DAT_00354cbc_gate() const { return gate_; }

  private:
    std::array<SprayParticle, kCount> particles_{};
    std::vector<SprayParticleDraw> draws_;
    int live_ = 0;
    bool gate_ = false;
  };

  // The four world-space corners of one particle. `cameraYaw` is DAT_00355644,
  // the same yaw FUN_00220C00 undoes for a hit spark.
  std::array<orphen::ported::psm2::Vec3, 4>
  FUN_0021e808_build_corners(const SprayParticleDraw &particle, float cameraYaw);

  // DAT_00315858's four ST pairs, as texels on the 256-wide sheet slot 0x21 --
  // the same sheet the dust puffs come off, a different sprite on it.
  inline constexpr float kSprayTexels[4][2] = {
      {0.814062476158142f * 256.0f, 0.001562500023283f * 256.0f},
      {0.814062476158142f * 256.0f, 0.028906250372529f * 256.0f},
      {0.841406226158142f * 256.0f, 0.028906250372529f * 256.0f},
      {0.841406226158142f * 256.0f, 0.001562500023283f * 256.0f}};

  inline constexpr int kSprayTextureSlot = 0x21;
  // FUN_0021E808 passes 0x0121 when the colour is zero; the high byte is the
  // CLUT bank, the same encoding FUN_0021A820's 0x0221 uses.
  inline constexpr int kSprayDefaultClutBank = 1;
  inline constexpr std::uint32_t kSprayDefaultColour = 0xF0F0F0u;
  // FUN_002190F8's param_3 is 0x10004580 here. FUN_00207DE8's `& 0x1C000`
  // ladder reads bit 0x4000 first, so this is mode 1 -- the alpha blend, not
  // the additive one the hit sparks get.
  inline constexpr int kSprayBlendMode = 1;
  inline constexpr int kSprayDisplayListBucket = 0x1000;

} // namespace orphen::ported::entity
