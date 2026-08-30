#include "ported/model/entity_animation.h"

#include <algorithm>
#include <span>

namespace orphen::ported::model
{
  namespace
  {
    // FUN_00225c90 line 22 / 143: bit 0x10 of entity +0x06 stops the walk dead.
    constexpr std::uint16_t kAnimationHeld = 0x0010;
    // Bit 0x02 marks "the entry that just started was the last one", which is
    // what makes the next step restart from the top rather than run past it.
    constexpr std::uint16_t kAnimationLooped = 0x0002;
    // Bit 0x04 is the original's "expired this frame" latch. Bit 0x08 marks
    // that a new entry was taken.
    constexpr std::uint16_t kAnimationExpired = 0x0004;
    constexpr std::uint16_t kAnimationStepped = 0x0008;

    constexpr std::int16_t kNeverExpires = 0x7FFF;
    constexpr std::uint16_t kLastEntryBit = 0x8000;

    std::uint16_t u16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    bool fits(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t needed)
    {
      return offset <= bytes.size() && needed <= bytes.size() - offset;
    }
  } // namespace

  std::int16_t FUN_00225c60_duration_to_ticks(int duration)
  {
    if (duration == 9999)
    {
      return kNeverExpires;
    }
    const int ticks = duration << 5;
    return static_cast<std::int16_t>(ticks > 0x7FFE ? 0x7FFE : ticks);
  }

  // FUN_00225c90's **first** branch, the one taken when entity +0x02 has bit
  // 0x200 -- a sprite strip rather than a skinned model.
  //
  // Same shape as the skeletal half and the same +0x06 latches, with three
  // differences that all follow from there being no bones:
  //
  //   entries are four bytes, not six: a column and a duration, with no
  //   trailing event halfword, so +0xAA is never written and a sprite strip
  //   cannot carry the keyframe events the sword swing is driven by;
  //
  //   the animation table is a u32 per animation rather than an eight-byte
  //   record, so the id indexes it by 4;
  //
  //   the blend ratio at +0x13C is left at 1.0. Sprite frames do not
  //   interpolate -- they cut.
  //
  // Duration bit 0x4000 is a conditional entry: skip past it unless +0x06 bit
  // 0x20 is set, which is how one strip carries two variants of the same
  // effect.
  bool FUN_00225c90_advance_sprite_strip(orphen::ported::entity::OriginalEntity &entity,
                                         const Psc3Model &model,
                                         std::uint32_t frameTicks)
  {
    entity.animationBlend13c = 1.0f;

    const std::span<const std::uint8_t> blob{model.blob};
    if (model.animationTableOffset == 0)
    {
      return false;
    }
    if ((entity.flags06 & kAnimationHeld) != 0)
    {
      return true;
    }

    const std::size_t record =
        model.animationTableOffset + static_cast<std::size_t>(entity.animationA0) * 4;
    if (!fits(blob, record, 4))
    {
      return false;
    }
    const std::uint32_t timeline = u32At(blob, record);
    if (timeline == 0)
    {
      return false;
    }

    std::uint16_t flags = static_cast<std::uint16_t>(entity.flags06 & 0xF7);
    std::int32_t countdown = 0;

    // Reads entry `cursor / 2` and publishes its column. Returns the raw
    // duration word so the caller can test bits 0x8000 and 0x4000 on it.
    const auto readEntry = [&](std::uint16_t cursor, std::uint16_t &durationWord) -> bool {
      const std::size_t at = timeline + static_cast<std::size_t>(cursor) * 2;
      if (!fits(blob, at, 4))
      {
        return false;
      }
      const std::uint16_t column = u16At(blob, at + 0);
      durationWord = u16At(blob, at + 2);
      entity.previousPoseColumnAe = entity.poseColumnAc;
      entity.poseColumnAc = static_cast<std::uint16_t>(column & 0x0FFF);
      return true;
    };

    std::uint16_t durationWord = 0;

    if (entity.animationA0 == entity.previousSubstateA2)
    {
      countdown = static_cast<std::int16_t>(entity.stateResetA4);
      if (countdown != kNeverExpires)
      {
        countdown = static_cast<std::int16_t>(countdown - static_cast<int>(frameTicks & 0xFFFF));
        if ((entity.flags06 & kAnimationExpired) == 0)
        {
          if (countdown <= 0)
          {
            countdown = 1;
          }
        }
        else if (countdown > 0)
        {
          countdown = 0;
        }
      }

      if (countdown <= 0)
      {
        flags = static_cast<std::uint16_t>(entity.flags06 & 0xF3);
        if ((entity.flags06 & kAnimationLooped) != 0)
        {
          // The strip ended last frame: restart at entry 0 rather than running
          // off it.
          flags = static_cast<std::uint16_t>(entity.flags06 & 0xF0);
          entity.timelineCursorA8 = 0;
          if (!readEntry(0, durationWord))
          {
            return false;
          }
        }
        else
        {
          // Walk forward until an entry that is not conditionally skipped.
          for (;;)
          {
            entity.timelineCursorA8 = static_cast<std::uint16_t>(entity.timelineCursorA8 + 2);
            if (!readEntry(entity.timelineCursorA8, durationWord))
            {
              return false;
            }
            if ((durationWord & 0x4000) == 0 || (flags & 0x0020) != 0)
            {
              break;
            }
          }
        }
        durationWord = static_cast<std::uint16_t>(durationWord & 0xBFFF);

        if ((durationWord & kLastEntryBit) != 0)
        {
          durationWord = static_cast<std::uint16_t>(durationWord & 0x7FFF);
          flags |= kAnimationLooped;
        }
        std::int32_t ticks = FUN_00225c60_duration_to_ticks(durationWord);
        entity.keyframeTicksA6 = static_cast<std::uint16_t>(ticks);
        if (ticks != kNeverExpires)
        {
          ticks = static_cast<std::int16_t>(ticks + countdown);
        }
        countdown = ticks < 1 ? 1 : ticks;
      }
    }
    else
    {
      flags = static_cast<std::uint16_t>(entity.flags06 & 0xF0);
      entity.previousSubstateA2 = entity.animationA0;
      entity.timelineCursorA8 = 0;
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFBF);
      if (!readEntry(0, durationWord))
      {
        return false;
      }
      if ((durationWord & kLastEntryBit) != 0)
      {
        durationWord = static_cast<std::uint16_t>(durationWord & 0x7FFF);
        flags |= kAnimationLooped;
      }
      countdown = FUN_00225c60_duration_to_ticks(durationWord);
      entity.keyframeTicksA6 = static_cast<std::uint16_t>(countdown);
    }

    if (static_cast<int>(frameTicks) >= countdown)
    {
      flags |= kAnimationExpired;
      if ((flags & kAnimationLooped) != 0)
      {
        flags |= 0x0001;
      }
    }

    entity.stateResetA4 = static_cast<std::uint16_t>(countdown);
    entity.flags06 = flags;
    return true;
  }

  bool FUN_00225c90_advance_animation(orphen::ported::entity::OriginalEntity &entity,
                                      const Psc3Model &model,
                                      std::uint32_t frameTicks)
  {
    // FUN_00225c90's very first test. The two halves share nothing but the
    // latches they write.
    if ((entity.descriptorFlags02 & 0x0200u) != 0)
    {
      return FUN_00225c90_advance_sprite_strip(entity, model, frameTicks);
    }
    // The original sets the blend ratio to 1.0 up front and only narrows it at
    // the end, so every early return leaves the pose fully at its column.
    entity.animationBlend13c = 1.0f;

    const std::span<const std::uint8_t> blob{model.blob};
    if (model.animationTableOffset == 0)
    {
      return false;
    }
    if ((entity.flags06 & kAnimationHeld) != 0)
    {
      return true; // held, not broken
    }

    // FUN_00225c90 line 27: PSC3 header +0x06 is the animation count, and an id
    // at or past it is the ER_ANIM case rather than a silent clamp.
    if (entity.animationA0 >= model.animationCount)
    {
      return false;
    }

    const std::size_t record =
        model.animationTableOffset + static_cast<std::size_t>(entity.animationA0) * 8;
    if (!fits(blob, record, 8))
    {
      return false;
    }
    const std::uint32_t timeline = u32At(blob, record);
    if (timeline == 0)
    {
      return false;
    }

    std::uint16_t flags = static_cast<std::uint16_t>(entity.flags06 & 0xF7);
    int countdown = 0;
    bool tookEntry = false;

    const auto readEntry = [&](std::size_t entryIndex) -> bool {
      const std::size_t at = timeline + entryIndex * 6;
      if (!fits(blob, at, 6))
      {
        return false;
      }
      const std::uint16_t column = u16At(blob, at + 0);
      std::uint16_t duration = u16At(blob, at + 2);
      const std::uint16_t trailing = u16At(blob, at + 4);

      entity.previousPoseColumnAe = entity.poseColumnAc;
      entity.poseColumnAc = column;
      entity.flagsAa = trailing;

      if ((duration & kLastEntryBit) != 0)
      {
        duration = static_cast<std::uint16_t>(duration & 0x7FFF);
        flags |= kAnimationLooped;
      }
      entity.keyframeTicksA6 = static_cast<std::uint16_t>(FUN_00225c60_duration_to_ticks(duration));
      return true;
    };

    if (entity.animationA0 == entity.previousSubstateA2)
    {
      // Same animation still running: burn this frame's ticks off the
      // countdown, and only step when it runs out.
      countdown = static_cast<std::int16_t>(entity.stateResetA4);
      if (countdown != kNeverExpires)
      {
        countdown = static_cast<std::int16_t>(countdown - static_cast<int>(frameTicks & 0xFFFF));
        if ((entity.flags06 & kAnimationExpired) == 0)
        {
          if (countdown <= 0)
          {
            countdown = 1;
          }
        }
        else if (countdown > 0)
        {
          countdown = 0;
        }
      }

      if (countdown <= 0)
      {
        flags = static_cast<std::uint16_t>(entity.flags06 & 0xF3);
        if ((entity.flags06 & kAnimationLooped) == 0)
        {
          entity.timelineCursorA8 = static_cast<std::uint16_t>(entity.timelineCursorA8 + 2);
        }
        else
        {
          flags = static_cast<std::uint16_t>(entity.flags06 & 0xF0);
          // Bit 0x80 holds the cursor at the end instead of restarting.
          if ((entity.flags06 & 0x80) == 0)
          {
            entity.timelineCursorA8 = 0;
          }
        }

        // The cursor steps by 2 while an entry is 6 bytes: the original indexes
        // `timeline + (cursor / 2) * 3` over halfwords.
        if (!readEntry(static_cast<std::int16_t>(entity.timelineCursorA8) / 2))
        {
          return false;
        }
        tookEntry = true;

        const std::int16_t ticks = static_cast<std::int16_t>(entity.keyframeTicksA6);
        countdown = ticks != kNeverExpires ? static_cast<std::int16_t>(ticks + countdown) : ticks;
        if (countdown < 1)
        {
          countdown = 1;
        }
      }
    }
    else
    {
      // A different animation was selected: restart from the first entry.
      flags = static_cast<std::uint16_t>(entity.flags06 & 0xF0);
      entity.previousSubstateA2 = entity.animationA0;
      entity.timelineCursorA8 = 0;
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFF3F);
      if (!readEntry(0))
      {
        return false;
      }
      tookEntry = true;
      countdown = static_cast<std::int16_t>(entity.keyframeTicksA6);
    }

    if (tookEntry)
    {
      flags |= kAnimationStepped;
    }

    if (static_cast<int>(frameTicks) >= countdown)
    {
      flags |= kAnimationExpired;
      if ((flags & kAnimationLooped) != 0)
      {
        flags |= 0x0001;
      }
    }

    // The blend ratio the pose sampler reads. Narrower than 1.0 only while a
    // keyframe still has more ticks left than this frame consumes.
    //
    // FUN_00225c90 line 130 gates the whole thing on entity +0x08 bit 0x10 --
    // the entity was not drawn last frame, so there is no previous pose to ease
    // out of and the keyframe snaps. FUN_0020c810 sets that bit when it culls
    // and consumes it on the next draw.
    if ((entity.halfword08 & 0x0010) != 0)
    {
      entity.animationBlend13c = 1.0f;
    }
    else if (static_cast<int>(frameTicks) < countdown)
    {
      entity.animationBlend13c = static_cast<float>(frameTicks) / static_cast<float>(countdown);
    }
    else
    {
      entity.animationBlend13c = 1.0f;
    }

    entity.stateResetA4 = static_cast<std::uint16_t>(countdown);
    entity.flags06 = flags;
    return true;
  }

} // namespace orphen::ported::model
