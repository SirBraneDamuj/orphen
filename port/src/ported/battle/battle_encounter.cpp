#include "ported/battle/battle_encounter.h"

#include "ported/battle/battle_tables.h"

namespace orphen::ported::battle
{

  void BattleEncounter::FUN_0023f288_reset()
  {
    memory_.clear();
    blob_ = 0;
    actorArray_ = 0;
    masterScript_ = 0;
    placement_ = 0;
    actorCount_ = 0;
    groupActorArrays_.clear();
    DAT_00354fa4_ = 0;
    countdown_ = 0;
    cameraSplines_ = 0;
    cameraSplineCount_ = 0;
    masterPc_ = 0;
    masterYield_ = 0;
    masterTriggers_ = 0;
    masterHalted_ = false;
    masterHaltOpcode_ = 0;
    masterHaltOffset_ = 0;
    actorHalted_ = false;
    actorHaltOpcode_ = 0;
    actorHaltOffset_ = 0;
  }

  bool BattleEncounter::FUN_0023f318_load(const std::vector<std::uint8_t> &script,
                                          std::uint32_t headerWord7)
  {
    FUN_0023f288_reset();
    if (script.size() < headerWord7 + 4u || headerWord7 == 0)
    {
      return false;
    }

    // FUN_0025ba28(0). The section table's first entry is the battle data, and
    // a zero there is how a section-14 scene says it has none.
    memory_ = script;
    const std::uint32_t blob = read<std::uint32_t>(headerWord7);
    if (blob == 0 || blob + 0x18u > memory_.size())
    {
      memory_.clear();
      return false;
    }
    blob_ = (blob + 3u) & ~3u; // :23-25, the same align-up

    // :31-36. DAT_00354F98 is the blob's body and DAT_00354F94 is the base
    // every offset inside it is relative to -- or zero, if bit 31 of the first
    // word says the blob has already been relocated once. The port reloads a
    // fresh copy per scene, so the bit is always clear here; it is reproduced
    // because it is what makes a second FUN_0023f318 on the same blob a no-op.
    const std::uint32_t body = blob_ + 0x10u;
    const bool alreadyRelocated = read<std::int32_t>(blob_) < 0;
    const std::uint32_t base = alreadyRelocated ? 0u : body;
    write<std::uint32_t>(blob_, read<std::uint32_t>(blob_) | 0x80000000u);

    // :38-50. The placement sub-blob, DAT_00354FA8. Six pointers inside it are
    // relocated the same way. FUN_002471e8 and FUN_00247520 walk it for the
    // battle's camera splines and its scripted 0x192 markers; neither is
    // ported yet, so the fixups are all this does with it.
    if (read<std::uint32_t>(blob_ + 4) != 0)
    {
      const std::uint32_t placement = read<std::uint32_t>(blob_ + 4) + base;
      write<std::uint32_t>(blob_ + 4, placement);
      placement_ = placement;
      for (const std::uint32_t field : {0x48u, 0x50u, 0x70u, 0x58u, 0x60u, 0x68u})
      {
        write<std::uint32_t>(placement + field, read<std::uint32_t>(placement + field) + base);
      }
      // FUN_00246fc0's answer, without its per-spline relocation and its
      // thousandths-to-float pass: +0x5C is the number of camera spline pairs
      // and +0x60 the array. FUN_0023c340 only needs the array to be non-null.
      cameraSplineCount_ = read<std::uint32_t>(placement + 0x5C);
      cameraSplines_ = cameraSplineCount_ != 0 ? read<std::uint32_t>(placement + 0x60) : 0;
    }

    // :64-72. The group table: one count, then two offsets per group -- the
    // actor array and the master battle script. FUN_0023f318 takes group 0 and
    // never looks at the rest.
    const std::uint32_t groupCount = read<std::uint32_t>(body);
    for (std::uint32_t index = 0; index < groupCount; ++index)
    {
      const std::uint32_t entry = body + 4u + index * 8u;
      write<std::uint32_t>(entry + 4, read<std::uint32_t>(entry + 4) + (base & ~3u));
      write<std::uint32_t>(entry + 0, read<std::uint32_t>(entry + 0) + (base & ~3u));
      // uGpffffb02c / iGpffffb030. FUN_0023f318 only reads group 0, but
      // FUN_0023f8b8 searches every group for the record an entity belongs to,
      // so the whole table is kept rather than just the selected pair.
      groupActorArrays_.push_back(read<std::uint32_t>(entry + 0));
    }
    if (groupCount == 0)
    {
      memory_.clear();
      blob_ = 0;
      groupActorArrays_.clear();
      return false;
    }

    actorArray_ = read<std::uint32_t>(body + 4);
    masterScript_ = read<std::uint32_t>(body + 8);
    // :74. DAT_00354EAC = DAT_0031DBD8: the VM starts on the master script.
    masterPc_ = masterScript_;
    // :78. DAT_00354FA4 = DAT_00354F98 + count * 2 + 1, i.e. the word just past
    // the group table. FUN_0023fb50 reads a halfword there for the pre-battle
    // countdown and nothing else looks at it.
    DAT_00354fa4_ = body + 4u + groupCount * 8u;

    // The original stores a pointer at +0x08 and the port stores a pool slot,
    // so "no entity" has to be spelled differently. Every record starts unbound.
    for (std::uint32_t index = 0; index < 64u; ++index)
    {
      const std::uint32_t at = record(index);
      if (at + kActorRecordStride > memory_.size() || read<std::uint8_t>(at + actor::kId00) == 0)
      {
        break;
      }
      setEntitySlot(at, kNoEntity);
    }

    FUN_0023fb50_reset_records();
    return true;
  }

  void BattleEncounter::FUN_0023fb50_reset_records()
  {
    if (!available())
    {
      return;
    }
    // :8-13. The countdown, half the halfword past the group table, never
    // below 0x5A. FUN_0023fd30 spends it and the frame it lands on zero is when
    // the cursors appear.
    const std::int32_t seed = read<std::int16_t>(DAT_00354fa4_) / 2;
    countdown_ = static_cast<std::int16_t>(seed < 0x5A ? 0x5A : seed);

    // :17-30. Walk while the record's id is non-zero, clearing the four fields
    // a previous battle left behind. +0x02 is spared when it holds -1, which is
    // the "this actor is permanently out" marker FUN_00242660 sub-op 3 sets.
    std::uint32_t at = actorArray_;
    while (at + kActorRecordStride <= memory_.size() && read<std::uint8_t>(at + actor::kId00) != 0)
    {
      if (read<std::int8_t>(at + 0x02) != -1)
      {
        write<std::int8_t>(at + 0x02, 0);
      }
      write<std::int8_t>(at + 0x03, 0);
      write<std::int8_t>(at + 0x04, 0);
      write<std::int8_t>(at + 0x05, 0);
      write<std::int8_t>(at + 0x06, 0);
      write<std::int8_t>(at + 0x07, 0);
      at += kActorRecordStride;
    }

    // :32. **FUN_0023fb50 ends on FUN_0023fc08**, and FUN_0023f318 calls
    // FUN_0023fb50 as it loads -- so the actor count is established the moment
    // the scene's encounter data is read, long before any battle starts. The
    // port used to recount only inside the running battle, which left
    // DAT_00354EBA at zero through a whole preamble; FUN_00247d80 then answered
    // -1 for every id and FUN_00244248 dropped every order. That is what made
    // s14_e001's animatic unable to give the crab an action.
    //
    // The pool-reading half of FUN_0023fc08 is inert here: every record was just
    // given kNoEntity above, so the loop only counts.
    FUN_0023fc08_count();
  }

  void BattleEncounter::FUN_0023fc08_count()
  {
    actorCount_ = 0;
    if (!available() || read<std::uint8_t>(actorArray_ + actor::kId00) == 0)
    {
      return;
    }
    std::uint32_t at = actorArray_;
    do
    {
      actorCount_ = static_cast<std::int8_t>(actorCount_ + 1);
      at += kActorRecordStride;
    } while (at + kActorRecordStride <= memory_.size() &&
             read<std::uint8_t>(at + actor::kId00) != 0);
  }

  void BattleEncounter::FUN_0023fc08_bind(const orphen::ported::entity::EntityPool &pool)
  {
    actorCount_ = 0;
    if (!available() || read<std::uint8_t>(actorArray_ + actor::kId00) == 0)
    {
      return;
    }

    std::uint32_t at = actorArray_;
    do
    {
      ++actorCount_;
      const std::int32_t slot = entitySlot(at);
      const std::uint8_t id = read<std::uint8_t>(at + actor::kId00);

      // The three ways a binding goes stale, in the original's order: the
      // record never had one, the entity has been recycled into a type-0 hole,
      // or the entity is alive but is no longer carrying this record's id in
      // +0x95. All three clear +0x02 as well, so a script waiting on the group
      // marker sees the actor leave.
      bool bound = slot >= 0 && static_cast<std::size_t>(slot) < orphen::ported::entity::kEntitySlotCount;
      const orphen::ported::entity::OriginalEntity *entity =
          bound ? &pool.slot(static_cast<std::size_t>(slot)) : nullptr;
      if (bound && (entity->typeId00 == 0 || entity->byte95 != id))
      {
        bound = false;
      }

      if (!bound)
      {
        write<std::int8_t>(at + 0x02, 0);
        write<std::int8_t>(at + actor::kAlive0c, 0);
        setEntitySlot(at, kNoEntity);
      }
      else
      {
        // :36-40. +0x0C is a *death* counter, not a liveness one. Ghidra
        // spells the test `psVar3[0x95] < 1` off a short pointer, which is the
        // halfword at +0x12A -- the hit points -- and the byte test beside it
        // is the entity's own +0x95 against 0x51. So: this record's actor is
        // down, is on the enemy side of the id line, and is not mid-spawn on
        // either action byte. FUN_00240870's respawn arm reads it.
        if (static_cast<std::int16_t>(entity->staggerTimer12a) < 1 && entity->byte95 < 0x51 &&
            read<std::uint8_t>(at + actor::kPendingAction0e) != 0x11 &&
            read<std::uint8_t>(at + actor::kCurrentAction0f) != 0x11)
        {
          write<std::uint8_t>(at + actor::kAlive0c,
                              static_cast<std::uint8_t>(read<std::uint8_t>(at + actor::kAlive0c) + 1));
        }

        // :42-51. Bit 1 of the flag word is "this actor is idle": raised
        // whenever bit 0 is clear, or the current action is one of the three
        // neutral ones. FUN_00242660 sub-op 3 reads it to decide a wave is over.
        const std::uint32_t flags = read<std::uint32_t>(at + actor::kFlags38);
        const std::uint8_t action = read<std::uint8_t>(at + actor::kCurrentAction0f);
        const bool idle = (flags & 1) == 0 || action == 0x0A || action == 0x06 || action == 0x0B;
        write<std::uint32_t>(at + actor::kFlags38, idle ? (flags | 2u) : (flags & ~2u));
      }

      at += kActorRecordStride;
    } while (at + kActorRecordStride <= memory_.size() &&
             read<std::uint8_t>(at + actor::kId00) != 0);
  }

  std::uint32_t BattleEncounter::FUN_0023eba0_find(std::uint16_t id, bool allowUnbound) const
  {
    // The party half of FUN_0023eba0 -- id 0xFFFF for the master pseudo-record
    // and ids below 10 for the control blocks -- lives in BattleParty, because
    // that is where those tables are. This is the actor half only.
    if (!available())
    {
      return 0;
    }
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      const std::uint32_t at = record(static_cast<std::size_t>(index));
      if (entitySlot(at) < 0 && !allowUnbound)
      {
        continue;
      }
      if (read<std::uint8_t>(at + actor::kId00) == static_cast<std::uint8_t>(id))
      {
        return at;
      }
    }
    return 0;
  }

  std::int32_t BattleEncounter::FUN_00247d80_index_of(std::uint8_t id) const
  {
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      if (read<std::uint8_t>(record(static_cast<std::size_t>(index)) + actor::kId00) == id)
      {
        return index;
      }
    }
    return -1;
  }

  std::int32_t BattleEncounter::FUN_002476c0_cycle(const orphen::ported::entity::EntityPool &pool,
                                                   std::int32_t currentTarget,
                                                   std::int16_t direction, bool halfWrap) const
  {
    const std::int32_t count = DAT_00354eba_actorCount();

    // Is a record still worth aiming at? The four tests are shared by the
    // "keep what we have" arm and the search loop; the original spells them
    // twice, in the same order.
    const auto targetable = [&](std::uint32_t at, std::int32_t *entitySlotOut) {
      const std::int32_t slot = entitySlot(at);
      if (entitySlotOut != nullptr)
      {
        *entitySlotOut = slot;
      }
      if (slot < 0 || static_cast<std::size_t>(slot) >= orphen::ported::entity::kEntitySlotCount)
      {
        return false;
      }
      const auto &entity = pool.slot(static_cast<std::size_t>(slot));
      return entity.typeId00 != 0 && static_cast<std::int16_t>(entity.staggerTimer12a) > 0 &&
             (read<std::uint32_t>(at + actor::kFlags38) & 0x40u) == 0 &&
             read<std::uint8_t>(at + actor::kPendingAction0e) != 0x11 &&
             read<std::uint8_t>(at + actor::kCurrentAction0f) != 0x11;
    };

    // :20-24. Where the walk starts: the record the *current* target belongs
    // to. With no current target FUN_00247d80 answers -1, and the original
    // starts one past the end and forces a forward step -- so "no target, press
    // right" lands on record 0.
    std::int32_t start = -1;
    if (currentTarget >= 0 &&
        static_cast<std::size_t>(currentTarget) < orphen::ported::entity::kEntitySlotCount)
    {
      start = FUN_00247d80_index_of(pool.slot(static_cast<std::size_t>(currentTarget)).byte95);
    }
    if (start < 0)
    {
      start = count;
      direction = 1;
    }

    std::int32_t next = start + direction;
    if (direction == 0)
    {
      // :27-45. The validate-only call. It keeps the current record *only* if
      // the record's entity is the very entity being aimed at -- which is how a
      // target that has been recycled into some other actor gets dropped even
      // though the record still looks healthy.
      std::int32_t slot = kNoEntity;
      const std::uint32_t at = record(static_cast<std::size_t>(start < count ? start : 0));
      direction = 1;
      next = start + 1;
      if (start < count && targetable(at, &slot) && slot == currentTarget)
      {
        return start;
      }
    }

    // :47-51. Bit 0 of the third parameter jumps half the table instead of one
    // place. FUN_002462c8 sets it when the press was Up-and-Down or
    // Left-and-Down together, and arms a 120-frame lockout behind it.
    if (halfWrap)
    {
      next = static_cast<std::int16_t>(next + count / 2);
    }

    std::int32_t index = next;
    std::int32_t tries = 0;
    if (count > 0)
    {
      do
      {
        // :55-60. The wrap, in the original's asymmetric form: past the end
        // snaps to 0, before the start snaps to the last record.
        if (index >= count)
        {
          index = 0;
        }
        else if (index < 0)
        {
          index = count - 1;
        }
        const std::uint32_t at = record(static_cast<std::size_t>(index));
        // The half-wrap arm refuses to land back on the record it started
        // from, so a chord with two live targets always moves.
        if (targetable(at, nullptr) && (!halfWrap || index != start))
        {
          break;
        }
        ++tries;
        index = static_cast<std::int16_t>(index + direction);
      } while (tries < count);
    }
    return tries != count ? index : -1;
  }

  std::int32_t
  BattleEncounter::FUN_0023eff8_enemy_count(const orphen::ported::entity::EntityPool &pool) const
  {
    std::int32_t count = 0;
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      const std::uint32_t at = record(static_cast<std::size_t>(index));
      const std::int32_t slot = entitySlot(at);
      const std::uint8_t id = read<std::uint8_t>(at + actor::kId00);
      if (slot < 0 || id >= kFirstFriendlyId)
      {
        continue;
      }
      if (pool.slot(static_cast<std::size_t>(slot)).byte95 == id)
      {
        ++count;
      }
    }
    return count;
  }

  std::int32_t
  BattleEncounter::FUN_0023f080_living_enemy_count(const orphen::ported::entity::EntityPool &pool) const
  {
    std::int32_t count = 0;
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      const std::uint32_t at = record(static_cast<std::size_t>(index));
      const std::int32_t slot = entitySlot(at);
      const std::uint8_t id = read<std::uint8_t>(at + actor::kId00);
      if (slot < 0 || id >= kFirstFriendlyId)
      {
        continue;
      }
      const auto &entity = pool.slot(static_cast<std::size_t>(slot));
      if (entity.byte95 == id && static_cast<std::int16_t>(entity.staggerTimer12a) > 0)
      {
        ++count;
      }
    }
    return count;
  }


  // FUN_0023f8b8. See the header for why this, and not the battle VM, is what
  // puts an enemy in the actor table.
  std::uint32_t BattleEncounter::FUN_0023f8b8_bind_entity(
      orphen::ported::entity::EntityPool &pool, std::size_t entitySlot,
      const orphen::ported::resource::HitParameterTable *hits)
  {
    if (groupActorArrays_.empty() || entitySlot >= pool.slotCount())
    {
      return 0;
    }
    auto &entity = pool.slot(entitySlot);
    const auto wanted = static_cast<std::uint8_t>(entity.byte95);

    // :12-52. Every group, every record, first id match wins. A record whose
    // +0x0E already reads 0x11 is mid-spawn and keeps its action bytes and its
    // flag word; everything else is wiped back to a fresh participant.
    std::uint32_t at = 0;
    for (std::uint32_t group = 0; group < groupActorArrays_.size() && at == 0; ++group)
    {
      const std::uint32_t base = groupActorArrays_[group];
      for (std::uint32_t index = 0; index < 64u; ++index)
      {
        const std::uint32_t candidate = base + index * kActorRecordStride;
        if (candidate + kActorRecordStride > memory_.size())
        {
          break;
        }
        const std::uint8_t id = read<std::uint8_t>(candidate + actor::kId00);
        if (id == 0)
        {
          break;
        }
        if (id != wanted)
        {
          continue;
        }
        if (read<std::uint8_t>(candidate + actor::kPendingAction0e) != 0x11)
        {
          write<std::uint8_t>(candidate + actor::kGroup02, 0);
          write<std::uint32_t>(candidate + actor::kFlags38, 0);
          write<std::uint8_t>(candidate + actor::kPendingAction0e, 0);
          write<std::uint8_t>(candidate + actor::kCurrentAction0f, 0);
        }
        write<std::uint32_t>(candidate + actor::kFlags10, 0);
        write<std::uint8_t>(candidate + 0x03, 0);
        write<std::uint32_t>(candidate + 0x04, 0);
        at = candidate;
        break;
      }
    }
    if (at == 0)
    {
      return 0;
    }

    // :56-77. Walk the type's attack records and summarise what it can do into
    // +0x10: bit 0x10 for an attack with flag bit 0, 0x20 for bits 1..8, 0x40
    // for bit 9. DAT_00354C64 -- the count FUN_00216078 publishes -- bounds the
    // walk, so a type with no entry in the table contributes nothing.
    if (hits != nullptr)
    {
      const std::uint32_t count = hits->FUN_00216078_count(entity.typeId00);
      std::uint32_t flags = read<std::uint32_t>(at + actor::kFlags10);
      for (std::uint32_t index = 0; index < count; ++index)
      {
        const auto record = hits->FUN_00216078_record(entity.typeId00, index);
        if (!record.has_value())
        {
          continue;
        }
        if ((record->flags & 1u) != 0)
        {
          flags |= 0x10u;
        }
        else if ((record->flags & 0x1FEu) != 0)
        {
          flags |= 0x20u;
        }
        else if ((record->flags & 0x200u) != 0)
        {
          flags |= 0x40u;
        }
      }
      write<std::uint32_t>(at + actor::kFlags10, flags);
    }

    // :79-95. The tail. A record still holding a 0x192 cursor from a previous
    // mid-spawn drops it; a record whose authored spawn x reads 1000 is a
    // "wherever this thing already stands" record and takes the entity's own
    // position, in tenths, instead.
    if (read<std::uint8_t>(at + actor::kPendingAction0e) == 0x11)
    {
      const std::uint8_t cursor = read<std::uint8_t>(at + actor::kCursor0d);
      if (cursor != 0 && pool.slot(cursor).typeId00 == 0x192)
      {
        pool.releaseSlot(cursor);
      }
    }
    else if (read<std::int16_t>(at + actor::kSpawnX14) == 1000)
    {
      write<std::int16_t>(at + actor::kSpawnX14,
                          static_cast<std::int16_t>(entity.positionX20 * 10.0f));
      write<std::int16_t>(at + actor::kSpawnY16,
                          static_cast<std::int16_t>(entity.positionZ24 * 10.0f));
      write<std::int16_t>(at + actor::kSpawnZ18,
                          static_cast<std::int16_t>(entity.positionY28 * 10.0f));
    }
    else
    {
      write<std::int8_t>(at + actor::kGroup02, -1);
    }

    setEntitySlot(at, static_cast<std::int32_t>(entitySlot));
    entity.battleFlags96 |= 1u;
    write<std::int16_t>(at + actor::kTarget2c, -1);
    return at;
  }

  // FUN_00247d80: the index of the record carrying this id, or -1. The scan
  // stops at DAT_00354EBA, so it only ever sees records FUN_0023fc08 counted.
  std::int32_t BattleEncounter::FUN_00247d80_index_for_id(std::uint8_t id) const
  {
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      if (read<std::uint8_t>(record(static_cast<std::size_t>(index)) + actor::kId00) == id)
      {
        return index;
      }
    }
    return -1;
  }

  // FUN_0023eff8. A record counts when it has an entity, its id is below 0x50
  // -- the enemy side of the line -- and the entity is still carrying that id
  // in +0x95. Hit points are *not* tested, so this is "how many enemies are in
  // the fight", not "how many are still standing"; FUN_0023f080 is the one
  // that checks.
  std::int32_t BattleEncounter::FUN_0023eff8_bound_enemy_count(
      const orphen::ported::entity::EntityPool &pool) const
  {
    std::int32_t count = 0;
    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      const std::uint32_t at = record(static_cast<std::size_t>(index));
      const std::int32_t slot = entitySlot(at);
      if (slot < 0 || static_cast<std::size_t>(slot) >= orphen::ported::entity::kEntitySlotCount)
      {
        continue;
      }
      const std::uint8_t id = read<std::uint8_t>(at + actor::kId00);
      if (id >= kFirstFriendlyId)
      {
        continue;
      }
      if (pool.slot(static_cast<std::size_t>(slot)).byte95 == id)
      {
        ++count;
      }
    }
    return count;
  }

  // FUN_00244248, **the action request**, and the one function that decides
  // whether an AI script's order is taken or bounced.
  //
  // The block it writes into is chosen by the entity's +0x95: below 11 that is
  // a party slot and the block is `DAT_0031D780 + slot * 0x3C`, which is the
  // control block *plus 0x0C*; from 11 up it is an actor id and the block is
  // `DAT_00354EB4 + FUN_00247d80(id) * 0x3C + 0x0C`. Both are biased by the
  // same 0x0C, which is why the decompilation spells the pending and current
  // action bytes `[2]` and `[3]` -- they are the record's +0x0E and +0x0F, the
  // same two bytes an enemy reaches through its entity's +0x198.
  //
  //   if (!force && (flags38 & 1) && pending != 0x0A && pending != 0x06) {
  //     if (pending == 0x0B) { pending = action; return 1; }
  //     if (current != 0x06) return -1;          // busy: bounce it
  //   }
  //   pending = action; return 1;
  //
  // So bit 0 of +0x38 -- the bit the enemy's own action check sets when it
  // takes an order, and its state 1 clears when it goes idle again -- is what
  // makes the AI script's `aiact` opcode spin instead of stamping over an
  // attack in progress.
  std::int32_t BattleEncounter::FUN_00244248_request_action(const VmEnvironment &environment,
                                                            std::int32_t entityId,
                                                            std::uint8_t action,
                                                            bool force)
  {
    if (entityId < 11)
    {
      if (!environment.FUN_00244248_party)
      {
        return 0;
      }
      return environment.FUN_00244248_party(static_cast<std::uint8_t>(entityId), action, force);
    }

    const std::int32_t index = FUN_00247d80_index_for_id(static_cast<std::uint8_t>(entityId));
    if (index < 0)
    {
      return 0;
    }
    const std::uint32_t at = record(static_cast<std::size_t>(index));

    if (!force)
    {
      const std::uint32_t flags = read<std::uint32_t>(at + actor::kFlags38);
      const std::int8_t pending = read<std::int8_t>(at + actor::kPendingAction0e);
      if ((flags & 1u) != 0 && pending != 0x0A && pending != 0x06)
      {
        if (pending == 0x0B)
        {
          write<std::uint8_t>(at + actor::kPendingAction0e, action);
          return 1;
        }
        if (read<std::int8_t>(at + actor::kCurrentAction0f) != 0x06)
        {
          return -1;
        }
      }
    }
    write<std::uint8_t>(at + actor::kPendingAction0e, action);
    return 1;
  }

  // FUN_0023fd30:44-53 and :57-90, the VM loop, in the one form both callers
  // use:
  //
  //   while (true) {
  //     pbGpffffaf3c = block->pc;
  //     yield = handler[*pc](block, yield);
  //     block->pc = pbGpffffaf3c;
  //     if (0 <= (short)yield) break;
  //     yield = 0;
  //   }
  //
  // A handler that returns a negative value has not finished the frame's work
  // and the loop runs the next opcode immediately; anything else is stored as
  // the yield and ends the step. Every handler moves the program counter
  // itself -- the loop only saves and restores it around the call, because the
  // handler writes it through the global DAT_00354EAC.
  //
  // Seven of the nineteen are bare LAB_ blocks with no src/FUN_*.c; their
  // bodies were recovered from SLUS_200.11 and are named in the table below.
  //
  // s14_e012's master script, at 0x1A0C, is the whole shape of the thing:
  //
  //   1a0c  12  FUN_00242c40(500, 0)        script variable 25 = 500, which is
  //                                         what makes opcode 0xBD method 0x68
  //                                         the *target display*
  //   1a14  18  install actor script, id 0x1E   ... and 0x1F, 0x20, 0x22, 0x23
  //   1a3c   5  sub-op 1: wait 2 ticks
  //   1a40   3  living enemies == 0 ? jump +0x14 (out) : fall through
  //   1a48   3  unconditional jump -12, back to the wait
  //
  // and the five scripts it installs are five overlapping tails of one body:
  //
  //   1a8c   5  sub-op 1: wait 0x78            (only the two longest have it)
  //   1a94  16  sub-op 0: 0..0x3B ticks, randomised
  //   1a98  13  sub-op 0x80: request action 6  -- go idle, face the target
  //   1a9c  17  sub-op 1: hold while any actor is attacking
  //   1aa0  13  sub-op 0x80: request action 2  -- close and strike
  //   1aa4  17  sub-op 0: hold until this record's +0x0F is back to 6
  //   1aa8  16  sub-op 0: another 0..0x3B
  //   1aac  16  sub-op 2: 0x1E ticks per enemy still in the fight
  //   ...   the same again with action 4, then a jump back to 1a94
  //
  // So an enemy's whole behaviour is: wait a random beat, take a turn only
  // when nobody else is mid-attack, close, wait for the strike to finish, wait
  // a beat scaled by how crowded the fight is, repeat.
  bool BattleEncounter::stepVmBlock(VmBlock &block, const VmEnvironment &environment,
                                    VmStepResult &result)
  {
    const orphen::ported::entity::EntityPool *pool = environment.pool;

    for (int guard = 0; guard < 256; ++guard)
    {
      const std::uint8_t opcode = read<std::uint8_t>(block.pc);
      const std::uint32_t pc = block.pc;
      std::int32_t yield = 0;
      bool handled = true;

      switch (opcode)
      {
      case 0: // LAB_00241A58: pc += 4, yield 0.
        block.pc = pc + 4;
        yield = 0;
        break;

      case 1: // LAB_00241A70: pc -= *(u16 *)(pc + 2), yield 0.
        block.pc = pc - read<std::uint16_t>(pc + 2);
        yield = 0;
        break;

      case 3:
      {
        // FUN_00242000. Count something, compare it, and either branch by the
        // signed halfword at +6 or fall through eight bytes.
        //
        //   +1  0 = equal, 1 = the count is below the operand
        //   +2  0 = "is the actor at +3 alive at all", 1 = "how many enemies
        //           are still standing", anything else = branch unconditionally
        //   +4  the operand
        //   +6  the branch, in bytes, signed
        std::int32_t count = 0;
        const std::int8_t mode = read<std::int8_t>(pc + 2);
        const std::int8_t compare = read<std::int8_t>(pc + 1);
        if (mode == 0)
        {
          const std::int32_t slot =
              pool != nullptr ? FUN_00248f18_find_by_tag(*pool, read<std::uint8_t>(pc + 3)) : -1;
          if (slot < 1)
          {
            block.pc = pc + read<std::int16_t>(pc + 6);
            continue;
          }
          count = static_cast<std::int16_t>(pool->slot(static_cast<std::size_t>(slot)).staggerTimer12a);
          if (count < 0)
          {
            count = 0;
          }
        }
        else if (mode == 1)
        {
          count = pool != nullptr ? FUN_0023f080_living_enemy_count(*pool) : 0;
        }
        else
        {
          block.pc = pc + read<std::int16_t>(pc + 6);
          continue;
        }

        const std::int16_t operand = read<std::int16_t>(pc + 4);
        if (compare == 0 ? (count == operand) : (compare != 1 ? true : (count < operand)))
        {
          block.pc = pc + read<std::int16_t>(pc + 6);
          continue;
        }
        block.pc = pc + 8;
        continue;
      }

      case 5:
      {
        // FUN_00242660. Sub-op 1 is the timer wait: the first visit returns
        // `operand * 2` as the yield and the later ones spend it with
        // FUN_00248e00, advancing four bytes when it runs out. Sub-op 0 waits
        // for an actor to exist; sub-op 3 stamps every bound record's +0x02.
        const std::int8_t sub = read<std::int8_t>(pc + 1);
        if (sub != 1)
        {
          handled = false;
          break;
        }
        if (block.yield == 0)
        {
          yield = static_cast<std::int16_t>(read<std::uint16_t>(pc + 2) << 1);
        }
        else
        {
          yield = FUN_00248e00_step_slow(static_cast<std::int16_t>(block.yield),
                                         environment.frameTicks);
          if (yield == 0)
          {
            block.pc = pc + 4;
          }
        }
        break;
      }

      case 8: // LAB_00240C58: pc += 6, yield 0.
        block.pc = pc + 6;
        yield = 0;
        break;

      case 9:
        // LAB_00240C70: `block->+0x34 = pc; pc += count * 16 + 4`, return -1.
        // The pointer is installed, but FUN_00240870 -- the trigger walker that
        // spawns reinforcement waves off it -- is not run yet, so a script that
        // uses this gets its table remembered and nothing spawned. Recorded
        // rather than left silent.
        block.triggers = pc;
        block.pc = pc + 4 + static_cast<std::uint32_t>(read<std::int16_t>(pc + 2)) * 16u;
        result.triggerTableInstalled = true;
        continue;

      case 10: // LAB_00240C98: return yield + 1, pc unmoved.
        yield = static_cast<std::int16_t>(block.yield + 1);
        break;

      case 11:
      {
        // LAB_00240CB0: script variable [*(u16 *)(pc + 2)] against the dword at
        // +4. Equal advances eight bytes and continues; otherwise the step ends
        // with a yield of 1, so the comparison is retried next frame.
        const std::uint32_t index = read<std::uint16_t>(pc + 2);
        const std::uint32_t wanted = read<std::uint32_t>(pc + 4);
        const std::uint32_t value =
            index < environment.scriptVarCount ? environment.scriptVars[index] : 0u;
        if (value == wanted)
        {
          block.pc = pc + 8;
          continue;
        }
        yield = 1;
        break;
      }

      case 12:
      {
        // FUN_00240ce8 sub-op 0 -> FUN_00242c40(mode, delay): script variable
        // 25 takes the mode, unless it is already 1000 or more. The five camera
        // globals FUN_00242c40 also writes belong to the swing this port does
        // not reproduce.
        if (read<std::int8_t>(pc + 1) == 0)
        {
          const std::int16_t mode = read<std::int16_t>(pc + 4);
          if (mode > 0 && environment.scriptVarCount > 25 &&
              static_cast<std::int32_t>(environment.scriptVars[25]) < 1000)
          {
            environment.scriptVars[25] = static_cast<std::uint32_t>(mode);
            result.cameraMode = mode;
          }
        }
        block.pc = pc + 8;
        continue;
      }

      case 13:
      {
        // FUN_00240d78, **the order**. `+0x03` names the record -- zero means
        // the one the VM is running on -- and the sub-op at `+0x01` picks how
        // hard the request pushes:
        //
        //   0x01  FUN_00244248(entity, +0x02, force)   always taken
        //   0x02  FUN_00244248(entity, +0x02, 0)       taken or dropped
        //   0x80  the same, but a bounce parks the script here and it is tried
        //         again next frame -- unless the actor has died in the meantime
        //
        // The sub-ops this port does not carry -- 0x40, the party-side spell
        // chooser that rolls DAT_0031DA2E against the four button masks, 0x10
        // and 0x20 -- are player auto-battle and are not on an enemy's path.
        //
        // Every path returns 0, so an order is the last thing a script does in
        // a frame.
        const std::int8_t sub = read<std::int8_t>(pc + 1);
        if (sub != 1 && sub != 2 && sub != -0x80)
        {
          handled = false;
          break;
        }

        std::uint32_t at = block.record;
        const std::uint8_t named = read<std::uint8_t>(pc + 3);
        if (named != 0)
        {
          at = FUN_0023eba0_find(named, true);
        }
        if (at == 0)
        {
          block.pc = pc + 4;
          continue;
        }

        const std::int32_t slot = entitySlot(at);
        const bool bound = slot >= 0 &&
                           static_cast<std::size_t>(slot) < orphen::ported::entity::kEntitySlotCount &&
                           pool != nullptr;
        std::int32_t accepted = 1;
        if (bound)
        {
          const std::int32_t entityId =
              static_cast<std::int8_t>(pool->slot(static_cast<std::size_t>(slot)).byte95);
          accepted = FUN_00244248_request_action(environment, entityId,
                                                 read<std::uint8_t>(pc + 2), sub == 1);
        }

        if (accepted < 0)
        {
          ++result.actionsRefused;
        }
        else
        {
          ++result.actionsRequested;
        }

        // Only sub-op 0x80 waits for a bounce, and only while the actor is
        // still alive -- a dead one would spin here forever.
        bool advance = true;
        if (sub == -0x80 && accepted < 0 && bound &&
            static_cast<std::int16_t>(pool->slot(static_cast<std::size_t>(slot)).staggerTimer12a) > 0)
        {
          advance = false;
        }
        if (advance)
        {
          block.pc = pc + 4;
        }
        yield = 0;
        break;
      }

      case 14: // LAB_00241640: return the yield unchanged, pc unmoved.
        yield = static_cast<std::int16_t>(block.yield);
        break;

      case 16:
      {
        // FUN_00241698, the delay. The first visit rolls a tick count and arms
        // it with FUN_00248e48 -- `(n << 21) >> 16`, which is n truncated to
        // eleven bits and multiplied by 32, the same 32-ticks-per-unit the
        // enemy idle hold uses -- and every visit after that spends it with
        // FUN_00248e58, advancing four bytes on the frame it lands on zero.
        //
        //   0  a random 0..(operand|1)-1, or nothing at all when the operand
        //      is zero
        //   1  the same, biased by the member's DAT_0031DAD4 aggression
        //   2  the operand times the number of enemies in the fight
        //   3  a random operand, times the same
        //
        // Sub-op 1 reads a party table this class does not own and no enemy
        // script uses it; it halts rather than guessing.
        std::int16_t remaining = block.yield;
        if (remaining == 0)
        {
          const std::int8_t sub = read<std::int8_t>(pc + 1);
          const std::uint16_t operand = read<std::uint16_t>(pc + 2);
          std::int32_t ticks = 0;
          if (sub == 0)
          {
            if (operand != 0 && environment.FUN_00216868_random)
            {
              ticks = static_cast<std::int16_t>(
                  static_cast<std::int32_t>(environment.FUN_00216868_random() & 0xFFFFu) %
                  static_cast<std::int32_t>(static_cast<std::int16_t>(operand | 1u)));
            }
          }
          else if (sub == 2 || sub == 3)
          {
            std::int16_t base = static_cast<std::int16_t>(operand);
            if (sub == 3)
            {
              base = environment.FUN_00216868_random
                         ? static_cast<std::int16_t>(
                               static_cast<std::int32_t>(environment.FUN_00216868_random() & 0xFFFFu) %
                               static_cast<std::int32_t>(static_cast<std::int16_t>(operand | 1u)))
                         : static_cast<std::int16_t>(0);
            }
            const std::int32_t enemies = pool != nullptr ? FUN_0023eff8_bound_enemy_count(*pool) : 0;
            ticks = static_cast<std::int16_t>(base * enemies);
            if (ticks == 0)
            {
              ticks = 1;
            }
          }
          else
          {
            handled = false;
            break;
          }
          // FUN_00248e48.
          remaining = static_cast<std::int16_t>(
              (static_cast<std::int32_t>(ticks) << 21) >> 16);
          if (remaining == 0)
          {
            remaining = 1;
          }
        }
        // FUN_00248e58: spend a whole frame's ticks, floored at zero.
        const std::uint16_t before = static_cast<std::uint16_t>(remaining);
        std::uint16_t after = 0;
        if (before != 0)
        {
          after = static_cast<std::uint16_t>(before -
                                             static_cast<std::uint16_t>(environment.frameTicks));
          if (before < after)
          {
            after = 0;
          }
        }
        if (after == 0)
        {
          block.pc = pc + 4;
        }
        yield = static_cast<std::int16_t>(after);
        break;
      }

      case 17:
      {
        // FUN_00241830, the gate. It never advances until its condition is met,
        // and when it is met it returns -1 so the next opcode runs in the same
        // frame.
        //
        //   0  hold until the record at +0x02 -- zero meaning this one -- has
        //      its *current* action byte equal to +0x03, or has died. This is
        //      the "wait for the strike to finish" half of an enemy's loop, and
        //      it is skipped entirely while DAT_00354ECC has the battle
        //      suspended.
        //   1  hold while **any** record is attacking: current or pending in
        //      2..5. One actor at a time is the whole reason enemies take
        //      turns instead of swarming.
        //   2  the same, but only counting records aimed at the same target.
        const std::int8_t sub = read<std::int8_t>(pc + 1);
        if (sub == 0)
        {
          yield = block.yield;
          if (environment.DAT_00354ecc_suspended == 0)
          {
            std::uint32_t at = block.record;
            const std::uint8_t named = read<std::uint8_t>(pc + 2);
            if (named != 0)
            {
              at = FUN_0023eba0_find(named, false);
            }
            bool done = at == 0;
            if (!done)
            {
              const std::int32_t slot = entitySlot(at);
              if (slot < 0 ||
                  static_cast<std::size_t>(slot) >= orphen::ported::entity::kEntitySlotCount ||
                  pool == nullptr ||
                  static_cast<std::int16_t>(
                      pool->slot(static_cast<std::size_t>(slot)).staggerTimer12a) < 1)
              {
                done = true;
              }
              else if (read<std::int8_t>(at + actor::kCurrentAction0f) == read<std::int8_t>(pc + 3))
              {
                done = true;
              }
            }
            if (done)
            {
              block.pc = pc + 4;
              yield = -1;
            }
          }
        }
        else if (sub == 1 || sub == 2)
        {
          const std::int16_t ownTarget = read<std::int16_t>(block.record + actor::kTarget2c);
          bool busy = false;
          for (std::int32_t index = 0; index < actorCount_ && !busy; ++index)
          {
            const std::uint32_t at = record(static_cast<std::size_t>(index));
            if (entitySlot(at) < 0)
            {
              continue;
            }
            if (sub == 2 && read<std::int16_t>(at + actor::kTarget2c) != ownTarget)
            {
              continue;
            }
            const std::uint8_t current =
                static_cast<std::uint8_t>(read<std::uint8_t>(at + actor::kCurrentAction0f) - 2u);
            const std::uint8_t pending =
                static_cast<std::uint8_t>(read<std::uint8_t>(at + actor::kPendingAction0e) - 2u);
            if (current < 4 || pending < 4)
            {
              busy = true;
            }
          }
          if (busy)
          {
            yield = block.yield;
          }
          else
          {
            block.pc = pc + 4;
            yield = -1;
          }
        }
        else
        {
          yield = block.yield;
        }
        break;
      }

      case 18:
      {
        // FUN_002419e8: install a per-actor script. `+0x02` names the record --
        // zero means the record the VM is already running on, which for the
        // master pseudo-record is nothing -- and `+0x04` is the offset from
        // this instruction to the script body. +0x38 bit 0x20 is what makes
        // FUN_0023fd30's second loop step it.
        const std::int16_t id = read<std::int16_t>(pc + 2);
        std::uint32_t at = block.record;
        if (id != 0)
        {
          at = FUN_0023eba0_find(static_cast<std::uint16_t>(id), false);
        }
        if (at != 0)
        {
          write<std::uint32_t>(at + actor::kScriptPc30, pc + read<std::uint32_t>(pc + 4));
          write<std::uint32_t>(at + actor::kFlags38,
                               read<std::uint32_t>(at + actor::kFlags38) | 0x20u);
          ++result.actorScriptsInstalled;
        }
        block.pc = pc + 8;
        continue;
      }

      default:
        handled = false;
        break;
      }

      if (!handled)
      {
        result.halted = true;
        result.haltOpcode = opcode;
        result.haltOffset = pc;
        return false;
      }

      block.yield = static_cast<std::int16_t>(yield);
      if (static_cast<std::int16_t>(yield) >= 0)
      {
        break;
      }
      block.yield = 0;
    }
    return true;
  }

  BattleEncounter::MasterStepResult BattleEncounter::FUN_0023fd30_step_master_script(
      const VmEnvironment &environment)
  {
    MasterStepResult result;
    if (!available() || masterPc_ == 0 || masterHalted_)
    {
      return result;
    }

    VmBlock block;
    block.record = 0; // DAT_0031DBA8, which has no record fields of its own
    block.pc = masterPc_;
    block.yield = masterYield_;
    block.triggers = masterTriggers_;

    if (!stepVmBlock(block, environment, result))
    {
      masterHalted_ = true;
      masterHaltOpcode_ = result.haltOpcode;
      masterHaltOffset_ = result.haltOffset;
    }

    masterPc_ = block.pc;
    masterYield_ = block.yield;
    masterTriggers_ = block.triggers;
    return result;
  }

  // FUN_0023fd30:57-130. One pass over the actor table doing two independent
  // things per record.
  BattleEncounter::VmStepResult BattleEncounter::FUN_0023fd30_step_actor_scripts(
      const VmEnvironment &environment)
  {
    VmStepResult result;
    if (!available())
    {
      return result;
    }
    const orphen::ported::entity::EntityPool *pool = environment.pool;

    for (std::int32_t index = 0; index < actorCount_; ++index)
    {
      const std::uint32_t at = record(static_cast<std::size_t>(index));

      // :62-64. All three have to hold: an entity, a script, and the bit
      // opcode 18 raised when it installed one.
      if (entitySlot(at) >= 0 && read<std::uint32_t>(at + actor::kScriptPc30) != 0 &&
          (read<std::uint32_t>(at + actor::kFlags38) & 0x20u) != 0)
      {
        VmBlock block;
        block.record = at;
        block.pc = read<std::uint32_t>(at + actor::kScriptPc30);
        block.yield = read<std::int16_t>(at + actor::kYield2e);
        block.triggers = read<std::uint32_t>(at + actor::kTriggers34);

        if (!stepVmBlock(block, environment, result))
        {
          // The script is left parked on the opcode rather than dropped, so a
          // later frame reports the same halt and nothing is silently skipped.
          if (!actorHalted_)
          {
            actorHalted_ = true;
            actorHaltOpcode_ = result.haltOpcode;
            actorHaltOffset_ = result.haltOffset;
          }
        }

        write<std::uint32_t>(at + actor::kScriptPc30, block.pc);
        write<std::int16_t>(at + actor::kYield2e, block.yield);
        write<std::uint32_t>(at + actor::kTriggers34, block.triggers);
      }

      // :92-130, the target validation, which runs whether or not the record
      // has a script. `+0x1F` is the tail of the three-byte preference ring at
      // +0x1D; a positive value names a party member, one-based.
      const std::int16_t target = read<std::int16_t>(at + actor::kTarget2c);
      if (target >= 1)
      {
        const std::int8_t preferred = read<std::int8_t>(at + actor::kRing1d + 2);
        if (preferred < 1)
        {
          // No preference: drop the target once the thing it names is down.
          // Ghidra spells this `(&DAT_0058bfda)[target * 0xEC]`, which is the
          // halfword at pool[target] + 0x12A -- the hit points.
          if (pool == nullptr ||
              static_cast<std::size_t>(target) >= orphen::ported::entity::kEntitySlotCount ||
              static_cast<std::int16_t>(
                  pool->slot(static_cast<std::size_t>(target)).staggerTimer12a) < 1)
          {
            write<std::int16_t>(at + actor::kTarget2c, -1);
          }
        }
        else
        {
          const std::int32_t slot = environment.DAT_0031d7b8_memberEntity
                                        ? environment.DAT_0031d7b8_memberEntity(preferred - 1)
                                        : -1;
          if (!memberIsTargetable(environment, slot))
          {
            write<std::int16_t>(at + actor::kTarget2c, -1);
          }
        }
      }

      if (read<std::int16_t>(at + actor::kTarget2c) < 0)
      {
        // :106-130. Rotate the ring one place -- +0x1D takes +0x1E, +0x1E takes
        // +0x1F, and +0x1F takes the *negated* byte that fell off the front, so
        // a member that has just been tried goes to the back marked used -- and
        // take the first rotation whose member is targetable. Three tries; if
        // none of them lands, pick a member at random.
        int tries = 0;
        bool found = false;
        for (; tries < 3 && !found; ++tries)
        {
          const std::int8_t front = read<std::int8_t>(at + actor::kRing1d);
          write<std::int8_t>(at + actor::kRing1d, read<std::int8_t>(at + actor::kRing1d + 1));
          write<std::int8_t>(at + actor::kRing1d + 1, read<std::int8_t>(at + actor::kRing1d + 2));
          write<std::int8_t>(at + actor::kRing1d + 2, static_cast<std::int8_t>(-front));

          if (front > 0 && front <= environment.sGpffffaf4c_memberCount + 1)
          {
            const std::int32_t slot = environment.DAT_0031d7b8_memberEntity
                                          ? environment.DAT_0031d7b8_memberEntity(front - 1)
                                          : -1;
            if (memberIsTargetable(environment, slot))
            {
              write<std::int16_t>(at + actor::kTarget2c, static_cast<std::int16_t>(slot));
              found = true;
            }
          }
        }

        if (!found && environment.sGpffffaf4c_memberCount > 0)
        {
          const std::uint32_t roll =
              environment.FUN_00216868_random ? environment.FUN_00216868_random() : 0u;
          const std::int32_t member =
              static_cast<std::int32_t>((roll & 0xFFFFu) %
                                        static_cast<std::uint32_t>(
                                            environment.sGpffffaf4c_memberCount));
          const std::int32_t slot = environment.DAT_0031d7b8_memberEntity
                                        ? environment.DAT_0031d7b8_memberEntity(member)
                                        : -1;
          write<std::int16_t>(at + actor::kTarget2c,
                              memberIsTargetable(environment, slot)
                                  ? static_cast<std::int16_t>(slot)
                                  : static_cast<std::int16_t>(-1));
        }
      }
    }
    return result;
  }

  // The test the retarget block applies to a control block's entity, in the
  // original's order: it must exist, it must have hit points (Ghidra reads
  // `psVar3[0x95]` off a *short* pointer, so that is +0x12A, not +0x95), and
  // its type id at +0x00 must not be negative.
  bool BattleEncounter::memberIsTargetable(const VmEnvironment &environment,
                                           std::int32_t slot) const
  {
    if (slot < 0 || environment.pool == nullptr ||
        static_cast<std::size_t>(slot) >= orphen::ported::entity::kEntitySlotCount)
    {
      return false;
    }
    const auto &entity = environment.pool->slot(static_cast<std::size_t>(slot));
    return static_cast<std::int16_t>(entity.staggerTimer12a) > 0 && entity.typeId00 >= 0;
  }


  // FUN_00248f18: the pool slot whose +0x95 carries this id, or -1.
  std::int32_t BattleEncounter::FUN_00248f18_find_by_tag(
      const orphen::ported::entity::EntityPool &pool, std::uint8_t id)
  {
    for (std::size_t slot = 0; slot < pool.slotCount(); ++slot)
    {
      if (pool.status(slot) == orphen::ported::entity::SlotStatus::Free)
      {
        continue;
      }
      if (pool.slot(slot).byte95 == id)
      {
        return static_cast<std::int32_t>(slot);
      }
    }
    return -1;
  }

} // namespace orphen::ported::battle
