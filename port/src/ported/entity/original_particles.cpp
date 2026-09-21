#include "ported/entity/original_particles.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_00354678. Bled off the horizontal speed once a frame, and also the
    // floor it stops at -- the test is `speed > this` before the subtract.
    constexpr float kDAT_00354678_speedDecay = 5.0e-05f;
    // DAT_00354690, pi/2. The burst fans out from the projectile's facing plus
    // a quarter turn, so the first spark leaves at right angles to the flight.
    constexpr float kDAT_00354690_burstBaseAngle = 1.57079637f;
    // DAT_00354694 / DAT_00354698 / DAT_0035469c.
    constexpr float kDAT_00354694_speedDivisor = 10000.0f;
    constexpr float kDAT_00354698_gravityDivisor = 100000.0f;
    constexpr float kDAT_0035469c_burstAngleStep = 0.0349065810f; // two degrees

    // DAT_003546b4 / DAT_003546b8, FUN_002d3320's two constants: the sideways
    // drift a rising spark takes each frame, and what divides the seed height
    // into a rise speed. 199/300000 is 0.00066 a tick, so the fastest spark
    // climbs about a fiftieth of a unit a frame.
    constexpr float kDAT_003546b4_dustDrift = 0.003000000026077032f;
    constexpr float kDAT_003546b8_dustRiseDivisor = 300000.0f;

    // DAT_00326708, read as `(group - 1) + weaponClass * 4`. Every row of it
    // holds the same four bone indices, so the class never changes the answer;
    // it is kept as a table because the original indexes it that way, and
    // because the six rows are per weapon class rather than decoration.
    constexpr std::size_t kDustBoneCount = 24;
    constexpr std::size_t kDAT_00326708_dustBones[kDustBoneCount] = {
        0x0E, 0x18, 0x04, 0x08, 0x0E, 0x18, 0x04, 0x08, 0x0E, 0x18, 0x04, 0x08,
        0x0E, 0x18, 0x04, 0x08, 0x0E, 0x18, 0x04, 0x08, 0x0E, 0x18, 0x04, 0x08,
    };

    // DAT_0035218c / DAT_00352188 / DAT_00352190: FUN_00216690 wraps an angle
    // into (-pi, pi], giving up after sixteen passes.
    constexpr float kTwoPi = 6.28318548f;
    float FUN_00216690_wrap_angle(float angle)
    {
      for (int pass = 0; pass < 16; ++pass)
      {
        if (angle > 3.14159274f)
        {
          angle -= kTwoPi;
        }
        else if (angle < -3.14159274f)
        {
          angle += kTwoPi;
        }
        else
        {
          return angle;
        }
      }
      return angle;
    }
  } // namespace

  void FUN_002d2348_spark_step(OriginalParticle &particle, std::uint32_t frameTicks)
  {
    // 0x002d2350. A free entry is skipped entirely -- it does not age.
    if (particle.alive1c == 0)
    {
      return;
    }

    // 0x002d2364. The timer is a 16-bit subtract and is allowed to go negative,
    // which is what makes the fade below run every frame once it expires.
    const std::int16_t remaining = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(particle.timer22) - static_cast<std::uint16_t>(frameTicks));
    particle.timer22 = remaining;
    if (remaining < 1)
    {
      // The alpha byte falls by two a frame; below one the particle is free.
      const int alpha = static_cast<int>(particle.colour18 >> 24) - 2;
      if (alpha < 1)
      {
        particle.alive1c = 0;
      }
      else
      {
        particle.colour18 =
            (particle.colour18 & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24);
      }
    }

    // 0x002d23bc. Note the two different tick scalings: the vertical integration
    // uses ticks/8 and the horizontal step uses ticks/2. They are not the same
    // quantity and the original does not treat them as one.
    const float ticks = static_cast<float>(static_cast<int>(frameTicks));
    const float verticalStep = ticks * 0.125f;
    const float horizontalStep = particle.speed14 * 0.5f * ticks;

    // The decay test reads the speed from *before* the step, so a particle
    // always gets one full-speed frame.
    const bool decaying = particle.speed14 > kDAT_00354678_speedDecay;

    particle.x00 += horizontalStep * std::cos(particle.heading28);
    particle.y04 += horizontalStep * std::sin(particle.heading28);

    // A ballistic arc, integrated properly: the position takes the old velocity
    // and half the acceleration term, and the velocity is updated from the same
    // old value. Reordering these changes the trajectory.
    const float velocity = particle.velocityZ10;
    particle.velocityZ10 = velocity - particle.gravity24 * verticalStep;
    particle.z08 += velocity * verticalStep -
                    particle.gravity24 * verticalStep * verticalStep * 0.5f;

    if (decaying)
    {
      particle.speed14 -= kDAT_00354678_speedDecay;
    }
  }

  void FUN_002d3320_game_over_dust_step(OriginalParticle &particle,
                                        std::size_t index,
                                        std::uint32_t frameTicks,
                                        const GameOverDustContext &context)
  {
    // 0x002d3334. The timer runs on every entry, alive or free. On a free one
    // it is the stagger before it may be seeded again, which is why
    // FUN_002d3290's -160..+448 spread matters to this behaviour and not to the
    // sparks.
    const std::int16_t remaining = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(particle.timer22) - static_cast<std::uint16_t>(frameTicks));
    particle.timer22 = remaining;

    const float ticks = static_cast<float>(static_cast<int>(frameTicks));

    if (particle.alive1c != 0)
    {
      // 0x002d3524. Alive: rise, and once the timer expires fade out instead.
      if (remaining >= 1)
      {
        particle.z08 += particle.velocityZ10 * ticks;
        // The drift heading is the *timer*, wrapped -- so a spark curls as it
        // ages and two sparks seeded a frame apart curl differently. It is not
        // a stored heading; +0x28 is never written by this behaviour.
        const float heading = FUN_00216690_wrap_angle(static_cast<float>(remaining) / 100.0f);
        particle.x00 += std::cos(heading) * kDAT_003546b4_dustDrift;
        particle.y04 += std::sin(heading) * kDAT_003546b4_dustDrift;
        return;
      }

      const int alpha = static_cast<int>(particle.colour18 >> 24) - 8;
      if (alpha < 0)
      {
        particle.alive1c = 0;
        // 0..9 frames before the entry is eligible again.
        const int stagger =
            context.FUN_00216868_random ? static_cast<int>(context.FUN_00216868_random() % 10) : 0;
        particle.timer22 = static_cast<std::int16_t>(stagger << 5);
        return;
      }
      particle.colour18 =
          (particle.colour18 & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24);
      // Half speed while fading: the spark slows as it goes out.
      particle.z08 += particle.velocityZ10 * 0.5f * ticks;
      return;
    }

    // 0x002d3358. Free, and the stagger has run out.
    if (remaining >= 1)
    {
      return;
    }
    if (!context.FUN_00216868_random || !context.FUN_0020dc88_bone_point)
    {
      return;
    }

    // 0x002d3368. The live population, and which emitter this entry belongs to.
    // Entries 0..999 come off bone 0 with a wide spread; 1000 and up split into
    // groups of a hundred that ride a named bone -- and their limit is a tenth
    // of the first, so at the ordinary scale of 1.0 none of them ever seed.
    int group = 0;
    int limit = static_cast<int>(context.DAT_0058bffc_leadScale * 1000.0f);
    if (index > 999)
    {
      group = static_cast<int>(index / 100);
      limit /= 10;
    }
    if (static_cast<int>(index) >= limit)
    {
      return;
    }

    const auto &random = context.FUN_00216868_random;
    std::size_t bone = 0;
    orphen::ported::psm2::Vec3 offset{};
    if (group == 0)
    {
      offset.x = static_cast<float>(static_cast<int>(random() % 300) - 0x96) / 1000.0f;
      offset.z = static_cast<float>(static_cast<int>(random() % 800) - 500) / 1000.0f;
    }
    else
    {
      offset.x = static_cast<float>(static_cast<int>(random() % 10) - 5) / 1000.0f;
      offset.z = static_cast<float>(static_cast<int>(random() % 20) - 10) / 1000.0f;
      const std::size_t entry =
          static_cast<std::size_t>(group - 1) +
          static_cast<std::size_t>(context.FUN_002298d0_weaponClass) * 4u;
      bone = kDAT_00326708_dustBones[entry % kDustBoneCount];
    }

    // 50..199, and it is both the local rise of the seed point and the speed.
    const float rise = static_cast<float>(static_cast<int>(random() % 0x96) + 0x32);
    offset.y = rise / 1000.0f;

    const orphen::ported::psm2::Vec3 world = context.FUN_0020dc88_bone_point(bone, offset);
    particle.x00 = world.x;
    particle.y04 = world.y;
    particle.z08 = world.z;
    particle.velocityZ10 = rise / kDAT_003546b8_dustRiseDivisor;

    // 15..74 frames of rise before the fade starts.
    particle.timer22 =
        static_cast<std::int16_t>((static_cast<int>(random() % 0x3C) + 0xF) * 0x20);
    // Four in five are the blue of DAT_00806428 -- r 0x28, g 0x64, b 0x80 in
    // the GS byte order -- and the rest are near-white.
    particle.colour18 =
        (static_cast<int>(random() % 1000) < 800) ? 0x00806428u : 0x00F8F8F8u;
    particle.colour18 |= ((random() & 0x3Fu) + 0xC0u) << 24;
    particle.widthUnits1e = static_cast<std::int16_t>((random() & 1u) + 1u);
    particle.alive1c = 1;
    particle.heightUnits20 = static_cast<std::int16_t>((random() & 1u) + 1u);
  }

  void ParticlePool::FUN_002d3290_reset(const std::function<std::uint32_t()> &random)
  {
    for (auto &particle : particles_)
    {
      particle = OriginalParticle{};
      // FUN_002d3290:16. ((rand % 20) - 5) * 32 ticks, so -160..+448.
      const int stagger = random ? static_cast<int>(random() % 0x14) : 0;
      particle.timer22 = static_cast<std::int16_t>((stagger - 5) * 0x20);
    }
    behaviour_ = ParticleBehaviour::None;
  }

  void ParticlePool::FUN_002d36f8_install_game_over_dust(
      const std::function<std::uint32_t()> &random)
  {
    FUN_002d3290_reset(random);
    behaviour_ = ParticleBehaviour::FUN_002d3320_gameOverDust;
  }

  void ParticlePool::FUN_002d3218_step(std::uint32_t frameTicks,
                                       const GameOverDustContext *dust)
  {
    // FUN_002d3218:8. A null behaviour skips the whole pool, alive or not.
    if (behaviour_ == ParticleBehaviour::FUN_002d2348_sparks)
    {
      for (auto &particle : particles_)
      {
        FUN_002d2348_spark_step(particle, frameTicks);
      }
      return;
    }
    if (behaviour_ == ParticleBehaviour::FUN_002d3320_gameOverDust && dust != nullptr)
    {
      std::size_t index = 0;
      for (auto &particle : particles_)
      {
        FUN_002d3320_game_over_dust_step(particle, index++, frameTicks, *dust);
      }
    }
  }

  std::size_t ParticlePool::FUN_002d2470_spawn_impact_burst(
      const OriginalEntity &source, std::size_t sourceSlot,
      const std::function<std::uint32_t()> &random)
  {
    if (!random)
    {
      return 0;
    }

    int remaining = 100;
    float angle = kDAT_00354690_burstBaseAngle;
    std::size_t seeded = 0;

    for (std::size_t index = 0; index < kCount; ++index)
    {
      OriginalParticle &particle = particles_[index];
      if (particle.alive1c == 0)
      {
        // 0x002d2840. Red is all or nothing on a coin flip; green and blue are
        // each a random 0xC0..0xFF. So half the shower is white and half is
        // cyan, and none of it is dim.
        const std::uint32_t red = (static_cast<int>(random() % 1000) > 500) ? 0x00u : 0xFFu;
        const std::uint32_t green = (random() & 0x3Fu) | 0xC0u;
        const std::uint32_t blue = (random() & 0x3Fu) | 0xC0u;

        particle.x00 = source.positionX20;
        particle.y04 = source.positionZ24;
        particle.z08 = source.positionY28;
        particle.colour18 = (blue << 16) | (green << 8) | red | 0xE0000000u;

        // 0.0005 or 0.0015 -- bit 1 of the draw, not bit 0.
        particle.velocityZ10 = static_cast<float>((random() & 2u) + 1u) / 2000.0f;
        particle.speed14 =
            static_cast<float>(static_cast<int>(random() % 10) + 5) / kDAT_00354694_speedDivisor;

        // Width and height take the same value, 1 or 3, so a spark is either
        // small or three times as big with nothing in between.
        const std::int16_t units = static_cast<std::int16_t>((random() & 2u) + 1u);
        particle.alive1c = 1;
        particle.widthUnits1e = units;
        particle.heightUnits20 = units;

        // 30 to 59 frames before the fade starts.
        particle.timer22 =
            static_cast<std::int16_t>((static_cast<int>(random() % 0x1E) + 0x1E) * 0x20);

        // The fan. `angle` advances once per particle *seeded*, not once per
        // slot examined, so a burst landing in a fragmented pool still gets an
        // even spread.
        particle.heading28 = FUN_00216690_wrap_angle(source.facingRadians5c + angle);
        particle.gravity24 =
            static_cast<float>(static_cast<int>(random() % 5) + 1) / kDAT_00354698_gravityDivisor;
        particle.ownerSlot2c = static_cast<std::int16_t>(sourceSlot);

        angle += kDAT_0035469c_burstAngleStep;
        --remaining;
        ++seeded;
      }

      if (remaining == 0)
      {
        break;
      }
    }

    behaviour_ = ParticleBehaviour::FUN_002d2348_sparks;
    return seeded;
  }

  std::size_t ParticlePool::aliveCount() const
  {
    std::size_t alive = 0;
    for (const auto &particle : particles_)
    {
      if (particle.alive1c != 0)
      {
        ++alive;
      }
    }
    return alive;
  }

} // namespace orphen::ported::entity
