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
// ---- the draw, and where it had to come from ------------------------------
//
// **The draw.** FUN_00212F38's packet is a VU1 program's input, not a GIF
// stream: it unpacks 40 quadwords of template to VU address 32, then the
// per-particle stream is 32 positions (V4-32 to address 6) plus 32 four-byte
// attribute quads (V4-8 to address 47) whose byte 0 is `(i & 3) * 10` and byte
// 3 the alpha. The template is **four** ten-quadword variants, which is what
// that byte selects: NLOOP 1, EOP, PRE, NREG 9, FLG PACKED, REGS = RGBAQ then
// four (UV, XYZ2) pairs, PRIM = triangle fan with TME, ABE and FST set and IIP
// clear. There is no TEX0 and no ALPHA register anywhere in the packet, and the
// quad's size is applied by the microprogram, so none of the three could be
// read out of this function. A GS dump settled all of them -- see below.
//
// It does not go through FUN_00207DE8 either. The tail writes the chain
// straight into `DAT_7000000C + 0xFFF4`, one fixed slot, so the cloud has no
// display-list bucket: it lands after every 0x1000 effect packet and before the
// 0x1005 overlay ones, which is where the dump shows it.
//
// ---- what the GS dump says, measured 2026-09-18 ---------------------------
//
// Captured in PCSX2 at the "Hey! Smoke!" beat of s01_e014 and read with
// port/attic/gsparse.py. The cloud is one unmistakable group: 2236 quads at
// PRIM 0x155 -- trifan, TME, ABE, FST, IIP clear, exactly the template's --
// with UV corners (81,81)..(95,95), exactly the 0x510/0x5F0 this function
// writes. 2528 were armed; the missing 292 are near-clipped.
//
//   TEX0_1   TBP0 0x3828, TBW 4, 256x256, TCC 1, TFX 0 (MODULATE)
//   TEX2_1   PSM PSMT4, CBP 0x3F00, CSA 0
//   ALPHA_1  0x44     (Cs - Cd) * As + Cd -- plain src-alpha, no FIX
//   TEST_1   0x5000d  ATE, ATST GREATER, AREF 0
//   ZBUF_1   ZMSK 1   depth *test* GEQUAL, depth *write* off
//   TEX1_1   0x60     bilinear both ways, no mipmaps
//   RGBAQ    (48, 48, 48, 0..10) -- DAT_00355A44 and the per-particle alpha,
//            passed through unhalved. FUN_00207DE8's fold is the UI path, and
//            this packet never goes near it.
//
// TBP0 0x3828 solves FUN_002103D0's slot >= 0x18 mapping exactly --
// `(slot - 0x18) * 0x84 + 12000` -- at **slot 0x2A**, which is texture 0x178,
// the sheet the book prompt, the HUD quads and the target pips already use. So
// the cloud needs no asset of its own. PSMT4 and CSA 0 say bank 0 of it.
//
// Two things about reading that dump, both of which cost time here:
//
// - **gsparse snapshots TEX0 only.** A slot at or above 0x18 is a PSMT4 page
//   whose real PSM and CLUT arrive in a *TEX2_1* write (A+D register 0x16),
//   which TEX0 never sees. Read TEX0 alone and this sheet reports PSMT8 at
//   CBP 0x38A8, which is neither the format nor the palette it draws with.
// - **A save state's end-of-frame register file is not attribution.** Read
//   that way first, this came back as TBP0 0x3A38 with CBP 0x3F00 -- the
//   *next* draw's base against the smoke's own CLUT, a pair that belongs to no
//   draw at all and refused to solve. Attribute at the batch's first vertex.
//
// ---- the size the microprogram applies ------------------------------------
//
// :92 stages one template float, `DAT_00355658 * DAT_00355A48` -- the camera
// zoom times the stored scale -- and VU1 turns it into the quad. What it does
// with it is not in this function, so it was measured off the same dump, over
// all 2236 quads:
//
//   width / height = 2.0000 exactly       the usual 2:1 GS pixel aspect
//   gsZ vs half-width: slope 14.998       = 1 / (zoom * scale) = 15.000
//
// So the half-width in GS 12.4 units is simply `zoom * scale * gsZ`, with the
// sprite pass's own `gsZ = DAT_003555A4 / viewZ`. The depths that falls out to
// run 0.40 .. 3.51, which is the 0.4 near clip at one end and the far corner of
// the 3x3x3 box at the other -- the cloud is entirely inside its own volume,
// which is the check that the reading is not a coincidence.
//
// The four template variants differ only in which corner gets which texel, so
// `(i & 3) * 10` is a 90-degree rotation of the puff. Decoded from :73-74,
// `U[c] = f((m + c) & 2)` and `V[c] = f((m + 1 + c) & 2)` with f picking 81 for
// a clear bit 1 and 95 for a set one. All 2236 quads in the dump match one of
// those four, and the variant walks 0,1,2,3 in pool order.
//
// ---- it depends on a full 16-bit draw -------------------------------------
//
// FUN_00212DB0:49 seeds each axis's phase with a whole `FUN_00216868()`, and
// the phase is a **torus angle**: the wrap in the step gives it meaning over
// the full 0..0xFFFF. The port used to answer that call with a 15-bit LCG
// stand-in, so half of every axis was unreachable -- the box filled 1.8 of its
// 3 units, the cloud stopped partway across the screen, and the same 2528
// particles crammed into 60% of the volume read as too dense. Both symptoms,
// one cause. See original_random.h; this pool is the most sensitive caller
// the generator has, because it is the only one that uses the raw width.
//
// ---- two traps when poking it live ----------------------------------------
//
// The arm parameters match this port exactly: DAT_00354C5C 2528, DAT_00355A40
// 0x0A, DAT_00355A44 0x303030, DAT_00355A48 0.0666667. That it really is this
// pool was settled by poking the live globals rather than by inference --
// zeroing DAT_00354C5C clears the corridor completely, and writing a red
// DAT_00355A44 turns the haze red. Doing that:
//
// - **The screen smear lies for about three frames.** FUN_00201A38 keeps
//   blitting the old smoky framebuffer, so a capture two frames after a change
//   still shows the grey wash. Step at least four. This made "count = 0" look
//   like it made the fog *thicker*.
// - **Density is not evidence.** 32 particles at scale 0.5 fill the screen as
//   convincingly as 2528 at 0.0667. Read the sprite shape: enlarged, each is a
//   chunky ~15-texel puff mask with hard alpha-tested edges.
//
#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

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

  // What one particle draws as. The VU1 stream's two halves: the V4-32
  // position and the V4-8 attribute quad's two live bytes.
  struct SmokeDrawPoint
  {
    orphen::ported::psm2::Vec3 position{};
    std::uint8_t sizeIndex = 0; // `(i & 3) * 10`, the template variant
    std::uint8_t alpha = 0;
  };

  // Texture slot 0x2A, which FUN_002103D0's `(slot - 0x18) * 0x84 + 12000`
  // puts at the dump's TBP0 0x3828. Slot 0x2A is texture 0x178, the shared UI
  // sheet; TEX2_1's PSMT4 and CSA 0 make it bank 0 of it.
  inline constexpr int kSmokeTextureSlot = 0x2A;
  inline constexpr int kSmokeClutBank = 0;
  // ALPHA_1 0x44 -- (Cs - Cd) * As + Cd, the port's mode 1.
  inline constexpr int kSmokeBlendMode = 1;
  // No display-list bucket of its own: the packet goes straight into the chain
  // at DAT_7000000C + 0xFFF4, which the dump shows drawing after every 0x1000
  // effect and before the 0x1005 overlays.
  inline constexpr int kSmokeDisplayListBucket = 0x1001;
  // :73-74. `0x58` is the centre texel and `7` the half-extent, both scaled by
  // 0x10 into GS 4-bit fixed and undone here.
  inline constexpr float kSmokeTexelCentre = 88.0f;
  inline constexpr float kSmokeTexelHalfExtent = 7.0f;
  // DAT_003555A4, the sprite pass's depth numerator. The half-width in GS 12.4
  // units is `zoom * scale * gsZ` and `gsZ` is this over the view depth, so the
  // two fold into one constant here.
  inline constexpr float kDAT_003555a4_smokeDepthNumerator = 19706.0859f;

  // One particle's quad, in the GS screen units the microprogram works in.
  struct SmokeQuadInputs
  {
    // FUN_0020B600's integer origin for the particle, GS 12.4 units.
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    // ViewProjection::projection at (0,0), (1,1), (2,0) and (2,1) -- what the
    // un-projection back to view space needs, as the other pools take it.
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    // DAT_00355658 * DAT_00355A48, the one float :92 stages for VU1.
    float zoomTimesScale = 0.0666667f;
    // The template variant, 0..3 -- `sizeIndex / 10`.
    int variant = 0;
    // DAT_00355A44 and the particle's own alpha, both raw GS bytes.
    std::uint32_t rgb = 0;
    std::uint8_t alpha = 0;
  };

  // FUN_00212F38's draw half, as VU1 applies it.
  orphen::ported::render::SpriteQuad
  FUN_00212f38_build_smoke_quad(const SmokeQuadInputs &inputs);

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
