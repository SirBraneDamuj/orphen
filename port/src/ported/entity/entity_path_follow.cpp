#include "ported/entity/entity_path_follow.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_0035271c, read out of the save state: 2*pi. FUN_002446e8 uses it as
    // `(units * 2pi) / 360`, so its turn rates are in degrees.
    constexpr float kTwoPiDAT_0035271c = 6.283185307179586f;
    // DAT_003525f0 / DAT_003525f4, FUN_0023a320's half-degree dead zone.
    constexpr float kTurnDeadzone = 0.008726644329726696f;

    // FUN_0023a320(current, target, rate) -> the step to take, 0 once inside the
    // dead zone. The port keeps hitting this in anonymous namespaces; it is four
    // lines and duplicating it beats exporting it from the player controller.
    float FUN_0023a320_approach(float current, float target, float rate)
    {
      float difference = target - current;
      while (difference > 3.141592025756836f)
      {
        difference -= 2.0f * 3.141592025756836f;
      }
      while (difference < -3.141592025756836f)
      {
        difference += 2.0f * 3.141592025756836f;
      }
      if (difference > kTurnDeadzone)
      {
        return std::min(difference, rate);
      }
      if (difference < -kTurnDeadzone)
      {
        return std::max(difference, -rate);
      }
      return 0.0f;
    }
  } // namespace

  void PathFollowerTable::reset()
  {
    slots_ = {};
    started_ = 0;
  }

  // FUN_00266a78(slot + 0x6A, points, count, mode). Bit 30 of the duration picks
  // the knot spacing; every call in the executable has it clear, which is the
  // uniform i/(n-1) spacing the camera path already uses.
  void PathFollowerTable::buildSpline(PathFollower &slot)
  {
    const std::size_t count = slot.waypointCount;
    std::array<float, kMaxPathWaypoints> knots{};
    std::array<float, kMaxPathWaypoints> channel{};
    const float last = count > 1 ? static_cast<float>(count - 1) : 1.0f;
    for (std::size_t index = 0; index < count; ++index)
    {
      knots[index] = static_cast<float>(index) / last;
    }
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      for (std::size_t index = 0; index < count; ++index)
      {
        const Vec3 &point = slot.waypoints[index];
        channel[index] = axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
      }
      slot.spline[axis].build(std::span<const float>(knots.data(), count),
                              std::span<const float>(channel.data(), count));
    }
  }

  PathFollower *PathFollowerTable::FUN_00244318_allocate()
  {
    // FUN_00244318:32-40. A slot counts as free when its mode is 0, when it has
    // no entity, or when that entity's type id has been cleared -- the original
    // tests `**(short **)(psVar2 + 8) == 0`, the entity's +0x00.
    for (auto &slot : slots_)
    {
      if (slot.mode00 == 0 || slot.entitySlot10 < 0)
      {
        return &slot;
      }
    }
    return nullptr;
  }

  int PathFollowerTable::FUN_002443f8_start(std::size_t entitySlot,
                                            std::span<const Vec3> waypoints,
                                            std::uint32_t duration)
  {
    PathFollower *slot = FUN_00244318_allocate();
    if (slot == nullptr)
    {
      // FUN_002443f8 seeds its result with -1 and only overwrites it once a slot
      // was taken, so a full table reports failure -- and the script's `only
      // install the wait loop if this succeeded` shape depends on that.
      return -1;
    }

    *slot = PathFollower{};
    const std::size_t count = std::min<std::size_t>(waypoints.size(), kMaxPathWaypoints);
    slot->waypointCount = count;
    std::copy_n(waypoints.begin(), count, slot->waypoints.begin());
    slot->count02 = static_cast<std::uint16_t>(count);

    slot->mode00 = 1;
    // FUN_002443f8:56-62. Two thresholds, and the second reads the *duration*
    // again after it was stored, before the <<4 -- so both compare the same
    // number and a short path simply turns faster.
    if ((duration & 0xFFFFu) < 100)
    {
      slot->mode00 = 2;
    }
    if ((duration & 0xFFFFu) < 0x32)
    {
      slot->mode00 = 3;
    }
    slot->total04 = static_cast<std::uint16_t>((duration & 0xFFFFu) << 4);
    slot->elapsed06 = 0;
    slot->entitySlot10 = static_cast<std::int32_t>(entitySlot);

    buildSpline(*slot);
    ++started_;
    return 1;
  }

  int PathFollowerTable::FUN_0024a870_start_return_walk(std::size_t entitySlot, const Vec3 &from,
                                                        const Vec3 &mid, const Vec3 &home,
                                                        std::uint16_t duration, bool raiseY)
  {
    PathFollower *slot = FUN_00244318_allocate();
    if (slot == nullptr)
    {
      return 0;
    }
    *slot = PathFollower{};
    slot->mode00 = 1;
    slot->count02 = 3;
    slot->waypointCount = 3;
    slot->waypoints[0] = from;
    slot->waypoints[1] = mid;
    slot->waypoints[2] = home;
    // FUN_0030bd20(distance * 200.0) straight into +0x04. FUN_002443f8's `<< 4`
    // is not applied here, so this walk runs sixteen times faster per unit than
    // a scripted path of the same nominal length.
    slot->total04 = duration;
    slot->elapsed06 = 0;
    slot->entitySlot10 = static_cast<std::int32_t>(entitySlot);
    buildSpline(*slot);
    slot->steer09 = 0xB5;
    slot->flags08 = static_cast<std::uint16_t>(slot->flags08 | 2u);
    (void)raiseY; // the caller sets entity +0x04 bit 3; FUN_002446e8 reads it there
    ++started_;
    return 1;
  }

  int PathFollowerTable::FUN_002445c8_progress(std::size_t entitySlot) const
  {
    for (const auto &slot : slots_)
    {
      if (slot.mode00 == 0 || slot.entitySlot10 != static_cast<std::int32_t>(entitySlot))
      {
        continue;
      }
      if (slot.total04 == 0)
      {
        // The original traps here rather than dividing by zero.
        return 0;
      }
      return static_cast<int>((static_cast<std::uint32_t>(slot.total04 - slot.elapsed06) * 1000u) /
                              slot.total04) +
             1;
    }
    // Not found: the walk is over. This is the value the script's wait loop is
    // spinning for.
    return 0;
  }

  void PathFollowerTable::FUN_002446e8_update(EntityPool &pool, std::uint32_t frameTicks)
  {
    for (auto &slot : slots_)
    {
      if (slot.mode00 == 0 || slot.entitySlot10 < 0)
      {
        continue;
      }
      if (static_cast<std::size_t>(slot.entitySlot10) >= EntityPool::slotCount())
      {
        continue;
      }
      OriginalEntity &entity = pool.slot(static_cast<std::size_t>(slot.entitySlot10));

      // FUN_002446e8:46-50. Arriving frees the slot, and *that* is what makes
      // the script's 0x72 poll finally read 0.
      if (slot.elapsed06 == slot.total04)
      {
        slot.entitySlot10 = -1;
        slot.mode00 = 0;
        continue;
      }

      const std::uint32_t elapsed = static_cast<std::uint32_t>(slot.elapsed06) + frameTicks;
      slot.elapsed06 = static_cast<std::uint16_t>(std::min<std::uint32_t>(elapsed, slot.total04));

      const float t = slot.total04 != 0
                          ? static_cast<float>(slot.elapsed06) / static_cast<float>(slot.total04)
                          : 1.0f;
      const Vec3 point{slot.spline[0].evaluate(t), slot.spline[1].evaluate(t),
                       slot.spline[2].evaluate(t)};

      // The whole point of the subsystem: a *request*, not a teleport, so the
      // collision clamps and the ground follow still get their say.
      entity.desiredDeltaX30 = point.x - entity.positionX20;
      entity.desiredDeltaZ34 = point.y - entity.positionZ24;
      if ((entity.halfword04 & 8) != 0)
      {
        entity.desiredDeltaY38 = point.z - entity.positionY28;
      }

      // Slot +0x08 bit 0 is the hard-placement variant: no request at all, the
      // entity is written straight onto the curve along with both ground heights.
      if ((slot.flags08 & 1) != 0)
      {
        entity.desiredDeltaX30 = 0.0f;
        entity.desiredDeltaZ34 = 0.0f;
        entity.desiredDeltaY38 = 0.0f;
        entity.positionX20 = point.x;
        entity.positionZ24 = point.y;
        entity.positionY28 = point.z;
        entity.groundHeight4c = point.z;
        entity.previousGroundHeight50 = point.z;
      }

      // Steering. Below 0xB5 the actor faces the way it is travelling, with the
      // byte doubled and read as degrees of constant offset; at or above, the
      // byte selects a waypoint index to face instead.
      float towardX = entity.desiredDeltaX30;
      float towardY = entity.desiredDeltaZ34;
      if (slot.steer09 >= 0xB5)
      {
        std::size_t index = static_cast<std::size_t>(slot.steer09 - 0xB5);
        if (slot.waypointCount != 0)
        {
          index = std::min(index, slot.waypointCount - 1);
          towardX = slot.waypoints[index].x - entity.positionX20;
          towardY = slot.waypoints[index].y - entity.positionZ24;
        }
      }

      const float target = std::atan2(towardY, towardX);
      const float rate =
          (static_cast<float>(slot.mode00 * frameTicks) * kTwoPiDAT_0035271c) / 360.0f;
      const float step = FUN_0023a320_approach(entity.facingRadians5c, target, rate);

      if ((slot.flags08 & 2) == 0)
      {
        // A zero step means the turn is inside the dead zone, so it snaps rather
        // than creeping the last half degree forever.
        entity.facingRadians5c = step == 0.0f ? target : entity.facingRadians5c + step;
        if (slot.steer09 < 0xB5)
        {
          entity.facingRadians5c +=
              (static_cast<float>(slot.steer09) * 2.0f * kTwoPiDAT_0035271c) / 360.0f;
        }
      }

      // FUN_002446e8:100-105 fires a footstep sound here, gated on the entity's
      // +0x06 and a bit of +0xAA selected by slot +0x0B. Not wired up: the sound
      // ids come from the same record and nothing this scene starts sets one.
    }
  }

  std::uint32_t PathFollowerTable::activeCount() const
  {
    std::uint32_t active = 0;
    for (const auto &slot : slots_)
    {
      if (slot.mode00 != 0 && slot.entitySlot10 >= 0)
      {
        ++active;
      }
    }
    return active;
  }

} // namespace orphen::ported::entity
