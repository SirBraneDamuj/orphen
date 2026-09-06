#include "ported/entity/original_status_aura.h"

#include "ported/script/object_registers.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_0031DA6C, the per-member status word. Spelled here rather than pulled
    // in from the battle module, which this layer does not depend on -- the two
    // reads go through the environment's table accessors.
    inline constexpr std::uint32_t kDAT_0031da6c_memberFlags = 0x0031DA6Cu;

    // fGpffffa868: how fast the icon spins, per tick.
    inline constexpr float kFGpffffa868_spinRate = 0.0022689274f;
    // All six bodies fade at the same rate and vanish at the same size --
    // fGpffffa86c / DAT_003547E8 / DAT_003547FC / DAT_00354804 / DAT_0035480C /
    // fGpffffa8A4 are all 0.9, and their floors all 0.4.
    inline constexpr float kStatusFadeRate = 0.899999976f;
    inline constexpr float kStatusFadeFloor = 0.400000006f;
    // The floor a status drain will not take a character below.
    inline constexpr std::int16_t kStatusDrainFloor = 5;

    // The light each status lights the character with, from the block each body
    // writes on the frame it takes a slot: DAT_00343894 / 95 / 96.
    struct StatusLook
    {
      std::uint8_t red = 0;
      std::uint8_t green = 0;
      std::uint8_t blue = 0;
    };

    // FUN_0030bdb0: float to unsigned, truncating.
    std::uint8_t FUN_0030bdb0_trunc_u8(float value)
    {
      if (value <= 0.0f)
      {
        return 0;
      }
      const float truncated = std::trunc(value);
      return truncated >= 255.0f ? static_cast<std::uint8_t>(255)
                                 : static_cast<std::uint8_t>(truncated);
    }

    // FUN_00248e00: the aura's own countdown. Not FUN_0023a678 -- this one steps
    // by a *sixteenth* of the frame's ticks, so 0x4B0 is 600 frames rather than
    // 37, and it takes one off anyway when that sixteenth rounds to nothing.
    std::int16_t FUN_00248e00_countdown(std::int16_t timer, std::uint32_t frameTicks)
    {
      const std::int32_t ticks = static_cast<std::int32_t>(frameTicks);
      const std::int32_t step = (ticks < 0 ? ticks + 0x0F : ticks) >> 4;
      std::int32_t remaining = static_cast<std::int16_t>(timer - static_cast<std::int16_t>(step));
      if (step == 0)
      {
        remaining = static_cast<std::int16_t>(remaining - 1);
      }
      return remaining < 1 ? static_cast<std::int16_t>(0) : static_cast<std::int16_t>(remaining);
    }

    // FUN_00266098: give the light slot back by zeroing its radius, which is the
    // only thing that frees it.
    void FUN_00266098_release_light(OriginalEntity &aura, const ActorEnvironment &environment)
    {
      if (aura.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
      {
        environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(aura.lightSlot195))
            .radius = 0.0f;
      }
      aura.lightSlot195 = -1;
    }

    // FUN_002660d0: the light follows the entity.
    void FUN_002660d0_move_light(const OriginalEntity &aura, const ActorEnvironment &environment)
    {
      if (aura.lightSlot195 < 0 || environment.DAT_00343888_lights == nullptr)
      {
        return;
      }
      auto &light =
          environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(aura.lightSlot195));
      light.x = aura.positionX20;
      light.y = aura.positionZ24;
      light.z = aura.positionY28;
    }

    // FUN_0023eb20: a light from slot 3 up, falling back to the whole table, and
    // alpha 1 on whatever it gets.
    std::int32_t FUN_0023eb20_allocate_light(const ActorEnvironment &environment)
    {
      if (environment.DAT_00343888_lights == nullptr)
      {
        return -1;
      }
      auto &table = *environment.DAT_00343888_lights;
      std::int32_t index = table.FUN_00266008_allocateFromThree();
      if (index < 0)
      {
        index = table.FUN_00266050_allocateFromZero();
      }
      if (index != 0 && index >= 0)
      {
        table.slot(static_cast<std::uint32_t>(index)).alpha = 1;
      }
      return index;
    }

    // The drain statuses 4 and 9 share: one hit point per animation loop, and
    // the lower health bar armed with it. Gated on the victim being a party
    // member that is not already reeling from something else and whose control
    // block is mid-action.
    void status_drain(OriginalEntity &victim,
                      bool onKeyframe,
                      const ActorEnvironment &environment)
    {
      const std::int32_t member = static_cast<std::int32_t>(victim.byte95) - 1;
      if (member < 0)
      {
        return;
      }
      // +0xBC's upper three bytes: the freeze counter and the pending damage.
      // A character that is already taking a hit does not also tick.
      if (victim.freezeTimerBd != 0 || victim.pendingDamageBe != 0)
      {
        return;
      }
      ActorEnvironment::BattleMemberView view;
      if (!environment.DAT_0031d7b0_battleMember ||
          !environment.DAT_0031d7b0_battleMember(static_cast<std::uint32_t>(member), view))
      {
        return;
      }
      const bool acting = static_cast<std::uint8_t>(view.pendingAction0e + 0x7C) < 0x0F ||
                          static_cast<std::uint8_t>(view.currentAction0f + 0x7C) < 0x0F;
      if (!acting)
      {
        return;
      }

      // FUN_0021e088 / FUN_0021f6e8, the drip of particles the status leaves on
      // the character. The port has no equivalent spawner, the same way the
      // other unported effect pools are left out rather than approximated.

      const auto hitPoints = static_cast<std::int16_t>(victim.staggerTimer12a);
      if (hitPoints <= kStatusDrainFloor || !onKeyframe)
      {
        return;
      }
      victim.staggerTimer12a = static_cast<std::uint16_t>(hitPoints - 1);
      if (environment.FUN_002d5630_damage_bar)
      {
        // Bank 0: the *lower* bar, the character's own.
        environment.FUN_002d5630_damage_bar(false, hitPoints - 1,
                                            static_cast<std::int16_t>(victim.maxHitPoints128), 1);
      }
    }

    // The body all six statuses share. Returns false when the icon is already
    // hidden -- the original's guard is at the top of each body, so the extras
    // below do not run either.
    bool status_body(OriginalEntity &aura,
                     OriginalEntity &victim,
                     const StatusLook &look,
                     const ActorEnvironment &environment)
    {
      if ((aura.halfword08 & 1u) != 0)
      {
        return false;
      }

      aura.positionX20 = victim.positionX20;
      aura.positionZ24 = victim.positionZ24;
      aura.positionY28 = victim.height58 + victim.positionY28 + aura.auraHeight19c;
      FUN_002660d0_move_light(aura, environment);

      const std::int16_t timer = FUN_00248e00_countdown(
          static_cast<std::int16_t>(aura.fadeRamp62), environment.frameTicks);
      aura.fadeRamp62 = static_cast<std::uint16_t>(timer);
      if (timer != 0)
      {
        if (aura.lightSlot195 < 0)
        {
          const std::int32_t index = FUN_0023eb20_allocate_light(environment);
          aura.lightSlot195 = static_cast<std::int8_t>(index);
          if (index >= 0 && environment.DAT_00343888_lights != nullptr)
          {
            auto &light =
                environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(index));
            light.radius = 1.0f;
            light.red = look.red;
            light.green = look.green;
            light.blue = look.blue;
            FUN_002660d0_move_light(aura, environment);
          }
        }
        return true;
      }

      // The timer is out: shrink a tenth a frame, and take the light down with
      // it. The radius is the *truncated* scale, so it hits zero -- and frees
      // the slot -- on the very first fading frame.
      const float scale = aura.scaleZ150 * kStatusFadeRate;
      aura.scaleZ150 = scale;
      aura.scale14c = scale;
      if (aura.lightSlot195 >= 0 && environment.DAT_00343888_lights != nullptr)
      {
        auto &light =
            environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(aura.lightSlot195));
        const std::uint8_t level = FUN_0030bdb0_trunc_u8(scale * 128.0f);
        light.red = level;
        light.green = level;
        light.radius = static_cast<float>(FUN_0030bdb0_trunc_u8(scale));
      }
      if (aura.scale14c >= kStatusFadeFloor)
      {
        return true;
      }

      aura.flags06 = static_cast<std::uint16_t>(aura.flags06 | 0x10u);
      aura.halfword08 = static_cast<std::uint16_t>(aura.halfword08 | 1u);
      FUN_00266098_release_light(aura, environment);

      // **The one place the status bit goes away.**
      const std::int32_t member = static_cast<std::int32_t>(victim.byte95) - 1;
      if (member >= 0 && environment.DAT_0031d3c8_battleTableWord &&
          environment.DAT_0031d3c8_setBattleTableWord)
      {
        const std::uint32_t at =
            kDAT_0031da6c_memberFlags + static_cast<std::uint32_t>(member) * 4u;
        const std::uint32_t bit = 1u << (static_cast<std::uint32_t>(aura.auraStatus19a) & 0x1Fu);
        environment.DAT_0031d3c8_setBattleTableWord(
            at, environment.DAT_0031d3c8_battleTableWord(at) & ~bit);
      }
      return true;
    }
  } // namespace

  void FUN_002d8ce0_status_aura(OriginalEntity &aura,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t victimSlot = static_cast<std::size_t>(aura.auraVictim198);
    if (victimSlot >= pool.slotCount())
    {
      return;
    }
    OriginalEntity &victim = pool.slot(victimSlot);

    // The victim held by something -- the Maneater clone's grab is the one that
    // does it -- or uGpffffb052 bit 2, and the icon is pulled to nothing and the
    // light handed back. Neither is permanent: the body picks straight back up
    // at whatever scale it left off, because +0x08 bit 0 is untouched.
    if ((victim.battleFlags96 & 4u) != 0 || (environment.sGpffffb052_battleFlags & 4u) != 0)
    {
      aura.scale14c = 0.0f;
      aura.scaleZ150 = 0.0f;
      FUN_00266098_release_light(aura, environment);
      return;
    }

    // A live status resets the size every frame, so the shrink below only ever
    // runs once the timer is out.
    if (static_cast<std::int16_t>(aura.fadeRamp62) != 0)
    {
      aura.scaleZ150 = 1.0f;
      aura.scale14c = 1.0f;
    }
    aura.facingRadians5c = orphen::ported::script::FUN_00216690_wrapAngle(
        aura.facingRadians5c + static_cast<float>(environment.frameTicks) * kFGpffffa868_spinRate);

    switch (aura.auraStatus19a)
    {
    case 1: // FUN_002d9908
      status_body(aura, victim, StatusLook{0x28, 0x50, 0x80}, environment);
      break;
    case 4: // FUN_002d90d8
      if (status_body(aura, victim, StatusLook{0x80, 0x00, 0x00}, environment))
      {
        status_drain(victim, (aura.flags06 & 1u) != 0, environment);
      }
      break;
    case 5: // FUN_002d9730
      status_body(aura, victim, StatusLook{0x80, 0x00, 0x80}, environment);
      break;
    case 6:  // FUN_002d9370, shared with 10
    case 10:
    {
      const bool ran = status_body(aura, victim, StatusLook{0x00, 0x00, 0x80}, environment);
      // Being hit shakes these two off: the aura keeps the hit points the
      // victim had when it landed, and any loss ends it next frame.
      if (ran && static_cast<std::int16_t>(aura.fadeRamp62) != 0 &&
          static_cast<std::int16_t>(victim.staggerTimer12a) <
              static_cast<std::int16_t>(aura.staggerTimer12a))
      {
        aura.fadeRamp62 = 1;
      }
      break;
    }
    case 9: // FUN_002d8e30, the poison
      if (status_body(aura, victim, StatusLook{0x80, 0x80, 0x00}, environment))
      {
        status_drain(victim, (aura.flags06 & 4u) != 0, environment);
      }
      break;
    case 12: // FUN_002d9558, the confusion
      status_body(aura, victim, StatusLook{0x00, 0x80, 0x00}, environment);
      break;
    default:
      // The dispatch has no default; a status with no body simply sits there
      // until something else takes it down.
      break;
    }
    (void)slot;
  }

} // namespace orphen::ported::entity
