#pragma once

// Motion trails -- the green ribbon the sword leaves as it swings.
//
//   src/FUN_0020e840.c  0x0020e840  record a sample, build the ribbon, submit it
//   src/FUN_0020e760.c  0x0020e760  claim a trail slot
//   src/FUN_0020e7b0.c  0x0020e7b0  release one
//   src/FUN_00266a78.c  0x00266a78  load a natural cubic spline with n points
//   src/FUN_00266460.c  0x00266460  its tridiagonal solve
//   src/FUN_00266610.c  0x00266610  which segment a parameter falls in
//   src/FUN_00266668.c  0x00266668  evaluate the cubic
//   src/FUN_0020d820.c  0x0020d820  project a run of points and reject off-screen
//
// WHERE IT LIVES. FUN_0020c810 calls this last, after the bone palette is
// composed and after FUN_0020eec0 has computed the entity's depth bucket -- so
// it is part of the per-entity model draw, not one of the standalone effect
// systems FUN_002192c0 runs. That is why every one of those systems' gate
// globals reads zero in the sword_trail save state while the trail is plainly
// on screen: none of them owns it.
//
// WHAT DRIVES IT. The low eight bits of entity +0xAA -- the halfword the
// animation stepper stages out of the current keyframe -- are an enable mask,
// one bit per descriptor in the model's header +0x38 table. A bit that goes
// from clear to set claims a slot out of a global pool of 32; a bit that goes
// from set to clear releases it. Entity +0xB0 holds last frame's mask and
// +0xB1..+0xB8 the eight slot handles.
//
// grp_0179, the sword blade, carries two descriptors and its **animation 0**
// turns both on. Animation 0 is the long phase after the six-frame spawn
// flourish -- the one that also runs the swept hit test -- so the trail is
// alive for exactly as long as the blade can hit something.
//
// THE SHAPE. A trail is a ribbon between two named vertices of the model,
// skinned by their own bones like any other vertex, sampled once per frame into
// a 16-deep history with the newest at index 0. Each frame the last
// `sampleCount` of those samples become the control points of a natural cubic
// spline -- one per edge -- resampled at twelve points, and the twelve pairs
// are stitched into eleven untextured gouraud quads whose alpha ramps linearly
// from the descriptor's own alpha at the newest end to zero at the oldest.
//
// The quads are triangle fans with IIP set and TME clear (FUN_00207de8's PRIM
// word 0x0D, ABE raised by the mode word), which is why there is no texture
// anywhere in this file.

#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orphen::ported::render
{

  using orphen::ported::model::Matrix4;
  using orphen::ported::psm2::Vec3;

  namespace weaponTrail
  {
    // FUN_0020e760 walks 32 slots from 0x004FBE00, each 0x184 bytes, and hands
    // back a **one-based** handle -- zero is "none free". The pool base it
    // indexes is 0x004FBC7C, one stride lower, so handle h is the h'th record.
    inline constexpr std::size_t kSlotCount = 32;
    // One byte of live sample count and two arrays of 16 float3, which is
    // exactly the 0x184 the allocator strides by.
    inline constexpr std::size_t kHistoryDepth = 16;
    // FUN_0020e840 clamps the descriptor's stored count into [3, 16] before
    // clamping it again to what has actually been recorded.
    inline constexpr int kMinimumSampleCount = 3;
    // Twelve points per edge, eleven quads between them. Both numbers are
    // literal in FUN_0020e840: the resample loop runs to 0xC and the emit loop
    // to 0xB, and the alpha ramp divides by 0xC.
    inline constexpr std::size_t kResampleCount = 12;
    inline constexpr std::size_t kQuadCount = kResampleCount - 1;
    // The parameter the resample walks: `i / 11.0`, so it reaches 1.0 exactly.
    inline constexpr float kResampleDivisor = 11.0f;

    // FUN_0020e840's mode word DAT_10004780. Bit 0x80 is the gate FUN_00207de8
    // tests before it will emit anything at all; the `& 0x1C000` ladder sees
    // 0x4000 and picks blend mode 1, which is where this differs from the hit
    // sparks' 0x8000 / mode 2 additive; and bit 0x10000000 says the vertex
    // coordinates are already integers, which they are because FUN_0020d820
    // wrote them through vftoi0.
    inline constexpr std::uint32_t kDAT_10004780_modeWord = 0x10004780u;
    inline constexpr int kBlendMode = 1;
    // FUN_0020e840 writes 0xFFFF into the packet's texture halfword, and
    // FUN_00207de8 increments that field before use -- so it arrives as zero and
    // takes the untextured branch, PRIM 0x0D rather than 0x1D. It is the only
    // caller in the executable that asks for an untextured primitive.
    inline constexpr std::uint16_t kNoTexture = 0xFFFFu;

    // FUN_00207de8's colour fixup for an untextured packet: only the alpha is
    // halved, and the rgb reaches the GS untouched. (A textured one has all
    // four channels halved.) 0x80 is 1.0 on the GS, so the descriptor's 0xC8
    // alpha lands at 100/128 and its rgb is free to run over one.
    inline constexpr float kGsUnit = 128.0f;

    // FUN_0020d820 rejects a point whose projected depth reaches the value the
    // projection puts at the near plane. uGpffff80b0 is 65534.0 and
    // FUN_0020bd58 maps the near plane to exactly it, so the test is "in front
    // of the near plane" written in screen units.
    inline constexpr int kScreenZAtNearPlane = 0xFFFE;
  } // namespace weaponTrail

  // FUN_00266a78's workspace, laid out the way the original lays it out over
  // the 0x204 bytes at its argument: a count, a cached segment index, three
  // arrays of 16 values, three arrays of 16 cubic coefficients and the knots.
  // Three curves, because it is always fed a run of 3D points.
  struct CubicSpline
  {
    int pointCount = 0;
    int segment = 0;
    std::array<std::array<float, weaponTrail::kHistoryDepth>, 3> value{};
    std::array<std::array<float, weaponTrail::kHistoryDepth>, 3> coefficient{};
    std::array<float, weaponTrail::kHistoryDepth> knot{};
  };

  // FUN_00266460. Solves for `coefficient` given the knots and the values; both
  // ends are pinned to zero, which is what makes it the *natural* spline.
  void FUN_00266460_solve(int pointCount,
                          const float *knot,
                          const float *value,
                          float *coefficient);
  // FUN_00266610. A binary search for the segment containing `parameter`.
  int FUN_00266610_segmentFor(float parameter, int pointCount, const float *knot);
  // FUN_00266668. The cubic itself, in the original's own grouping -- its
  // coefficient is a third of the textbook one and the powers are arranged to
  // match, so the two only agree when they are read together.
  float FUN_00266668_evaluate(float parameter,
                              int segment,
                              const float *knot,
                              const float *value,
                              const float *coefficient);
  // FUN_00266a78 with its param_4 at zero, the only way FUN_0020e840 calls it:
  // the knots are a uniform 0..1 rather than chord lengths.
  void FUN_00266a78_load(CubicSpline &spline, const Vec3 *points, int pointCount);
  // FUN_00266ce8.
  Vec3 FUN_00266ce8_sample(CubicSpline &spline, float parameter);

  // One entry of the pool at DAT_004FBC7C. Index 0 is the newest sample.
  struct TrailSlot
  {
    std::uint8_t sampleCount = 0;
    std::array<Vec3, weaponTrail::kHistoryDepth> edgeA{};
    std::array<Vec3, weaponTrail::kHistoryDepth> edgeB{};
  };

  // One quad, in world space and with its own colour at each corner. The
  // original has already projected by this point; the port keeps the corners in
  // world space for the same reason the hit sparks do, so the scene's own
  // projection is the only one in the picture.
  struct TrailQuad
  {
    std::array<Vec3, 4> corner{};
    // RGBA over the GS's 0x80, in the packet's own corner order.
    std::array<std::array<float, 4>, 4> colour{};
  };

  // The 32-slot pool and its allocation mask, DAT_00355A3C.
  class WeaponTrailPool
  {
   public:
    // FUN_0020e760.
    int FUN_0020e760_claim();
    // FUN_0020e7b0. Frees the bit; the samples are left where they are, and the
    // claim clears the count rather than the release.
    void FUN_0020e7b0_release(int handle);

    void reset();

    // FUN_0020e840. `mask` is the low byte of entity +0xAA and `previousMask`
    // entity +0xB0, which this updates; `handle` is entity +0xB1..+0xB8.
    // Appends this frame's quads to `out`.
    //
    // `bonePalette` is the entity's composed world matrices, one per submesh --
    // FUN_0020e840 reads the same buffer the skinning does, at ctx+0x158.
    void FUN_0020e840_step(const orphen::ported::model::Psc3Model &model,
                           const std::vector<Matrix4> &bonePalette,
                           std::uint16_t flagsAa,
                           std::uint8_t &previousMask,
                           std::array<std::uint8_t, 8> &handle,
                           std::vector<TrailQuad> &out);

    std::size_t liveSlotCount() const;
    std::uint32_t DAT_00355a3c_mask() const { return allocationMask_; }

   private:
    std::uint32_t allocationMask_ = 0;
    std::array<TrailSlot, weaponTrail::kSlotCount> slot_{};
  };

} // namespace orphen::ported::render
