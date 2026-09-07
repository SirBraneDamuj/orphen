#include "ported/entity/original_water_splash.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    // DAT_00354A3C: the water surface, -0.7. Every 0x10D is put at exactly this
    // height whatever made it, which is why neither spawner passes one.
    inline constexpr float kDAT_00354a3c_waterSurface = -0.699999988079071f;
    // DAT_00354A40 / 48 / 4C: two turns, and the 135 degrees the wading fan
    // starts behind the facing. The two turns are separate words.
    inline constexpr float kDAT_00354a40_turn = 6.28318548202515f;
    inline constexpr float kDAT_00354a48_turn = 6.28318548202515f;
    inline constexpr float kDAT_00354a4c_fanStart = 2.35619449615479f;
    // DAT_00354A44: the wading spray needs the entity at or below -0.7 as well.
    inline constexpr float kDAT_00354a44_wadeLine = -0.699999988079071f;

    // FUN_002EB180's drift, ten units per 32000 ticks.
    inline constexpr float kFUN_002eb180_driftSpeed = 10.0f;

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    // FUN_002EB278(scaleA, scaleB, heading, source, xy, mode, lifeTicks).
    //
    // The source entity is read for two things only -- its ground height at
    // +0x4C and +0x50 -- and never for its position; the caller has already
    // worked that out. A negative life gives a random one, 0..99, which is what
    // spreads the wading spray out.
    void FUN_002eb278_spawn(float scaleA,
                            float scaleB,
                            float heading,
                            const OriginalEntity &source,
                            float x,
                            float y,
                            std::uint16_t mode,
                            std::int16_t lifeTicks,
                            const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t slot =
          pool.FUN_00265e28_allocate_and_initialize(kWaterSplashTypeId, *environment.descriptors);
      if (slot >= pool.slotCount())
      {
        // FUN_00265E28 answering 0 is the pool being full; the original just
        // drops the particle and so does this.
        return;
      }

      OriginalEntity &splash = pool.slot(slot);
      splash.groundHeight4c = source.groundHeight4c;
      splash.previousGroundHeight50 = source.previousGroundHeight50;
      splash.facingRadians5c = heading;
      splash.scaleZ150 = scaleA;
      splash.scale14c = scaleB;
      splash.positionX20 = x;
      splash.positionZ24 = y;
      splash.positionY28 = kDAT_00354a3c_waterSurface;

      // FUN_0023A620(entity, 1, 4): animation 1, the state timer cleared, and a
      // random start two frames into a four-frame clip so a ring of them is not
      // in lockstep.
      splash.stateResetA4 = 0;
      splash.animationA0 = 1;
      const std::uint32_t roll = environment.random ? environment.random() : 0;
      splash.timelineCursorA8 =
          static_cast<std::uint16_t>((static_cast<std::int32_t>(roll) % 4) * 2);

      std::int16_t life = lifeTicks;
      if (life < 0)
      {
        const std::uint32_t lifeRoll = environment.random ? environment.random() : 0;
        life = static_cast<std::int16_t>(static_cast<std::int32_t>(lifeRoll) % 100);
      }
      splash.fadeRamp62 = static_cast<std::uint16_t>(static_cast<std::int32_t>(life) << 5);

      splash.splashScaleB1a0 = scaleB;
      splash.splashLife198 = static_cast<std::int16_t>(splash.fadeRamp62);
      splash.state60 = static_cast<std::uint16_t>(mode & 0xFFu);
      splash.splashScaleA19c = scaleA;
    }
  } // namespace

  void FUN_002eb180_water_splash(OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
  {
    // The whole animation is the timer: both scales are the spawn scale times
    // the fraction of the life left, so the quad shrinks into nothing and is
    // gone the frame the timer reaches zero.
    const auto remaining = static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62));
    const auto total = static_cast<float>(entity.splashLife198);
    const float fraction = total != 0.0f ? remaining / total : 0.0f;
    entity.scaleZ150 = entity.splashScaleA19c * fraction;
    entity.scale14c = entity.splashScaleB1a0 * fraction;

    const std::int16_t stepped = FUN_0023a678_countdown(
        static_cast<std::int16_t>(entity.fadeRamp62), environment.frameTicks);
    entity.fadeRamp62 = static_cast<std::uint16_t>(stepped);
    if (stepped == 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
      return;
    }

    // Mode 1 -- the wading spray -- crawls outward along its own heading. Mode
    // 0, the burst, stays where it was put.
    if (entity.state60 != 0)
    {
      const float travel = (static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
                            kFUN_002eb180_driftSpeed) /
                           32000.0f;
      entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
      entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
    }
  }

  void FUN_002eb398_splash_ring(float radius,
                                float scaleA,
                                float scaleB,
                                const OriginalEntity &at,
                                int count,
                                std::int16_t lifeTicks,
                                const ActorEnvironment &environment)
    {
    if (count <= 0)
    {
      return;
    }
    // The ring starts at a random whole degree and steps a whole turn divided
    // by the count. Both the start and the step go through the same
    // degrees-to-radians conversion the original spells out longhand.
    const std::uint32_t roll = environment.random ? environment.random() : 0;
    float heading =
        (static_cast<float>(static_cast<std::int32_t>(roll) % 0x168) * kDAT_00354a40_turn) / 360.0f;
    const float step = static_cast<float>(0x168 / count) * kDAT_00354a40_turn;

    for (int index = 0; index < count; ++index)
    {
      const float x = at.positionX20 + radius * cos_of(heading);
      const float y = at.positionZ24 + radius * sin_of(heading);
      FUN_002eb278_spawn(scaleA, scaleB, heading, at, x, y, 0, lifeTicks, environment);
      heading += step / 360.0f;
    }
  }

  void FUN_002eb500_wade_spray(const OriginalEntity &at,
                               int count,
                               const ActorEnvironment &environment)
  {
    if (count <= 0)
    {
      return;
    }
    // Ninety degrees spread over `count`, starting 135 degrees off the facing --
    // so the spray comes off behind whatever is wading.
    const float step = (static_cast<float>(0x5A / count) * kDAT_00354a48_turn) / 360.0f;
    float heading = at.facingRadians5c + kDAT_00354a4c_fanStart;
    if (at.positionY28 > kDAT_00354a44_wadeLine)
    {
      return;
    }

    for (int index = 0; index < count; ++index)
    {
      const std::uint32_t roll = environment.random ? environment.random() : 0;
      const float distance = static_cast<float>(static_cast<std::int32_t>(roll) % 0x32 + 0x32) / 100.0f;
      const float x = at.positionX20 + distance * cos_of(heading);
      const float y = at.positionZ24 + distance * sin_of(heading);
      const float next = heading + step;
      FUN_002eb278_spawn(1.0f, 1.0f, heading, at, x, y, 1, -1, environment);
      heading = next;
    }
  }

} // namespace orphen::ported::entity
