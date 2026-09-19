#pragma once

// The rain, which opcode 0x102 arms -- the **first** of the six pool gates at
// uGpffffad38..ad4c, and the biggest pool in the executable:
//
//   src/FUN_0021ad00.c  0x0021AD00  carve it: 60000 bytes, 3000 x 0x14
//   src/FUN_0021ac00.c  0x0021AC00  set the parameters and the live count
//   src/FUN_0021ad98.c  0x0021AD98  the per-frame walk: matrices, then all 3000
//   src/FUN_0021afa0.c  0x0021AFA0  one record's three-state machine
//   src/FUN_0021b398.c  0x0021B398  the splash, state 2's whole draw
//   src/FUN_00262250.c  0x00262250  opcode 0x102 itself, shared with 0x106/0x108
//
// s01_e013's start entry arms it at 0x3072 with
//
//     0x102(1000, 150000, 50000, 6, 10, 0deg, 0deg, -1)
//
// which is where every number below comes from. Read back off the hardware at
// s01_e013 frame 11524: live count 500, streak length 1.5, fall 0.5, radius 6,
// height 10, no entity.
//
// ---- a record ------------------------------------------------------------
//
// Twenty bytes: a position, a splash timer, a state and a probe countdown.
// +0x0C is never touched. The position is in the **pool's own space** while
// the drop is falling and in **world space** once it has splashed, because the
// state 1 -> 2 transition copies a transformed corner back into it.
//
// ---- where the rain is -----------------------------------------------------
//
// A cylinder of radius `magnitude` and height `height`, anchored by
// FUN_00218F40. With no entity that anchor is
//
//     (eyeX + magnitude*cos(yaw), eyeY + magnitude*sin(yaw), eyeZ)
//
// -- `magnitude` units in front of the camera at eye height, so the volume
// travels with you and there is never rain behind your head. FUN_00218FE0 then
// turns it by Rx(param3) and Rz(param4) before the translation; s01_e013 passes
// zero for both, so for that scene the matrix is a pure translation.
//
// ---- the count only ever gets halfway there ------------------------------
//
// FUN_0021AC00's growth loop is the same arithmetic as the haze field's:
// `added < target - liveCount` with **both** sides moving each pass. Asking for
// 1000 from empty makes 500 live, and the hardware reads exactly 500. Not a
// decompiler artifact -- see [[original_haze_particles.h]] for the other one.
//
// ---- the three states ----------------------------------------------------
//
// **0, spawn.** A fresh angle (0..359 degrees, converted through 2*pi/360), a
// fractional `frac = (rand%100)/100`, a radius of `frac + rand%magnitude` and a
// height of `rand%height + frac`. Note `frac` is rolled once and used twice.
//
// **1, falling.** `z -= fall * frameTicks / 32` every frame. Below `-height`
// the record goes back to state 0. Otherwise it builds its streak: four corner
// offsets, shared by the whole pool and rebuilt once a frame, added to the
// record's position and put through the pool matrix.
//
// The probe countdown at +0x13 ticks down every falling frame, and at zero the
// record asks FUN_00227798 for the ground under the **bottom** of its streak.
// The reschedule is two independent `if`s, verified in the disassembly at
// 0x0021B26C: `height > 5` writes 100 and `height > 1` then writes 50 over it,
// so 100 is unreachable and a drop over ground above z=1 re-probes every 50
// frames. Reproduced as written.
//
// If the streak's bottom has reached that ground and the ground is below 64
// (the "nothing there" sentinel is larger), the record jumps to the ground
// point plus fGpffff83B0 and becomes a splash -- and draws no streak that
// frame.
//
// **2, splash.** A flat ring on the ground that grows and fades over 960 ticks,
// then back to state 0. The corners are the record's world position plus
// (+/-1, +/-1) * radius with z held, so it lies in the ground plane and is not
// billboarded.
//
// ---- the streak is a world-space quad, not a screen sprite ---------------
//
// Unlike the dust, spray, fountain and haze pools, which build their corners in
// GS units around a projected origin, this one is shaped like the hit sparks:
// four world points, each projected on its own, each carrying its own depth.
// The four local offsets are
//
//     (-0.006, 0, length)  (-0.006, 0, 0)  (+0.006, 0, 0)  (+0.006, 0, length)
//
// turned about Z by `-cameraYaw - pi/2` so the ribbon faces the camera. At
// s01_e013's length of 1.5 that is a 0.012 x 1.5 world-space sliver -- one or
// two pixels wide and long enough to read as a streak.
//
// ---- the sheet -----------------------------------------------------------
//
// Texture slot 0x20 for both quads, which is FUN_00221FD8's fixed bind of
// texture 0x19C, so it is loaded in every scene whether or not the rain is on.
// The two UV boxes live in .data as normalised pairs and are held here in
// texels the way the hit sparks' are:
//
//   streak  0x315558  (251.4, 8.4)..(252.4, 39.4)   a 1 x 31 column
//   splash  0x315578  (192.4, 0.4)..(223.4, 31.4)   a 31 x 31 patch
//
// Packet +0x0C is 0x10004580 for both: bit 0x4000 is the first rung of
// FUN_00207DE8's `& 0x1C000` ladder, so **blend mode 1**, not the additive mode
// the hit sparks take. Colour is 0xF0808080 on a streak and 0xF0F0F0 with a
// falling top byte on a splash -- **before the fold**. FUN_00207DE8:130-141
// halves all four channels of a textured packet, so a streak reaches the GS as
// (64, 64, 64, 120): half-grey at alpha 0.9375, not the white-at-1.875 the raw
// word reads as. Every one of the 411 streak draws in a GS dump of s01_e013 at
// frame 11524 carries exactly that colour, with ALPHA 0x44 -- `(Cs-Cd)*As+Cd`,
// the ordinary source-alpha blend -- TEST 0x5000d and TEX1 0x60.
//
// Skipping the fold is what makes the rain look like white bars instead of
// drizzle, and it is the same mistake the dust pool's note warns about.
//
// ---- the one thing here that is not reproduced ---------------------------
//
// FUN_002190F8 drops a quad when FUN_0020B6A0's clip flags have `& 0xE0`, which
// no pool in this port models. FUN_00218EE0's near test **is** modelled, the
// same way the hit sparks model it.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace orphen::ported::entity
{

  // fGpffff83A4 / fGpffff83A8, the streak's two half-widths. Read off the
  // hardware at 0x352314 and 0x352318.
  inline constexpr float kfGpffff83a4_streakLeft = -0.006f;
  inline constexpr float kfGpffff83a8_streakRight = 0.006f;
  // fGpffff83A0. Added to the negated camera yaw before the billboard turn.
  inline constexpr float kfGpffff83a0_halfPi = 1.57079637f;
  // fGpffff83AC, the degrees-to-radians numerator: `deg * this / 360`.
  inline constexpr float kfGpffff83ac_twoPi = 6.28318548f;
  // fGpffff83B0. Lifts a splash off the ground it landed on.
  inline constexpr float kfGpffff83b0_splashLift = 0.00999999978f;
  // DAT_00352324 / DAT_00352328: the splash ring's radius is
  // `(t / 960) * grow + base`.
  inline constexpr float kDAT_00352324_splashGrow = 0.0500000007f;
  inline constexpr float kDAT_00352328_splashBase = 0.00999999978f;
  // 0x0021B178's compare, and 0x0021B348's: the fall is scaled by 1/32 and the
  // splash runs for 960 ticks.
  inline constexpr float kFallScale = 0.03125f;
  inline constexpr float kSplashTicks = 960.0f;
  // 0x0021B2BC. A ground query that answers at or above this found nothing
  // worth splashing on.
  inline constexpr float kGroundSentinel = 64.0f;

  // FUN_002190F8's param_5 low byte, for both quads.
  inline constexpr int kRainTextureSlot = 0x20;
  // Bit 0x4000 of 0x10004580, the first rung of FUN_00207DE8's ladder.
  inline constexpr int kRainBlendMode = 1;
  // FUN_002190F8's tail call is FUN_00207DE8(0x1000), like every other pool
  // that goes through it.
  inline constexpr int kRainDisplayListBucket = 0x1000;
  // FUN_0021AFA0's param_4, before FUN_00207DE8's fold halves it to
  // (64, 64, 64, 120). 0x80 is 1.0 on the GS.
  inline constexpr std::uint32_t kRainStreakColour = 0xF0808080u;
  // FUN_0021B398's, before the top byte is replaced by the fade.
  inline constexpr std::uint32_t kRainSplashColour = 0x00F0F0F0u;

  // DAT_00315558 and DAT_00315578, x256 the way kHitSparkTexels are. Corner
  // order is the packet's, matched to the corner order below.
  inline constexpr std::array<std::array<float, 2>, 4> kRainStreakTexels{{
      {251.400009f, 8.40000057f},
      {251.400009f, 39.4000015f},
      {252.400009f, 39.4000015f},
      {252.400009f, 8.40000057f},
  }};
  inline constexpr std::array<std::array<float, 2>, 4> kRainSplashTexels{{
      {192.399994f, 0.400000006f},
      {192.399994f, 31.3999996f},
      {223.399994f, 31.3999996f},
      {223.399994f, 0.400000006f},
  }};

  // DAT_00315598, the splash ring's four corner offsets. FUN_0021B398 reads
  // them as (x, y) pairs and scales both by the ring's radius.
  inline constexpr std::array<std::array<float, 2>, 4> kDAT_00315598_splashCorners{{
      {-1.0f, -1.0f},
      {1.0f, -1.0f},
      {1.0f, 1.0f},
      {-1.0f, 1.0f},
  }};

  // One entry of the pool at uGpffffbb50. Offsets are the entry's own.
  struct RainParticle
  {
    // +0x00. Pool space while falling, world space once splashing.
    float x00 = 0.0f;
    float y04 = 0.0f;
    float z08 = 0.0f;
    // +0x10. The splash timer, held as a signed short because FUN_0021AD98
    // rounds the scratchpad float back into it with FUN_0030BD20 every frame.
    std::int16_t splashTicks10 = 0;
    // +0x12. Negative is free; 0 spawn, 1 falling, 2 splashing.
    std::int8_t state12 = -1;
    // +0x13. Frames until the next ground probe. Signed: the test is
    // `(int8)(value - 1) < 1`.
    std::int8_t groundProbe13 = 0;

    bool alive() const { return state12 >= 0; }
  };

  // What FUN_00218F40's no-entity branch needs. The same five words the
  // fountain and haze pools carry.
  struct RainCameraFrame
  {
    float fGpffffb6d4_yaw = 0.0f;
    float DAT_0058c0a8_eyeX = 0.0f;
    float DAT_0058c0ac_eyeY = 0.0f;
    float DAT_0058c0b0_eyeZ = 0.0f;
  };

  // FUN_00218F40's entity branch: the selected entity's +0x20/+0x24/+0x28.
  struct RainEntityAnchor
  {
    float positionX20 = 0.0f;
    float positionY24 = 0.0f;
    float positionZ28 = 0.0f;
  };

  // One quad the walk produced, in world space. The pass projects it.
  struct RainQuad
  {
    std::array<orphen::ported::psm2::Vec3, 4> corners{};
    // 0..255, and only a splash ever moves it off 0xF0.
    int alpha = 0xF0;
    bool splash = false;
  };

  class RainParticlePool
  {
  public:
    // 60000 bytes of 0x14.
    static constexpr std::size_t kCount = 3000;

    // FUN_0021AD00. Marks every record free, zeroes the timers, drops the gate.
    void FUN_0021ad00_reset();

    // FUN_0021AC00, opcode 0x102's target. `entityIndex` is the opcode's eighth
    // expression as FUN_00262250 leaves it: negative or at 0x100 and above
    // means no entity.
    void FUN_0021ac00_arm(float length, float fall, float rotateX, float rotateZ,
                          int count, int magnitude, int height, int entityIndex);

    // FUN_002620A8's first jump-table arm, which opcode 0x100's inline byte
    // selects with a **1** -- the handler subtracts one before indexing. A bare
    // store of zero into uGpffffad38. The records keep everything and come back the moment
    // the scene arms the pool again.
    void FUN_002620a8_clear_gate() { uGpffffad38_gate_ = false; }

    // FUN_0021AD98 plus FUN_0021AFA0 and FUN_0021B398. `anchor` is the resolved
    // entity when uGpffffbb4c_entityIndex() names one and nullopt otherwise;
    // nullopt takes the camera-locked branch.
    void FUN_0021ad98_step(std::uint32_t frameTicks,
                           const RainCameraFrame &camera,
                           const std::optional<RainEntityAnchor> &anchor,
                           const std::function<std::uint32_t()> &random,
                           const std::function<std::optional<float>(float, float, float)> &FUN_00227798_probe);

    const std::vector<RainQuad> &drawList() const { return draws_; }
    // Emptied on a frame FUN_002192C0 never runs, so the pool emits nothing
    // while its records stand still. See PortRuntime's gate on DAT_00354D2C.
    void clearFrameDraws() { draws_.clear(); }
    int iGpffffbb30_aliveCount() const { return live_; }
    bool uGpffffad38_gate() const { return uGpffffad38_gate_; }
    int uGpffffbb4c_entityIndex() const { return entityIndex_; }

  private:
    std::array<RainParticle, kCount> particles_{};
    std::vector<RainQuad> draws_;
    int live_ = 0;                  // iGpffffbb30
    bool uGpffffad38_gate_ = false; // uGpffffad38
    float length_ = 0.0f;           // uGpffffbb38, the streak's length
    float fall_ = 0.0f;             // fGpffffbb34
    float rotateX_ = 0.0f;          // uGpffffbb44
    float rotateZ_ = 0.0f;          // uGpffffbb48
    int height_ = 1;                // iGpffffbb3c
    int magnitude_ = 1;             // iGpffffbb40, the spawn radius
    int entityIndex_ = -1;          // uGpffffbb4c, as a pool index
  };

} // namespace orphen::ported::entity
