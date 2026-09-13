#pragma once

// The **smoke cloud** -- a ninth particle pool, and the first one in
// FUN_002192C0's list. s01_e014's "which way do we go" argument arms it when
// Magnus shouts "Hey! Smoke!".
//
// Source map:
//   src/FUN_00262cf0.c  0x00262CF0  opcodes 0x110 and 0x111
//   src/FUN_00212db0.c  0x00212DB0  0x110's target: arm it *and reseed*
//   src/FUN_00212d60.c  0x00212D60  0x111's target: change the parameters only
//   src/FUN_00212f38.c  0x00212F38  step and draw, first call in FUN_002192C0
//   src/FUN_0022f020.c  0x0022F020  clear it (count = 0)
//
// Globals, all one gp window apart:
//
//   DAT_00354C5C  uGpffffacec  particle count, 0 when off
//   DAT_0054F080               the records, 8 bytes each
//   DAT_0055F080               the lead's position as of last frame
//   DAT_00355A40  uGpffffbad0  alpha ceiling, the colour word's top byte
//   DAT_00355A44  uGpffffbad4  0xRRGGBB -- 0x44 is B, 0x45 G, 0x46 R
//   DAT_00355A48  fGpffffbad8  sprite scale
//   DAT_00355A4C  uGpffffbadc  zeroed by the arm and never read again
//
// ---- it is not a puff, it is a volume ------------------------------------
//
// Nothing here has a position of its own. A record holds three 16-bit **phase
// angles** and a packed nibble triple, and a particle's world position is
// reconstructed every frame from the phase *relative to where the camera is*:
//
//     box[c]  = eye[c] + forward[c] * 1.5           (c = 0, 1)
//     box[2]  = eye.z  + (0.6 - 0.2) + forward.z * 2
//     cell[c] = (int)(box[c] * 65536/3)
//     u       = (phase[c] - cell[c]) & 0xFFFF
//     pos[c]  = u * 3/65536 + box[c] - 1.5
//
// which is a 3x3x3 world-unit box hanging in front of the eye, wrapping in all
// three axes. Each phase drifts by its own nibble (0..15 biased to -8..+7, with
// 7, 8 and 9 remapped to 5, 13 and 11 so nothing sits still on an axis), so the
// particles crawl through a torus and the cloud never runs out.
//
// Alpha is the wrap seam: within 1/16 of a turn of the camera's own cell the
// particle fades linearly to nothing, and the three axes take the minimum. That
// is what stops a particle popping as it wraps.
//
// ---- the puff around the player ------------------------------------------
//
// The packed word's top nibble is a **counter**, not part of the drift. A
// particle inside a 0.8 x 0.8 x 1.1 box around the lead's waist gets it set to
// 15 whenever the lead is moving -- either its position actually changed, or
// its animation id is 3 or more, which is anything but a stand. While the
// counter is up and the particle is at full alpha its phase advances by an
// *extra* `dir * counter * 2` a frame and the counter comes down one every
// fourth frame, so walking through the cloud shoves it aside and it drifts back.
//
// A particle that is fading (alpha below the ceiling) has its counter dropped
// outright rather than decremented.
//
// ---- what is deliberately missing ----------------------------------------
//
// **The draw.** FUN_00212F38's packet is a VU1 program's input, not a GIF
// stream the port can read a texture out of: it unpacks 40 quadwords of
// template to VU address 32, then the per-particle stream is 32 positions
// (V4-32 to address 6) plus 32 four-byte attribute quads (V4-8 to address 47)
// whose byte 0 is `(i & 3) * 10` and byte 3 the alpha.
//
// The template's GIFtag reads NLOOP 1, EOP, PRE, NREG 9, FLG PACKED with
// REGS = RGBAQ then four (UV, XYZ2) pairs, and PRIM = triangle fan with TME,
// ABE and FST set and IIP clear -- a flat, textured, blended quad addressed in
// UV, whose four corners are 0x510 and 0x5F0 in 4-bit fixed, i.e. texels 81..95
// on both axes. **There is no TEX0 anywhere in the packet**, so which sheet
// those texels belong to is not in this function, and neither is the ALPHA
// register behind that ABE. Drawing it would mean choosing a page and a blend,
// which is the guess the fidelity rule exists to stop. A GS dump of the scene
// (port/attic/gsparse.py) settles both.
//
// So the pool is stepped and reported and draws nothing yet. That is stated in
// --actor-report rather than left to be discovered.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // FUN_00212DB0:2-8. Above this the count is clamped; below it, rounded *up*
  // to a multiple of 32, which is the per-packet vertex batch.
  inline constexpr int kSmokeMaxParticles = 0x2000;

  // fGpffff813c and fGpffff8138, both 1/12. The arm multiplies the incoming
  // scale by it before it is stored.
  inline constexpr float kFGpffff813c_smokeScale = 0.0833333358f;
  // fGpffff8d18 and fGpffff8d1c, both 100000 -- the script coordinate scale, so
  // the third operand is a world value like every other one.
  inline constexpr float kFGpffff8d18_smokeScaleDivisor = 100000.0f;

  // DAT_003520B0, and DAT_0058C0E0 which FUN_00228E28:206 sets once at boot.
  // The box's z sits `0.6 - 0.2` above the eye plus twice the forward z.
  inline constexpr float kDAT_003520b0_boxZBias = 0.200000003f;
  inline constexpr float kDAT_0058c0e0_cameraHeight = 0.600000024f;
  // DAT_003520B4, 65536/3: phase angle per world unit.
  inline constexpr float kDAT_003520b4_phasePerUnit = 21845.3339844f;
  // Its reciprocal, spelled as the original spells it.
  inline constexpr float kSmokeUnitPerPhase = 4.57763672e-05f;
  // DAT_003520B8 / DAT_003520BC / DAT_003520C0 and the -0.5 literal: the box
  // around the lead that re-arms a particle's counter.
  inline constexpr float kDAT_003520b8_reArmXY = 0.400000006f;
  inline constexpr float kDAT_003520bc_reArmXYLow = -0.400000006f;
  inline constexpr float kDAT_003520c0_reArmZHigh = 0.600000024f;
  inline constexpr float kSmokeReArmZLow = -0.5f;
  // FUN_00212F38:0x002131E8. Entity +0xA0 below 3 is a stand, and only then is
  // the lead's movement decided by comparing positions.
  inline constexpr std::int16_t kSmokeIdleAnimationCeiling = 3;

  // One 8-byte record of DAT_0054F080.
  struct SmokeParticle
  {
    // +0x00, +0x02, +0x04. Phase angles, one per axis, seeded from the RNG.
    std::array<std::uint16_t, 3> phase{};
    // +0x06. Low twelve bits are three drift nibbles; the top nibble is the
    // shove counter.
    std::uint16_t packed06 = 0;
  };

  // What one particle would draw as, if the sheet behind it were known.
  struct SmokeDrawPoint
  {
    orphen::ported::psm2::Vec3 position{};
    std::uint8_t sizeIndex = 0; // `(i & 3) * 10`
    std::uint8_t alpha = 0;
  };

  class SmokeCloud
  {
  public:
    // FUN_00212DB0, opcode 0x110: set the parameters, reseed every record and
    // latch the lead's position. `leadPosition` is DAT_0058BED0.
    void FUN_00212db0_arm(int count,
                          std::uint32_t colour,
                          float scale,
                          const orphen::ported::psm2::Vec3 &leadPosition,
                          const std::function<std::uint32_t()> &random);

    // FUN_00212D60, opcode 0x111: the same parameters with no reseed and no
    // position latch. A count raised this way walks records the previous arm
    // left behind, which is the original's behaviour and not an oversight here.
    void FUN_00212d60_set(int count, std::uint32_t colour, float scale);

    // FUN_0022F020:75.
    void FUN_0022f020_clear() { DAT_00354c5c_count_ = 0; }

    // FUN_00212F38's simulation half. `forward` is DAT_0058BEA0, the camera
    // forward FUN_00216AA0 normalises out of (lookAt - eye); `leadHeight` is
    // the lead's +0x58 and `leadAnimation` its +0xA0.
    void FUN_00212f38_step(const orphen::ported::psm2::Vec3 &eye,
                           const orphen::ported::psm2::Vec3 &forward,
                           const orphen::ported::psm2::Vec3 &leadPosition,
                           float leadHeight,
                           std::int16_t leadAnimation,
                           std::uint32_t frameCounter);

    int DAT_00354c5c_count() const { return DAT_00354c5c_count_; }
    std::uint32_t DAT_00355a40_alphaCeiling() const { return DAT_00355a40_alphaCeiling_; }
    std::uint32_t DAT_00355a44_rgb() const { return DAT_00355a44_rgb_; }
    float DAT_00355a48_scale() const { return DAT_00355a48_scale_; }
    // Rebuilt by every step, in pool order.
    const std::vector<SmokeDrawPoint> &drawList() const { return drawList_; }
    // How many of the step's points came out with any alpha at all.
    int visibleCount() const { return visibleCount_; }

  private:
    int DAT_00354c5c_count_ = 0;
    std::uint32_t DAT_00355a40_alphaCeiling_ = 0;
    std::uint32_t DAT_00355a44_rgb_ = 0;
    float DAT_00355a48_scale_ = 0.0f;
    // DAT_0055F080. Not the cloud's origin -- the lead's position as of the
    // last step, which is only there so a stationary lead can be told apart
    // from a moving one.
    orphen::ported::psm2::Vec3 DAT_0055f080_lastLeadPosition_{};
    std::vector<SmokeParticle> DAT_0054f080_particles_;
    std::vector<SmokeDrawPoint> drawList_;
    int visibleCount_ = 0;
  };

} // namespace orphen::ported::entity
