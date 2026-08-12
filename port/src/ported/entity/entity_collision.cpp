#include "ported/entity/entity_collision.h"

#include <cmath>
#include <cstdint>

namespace orphen::ported::entity
{
  namespace
  {
    EntityCollisionStats g_stats;

    // Read out of the save state at 0x0035247C / 0x00352480 / 0x00352484. The
    // table repeats the same triple four times, once per direction
    // (0x00352488.., 0x00352494..), so one set of constants covers all four
    // functions -- the originals just each reach for their own copy.
    constexpr float kShoveDAT_0035247c = 0.02f;
    constexpr float kSkinDAT_00352480 = 0.001f;
    constexpr float kTouchDAT_00352484 = 0.01f;

    // The solve workspace FUN_002262c0 stacks at DAT_70000000, reduced to the
    // fields these four read. Offsets are the workspace's own.
    struct Solve
    {
      OriginalEntity *self = nullptr; // +0x128
      std::size_t selfSlot = 0;
      float selfX = 0.0f;             // +0x134
      float selfY = 0.0f;             // +0x138
      float deltaX = 0.0f;            // +0x140
      float deltaY = 0.0f;            // +0x144
      float height = 0.0f;            // +0x14C
      float radius = 0.0f;            // +0x150
      float heading = 0.0f;           // +0x154, FUN_00305408(deltaY, deltaX)
      std::uint16_t flags04 = 0;      // +0x160, the entity's own +0x04
    };

    bool live(const EntityPool &pool, std::size_t index)
    {
      // `'\0' < (char)(&DAT_005a96b0)[i]` -- strictly positive, so an Allocated
      // slot (-1) is skipped as well as a Free one.
      return static_cast<std::int8_t>(pool.status(index)) > 0;
    }

    // The vertical overlap test all four share: the two capsules' [z, z+height)
    // bands must intersect. Note it is the *workspace* height on one side and
    // the other entity's +0x58 on the other.
    bool verticalOverlap(const Solve &solve, const OriginalEntity &other)
    {
      const float selfZ = solve.self->positionY28;
      return other.positionY28 < selfZ + solve.height &&
             selfZ < other.positionY28 + other.height58;
    }

    // Common to all four: skip self, skip anything whose +0x04 bit 0 is set,
    // and skip a pair that shares bit 0x2000.
    bool candidate(const Solve &solve, std::size_t index, const OriginalEntity &other)
    {
      return index != solve.selfSlot && (other.halfword04 & 1) == 0 &&
             (solve.flags04 & other.halfword04 & 0x2000) == 0;
    }

    // FUN_00228380:39-63. The shove: the blocker is nudged along the *mover's*
    // heading, not along the contact normal, and only when the gap has already
    // closed to within 0.01. Suppressed if the mover carries +0x04 bit 0x40 or
    // the blocker carries bit 0x80 -- "I do not push" and "I am not pushed".
    void applyContact(Solve &solve, std::size_t index, OriginalEntity &other, float gap)
    {
      const bool touching = gap <= kTouchDAT_00352484;
      ++g_stats.clamps;
      solve.self->blockedBy64 = static_cast<std::int32_t>(index);
      // The original ORs 0x20 into the workspace's result word at +0x12C and
      // merges it into the entity later; the port has nowhere else to put it.
      solve.self->collisionFlags0c |= 0x20u;
      if (touching && (solve.flags04 & 0x40) == 0 && (other.halfword04 & 0x80) == 0)
      {
        other.desiredDeltaX30 += std::cos(solve.heading) * kShoveDAT_0035247c;
        other.desiredDeltaZ34 += std::sin(solve.heading) * kShoveDAT_0035247c;
        ++g_stats.shoves;
      }
    }

    // src/FUN_00228380.c -- travelling +X.
    void FUN_00228380(EntityPool &pool, Solve &solve)
    {
      // The function's own first test: self +0x04 bit 0 disables entity
      // collision for this entity outright, as mover as well as blocker.
      if ((solve.flags04 & 1) != 0)
      {
        return;
      }

      float allowed = solve.deltaX; // +0x15C
      const float r = solve.radius;
      const float xEdge = solve.selfX + r;
      float yHigh = solve.self->positionZ24 + r;
      float yLow = solve.self->positionZ24 - r;
      // The sideways sweep is widened on whichever side the Y request is
      // heading, so a diagonal move tests the volume it will actually pass
      // through rather than the one it starts in.
      if (solve.deltaY > 0.0f)
      {
        yHigh += solve.deltaY;
      }
      else
      {
        yLow += solve.deltaY;
      }

      for (std::size_t index = 0; index < EntityPool::slotCount(); ++index)
      {
        if (!live(pool, index))
        {
          continue;
        }
        OriginalEntity &other = pool.slot(index);
        if (!candidate(solve, index, other) || !(solve.selfX < other.positionX20))
        {
          continue;
        }
        const float otherRadius = other.radius54;
        float gap = ((other.positionX20 - otherRadius) - xEdge) - kSkinDAT_00352480;
        if (gap < allowed && (other.positionZ24 - otherRadius) < yHigh &&
            yLow < (other.positionZ24 + otherRadius) && verticalOverlap(solve, other))
        {
          if (gap < 0.0f)
          {
            gap = 0.0f;
          }
          allowed = gap;
          applyContact(solve, index, other, gap);
        }
      }

      if (solve.deltaX != allowed)
      {
        solve.deltaX = allowed;
      }
    }

    // src/FUN_002285d8.c -- travelling -X. Mirrors the above: +0x15C holds the
    // *magnitude*, and the writeback negates it.
    void FUN_002285d8(EntityPool &pool, Solve &solve)
    {
      // The function's own first test: self +0x04 bit 0 disables entity
      // collision for this entity outright, as mover as well as blocker.
      if ((solve.flags04 & 1) != 0)
      {
        return;
      }

      float allowed = -solve.deltaX;
      const float r = solve.radius;
      const float xEdge = solve.selfX - r;
      float yHigh = solve.self->positionZ24 + r;
      float yLow = solve.self->positionZ24 - r;
      if (solve.deltaY > 0.0f)
      {
        yHigh += solve.deltaY;
      }
      else
      {
        yLow += solve.deltaY;
      }

      for (std::size_t index = 0; index < EntityPool::slotCount(); ++index)
      {
        if (!live(pool, index))
        {
          continue;
        }
        OriginalEntity &other = pool.slot(index);
        if (!candidate(solve, index, other) || !(other.positionX20 < solve.selfX))
        {
          continue;
        }
        const float otherRadius = other.radius54;
        float gap = (xEdge - (other.positionX20 + otherRadius)) - kSkinDAT_00352480;
        if (gap < allowed && (other.positionZ24 - otherRadius) < yHigh &&
            yLow < (other.positionZ24 + otherRadius) && verticalOverlap(solve, other))
        {
          if (gap < 0.0f)
          {
            gap = 0.0f;
          }
          allowed = gap;
          applyContact(solve, index, other, gap);
        }
      }

      if (solve.deltaX != allowed)
      {
        solve.deltaX = -allowed;
      }
    }

    // src/FUN_00228838.c -- travelling +Y.
    void FUN_00228838(EntityPool &pool, Solve &solve)
    {
      // The function's own first test: self +0x04 bit 0 disables entity
      // collision for this entity outright, as mover as well as blocker.
      if ((solve.flags04 & 1) != 0)
      {
        return;
      }

      float allowed = solve.deltaY;
      const float r = solve.radius;
      const float yEdge = solve.selfY + r;
      float xHigh = solve.self->positionX20 + r;
      float xLow = solve.self->positionX20 - r;
      if (solve.deltaX > 0.0f)
      {
        xHigh += solve.deltaX;
      }
      else
      {
        xLow += solve.deltaX;
      }

      for (std::size_t index = 0; index < EntityPool::slotCount(); ++index)
      {
        if (!live(pool, index))
        {
          continue;
        }
        OriginalEntity &other = pool.slot(index);
        if (!candidate(solve, index, other) || !(solve.selfY < other.positionZ24))
        {
          continue;
        }
        const float otherRadius = other.radius54;
        float gap = ((other.positionZ24 - otherRadius) - yEdge) - kSkinDAT_00352480;
        if (gap < allowed && (other.positionX20 - otherRadius) < xHigh &&
            xLow < (other.positionX20 + otherRadius) && verticalOverlap(solve, other))
        {
          if (gap < 0.0f)
          {
            gap = 0.0f;
          }
          allowed = gap;
          applyContact(solve, index, other, gap);
        }
      }

      if (solve.deltaY != allowed)
      {
        solve.deltaY = allowed;
      }
    }

    // src/FUN_00228a90.c -- travelling -Y.
    void FUN_00228a90(EntityPool &pool, Solve &solve)
    {
      // The function's own first test: self +0x04 bit 0 disables entity
      // collision for this entity outright, as mover as well as blocker.
      if ((solve.flags04 & 1) != 0)
      {
        return;
      }

      float allowed = -solve.deltaY;
      const float r = solve.radius;
      const float yEdge = solve.selfY - r;
      float xHigh = solve.self->positionX20 + r;
      float xLow = solve.self->positionX20 - r;
      if (solve.deltaX > 0.0f)
      {
        xHigh += solve.deltaX;
      }
      else
      {
        xLow += solve.deltaX;
      }

      for (std::size_t index = 0; index < EntityPool::slotCount(); ++index)
      {
        if (!live(pool, index))
        {
          continue;
        }
        OriginalEntity &other = pool.slot(index);
        if (!candidate(solve, index, other) || !(other.positionZ24 < solve.selfY))
        {
          continue;
        }
        const float otherRadius = other.radius54;
        float gap = (yEdge - (other.positionZ24 + otherRadius)) - kSkinDAT_00352480;
        if (gap < allowed && (other.positionX20 - otherRadius) < xHigh &&
            xLow < (other.positionX20 + otherRadius) && verticalOverlap(solve, other))
        {
          if (gap < 0.0f)
          {
            gap = 0.0f;
          }
          allowed = gap;
          applyContact(solve, index, other, gap);
        }
      }

      if (solve.deltaY != allowed)
      {
        solve.deltaY = -allowed;
      }
    }
  } // namespace

  void FUN_002262c0_clamp_movement_against_entities(EntityPool &pool, std::size_t slot)
  {
    OriginalEntity &entity = pool.slot(slot);

    // FUN_002262c0:37 clears +0x64 before anything else runs, whether or not a
    // clamp fires this frame.
    entity.blockedBy64 = -1;

    // The clamp block sits behind `if (+0x30 != 0) ... else if (+0x34 != 0)`,
    // so a stationary entity never walks the pool. That matters: this is an
    // O(256) sweep per moving entity, and in a normal scene almost nothing is
    // moving.
    if (entity.desiredDeltaX30 == 0.0f && entity.desiredDeltaZ34 == 0.0f)
    {
      return;
    }

    // FUN_002262c0:36 -- the gate the whole solve is behind.
    if ((entity.halfword04 & 0x100) != 0)
    {
      return;
    }

    ++g_stats.sweeps;

    Solve solve;
    solve.self = &entity;
    solve.selfSlot = slot;
    solve.selfX = entity.positionX20;
    solve.selfY = entity.positionZ24;
    solve.deltaX = entity.desiredDeltaX30;
    solve.deltaY = entity.desiredDeltaZ34;
    solve.height = entity.height58;
    solve.radius = entity.radius54;
    // FUN_00305408(+0x34, +0x30) at 0x00226794 -- atan2 of the request, which
    // is the heading the shove is applied along.
    solve.heading = std::atan2(entity.desiredDeltaZ34, entity.desiredDeltaX30);
    solve.flags04 = entity.halfword04;

    // FUN_002262c0:0x002267C4..0x00226820, in this order. X resolves first, and
    // the Y pass reads the X request *after* it was clamped, so a diagonal move
    // into a corner narrows twice.
    if (solve.deltaX > 0.0f)
    {
      FUN_00228380(pool, solve);
    }
    else if (solve.deltaX < 0.0f)
    {
      FUN_002285d8(pool, solve);
    }

    if (solve.deltaY > 0.0f)
    {
      FUN_00228838(pool, solve);
    }
    else if (solve.deltaY < 0.0f)
    {
      FUN_00228a90(pool, solve);
    }

    entity.desiredDeltaX30 = solve.deltaX;
    entity.desiredDeltaZ34 = solve.deltaY;
  }

  const EntityCollisionStats &entityCollisionStats() { return g_stats; }
  void resetEntityCollisionStats() { g_stats = EntityCollisionStats{}; }

} // namespace orphen::ported::entity
