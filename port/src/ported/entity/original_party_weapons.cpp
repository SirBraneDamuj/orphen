#include "ported/entity/original_party_weapons.h"

#include "ported/entity/original_hit_test.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/resource/hit_parameter_table.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{

  namespace
  {
    // FUN_002d07d0's impact burst, FUN_0021ed50's six shape floats.
    constexpr float kDAT_003545f8_burstRise = 0.00999999978f;
    constexpr float kDAT_003545f4_burstFall = 0.0199999996f;
    constexpr float kDAT_003545fc_burstSpeedRange = 0.300000012f;
    constexpr float kDAT_00354600_burstJitter = 0.100000001f;
    constexpr float kDAT_00354604_burstSize = 0.0299999993f;
    constexpr int kOrbBurstCount = 100;
    constexpr std::int16_t kOrbBurstLife = 0x3c;
    constexpr std::uint32_t kOrbBurstColour = 0xe080;

    // FUN_002d06b0.
    constexpr float kDAT_003545f0_orbSpeed = 0.00179999997f;
    constexpr std::int32_t kOrbWord1a0 = 0x2580;
    constexpr float kOrbLightRadius = 2.0f; // 0x40000000

    // FUN_002d0a30's release. The speed is SQRT(fGpffffa698).
    constexpr float kfGpffffa698_throwSpeedSquared = 0.00600000005f;
    constexpr float kuGpffffa69c_throwGravity = 4.99999987e-05f;
    constexpr float kuGpffffa6a0_throwDecel = 0.000250000012f;

    // FUN_002d0c00.
    constexpr float kfGpffffa6a4_driftTurn = 0.00272707641f;
    constexpr float kfGpffffa6a8_driftClimbBand = 0.200000003f;
    constexpr float kfGpffffa6ac_driftClimb = 0.00499999989f;
    constexpr float kfGpffffa6b0_driftSink = 0.00499999989f;
    constexpr float kfGpffffa6b4_driftBobRate = 0.00109083054f;
    constexpr float kfGpffffa6b8_driftBobDepth = 0.000312499993f;

    constexpr std::uint16_t kAnimationComplete06 = 0x0001;

    // FUN_0023A4B8: the heading from one entity to another, over X and Z.
    float FUN_0023a4b8_heading_to(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_00216078(type, index, dest): the attack record, or the destination
    // left as it was when the type has none.
    void FUN_00216078_record(std::int16_t typeId, std::uint32_t index, std::uint32_t &packed,
                             const ActorEnvironment &environment)
    {
      if (environment.DAT_00354d6c_hitParameters == nullptr)
      {
        return;
      }
      const auto record = environment.DAT_00354d6c_hitParameters->FUN_00216078_record(typeId, index);
      if (record.has_value())
      {
        packed = record->packed();
      }
    }

    orphen::ported::render::LightTable::Slot *lightFor(const OriginalEntity &entity,
                                                       const ActorEnvironment &environment)
    {
      if (entity.lightSlot195 < 0 || environment.DAT_00343888_lights == nullptr)
      {
        return nullptr;
      }
      return &environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(entity.lightSlot195));
    }

    // FUN_002660d0: the entity's light slot takes its +0x20..+0x2B.
    void FUN_002660d0_light_follows(const OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (auto *light = lightFor(entity, environment))
      {
        light->x = entity.positionX20;
        light->y = entity.positionZ24;
        light->z = entity.positionY28;
      }
    }

    std::size_t allocate(std::int16_t typeId, const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return kEntitySlotCount;
      }
      return environment.entityPool->FUN_00265e28_allocate_and_initialize(typeId,
                                                                           *environment.descriptors);
    }

    std::uint8_t handBone(std::size_t ownerSlot, const ActorEnvironment &environment)
    {
      return static_cast<std::uint8_t>(
          environment.FUN_0020dd78_bone_for_role ? environment.FUN_0020dd78_bone_for_role(ownerSlot, 4)
                                                 : 0);
    }
  } // namespace

  std::int8_t partyWeaponHitTest(OriginalEntity &attacker, std::size_t slot,
                                 std::uint32_t packedParameters,
                                 const ActorEnvironment &environment)
  {
    if (environment.hitTest == nullptr)
    {
      return 0;
    }
    return FUN_002148a8_swept_hit_test(attacker, slot,
                                       orphen::ported::resource::HitParameters::unpack(packedParameters),
                                       *environment.hitTest);
  }

  void holdAtHand(OriginalEntity &held,
                  std::size_t ownerSlot,
                  const orphen::ported::psm2::Vec3 &offset,
                  const ActorEnvironment &environment)
  {
    if (!environment.FUN_0020dc88_bone_point)
    {
      return;
    }
    const auto point = environment.FUN_0020dc88_bone_point(ownerSlot, handBone(ownerSlot, environment), offset);
    held.positionX20 = point.x;
    held.positionZ24 = point.y;
    held.positionY28 = point.z;
    held.groundHeight4c = held.positionY28;
  }

  // FUN_00256BB8, the tail of the attack branch for classes 3 and 4:
  //
  //   iVar8 = FUN_00265e28(type);            // 0x4E or 0x50
  //   +0x02 = 0x1000;
  //   0x4E: +0x194 = 0xD7; +0x134 = 3; +0x62 = 0x60; +0xA0 = 3; +0x5C = lead's
  //   0x50: +0xA0 = 1;     +0x194 = 0x0D;
  //   +0x192 = 0; +0x08 = 0; +0x12C = lead +0x12C; +0x60 = lead +0x60;
  //   FUN_00216078(*lead, 0, iVar8 + 0x198);
  //   lead +0x198 = iVar8;
  //
  // Both hang off slot 0 -- the original writes a literal 0 to +0x192 -- and
  // the bone byte's sign picks how. 0x50's 0x0D is bone 13, fully attached; 0x4E's
  // 0xD7 is -41, which is FUN_0020CDC0's position-only follow, the same branch
  // the bandana takes. 0x4E also starts faded (+0x134 = 3) and its own frame
  // brings it in.
  std::int32_t FUN_00256bb8_spawn_held_weapon(const OriginalEntity &owner,
                                              std::size_t ownerSlot,
                                              std::int16_t typeId,
                                              const ActorEnvironment &environment)
  {
    (void)ownerSlot;
    const std::size_t slot = allocate(typeId, environment);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &weapon = environment.entityPool->slot(slot);
    weapon.descriptorFlags02 = 0x1000;
    if (typeId == kPartyHeldWeapon4eTypeId)
    {
      weapon.attachBone194 = static_cast<std::int8_t>(0xD7);
      weapon.fadeLevel134 = 3;
      weapon.fadeRamp62 = 0x60;
      weapon.animationA0 = 3;
      weapon.facingRadians5c = owner.facingRadians5c;
    }
    else
    {
      weapon.animationA0 = 1;
      weapon.attachBone194 = 0x0D;
    }
    weapon.parentSlot192 = 0;
    weapon.halfword08 = 0;
    weapon.attackPower12c = owner.attackPower12c;
    weapon.state60 = owner.state60;
    FUN_00216078_record(owner.typeId00, 0, weapon.hitParameters198, environment);
    return static_cast<std::int32_t>(slot);
  }

  // FUN_002D06B0. Nearly FUN_002D2E00's shape -- a light of its own, a homing
  // pick -- but the pick is written and never read again, and the record goes to
  // +0x1AA rather than +0x1AC.
  std::int32_t FUN_002d06b0_spawn_orb(const OriginalEntity &owner,
                                      std::size_t ownerSlot,
                                      const orphen::ported::psm2::Vec3 &point,
                                      const ActorEnvironment &environment)
  {
    const std::size_t slot = allocate(kPartyOrb4fTypeId, environment);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &orb = environment.entityPool->slot(slot);
    orb.descriptorFlags02 = 0x1000;
    orb.halfword04 = 0x19;
    FUN_00225bc8_set_animation(orb, 1);
    orb.positionX20 = point.x;
    orb.positionZ24 = point.y;
    orb.positionY28 = point.z;
    orb.rejectTerrainMask74 = 0;
    orb.groundHeight4c = owner.groundHeight4c;
    orb.orbOwner198 = static_cast<std::int32_t>(ownerSlot);
    orb.facingRadians5c = owner.facingRadians5c;
    FUN_00216078_record(owner.typeId00, 1, orb.hitParameters1aa, environment);
    orb.orbOwnerState1a8 = static_cast<std::int16_t>(owner.state60);
    orb.orbSpeed1a4 = kDAT_003545f0_orbSpeed;

    // FUN_00266050, then 0x00040404 into the slot's colour and 2.0 into its
    // radius, then FUN_002660d0.
    orb.lightSlot195 = -1;
    if (environment.DAT_00343888_lights != nullptr)
    {
      const std::int32_t lightSlot = environment.DAT_00343888_lights->FUN_00266050_allocateFromZero();
      orb.lightSlot195 = static_cast<std::int8_t>(lightSlot);
      if (lightSlot >= 0)
      {
        auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
        light.red = 4;
        light.green = 4;
        light.blue = 4;
        light.alpha = 0;
        light.radius = kOrbLightRadius;
        environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(lightSlot), kOrbLightRadius);
        FUN_002660d0_light_follows(orb, environment);
      }
    }

    orb.orbTarget19c = FUN_002d2ca8_find_homing_target(environment, orb);
    orb.orbWord1a0 = kOrbWord1a0;
    return static_cast<std::int32_t>(slot);
  }

  // FUN_002567C0, cursor 0 of animation 0x14: the prop in the hand. +0x04 =
  // 0x100 turns its physics off, and it is attached to the role-4 bone.
  std::int32_t FUN_002567c0_spawn_hand_prop(const OriginalEntity &owner,
                                            std::size_t ownerSlot,
                                            const ActorEnvironment &environment)
  {
    const std::size_t slot = allocate(kPartyHandProp51TypeId, environment);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &prop = environment.entityPool->slot(slot);
    prop.animationA0 = 0;
    prop.halfword04 = 0x100;
    prop.parentSlot192 = static_cast<std::int16_t>(ownerSlot);
    prop.attachBone194 = static_cast<std::int8_t>(handBone(ownerSlot, environment));
    prop.state60 = owner.state60;
    return static_cast<std::int32_t>(slot);
  }

  // FUN_002567C0, cursor 8: the projectile. It is not attached; the state
  // handler writes its position to the hand every frame until the release.
  std::int32_t FUN_002567c0_spawn_throw(const OriginalEntity &owner,
                                        std::size_t ownerSlot,
                                        const ActorEnvironment &environment)
  {
    const std::size_t slot = allocate(kPartyThrow52TypeId, environment);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &shot = environment.entityPool->slot(slot);
    shot.animationA0 = 6;
    shot.descriptorFlags02 = 0x1000;
    shot.halfword04 = 0x19;
    shot.facingRadians5c = owner.facingRadians5c;
    shot.state60 = owner.state60;
    FUN_00216078_record(owner.typeId00, 1, shot.hitParameters198, environment);
    shot.throwCaster130 = static_cast<std::int16_t>(ownerSlot);
    return static_cast<std::int32_t>(slot);
  }

  std::int32_t FUN_002569d8_spawn_drift(const OriginalEntity &owner,
                                        std::size_t ownerSlot,
                                        const ActorEnvironment &environment)
  {
    const std::size_t slot = allocate(kPartyDrift56TypeId, environment);
    if (slot >= kEntitySlotCount)
    {
      return -1;
    }
    OriginalEntity &shot = environment.entityPool->slot(slot);
    holdAtHand(shot, ownerSlot, kDAT_0031e0c8_driftHandOffset, environment);
    shot.facingRadians5c = owner.facingRadians5c;
    FUN_00216078_record(owner.typeId00, 1, shot.hitParameters198, environment);
    return static_cast<std::int32_t>(slot);
  }

  // FUN_002D05E8, type 0x4E. Sweep its own hit volume every frame until its
  // animation ends; die with the lead's state; fade in over the first frames.
  //
  // The original can reach FUN_00265EC0 twice in one frame (animation over,
  // then the state test reading the released slot). The second call and the
  // fade write after it land on a slot whose type is already zero and change
  // nothing visible, so the port stops at the first.
  void FUN_002d05e8_held_weapon_4e(OriginalEntity &weapon, std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    FUN_0023a068_freeze_gate(weapon, environment.frameTicks);
    if ((weapon.flags06 & kAnimationComplete06) == 0)
    {
      partyWeaponHitTest(weapon, slot, weapon.hitParameters198, environment);
    }
    else
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }
    // DAT_0058bf10, the lead's +0x60.
    if (weapon.state60 != environment.entityPool->leadPlayer().state60)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }
    if (weapon.fadeLevel134 != 0)
    {
      weapon.fadeRamp62 = static_cast<std::uint16_t>(weapon.fadeRamp62 + environment.frameTicks * 4u);
      const int ramp = static_cast<std::int16_t>(weapon.fadeRamp62);
      if (ramp < 0xFE0)
      {
        weapon.fadeLevel134 = static_cast<std::uint8_t>((ramp < 0 ? ramp + 0x1F : ramp) >> 5);
      }
      else
      {
        weapon.fadeLevel134 = 0;
      }
    }
  }

  // FUN_002D07D0, type 0x4F. Three states:
  //
  //   0  charging where it was spawned: brighten the light by 4 a frame, and
  //      die if the caster has left the state it was spawned in.
  //   1  flying straight at +0x1A4 per tick along +0x5C. A contact, or any of
  //      +0x0C's 0x262 wall/floor bits, throws a fountain burst; a contact
  //      then deletes it, a wall puts it in state 2.
  //   2  play animation 4 out and delete.
  void FUN_002d07d0_orb_4f(OriginalEntity &orb, std::size_t slot, const ActorEnvironment &environment)
  {
    FUN_0023a068_freeze_gate(orb, environment.frameTicks);
    EntityPool &pool = *environment.entityPool;

    if (orb.state60 == 0)
    {
      const bool ownerValid = orb.orbOwner198 >= 0 && orb.orbOwner198 < static_cast<std::int32_t>(kEntitySlotCount);
      if (ownerValid &&
          static_cast<std::int16_t>(pool.slot(static_cast<std::size_t>(orb.orbOwner198)).state60) ==
              orb.orbOwnerState1a8)
      {
        if (auto *light = lightFor(orb, environment))
        {
          const std::uint8_t level =
              static_cast<std::uint8_t>(light->red + 4u < 0x100u ? light->red + 4u : 0xFFu);
          light->red = level;
          light->blue = level;
          light->green = level;
        }
      }
      else
      {
        FUN_00265ec0_destroy_entity(slot, environment);
      }
      return;
    }

    if (orb.state60 == 1)
    {
      const std::int8_t contacts = partyWeaponHitTest(orb, slot, orb.hitParameters1aa, environment);
      if (contacts != 0 || (orb.collisionFlags0c & 0x262u) != 0)
      {
        if (environment.FUN_0021ed50_spawn_fountain)
        {
          environment.FUN_0021ed50_spawn_fountain(
              kDAT_003545f8_burstRise, kDAT_003545f4_burstFall, kDAT_003545f4_burstFall,
              kDAT_003545fc_burstSpeedRange, kDAT_00354600_burstJitter, kDAT_00354604_burstSize,
              orb.positionX20, orb.positionZ24, orb.positionY28, kOrbBurstCount, kOrbBurstLife, 0, 0,
              kOrbBurstColour);
        }
        if (contacts == 0)
        {
          FUN_00225bf0_set_state_and_animation(orb, 2, 4);
          return;
        }
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }
      const float step = orb.orbSpeed1a4 * static_cast<float>(environment.frameTicks);
      orb.desiredDeltaX30 += step * std::cos(orb.facingRadians5c);
      orb.desiredDeltaZ34 += step * std::sin(orb.facingRadians5c);
      FUN_002660d0_light_follows(orb, environment);
      if (orb.animationA0 == 2 && (orb.flags06 & kAnimationComplete06) != 0)
      {
        FUN_00225bc8_set_animation(orb, 0);
      }
      return;
    }

    if (orb.state60 == 2 && (orb.flags06 & kAnimationComplete06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // 0x002D09B8, type 0x50. No src/ file; the whole body is
  //
  //   if (lead +0x00 != 6 || lead +0x60 != 0x20) FUN_00265ec0(self);
  //
  // so the weapon lives exactly as long as a type 6 lead stays in state 0x20.
  // Its hit test is run by that state, not here.
  void FUN_002d09b8_held_weapon_50(OriginalEntity &weapon, std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    (void)weapon;
    const OriginalEntity &lead = environment.entityPool->leadPlayer();
    if (lead.typeId00 != 6 || lead.state60 != 0x20)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // 0x002D09F0, type 0x51. No src/ file:
  //
  //   parent = pool + self +0x192 * 0x1D8;
  //   if (parent +0x60 != self +0x60) FUN_00265ec0(self);
  void FUN_002d09f0_hand_prop_51(OriginalEntity &prop, std::size_t slot,
                                 const ActorEnvironment &environment)
  {
    const auto parent = static_cast<std::size_t>(static_cast<std::uint16_t>(prop.parentSlot192));
    if (parent >= kEntitySlotCount ||
        environment.entityPool->slot(parent).state60 != prop.state60)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  // FUN_002D0A30, type 0x52. +0x94 is its phase: 0 held (the state handler
  // positions it), 1 released this frame, 2 flying.
  //
  // In flight it decelerates linearly from sqrt(0.006) and falls under a light
  // gravity, pitching its model (+0x154) along its own arc. A contact or
  // either of +0x0C's bits 1/2 ends it. With the free-look camera up, the
  // release takes the camera's yaw instead of the thrower's facing.
  void FUN_002d0a30_throw_52(OriginalEntity &shot, std::size_t slot, const ActorEnvironment &environment)
  {
    if (shot.spawnParam94 == 0)
    {
      const auto caster = static_cast<std::size_t>(static_cast<std::uint16_t>(shot.throwCaster130));
      if (caster >= kEntitySlotCount ||
          environment.entityPool->slot(caster).state60 != shot.state60)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
      }
      return;
    }

    if (shot.spawnParam94 == 1)
    {
      shot.throwSpeed19c = std::sqrt(kfGpffffa698_throwSpeedSquared);
      shot.halfword04 = static_cast<std::uint16_t>(shot.halfword04 & 0xFFF7u);
      shot.throwDecel1a0 = kuGpffffa6a0_throwDecel;
      shot.verticalAcceleration48 = kuGpffffa69c_throwGravity;
      shot.spawnParam94 = 2;
      shot.collisionFlags0c &= 0xFFFFFF98u;
      // cGpffffb6e4 / uGpffffb6d4.
      if (environment.camera != nullptr && environment.camera->freeLookActive())
      {
        shot.facingRadians5c = environment.camera->yawRadians();
      }
    }

    FUN_0023a068_freeze_gate(shot, environment.frameTicks);
    const std::int8_t contacts = partyWeaponHitTest(shot, slot, shot.hitParameters198, environment);
    if (contacts != 0 || (shot.collisionFlags0c & 6u) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    const float speed = shot.throwSpeed19c;
    const float dt = static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) * 0.125f;
    const float decel = shot.throwDecel1a0 * dt;
    const float next = speed - decel;
    shot.throwSpeed19c = next;
    const float distance = speed * dt - decel * dt * 0.5f;
    if (next < 0.0f)
    {
      shot.throwSpeed19c = 0.0f;
    }
    shot.desiredDeltaX30 += distance * std::cos(shot.facingRadians5c);
    shot.desiredDeltaZ34 += distance * std::sin(shot.facingRadians5c);
    shot.rotationX154 =
        std::atan2(-shot.verticalVelocity44 - shot.verticalAcceleration48 * 5.0f, distance);
  }

  // FUN_002D0C00, type 0x56.
  //
  //   0  pick a speed of 5..14 thousandths per 32 ticks, a target, and a life
  //      of 6..9 x 0x780 ticks, then go to 1.
  //   1  turn toward the target at ~0.087 rad a frame, drift forward, and
  //      climb or sink 0.005 when more than 0.2 off the target's height. A
  //      contact, a wall bit or the life running out moves to state 2.
  //   2  **dies on its first frame.** The body reads as a fade from 0xFE0 in
  //      steps of 2 x frameTicks, but the test is `ramp < 0x80` for the fade
  //      and FUN_00265EC0 otherwise (0x002D0E10 slti / beqz), and the ramp
  //      starts far above 0x80. Reproduced as the binary has it.
  //
  // A slow vertical bob runs under all three.
  void FUN_002d0c00_drift_56(OriginalEntity &shot, std::size_t slot, const ActorEnvironment &environment)
  {
    FUN_0023a068_freeze_gate(shot, environment.frameTicks);
    EntityPool &pool = *environment.entityPool;
    const auto ticks = static_cast<std::int32_t>(environment.frameTicks);

    const auto target = [&]() -> const OriginalEntity * {
      if (shot.driftTarget19c < 0 || shot.driftTarget19c >= static_cast<std::int32_t>(kEntitySlotCount))
      {
        return nullptr;
      }
      return &pool.slot(static_cast<std::size_t>(shot.driftTarget19c));
    };

    if (shot.state60 == 0)
    {
      const auto roll = static_cast<std::int32_t>(environment.random ? environment.random() : 0u);
      shot.driftSpeed1a0 = (static_cast<float>(roll % 10 + 5) / 1000.0f) * 0.03125f;
      shot.driftTarget19c = FUN_002d2ca8_find_homing_target(environment, shot);
      shot.driftHeading1a4 = target() != nullptr ? FUN_0023a4b8_heading_to(shot, *target())
                                                 : shot.facingRadians5c;
      const auto life = static_cast<std::uint16_t>(environment.random ? environment.random() : 0u);
      shot.state60 = 1;
      shot.fadeRamp62 = static_cast<std::uint16_t>(((life & 3u) + 6u) * 0x780u);
    }
    else if (shot.state60 == 1)
    {
      float facing = shot.facingRadians5c;
      const std::int8_t contacts = partyWeaponHitTest(shot, slot, shot.hitParameters198, environment);
      facing += FUN_0023a320_approach_angle(facing, shot.driftHeading1a4,
                                            static_cast<float>(ticks) * kfGpffffa6a4_driftTurn);
      const std::int32_t life = static_cast<std::int32_t>(shot.fadeRamp62) - (ticks & 0xFFFF);
      shot.fadeRamp62 = static_cast<std::uint16_t>(life);
      if (contacts != 0 || (shot.collisionFlags0c & 6u) != 0 ||
          static_cast<std::int16_t>(life) < 1)
      {
        shot.fadeRamp62 = 0xFE0;
        shot.state60 = 2;
      }
      const float speed = shot.driftSpeed1a0 * static_cast<float>(ticks);
      shot.desiredDeltaX30 += speed * std::cos(facing);
      shot.facingRadians5c = facing;
      shot.desiredDeltaZ34 += speed * std::sin(facing);
      if (const OriginalEntity *aim = target())
      {
        const float dy = shot.positionY28 - aim->positionY28;
        if (kfGpffffa6a8_driftClimbBand < std::fabs(dy))
        {
          shot.desiredDeltaY38 = dy < 0.0f ? shot.desiredDeltaY38 + kfGpffffa6ac_driftClimb
                                           : shot.desiredDeltaY38 - kfGpffffa6b0_driftSink;
        }
        shot.driftHeading1a4 = FUN_0023a4b8_heading_to(shot, *aim);
      }
    }
    else if (shot.state60 == 2)
    {
      shot.fadeRamp62 = static_cast<std::uint16_t>(shot.fadeRamp62 + ticks * -2);
      const int ramp = static_cast<std::int16_t>(shot.fadeRamp62);
      if (ramp < 0x80)
      {
        shot.fadeLevel134 = static_cast<std::uint8_t>((ramp < 0 ? ramp + 0x1F : ramp) >> 5);
      }
      else
      {
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }
    }

    shot.driftBobPhase1a8 = orphen::ported::model::FUN_00216690_wrap_angle(
        shot.driftBobPhase1a8 + static_cast<float>(ticks) * kfGpffffa6b4_driftBobRate);
    shot.desiredDeltaY38 +=
        static_cast<float>(ticks) * kfGpffffa6b8_driftBobDepth * std::sin(shot.driftBobPhase1a8);
  }

} // namespace orphen::ported::entity
