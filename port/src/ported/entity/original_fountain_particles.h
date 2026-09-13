#pragma once

// The particle fountain opcode 0x10F throws -- a seventh pool, sitting in RAM
// immediately after the spray pool and sharing nothing with it.
//
// Source map:
//   src/FUN_0021f108.c  0x0021F108  carve it: 0x21340 bytes, 2000 x 0x44
//   src/FUN_0021ed50.c  0x0021ED50  spawn a burst -- opcode 0x10F's target
//   src/FUN_0021f1a8.c  0x0021F1A8  walk the pool once a frame
//   src/FUN_0021f310.c  0x0021F310  one entry: step it, then build its quad
//   src/FUN_0021ebe8.c  0x0021EBE8  put a camera-relative point into the world
//   src/FUN_00262b90.c  0x00262B90  opcode 0x10F
//
// The globals sit one word apart from the spray pool's: DAT_00355B5C is the
// live count (iGpffffbbec), DAT_00355B60 the base pointer (pfGpffffbbf0) and
// DAT_00354CC0 the gate (uGpffffad50), against DAT_00355B54 / B58 / 00354CBC.
// That adjacency is the only thing the two have in common.
//
// ---- what a particle does ------------------------------------------------
//
// It is a **fountain**, and its +0x40 is a phase rather than a bare live flag:
//
//   phase 0   rises at `+0x28` a tick, at full alpha
//   phase 1   falls at `+0x2C` a tick, fading out across its own life
//   0xFF      free
//
// Each phase lasts the entry's whole life and the age resets between them, so a
// particle gets two full lives. In both, and in any phase value above 1, it
// also drifts horizontally at `+0x30` a tick along the fixed heading it was
// given at spawn. A rise rate of zero skips phase 0 outright -- the entry is
// pushed to phase 1 on its first step rather than hanging at the top.
//
// At the end of phase 1, `+0x41` decides: zero frees the slot, non-zero
// **restarts** it from the base position at +0x18 with the launch offset it was
// born with, which is what makes a continuous emitter.
//
// `+0x42` says the base position is **camera-relative**: FUN_0021EBE8 runs it
// through yaw, pitch and the camera's own position before it is used. Note that
// FUN_0021ED50 does this to a copy on its own stack and then spawns from the
// untransformed registers anyway, so the flag only takes effect from the first
// restart onward. That is a quirk of the original, reproduced here.
//
// ---- the quad ------------------------------------------------------------
//
// A screen-aligned rectangle built in GS units around the projected origin, the
// way FUN_0021A820's dust puffs are and not the way the spray pool's world
// billboard is: 32 * `+0x38` wide and 16 * `+0x38` tall, both scaled by
// `400 * q`. Same sprite as the spray pool -- DAT_00315878 is a second copy of
// DAT_00315858's four ST pairs -- but a different blend: FUN_0021F310 writes
// 0x10008580 into the packet, so bit 0x8000 picks mode 2, the additive one.
//
// A zero colour means 0xF0F0F0 and CLUT bank **3** (packet halfword 0x0321),
// where the spray pool's zero means bank 1 and the dust's means bank 2.
//
// A particle is skipped while its projected q is above 0.7 -- the same near
// cutoff, read from the same DAT_003523CC, that the dust pool has.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // One entry of the pool at DAT_00355B60. Offsets are the entry's own.
  struct FountainParticle
  {
    float x00 = 0.0f; // +0x00, where it is now
    float y04 = 0.0f; // +0x04
    float z08 = 0.0f; // +0x08
    // +0x0C. The launch offset, rolled once at spawn and kept: a restart adds
    // it to the base again rather than rolling a new one.
    float launchX0c = 0.0f;
    float launchY10 = 0.0f; // +0x10
    // +0x14. A second copy of the base z that nothing ever reads back. The
    // original writes both; kept so the struct says what the entry holds.
    float baseZ14 = 0.0f;
    float baseX18 = 0.0f;   // +0x18, what a restart returns to
    float baseY1c = 0.0f;   // +0x1C
    float baseZ20 = 0.0f;   // +0x20
    float heading24 = 0.0f; // +0x24, radians, fixed for life
    float riseRate28 = 0.0f;  // +0x28, per tick in phase 0
    float fallRate2c = 0.0f;  // +0x2C, per tick in phase 1
    float driftRate30 = 0.0f; // +0x30, per tick in every phase
    std::uint16_t age34 = 0;  // +0x34
    std::uint16_t life36 = 0; // +0x36, `(rand % N + 1) * 16` ticks
    float size38 = 0.0f;      // +0x38, half the quad in units of sixteen
    std::uint32_t colour3c = 0; // +0x3C, RGB; zero means 0xF0F0F0 and bank 3
    // +0x40. 0 and 1 are the two phases, negative is free.
    std::int8_t phase40 = -1;
    // +0x41. Non-zero restarts the entry at the end of phase 1 instead of
    // freeing it.
    std::uint8_t loop41 = 0;
    // +0x42. Non-zero means the base position is camera-relative.
    std::int8_t cameraRelative42 = 0;

    bool alive() const { return phase40 >= 0; }
  };

  // What FUN_0021EBE8 needs: the camera's yaw, pitch and position.
  struct FountainCameraFrame
  {
    float fGpffffb6d4_yaw = 0.0f;
    float fGpffffb6d8_pitch = 0.0f;
    float DAT_0058c0a8_eyeX = 0.0f;
    float DAT_0058c0ac_eyeY = 0.0f;
    float DAT_0058c0b0_eyeZ = 0.0f;
  };

  // One particle that survived its step.
  struct FountainParticleDraw
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float size = 0.0f;
    std::uint32_t colour = 0;
    int alpha = 0; // 0..255
  };

  class FountainParticlePool
  {
  public:
    static constexpr std::size_t kCount = 2000;

    // FUN_0021F108. 0x21340 bytes zeroed, every entry marked free.
    void FUN_0021f108_reset();

    // FUN_0021ED50. The parameters keep the original's own order, which is not
    // the stream order -- see the 0x10F case in scene_script_opcodes.cpp.
    //
    //   rise/fall/drift  per-tick rates for the two phases and the drift
    //   speedRange       launch speed range, in hundredths; zero becomes 1.0
    //   zJitterRange     spawn z spread, in hundredths; zero becomes 1.0
    //   size             half the quad, in units of sixteen GS units
    //   baseX/Y/Z        where it starts, and where a restart returns to
    //   count            how many to allocate
    //   lifeUnit         life is `(rand % this + 1) * 16` ticks; zero becomes 1
    //   loop             +0x41, restart rather than free
    //   cameraRelative   +0x42
    //   colour           +0x3C
    void FUN_0021ed50_spawn(float rise, float fall, float drift,
                            float speedRange,
                            float zJitterRange,
                            float size,
                            float baseX, float baseY, float baseZ,
                            int count,
                            std::int16_t lifeUnit,
                            std::uint8_t loop,
                            std::int8_t cameraRelative,
                            std::uint32_t colour,
                            const std::function<std::uint32_t()> &random);

    // FUN_0021F1A8 and FUN_0021F310's step half.
    void FUN_0021f1a8_step(std::uint32_t frameTicks, const FountainCameraFrame &camera);

    // FUN_00262D88, opcode 0x112. The gate is an ordinary global that the
    // script can write directly; clearing it stops the walk without freeing a
    // single particle, and setting it again resumes them mid-flight.
    void FUN_00262d88_set_gate(bool open) { gate_ = open; }

    const std::vector<FountainParticleDraw> &drawList() const { return draws_; }
    const std::array<FountainParticle, kCount> &particles() const { return particles_; }
    int DAT_00355b5c_aliveCount() const { return live_; }
    bool DAT_00354cc0_gate() const { return gate_; }

  private:
    std::array<FountainParticle, kCount> particles_{};
    std::vector<FountainParticleDraw> draws_;
    int live_ = 0;
    bool gate_ = false;
  };

  // FUN_0021EBE8: a point in camera space, put where the camera is looking.
  orphen::ported::psm2::Vec3 FUN_0021ebe8_to_world(const orphen::ported::psm2::Vec3 &point,
                                                   const FountainCameraFrame &camera);

  // What FUN_0021F310 needs from the projection to place one particle. The
  // rectangle is symmetric, unlike the dust's two shapes.
  struct FountainQuadInputs
  {
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    float size = 0.0f;
    std::uint32_t colour = 0;
    int alpha = 255;
  };

  orphen::ported::render::SpriteQuad
  FUN_0021f310_build_fountain_quad(const FountainQuadInputs &inputs);

  // DAT_003523CC. A particle whose projected q is above this is skipped -- the
  // same word, and the same near cutoff, the dust pool reads.
  inline constexpr float kDAT_003523cc_fountainNearCutoff = 0.699999988079071f;

} // namespace orphen::ported::entity
