#include "ported/entity/original_fountain_particles.h"

#include "ported/render/original_view_projection.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // fGpffff8458, the full turn the heading is rolled out of -- a third copy
    // of the authored 6.283184, at 0x003523C8.
    inline constexpr float kFGpffff8458_turn = 6.283184051513672f;
    // fGpffff8450 and fGpffff8454, the two quarter turns FUN_0021EBE8 folds
    // into the camera's own pitch and yaw. They are the same magnitude with
    // opposite signs, and the code adds both, so they do not cancel.
    inline constexpr float kFGpffff8450_negHalfPi = -1.570796012878418f;
    inline constexpr float kFGpffff8454_halfPi = 1.570796012878418f;
    inline constexpr float kTickScale = 0.03125f;
    // The sprite is 16 units of +0x38 out from its centre in X and half that in
    // Y, and the packet scales every corner by 400 * q.
    inline constexpr float kCornerUnits = 16.0f;
    inline constexpr float kCornerScale = 400.0f;

    // DAT_00315878's four ST pairs -- byte for byte the same table as
    // DAT_00315858, so the two pools draw the same sprite off slot 0x21.
    inline constexpr float kFountainU0 = 0.814062476158142f * 256.0f;
    inline constexpr float kFountainV0 = 0.001562500023283f * 256.0f;
    inline constexpr float kFountainU1 = 0.841406226158142f * 256.0f;
    inline constexpr float kFountainV1 = 0.028906250372529f * 256.0f;

    inline constexpr int kFountainTextureSlot = 0x21;
    // Packet halfword 0x0321 for a colourless particle: bank 3, where the spray
    // pool's zero gives bank 1 and the dust's bank 2.
    inline constexpr int kFountainDefaultClutBank = 3;
    inline constexpr std::uint32_t kFountainDefaultColour = 0xF0F0F0u;
    // 0x10008580 into the packet's +0x0C, so FUN_00207DE8's ladder sees bit
    // 0x8000 -- mode 2, additive.
    inline constexpr int kFountainBlendMode = 2;
    inline constexpr int kFountainDisplayListBucket = 0x1000;

    std::uint32_t roll(const std::function<std::uint32_t()> &random)
    {
      return random ? random() : 0u;
    }

    std::int16_t truncate16(float value) { return static_cast<std::int16_t>(value); }
    int truncate32(float value) { return static_cast<int>(value); }

    // FUN_00218EB0, one point at a time.
    Vec3 FUN_00218eb0_transform(const Vec3 &point, const Matrix4 &matrix)
    {
      return {point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) +
                  point.z * matrix.at(2, 0) + matrix.at(3, 0),
              point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) +
                  point.z * matrix.at(2, 1) + matrix.at(3, 1),
              point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) +
                  point.z * matrix.at(2, 2) + matrix.at(3, 2)};
    }
  } // namespace

  Vec3 FUN_0021ebe8_to_world(const Vec3 &point, const FountainCameraFrame &camera)
  {
    namespace render = orphen::ported::render;

    // FUN_0021EBE8 builds three matrices on the VU0 scratchpad and accumulates
    // them oldest first through microprogram 0x60, the same way FUN_00220C00
    // builds a hit spark's: a pitch about X, a yaw about Z, then the camera's
    // own position put in last so neither turn moves it.
    Matrix4 pitch = render::FUN_0020bc38_identity();
    render::FUN_0020ba30_setRotationX(pitch, camera.fGpffffb6d8_pitch + kFGpffff8450_negHalfPi);
    Matrix4 yaw = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(yaw, -camera.fGpffffb6d4_yaw - kFGpffff8454_halfPi);
    Matrix4 place = render::FUN_0020bc38_identity();
    render::FUN_0020bb48_setTranslation(place, camera.DAT_0058c0a8_eyeX,
                                        camera.DAT_0058c0ac_eyeY, camera.DAT_0058c0b0_eyeZ);

    Matrix4 world = render::FUN_0020bb58_multiply(pitch, yaw);
    world = render::FUN_0020bb58_multiply(world, place);
    return FUN_00218eb0_transform(point, world);
  }

  void FountainParticlePool::FUN_0021f108_reset()
  {
    for (FountainParticle &particle : particles_)
    {
      particle = FountainParticle{};
    }
    draws_.clear();
    live_ = 0;
    gate_ = false;
  }

  void FountainParticlePool::FUN_0021ed50_spawn(float rise, float fall, float drift,
                                                float speedRange,
                                                float zJitterRange,
                                                float size,
                                                float baseX, float baseY, float baseZ,
                                                int count,
                                                std::int16_t lifeUnit,
                                                std::uint8_t loop,
                                                std::int8_t cameraRelative,
                                                std::uint32_t colour,
                                                const std::function<std::uint32_t()> &random)
  {
    // FUN_0021ED50's three floors, all applied before anything is rolled.
    int units = static_cast<int>(lifeUnit);
    if (units == 0)
    {
      units = 1;
    }
    if (speedRange == 0.0f)
    {
      speedRange = 1.0f;
    }
    if (zJitterRange == 0.0f)
    {
      zJitterRange = 1.0f;
    }

    // FUN_0021ED50:0x0021EE1C runs FUN_0021EBE8 over a copy of the base
    // position on its own stack when `cameraRelative` is set -- and then never
    // reads that copy back, spawning from the untransformed registers instead.
    // The flag only starts mattering at the first restart. Not reproducing the
    // dead call costs nothing: FUN_0021EBE8 rolls no random numbers.

    for (int spawned = 0; spawned < count; ++spawned)
    {
      // Unlike the spray pool's, this cursor is reloaded from the base of the
      // pool for **every** particle, so a burst of N always fills the first N
      // free slots from the front.
      std::size_t cursor = 0;
      int steps = 0;
      while (cursor < kCount && particles_[cursor].alive() && steps < 1999)
      {
        ++steps;
        ++cursor;
      }
      if (cursor >= kCount || particles_[cursor].alive())
      {
        // The pool is full. The original falls out of the scan and moves on to
        // the next particle of the burst rather than returning, so a full pool
        // costs the whole walk once per particle and spawns nothing.
        continue;
      }

      FountainParticle &particle = particles_[cursor];
      particle.colour3c = colour;
      particle.loop41 = loop;
      particle.phase40 = 0;
      particle.cameraRelative42 = cameraRelative;
      particle.age34 = 0;

      // Every divide here is `divu`. The life is `trunc((rand % N + 1) * 16)`,
      // written as `* 0.5 * 32.0` -- the two constants are separate immediates
      // in the original and the product is truncated, not rounded.
      const std::uint32_t lifeRoll = roll(random) % static_cast<std::uint32_t>(units);
      particle.life36 = static_cast<std::uint16_t>(
          truncate16(static_cast<float>(lifeRoll + 1u) * 0.5f * 32.0f));

      particle.heading24 =
          (static_cast<float>(roll(random) % 0x168u) * kFGpffff8458_turn) / 360.0f;
      particle.riseRate28 = rise;
      particle.fallRate2c = fall;
      particle.driftRate30 = drift;

      // The launch speed defaults to a **whole unit** and is only replaced when
      // the range survives truncation to a halfword of hundredths.
      float launchSpeed = 1.0f;
      const std::int16_t speedHundredths = truncate16(speedRange * 100.0f);
      if (speedHundredths != 0)
      {
        launchSpeed = static_cast<float>(roll(random) %
                                         static_cast<std::uint32_t>(speedHundredths)) /
                      100.0f;
      }

      particle.baseX18 = baseX;
      particle.baseY1c = baseY;
      particle.baseZ20 = baseZ;
      particle.baseZ14 = baseZ;
      particle.launchX0c = launchSpeed * std::cos(particle.heading24);
      particle.launchY10 = launchSpeed * std::sin(particle.heading24);

      // The z jitter burns **two** random numbers and uses the second. When the
      // range truncates to zero the original skips the first and then divides
      // by it anyway, which traps; taking no jitter is the only deviation, and
      // it needs a range in (0, 0.01) to be reachable at all.
      float zJitter = 0.0f;
      const std::int16_t zHundredths = truncate16(zJitterRange * 100.0f);
      if (zHundredths != 0)
      {
        roll(random);
        zJitter = static_cast<float>(roll(random) %
                                     static_cast<std::uint32_t>(zHundredths)) /
                  100.0f;
      }

      particle.size38 = size;
      gate_ = true;
      particle.x00 = baseX + particle.launchX0c;
      particle.y04 = baseY + particle.launchY10;
      particle.z08 = baseZ + zJitter;
      ++live_;
    }
  }

  void FountainParticlePool::FUN_0021f1a8_step(std::uint32_t frameTicks,
                                               const FountainCameraFrame &camera)
  {
    draws_.clear();

    // FUN_0021F1A8:6. Nothing live, or the gate down, and the walk is skipped.
    if (live_ <= 0 || !gate_)
    {
      return;
    }

    const float ticks = static_cast<float>(frameTicks);

    for (FountainParticle &particle : particles_)
    {
      if (!particle.alive())
      {
        continue;
      }

      // The age is a halfword add and the comparison against the life is done
      // on the sign-extended halfword, not on the sum.
      const auto stepped =
          static_cast<std::uint16_t>(particle.age34 + static_cast<std::uint16_t>(frameTicks));
      particle.age34 = stepped;

      if (static_cast<std::int16_t>(stepped) > static_cast<std::int16_t>(particle.life36))
      {
        if (particle.phase40 == 0)
        {
          particle.phase40 = 1;
        }
        else if (particle.loop41 == 0)
        {
          particle.phase40 = -1;
          --live_;
          if (live_ < 1)
          {
            gate_ = false;
          }
        }
        else
        {
          // The restart, and the only place +0x42 is ever honoured.
          orphen::ported::psm2::Vec3 base{particle.baseX18, particle.baseY1c, particle.baseZ20};
          if (particle.cameraRelative42 != 0)
          {
            base = FUN_0021ebe8_to_world(base, camera);
          }
          particle.z08 = base.z;
          particle.x00 = base.x + particle.launchX0c;
          particle.y04 = base.y + particle.launchY10;
          particle.phase40 = 0;
        }
        particle.age34 = 0;
        // Neither the end of a phase nor a restart draws on the frame it
        // happens.
        continue;
      }

      float fade = 0.0f;
      if (particle.phase40 == 0)
      {
        if (particle.riseRate28 > 0.0f)
        {
          particle.z08 += particle.riseRate28 * ticks * kTickScale;
        }
        else
        {
          // No rise rate at all: straight to the falling phase, still drawn
          // this frame and still at full alpha.
          particle.phase40 = 1;
        }
      }
      else if (particle.phase40 == 1)
      {
        if (particle.fallRate2c > 0.0f)
        {
          particle.z08 -= particle.fallRate2c * ticks * kTickScale;
        }
        fade = static_cast<float>(static_cast<std::int16_t>(particle.age34)) /
               static_cast<float>(static_cast<std::int16_t>(particle.life36));
      }

      // Every phase drifts, including any value above 1.
      if (particle.driftRate30 > 0.0f)
      {
        const float step = particle.driftRate30 * ticks * kTickScale;
        particle.x00 += step * std::cos(particle.heading24);
        particle.y04 += step * std::sin(particle.heading24);
      }

      draws_.push_back(FountainParticleDraw{particle.x00, particle.y04, particle.z08,
                                            particle.size38, particle.colour3c,
                                            truncate32(255.0f - fade * 255.0f)});
    }
  }

  orphen::ported::render::SpriteQuad
  FUN_0021f310_build_fountain_quad(const FountainQuadInputs &inputs)
  {
    orphen::ported::render::SpriteQuad quad;

    const float viewZ = inputs.viewZ > orphen::ported::render::kDAT_0035209c_spriteNearClip
                            ? inputs.viewZ
                            : orphen::ported::render::kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / viewZ;

    const float halfWidth = inputs.size * kCornerUnits * kCornerScale * q;
    const float halfHeight = halfWidth * 0.5f;

    const auto gsX0 = static_cast<int>(static_cast<float>(inputs.gsOriginX) - halfWidth);
    const auto gsX1 = static_cast<int>(static_cast<float>(inputs.gsOriginX) + halfWidth);
    const auto gsY0 = static_cast<int>(static_cast<float>(inputs.gsOriginY) - halfHeight);
    const auto gsY1 = static_cast<int>(static_cast<float>(inputs.gsOriginY) + halfHeight);

    const float perX = inputs.projectionScaleX != 0.0f ? viewZ / inputs.projectionScaleX : 0.0f;
    const float perY = inputs.projectionScaleY != 0.0f ? viewZ / inputs.projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
    quad.viewZ = viewZ;

    quad.u0 = kFountainU0;
    quad.v0 = kFountainV0;
    quad.u1 = kFountainU1;
    quad.v1 = kFountainV1;

    const std::uint32_t colour =
        inputs.colour != 0 ? inputs.colour : kFountainDefaultColour;
    // The packet's texture halfword is 0x0021 or 0x0321, so FUN_00207DE8 takes
    // the textured branch and halves all four channels.
    const auto folded = [](std::uint32_t component)
    { return static_cast<float>((component & 0xFEu) >> 1) / 128.0f; };
    quad.colour[0] = folded(colour & 0xFFu);
    quad.colour[1] = folded((colour >> 8) & 0xFFu);
    quad.colour[2] = folded((colour >> 16) & 0xFFu);
    quad.colour[3] = folded(static_cast<std::uint32_t>(inputs.alpha) & 0xFFu);

    quad.blendMode = kFountainBlendMode;
    quad.textureSlot = kFountainTextureSlot;
    quad.displayListBucket = kFountainDisplayListBucket;
    quad.clutBank = inputs.colour == 0 ? kFountainDefaultClutBank : -1;
    return quad;
  }

} // namespace orphen::ported::entity
