#pragma once

// Orphen's bandana.
//
// It is not part of his mesh. It is a **whole separate entity** -- type 0x19,
// model grp_001E -- parked in reserved pool slot 4, attached to the player's
// neck bone, and driven every frame by a rope simulation that writes straight
// into the scripted bone override table. Three original functions:
//
//   src/FUN_00251e40.c  create it, once, and only when the lead player is type 1
//   src/FUN_00213720.c  the simulation, reached as type 0x19's actor handler
//   src/FUN_00213640.c  enable/disable, clearing all 18 overrides on the way out
//
// **The dispatch table is the proof that FUN_00213720 is a behavior.** Nothing
// in the executable calls it by name; it is `PTR_FUN_0031c6c0[0x19 - 1]`, and
// every neighbouring entry in that table is either the generic no-op FUN_00239e78
// or the party-member shell FUN_0025ab68.
//
// The model is 20 bones: bone 0 is the knot, bones 1..9 are one tail and bones
// 10..18 are the other, and all 18 hang off bone 0 as *siblings* rather than as
// two chains -- they have to, because the simulation hands each bone an absolute
// translation relative to the knot rather than a link-to-link one. Bone 19 is
// an empty second root. Every primitive draws untextured through the
// `0x8000` subdraw path, so the colour is colour-table entry 0: (191, 0, 0).
//
// Confirmed field by field against the EE dump `s01_e24.bin`, which has slot 4
// live: type 25, +0x20..+0x28 = (0, 0.04, 0.08), +0x192 = 0, +0x194 = 0xE0
// (-32, and bone 32 of grp_0001 is the one carrying role 7), +0x94 = 1, and the
// two chains' state at 0x0054EE00 hanging at exactly 0.025 per segment.

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"
#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace orphen::ported::entity
{

  // FUN_00251e40's `FUN_0020dd78(player, 7)`. Role 7 is the neck on grp_0001
  // (bone 32), whose child is the head at role 1.
  inline constexpr std::uint8_t kBandanaAnchorBoneRole = 7;
  inline constexpr std::int32_t kBandanaTypeId = 0x19;
  // FUN_0022a418 releases slots 2..9 by address and then calls FUN_00251e40,
  // which builds this one. 0x0058C610 - 0x0058BEB0 == 4 * 0x1D8.
  inline constexpr std::size_t kBandanaSlot = 4;

  // uGpffff88b0 / uGpffff88b4 at 0x00352820, written into the entity's
  // +0x24/+0x28 with +0x20 left at zero. For a parented entity those three are a
  // bone-local offset, so this is where the knot sits relative to the neck.
  inline constexpr float kuGpffff88b0_anchorOffsetY = 0.04f;
  inline constexpr float kuGpffff88b4_anchorOffsetZ = 0.08f;

  // The two chains and their ten slots each, DAT_0054EE00 with stride 0x20 per
  // segment and 0x140 per chain. Slot 0 is the anchor, not a simulated segment.
  inline constexpr std::size_t kBandanaChainCount = 2;
  inline constexpr std::size_t kBandanaSegmentCount = 10;

  // Every tuning constant, read out of SLUS_200.11 rather than guessed. The
  // names are the decompiled symbols so each one can be checked against
  // src/FUN_00213720.c line by line.
  inline constexpr float kDAT_003520c4_initialGravity = 0.003f;
  inline constexpr float kDAT_003520c8_halfPi = 1.570796012878418f;
  // Re-rolled whenever `DAT_003555b4 & 0x3F` is zero, i.e. every 64th frame:
  // `(rng & 3) * 0.004 + 0.006`, so one of 0.006 / 0.010 / 0.014 / 0.018. The EE
  // dump caught the two chains holding 0.018 and 0.010.
  inline constexpr float kDAT_003520cc_gravityStep = 0.004f;
  inline constexpr float kDAT_003520d0_gravityBase = 0.006f;
  // Half the gap between the two tails: chain 0 gets -0.011, chain 1 +0.011.
  inline constexpr float kDAT_003520d4_chainSpread = 0.011f;
  // Only reached when the entity's +0x198 is 1, which nothing sets on slot 4.
  inline constexpr float kDAT_003520d8_jitterX = 0.01f;
  inline constexpr float kDAT_003520dc_jitterZ = 0.004f;
  // The rope link length. The dump's segments sit exactly this far apart.
  inline constexpr float kDAT_003520e0_segmentLength = 0.025f;
  inline constexpr float kDAT_003520e4_twoPi = 6.283184051513672f;
  inline constexpr float kDAT_003520e8_chain1SweepDivisor = 7000.0f;
  inline constexpr float kDAT_003520ec_twoPi = 6.283184051513672f;
  inline constexpr float kDAT_003520f0_chain0SweepDivisor = 9000.0f;
  inline constexpr float kDAT_003520f4_twoPi = 6.283184051513672f;
  // Chain 0 divides the tick counter by 512 before the fmod; chain 1 by 480, so
  // the two tails run at slightly different periods and drift apart.
  inline constexpr float kChain0WavePeriod = 512.0f;
  inline constexpr float kChain1WavePeriod = 480.0f;
  inline constexpr float kWaveAmplitudeDivisor = 4000.0f;
  inline constexpr float kSegmentPhaseTicks = 128.0f; // iVar15 * 0x80

  // DAT_003151a0, indexed by segment. How far *forward* of the anchor a segment
  // is allowed to sit while it is below the anchor -- the clamp that stops a
  // tail sinking through Orphen's back.
  inline constexpr std::array<float, kBandanaSegmentCount> kDAT_003151a0_forwardLimit{
      0.0f, 0.0f, 0.04f, 0.05f, 0.058f, 0.063f, 0.067f, 0.067f, 0.067f, 0.067f};

  // One segment's 0x20 block. Only these fields are ever written; the first
  // three floats stay at zero, which is why the bandana's bones never take an X
  // or Y rotation.
  struct BandanaSegment
  {
    orphen::ported::model::Vec3 position{}; // +0x0C..+0x14, world space
    float scale = 1.0f;                     // +0x18, handed to the override as-is
  };

  struct BandanaChain
  {
    std::array<BandanaSegment, kBandanaSegmentCount> segments{};
    float gravity = kDAT_003520c4_initialGravity; // segment 0's +0x1C
  };

  // DAT_0054EE00. Lives beside the entity rather than inside it because the
  // original's does too -- it is a fixed BSS block, not part of the pool slot.
  struct BandanaState
  {
    std::array<BandanaChain, kBandanaChainCount> chains{};
  };

  // What FUN_00213720 reaches for outside its own entity.
  struct BandanaEnvironment
  {
    // The bandana's *own* matrix palette, which is where the anchor comes from:
    // FUN_00213720 asks FUN_0020dc88 for its own bone 0, not for the neck. The
    // neck reaches the rope only through the entity root matrix FUN_0020cdc0
    // built from it. This is last frame's palette, exactly as in the original --
    // the behavior runs in FUN_00239ce0 and the palette is not rebuilt until
    // FUN_0020c5a8, later in the same frame.
    std::span<const orphen::ported::model::Matrix4> selfPalette;
    // FUN_0020dc88's no-palette fallback, for the first frame: the attachment
    // root's world X with the entity's own +0x24 and +0x28 + height/2.
    orphen::ported::model::Vec3 anchorFallback{};

    // Walked up +0x192 by the caller. FUN_00213720 copies both onto itself, so
    // the bandana turns with the actor and fades with it.
    float rootFacingRadians = 0.0f;
    std::uint8_t rootFadeLevel = 0;

    // DAT_003555b4 and DAT_003555b8. The frame counter and the accumulated tick
    // counter; three EE dumps all read b8 == b4 * 32 exactly, which is
    // `b8 += DAT_003555bc` at the nominal tick rate.
    std::uint32_t frameCounter003555b4 = 0;
    std::uint32_t tickCounter003555b8 = 0;

    // FUN_00216868. Only its low two bits are used.
    std::function<std::uint32_t()> random;
  };

  // FUN_00213720, type 0x19's actor handler. Writes 18 bone overrides with
  // duration 1, so every one of them snaps rather than easing.
  void FUN_00213720_bandana(OriginalEntity &entity,
                            BandanaState &state,
                            orphen::ported::model::EntityBoneOverrides &overrides,
                            const BandanaEnvironment &environment);

  // FUN_00213640's clear path: the 18 driven bones, released back to the
  // model's own (static) pose.
  void FUN_00213640_release_bandana_bones(orphen::ported::model::EntityBoneOverrides &overrides);

  // FUN_00251e40. Does nothing unless the lead player is type 1 -- Orphen is the
  // only party member who wears one, and this is the whole of the check: a
  // single `*(short *)param_1 == 1`. Returns true when slot 4 was built.
  //
  // `playerModel` is grp_0001, needed for FUN_0020dd78's role lookup. Without it
  // the anchor bone cannot be resolved and the attachment is not created, which
  // is reported rather than silently defaulting to bone 0.
  bool FUN_00251e40_attach_bandana(EntityPool &pool,
                                   const EntityDescriptorTable &descriptors,
                                   const orphen::ported::model::Psc3Model *playerModel);

} // namespace orphen::ported::entity
