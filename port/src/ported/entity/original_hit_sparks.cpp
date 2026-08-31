#include "ported/entity/original_hit_sparks.h"

#include "ported/render/original_view_projection.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // FUN_00218eb0: row-vector, with the matrix's row 3 added as the
    // translation. The original runs it on four points at a time through VU0
    // macro mode; the arithmetic is the same one point at a time.
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

  // FUN_00220910:0x002209d8-0x00220ad8 builds two matrices per spark on the VU0
  // scratchpad. Reading the disassembly rather than the decompiler's C is what
  // makes the sequence legible: vf20..23 are loaded with the identity once and
  // never written again, so every `sqc2 vf20` in the middle of the run is
  // *restoring the identity* to the scratch before the next rotation builder
  // writes its four entries over it. The vcallms at 0xC then accumulates into
  // vf28..31, oldest first.
  //
  // Without that, the four builders look like they overwrite parts of a product
  // matrix, which would be meaningless.
  HitSparkQuad FUN_00220c00_build_quad(const HitSpark &spark, float fGpffffb6d4_cameraYaw)
  {
    namespace render = orphen::ported::render;

    // 0x00220aa8. The streak's own turn, about Z and **negated** -- the corner
    // at local +x ends up along `(cos fan, sin fan)`, which is exactly the
    // direction FUN_00220c00 slides the spark in.
    Matrix4 spin = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(spin, -spark.fanAngle20);

    // 0x00220a00 then 0x00220a30: the spark's random yaw about Y, then the
    // camera's own yaw undone about Z.
    Matrix4 yaw = render::FUN_0020bc38_identity();
    render::FUN_0020ba88_setRotationY(yaw, spark.yaw1c);
    Matrix4 faceCamera = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(faceCamera, -fGpffffb6d4_cameraYaw - kfGpffff849c_halfPi);
    // 0x00220a68. The burst's origin, put in last so it is not turned by either.
    Matrix4 place = render::FUN_0020bc38_identity();
    render::FUN_0020bb48_setTranslation(place, spark.x00, spark.y04, spark.z08);

    Matrix4 world = render::FUN_0020bb58_multiply(yaw, faceCamera);
    world = render::FUN_0020bb58_multiply(world, place);

    HitSparkQuad quad;
    // 0x00220ce4. `sourceSide` of exactly 1 -- a party-side victim -- takes the
    // shorter rectangle; everything else, including the 0 an enemy gets, takes
    // the other.
    quad.texelRectangle = spark.sourceSide28 == 1 ? 0 : 1;

    const Vec3 travelled{spark.offsetX0c, spark.offsetY10, spark.offsetZ14};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      // 0x00220cf8-0x00220d70. y is a flat zero and the thin axis is *not*
      // scaled, so a spark stays 0.02 units wide however long it is drawn.
      const Vec3 local{kDAT_0034b988_corners[corner][0] * spark.scale2c, 0.0f,
                       kDAT_0034b988_corners[corner][1]};
      const Vec3 turned = FUN_00218eb0_transform(local, spin);
      const Vec3 offset{travelled.x + turned.x, travelled.y + turned.y, travelled.z + turned.z};
      quad.corners[corner] = FUN_00218eb0_transform(offset, world);
    }
    return quad;
  }

  void HitSparkPool::FUN_002205d0_reset()
  {
    // The original zeroes the whole 56000 bytes and then walks it marking every
    // entry dead, which is what the default HitSpark already is.
    sparks_.fill(HitSpark{});
    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      groups_[group].buffer = static_cast<std::uint8_t>(group);
      groups_[group].count = 0;
    }
    activeGroups_ = 0;
  }

  std::size_t HitSparkPool::FUN_002206a8_spawn(const OriginalEntity &victim,
                                               std::int16_t sourceSide,
                                               const std::function<std::uint32_t()> &random)
  {
    // FUN_002206a8:8. No pending damage, no sparks.
    const std::int16_t pending = static_cast<std::int16_t>(victim.pendingDamageBe);
    if (pending == 0)
    {
      return 0;
    }

    // 0x00220708. Two reactions get the fat streak and their own mip bias.
    std::uint8_t bigReaction = 0;
    float scale = kDAT_003523f8_scale;
    if (victim.hitReactionBc == 0x1B || victim.hitReactionBc == 0x1D)
    {
      bigReaction = 1;
      scale = kDAT_003523fc_bigScale;
    }

    // 0x00220744. Ten sparks a point, capped at a hundred -- the whole of a
    // group. The original writes it as `damage * 0xA0000 >> 0x10` in 32-bit
    // registers, which is the same thing as truncating `damage * 10` to a
    // halfword and sign extending it.
    int count = static_cast<std::int16_t>(static_cast<std::int32_t>(pending) * 10);
    if (count > static_cast<int>(kGroupCapacity))
    {
      count = static_cast<int>(kGroupCapacity);
    }
    if (count == 0)
    {
      // The original traps here -- the divide below is by this. FUN_00216140
      // floors the damage at one and skips the call for a blocked hit, so
      // nothing can reach it; not dividing is the whole of the difference.
      return 0;
    }

    // 0x00220758. An **integer** 360/count first, so a burst of seven fans by
    // 51 degrees a spark and leaves a gap rather than closing the circle.
    const float step = static_cast<float>(360 / count) * kDAT_00352400_twoPi / 360.0f;

    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      // 0x00220774. The first group that is not currently showing anything.
      if (groups_[group].count >= 1)
      {
        continue;
      }

      HitSpark *entry = &sparks_[static_cast<std::size_t>(groups_[group].buffer) * kGroupCapacity];
      float fan = kDAT_00352404_fanStart;
      for (int index = 0; index < count; ++index, ++entry)
      {
        entry->x00 = victim.positionX20;
        entry->y04 = victim.positionZ24;
        // 0x002207a0. Three quarters of the way up the victim's collision
        // height, which is why a spark shower reads as coming off the body
        // rather than off the feet.
        entry->z08 = victim.positionY28 + victim.height58 * 0.75f;

        entry->offsetX0c = 0.0f;
        entry->offsetY10 = 0.0f;
        entry->offsetZ14 = 0.0f;

        // 0x002207b0. `rand % 360` degrees, converted the long way round.
        const std::uint32_t roll = random ? random() : 0;
        entry->yaw1c = static_cast<float>(roll % 0x168u) * kDAT_00352400_twoPi / 360.0f;
        entry->fanAngle20 = fan;

        entry->lifetime24 = kSparkLifetime;
        entry->lifetime26 = kSparkLifetime;
        entry->sourceSide28 = sourceSide;
        entry->group2a = groups_[group].buffer;
        entry->scale2c = scale;
        entry->speed30 = kSparkSpeed;
        entry->bigReaction34 = bigReaction;

        fan += step;
      }

      // A negative count -- which needs a victim carrying damage that was
      // negated by a block and never consumed -- seeds nothing but still claims
      // the group and still counts as active, exactly as the original leaves it.
      groups_[group].count = static_cast<std::int16_t>(count);
      ++activeGroups_;
      return count < 0 ? 0u : static_cast<std::size_t>(count);
    }

    // Every group busy. The original falls out of the same loop and returns
    // having done nothing, so an eleventh simultaneous hit shows no sparks.
    return 0;
  }

  void HitSparkPool::FUN_00220ba8_retire(HitSpark &spark)
  {
    // FUN_00220ba8. Guarded on the active-group count, so a retire that somehow
    // ran with the pool already empty changes nothing.
    if (activeGroups_ > 0)
    {
      HitSparkGroup &group = groups_[spark.group2a];
      group.count = static_cast<std::int16_t>(group.count - 1);
      if (group.count < 1)
      {
        group.count = 0;
        activeGroups_ = static_cast<std::int8_t>(activeGroups_ - 1);
        if (activeGroups_ < 1)
        {
          activeGroups_ = 0;
        }
      }
    }
    spark.sourceSide28 = -1;
    spark.group2a = 0xFF;
  }

  void HitSparkPool::FUN_00220910_step(std::uint32_t frameTicks)
  {
    // FUN_00220910:8. Nothing showing, nothing to walk.
    if (activeGroups_ <= 0)
    {
      return;
    }

    for (std::size_t group = 0; group < kGroupCount; ++group)
    {
      if (groups_[group].count < 1)
      {
        continue;
      }

      HitSpark *entry = &sparks_[static_cast<std::size_t>(groups_[group].buffer) * kGroupCapacity];
      // 0x002209b4. The head of the slice has to be live for the walk to start
      // at all, and the loop then stops at the first dead entry after it. A
      // burst fills its group from the front and every spark in it is given the
      // same lifetime, so the run is always contiguous.
      if (!entry->alive())
      {
        continue;
      }

      for (std::size_t index = 0; index < kGroupCapacity; ++index, ++entry)
      {
        // FUN_00220c00:9. The subtract is on the unsigned halfword and the test
        // is on its sign as a signed one.
        const std::int16_t remaining =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(entry->lifetime24) -
                                      static_cast<std::uint16_t>(frameTicks));
        entry->lifetime24 = static_cast<std::uint16_t>(remaining);
        if (remaining < 1)
        {
          FUN_00220ba8_retire(*entry);
        }
        else
        {
          // 0x00220c50. A flat slide along the fan angle, in the burst's own
          // local frame -- FUN_00220c00_build_quad is what carries it out into
          // the world. The third component is loaded and stored back untouched.
          const float travel = entry->speed30 * static_cast<float>(frameTicks) / kSparkSpeedDivisor;
          entry->offsetX0c += travel * std::cos(entry->fanAngle20);
          entry->offsetY10 += travel * std::sin(entry->fanAngle20);
        }

        if (index + 1 >= kGroupCapacity || !entry[1].alive())
        {
          break;
        }
      }
    }
  }

  std::size_t HitSparkPool::aliveCount() const
  {
    std::size_t alive = 0;
    for (const auto &spark : sparks_)
    {
      if (spark.alive())
      {
        ++alive;
      }
    }
    return alive;
  }

} // namespace orphen::ported::entity
