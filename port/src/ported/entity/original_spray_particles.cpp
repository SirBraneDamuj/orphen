#include "ported/entity/original_spray_particles.h"

#include "ported/render/original_view_projection.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // DAT_003523AC, the full turn the spawner rolls a heading out of. The same
    // authored 6.283184 every other ring stepper uses, three ULP short of
    // float(2*pi); fGpffff844C, which FUN_0021E808 reads on the mode-2 restart,
    // is a second copy of it at 0x003523BC.
    inline constexpr float kDAT_003523ac_turn = 6.283184051513672f;
    // DAT_003523B0, the half turn the billboard adds to the camera yaw.
    inline constexpr float kDAT_003523b0_halfPi = 1.570796012878418f;
    // DAT_003523B4 and DAT_003523B8: the per-tick heading step (one degree) and
    // the per-tick radial step. Both are divided by 32 at use, so they are per
    // *frame* at the nominal 32 ticks.
    inline constexpr float kDAT_003523b4_degree = 0.017453288659453392f;
    inline constexpr float kDAT_003523b8_radialStep = 0.004999999888241291f;
    inline constexpr float kTickScale = 0.03125f;

    // DAT_00315838..DAT_00315854. Four vectors of (x, 0, z), read out of the
    // ELF: a flat 0.03-unit square. FUN_0021E5E0 stages them with lane Y
    // explicitly zeroed before FUN_00218EB0 turns them.
    inline constexpr float kDAT_00315838_corners[4][2] = {
        {-0.014999999664723873f, 0.014999999664723873f},
        {-0.014999999664723873f, -0.014999999664723873f},
        {0.014999999664723873f, -0.014999999664723873f},
        {0.014999999664723873f, 0.014999999664723873f}};

    // FUN_00218EB0, one point at a time -- the same row-vector transform the
    // hit sparks use, with the matrix's row 3 as the translation.
    Vec3 FUN_00218eb0_transform(const Vec3 &point, const Matrix4 &matrix)
    {
      return {point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) +
                  point.z * matrix.at(2, 0) + matrix.at(3, 0),
              point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) +
                  point.z * matrix.at(2, 1) + matrix.at(3, 1),
              point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) +
                  point.z * matrix.at(2, 2) + matrix.at(3, 2)};
    }

    std::uint32_t roll(const std::function<std::uint32_t()> &random)
    {
      return random ? random() : 0u;
    }

    // FUN_0030BD20 and FUN_0030BDB0, both plain truncations toward zero.
    std::int16_t truncate16(float value) { return static_cast<std::int16_t>(value); }
    int truncate32(float value) { return static_cast<int>(value); }
  } // namespace

  std::array<Vec3, 4> FUN_0021e808_build_corners(const SprayParticleDraw &particle,
                                                 float cameraYaw)
  {
    namespace render = orphen::ported::render;

    // FUN_0021E5E0:0x0021E60C. FUN_0020BC78 copies the shared scratch matrix at
    // DAT_00342828 -- whose rows 2 and 3 are left identity by everything that
    // writes it -- and FUN_0020BAE0 then overwrites rows 0 and 1 with the turn.
    // The net of the pair is a bare Z rotation, so building it from the
    // identity is the same matrix, not an approximation of it.
    Matrix4 faceCamera = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(faceCamera, -cameraYaw - kDAT_003523b0_halfPi);

    std::array<Vec3, 4> corners{};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      const Vec3 local{kDAT_00315838_corners[corner][0], 0.0f,
                       kDAT_00315838_corners[corner][1]};
      const Vec3 turned = FUN_00218eb0_transform(local, faceCamera);
      corners[corner] = {particle.x + turned.x, particle.y + turned.y, particle.z + turned.z};
    }
    return corners;
  }

  void SprayParticlePool::FUN_0021e540_reset()
  {
    for (SprayParticle &particle : particles_)
    {
      particle = SprayParticle{};
    }
    draws_.clear();
    live_ = 0;
    gate_ = false;
  }

  void SprayParticlePool::FUN_0021e088_spawn(float rise, float x, float y, float z,
                                             int count,
                                             int speedRange,
                                             std::int16_t lifeUnit,
                                             std::int8_t mode,
                                             std::uint32_t colour,
                                             std::uint32_t frameTicks,
                                             const std::function<std::uint32_t()> &random)
  {
    // FUN_0021E088:1-9. Both floors are applied before anything is rolled, and
    // the life unit's is **10**, not 1.
    int lifeUnits = static_cast<int>(lifeUnit);
    if (lifeUnits < 1)
    {
      lifeUnits = 10;
    }
    int speed = 1;
    if (speedRange > 0)
    {
      speed = speedRange;
    }

    if (count > 0)
    {
      // The allocation cursor is reset to the base of the pool on entry and
      // then walks **forward across the whole burst** -- it is not reset per
      // particle, so a burst of N fills N consecutive free slots. The original
      // caps the scan at 2000 steps from wherever it stands, which lets it read
      // past the end of the pool into the next heap allocation when the pool is
      // nearly full; stopping at the end instead is the one deviation here, and
      // it changes nothing a run can reach without first filling all 2000.
      std::size_t cursor = 0;
      for (int spawned = 0; spawned < count; ++spawned)
      {
        int steps = 0;
        while (cursor < kCount && particles_[cursor].alive() && steps <= 1999)
        {
          ++steps;
          ++cursor;
        }
        if (cursor >= kCount || steps > 1999)
        {
          // FUN_0021E088:0x0021E2A0 returns outright, and **without** raising
          // the gate.
          return;
        }

        SprayParticle &particle = particles_[cursor];
        particle.colour28 = colour;
        particle.anchorX0c = x;
        particle.anchorY10 = y;
        particle.anchorZ14 = z;
        particle.speedRange20 = static_cast<std::int16_t>(speed);
        particle.rise1c = rise * static_cast<float>(frameTicks) * kTickScale;

        // Modes 0 and 2 roll a life; mode 1 takes a fixed one and so uses one
        // fewer random number than they do. Every other mode value sets neither
        // a life nor a direction and leaves both as the slot found them.
        //
        // Every divide in the function is `divu`, so the remainders are
        // unsigned however negative FUN_00216868's word looks as an int. The
        // life is built in 32 bits and only truncated by the halfword store.
        const bool launched = (mode == 0 || mode == 1 || mode == 2);
        if (mode == 0 || mode == 2)
        {
          const std::uint32_t units = roll(random) % static_cast<std::uint32_t>(lifeUnits);
          particle.life24 = static_cast<std::int16_t>((units + 1u) << 5);
        }
        else if (mode == 1)
        {
          particle.life24 = static_cast<std::int16_t>(static_cast<std::uint32_t>(lifeUnits) << 5);
        }

        if (launched)
        {
          const float degrees =
              static_cast<float>(roll(random) % 0x168u) * kDAT_003523ac_turn;
          const float radians = degrees / 360.0f;
          particle.offsetZ08 = static_cast<float>(roll(random) % 100u) / 100.0f;
          const float launchSpeed =
              static_cast<float>(roll(random) %
                                 static_cast<std::uint32_t>(particle.speedRange20)) /
              100.0f;
          particle.offsetX00 = launchSpeed * std::cos(radians);
          particle.offsetY04 = launchSpeed * std::sin(radians);
        }

        particle.mode2c = mode;
        particle.age22 = 0;
        ++live_;
        particle.heading18 =
            (static_cast<float>(roll(random) % 0x168u) * kDAT_003523ac_turn) / 360.0f;
      }
    }

    // FUN_0021E088's tail raises the gate even when the count was zero, and
    // only the full-pool return above skips it.
    gate_ = true;
  }

  void SprayParticlePool::FUN_0021e5e0_step(std::uint32_t frameTicks,
                                            const std::function<std::uint32_t()> &random)
  {
    draws_.clear();

    // FUN_0021E5E0:8. No live particles, or the gate down, and the whole walk
    // is skipped -- nothing steps and nothing draws.
    if (live_ <= 0 || !gate_)
    {
      return;
    }

    const float ticks = static_cast<float>(frameTicks);
    const float headingStep = ticks * kDAT_003523b4_degree * kTickScale;
    const float radialStep = ticks * kDAT_003523b8_radialStep * kTickScale;

    for (SprayParticle &particle : particles_)
    {
      // FUN_0021E808:1. A dead slot is still copied through the scratchpad and
      // written straight back, so it is a no-op rather than a skip.
      if (!particle.alive())
      {
        continue;
      }

      const float startAge = static_cast<float>(particle.age22);
      const float steppedAge = startAge + ticks;

      if (steppedAge <= static_cast<float>(particle.life24))
      {
        particle.offsetZ08 += particle.rise1c;
        particle.heading18 += headingStep;
        // Modes 0 and 2 spiral; mode 1 and anything else only rises. The two
        // branches are separate tests in the original rather than one compare,
        // and both read the heading **after** the step above.
        if (particle.mode2c == 0 || particle.mode2c == 2)
        {
          particle.offsetX00 += radialStep * std::cos(particle.heading18);
          particle.offsetY04 += radialStep * std::sin(particle.heading18);
        }

        // The fade reads +0x22, the age as it stood at the start of the frame,
        // not the value about to be written back.
        const int alpha = truncate32(
            255.0f - (static_cast<float>(particle.age22) / static_cast<float>(particle.life24)) *
                         255.0f);
        draws_.push_back(SprayParticleDraw{particle.anchorX0c + particle.offsetX00,
                                           particle.anchorY10 + particle.offsetY04,
                                           particle.anchorZ14 + particle.offsetZ08,
                                           particle.colour28, alpha});
        particle.age22 = truncate16(steppedAge);
        continue;
      }

      // Out of life. A mode-2 particle restarts from the anchor with a fresh
      // direction and is **not** drawn on the frame it wraps; every other mode
      // frees the slot, and the last one out lowers the gate.
      if (particle.mode2c == 2)
      {
        particle.heading18 = 0.0f;
        const float radians =
            (static_cast<float>(roll(random) % 0x168u) * kDAT_003523ac_turn) / 360.0f;
        const float launchSpeed =
            static_cast<float>(roll(random) %
                               static_cast<std::uint32_t>(particle.speedRange20)) /
            100.0f;
        particle.offsetZ08 = 0.0f;
        particle.offsetX00 = launchSpeed * std::cos(radians);
        particle.offsetY04 = launchSpeed * std::sin(radians);
        particle.age22 = 0;
        continue;
      }

      particle.mode2c = -1;
      --live_;
      if (live_ < 1)
      {
        gate_ = false;
      }
      particle.age22 = truncate16(steppedAge);
    }
  }

} // namespace orphen::ported::entity
