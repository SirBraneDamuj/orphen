#include "ported/entity/party_follower.h"

#include "ported/camera/original_field_camera.h"
#include "ported/model/psc3_skeleton.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // The lead player is pool slot 0. Every DAT_0058beXX the follower reads is
    // a field of it -- DAT_0058bed0 is 0x58BEB0 + 0x20, DAT_0058bf0c is +0x5C,
    // DAT_0058befc is +0x4C, DAT_0058bee0/e4 are +0x30/+0x34.
    constexpr std::size_t kLeadSlot = 0;

    // The pathfinder's own constants.
    constexpr float kDAT_00352a1c_twoPi = 6.28318405f;       // FUN_00259178's spread scale
    constexpr float kDAT_00352a20_yieldTurn = 1.57079601f;   // a quarter turn off a blocker
    constexpr float kDAT_00352a24_yieldTurn = 1.57079601f;
    constexpr float kDAT_00352a28_pathSpeedScale = 1.20000005f;
    constexpr float kfGpffff8afc_pathArrivalRadius = 1.20000005f;
    constexpr float kfGpffff8b00_pathSpeedDivisor = 200000.0f;
    // FUN_0023a0a8's two probe reaches and its step limit, at 0x003525e0.
    constexpr float kDAT_003525e0_nearProbe = 0.3f;
    constexpr float kDAT_003525e4_farProbe = 0.8f;
    constexpr float kDAT_003525e8_probeStepLimit = 0.28f;

    // The constant block at 0x00352A28, read straight out of SLUS_200.11.
    constexpr float kDAT_00352a2c_formationDropFloor = -0.6f;
    constexpr float kDAT_00352a30_formationDropCeiling = 0.6f;
    constexpr float kDAT_00352a34_formationDeadZone = 0.3f;
    constexpr float kDAT_00352a38_initSampleLift = 0.4f;
    constexpr float kDAT_00352a3c_firstFormationAngle = 2.6179933547973633f; // 150 degrees
    constexpr float kDAT_00352a40_pi = 3.141592025756836f;
    constexpr float kDAT_00352a44_pushLow = -0.785398006439209f;
    constexpr float kDAT_00352a48_pushHigh = 0.785398006439209f;
    constexpr float kDAT_00352a4c_pushSpeedScale = 1.2f;
    constexpr float kDAT_00352a50_pushLeft = 1.570796012878418f;
    constexpr float kDAT_00352a54_pushRight = -1.570796012878418f;
    constexpr float kDAT_00352a58_followSpeedScale = 1.2f;
    constexpr float kDAT_00352a5c_twoPi = 6.283184051513672f;
    constexpr float kDAT_00352a60_state2TurnRate = 0.34906578063964844f;
    constexpr float kDAT_00352a64_state2BumpTurn = 0.5235986709594727f;
    constexpr float kDAT_00352a68_state2SpeedDivisor = 200000.0f;
    constexpr float kDAT_00352a78_state7SpeedDivisor = 200000.0f;
    constexpr float kDAT_00352a7c_state8SpeedDivisor = 200000.0f;
    constexpr float kDAT_00352a80_blockerScatter = 6.283184051513672f;
    constexpr float kDAT_00352a84_blockerRight = -1.570796012878418f;
    constexpr float kDAT_00352a88_blockerLeft = 1.570796012878418f;
    constexpr float kDAT_00352a8c_aheadLow = -0.785398006439209f;
    constexpr float kDAT_00352a90_aheadHigh = 0.785398006439209f;
    constexpr float kDAT_00352a94_sidestepScatter = 6.283184051513672f;
    constexpr float kDAT_00352a98_catchUpBonus = 0.05f;
    constexpr float kDAT_00352a9c_walkStep = 0.023f;
    constexpr float kDAT_00352aa0_runStep = 0.045f;
    constexpr float kDAT_00352aa4_runSpeedScale = 1.2f;
    constexpr float kDAT_00352aa8_turnRate = 0.005454152822494507f;
    constexpr float kfGpffff8b3c_sidestepLow = -0.785398006439209f;
    constexpr float kfGpffff8b40_sidestepHigh = 0.785398006439209f;
    constexpr float kfGpffff8b44_state9SpeedDivisor = 200000.0f;
    constexpr float kfGpffff8a98_pi = 3.141592025756836f;
    // FUN_0025a500's `* 0.03125` after the tick multiply, inline in the code.
    constexpr float kTickToFrame = 0.03125f;
    // The look-at cone and the head/bust share of it, from the block at
    // 0x003529D8: FUN_00257c78's four thresholds and its bust ratio.
    constexpr float kfGpffff8a68_lookDeadHigh = 0.17453289031982422f;  // 10 degrees
    constexpr float kfGpffff8a6c_lookDeadLow = -0.17453289031982422f;
    constexpr float kfGpffff8a70_lookLimitHigh = 1.0471973419189453f;  // 60 degrees
    constexpr float kfGpffff8a74_lookLimitLow = -1.0471973419189453f;
    constexpr float kfGpffff8a78_bustShare = 0.30000001192092896f;

    // FUN_00257c78 / FUN_00257f78 / FUN_00257f18 all drive the same two bones:
    // role 2 first, then role 1.
    constexpr std::uint8_t kBustRole = 2;
    constexpr std::uint8_t kHeadRole = 1;
    // FUN_002597d0 and FUN_00257c78 both pass 10 as the override duration.
    constexpr int kLookOverrideFrames = 10;

    // The pose field FUN_00257c78 twists: FUN_0020d8c0 takes rotation xyz first,
    // so index 2 is the yaw.
    constexpr std::size_t kYawField = 2;

    float FUN_00216690_wrap(float angle)
    {
      return orphen::ported::model::FUN_00216690_wrap_angle(angle);
    }

    float FUN_002166e8_delta(float from, float to)
    {
      return orphen::ported::model::FUN_002166e8_angle_delta(from, to);
    }

    // FUN_00216608: sqrt(x*x + y*y).
    float FUN_00216608_length(float x, float y) { return std::sqrt(x * x + y * y); }

    // FUN_0023a480: the angle from `entity` to the lead.
    float FUN_0023a480_angle_to_lead(const OriginalEntity &entity, const OriginalEntity &lead)
    {
      return std::atan2(lead.positionZ24 - entity.positionZ24, lead.positionX20 - entity.positionX20);
    }

    // FUN_0023a418: the flat distance from `entity` to the lead.
    float FUN_0023a418_distance_to_lead(const OriginalEntity &entity, const OriginalEntity &lead)
    {
      return FUN_00216608_length(entity.positionX20 - lead.positionX20,
                                 entity.positionZ24 - lead.positionZ24);
    }

    // FUN_0023a4b8 / FUN_0023a4e8: the same two, between any pair.
    float FUN_0023a4b8_angle_between(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    float FUN_0023a4e8_distance_between(const OriginalEntity &from, const OriginalEntity &to)
    {
      return FUN_00216608_length(to.positionX20 - from.positionX20,
                                 to.positionZ24 - from.positionZ24);
    }

    // FUN_002298d0: a party character's type id back to its slot in the seven.
    int FUN_002298d0_party_slot_for_type(std::int16_t type)
    {
      switch (type)
      {
      case 1:
        return 0;
      case 3:
        return 1;
      case 4:
        return 2;
      case 5:
        return 3;
      case 6:
        return 4;
      case 7:
        return 5;
      case 0x16:
        return 6;
      default:
        return 7;
      }
    }

    // The pool index in party slot `index`, or -1 when the slot holds nothing
    // this follower should look at. The original's own guards differ by caller
    // -- FUN_002596c8 takes `!= 0 && < 0x100`, FUN_0025a500 takes `>= 0 &&
    // < 0x100`, FUN_002597d0 takes the unsigned `< 0x100` -- so each caller
    // applies its own test on top of this bounds check.
    std::int16_t partySlotEntry(const ActorEnvironment &environment, std::size_t index)
    {
      if (index >= environment.DAT_00343692_partySlots.size())
      {
        return -1;
      }
      return static_cast<std::int16_t>(environment.DAT_00343692_partySlots[index]);
    }

    bool poolIndexInRange(std::int16_t index)
    {
      return index >= 0 && static_cast<std::size_t>(index) < kEntitySlotCount;
    }

    orphen::ported::model::EntityBoneOverrides *overridesFor(const ActorEnvironment &environment,
                                                             std::size_t slot)
    {
      if (slot >= environment.boneOverrides.size())
      {
        return nullptr;
      }
      return &environment.boneOverrides[slot];
    }

    std::size_t boneForRole(const ActorEnvironment &environment, std::size_t slot, std::uint8_t role)
    {
      return environment.FUN_0020dd78_bone_for_role
                 ? environment.FUN_0020dd78_bone_for_role(slot, role)
                 : 0;
    }

    // FUN_00257f78: drop both look-at overrides.
    void FUN_00257f78_release_look(const ActorEnvironment &environment, std::size_t slot)
    {
      auto *state = overridesFor(environment, slot);
      if (state == nullptr)
      {
        return;
      }
      orphen::ported::model::FUN_0020d9c8_clear_bone_override(
          *state, boneForRole(environment, slot, kBustRole));
      orphen::ported::model::FUN_0020d9c8_clear_bone_override(
          *state, boneForRole(environment, slot, kHeadRole));
    }

    // FUN_00257f18: true unless *both* overrides are still running. Status 1 is
    // "no override", 0 is "running", 2 is "expired", and the original sums the
    // two and tests against zero -- so a pair that has just been installed is
    // the only case that answers false.
    bool FUN_00257f18_look_idle(const ActorEnvironment &environment, std::size_t slot)
    {
      const auto *state = overridesFor(environment, slot);
      if (state == nullptr)
      {
        return true;
      }
      const int bust = orphen::ported::model::FUN_0020d968_bone_override_status(
          *state, boneForRole(environment, slot, kBustRole));
      const int head = orphen::ported::model::FUN_0020d968_bone_override_status(
          *state, boneForRole(environment, slot, kHeadRole));
      return bust + head != 0;
    }

    using Pose = std::array<float, orphen::ported::model::kPoseFieldCount>;

    // FUN_0020da68: the bone's pose in the current animation. Its own miss path
    // writes zeroes with a unit scale, which is what an absent model gets here.
    Pose FUN_0020da68_sample(const ActorEnvironment &environment,
                             std::size_t slot,
                             std::size_t bone,
                             std::uint16_t animation)
    {
      if (environment.FUN_0020da68_sample_bone_pose)
      {
        if (const auto pose = environment.FUN_0020da68_sample_bone_pose(slot, bone, animation);
            pose.has_value())
        {
          return *pose;
        }
      }
      Pose pose{};
      pose[6] = 1.0f;
      return pose;
    }

    // FUN_00257c78: turn the head and bust toward a world point without moving
    // the body, and report what it had to do.
    //
    //   2  the point is close to straight ahead -- snap the whole body to it and
    //      drop the overrides
    //   1  within the 60 degree cone -- hold the twist on the two bones
    //   0  outside it -- ease whatever twist is installed back out
    //
    // Only the caller's `!= 0` matters: state 1 stops and turns in place when
    // this answers 0.
    int FUN_00257c78_look_at(OriginalEntity &entity,
                             const ActorEnvironment &environment,
                             std::size_t slot,
                             float targetX,
                             float targetZ)
    {
      const float dx = targetX - entity.positionX20;
      const float dz = targetZ - entity.positionZ24;
      const float toTarget = std::atan2(dz, dx);

      auto *state = overridesFor(environment, slot);
      const std::size_t bustBone = boneForRole(environment, slot, kBustRole);
      const std::size_t headBone = boneForRole(environment, slot, kHeadRole);

      Pose bustPose = FUN_0020da68_sample(environment, slot, bustBone, entity.animationA0);
      Pose headPose = FUN_0020da68_sample(environment, slot, headBone, entity.animationA0);

      const float delta = FUN_002166e8_delta(entity.facingRadians5c, toTarget);

      // Inside the dead zone *and* close by: no twist is worth it, so the body
      // takes the angle directly.
      if (delta < kfGpffff8a68_lookDeadHigh && delta > kfGpffff8a6c_lookDeadLow &&
          FUN_00216608_length(dx, dz) < 0.5f)
      {
        FUN_00257f78_release_look(environment, slot);
        entity.facingRadians5c = toTarget;
        return 2;
      }

      if (state == nullptr)
      {
        return 0;
      }

      if (delta < kfGpffff8a70_lookLimitHigh && delta > kfGpffff8a74_lookLimitLow)
      {
        bustPose[kYawField] = delta;
        headPose[kYawField] = delta * kfGpffff8a78_bustShare;
        orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, bustBone, bustPose,
                                                              kLookOverrideFrames);
        orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, headBone, headPose,
                                                              kLookOverrideFrames);
        return 1;
      }

      // Out of range: unwind. A bone whose override is *not still running*
      // (status 1 or 2, i.e. never set or already expired) but whose filtered
      // pose still carries a twist gets ten more frames of the animation's own
      // pose, so the head eases back instead of snapping when the override ends.
      //
      // The original reads bone 2's filtered yaw for *both* tests -- it passes
      // uVar2 twice where the second should be uVar3. Reproduced: the head only
      // unwinds when the bust still has a twist to unwind, which is always true
      // in practice because they are installed and expire together.
      const float twist = environment.FUN_0020d9d8_bone_yaw
                              ? environment.FUN_0020d9d8_bone_yaw(slot, bustBone)
                              : 0.0f;
      if (orphen::ported::model::FUN_0020d968_bone_override_status(*state, bustBone) != 0 &&
          twist != 0.0f)
      {
        orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, bustBone, bustPose,
                                                              kLookOverrideFrames);
      }
      if (orphen::ported::model::FUN_0020d968_bone_override_status(*state, headBone) != 0 &&
          twist != 0.0f)
      {
        orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, headBone, headPose,
                                                              kLookOverrideFrames);
      }
      return 0;
    }

    // FUN_00227798: the floor under one point, with no body band and no reject
    // mask, plus the primitive that answered -- the original leaves that in
    // DAT_00354d4e for its caller to read. `entityFlags04 & 2` is what selects
    // the single-sample path in FUN_00227070.
    struct PointGround
    {
      float height = 0.0f;
      std::int32_t primitive = -1;
      bool found = false;
    };

    PointGround FUN_00227798_ground_at(const ActorEnvironment &environment, float x, float z, float y)
    {
      PointGround result;
      if (!environment.terrainSurface)
      {
        return result;
      }
      const auto surface = environment.terrainSurface(x, z, y, 0.0f, 0.0f, 2u, 0u);
      if (!surface.has_value())
      {
        return result;
      }
      result.height = surface->height;
      result.primitive = surface->primitiveIndex;
      result.found = true;
      return result;
    }

    // FUN_0023a0a8: is a heading clear enough to walk? Two probes ahead, at
    // 0.3 and 0.8, must both be within 0.28 of the actor's own height, and
    // their terrain words ANDed together must still satisfy the actor's
    // required-terrain mask.
    bool FUN_0023a0a8_heading_is_clear(const OriginalEntity &entity,
                                       const ActorEnvironment &environment,
                                       float heading)
    {
      const float cosine = std::cos(heading);
      const float sine = std::sin(heading);
      const auto probe = [&](float reach) {
        return FUN_00227798_ground_at(environment, entity.positionX20 + reach * cosine,
                                      entity.positionZ24 + reach * sine, entity.positionY28);
      };
      const PointGround near = probe(kDAT_003525e0_nearProbe);
      std::uint32_t shared = 0;
      if (near.found && near.primitive >= 0 && environment.mapPrimitive)
      {
        const auto record = environment.mapPrimitive(near.primitive);
        shared = record.has_value() ? record->terrainFlags : 0;
      }
      const PointGround far = probe(kDAT_003525e4_farProbe);
      if (!near.found || !far.found)
      {
        shared = 0;
      }
      else if (far.primitive >= 0 && environment.mapPrimitive)
      {
        const auto record = environment.mapPrimitive(far.primitive);
        shared &= record.has_value() ? record->terrainFlags : 0;
      }
      if (entity.requiredTerrainMask78 != 0 && (shared & entity.requiredTerrainMask78) == 0)
      {
        return false;
      }
      return near.height - entity.positionY28 <= kDAT_003525e8_probeStepLimit &&
             far.height - entity.positionY28 <= kDAT_003525e8_probeStepLimit;
    }

    // FUN_0023a3a0: straight-line distance from an entity to the lead, in three
    // dimensions. Ghidra shows this as an empty function because the body ends
    // in a tail call it could not follow; the disassembly at 0x0023a3a0 is a
    // dot product against DAT_0058beb0 +0x20..+0x28 followed by `sqrt.s`.
    float FUN_0023a3a0_distance3_to_lead(const OriginalEntity &entity, const OriginalEntity &lead)
    {
      const float dx = entity.positionX20 - lead.positionX20;
      const float dz = entity.positionZ24 - lead.positionZ24;
      const float dy = entity.positionY28 - lead.positionY28;
      return std::sqrt(dx * dx + dz * dz + dy * dy);
    }

    // FUN_00259178: the shared "something is in the way, do I keep going?" test
    // that states 4 and 5 run before they commit a step.
    //
    //   nothing blocking          keep going
    //   terrain (0x202)           pick a heading either side of the current one
    //                             that FUN_0023a0a8 says is clear, hand to
    //                             state 2 and stop
    //   another actor (0x60)      one roll in twenty says stand still for 15
    //                             ticks; otherwise turn a quarter turn away
    //                             from it and hand to state 2
    //
    // Returns 1 to carry on, 0 when it has redirected the follower.
    int FUN_00259178_yield(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;

      if ((entity.collisionFlags0c & 0x202u) == 0)
      {
        if ((entity.collisionFlags0c & 0x60u) == 0)
        {
          return 1;
        }
        const std::int32_t blocker = entity.blockedBy64;
        if (blocker < 0 || static_cast<std::size_t>(blocker) >= kEntitySlotCount)
        {
          return 1;
        }
        OriginalEntity &other = pool.slot(static_cast<std::size_t>(blocker));

        // +0x02 bit 1 marks an actor worth waiting for rather than walking
        // round. FUN_00216868 % 1000 > 800 is the one-in-five roll.
        if ((other.descriptorFlags02 & 2u) != 0 && environment.random &&
            (environment.random() % 1000u) > 800u &&
            (other.state60 != 1 || other.fadeRamp62 == 0))
        {
          entity.fadeRamp62 = 0x0F;
          FUN_00225bf0_set_state_and_animation(entity, 1, 0);
          return 0;
        }

        const float toBlocker = FUN_0023a4b8_angle_between(entity, other);
        const float delta = FUN_002166e8_delta(entity.facingRadians5c, toBlocker);
        entity.desiredFacing1a8 = delta < 0.0f
                                      ? entity.facingRadians5c + kDAT_00352a20_yieldTurn
                                      : entity.facingRadians5c - kDAT_00352a24_yieldTurn;
      }
      else
      {
        // Wedged on geometry: try a wide turn one way, then the other, and
        // settle for a near-reversal if neither is clear.
        const std::uint32_t roll = environment.random ? environment.random() : 0;
        const float spread =
            (static_cast<float>(static_cast<int>(roll % 0x1E) + 0x4B) * kDAT_00352a1c_twoPi) / 360.0f;
        entity.desiredFacing1a8 = entity.facingRadians5c + spread;
        if (!FUN_0023a0a8_heading_is_clear(entity, environment, entity.desiredFacing1a8))
        {
          entity.desiredFacing1a8 = entity.facingRadians5c - spread;
          if (!FUN_0023a0a8_heading_is_clear(entity, environment, entity.desiredFacing1a8))
          {
            entity.desiredFacing1a8 =
                entity.facingRadians5c +
                (static_cast<float>(static_cast<int>(roll % 0x1E) + 0xA5) * kDAT_00352a1c_twoPi) /
                    360.0f;
          }
        }
      }

      entity.fadeRamp62 = 5;
      entity.state60 = 2;
      entity.animationA0 = 4;
      entity.desiredHeight1ac = static_cast<float>(entity.followSpeedBase1a2);
      return 0;
    }

    // FUN_00259378: ask the navigation graph for a way to +0x1B0.
    //
    // Three steps. Sanity-check the target against the floor under it and fall
    // back to the lead's own position when they disagree by more than half a
    // unit; flood the graph out from the target into this follower's lane; then
    // take one step down the gradient from where the follower is standing.
    //
    //   a step exists   state 4, animation 0x0E, +0x62 = 1, speed x 1.2
    //   none, and the target is far enough away to be worth a detour
    //                   FUN_00258b80, which in the retail build is a loop whose
    //                   result is discarded and a hard `return -1` -- so this
    //                   always lands on state 6. State 5 (FUN_0025a0c8, walk to
    //                   a chosen breadcrumb) is dead code because of it, and is
    //                   the one piece of this subsystem deliberately not ported.
    //   none, and it is close                nothing; the caller stays put
    //
    // Returns 1 when it has put the follower on a path.
    int FUN_00259378_pathfind(OriginalEntity &entity,
                              const ActorEnvironment &environment,
                              std::size_t slot)
    {
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);

      const PointGround under = FUN_00227798_ground_at(
          environment, entity.followTargetX1b0, entity.followTargetZ1b4, entity.followTargetY1b8);
      float mismatch = under.height - entity.followTargetY1b8;
      if (mismatch < 0.0f)
      {
        mismatch = -mismatch;
      }
      if (0.5f < mismatch)
      {
        entity.followTargetX1b0 = lead.positionX20;
        entity.followTargetZ1b4 = lead.positionZ24;
      }

      const float toTarget = FUN_00216608_length(entity.positionX20 - entity.followTargetX1b0,
                                                 entity.positionZ24 - entity.followTargetZ1b4);

      if (environment.followerNavmesh == nullptr || environment.psm2Map == nullptr ||
          !environment.FUN_00227798_probe)
      {
        return 0;
      }

      environment.followerNavmesh->FUN_002584b0_flood(
          *environment.psm2Map, environment.FUN_00227798_probe, entity.followTargetX1b0,
          entity.followTargetZ1b4, entity.followTargetY1b8, entity.followSpot1c6);

      const bool skipCornerCut = environment.DAT_00355030_skipCornerCut != nullptr &&
                                 *environment.DAT_00355030_skipCornerCut;
      if (environment.DAT_00355030_skipCornerCut != nullptr)
      {
        // FUN_00258c70:102-105 consumes the flag on the way through.
        *environment.DAT_00355030_skipCornerCut = false;
      }
      const NavRecord *step = environment.followerNavmesh->FUN_00258c70_step(
          *environment.psm2Map, environment.FUN_00227798_probe, &entity, environment.entityPool,
          entity.positionX20, entity.positionZ24, entity.positionY28, entity.followSpot1c6,
          skipCornerCut);
      entity.pathNode198 = step;

      if (step != nullptr)
      {
        FUN_00225bf0_set_state_and_animation(entity, 4, 0x0E);
        entity.fadeRamp62 = 1;
        entity.desiredHeight1ac =
            static_cast<float>(entity.followSpeedBase1a2) * kDAT_00352a28_pathSpeedScale;
        return 1;
      }

      if (entity.followBlocked1ca != 0)
      {
        return 0;
      }
      if (0.5f < mismatch || 2.0f < toTarget)
      {
        // FUN_00258b80 always answers -1, so this is always the state 6 leg.
        entity.followPathNode1c0 = -1;
        FUN_00225bf0_set_state_and_animation(entity, 6, 0);
      }
      (void)slot;
      return 0;
    }

    // FUN_00259ec0, state 4: walk the graph.
    //
    // +0x62 is the "have a step" latch. With it clear the state asks
    // FUN_00258c70 for the next cell and gives up to state 1 when there is
    // none; with it set it steers at that cell's centre until it arrives, the
    // lead moves more than 2 units from the target the path was built for, or
    // the floor under the cell turns out to be more than a unit away.
    void FUN_00259ec0_state4_walk_path(OriginalEntity &entity,
                                       const ActorEnvironment &environment,
                                       std::size_t slot)
    {
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);
      const auto abandon = [&entity, &lead]() {
        entity.pathNode198 = nullptr;
        entity.fadeRamp62 = 0;
        entity.followPathNode1c0 = -1;
        entity.state60 = 1;
        entity.facingRadians5c = FUN_0023a480_angle_to_lead(entity, lead);
      };

      if (environment.followerNavmesh == nullptr || environment.psm2Map == nullptr ||
          !environment.FUN_00227798_probe)
      {
        abandon();
        return;
      }

      if (entity.fadeRamp62 == 0)
      {
        const NavRecord *step = environment.followerNavmesh->FUN_00258c70_step(
            *environment.psm2Map, environment.FUN_00227798_probe, &entity, environment.entityPool,
            entity.positionX20, entity.positionZ24, entity.positionY28, entity.followSpot1c6,
            false);
        entity.pathNode198 = step;
        if (step == nullptr)
        {
          entity.state60 = 1;
          return;
        }
        if ((static_cast<std::uint16_t>(entity.groundPrimitive0a) & 0x3FFFu) ==
            (static_cast<std::uint16_t>(step->packedPrimitive) & 0x3FFFu))
        {
          abandon();
          return;
        }
        entity.fadeRamp62 = 1;
      }

      // fGpffff8afc: inside 1.2 units of the lead there is nothing left to
      // route around, so the walk ends and idle takes over.
      if (FUN_0023a3a0_distance3_to_lead(entity, lead) < kfGpffff8afc_pathArrivalRadius)
      {
        return;
      }

      const float driftX = lead.positionX20 - entity.followTargetX1b0;
      const float driftZ = lead.positionZ24 - entity.followTargetZ1b4;
      const float driftY = lead.positionY28 - entity.followTargetY1b8;
      if (2.0f < std::sqrt(driftX * driftX + driftZ * driftZ + driftY * driftY))
      {
        // The lead has walked away from the target this path was built for.
        entity.fadeRamp62 = 0;
        entity.state60 = 1;
        return;
      }

      if (FUN_00259178_yield(entity, environment) == 0)
      {
        return;
      }
      const NavRecord *step = entity.pathNode198;
      if (step == nullptr)
      {
        return;
      }
      const std::size_t primitive =
          static_cast<std::size_t>(step->packedPrimitive) & 0x3FFFu;
      if (primitive >= environment.psm2Map->DAT_003556ac_dRecords80.size())
      {
        return;
      }
      const auto &center = environment.psm2Map->DAT_003556ac_dRecords80[primitive].center;

      const float toCenterX = center.x - entity.positionX20;
      const float toCenterZ = center.y - entity.positionZ24;
      const float heading = std::atan2(toCenterZ, toCenterX);
      const float step2d = (entity.desiredHeight1ac * static_cast<float>(environment.frameTicks)) /
                           kfGpffff8b00_pathSpeedDivisor;
      if (FUN_00216608_length(toCenterX, toCenterZ) < step2d)
      {
        entity.fadeRamp62 = 0; // arrived: ask for the next cell next frame
      }
      entity.desiredDeltaX30 += step2d * std::cos(heading);
      entity.facingRadians5c = heading;
      entity.desiredDeltaZ34 += step2d * std::sin(heading);

      if (std::abs(entity.groundHeight4c - center.z) <= 1.0f)
      {
        return;
      }
      abandon();
      (void)slot;
    }

    void FUN_0025a500_state8_follow(OriginalEntity &entity,
                                    const ActorEnvironment &environment,
                                    std::size_t slot,
                                    ActorTrace &trace);
    void FUN_002597d0_state1_idle(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  std::size_t slot,
                                  ActorTrace &trace);

    // FUN_00259520: where this follower should be standing, and whether it is
    // far enough off it to be worth walking.
    //
    //   1  +0x1B0..+0x1B8 now holds the formation spot beside the lead
    //   2  the navmesh took over (FUN_00259378 found a path)
    //   0  neither -- stay put
    int FUN_00259520_formation_target(OriginalEntity &entity,
                                      const ActorEnvironment &environment,
                                      ActorTrace &trace)
    {
      (void)trace;
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);

      const float ring = FUN_00216690_wrap(lead.facingRadians5c + entity.followFormationAngle1bc);
      float goalX = lead.positionX20 + std::cos(ring);
      float goalZ = lead.positionZ24 + std::sin(ring);
      const float goalY = lead.groundHeight4c;

      // The height is the *lead's* +0x4C, not the sample's: the query is run
      // for its DAT_00354d4e, and a spot with no floor under it collapses back
      // onto the lead.
      const PointGround ground = FUN_00227798_ground_at(environment, goalX, goalZ, goalY);
      if (!ground.found || ground.primitive < 0)
      {
        goalX = lead.positionX20;
        goalZ = lead.positionZ24;
      }

      const float flat = FUN_00216608_length(goalX - entity.positionX20, goalZ - entity.positionZ24);
      const float drop = goalY - entity.positionY28;

      if (drop < kDAT_00352a2c_formationDropFloor || kDAT_00352a30_formationDropCeiling < drop ||
          entity.followBlocked1ca != 0)
      {
        // The formation spot is unreachable in a straight line, so aim at the
        // centre of the primitive the *lead* is standing on and let the graph
        // work out the way there. Returning 2 rather than 1 is how the caller
        // knows the follower has been put on a path instead of sent at a spot.
        if (environment.mapPrimitive)
        {
          const auto leadCell = environment.mapPrimitive(lead.groundPrimitive0a);
          if (leadCell.has_value())
          {
            entity.followTargetX1b0 = leadCell->centerX;
            entity.followTargetZ1b4 = leadCell->centerZ;
            entity.followTargetY1b8 = leadCell->centerY;
          }
        }
        return FUN_00259378_pathfind(entity, environment, environment.currentSlot) != 0 ? 2 : 0;
      }

      if (kDAT_00352a34_formationDeadZone < flat &&
          1.5f < FUN_0023a418_distance_to_lead(entity, lead))
      {
        entity.followTargetX1b0 = goalX;
        entity.followTargetZ1b4 = goalZ;
        entity.followTargetY1b8 = goalY;
        return 1;
      }
      return 0;
    }

    // FUN_002596c8, state 0: the one-shot init opcode 0xAC's `state = 0` runs
    // into on the follower's first tick.
    void FUN_002596c8_state0_init(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  std::size_t slot)
    {
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);

      FUN_00225bf0_set_state_and_animation(entity, 2, 4);
      entity.followBumpCount1c8 = 0;
      entity.fadeRamp62 = 2;
      entity.desiredHeight1ac = static_cast<float>(entity.followSpeedBase1a2);
      entity.desiredFacing1a8 = FUN_0023a480_angle_to_lead(entity, lead);

      const PointGround ground = FUN_00227798_ground_at(
          environment, entity.positionX20, entity.positionZ24,
          entity.positionY28 + kDAT_00352a38_initSampleLift);
      entity.groundPrimitive0a = static_cast<std::int16_t>(ground.primitive);

      entity.rejectTerrainMask74 |= 0x0D000000u;
      entity.maxStepDown7c = lead.maxStepDown7c;

      // The first follower walks 150 degrees round from the lead's facing; each
      // later one takes the negation of the first one it finds, so a second
      // follower ends up on the opposite shoulder.
      entity.followFormationAngle1bc = kDAT_00352a3c_firstFormationAngle;
      for (std::size_t index = 0; index < environment.DAT_00343692_partySlots.size(); ++index)
      {
        const std::int16_t other = partySlotEntry(environment, index);
        if (other == 0 || !poolIndexInRange(other))
        {
          continue;
        }
        if (static_cast<std::size_t>(other) == slot)
        {
          continue;
        }
        const OriginalEntity &peer = environment.entityPool->slot(static_cast<std::size_t>(other));
        if (static_cast<std::int16_t>(peer.state60) > 0)
        {
          entity.followFormationAngle1bc = -peer.followFormationAngle1bc;
          return;
        }
      }
    }

    // FUN_00259d00, state 2: turn to +0x1A8, then push forward on it until the
    // timer runs out. Three walls in a row gives up to state 6.
    void FUN_00259d00_state2_turn_and_push(OriginalEntity &entity,
                                           const ActorEnvironment &environment)
    {
      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.desiredFacing1a8,
          static_cast<float>(environment.frameTicks) * kDAT_00352a60_state2TurnRate * kTickToFrame);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      const std::int16_t remaining = static_cast<std::int16_t>(entity.fadeRamp62 - 1);
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (remaining <= 0)
      {
        entity.fadeRamp62 = 0;
        entity.state60 = 1;
        return;
      }

      if ((entity.collisionFlags0c & 0x202u) != 0)
      {
        entity.followBumpCount1c8 = static_cast<std::int8_t>(entity.followBumpCount1c8 + 1);
        if (entity.followBumpCount1c8 < 3)
        {
          entity.fadeRamp62 = 5;
          entity.desiredFacing1a8 += kDAT_00352a64_state2BumpTurn;
          return;
        }
        entity.followBumpCount1c8 = 0;
        FUN_00225bf0_set_state_and_animation(entity, 6, 0);
        return;
      }

      const float speed = (entity.desiredHeight1ac * static_cast<float>(environment.frameTicks)) /
                          kDAT_00352a68_state2SpeedDivisor;
      entity.desiredDeltaX30 += speed * std::cos(entity.facingRadians5c);
      entity.desiredDeltaZ34 += speed * std::sin(entity.facingRadians5c);
    }

    // FUN_00259e50, state 3: turn in place toward +0x1A8 and go back to idle
    // once there. +0x1AC is a *turn rate* here, not a speed -- state 1 sets it
    // from the angle it has to cover.
    void FUN_00259e50_state3_turn(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  std::size_t slot)
    {
      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.desiredFacing1a8,
          entity.desiredHeight1ac * static_cast<float>(environment.frameTicks));
      if (step == 0.0f)
      {
        FUN_00257f78_release_look(environment, slot);
        entity.state60 = 1;
        return;
      }
      entity.facingRadians5c += step;
    }

    // FUN_0025a450, state 7: walk blind on the current facing for +0x62 ticks.
    // This is the "the lead walked into me" shove -- state 1 aims the facing
    // sideways and hands over here.
    void FUN_0025a450_state7_walk_blind(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const std::int16_t remaining = static_cast<std::int16_t>(entity.fadeRamp62 - 1);
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (remaining < 1)
      {
        entity.state60 = 1;
        return;
      }
      if ((entity.collisionFlags0c & 0x62u) != 0)
      {
        entity.fadeRamp62 = 1;
        return;
      }
      const float speed = (entity.desiredHeight1ac * static_cast<float>(environment.frameTicks)) /
                          kDAT_00352a78_state7SpeedDivisor;
      entity.desiredDeltaX30 += speed * std::cos(entity.facingRadians5c);
      entity.desiredDeltaZ34 += speed * std::sin(entity.facingRadians5c);
    }

    // FUN_0025aa48, state 9: step out of a crowd. Runs on the facing state 1 or
    // state 8 scattered, and quits early once it is clear of the lead.
    void FUN_0025aa48_state9_sidestep(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);
      const std::int16_t remaining = static_cast<std::int16_t>(entity.fadeRamp62 - 1);
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);

      if (remaining > 0 && (entity.collisionFlags0c & 0x62u) == 0)
      {
        const float delta =
            FUN_002166e8_delta(entity.facingRadians5c, FUN_0023a480_angle_to_lead(entity, lead));
        const bool stillCrowded = delta > kfGpffff8b3c_sidestepLow &&
                                  delta < kfGpffff8b40_sidestepHigh &&
                                  FUN_0023a418_distance_to_lead(entity, lead) < 1.0f;
        if (!stillCrowded)
        {
          const float speed = (entity.desiredHeight1ac * static_cast<float>(environment.frameTicks)) /
                              kfGpffff8b44_state9SpeedDivisor;
          entity.desiredDeltaX30 += speed * std::cos(entity.facingRadians5c);
          entity.desiredDeltaZ34 += speed * std::sin(entity.facingRadians5c);
          return;
        }
      }
      entity.state60 = 1;
    }

    // 0x0025AB48, state 10: hold the stagger until the floor is under us again,
    // then back to idle. +0x06 bit 0 is the grounded bit the physics publishes.
    void FUN_0025ab48_state10_wait_for_floor(OriginalEntity &entity)
    {
      if ((entity.flags06 & 1u) != 0)
      {
        FUN_00225bf0_set_state_and_animation(entity, 1, 0);
      }
    }

    // FUN_0025a298, state 6: the bail-out a follower reaches when pathing has
    // given up -- three walls in a row in state 2, or a wedge in state 8.
    //
    // It walks the lead's own breadcrumb trail backwards looking for somewhere
    // to reappear: off camera, at least a unit from the lead, and on a primitive
    // this follower's terrain masks accept. Found, it teleports onto that
    // primitive's centre and goes back to idle; not found, it runs state 1's
    // body with +0x1CA held so state 1 skips its idle look and asks the navmesh
    // instead, and comes back here if that asks for state 5.
    //
    // Without this a follower that reached state 6 stopped for the rest of the
    // scene, because state 6 is the only state with no other way out.
    void FUN_0025a298_state6_recover(OriginalEntity &entity,
                                     const ActorEnvironment &environment,
                                     std::size_t slot,
                                     ActorTrace &trace)
    {
      const OriginalEntity &lead = environment.entityPool->slot(kLeadSlot);

      const auto onCamera = [&environment](float x, float z) {
        return environment.FUN_0023ae60_on_camera_axis
                   ? environment.FUN_0023ae60_on_camera_axis(x, z)
                   : false;
      };

      // +0x0C bit 0x1000 is the "do not teleport me" physics result; on camera
      // is the other refusal, and the original tests this follower's own
      // position for it before it even starts scanning.
      if (!onCamera(entity.positionX20, entity.positionZ24) &&
          (entity.collisionFlags0c & 0x1000u) == 0 && environment.mapPrimitive &&
          !environment.DAT_00355704_leadTrail.empty())
      {
        const auto &trail = environment.DAT_00355704_leadTrail;
        const std::size_t capacity = trail.size();
        std::int32_t cursor = static_cast<std::int32_t>(environment.DAT_00355708_leadTrailCursor);

        // 511 steps backwards from the newest entry, wrapping at the bottom.
        for (std::size_t step = 0; step < capacity - 1; ++step)
        {
          --cursor;
          if (cursor < 0)
          {
            cursor = static_cast<std::int32_t>(capacity) - 1;
          }
          const auto &point = trail[static_cast<std::size_t>(cursor)];
          // The original reads the primitive halfword whether or not the entry
          // was ever written -- FUN_0022a418 seeds the positions and leaves that
          // field stale. The port seeds it to -1 and skips those instead.
          if (point.primitive < 0)
          {
            continue;
          }
          if (onCamera(point.x, point.z))
          {
            continue;
          }
          if (FUN_00216608_length(point.x - lead.positionX20, point.z - lead.positionZ24) <= 1.0f)
          {
            continue;
          }
          const auto primitive = environment.mapPrimitive(point.primitive);
          if (!primitive.has_value())
          {
            continue;
          }
          // 0x5000000 is folded into the reject mask here and nowhere else.
          const std::uint32_t flags = primitive->terrainFlags;
          if (((entity.rejectTerrainMask74 | 0x05000000u) & flags) != 0)
          {
            continue;
          }
          if (entity.requiredTerrainMask78 != 0 && (entity.requiredTerrainMask78 & flags) == 0)
          {
            continue;
          }

          entity.positionX20 = primitive->centerX;
          entity.positionZ24 = primitive->centerZ;
          const PointGround ground = FUN_00227798_ground_at(
              environment, primitive->centerX, primitive->centerZ, primitive->centerY + 0.5f);
          entity.groundHeight4c = ground.height;
          entity.state60 = 1;
          return;
        }
      }

      entity.followBlocked1ca = 1;
      FUN_002597d0_state1_idle(entity, environment, slot, trace);
      entity.followBlocked1ca = 0;
      if (entity.state60 == 5)
      {
        entity.animationA0 = 0;
        entity.state60 = 6;
      }
    }

    // FUN_002597d0, state 1: idle. Shared with the lead's own state table, so
    // every follower-only branch is behind a `type == 0x37` test.
    void FUN_002597d0_state1_idle(OriginalEntity &entity,
                                  const ActorEnvironment &environment,
                                  std::size_t slot,
                                  ActorTrace &trace)
    {
      EntityPool &pool = *environment.entityPool;
      const OriginalEntity &lead = pool.slot(kLeadSlot);
      const float toLead = FUN_0023a480_angle_to_lead(entity, lead);

      // The original threads three gotos through this. They all land on the
      // same two-line tail -- "if the animation is not the idle one, make it the
      // idle one and re-arm the look timer" -- and nothing between them touches
      // the animation, so `reachedDecision` stands in for reaching LAB_00259B0C
      // with that tail already skipped.
      bool reachedDecision = false;

      if (entity.followBlocked1ca == 0 &&
          (lead.desiredDeltaX30 != 0.0f || lead.desiredDeltaZ34 != 0.0f))
      {
        // The lead is moving. If it is walking *into* us -- coming from within
        // 45 degrees of straight on, and within one unit -- step aside rather
        // than be pushed.
        const float shove = FUN_002166e8_delta(
            toLead,
            std::atan2(lead.desiredDeltaZ34, lead.desiredDeltaX30) + kDAT_00352a40_pi);
        if (kDAT_00352a44_pushLow < shove && shove < kDAT_00352a48_pushHigh &&
            FUN_0023a418_distance_to_lead(entity, lead) < 1.0f)
        {
          if (static_cast<float>(entity.followSpeedBase1a2) == 0.0f)
          {
            entity.followSpeedBase1a2 = 180;
          }
          entity.desiredHeight1ac =
              static_cast<float>(entity.followSpeedBase1a2) * kDAT_00352a4c_pushSpeedScale;
          const float away = shove >= 0.0f ? kDAT_00352a50_pushLeft : kDAT_00352a54_pushRight;
          entity.facingRadians5c = FUN_00216690_wrap(toLead + away);
          entity.fadeRamp62 = static_cast<std::uint16_t>(
              (environment.random ? environment.random() & 3u : 0u) + 8u);
          if (entity.typeId00 == 0x37)
          {
            entity.state60 = 7;
          }
          else
          {
            entity.state60 = 2;
            entity.followTargetX1b0 = entity.positionX20;
            entity.followTargetZ1b4 = entity.positionZ24;
            entity.hitFlash1c2 = 0xF00;
          }
          entity.animationA0 = 0x0E;
          FUN_00257f78_release_look(environment, slot);
          return;
        }
      }

      if (entity.followBlocked1ca == 0 && entity.animationA0 == 0)
      {
        // Standing still with the idle animation on. Watch the lead, unless the
        // look timer is still running down from the last time we did.
        reachedDecision = true;
        if (static_cast<std::int16_t>(entity.hitFlash1c2) < 1)
        {
          if (FUN_00257f18_look_idle(environment, slot) &&
              FUN_00257c78_look_at(entity, environment, slot, lead.positionX20,
                                   lead.positionZ24) == 0)
          {
            // Too far round to twist to. Unwind both bones over ten frames and
            // turn the whole body instead: +0x1AC becomes the turn rate state 3
            // runs on, scaled from how far there is to go.
            const float delta = FUN_002166e8_delta(entity.facingRadians5c, toLead);
            entity.desiredFacing1a8 = toLead;
            entity.desiredHeight1ac = (std::fabs(delta) / 10.0f) * kTickToFrame;
            entity.state60 = entity.typeId00 == 0x37 ? 3 : 4;

            if (auto *state = overridesFor(environment, slot); state != nullptr)
            {
              const std::size_t bustBone = boneForRole(environment, slot, kBustRole);
              const std::size_t headBone = boneForRole(environment, slot, kHeadRole);
              Pose bustPose = FUN_0020da68_sample(environment, slot, bustBone, entity.animationA0);
              Pose headPose = FUN_0020da68_sample(environment, slot, headBone, entity.animationA0);
              bustPose[kYawField] = 0.0f;
              headPose[kYawField] = 0.0f;
              orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, headBone, headPose,
                                                                    kLookOverrideFrames);
              orphen::ported::model::FUN_0020d8c0_set_bone_override(*state, bustBone, bustPose,
                                                                    kLookOverrideFrames);
            }
            return;
          }
        }
        else
        {
          entity.hitFlash1c2 = static_cast<std::uint16_t>(
              entity.hitFlash1c2 - static_cast<std::uint16_t>(environment.frameTicks));
        }
      }

      // Coming out of a walk: drop back to the idle animation and re-arm the
      // look timer.
      if (!reachedDecision && entity.animationA0 != 0)
      {
        entity.animationA0 = 0;
        entity.hitFlash1c2 = 0x280;
      }

      if (entity.typeId00 != 0x37)
      {
        return;
      }

      if (static_cast<std::int16_t>(entity.fadeRamp62) >= 1)
      {
        entity.fadeRamp62 = static_cast<std::uint16_t>(entity.fadeRamp62 - 1);
        return;
      }

      if (FUN_00259520_formation_target(entity, environment, trace) == 1)
      {
        entity.followStuckCount1c9 = 0;
        entity.state60 = 8;
        entity.animationA0 = 0x0E;
        entity.desiredHeight1ac =
            static_cast<float>(entity.followSpeedBase1a2) * kDAT_00352a58_followSpeedScale;
        FUN_0025a500_state8_follow(entity, environment, slot, trace);
      }
      if (entity.state60 != 1)
      {
        FUN_00257f78_release_look(environment, slot);
      }

      // Standing inside another party member. Scatter the facing by up to 45
      // degrees either way and sidestep out.
      const int ownSlot = FUN_002298d0_party_slot_for_type(entity.partyOriginalType1a0);
      for (std::size_t index = 0; index < environment.DAT_00343692_partySlots.size(); ++index)
      {
        if (index == 6 || static_cast<int>(index) == ownSlot)
        {
          continue;
        }
        const std::int16_t other = partySlotEntry(environment, index);
        if (!poolIndexInRange(other))
        {
          continue;
        }
        const OriginalEntity &peer = pool.slot(static_cast<std::size_t>(other));
        if (peer.typeId00 <= 0)
        {
          continue;
        }
        const float gap = FUN_00216608_length(entity.positionX20 - peer.positionX20,
                                              entity.positionZ24 - peer.positionZ24);
        if (gap >= entity.radius54 + peer.radius54)
        {
          continue;
        }
        const std::int32_t roll =
            environment.random ? static_cast<std::int32_t>(environment.random()) : 0;
        entity.facingRadians5c = FUN_00216690_wrap(
            entity.facingRadians5c +
            (static_cast<float>(roll % 0x5A - 0x2D) * kDAT_00352a5c_twoPi) / 360.0f);
        entity.fadeRamp62 = 0x0F;
        entity.state60 = 9;
        entity.desiredHeight1ac = static_cast<float>(entity.followSpeedBase1a2);
        entity.animationA0 = 4;
        FUN_00257f78_release_look(environment, slot);
        return;
      }
    }

    // FUN_0025a500, state 8: the follow walk.
    void FUN_0025a500_state8_follow(OriginalEntity &entity,
                                    const ActorEnvironment &environment,
                                    std::size_t slot,
                                    ActorTrace &trace)
    {
      EntityPool &pool = *environment.entityPool;
      const OriginalEntity &lead = pool.slot(kLeadSlot);
      const float ticks = static_cast<float>(environment.frameTicks);

      float toTargetX = entity.followTargetX1b0 - entity.positionX20;
      float toTargetZ = entity.followTargetZ1b4 - entity.positionZ24;
      float step = (entity.desiredHeight1ac * ticks) / kDAT_00352a7c_state8SpeedDivisor;

      bool retarget = FUN_00216608_length(toTargetX, toTargetZ) < step;
      bool clearStuckCount = false;
      bool walk = false;

      // The original's `while (true)` around the retarget: arriving at the spot
      // picks a new one and re-tests, rather than costing a frame. The cap is
      // the port's -- the loop's only way back to the top needs the collision
      // flags to still be set with the *lead* as the blocker, and every path
      // out of the retarget either returns or moves the target, so it is a
      // guard against a state combination rather than an expected exit.
      for (int pass = 0; pass < 8; ++pass)
      {
        if (!retarget)
        {
          const std::int16_t timer = static_cast<std::int16_t>(entity.fadeRamp62);
          if (timer != 0)
          {
            const std::int16_t remaining = static_cast<std::int16_t>(timer - 1);
            entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
            if (remaining > 0)
            {
              return;
            }
            entity.animationA0 = 0x0E;
          }

          if ((entity.collisionFlags0c & 0x262u) == 0)
          {
            clearStuckCount = true;
            walk = true;
            break;
          }

          entity.followStuckCount1c9 = static_cast<std::int8_t>(entity.followStuckCount1c9 + 1);
          if (entity.followStuckCount1c9 < 5)
          {
            walk = true;
            break;
          }

          if ((entity.collisionFlags0c & 0x60u) == 0)
          {
            // Wedged against geometry rather than an actor: aim straight at the
            // lead and let the graph take it from there. DAT_00355030 is raised
            // across the call so this first step is the plain neighbour, not a
            // corner cut past the thing that is in the way.
            entity.followTargetX1b0 = lead.positionX20;
            entity.followTargetZ1b4 = lead.positionZ24;
            entity.followTargetY1b8 = lead.positionY28;
            if (environment.DAT_00355030_skipCornerCut != nullptr)
            {
              *environment.DAT_00355030_skipCornerCut = true;
            }
            const int routed = FUN_00259378_pathfind(entity, environment, slot);
            if (environment.DAT_00355030_skipCornerCut != nullptr)
            {
              *environment.DAT_00355030_skipCornerCut = false;
            }
            if (routed != 0)
            {
              return;
            }
            FUN_00225bf0_set_state_and_animation(entity, 6, 0);
            return;
          }

          const std::int32_t blocker = entity.blockedBy64;
          if (blocker < 0 || static_cast<std::size_t>(blocker) >= kEntitySlotCount)
          {
            walk = true;
            break;
          }
          if (static_cast<std::size_t>(blocker) != kLeadSlot)
          {
            const OriginalEntity &other = pool.slot(static_cast<std::size_t>(blocker));
            // Queued behind another follower that is walking and not already
            // waiting: stand still for eight ticks rather than shoulder past.
            if (other.typeId00 == 0x37 && other.state60 == 8 && other.fadeRamp62 == 0)
            {
              entity.fadeRamp62 = 8;
              entity.animationA0 = 0;
              return;
            }
            const float delta = FUN_002166e8_delta(entity.facingRadians5c,
                                                   FUN_0023a4b8_angle_between(entity, other));
            const std::int32_t roll =
                environment.random ? static_cast<std::int32_t>(environment.random()) : 0;
            const float away = delta >= 0.0f ? kDAT_00352a84_blockerRight : kDAT_00352a88_blockerLeft;
            entity.desiredFacing1a8 = FUN_00216690_wrap(
                entity.facingRadians5c + away +
                (static_cast<float>(roll % 0x14 - 10) * kDAT_00352a80_blockerScatter) / 360.0f);
            entity.state60 = 2;
            entity.fadeRamp62 = 8;
            return;
          }
          // Blocked by the lead itself: fall through and pick a fresh spot.
        }

        retarget = false;
        const int found = FUN_00259520_formation_target(entity, environment, trace);
        if (found == 0)
        {
          if (FUN_0023a418_distance_to_lead(entity, lead) < 1.5f)
          {
            entity.fadeRamp62 = 0;
            entity.state60 = 1;
            return;
          }
        }
        else if (found == 2)
        {
          return;
        }
        toTargetX = entity.followTargetX1b0 - entity.positionX20;
        toTargetZ = entity.followTargetZ1b4 - entity.positionZ24;
      }

      if (!walk)
      {
        return;
      }
      if (clearStuckCount)
      {
        entity.followStuckCount1c9 = 0;
      }

      const float goal = std::atan2(toTargetZ, toTargetX);

      // Anything in the party standing in the way within one unit and 45
      // degrees of dead ahead. The lead sends us back to idle; another follower
      // that is not already sidestepping or idle sends us into state 9.
      for (std::size_t index = 0; index < environment.DAT_00343692_partySlots.size(); ++index)
      {
        const std::int16_t other = partySlotEntry(environment, index);
        if (!poolIndexInRange(other) || static_cast<std::size_t>(other) == slot)
        {
          continue;
        }
        const OriginalEntity &peer = pool.slot(static_cast<std::size_t>(other));
        const float delta =
            FUN_002166e8_delta(entity.facingRadians5c, FUN_0023a4b8_angle_between(entity, peer));
        if (delta <= kDAT_00352a8c_aheadLow || delta >= kDAT_00352a90_aheadHigh ||
            FUN_0023a4e8_distance_between(entity, peer) >= 1.0f)
        {
          continue;
        }
        if (static_cast<std::size_t>(other) == kLeadSlot)
        {
          entity.fadeRamp62 = 0;
          entity.state60 = 1;
          return;
        }
        if (peer.state60 != 9 && peer.state60 != 1)
        {
          const std::int32_t roll =
              environment.random ? static_cast<std::int32_t>(environment.random()) : 0;
          entity.state60 = 9;
          entity.fadeRamp62 = 8;
          entity.facingRadians5c =
              FUN_0023a480_angle_to_lead(entity, lead) +
              (static_cast<float>(roll % 0x32 + 0x9B) * kDAT_00352a94_sidestepScatter) / 360.0f;
          return;
        }
        break;
      }

      float speedScale = kDAT_00352aa4_runSpeedScale;
      if (2.0f < FUN_0023a418_distance_to_lead(entity, lead))
      {
        // Falling behind: a flat bonus on the step, and the spot moves to
        // wherever the lead is *now*. The heading is deliberately not
        // recomputed -- the new spot only steers from next frame.
        step += kDAT_00352a98_catchUpBonus;
        const float ring = FUN_00216690_wrap(lead.facingRadians5c + entity.followFormationAngle1bc);
        entity.followTargetX1b0 = lead.positionX20 + std::cos(ring);
        entity.followTargetZ1b4 = lead.positionZ24 + std::sin(ring);
      }
      else if (environment.DAT_003555e8_stickMagnitude != 0.0f)
      {
        // Close enough: match the lead's gait instead of chasing a distance.
        // The stick, not the lead's speed, is what picks walk from run.
        if (environment.DAT_003555e8_stickMagnitude < 100.0f)
        {
          step = ticks * kDAT_00352a9c_walkStep * kTickToFrame;
          entity.animationA0 = 4;
          entity.desiredHeight1ac = static_cast<float>(entity.followSpeedBase1a2);
        }
        else
        {
          step = ticks * kDAT_00352aa0_runStep * kTickToFrame;
          entity.animationA0 = 0x0E;
          entity.desiredHeight1ac = static_cast<float>(entity.followSpeedBase1a2) * speedScale;
        }
      }

      float facing = entity.facingRadians5c;
      facing += FUN_0023a320_approach_angle(facing, goal, ticks * kDAT_00352aa8_turnRate);
      entity.desiredDeltaX30 += step * std::cos(facing);
      entity.facingRadians5c = facing;
      entity.desiredDeltaZ34 += step * std::sin(facing);
    }
  } // namespace

  void FUN_00258ab8_party_follower(OriginalEntity &entity,
                                   const ActorEnvironment &environment,
                                   ActorTrace &trace)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }
    const std::size_t slot = environment.currentSlot;

    // A pending hit turns the follower away from whatever landed it and plays
    // the stagger, which state 10 sits in until the floor is back underfoot.
    if (static_cast<std::int16_t>(entity.pendingDamageBe) > 0)
    {
      entity.facingRadians5c = FUN_00216690_wrap(entity.hitDirectionC4 + kfGpffff8a98_pi);
      FUN_00225bf0_set_state_and_animation(entity, 10, 0x1C);
      entity.pendingDamageBe = 0;
    }

    // -1 is the "rebuild me" sentinel: drop the look-at, arm the idle timer and
    // restart from state 1.
    if (static_cast<std::int16_t>(entity.state60) == -1)
    {
      FUN_00257f78_release_look(environment, slot);
      entity.hitFlash1c2 = 0x1E0;
      entity.fadeRamp62 = 0x0F;
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x4000u);
      FUN_00225bf0_set_state_and_animation(entity, 1, 0);
    }

    switch (entity.state60)
    {
    case 0:
      FUN_002596c8_state0_init(entity, environment, slot);
      break;
    case 1:
      FUN_002597d0_state1_idle(entity, environment, slot, trace);
      break;
    case 2:
      FUN_00259d00_state2_turn_and_push(entity, environment);
      break;
    case 3:
      FUN_00259e50_state3_turn(entity, environment, slot);
      break;
    case 4:
      FUN_00259ec0_state4_walk_path(entity, environment, slot);
      break;
    case 6:
      FUN_0025a298_state6_recover(entity, environment, slot, trace);
      break;
    case 7:
      FUN_0025a450_state7_walk_blind(entity, environment);
      break;
    case 8:
      FUN_0025a500_state8_follow(entity, environment, slot, trace);
      break;
    case 9:
      FUN_0025aa48_state9_sidestep(entity, environment);
      break;
    case 10:
      FUN_0025ab48_state10_wait_for_floor(entity);
      break;
    default:
    {
      // States 4 and 5 -- the navmesh cell walk and the walk to a chosen
      // waypoint. Both read map data the port does not publish, and neither is
      // reachable without FUN_00259378 setting them; see the header.
      const std::uint32_t handler =
          environment.dispatchTable != nullptr
              ? environment.dispatchTable->stateHandler(kPTR_FUN_0031e1a0_followerStates,
                                                        kFollowerStateCount, entity.state60)
              : 0;
      trace.recordStateDispatch(entity.typeId00, entity.state60, handler, false);
      break;
    }
    }
  }

} // namespace orphen::ported::entity
