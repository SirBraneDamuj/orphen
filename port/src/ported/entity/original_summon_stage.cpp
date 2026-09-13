#include "ported/entity/original_summon_stage.h"

#include <algorithm>
#include <array>

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_002DE4A8 and the three beside it all test `'\0' < status`, which is
    // exactly SlotStatus::ScriptSpawned: FUN_00229C40 gives a positive type id
    // a positive status and a negative one the -1 the test rejects.
    bool live(const EntityPool &pool, std::size_t slot)
    {
      return pool.status(slot) == SlotStatus::ScriptSpawned;
    }

    // FUN_00266A78:48-54 with param_4 == 0, the only parameterisation the
    // summons use. Identical to the camera path's own, which is file-local
    // there.
    std::array<float, orphen::ported::camera::kMaxSplinePoints> uniformKnots(std::size_t count)
    {
      std::array<float, orphen::ported::camera::kMaxSplinePoints> knots{};
      if (count < 2)
      {
        return knots;
      }
      for (std::size_t index = 0; index < count; ++index)
      {
        knots[index] = static_cast<float>(index) / static_cast<float>(count - 1);
      }
      return knots;
    }

    void buildCurve(std::array<orphen::ported::camera::CubicSpline, 3> &curve,
                    std::span<const Vec3> points)
    {
      const std::size_t count =
          std::min(points.size(), orphen::ported::camera::kMaxSplinePoints);
      const auto knots = uniformKnots(count);
      std::array<float, orphen::ported::camera::kMaxSplinePoints> channel{};
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        for (std::size_t index = 0; index < count; ++index)
        {
          channel[index] = (axis == 0)   ? points[index].x
                           : (axis == 1) ? points[index].y
                                         : points[index].z;
        }
        curve[axis].build({knots.data(), count}, {channel.data(), count});
      }
    }

    Vec3 sampleCurve(const std::array<orphen::ported::camera::CubicSpline, 3> &curve, float t)
    {
      return Vec3{curve[0].evaluate(t), curve[1].evaluate(t), curve[2].evaluate(t)};
    }
  } // namespace

  // ------------------------------------------------------------- the freeze

  void SummonStage::FUN_002de4a8_freeze_field(EntityPool &pool)
  {
    for (std::size_t slot = 0; slot < pool.slotCount(); ++slot)
    {
      if (!live(pool, slot))
      {
        continue;
      }
      OriginalEntity &entity = pool.slot(slot);
      // The one exemption. A target cursor that stops updating stops tracking
      // the enemy it is drawn over, and the cursor is on screen through the
      // whole summon.
      if (entity.typeId00 == 0x192)
      {
        continue;
      }
      entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 | 0x800u);
    }
  }

  void SummonStage::FUN_002de500_release_field(EntityPool &pool)
  {
    for (std::size_t slot = 0; slot < pool.slotCount(); ++slot)
    {
      if (!live(pool, slot))
      {
        continue;
      }
      OriginalEntity &entity = pool.slot(slot);
      entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 & 0xF7FFu);
    }
  }

  void SummonStage::FUN_002de548_release_hurt(EntityPool &pool)
  {
    for (std::size_t slot = 0; slot < pool.slotCount(); ++slot)
    {
      if (!live(pool, slot))
      {
        continue;
      }
      OriginalEntity &entity = pool.slot(slot);
      // +0xBE read as a signed short, the way the original does: anything that
      // took damage this frame is let go so it can play its reaction while the
      // rest of the field is still held.
      if (static_cast<std::int16_t>(entity.pendingDamageBe) >= 1)
      {
        entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 & 0xF7FFu);
      }
      // And type 0x74 unconditionally, hit or not. It is re-tested after the
      // damage branch rather than beside it, so an entity that is both gets
      // the bit cleared twice, which is the same thing.
      if (entity.typeId00 == 0x74)
      {
        entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 & 0xF7FFu);
      }
    }
  }

  void SummonStage::FUN_002de640_release_one(OriginalEntity &entity)
  {
    entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 & 0xF7FFu);
  }

  void SummonStage::FUN_002de640_release_one(EntityPool &pool, std::int32_t slot)
  {
    if (slot < 0 || static_cast<std::size_t>(slot) >= pool.slotCount())
    {
      return;
    }
    FUN_002de640_release_one(pool.slot(static_cast<std::size_t>(slot)));
  }

  // ---------------------------------------------------------------- the dim

  void SummonStage::FUN_002d6e20_build_dim_set(const EntityPool &pool)
  {
    DAT_0058bb00_dimSet_.fill(0xFFFFFFFFu);
    // Slots 0 and 1 are never in the set: the player and the one slot beside
    // him are what the stage is lit *for*.
    DAT_0058bb00_dimSet_[0] &= 0xFFFFFFFCu;

    for (std::size_t slot = 2; slot < pool.slotCount() && slot < 0x100u; ++slot)
    {
      bool drop = !live(pool, slot);
      if (!drop)
      {
        const OriginalEntity &entity = pool.slot(slot);
        // +0x96 bits 0 and 1. Only bit 0 is ever written -- FUN_0023F8B8's
        // "bound into an actor record" -- so this keeps every battle
        // participant out of the set; they are dimmed by the battle's own
        // paths, not by this one. The port models +0x96 twice because an
        // entity is never both kinds; the other copy carries only bit 0x40,
        // which this mask does not test.
        if ((entity.battleFlags96 & 3u) != 0)
        {
          drop = true;
        }
        // And anything already fading of its own accord is left alone.
        else if (entity.fadeLevel134 != 0)
        {
          drop = true;
        }
      }
      if (drop)
      {
        DAT_0058bb00_dimSet_[slot >> 5] &= ~(1u << (slot & 31u));
      }
    }
  }

  void SummonStage::FUN_002d6f38_exclude(std::int32_t slot)
  {
    if (slot < 0 || static_cast<std::size_t>(slot) >= kEntitySlotCount)
    {
      return;
    }
    const std::size_t index = static_cast<std::size_t>(slot);
    DAT_0058bb00_dimSet_[index >> 5] &= ~(1u << (index & 31u));
  }

  void SummonStage::FUN_002d6fa0_apply(EntityPool &pool, std::uint8_t level) const
  {
    for (std::size_t word = 0; word < DAT_0058bb00_dimSet_.size(); ++word)
    {
      const std::uint32_t bits = DAT_0058bb00_dimSet_[word];
      if (bits == 0)
      {
        continue;
      }
      for (std::size_t bit = 0; bit < 32; ++bit)
      {
        if (((bits >> bit) & 1u) == 0)
        {
          continue;
        }
        const std::size_t slot = word * 32 + bit;
        if (slot >= pool.slotCount() || !live(pool, slot))
        {
          continue;
        }
        pool.slot(slot).fadeLevel134 = level;
      }
    }
  }

  // ------------------------------------------------------------- the camera

  void SummonStage::FUN_00266a78_build_eye(std::span<const Vec3> points)
  {
    buildCurve(eye_, points);
  }

  void SummonStage::FUN_00266a78_build_look_at(std::span<const Vec3> points)
  {
    buildCurve(lookAt_, points);
  }

  Vec3 SummonStage::FUN_00266ce8_sample_eye(float t) const { return sampleCurve(eye_, t); }

  Vec3 SummonStage::FUN_00266ce8_sample_look_at(float t) const
  {
    return sampleCurve(lookAt_, t);
  }

  // -------------------------------------------------------------- the ramps

  void SummonStage::armCurves(std::int16_t eyeDuration, std::int16_t lookAtDuration)
  {
    DAT_00355574_eyeDuration_ = eyeDuration;
    DAT_00355576_lookAtDuration_ = lookAtDuration;
    DAT_00355578_eyeElapsed_ = 0;
    DAT_0035557a_lookAtElapsed_ = 0;
  }

  float SummonStage::stepEye(std::uint32_t frameTicks)
  {
    // The original adds the tick count as a *halfword* and only then widens it
    // to compare against the duration, so the elapsed value wraps at 0x10000
    // rather than saturating. Neither duration is anywhere near that.
    const std::int16_t stepped = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(DAT_00355578_eyeElapsed_) +
        static_cast<std::uint16_t>(frameTicks & 0xFFFFu));
    DAT_00355578_eyeElapsed_ =
        (stepped > DAT_00355574_eyeDuration_) ? DAT_00355574_eyeDuration_ : stepped;
    if (DAT_00355574_eyeDuration_ == 0)
    {
      return 0.0f;
    }
    return static_cast<float>(DAT_00355578_eyeElapsed_) /
           static_cast<float>(DAT_00355574_eyeDuration_);
  }

  float SummonStage::stepLookAt(std::uint32_t frameTicks)
  {
    const std::int16_t stepped = static_cast<std::int16_t>(
        DAT_0035557a_lookAtElapsed_ + static_cast<std::int16_t>(frameTicks));
    DAT_0035557a_lookAtElapsed_ =
        (stepped > DAT_00355576_lookAtDuration_) ? DAT_00355576_lookAtDuration_ : stepped;
    if (DAT_00355576_lookAtDuration_ == 0)
    {
      return 0.0f;
    }
    return static_cast<float>(DAT_0035557a_lookAtElapsed_) /
           static_cast<float>(DAT_00355576_lookAtDuration_);
  }

  SummonStage &DAT_0058bb00_summonStage()
  {
    static SummonStage stage{};
    return stage;
  }

} // namespace orphen::ported::entity
