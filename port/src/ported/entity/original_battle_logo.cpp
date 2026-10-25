#include "ported/entity/original_battle_logo.h"

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"

namespace orphen::ported::entity
{
  namespace
  {
    constexpr std::uint16_t kAnimationFinished06 = 0x0001;

    // LAB_00239fb8's numbers.
    constexpr std::int16_t kRunningAnimation = 1;
    constexpr std::uint8_t kFadeInStart = 3;
    constexpr std::uint8_t kFadeOutStart = 0x7F;
    constexpr std::uint8_t kFadeStep = 8;
    constexpr std::uint8_t kFadeOutLast = 0x0D; // `sltiu v0, v1, 0xd`: below this, release
    constexpr std::int16_t kHoldUntilCursor = 0x12;

    // +0x198's three values.
    constexpr std::uint32_t kPhaseHolding = 0;
    constexpr std::uint32_t kPhaseFadingIn = 1;
    constexpr std::uint32_t kPhaseFadingOut = 2;
  } // namespace

  std::size_t FUN_0025d5b8_spawn_battle_logo(EntityPool &pool, const EntityDescriptorTable &descriptors)
  {
    // `if (DAT_0058c7e8 < 1)` reads the type word raw, so it is the type and
    // not the slot status that decides.
    if (pool.slot(kBattleLogoSlot).typeId00 < 1)
    {
      pool.FUN_00229c40_initialize(kBattleLogoSlot, kBattleLogoTypeId, descriptors);
      OriginalEntity &logo = pool.slot(kBattleLogoSlot);
      logo.halfword08 = static_cast<std::uint16_t>(logo.halfword08 | kBattleLogoFlags08);
      FUN_00229ef0_set_scale(logo, kBattleLogoScale, &descriptors);
    }
    return kBattleLogoSlot;
  }

  void FUN_002255b8_place_battle_logo(OriginalEntity &logo,
                                      float eyeX, float eyeY, float eyeZ,
                                      float forwardX, float forwardY, float forwardZ,
                                      float yaw, float pitch, float roll)
  {
    logo.positionX20 = eyeX + forwardX * kBattleLogoDistance;
    logo.positionZ24 = eyeY + forwardY * kBattleLogoDistance;
    logo.positionY28 = (eyeZ + (kDAT_0058c0e0_cameraHeight - kDAT_0035241c_heightBias) +
                        forwardZ * kBattleLogoDistance) -
                       kBattleLogoDrop;
    logo.facingRadians5c = yaw;
    logo.rotationX154 = -pitch;
    logo.rotationY158 = roll;
    logo.groundHeight4c = logo.positionY28;
  }

  void LAB_00239fb8_battle_logo(OriginalEntity &logo, std::size_t slot, const ActorEnvironment &environment)
  {
    // 0x239fb8: `lh v1, 0xa0(a0); bne v1, 1, return`.
    if (static_cast<std::int16_t>(logo.animationA0) != kRunningAnimation)
    {
      return;
    }
    // 0x239fc8-0x239fe0, the first running frame.
    if (static_cast<std::int8_t>(logo.spawnParam94) == 0)
    {
      logo.spawnParam94 = 1;
      logo.fadeLevel134 = kFadeInStart;
      logo.eventFlagId198 = kPhaseFadingIn;
    }
    // 0x239fe4-0x239ff4, a tail call into FUN_00265EC0.
    if ((logo.flags06 & kAnimationFinished06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    if (logo.eventFlagId198 == kPhaseFadingIn)
    {
      // 0x23a004-0x23a020: step, and stop the frame the byte's sign bit sets.
      logo.fadeLevel134 = static_cast<std::uint8_t>(logo.fadeLevel134 + kFadeStep);
      if (static_cast<std::int8_t>(logo.fadeLevel134) < 0)
      {
        logo.eventFlagId198 = kPhaseHolding;
        logo.fadeLevel134 = 0;
      }
      return;
    }
    if (logo.eventFlagId198 == kPhaseFadingOut)
    {
      // 0x23a02c-0x23a048.
      if (logo.fadeLevel134 < kFadeOutLast)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
        return;
      }
      logo.fadeLevel134 = static_cast<std::uint8_t>(logo.fadeLevel134 - kFadeStep);
      return;
    }
    // 0x23a04c-0x23a05c, any other phase: hold until the timeline is far enough in.
    if (static_cast<std::int16_t>(logo.timelineCursorA8) >= kHoldUntilCursor)
    {
      logo.eventFlagId198 = kPhaseFadingOut;
      logo.fadeLevel134 = kFadeOutStart;
    }
  }

} // namespace orphen::ported::entity
