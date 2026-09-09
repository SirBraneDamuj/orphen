#include "ported/battle/battle_target_markers.h"

namespace orphen::ported::battle
{
  namespace
  {
    using orphen::ported::entity::EntityPool;
    using orphen::ported::entity::OriginalEntity;

    // The one predicate FUN_002481F0 uses in three places: a row is worth
    // aiming at while its entity still has a type and hit points, and -- for
    // the thirteen types 0x6E..0x7A -- while its +0x60 is still zero.
    bool aimable(const EntityPool &pool, std::int32_t slot)
    {
      if (slot < 0 || static_cast<std::size_t>(slot) >= pool.slotCount())
      {
        return false;
      }
      const OriginalEntity &entity = pool.slot(static_cast<std::size_t>(slot));
      if (entity.typeId00 == 0 || static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        return false;
      }
      const std::uint16_t band = static_cast<std::uint16_t>(entity.typeId00 - 0x6E);
      return band > 0x0C || entity.state60 == 0;
    }
  } // namespace

  void TargetMarkerTable::FUN_00267e78_clear()
  {
    entries_.fill(TargetMarker{});
  }

  void TargetMarkerTable::FUN_00247f18_register(std::int16_t count)
  {
    // Every caller passes 0x14, which is exactly the 400 bytes FUN_00267E78
    // clears; the clamp is here so a wrong one cannot walk off the array.
    count_ = count > static_cast<std::int16_t>(kMarkerCount)
                 ? static_cast<std::int16_t>(kMarkerCount)
                 : count;
  }

  std::int32_t TargetMarkerTable::FUN_00247f28_mark(EntityPool &pool,
                                                    std::int32_t entitySlot,
                                                    std::int16_t index,
                                                    std::int16_t kind,
                                                    std::int8_t depthOverride)
  {
    // :12. The bound is `sGpffffaf54 < index`, not `<=`: the original will
    // write row 20 of a twenty-row table. The port keeps the comparison and
    // clamps the store, because a real out-of-range index never happens --
    // FUN_0027D8D0 stops looking at 19.
    if (count_ < index || index < 0 || static_cast<std::size_t>(index) >= kMarkerCount)
    {
      return -1;
    }
    if (entitySlot < 0 || static_cast<std::size_t>(entitySlot) >= pool.slotCount())
    {
      return -1;
    }

    TargetMarker &row = entries_[static_cast<std::size_t>(index)];
    row.slot02 = static_cast<std::int16_t>(entitySlot);
    OriginalEntity &entity = pool.slot(static_cast<std::size_t>(entitySlot));
    entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 | 1u);

    if (kind == 2)
    {
      // FUN_002D8928 is a one-line forward to FUN_002D86B0(slot, &row + 8):
      // the cursor takes its world offset straight out of the row.
      const std::int32_t cursor =
          FUN_002d86b0_spawn_cursor
              ? FUN_002d86b0_spawn_cursor(entitySlot, row.offsetX08, row.offsetY0c, row.offsetZ10,
                                          depthOverride)
              : -1;
      kind = 3;
      row.cursor04 = static_cast<std::int16_t>(cursor);
    }
    row.kind00 = kind;
    return index;
  }

  void TargetMarkerTable::FUN_00248040_unmark(EntityPool &pool, std::int32_t entitySlot)
  {
    if (entitySlot >= 0 && static_cast<std::size_t>(entitySlot) < pool.slotCount())
    {
      OriginalEntity &entity = pool.slot(static_cast<std::size_t>(entitySlot));
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 & 0xFEu);
    }
    for (std::int16_t index = 0; index < count_ && static_cast<std::size_t>(index) < kMarkerCount;
         ++index)
    {
      TargetMarker &row = entries_[static_cast<std::size_t>(index)];
      if (row.slot02 != static_cast<std::int16_t>(entitySlot) || row.kind00 <= 1)
      {
        continue;
      }
      if (FUN_00265ec0_destroy && row.cursor04 >= 0)
      {
        FUN_00265ec0_destroy(row.cursor04);
      }
      row = TargetMarker{};
      return;
    }
  }

  void TargetMarkerTable::FUN_00248108_service(EntityPool &pool)
  {
    if (count_ == 0)
    {
      return;
    }
    for (std::int16_t index = 0; index < count_ && static_cast<std::size_t>(index) < kMarkerCount;
         ++index)
    {
      const std::int16_t kind = entries_[static_cast<std::size_t>(index)].kind00;
      const std::int32_t slot = entries_[static_cast<std::size_t>(index)].slot02;
      if (kind == 1)
      {
        FUN_00248040_unmark(pool, slot);
      }
      else if (kind == 2)
      {
        FUN_00247f28_mark(pool, slot, index, 2, 0);
      }
    }
  }

  std::int32_t TargetMarkerTable::FUN_002481f0_cycle(const EntityPool &pool,
                                                     std::int32_t current,
                                                     std::int16_t direction,
                                                     bool halfWrap) const
  {
    const std::int16_t count = count_;
    // :16-32. Find the row holding `current`. The first row is tested on its
    // own because the loop's `while` tail re-tests the kind, so a match on row
    // zero has to skip it.
    std::int32_t scanned = 0;
    std::int16_t at = 0;
    if (count > 0)
    {
      if (entries_[0].slot02 == static_cast<std::int16_t>(current) && entries_[0].kind00 > 1)
      {
        at = 0;
      }
      else
      {
        while (true)
        {
          ++scanned;
          if (scanned >= count)
          {
            break;
          }
          if (entries_[static_cast<std::size_t>(scanned)].slot02 ==
                  static_cast<std::int16_t>(current) &&
              entries_[static_cast<std::size_t>(scanned)].kind00 >= 2)
          {
            break;
          }
        }
        at = static_cast<std::int16_t>(scanned);
      }
    }

    // :33. Running off the end means "the target is not in the table", and the
    // search that follows starts forwards from wherever it stopped.
    if (scanned == count)
    {
      direction = 1;
    }
    const std::int16_t currentRow = at;
    std::int16_t next = static_cast<std::int16_t>(currentRow + direction);
    if (direction == 0)
    {
      // :36-46. A query rather than a step: hand `current` straight back when
      // it is still aimable, and only look for another when it is not.
      direction = 1;
      const bool alive = current >= 0 &&
                         static_cast<std::size_t>(current) < pool.slotCount() &&
                         pool.slot(static_cast<std::size_t>(current)).typeId00 != 0 &&
                         static_cast<std::int16_t>(
                             pool.slot(static_cast<std::size_t>(current)).staggerTimer12a) >= 1;
      if (!alive)
      {
        next = static_cast<std::int16_t>(currentRow + 1);
      }
      else
      {
        const OriginalEntity &entity = pool.slot(static_cast<std::size_t>(current));
        const std::uint16_t band = static_cast<std::uint16_t>(entity.typeId00 - 0x6E);
        if (band > 0x0C)
        {
          return current;
        }
        next = static_cast<std::int16_t>(currentRow + 1);
        if (entity.state60 == 0)
        {
          return current;
        }
      }
    }

    std::int32_t row = next;
    if (halfWrap)
    {
      // Half the table away, which is what makes Up and Down jump across the
      // line of enemies where Left and Right walk it.
      row = static_cast<std::int16_t>(row + (count - (count < 0 ? -1 : 0)) / 2);
    }

    std::int32_t steps = 0;
    if (count > 0)
    {
      while (true)
      {
        if (row < count)
        {
          if (row < 0)
          {
            row = count - 1;
          }
        }
        else
        {
          row = 0;
        }
        const TargetMarker &candidate = entries_[static_cast<std::size_t>(row)];
        if (candidate.kind00 > 1 && aimable(pool, candidate.slot02) &&
            (!halfWrap || row != currentRow))
        {
          break;
        }
        ++steps;
        row = static_cast<std::int16_t>(row + direction);
        if (steps >= count)
        {
          break;
        }
      }
    }
    if (steps != count)
    {
      return entries_[static_cast<std::size_t>(row)].slot02;
    }

    // :86-100. Nothing in the requested direction. One more pass from row zero
    // decides between "there is still something to aim at" and -1 -- and note
    // the original hands back `row`, the index the *first* scan stopped on,
    // not the one this pass found.
    std::int32_t fallback = 0;
    while (fallback < count)
    {
      const TargetMarker &candidate = entries_[static_cast<std::size_t>(fallback)];
      if (candidate.kind00 > 1 && aimable(pool, candidate.slot02))
      {
        break;
      }
      ++fallback;
    }
    if (fallback == count)
    {
      return -1;
    }
    return entries_[static_cast<std::size_t>(row)].slot02;
  }

} // namespace orphen::ported::battle
