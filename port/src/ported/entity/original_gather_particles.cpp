#include "ported/entity/original_gather_particles.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // fGpffff84A0 and fGpffff84A4, both the authored 6.283184: the first turns
    // the spread into a count of jitter steps, the second turns a step back
    // into radians. They are separate words holding the same value.
    inline constexpr float kFGpffff84a0_turn = 6.283184051513672f;
    inline constexpr float kFGpffff84a4_turn = 6.283184051513672f;
    inline constexpr float kTickScale = 0.03125f;
    inline constexpr float kCornerScale = 400.0f;

    // DAT_003159F8..DAT_00315A14, read out of the ELF: the four corners in GS
    // units, already at the 32-by-16 aspect the 2:1 GS pixel wants.
    inline constexpr float kDAT_003159f8_corners[4][2] = {
        {-16.0f, -8.0f}, {-16.0f, 8.0f}, {16.0f, 8.0f}, {16.0f, -8.0f}};

    // DAT_00315A18's ST pairs -- a third copy of DAT_00315858's.
    inline constexpr float kGatherU0 = 0.814062476158142f * 256.0f;
    inline constexpr float kGatherV0 = 0.001562500023283f * 256.0f;
    inline constexpr float kGatherU1 = 0.841406226158142f * 256.0f;
    inline constexpr float kGatherV1 = 0.028906250372529f * 256.0f;

    inline constexpr int kGatherTextureSlot = 0x21;
    inline constexpr int kGatherDefaultClutBank = 3;
    inline constexpr std::uint32_t kGatherDefaultColour = 0xF0F0F0u;
    // 0x10008080 into the packet's +0x0C: bit 0x8000, so mode 2, additive.
    inline constexpr int kGatherBlendMode = 2;
    inline constexpr int kGatherDisplayListBucket = 0x1000;

    std::uint32_t roll(const std::function<std::uint32_t()> &random)
    {
      return random ? random() : 0u;
    }

    std::int16_t truncate16(float value) { return static_cast<std::int16_t>(value); }

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

  void GatherParticlePool::reset()
  {
    for (GatherStreak &streak : streaks_)
    {
      streak = GatherStreak{};
    }
    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      groups_[group].buffer = static_cast<std::int8_t>(group);
      groups_[group].count = 0;
    }
    draws_.clear();
    activeGroups_ = 0;
    gate_ = false;
  }

  std::int8_t GatherParticlePool::FUN_00220f70_spawn(float x, float y, float z,
                                                     float speed,
                                                     float radiusRange,
                                                     float yaw,
                                                     float pitch,
                                                     float spread,
                                                     std::int16_t count,
                                                     std::uint32_t colour,
                                                     float size,
                                                     const std::function<std::uint32_t()> &random)
  {
    // FUN_00220F70:1-12. Three guards, all before anything is rolled: a
    // non-positive radius range or count falls straight out with -1, and a
    // non-positive speed does too, because the life divides by it.
    if (!(radiusRange > 0.0f))
    {
      return -1;
    }
    if (speed <= 0.0f)
    {
      return -1;
    }
    int wanted = static_cast<int>(count);
    if (wanted <= 0)
    {
      return -1;
    }
    if (wanted > static_cast<int>(kGroupCapacity))
    {
      wanted = static_cast<int>(kGroupCapacity);
    }

    // The spread is turned into a count of whole jitter steps first. The
    // original leaves this at zero when the spread is exactly zero and then
    // divides by it, which traps; taking no jitter is the deviation, and it
    // needs a caller that asks for a spread of zero to reach.
    int jitterSteps = 0;
    if (spread != 0.0f)
    {
      jitterSteps = truncate16((spread * 0.5f * 360.0f) / kFGpffff84a0_turn);
    }

    const std::int16_t radiusHundredths = truncate16(radiusRange * 100.0f);

    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      if (groups_[group].count >= 1)
      {
        continue;
      }

      groups_[group].count = static_cast<std::int16_t>(wanted);
      const std::size_t base = static_cast<std::size_t>(groups_[group].buffer) * kGroupCapacity;

      for (int index = 0; index < wanted; ++index)
      {
        GatherStreak &streak = streaks_[base + static_cast<std::size_t>(index)];

        // Five random numbers an entry: the radius, then a sign and a
        // magnitude for each of the two angles.
        const std::uint32_t radiusWord = roll(random);
        const std::uint32_t radiusRoll =
            radiusHundredths != 0
                ? (radiusWord % static_cast<std::uint32_t>(radiusHundredths)) + 1u
                : 1u;
        streak.alive40 = 0;
        const float radius = static_cast<float>(radiusRoll) / 100.0f;
        streak.originX00 = x;
        streak.originY04 = y;
        streak.originZ08 = z;

        const auto jitter = [&]() -> float
        {
          if (jitterSteps == 0)
          {
            return 0.0f;
          }
          return (static_cast<float>(roll(random) % static_cast<std::uint32_t>(jitterSteps)) *
                  kFGpffff84a4_turn) /
                 360.0f;
        };

        // The pitch first -- it lands at +0x28 -- then the yaw at +0x20. Each
        // takes its sign from bit 0 of a roll of its own, and the two read that
        // bit **the opposite way round**: a set bit adds to the pitch and
        // subtracts from the yaw. That asymmetry is in the original.
        const bool pitchBit = (roll(random) & 1u) != 0;
        const float pitchJitter = jitter();
        streak.pitch28 = pitchBit ? pitch + pitchJitter : pitch - pitchJitter;

        const bool yawBit = (roll(random) & 1u) != 0;
        const float yawJitter = jitter();
        streak.yaw20 = yawBit ? yaw - yawJitter : yaw + yawJitter;

        streak.age34 = 0;
        streak.life36 = static_cast<std::int16_t>(truncate16(radius / speed) << 5);
        streak.radius38 = radius;
        streak.localX0c = radius;
        streak.localY10 = 0.0f;
        streak.localZ14 = 0.0f;
        streak.size1c = size;
        streak.colour18 = colour;
        streak.speed3c = speed;
        streak.group42 = groups_[group].buffer;
        streak.matrixCached41 = 0;
        gate_ = true;
      }

      ++activeGroups_;
      return groups_[group].buffer;
    }

    // All ten groups busy. The original returns 0 here, which a caller cannot
    // tell from buffer 0.
    return 0;
  }

  void GatherParticlePool::FUN_002218f0_release(std::int8_t group,
                                                std::int8_t *hitSparkActiveGroups)
  {
    if (activeGroups_ <= 0)
    {
      return;
    }

    if (group < 0)
    {
      for (GatherGroup &entry : groups_)
      {
        entry.count = 0;
      }
      for (GatherStreak &streak : streaks_)
      {
        streak.alive40 = -1;
      }
      activeGroups_ = 0;
      return;
    }

    if (static_cast<std::size_t>(group) >= kGroupCount)
    {
      return;
    }
    groups_[static_cast<std::size_t>(group)].count = 0;
    // gp-0x43F4, the hit spark pool's DAT_00355B7C. See the header.
    if (hitSparkActiveGroups != nullptr)
    {
      --*hitSparkActiveGroups;
    }
    const std::size_t base =
        static_cast<std::size_t>(groups_[static_cast<std::size_t>(group)].buffer) * kGroupCapacity;
    for (std::size_t index = 0; index < kGroupCapacity; ++index)
    {
      streaks_[base + index].alive40 = -1;
    }
    // The per-group branch does **not** decrement DAT_00355B88; only the clear
    // -everything branch zeroes it.
  }

  void GatherParticlePool::FUN_00221398_step(std::uint32_t frameTicks)
  {
    namespace render = orphen::ported::render;

    draws_.clear();

    if (!gate_)
    {
      return;
    }
    if (activeGroups_ < 1)
    {
      gate_ = false;
      return;
    }

    const float ticks = static_cast<float>(frameTicks);

    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      if (groups_[group].count <= 0)
      {
        continue;
      }
      const std::size_t base = static_cast<std::size_t>(groups_[group].buffer) * kGroupCapacity;
      if (!streaks_[base].alive())
      {
        continue;
      }

      for (std::size_t index = 0; index < kGroupCapacity; ++index)
      {
        GatherStreak &streak = streaks_[base + index];

        if (streak.matrixCached41 == 0)
        {
          // FUN_00221398:0x0022142C. Identity, the entry's yaw about Y and its
          // pitch about Z -- both negated -- then the burst's origin put in
          // last so neither turn moves it. Built once and kept.
          Matrix4 yaw = render::FUN_0020bc38_identity();
          render::FUN_0020ba88_setRotationY(yaw, -streak.yaw20);
          Matrix4 pitch = render::FUN_0020bc38_identity();
          render::FUN_0020bae0_setRotationZ(pitch, -streak.pitch28);
          Matrix4 place = render::FUN_0020bc38_identity();
          render::FUN_0020bb48_setTranslation(place, streak.originX00, streak.originY04,
                                              streak.originZ08);
          Matrix4 world = render::FUN_0020bb58_multiply(yaw, pitch);
          streak.matrix44 = render::FUN_0020bb58_multiply(world, place);
          streak.matrixCached41 = 1;
        }

        // FUN_00221608. The age is a halfword add and the compare is signed.
        const auto stepped =
            static_cast<std::int16_t>(streak.age34 + static_cast<std::int16_t>(frameTicks));
        streak.age34 = stepped;

        if (static_cast<int>(streak.life36) < static_cast<int>(stepped))
        {
          // Out of life, and **not freed**: the streak goes back to its
          // starting radius and runs the walk again. Nothing in the pool ever
          // releases an entry except opcode 0x115.
          streak.age34 = 0;
          streak.localX0c = streak.radius38;
        }
        else
        {
          streak.localX0c -= streak.speed3c * ticks * kTickScale;

          const Vec3 world = FUN_00218eb0_transform(
              Vec3{streak.localX0c, streak.localY10, streak.localZ14}, streak.matrix44);
          // `age * 255 / life`, so the streak fades **in** as it converges.
          const int alpha = streak.life36 != 0
                                ? (static_cast<int>(streak.age34) * 0xFF) /
                                      static_cast<int>(streak.life36)
                                : 0;
          draws_.push_back(GatherStreakDraw{world.x, world.y, world.z, streak.size1c,
                                            streak.colour18, alpha});
        }

        // The walk stops at the first free entry, which is the one past the
        // count the group was filled with.
        if (index + 1 >= kGroupCapacity || !streaks_[base + index + 1].alive())
        {
          break;
        }
      }
    }
  }

  orphen::ported::render::SpriteQuad
  FUN_00221608_build_gather_quad(const GatherStreakDraw &streak,
                                 std::int32_t gsOriginX, std::int32_t gsOriginY,
                                 float viewZ,
                                 float projectionScaleX, float projectionScaleY,
                                 float screenCentreX, float screenCentreY)
  {
    orphen::ported::render::SpriteQuad quad;

    const float clamped = viewZ > orphen::ported::render::kDAT_0035209c_spriteNearClip
                              ? viewZ
                              : orphen::ported::render::kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / clamped;
    const float scale = streak.size * kCornerScale * q;

    const auto gsX0 =
        static_cast<int>(static_cast<float>(gsOriginX) + kDAT_003159f8_corners[0][0] * scale);
    const auto gsX1 =
        static_cast<int>(static_cast<float>(gsOriginX) + kDAT_003159f8_corners[2][0] * scale);
    const auto gsY0 =
        static_cast<int>(static_cast<float>(gsOriginY) + kDAT_003159f8_corners[0][1] * scale);
    const auto gsY1 =
        static_cast<int>(static_cast<float>(gsOriginY) + kDAT_003159f8_corners[2][1] * scale);

    const float perX = projectionScaleX != 0.0f ? clamped / projectionScaleX : 0.0f;
    const float perY = projectionScaleY != 0.0f ? clamped / projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - screenCentreY) * perY;
    quad.viewZ = clamped;

    quad.u0 = kGatherU0;
    quad.v0 = kGatherV0;
    quad.u1 = kGatherU1;
    quad.v1 = kGatherV1;

    const std::uint32_t rgb = streak.colour != 0 ? streak.colour : kGatherDefaultColour;
    const auto folded = [](std::uint32_t component)
    { return static_cast<float>((component & 0xFEu) >> 1) / 128.0f; };
    quad.colour[0] = folded(rgb & 0xFFu);
    quad.colour[1] = folded((rgb >> 8) & 0xFFu);
    quad.colour[2] = folded((rgb >> 16) & 0xFFu);
    quad.colour[3] = folded(static_cast<std::uint32_t>(streak.alpha) & 0xFFu);

    quad.blendMode = kGatherBlendMode;
    quad.textureSlot = kGatherTextureSlot;
    quad.displayListBucket = kGatherDisplayListBucket;
    quad.clutBank = streak.colour == 0 ? kGatherDefaultClutBank : -1;
    return quad;
  }

} // namespace orphen::ported::entity
