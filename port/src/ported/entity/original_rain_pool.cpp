#include "ported/entity/original_rain_pool.h"

#include "ported/render/original_view_projection.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // FUN_00218EB0 for one point: the 3x4 the VU0 microprogram applies.
    Vec3 FUN_00218eb0_transform(const Vec3 &point, const Matrix4 &matrix)
    {
      return Vec3{point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) +
                      point.z * matrix.at(2, 0) + matrix.at(3, 0),
                  point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) +
                      point.z * matrix.at(2, 1) + matrix.at(3, 1),
                  point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) +
                      point.z * matrix.at(2, 2) + matrix.at(3, 2)};
    }

    // FUN_00216868 % n, where the original's `divu` makes both operands
    // unsigned and a zero divisor is `break 7`. The pool's two divisors come
    // straight off the opcode, so a scene that arms it with a zero radius or a
    // zero height would stop the machine; answering zero is the port's floor.
    std::uint32_t modulo(std::uint32_t value, int divisor)
    {
      return divisor > 0 ? value % static_cast<std::uint32_t>(divisor) : 0u;
    }
  } // namespace

  void RainParticlePool::FUN_0021ad00_reset()
  {
    for (RainParticle &particle : particles_)
    {
      particle = RainParticle{};
    }
    draws_.clear();
    live_ = 0;
    uGpffffad38_gate_ = false;
  }

  void RainParticlePool::FUN_0021ac00_arm(float length, float fall, float rotateX, float rotateZ,
                                          int count, int magnitude, int height, int entityIndex)
  {
    // FUN_0021AC00's head: every parameter is stored before the count is
    // touched, and the gate goes up whatever the count does.
    length_ = length;
    fall_ = fall;
    rotateX_ = rotateX;
    rotateZ_ = rotateZ;
    magnitude_ = magnitude;
    height_ = height;
    entityIndex_ = (entityIndex < 0 || entityIndex >= 0x100) ? -1 : entityIndex;
    uGpffffad38_gate_ = true;

    if (live_ < count)
    {
      // **The loop only gets halfway there.** `added < count - live` with both
      // sides moving: asking for 1000 from empty leaves 500 live, which is what
      // the hardware reads in s01_e013. Same arithmetic as FUN_0021BD30's, and
      // deliberate here for the same reason -- a scene that re-arms every frame
      // converges on its target, and s01_e013 arms once.
      std::size_t cursor = 0;
      int added = 0;
      while (added < count - live_)
      {
        // FUN_0021AC00's rolling scan for a free record, which gives up after
        // 3000 -- so record index kCount is unreachable even though the
        // original's clear and walk loops both run one past the end. That
        // record is never live and never draws; the port simply does not have
        // it.
        std::size_t scanned = 0;
        while (cursor < kCount && particles_[cursor].state12 >= 0)
        {
          ++cursor;
          if (++scanned > kCount - 1)
          {
            break;
          }
        }
        if (cursor >= kCount)
        {
          return;
        }
        particles_[cursor].state12 = 0;
        ++live_;
        ++added;
      }
      return;
    }

    if (count < live_)
    {
      // The shrink half: walk from the front freeing live records until the
      // count has come down. The original's `iVar3 < iVar5` compare is against
      // the difference captured *before* the walk, so unlike the grow loop this
      // one really does reach its target.
      const int target = live_ - count;
      int freed = 0;
      for (std::size_t index = 0; index < kCount; ++index)
      {
        if (particles_[index].state12 > 0)
        {
          particles_[index].state12 = -1;
          ++freed;
          --live_;
        }
        if (target < freed)
        {
          break;
        }
      }
      uGpffffad38_gate_ = true;
    }
  }

  void RainParticlePool::FUN_0021ad98_step(
      std::uint32_t frameTicks,
      const RainCameraFrame &camera,
      const std::optional<RainEntityAnchor> &anchor,
      const std::function<std::uint32_t()> &random,
      const std::function<std::optional<float>(float, float, float)> &FUN_00227798_probe)
  {
    namespace render = orphen::ported::render;

    draws_.clear();
    // FUN_0021AD98's own gate: nothing at all happens with the gate down or the
    // pool empty, so a record's state is frozen rather than stepped.
    if (!uGpffffad38_gate_ || live_ <= 0)
    {
      return;
    }

    // FUN_00218F40. With no entity the pool sits `magnitude` units down the
    // camera's line of sight at eye height, which is why the rain travels with
    // you; with one it sits on that entity.
    Vec3 origin{};
    if (anchor.has_value())
    {
      origin = Vec3{anchor->positionX20, anchor->positionY24, anchor->positionZ28};
    }
    else
    {
      const float magnitude = static_cast<float>(magnitude_);
      origin = Vec3{camera.DAT_0058c0a8_eyeX + magnitude * std::cos(camera.fGpffffb6d4_yaw),
                    camera.DAT_0058c0ac_eyeY + magnitude * std::sin(camera.fGpffffb6d4_yaw),
                    camera.DAT_0058c0b0_eyeZ};
    }

    // FUN_00218FE0: identity, Rx, Rz, then the origin last so neither turn
    // moves it. s01_e013 passes zero for both angles, so for that scene this is
    // a pure translation -- kept general because the opcode carries them.
    Matrix4 poolPitch = render::FUN_0020bc38_identity();
    render::FUN_0020ba30_setRotationX(poolPitch, rotateX_);
    Matrix4 poolYaw = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(poolYaw, rotateZ_);
    Matrix4 poolPlace = render::FUN_0020bc38_identity();
    render::FUN_0020bb48_setTranslation(poolPlace, origin.x, origin.y, origin.z);
    Matrix4 pool = render::FUN_0020bb58_multiply(poolPitch, poolYaw);
    pool = render::FUN_0020bb58_multiply(pool, poolPlace);

    // FUN_0020BC78(0x342828) is the identity in .data, and FUN_0020BAE0 turns
    // it about Z. A pure rotation, so it billboards the offsets without moving
    // the record.
    Matrix4 billboard = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(billboard,
                                      -camera.fGpffffb6d4_yaw - kfGpffff83a0_halfPi);

    // The four local corners, written into the scratchpad at words 0x18..0x27
    // and turned into words 0x28..0x37. Rebuilt every frame because the camera
    // moves, and shared by all 3000 records.
    const std::array<Vec3, 4> localCorners{
        Vec3{kfGpffff83a4_streakLeft, 0.0f, length_},
        Vec3{kfGpffff83a4_streakLeft, 0.0f, 0.0f},
        Vec3{kfGpffff83a8_streakRight, 0.0f, 0.0f},
        Vec3{kfGpffff83a8_streakRight, 0.0f, length_},
    };
    std::array<Vec3, 4> cornerOffsets{};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      cornerOffsets[corner] = FUN_00218eb0_transform(localCorners[corner], billboard);
    }

    // Scratchpad word 0x78: `fGpffffbb34 * iGpffffb64c * 0.03125`.
    const float fallStep = fall_ * static_cast<float>(frameTicks) * kFallScale;
    const float splashStep = static_cast<float>(frameTicks);

    for (RainParticle &particle : particles_)
    {
      // FUN_0021AFA0's first test. A free record costs nothing.
      if (particle.state12 < 0)
      {
        continue;
      }

      if (particle.state12 == 0)
      {
        // Spawn. `frac` is rolled once and spent twice -- on the radius and on
        // the height -- which is the original's, not a transcription slip.
        const float angle =
            (static_cast<float>(modulo(random(), 360)) * kfGpffff83ac_twoPi) / 360.0f;
        const float frac = static_cast<float>(modulo(random(), 100)) / 100.0f;
        const float radius = frac + static_cast<float>(modulo(random(), magnitude_));
        particle.x00 = radius * std::cos(angle);
        particle.y04 = radius * std::sin(angle);
        particle.z08 = static_cast<float>(modulo(random(), height_)) + frac;
        particle.state12 = 1;
        particle.groundProbe13 = 0;
        continue;
      }

      if (particle.state12 == 1)
      {
        // The fall is committed to the record before the floor test, so a drop
        // that falls out the bottom is respawned from wherever it left.
        particle.z08 -= fallStep;
        if (particle.z08 < -static_cast<float>(height_))
        {
          particle.state12 = 0;
          continue;
        }

        std::array<Vec3, 4> world{};
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          world[corner] = FUN_00218eb0_transform(
              Vec3{particle.x00 + cornerOffsets[corner].x,
                   particle.y04 + cornerOffsets[corner].y,
                   particle.z08 + cornerOffsets[corner].z},
              pool);
        }

        // +0x13 ticks every falling frame; the probe runs when it reaches zero.
        particle.groundProbe13 = static_cast<std::int8_t>(particle.groundProbe13 - 1);
        if (particle.groundProbe13 < 1)
        {
          // The **second** corner: the bottom of the streak, on the -x side.
          const std::optional<float> ground =
              FUN_00227798_probe ? FUN_00227798_probe(world[1].x, world[1].y, world[1].z)
                                 : std::nullopt;
          const float groundHeight = ground.value_or(kGroundSentinel);
          // 0x0021B26C and 0x0021B290, two independent `if`s: the second writes
          // over the first for every height the first accepts, so 100 is dead
          // and a drop over ground above z=1 re-probes every 50 frames.
          if (groundHeight > 5.0f)
          {
            particle.groundProbe13 = 100;
          }
          if (groundHeight > 1.0f)
          {
            particle.groundProbe13 = 50;
          }
          if (world[1].z <= groundHeight && groundHeight < kGroundSentinel)
          {
            // Landed. The record's position becomes the **world** point it hit,
            // lifted clear of the surface, and no streak is drawn this frame.
            particle.x00 = world[1].x;
            particle.y04 = world[1].y;
            particle.z08 = groundHeight + kfGpffff83b0_splashLift;
            particle.state12 = 2;
            continue;
          }
        }

        RainQuad quad;
        quad.corners = world;
        quad.alpha = static_cast<int>((kRainStreakColour >> 24) & 0xFFu);
        quad.splash = false;
        draws_.push_back(quad);
        continue;
      }

      if (particle.state12 == 2)
      {
        // FUN_0021B398. A flat ring at the record's own world position, growing
        // and fading over 960 ticks. It goes straight into the world-corner
        // slots, so the pool matrix never touches it.
        const float progress = static_cast<float>(particle.splashTicks10) / kSplashTicks;
        const float radius = progress * kDAT_00352324_splashGrow + kDAT_00352328_splashBase;
        RainQuad quad;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          quad.corners[corner] =
              Vec3{particle.x00 + kDAT_00315598_splashCorners[corner][0] * radius,
                   particle.y04 + kDAT_00315598_splashCorners[corner][1] * radius,
                   particle.z08};
        }
        // FUN_0030BDB0 rounds `255 - progress * 255` into the packet's top byte.
        quad.alpha = static_cast<int>(std::lround(255.0f - progress * 255.0f));
        quad.splash = true;
        draws_.push_back(quad);

        // The timer is advanced after the draw, and the frame it passes 960 is
        // still drawn.
        const float advanced = static_cast<float>(particle.splashTicks10) + splashStep;
        if (advanced > kSplashTicks)
        {
          particle.state12 = 0;
          particle.splashTicks10 = 0;
        }
        else
        {
          particle.splashTicks10 = static_cast<std::int16_t>(advanced);
        }
      }
    }
  }

} // namespace orphen::ported::entity
