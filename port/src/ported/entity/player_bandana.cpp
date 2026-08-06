#include "ported/entity/player_bandana.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::model::Matrix4;
    using orphen::ported::model::Vec3;

    // FUN_00216598: plain 3D Euclidean distance. The decompiler renders it as an
    // empty function because the whole body is FPU work in the delay slots; the
    // disassembly at 0x00216598 is three subs, three muls, two adds and a sqrt.
    float FUN_00216598_distance(const Vec3 &a, const Vec3 &b)
    {
      const float dx = a.x - b.x;
      const float dy = a.y - b.y;
      const float dz = a.z - b.z;
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // FUN_00305530 is fmodf and FUN_00305218 is sinf, so both wave terms are
    // `sinf(fmodf(t, 2pi))`. The fmod is redundant for a well-behaved sinf; it
    // is kept because the original's operand can grow large enough for the
    // reduction to matter, and dropping it would be a silent behavioural change.
    float wave(float t, float twoPi) { return std::sin(std::fmod(t, twoPi)); }

    // The state block, DAT_0054EE00, is only ever indexed segment-relative, so
    // segment 0's fields double as the chain's own: +0x0C..+0x14 is the anchor
    // the rope hangs from and +0x1C is the chain's gravity.
    BandanaSegment &anchorOf(BandanaChain &chain) { return chain.segments[0]; }
  } // namespace

  void FUN_00213640_release_bandana_bones(orphen::ported::model::EntityBoneOverrides &overrides)
  {
    // FUN_00213640's nested loop walks 9 down to 0 and then 18 down to 9, so
    // bone 9 is cleared twice and bone 0 -- the knot, which the simulation never
    // drives -- is cleared once. Clearing 0..18 is the same set.
    for (std::size_t bone = 0; bone <= kBandanaChainCount * 9; ++bone)
    {
      orphen::ported::model::FUN_0020d9c8_clear_bone_override(overrides, bone);
    }
  }

  bool FUN_00251e40_attach_bandana(EntityPool &pool,
                                   const EntityDescriptorTable &descriptors,
                                   const orphen::ported::model::Psc3Model *playerModel)
  {
    if (pool.leadPlayer().typeId00 != 1)
    {
      return false;
    }
    if (playerModel == nullptr || !playerModel->valid)
    {
      return false;
    }

    // FUN_00229c40(0x58C610, 0x19). Slot 4 is not allocated through
    // FUN_00265dc0 -- the original writes the address of the slot literally, and
    // FUN_0022a418 releases it a few lines earlier so this always builds a fresh
    // one.
    pool.FUN_00229c40_initialize(kBandanaSlot, kBandanaTypeId, descriptors);

    OriginalEntity &bandana = pool.slot(kBandanaSlot);
    // +0x192 is the parent's *pool slot*, which the original computes from the
    // pointer it was handed; it is always slot 0 here, because FUN_00251e40 is
    // only ever called with the lead player.
    bandana.parentSlot192 = 0;
    // Negated, which is what puts FUN_0020cdc0 on its position-follow branch.
    bandana.attachBone194 = static_cast<std::int8_t>(
        -static_cast<int>(orphen::ported::model::FUN_0020dd78_bone_for_role(
            *playerModel, kBandanaAnchorBoneRole)));
    // Bone-local, not world: this is where the knot sits relative to the neck.
    bandana.positionX20 = 0.0f;
    bandana.positionZ24 = kuGpffff88b0_anchorOffsetY;
    bandana.positionY28 = kuGpffff88b4_anchorOffsetZ;
    return true;
  }

  void FUN_00213720_bandana(OriginalEntity &entity,
                            BandanaState &state,
                            orphen::ported::model::EntityBoneOverrides &overrides,
                            const BandanaEnvironment &environment)
  {
    // entity +0x94, the once-only seed. Every segment starts 100 units below the
    // world so the first frame's length constraint pulls the whole tail into a
    // straight line under the anchor rather than growing it out of a point.
    if (entity.spawnParam94 == 0)
    {
      entity.spawnParam94 = 1;
      for (BandanaChain &chain : state.chains)
      {
        for (BandanaSegment &segment : chain.segments)
        {
          segment.position = Vec3{0.0f, 0.0f, -100.0f};
          segment.scale = 1.0f;
        }
        chain.gravity = kDAT_003520c4_initialGravity;
      }
    }

    // FUN_00213720 walks +0x192 to the root of the attachment chain and takes
    // its facing and fade level, so an attachment hanging off an attachment
    // still tracks the actor at the bottom of it. Slot 4 hangs directly off the
    // lead player, so this resolves in one step -- the walk is in the original
    // and is reproduced by the caller, which passes the root's values in.
    entity.facingRadians5c = environment.rootFacingRadians;
    entity.fadeLevel134 = environment.rootFadeLevel;

    // Animation 0 is the only one that simulates. Nothing in the port selects
    // another, but FUN_00225bc8 can, and the original stops the rope dead when
    // it does rather than blending out of it.
    if (entity.animationA0 != 0)
    {
      return;
    }

    // The root's frame. Rotating by -(facing) - pi/2 turns a world-space offset
    // from the anchor into the (lateral, forward) pair the bone override wants,
    // because the bone translations are in the knot's space and the knot faces
    // the way the actor does.
    //
    // **FUN_00305218 is sinf and FUN_00305130 is cosf**, not the other way
    // round: 0x00305218's small-argument path calls `__kernel_sin(x, 0, 0)` and
    // its n&3 switch is fdlibm's sine, while 0x00305130 takes `__kernel_cos`.
    // Two older files under analyzed/ label them backwards. Getting it wrong
    // here rotates the whole frame a quarter turn, which puts DAT_003151a0's
    // body clamp -- up to 0.067, six times the 0.011 tail spread -- on the
    // sideways axis, so the tails drift into the neck instead of trailing.
    const float frameAngle = -entity.facingRadians5c - kDAT_003520c8_halfPi;
    // Named for the scratch slots they occupy, because which one multiplies
    // which component is the whole point: FUN_00213720 line 84 stores
    // FUN_00305218's result at +0x15 and line 86 stores FUN_00305130's at +0x16.
    const float slot15Sin = std::sin(frameAngle);
    const float slot16Cos = std::cos(frameAngle);

    // FUN_0020dc88(self, bone 0, DAT_00315190, out). DAT_00315190 is six zero
    // floats, so this is the knot bone's own origin -- last frame's, since the
    // palette is not rebuilt until after the actor loop.
    const Vec3 anchor = orphen::ported::model::FUN_0020dc88_bone_point(
        environment.selfPalette, 0, Vec3{0.0f, 0.0f, 0.0f}, environment.anchorFallback);

    const bool rerollGravity = (environment.frameCounter003555b4 & 0x3Fu) == 0;
    const float tick = static_cast<float>(environment.tickCounter003555b8);

    for (std::size_t chainIndex = 0; chainIndex < kBandanaChainCount; ++chainIndex)
    {
      BandanaChain &chain = state.chains[chainIndex];
      anchorOf(chain).position = anchor;

      // Every 64th frame each chain draws its own new fall rate out of
      // {0.006, 0.010, 0.014, 0.018}. That is the only source of gravity here --
      // there is no velocity, so a segment falls a fixed distance per frame and
      // the length constraint is what turns that into a swing.
      if (rerollGravity && environment.random)
      {
        chain.gravity = static_cast<float>(environment.random() & 3u) * kDAT_003520cc_gravityStep +
                        kDAT_003520d0_gravityBase;
      }

      // -0.011 for the first tail, +0.011 for the second. This is applied to the
      // bone translation, not to the simulation: both ropes hang from the same
      // point and are only pushed apart when they are drawn.
      const float spread =
          (static_cast<float>(chainIndex) * 2.0f - 1.0f) * kDAT_003520d4_chainSpread;
      const float sweepDivisor = chainIndex == 0 ? kDAT_003520f0_chain0SweepDivisor
                                                 : kDAT_003520e8_chain1SweepDivisor;
      const float wavePeriod = chainIndex == 0 ? kChain0WavePeriod : kChain1WavePeriod;

      for (std::size_t index = 1; index < kBandanaSegmentCount; ++index)
      {
        BandanaSegment &segment = chain.segments[index];
        const BandanaSegment &previous = chain.segments[index - 1];

        segment.position.z -= chain.gravity;

        // entity +0x198 == 1 adds a random tug every frame. Nothing writes it on
        // slot 4 -- FUN_00229c40 clears the slot and FUN_00251e40 does not set
        // it -- and the EE dump has it at zero, so this branch is dead in the
        // shipped game unless something else claims the type.
        if (entity.eventFlagId198 == 1 && environment.random)
        {
          segment.position.x -=
              static_cast<float>(environment.random() & 3u) * kDAT_003520d8_jitterX +
              kDAT_003520d8_jitterX;
          segment.position.z -=
              static_cast<float>(environment.random() & 3u) * kDAT_003520dc_jitterZ;
        }

        // The rope link. Only ever shortens: a segment that has drifted further
        // than 0.025 from the one before it is pulled straight back onto the
        // sphere of that radius, and one that is closer is left where it is.
        const float distance = FUN_00216598_distance(segment.position, previous.position);
        if (distance > kDAT_003520e0_segmentLength)
        {
          const float scale = kDAT_003520e0_segmentLength / distance;
          segment.position.x =
              previous.position.x + (segment.position.x - previous.position.x) * scale;
          segment.position.y =
              previous.position.y + (segment.position.y - previous.position.y) * scale;
          segment.position.z =
              previous.position.z + (segment.position.z - previous.position.z) * scale;
        }

        const Vec3 &anchorPosition = anchorOf(chain).position;
        const float relX = segment.position.x - anchorPosition.x;
        const float relY = segment.position.y - anchorPosition.y;
        const float relZ = segment.position.z - anchorPosition.z;

        // FUN_00213720 lines 141 and 144. `forward` is positive *behind* the
        // actor: it works out to -(rel . facingDirection), which is why
        // DAT_003151a0's limits are positive -- the clamp pushes a tail that has
        // swung under the chin back out behind the shoulders.
        float forward = relX * slot15Sin + relY * slot16Cos;
        float lateral = relX * slot16Cos - relY * slot15Sin;

        // The back-of-the-head clamp: a segment hanging below the knot may not
        // sit further forward than its limit, which grows from 0 at the first
        // link to 0.067 from the sixth on. Without it the tails swing through
        // Orphen's shoulders when he turns.
        if (forward < kDAT_003151a0_forwardLimit[index] && relZ < 0.0f)
        {
          forward = kDAT_003151a0_forwardLimit[index];
        }

        // Two cosine terms on top of the rope, both scaled by how far down the
        // tail the segment is. The 128-tick phase step between segments is what
        // makes it read as a ripple travelling outward rather than the whole
        // tail sliding sideways together.
        const float amplitude =
            static_cast<float>(index + 5) / kWaveAmplitudeDivisor;
        const float phase = tick + static_cast<float>(index) * kSegmentPhaseTicks;
        lateral += wave(phase / wavePeriod, kDAT_003520ec_twoPi) * amplitude;
        forward += wave(tick * static_cast<float>(index + 8) / sweepDivisor,
                        kDAT_003520f4_twoPi) *
                   amplitude;

        // The caller-order pose array FUN_0020d8c0 takes: rotation xyz, then
        // translation xyz, then scale. The first two rotations come straight off
        // the segment's own +0x00/+0x04, which nothing ever writes, so the
        // bandana's bones only ever roll -- proportionally to how far the
        // segment has swung sideways, and mirrored between the two tails.
        std::array<float, orphen::ported::model::kPoseFieldCount> pose{};
        pose[0] = 0.0f;
        pose[1] = 0.0f;
        pose[2] = chainIndex == 0 ? -lateral * 30.0f : lateral * 30.0f;
        pose[3] = lateral + spread;
        pose[4] = forward;
        pose[5] = relZ;
        pose[6] = segment.scale;

        // chain * 9 + (10 - index): bones 9..1 for the first tail and 18..10 for
        // the second, so the tip of each tail is its lowest bone index.
        const std::size_t bone = chainIndex * 9u + (kBandanaSegmentCount - index);
        orphen::ported::model::FUN_0020d8c0_set_bone_override(overrides, bone, pose, 1);
      }
    }
  }

} // namespace orphen::ported::entity
