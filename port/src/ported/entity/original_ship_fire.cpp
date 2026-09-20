#include "ported/entity/original_ship_fire.h"

#include "ported/entity/entity_pool.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // The gp constants the three handlers share, read straight out of the
    // executable's data at the addresses the gp offsets resolve to.
    //
    //   0x00354AA8  fGpffffab48  2*pi, the root's ring
    //   0x00354AAC  fGpffffab4c  0.2, how far above the water a ring link sits
    //   0x00354AB0  fGpffffab50  2*pi again, the puff shower's ring
    //   0x00354AB4  fGpffffab54  0.7, the smoke half's share of the root's scale
    //   0x00354AB8  fGpffffab58  0.7 again, the flame half's
    //   0x00354ABC  fGpffffab4c  2*pi a third time, FUN_002ED9A0's own
    //   0x00354AC0  fGpffffab50  0.2 again, FUN_002ED9A0's own
    //
    // Three copies of 2*pi and two of 0.2 is what the original ships; they are
    // kept apart here because they are separate words and a later slice may
    // find one of them is not what it looks like.
    // All three "2*pi" words hold **0x40C90FD8**, which is 6.283184 -- three
    // ULP short of float(2*pi). It is an authored six-digit constant, and
    // writing the mathematical one walks every ring here by a different step.
    inline constexpr float kFGpffffab48_rootRingTurn = 6.283184051513672f;
    inline constexpr float kFGpffffab4c_ringLift = 0.200000003f;
    inline constexpr float kFGpffffab50_showerTurn = 6.283184051513672f;
    inline constexpr float kFGpffffab54_smokeScale = 0.699999988f;
    inline constexpr float kFGpffffab58_flameScale = 0.699999988f;
    inline constexpr float kFUN_002ed9a0_ringTurn = 6.283184051513672f;
    inline constexpr float kFUN_002ed9a0_ringLift = 0.200000003f;

    // The literals in the instruction stream.
    inline constexpr float kDegreesPerTurn = 360.0f;
    inline constexpr float kScaleDivisor = 100.0f;      // lui 0x42C8
    inline constexpr float kRingScaleBase = 5.0f;       // lui 0x40A0
    inline constexpr float kFadeSpan = 124.0f;          // lui 0x42F8
    inline constexpr std::uint32_t kRingScaleRoll = 500;   // li 0x1F4
    inline constexpr std::uint32_t kChainScaleRoll = 100;
    inline constexpr std::uint32_t kPuffScaleRoll = 900;   // li 0x384
    inline constexpr std::uint32_t kPuffRadiusRoll = 5;    // li 0x5
    inline constexpr std::uint32_t kPuffAngleRoll = 360;   // li 0x168
    inline constexpr std::int32_t kPuffRadiusBase = 2;
    inline constexpr std::int32_t kShowerCount = 5;        // s2 = 4, `bgez` after
    inline constexpr std::uint8_t kRingDegreesPerStep = 0x78; // li 0x78, 120
    inline constexpr std::uint8_t kRingChainSeed = 2;
    inline constexpr std::uint8_t kRingDivisorSeed = 5;
    inline constexpr std::int16_t kSmokeFadeTicks = 0x0C80;
    inline constexpr std::int16_t kFlameFadeTicks = 0x0780;
    inline constexpr float kFUN_002ed9a0_fadeTicks = 2240.0f; // 0x8C0
    inline constexpr std::int32_t kFUN_002ed9a0_fadeLimit = 0x08C0;

    // FUN_00225C90's flags, the two this family reads. Bit 0x04 is "the entry
    // that was running expired on this frame" and bit 0x01 is "and that was the
    // last entry of the strip", which is what ends an effect.
    inline constexpr std::uint16_t kAnimationExpired06 = 0x0004;
    inline constexpr std::uint16_t kAnimationFinished06 = 0x0001;

    std::uint32_t roll(const ActorEnvironment &environment)
    {
      return environment.random ? environment.random() : 0u;
    }

    // FUN_0030BDB0, which is `__fixunssfsi`: truncate toward zero, and answer
    // zero for anything that is not a positive finite number. Only the negative
    // arm is reachable from here -- see the header's note on the last frame.
    std::uint8_t FUN_0030bdb0_truncate(float value)
    {
      if (!(value > 0.0f))
      {
        return 0;
      }
      return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value));
    }

    // `divu` then `mfhi`: the remainder is unsigned, so it is always in
    // [0, divisor) however the RNG's top bit fell. The `bltz` ladder around the
    // `cvt.s.w` in the disassembly is the compiler's u32-to-float sequence, not
    // a sign test on the result.
    std::uint32_t rollModulo(const ActorEnvironment &environment, std::uint32_t divisor)
    {
      return divisor == 0 ? 0u : roll(environment) % divisor;
    }

    // `(ring * 120) * 2pi / 360` -- degrees to radians the long way round,
    // which is how every ring in this file is spaced.
    float ringAngle(std::uint8_t ring, float turn)
    {
      return (static_cast<float>(static_cast<std::uint32_t>(ring) * kRingDegreesPerStep) * turn) /
             kDegreesPerTurn;
    }

    // The whole tail of FUN_002ED3E0: step the ramp, and stop it once it has
    // run past its length. `total` is re-read after the store on purpose.
    void stepFade(OriginalEntity &entity, std::uint32_t frameTicks)
    {
      if (entity.shipFireFade19c == 0)
      {
        return;
      }
      const std::int32_t stepped = static_cast<std::int32_t>(entity.fadeRamp62) +
                                   static_cast<std::int32_t>(frameTicks & 0xFFFFu);
      entity.fadeRamp62 = static_cast<std::uint16_t>(stepped);
      const auto elapsed = static_cast<std::int16_t>(stepped);
      if (entity.shipFireFade19c < elapsed)
      {
        entity.shipFireFade19c = 0;
      }

      // The reload. A zero here is the last frame, where the original divides
      // by zero, reaches -inf and gets 0 back from FUN_0030BDB0.
      const std::int16_t total = entity.shipFireFade19c;
      const std::uint8_t level =
          total == 0 ? 0u
                     : FUN_0030bdb0_truncate(
                           kFadeSpan - (static_cast<float>(elapsed) / static_cast<float>(total)) *
                                           kFadeSpan);
      entity.fadeLevel134 = static_cast<std::uint8_t>(level + 3);
    }

    OriginalEntity *spawn(const ActorEnvironment &environment,
                          std::int32_t typeId,
                          std::size_t &slotOut)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return nullptr;
      }
      const std::size_t slot =
          environment.entityPool->FUN_00265e28_allocate_and_initialize(typeId,
                                                                      *environment.descriptors);
      if (slot >= kEntitySlotCount)
      {
        return nullptr;
      }
      slotOut = slot;
      return &environment.entityPool->slot(slot);
    }
  } // namespace

  void FUN_002ed3e0_ship_fire(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment)
  {
    const float water = environment.DAT_003556fc_effectGroundZ;

    // :28-72. The one-shot, and only for the smoke half. A root or a flame half
    // still ticks +0x94 past zero on its first frame so the test never fires
    // again.
    if (entity.spawnParam94 == 0)
    {
      if (entity.state60 == 1)
      {
        std::size_t ringSlot = 0;
        OriginalEntity *ring = spawn(environment, kShipFireRingType, ringSlot);
        if (ring != nullptr)
        {
          const float scale =
              static_cast<float>(rollModulo(environment, kRingScaleRoll)) / kScaleDivisor +
              kRingScaleBase;
          ring->halfword08 = static_cast<std::uint16_t>(ring->halfword08 | 0x0080u);
          ring->scale14c = scale;
          ring->scaleZ150 = scale;
          ring->facingRadians5c = entity.facingRadians5c;
          ring->shipFireOriginX1a0 = entity.positionX20;
          ring->shipFireOriginY1a4 = entity.positionZ24;
          ring->shipFireRing19a = 0;
          ring->shipFireOriginZ1a8 = entity.positionY28;
          ring->shipFireChain198 = kRingChainSeed;
          ring->shipFireDivisor199 = kRingDivisorSeed;

          const auto radius =
              static_cast<float>(rollModulo(environment, ring->shipFireDivisor199));
          const float angle = ringAngle(ring->shipFireRing19a, kFGpffffab48_rootRingTurn);
          ring->positionX20 = ring->shipFireOriginX1a0 + radius * std::cos(angle);
          ring->positionZ24 = ring->shipFireOriginY1a4 + radius * std::sin(angle);
          ring->groundHeight4c = water;
          ring->previousGroundHeight50 = water;
          ring->positionY28 = water + kFGpffffab4c_ringLift;
        }
      }
      entity.spawnParam94 = static_cast<std::uint8_t>(entity.spawnParam94 + 1);
    }

    // :76-150. Once per animation cycle, on the frame the first timeline entry
    // expires.
    if (entity.timelineCursorA8 == 0 && (entity.flags06 & kAnimationExpired06) != 0)
    {
      if (entity.state60 == 1)
      {
        for (std::int32_t index = 0; index < kShowerCount; ++index)
        {
          std::size_t puffSlot = 0;
          OriginalEntity *puff = spawn(environment, kShipFirePuffType, puffSlot);
          if (puff == nullptr)
          {
            continue;
          }
          FUN_00225bc8_set_animation(*puff, 0);
          const float scale =
              static_cast<float>(rollModulo(environment, kPuffScaleRoll)) / kScaleDivisor;
          // Dead: the assignment from the parent's +0x08 below lands on top of
          // it. Kept because the original writes both.
          puff->halfword08 = static_cast<std::uint16_t>(puff->halfword08 | 0x0080u);
          puff->scale14c = scale;
          puff->scaleZ150 = scale;
          puff->facingRadians5c = entity.facingRadians5c;

          const auto radius = static_cast<float>(
              static_cast<std::int32_t>(rollModulo(environment, kPuffRadiusRoll)) +
              kPuffRadiusBase);
          const float angle =
              (static_cast<float>(rollModulo(environment, kPuffAngleRoll)) *
               kFGpffffab50_showerTurn) /
              kDegreesPerTurn;
          puff->positionX20 = entity.positionX20 + radius * std::cos(angle);
          puff->groundHeight4c = water;
          puff->halfword08 = entity.halfword08;
          puff->positionY28 = water;
          puff->positionZ24 = entity.positionZ24 + radius * std::sin(angle);
          puff->previousGroundHeight50 = water;
        }
      }

      // :133. The original jumps to the tail here when it is not the root, so
      // only a state-0 entity ever splits itself in two.
      if (entity.state60 == 0)
      {
        struct Half
        {
          std::uint16_t animation;
          std::uint16_t state;
          float scale;
        };
        const Half halves[2] = {{2, 1, kFGpffffab54_smokeScale},
                                {3, 2, kFGpffffab58_flameScale}};
        for (const Half &half : halves)
        {
          std::size_t halfSlot = 0;
          OriginalEntity *child = spawn(environment, kShipFireRootType, halfSlot);
          if (child == nullptr)
          {
            continue;
          }
          FUN_00225bc8_set_animation(*child, half.animation);
          const float scale = entity.scale14c * half.scale;
          child->state60 = half.state;
          child->halfword08 = static_cast<std::uint16_t>(child->halfword08 | 0x0080u);
          child->scale14c = scale;
          child->scaleZ150 = scale;
          child->positionX20 = entity.positionX20;
          child->halfword08 = entity.halfword08;
          child->groundHeight4c = water;
          child->positionZ24 = entity.positionZ24;
          child->positionY28 = water;
          child->previousGroundHeight50 = water;
        }
      }
    }

    // LAB_002ED84C. Both halves arm their fade off a fixed timeline entry of
    // their own animation, and the root -- animation 0 -- arms none, so it goes
    // when its strip ends rather than by dissolving.
    const std::uint16_t flags = entity.flags06;
    if (entity.animationA0 == 2 && entity.timelineCursorA8 == 2 &&
        (flags & kAnimationExpired06) != 0)
    {
      entity.fadeRamp62 = 0;
      entity.shipFireFade19c = kSmokeFadeTicks;
    }
    if (entity.animationA0 == 3 && entity.timelineCursorA8 == 4 &&
        (flags & kAnimationExpired06) != 0)
    {
      entity.fadeRamp62 = 0;
      entity.shipFireFade19c = kFlameFadeTicks;
    }

    stepFade(entity, environment.frameTicks);

    if ((flags & kAnimationFinished06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  void LAB_002ed980_ship_fire_puff(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    if ((entity.flags06 & kAnimationFinished06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  void FUN_002ed9a0_ship_fire_ring(OriginalEntity &entity,
                                   std::size_t slot,
                                   const ActorEnvironment &environment)
  {
    const float water = environment.DAT_003556fc_effectGroundZ;

    // :20-64. The same "first entry of the strip just expired" beat the root
    // uses, and the chain counter is spent on it.
    if (entity.timelineCursorA8 == 0 && (entity.flags06 & kAnimationExpired06) != 0)
    {
      const auto remaining =
          static_cast<std::int8_t>(static_cast<std::int8_t>(entity.shipFireChain198) - 1);
      entity.shipFireChain198 = static_cast<std::uint8_t>(remaining);
      if (remaining > 0)
      {
        std::size_t nextSlot = 0;
        OriginalEntity *next = spawn(environment, kShipFireRingType, nextSlot);
        if (next != nullptr)
        {
          next->groundHeight4c = water;
          next->previousGroundHeight50 = water;
          const float scale =
              static_cast<float>(rollModulo(environment, kChainScaleRoll)) / kScaleDivisor +
              kRingScaleBase;
          next->halfword08 = static_cast<std::uint16_t>(next->halfword08 | 0x0080u);
          next->scale14c = scale;
          next->scaleZ150 = scale;
          next->facingRadians5c = entity.facingRadians5c;
          next->shipFireOriginX1a0 = entity.shipFireOriginX1a0;
          next->shipFireOriginY1a4 = entity.shipFireOriginY1a4;
          next->shipFireOriginZ1a8 = entity.shipFireOriginZ1a8;
          next->shipFireChain198 = entity.shipFireChain198;

          // The ring index is stepped on *this* link and then copied, so the
          // three links of a chain sit 120 degrees apart.
          const auto ring = static_cast<std::uint8_t>(entity.shipFireRing19a + 1);
          entity.shipFireRing19a = ring;
          next->shipFireRing19a = ring;
          next->shipFireDivisor199 = entity.shipFireDivisor199;

          const auto radius =
              static_cast<float>(rollModulo(environment, next->shipFireDivisor199));
          const float angle = ringAngle(next->shipFireRing19a, kFUN_002ed9a0_ringTurn);
          next->positionX20 = next->shipFireOriginX1a0 + radius * std::cos(angle);
          next->positionZ24 = next->shipFireOriginY1a4 + radius * std::sin(angle);
          next->positionY28 = water + kFUN_002ed9a0_ringLift;
        }
      }
    }

    // :68-86. This one's fade length is the literal 2240 rather than a field,
    // and +0x94 is the latch instead of +0x19C -- so there is no reload and no
    // division by zero on the last frame.
    const std::uint16_t flags = entity.flags06;
    if (entity.timelineCursorA8 == 2 && (flags & kAnimationExpired06) != 0)
    {
      entity.fadeRamp62 = 0;
      entity.spawnParam94 = 1;
    }
    if (entity.spawnParam94 != 0)
    {
      const std::int32_t stepped = static_cast<std::int32_t>(entity.fadeRamp62) +
                                   static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      entity.fadeRamp62 = static_cast<std::uint16_t>(stepped);
      if (static_cast<std::int16_t>(stepped) > kFUN_002ed9a0_fadeLimit)
      {
        entity.spawnParam94 = 0;
      }
      const std::uint8_t level = FUN_0030bdb0_truncate(
          kFadeSpan - (static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) /
                       kFUN_002ed9a0_fadeTicks) *
                          kFadeSpan);
      entity.fadeLevel134 = static_cast<std::uint8_t>(level + 3);
    }

    if ((flags & kAnimationFinished06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

} // namespace orphen::ported::entity
