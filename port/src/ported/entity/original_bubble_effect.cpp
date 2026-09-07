#include "ported/entity/original_bubble_effect.h"

#include "ported/entity/original_enemy_attack.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    // fGpffffaabc / aac0 / aac4 / aac8: four separate words, all a full turn.
    // Modes 0 and 1 each pick between two of them on a coin flip -- the minus
    // branch and the plus branch read different words -- so the pair is kept
    // apart rather than folded into one constant.
    inline constexpr float kFGpffffaabc_turnPlus0 = 6.28318548202515f;
    inline constexpr float kFGpffffaac0_turnMinus0 = 6.28318548202515f;
    inline constexpr float kFGpffffaac4_turnPlus1 = 6.28318548202515f;
    inline constexpr float kFGpffffaac8_turnMinus1 = 6.28318548202515f;

    inline constexpr std::uint16_t kFUN_002eac48_life = 0x0C80;
    inline constexpr float kFUN_002ea7f0_stage2Climb = 5.0f;
    // The two +0x0C masks. 0x4006 is the map, a wall or a ceiling; 0x60 is
    // another entity.
    inline constexpr std::uint32_t kFUN_002ea7f0_solidMask = 0x4006u;
    inline constexpr std::uint32_t kFUN_002ea7f0_entityMask = 0x60u;
    // What the pop becomes: a half-unit box one unit tall, on animation 2.
    inline constexpr float kFUN_002ea7f0_popRadius = 0.5f;
    inline constexpr float kFUN_002ea7f0_popHeight = 1.0f;

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    std::int32_t roll(const ActorEnvironment &environment)
    {
      return static_cast<std::int32_t>(environment.random ? environment.random() : 0);
    }

    // LAB_002EAA04, reached from both states. Note it does *not* move the
    // bubble on the frame it fires.
    void enter_stage_two(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      entity.bubblePhase198 = 1;
      entity.bubbleRise1a0 += kFUN_002ea7f0_stage2Climb;
      const auto life = static_cast<std::int16_t>(((roll(environment) % 0x14) + 0x32) * 0x20);
      entity.fadeRamp62 = static_cast<std::uint16_t>(life);
      entity.bubbleLife1a4 = life;
    }

    // The state-0 pop. It is the only path that can land a hit, and
    // DAT_0035529C makes sure a column of twenty bubbles lands at most one.
    void FUN_002ea7f0_pop_and_hit(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
    {
      entity.state60 = 2;
      entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 | 0x2000u);
      entity.radius54 = kFUN_002ea7f0_popRadius;
      entity.height58 = kFUN_002ea7f0_popHeight;
      entity.animationA0 = 2;
      if (DAT_0035529c_bubbleHitLatch() != 0 || entity.bubbleAttack1a8 == nullptr)
      {
        return;
      }
      if (FUN_002ef510_effect_hit_test(entity, slot, *entity.bubbleAttack1a8, environment) != 0)
      {
        DAT_0035529c_bubbleHitLatch() = 1;
      }
    }

    // Stage one's travel: flat speed along +0x5C, climbing at +0x1A0.
    void travel_stage_one(OriginalEntity &entity, std::uint32_t ticks)
    {
      const float travel = (entity.bubbleSpeed19c * static_cast<float>(ticks)) / 32000.0f;
      entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
      entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
      entity.desiredDeltaY38 += (entity.bubbleRise1a0 * static_cast<float>(ticks)) / 32000.0f;
    }

    // LAB_002EAB34, stage two's travel. The horizontal speed is scaled by how
    // much of the second timer is left, so the bubble stalls as it surfaces,
    // while the climb is a flat five.
    void travel_stage_two(OriginalEntity &entity, std::int16_t remaining, std::uint32_t ticks)
    {
      const float fraction =
          entity.bubbleLife1a4 != 0
              ? static_cast<float>(remaining) / static_cast<float>(entity.bubbleLife1a4)
              : 0.0f;
      const float travel =
          (entity.bubbleSpeed19c * fraction * static_cast<float>(ticks)) / 32000.0f;
      entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
      entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
      entity.desiredDeltaY38 += (static_cast<float>(ticks) * kFUN_002ea7f0_stage2Climb) / 32000.0f;
    }

    std::int16_t step_timer(OriginalEntity &entity, std::uint32_t ticks)
    {
      const auto remaining = static_cast<std::int16_t>(
          static_cast<std::int16_t>(entity.fadeRamp62) - static_cast<std::int16_t>(ticks & 0xFFFFu));
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      return remaining;
    }
  } // namespace

  std::uint8_t &DAT_0035529c_bubbleHitLatch()
  {
    static std::uint8_t value = 0;
    return value;
  }

  void FUN_002ea7f0_bubble(OriginalEntity &entity,
                           std::size_t slot,
                           const ActorEnvironment &environment)
  {
    const std::uint32_t ticks = environment.frameTicks;

    // --- state 0: the bubble the ceiling stops, and the only one that hits ---
    if (entity.state60 == 0)
    {
      if (entity.bubblePhase198 == 0)
      {
        if (step_timer(entity, ticks) < 0)
        {
          enter_stage_two(entity, environment);
          return;
        }
        if ((entity.collisionFlags0c & kFUN_002ea7f0_solidMask) != 0)
        {
          FUN_00265ec0_destroy_entity(slot, environment);
          return;
        }
        if ((entity.collisionFlags0c & kFUN_002ea7f0_entityMask) != 0)
        {
          FUN_002ea7f0_pop_and_hit(entity, slot, environment);
          return;
        }
        travel_stage_one(entity, ticks);
        return;
      }
      if (entity.bubblePhase198 != 1)
      {
        return;
      }
      const std::int16_t remaining = step_timer(entity, ticks);
      if (remaining < 0)
      {
        FUN_002ea7f0_pop_and_hit(entity, slot, environment);
        return;
      }
      travel_stage_two(entity, remaining, ticks);
      return;
    }

    // --- state 1: the same machine, but nothing it runs into hurts anyone ---
    if (entity.state60 == 1)
    {
      if (entity.bubblePhase198 == 0)
      {
        if (step_timer(entity, ticks) < 0)
        {
          enter_stage_two(entity, environment);
          return;
        }
        if ((entity.collisionFlags0c & kFUN_002ea7f0_solidMask) != 0)
        {
          FUN_00265ec0_destroy_entity(slot, environment);
          return;
        }
        if ((entity.collisionFlags0c & kFUN_002ea7f0_entityMask) == 0)
        {
          travel_stage_one(entity, ticks);
          return;
        }
      }
      else
      {
        if (entity.bubblePhase198 != 1)
        {
          return;
        }
        const std::int16_t remaining = step_timer(entity, ticks);
        if (remaining >= 0)
        {
          travel_stage_two(entity, remaining, ticks);
          return;
        }
      }
      // Both fall-throughs land here: animation 1, state 2, and no hit.
      entity.animationA0 = 1;
      entity.state60 = 2;
      return;
    }

    // --- states 2 and 4: the pop, held until its clip ends ---
    if (entity.state60 == 2 || entity.state60 == 4)
    {
      if ((entity.flags06 & 1u) != 0)
      {
        FUN_00265ec0_destroy_entity(slot, environment);
      }
    }
  }

  void FUN_002eac48_spawn_bubble(const OriginalEntity &source,
                                 std::int16_t mode,
                                 const orphen::ported::psm2::Vec3 &position,
                                 std::uint32_t spread,
                                 const orphen::ported::resource::HitParameters *attack,
                                 const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t slot =
        pool.FUN_00265e28_allocate_and_initialize(kBubbleTypeId, *environment.descriptors);
    if (slot >= pool.slotCount())
    {
      return;
    }
    OriginalEntity &bubble = pool.slot(slot);
    spread &= 0xFFu;

    bubble.groundHeight4c = source.groundHeight4c;
    bubble.previousGroundHeight50 = source.previousGroundHeight50;
    bubble.attackPower12c = source.attackPower12c; // +0x12C
    bubble.halfword04 = static_cast<std::uint16_t>(bubble.halfword04 | 0x19u);
    bubble.bubbleAttack1a8 = attack;
    bubble.positionX20 = position.x;
    bubble.positionZ24 = position.y;
    bubble.positionY28 = position.z;

    // Modes 0 and 1 share everything but the state and which pair of turn
    // words the heading roll reads. The sign is rolled first and the magnitude
    // second, and `spread * 20` is the divisor -- which is why the original
    // traps on a zero spread rather than guarding it.
    const auto seedRise = [&](float turnMinus, float turnPlus, std::uint16_t state)
    {
      bubble.animationA0 = 0;
      const std::uint32_t divisor = spread * 0x14u;
      float heading = source.facingRadians5c;
      if (divisor != 0)
      {
        const bool plus = (static_cast<std::uint32_t>(roll(environment)) & 1u) != 0;
        const auto magnitude = static_cast<float>(
            static_cast<std::uint32_t>(roll(environment)) % divisor);
        heading = plus ? source.facingRadians5c + (magnitude * turnPlus) / 360.0f
                       : source.facingRadians5c - (magnitude * turnMinus) / 360.0f;
      }
      bubble.facingRadians5c = heading;
      bubble.bubbleSpeed19c = static_cast<float>((roll(environment) % 0x14) + 10);
      bubble.bubbleRise1a0 = static_cast<float>(roll(environment) % 5);
      const float scale = static_cast<float>((roll(environment) % 0x96) + 0x32) / 100.0f;
      bubble.fadeRamp62 = kFUN_002eac48_life;
      bubble.scale14c = scale;
      bubble.scaleZ150 = scale;
      // FUN_0023A620(entity, its own +0xA0, 5).
      bubble.stateResetA4 = 0;
      bubble.timelineCursorA8 = static_cast<std::uint16_t>((roll(environment) % 5) * 2);
      bubble.state60 = state;
    };

    if (mode == 1)
    {
      seedRise(kFGpffffaac8_turnMinus1, kFGpffffaac4_turnPlus1, 1);
      return;
    }
    if (mode < 2)
    {
      if (mode == 0)
      {
        seedRise(kFGpffffaac0_turnMinus0, kFGpffffaabc_turnPlus0, 0);
      }
      return;
    }
    if (mode != 2 && mode != 4)
    {
      return;
    }
    // Mode 2 writes animation 1 and double scale, and then the tail both modes
    // share overwrites the animation with 2. Kept in that order because that is
    // what the original does.
    if (mode == 2)
    {
      bubble.animationA0 = 1;
      bubble.scale14c = 2.0f;
      bubble.state60 = 2;
      bubble.scaleZ150 = 2.0f;
    }
    bubble.state60 = 2;
    bubble.scale14c = 2.0f;
    bubble.scaleZ150 = 2.0f;
    bubble.animationA0 = 2;
  }

} // namespace orphen::ported::entity
