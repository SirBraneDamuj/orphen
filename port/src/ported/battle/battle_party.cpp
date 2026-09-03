#include "ported/battle/battle_party.h"

#include "ported/entity/original_entity_sound.h"

#include <algorithm>

namespace orphen::ported::battle
{
  namespace
  {
    // FUN_002d6c68: FUN_00265e28 plus a diagnostic when the pool is full. The
    // port has no equivalent of the "could not spawn type %d" ring buffer at
    // DAT_00326aa8, so this is just the allocation.
    std::int32_t FUN_002d6c68_spawn(const BattleParty::Environment &environment, std::int32_t typeId)
    {
      if (environment.pool == nullptr || environment.descriptors == nullptr)
      {
        return kNoEntity;
      }
      const std::size_t slot =
          environment.pool->FUN_00265e28_allocate_and_initialize(typeId, *environment.descriptors);
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        return kNoEntity;
      }
      return static_cast<std::int32_t>(slot);
    }

    // FUN_00229888: the party roster index -> character type id table at
    // DAT_0031c1f0 = {1, 3, 4, 5, 6, 7, 0x16}. FUN_002298d0 is its inverse, so
    // roster index and loadout row are the same number -- which is what lets
    // FUN_002432d8's roster loop index DAT_003437a0 by `rosterIndex * 3`
    // without going through FUN_002298d0 again.
    constexpr std::int16_t kDAT_0031c1f0[7] = {1, 3, 4, 5, 6, 7, 0x16};

    std::int16_t FUN_00229888_character_id(std::int32_t rosterIndex)
    {
      if (rosterIndex < 0 || rosterIndex > 6)
      {
        return 0;
      }
      return kDAT_0031c1f0[rosterIndex];
    }
  } // namespace

  // ---------------------------------------------------------------- statics

  void BattleParty::loadStaticTables(const orphen::ported::resource::ElfDataReader &elf)
  {
    if (!elf.valid())
    {
      return;
    }

    // FUN_002294b8: FUN_00267da0(0x3437a0, 0x318b50, 0x15).
    for (std::size_t i = 0; i < defaultLoadout_.size(); ++i)
    {
      defaultLoadout_[i] =
          elf.readU8(kDAT_00318b50_defaultLoadout + static_cast<std::uint32_t>(i));
    }
    if (std::all_of(DAT_003437a0_.begin(), DAT_003437a0_.end(), [](std::uint8_t v) { return v == 0; }))
    {
      DAT_003437a0_ = defaultLoadout_;
    }

    // DAT_00324fc8: rows until the item id reads zero. FUN_002432d8's scan
    // stops on the same condition, so the terminator is the row count.
    spellTable_.clear();
    for (std::uint32_t row = 0; row < 64; ++row)
    {
      const std::uint32_t at = kDAT_00324fc8_spellTable + row * kSpellTableStride;
      SpellTableRow entry;
      entry.itemId = elf.readU16(at);
      if (entry.itemId == 0)
      {
        break;
      }
      entry.effectType = elf.readU16(at + 2);
      for (std::size_t i = 0; i < entry.extraTypes.size(); ++i)
      {
        entry.extraTypes[i] = elf.readU16(at + 4 + static_cast<std::uint32_t>(i) * 2);
      }
      entry.family = elf.readU32(kDAT_00325230_spellFamily + row * 4);
      spellTable_.push_back(entry);
    }
  }

  const SpellTableRow *BattleParty::spellRowForItem(std::uint16_t itemId) const
  {
    for (const auto &row : spellTable_)
    {
      if (row.itemId == itemId)
      {
        return &row;
      }
    }
    return nullptr;
  }

  std::int32_t BattleParty::entitySlotAt(std::uint32_t address) const
  {
    const std::int32_t biased = tables_.read<std::int32_t>(address);
    return (biased == 0) ? kNoEntity : biased - 1;
  }

  void BattleParty::setEntitySlotAt(std::uint32_t address, std::int32_t slot)
  {
    tables_.write<std::int32_t>(address, (slot < 0) ? 0 : slot + 1);
  }

  // ------------------------------------------------------------------ reset

  void BattleParty::FUN_0023f288_reset()
  {
    tables_.clearAll();
    sGpffffb052_ = 0;
    DAT_00354ebc_ = 0;
    DAT_00354eba_enemyCount_ = 0;
    DAT_00354ecc_ = 0;
    DAT_0031dad0_ = kNoEntity;
    // See the header: not one of FUN_0023f288's clears, and nothing in src/
    // writes it. 1 is what the save state reads and the only value that makes
    // FUN_002432d8's player pass address member 0's masks.
    DAT_00354ebe_ = 1;
  }

  void BattleParty::FUN_0022a418_propagate_loadout()
  {
    // FUN_0022a418:269-275. Ghidra spells it as three scalar copies of
    // DAT_003437a0 -- rows 1, 2 and 6 slot 0 -- but the save state holds
    // `05 07 01` on rows 0, 1, 2 and 6 alike, so all three slots travel.
    for (const std::size_t row : {std::size_t{1}, std::size_t{2}, std::size_t{6}})
    {
      for (std::size_t slot = 0; slot < kLoadoutSlots; ++slot)
      {
        DAT_003437a0_[row * kLoadoutSlots + slot] = DAT_003437a0_[slot];
      }
    }
  }

  void BattleParty::FUN_002239c8_fill_empty_loadout_slots(std::int32_t iGpffffb284)
  {
    // FUN_002239c8:35-67. Skips rows 1, 2 and 6 -- the ones the copy above has
    // just filled -- and only runs while the scene id is neither 0 nor 0xC.
    if (iGpffffb284 == 0xC || iGpffffb284 == 0)
    {
      return;
    }
    for (std::size_t row = 0; row < kLoadoutRows; ++row)
    {
      if (row == 1 || row == 2 || row == 6)
      {
        continue;
      }
      for (std::size_t slot = 0; slot < kLoadoutSlots; ++slot)
      {
        const std::size_t at = row * kLoadoutSlots + slot;
        if (DAT_003437a0_[at] == 0)
        {
          DAT_003437a0_[at] = defaultLoadout_[at];
        }
      }
    }
  }


  // ------------------------------------------------------- small shared helpers

  std::int32_t BattleParty::FUN_002494e0_elapsed(const orphen::ported::entity::EntityPool *pool,
                                                 std::uint32_t member,
                                                 std::int16_t divisor) const
  {
    // FUN_002494e0: zero for a divisor of zero, and zero while the entity's
    // state still carries the 0x4000 restart bit -- a just-entered action must
    // not read the previous one's elapsed time.
    if (divisor == 0)
    {
      return 0;
    }
    const std::uint32_t control = BattleTables::controlBlock(member);
    const std::int32_t slot = entitySlotAt(control + control::kEntity08);
    if (slot == kNoEntity || pool == nullptr)
    {
      return 0;
    }
    if ((pool->slot(static_cast<std::size_t>(slot)).state60 & 0x4000) != 0)
    {
      return 0;
    }
    // FUN_00249218: party record +0x3C divided by the divisor. The field is
    // the action timer FUN_00249108 resets and the state handlers accumulate.
    const std::uint16_t timer =
        tables_.read<std::uint16_t>(BattleTables::partyRecord(member) + record::kChargeTimer3c);
    if (timer == 0)
    {
      return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::int32_t>(timer) / divisor);
  }

  void BattleParty::FUN_00249108_clear_turn_flags(std::uint32_t member)
  {
    tables_.write<std::uint16_t>(BattleTables::partyRecord(member) + record::kChargeTimer3c, 0);
  }

  std::int32_t BattleParty::FUN_002493b8_target(std::uint32_t member) const
  {
    // FUN_002493b8. The `< -3` fixup re-reads the halfword through a signed
    // 16-bit shift pair, which for a value already negative is a no-op -- it is
    // there because the field is written both signed and unsigned.
    const std::int16_t target =
        tables_.read<std::int16_t>(BattleTables::controlBlock(member) + control::kTarget2c);
    return target;
  }

  void BattleParty::FUN_00249388_set_target(std::uint32_t member,
                                            std::uint32_t gate,
                                            std::int16_t target)
  {
    if ((gate & 0x4000) == 0)
    {
      return;
    }
    tables_.write<std::int16_t>(BattleTables::controlBlock(member) + control::kTarget2c, target);
  }

  std::int32_t BattleParty::effectAnimation(const orphen::ported::entity::EntityPool *pool,
                                            std::uint32_t tableBase,
                                            std::uint32_t member) const
  {
    const std::int32_t slot = entitySlotAt(tableBase + member * 4);
    if (slot == kNoEntity || pool == nullptr)
    {
      // The original dereferences the pointer unguarded; every call site has
      // already tested it against zero, so reaching here means the caller did
      // not and -1 keeps the "not 2" comparison behaving.
      return -1;
    }
    return static_cast<std::int32_t>(pool->slot(static_cast<std::size_t>(slot)).animationA0);
  }

  std::uint8_t BattleParty::selectedSlot(std::int16_t partySlot) const
  {
    if (partySlot < 0)
    {
      return 0;
    }
    return tables_.read<std::uint8_t>(kDAT_0031da65_selectedSlot +
                                      static_cast<std::uint32_t>(partySlot));
  }

  void BattleParty::setSelectedSlot(std::int16_t partySlot, std::uint8_t slot)
  {
    if (partySlot < 0)
    {
      return;
    }
    tables_.write<std::uint8_t>(kDAT_0031da65_selectedSlot + static_cast<std::uint32_t>(partySlot),
                                slot);
  }

  void BattleParty::stepTargetTimers(std::uint16_t frameTicks)
  {
    DAT_00354f80_ = FUN_00248e58_step_timer(DAT_00354f80_, frameTicks);
  }

  void BattleParty::clearTargetTimers()
  {
    DAT_00354e96_ = 0;
    DAT_00354f80_ = 0;
  }

  void BattleParty::FUN_0023f620_count(std::int16_t which, std::int8_t member)
  {
    (void)member;
    if (which >= 0 && static_cast<std::size_t>(which) < DAT_0031dbf8_counters_.size())
    {
      ++DAT_0031dbf8_counters_[static_cast<std::size_t>(which)];
    }
  }

  // ------------------------------------------------------------ the methods

  std::uint32_t BattleParty::FUN_00242de0_end_battle()
  {
    sGpffffb052_ = 0;
    return 0;
  }

  std::uint32_t BattleParty::FUN_00243d18_teardown(const Environment &environment)
  {
    // FUN_00243d18. The half that matters outside battle is the entity
    // restore: every member's type id, flags, state and party slot go back to
    // what FUN_002458a8 recorded, and the five effect entities are released.
    sGpffffb052_ = static_cast<std::uint16_t>(sGpffffb052_ & 0xFFFC);

    for (std::int32_t member = 0; member < static_cast<std::int32_t>(kPartyRecordCount); ++member)
    {
      const std::uint32_t base = BattleTables::partyRecord(static_cast<std::uint32_t>(member));
      const std::int32_t slot = entitySlotAt(base + record::kEntity10);
      if (slot == kNoEntity || environment.pool == nullptr)
      {
        continue;
      }
      auto &entity = environment.pool->slot(static_cast<std::size_t>(slot));
      entity.typeId00 = tables_.read<std::int16_t>(base + record::kTypeId04);
      entity.descriptorFlags02 = tables_.read<std::uint16_t>(base + record::kFlags06);
      entity.halfword04 = tables_.read<std::uint16_t>(base + record::kFlags08);
      entity.state60 = 0;
      entity.fadeRamp62 = tables_.read<std::uint16_t>(base + record::kSubState0c);
      entity.spawnParam94 = tables_.read<std::uint8_t>(base + record::kAnim0e);
      entity.byte95 = tables_.read<std::uint8_t>(base + record::kPartySlot0f);
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEF);
    }

    // The five per-member effect entities.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::uint32_t offset = static_cast<std::uint32_t>(member) * 4;
      for (const std::uint32_t table : {kDAT_0031da8c_slotEntity,
                                        kDAT_0031da7c_auraEntity,
                                        kDAT_0031da9c_attackEntity,
                                        kDAT_0031daac_shieldEntity,
                                        kDAT_0031dabc_guardEntity})
      {
        const std::int32_t slot = entitySlotAt(table + offset);
        if (slot != kNoEntity && environment.pool != nullptr)
        {
          environment.pool->releaseSlot(static_cast<std::size_t>(slot));
        }
        tables_.write<std::int32_t>(table + offset, 0);
      }
    }
    if (DAT_0031dad0_ != kNoEntity && environment.pool != nullptr)
    {
      environment.pool->releaseSlot(static_cast<std::size_t>(DAT_0031dad0_));
      DAT_0031dad0_ = kNoEntity;
    }
    return 0;
  }

  void BattleParty::bindSlot(const Environment &environment,
                             std::int32_t member,
                             std::int32_t maskMember,
                             std::int32_t slot,
                             std::uint8_t itemId)
  {
    if (environment.items == nullptr)
    {
      return;
    }
    const auto record = environment.items->FUN_00229688_record(itemId);
    if (!record.has_value())
    {
      return;
    }

    const std::uint32_t memberU = static_cast<std::uint32_t>(member);
    const std::uint32_t slotU = static_cast<std::uint32_t>(slot);
    const std::int8_t kind = record->kindByte();

    // FUN_002432d8:75 / :207. `cStack_d9` is item record +0x27.
    tables_.write<std::int8_t>(kDAT_0031da2e_kinds + memberU * 3 + slotU, kind);

    // :76-102 / :209-237. The kind byte picks which of the five mask pairs the
    // slot binds to, and DAT_0031d168[slot] is the button that fires it.
    const std::uint32_t maskBase = BattleTables::buttonMask(static_cast<std::uint32_t>(maskMember));
    const std::uint32_t controlBase = BattleTables::controlBlock(memberU);
    std::uint32_t field = 0;
    if (kind == 0)
    {
      tables_.write<std::uint16_t>(
          controlBase + control::kFlags10,
          static_cast<std::uint16_t>(tables_.read<std::uint16_t>(controlBase + control::kFlags10) | 0x10));
      field = mask::kTriggerAttack04;
    }
    else
    {
      tables_.write<std::uint16_t>(
          controlBase + control::kFlags10,
          static_cast<std::uint16_t>(tables_.read<std::uint16_t>(controlBase + control::kFlags10) | 0x20));
      if (kind >= 1)
      {
        field = mask::kTriggerSpellA0c;
      }
      else if (kind == -1)
      {
        field = mask::kTriggerSpellC1c;
      }
      else
      {
        field = mask::kTriggerSpellB14;
      }
    }
    tables_.write<std::uint32_t>(maskBase + field,
                                 tables_.read<std::uint32_t>(maskBase + field) |
                                     kDAT_0031d168_slotButtons[slotU]);

    // :110 / :240. The item id itself, for the HUD and FUN_0023f620's counters.
    tables_.write<std::uint8_t>(kDAT_0031da22_itemIds + memberU * 3 + slotU, itemId);

    // :111-126 / :241-255. The spell table lookup, which is what names the
    // effect entity FUN_00242df0 will spawn.
    if (const SpellTableRow *row = spellRowForItem(itemId); row != nullptr)
    {
      tables_.write<std::uint16_t>(kDAT_0031da3a_effectTypes + memberU * 6 + slotU * 2,
                                   row->effectType);
      if (member == 0)
      {
        // :125 writes DAT_0031da54 only in the player pass. It is not indexed
        // by member, and with six members DAT_0031da3a's own stride runs over
        // it -- see the aliasing note in battle_tables.h.
        tables_.write<std::uint32_t>(kDAT_0031da54_family + slotU * 4, row->family);
      }
    }
  }

  namespace
  {
    // FUN_002432d8:128-145 / :257-274, the element block. The item record's
    // +0x18..+0x27 is a sixteen-entry array indexed by element -- 0 physical,
    // 1 lightning, 2 wind, 4 fire, 5 dark, 10 ice -- holding that element's
    // power. The first non-zero entry is the spell's element, and the party
    // record gets `1 << index` as a mask plus the power beside it.
    struct ElementBinding
    {
      bool found = false;
      std::uint16_t mask = 0;
      std::uint8_t power = 0;
    };

    ElementBinding findElement(const orphen::ported::resource::ItemRecord &record)
    {
      ElementBinding binding;
      if (record.elementTable[0] != 0)
      {
        binding.found = true;
        binding.mask = 1;
        binding.power = record.elementTable[0];
        return binding;
      }
      for (std::size_t index = 1; index <= 0xF; ++index)
      {
        if (record.elementTable[index] != 0)
        {
          binding.found = true;
          binding.mask = static_cast<std::uint16_t>(1u << index);
          binding.power = record.elementTable[index];
          return binding;
        }
      }
      return binding;
    }
  } // namespace

  void BattleParty::buildPlayerSlots(const Environment &environment)
  {
    // FUN_002432d8:67-150. The player's loadout row comes from party record 0's
    // class through FUN_002298d0, and its masks are written at
    // DAT_00354ebe - 1, which is member 0.
    const std::int32_t maskMember = DAT_00354ebe_ - 1;
    const std::uint32_t recordBase = BattleTables::partyRecord(0);
    const std::int16_t characterClass = tables_.read<std::int16_t>(recordBase + record::kClass00);
    const int loadoutRow = orphen::ported::entity::FUN_002298d0_character_class(characterClass);

    // `puVar20` only advances on a slot that had an item, so the +0x18 blocks
    // are packed while the +0x14 bytes stay slot-indexed. Reproduced, not
    // tidied: an empty Triangle slot really does move Circle's element block
    // down to index 0.
    std::uint32_t blockCursor = recordBase + record::kSpellBlock18;

    for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kLoadoutSlots); ++slot)
    {
      const std::size_t at = static_cast<std::size_t>(loadoutRow) * kLoadoutSlots +
                             static_cast<std::size_t>(slot);
      if (at >= DAT_003437a0_.size())
      {
        continue;
      }
      const std::uint8_t itemId = DAT_003437a0_[at];
      if (itemId == 0)
      {
        continue;
      }
      bindSlot(environment, 0, maskMember, slot, itemId);

      if (environment.items == nullptr)
      {
        continue;
      }
      const auto record = environment.items->FUN_00229688_record(itemId);
      if (!record.has_value())
      {
        continue;
      }
      const ElementBinding element = findElement(*record);
      if (!element.found)
      {
        continue;
      }
      tables_.write<std::uint8_t>(recordBase + record::kSpellByte14 + static_cast<std::uint32_t>(slot),
                                  record->byte07);
      tables_.write<std::uint16_t>(blockCursor, element.mask);
      tables_.write<std::uint8_t>(blockCursor + 2, element.power);
      tables_.write<std::uint8_t>(blockCursor + 3, record->byte08);
      blockCursor += 4;
    }

    // :151. The Square mask is not chosen by any item -- it is DAT_0031d174
    // written outright, which is why the shield is always on Square no matter
    // what the three assignable slots hold.
    tables_.write<std::uint32_t>(BattleTables::buttonMask(0) + mask::kTriggerGuard24,
                                 kDAT_0031d174_guardButton);
  }

  void BattleParty::buildMemberSlots(const Environment &environment,
                                     std::int32_t member,
                                     std::int32_t loadoutRow)
  {
    const std::uint32_t recordBase = BattleTables::partyRecord(static_cast<std::uint32_t>(member));
    std::uint32_t blockCursor = recordBase + record::kSpellBlock18;

    for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kLoadoutSlots); ++slot)
    {
      const std::size_t at = static_cast<std::size_t>(loadoutRow) * kLoadoutSlots +
                             static_cast<std::size_t>(slot);
      if (at >= DAT_003437a0_.size())
      {
        continue;
      }
      const std::uint8_t itemId = DAT_003437a0_[at];
      if (itemId == 0)
      {
        continue;
      }
      bindSlot(environment, member, member, slot, itemId);

      if (environment.items == nullptr)
      {
        continue;
      }
      const auto record = environment.items->FUN_00229688_record(itemId);
      if (!record.has_value())
      {
        continue;
      }
      const ElementBinding element = findElement(*record);
      if (!element.found)
      {
        continue;
      }
      tables_.write<std::uint8_t>(recordBase + record::kSpellByte14 + static_cast<std::uint32_t>(slot),
                                  record->byte07);
      tables_.write<std::uint16_t>(blockCursor, element.mask);
      tables_.write<std::uint8_t>(blockCursor + 2, element.power);
      tables_.write<std::uint8_t>(blockCursor + 3, record->byte08);
      blockCursor += 4;
    }

    tables_.write<std::uint32_t>(
        BattleTables::buttonMask(static_cast<std::uint32_t>(member)) + mask::kTriggerGuard24,
        kDAT_0031d174_guardButton);
  }

  std::uint32_t BattleParty::FUN_002432d8_build_battle_party(const Environment &environment,
                                                             std::int32_t param1,
                                                             std::int32_t param2)
  {
    if (param1 == 1)
    {
      return FUN_00243d18_teardown(environment);
    }
    if (environment.pool == nullptr)
    {
      return 0;
    }

    // FUN_002432d8:46. Bit 1 is "the party has been built"; FUN_00243f80
    // tests it to decide whether it has to build one first.
    sGpffffb052_ = static_cast<std::uint16_t>(sGpffffb052_ | 2);

    // :47-51, the five clears. Their sizes are what pin the table extents.
    tables_.FUN_00267e78_clear(kDAT_0031d3c8_partyRecords, 1000);
    tables_.FUN_00267e78_clear(kDAT_0031d7b0_controlBlocks, 600);
    tables_.FUN_00267e78_clear(kDAT_0031da08_spellAux, kSpellAuxSize);
    tables_.FUN_00267e78_clear(kDAT_0031dc18_buttonMasks, 0xF0);
    tables_.FUN_00267e78_clear(kDAT_0031dd08_cooldowns, 0x3C);
    DAT_00354ecc_ = 0;

    // :53-66. Party record 0 and control block 0 are the lead player, taken
    // from pool slot 0 outright rather than through the roster.
    auto &lead = environment.pool->leadPlayer();
    const std::uint32_t record0 = BattleTables::partyRecord(0);
    FUN_002458a8_fill_party_record(0, lead, 0);
    tables_.write<std::int16_t>(record0 + record::kClass00,
                                static_cast<std::int16_t>(lead.typeId00 < 0 ? -lead.typeId00
                                                                            : lead.typeId00));
    lead.byte95 = 1;      // DAT_0058bf45
    lead.spawnParam94 = 0; // DAT_0058bf44
    lead.state60 = 0;      // DAT_0058bf10
    lead.fadeRamp62 = 0;   // DAT_0058bf12
    // DAT_0058bf46 = 2: entity +0x96, the byte after the party slot.
    if (param2 == 0)
    {
      // DAT_0058bfda = DAT_0058bfd8, i.e. +0x12A = +0x128 -- start the battle
      // at full health.
      lead.staggerTimer12a = lead.maxHitPoints128;
    }
    FUN_00245978_fill_control_block(0, lead, 0);

    DAT_00354ebc_ = 1;
    buildPlayerSlots(environment);

    // :156-290. Roster entries 1..6. FUN_00229888 and FUN_002298d0 are
    // inverses, so the roster index doubles as the loadout row.
    for (std::int32_t rosterIndex = 0; rosterIndex < 7; ++rosterIndex)
    {
      if (environment.DAT_00343692_partySlots == nullptr ||
          static_cast<std::size_t>(rosterIndex) >= environment.partySlotCount)
      {
        break;
      }
      const std::uint16_t poolSlot = environment.DAT_00343692_partySlots[rosterIndex];
      if (poolSlot == 0x100 || poolSlot == 0)
      {
        continue;
      }
      // :164. The scan stops before the seventh roster entry no matter what it
      // holds; class 0x16 members come in through the pool scan below instead.
      if (rosterIndex == 6)
      {
        break;
      }
      if (poolSlot >= orphen::ported::entity::kEntitySlotCount)
      {
        continue;
      }
      if (DAT_00354ebc_ >= static_cast<std::int16_t>(kPartyRecordCount))
      {
        break;
      }

      const std::int32_t member = DAT_00354ebc_;
      auto &entity = environment.pool->slot(poolSlot);
      const std::uint32_t base = BattleTables::partyRecord(static_cast<std::uint32_t>(member));
      tables_.write<std::int16_t>(base + record::kClass00, FUN_00229888_character_id(rosterIndex));
      FUN_002458a8_fill_party_record(member, entity, static_cast<std::int32_t>(poolSlot));

      // :169-194. The member is retyped to 0x5C -- the battle actor type --
      // keeping the sign the entity already carried.
      entity.typeId00 = static_cast<std::int16_t>(entity.typeId00 < 0 ? -0x5C : 0x5C);
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFEE);
      entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 | 1);
      entity.byte95 = static_cast<std::uint8_t>(member + 1);
      entity.spawnParam94 = 0;
      entity.state60 = 0;
      entity.fadeRamp62 = 0;
      // :190-194. The member's combat stats come out of its *roster* record,
      // not out of FUN_00251dc0 -- `puStack_c0[2]` is DAT_00343688 + 0x02 and
      // `puStack_c0[7]` is + 0x07, both read as signed bytes. +0x128 and +0x12A
      // take the same one, so a member joins the battle at full health.
      if (environment.DAT_00343688_partyRecords != nullptr &&
          static_cast<std::size_t>(rosterIndex) < environment.partySlotCount)
      {
        const auto &record = environment.DAT_00343688_partyRecords[rosterIndex];
        const auto hitPoints =
            static_cast<std::uint16_t>(static_cast<std::int8_t>(record.halfword02));
        entity.maxHitPoints128 = hitPoints;
        entity.staggerTimer12a = hitPoints;
        entity.attackPower12c =
            static_cast<std::uint16_t>(static_cast<std::int8_t>(record.byte07));
      }
      FUN_00245978_fill_control_block(member, entity, static_cast<std::int32_t>(poolSlot));

      buildMemberSlots(environment, member, rosterIndex);
      DAT_00354ebc_ = static_cast<std::int16_t>(DAT_00354ebc_ + 1);
    }

    // :291-330. One class-0x16 member picked out of the pool by type, if the
    // scene has one. Left out of this slice: it is an ally the enemy-side work
    // drives, and nothing in s14_e012 reaches it before the player is live.

    // :331. The shared hit effect every member's guard spawns against.
    DAT_0031dad0_ = FUN_002d6c68_spawn(environment, 0x1E3);
    return 0;
  }

  void BattleParty::FUN_002458a8_fill_party_record(std::int32_t member,
                                                   const orphen::ported::entity::OriginalEntity &entity,
                                                   std::int32_t poolSlot)
  {
    const std::uint32_t base = BattleTables::partyRecord(static_cast<std::uint32_t>(member));
    tables_.write<std::int16_t>(base + record::kTypeId04,
                                static_cast<std::int16_t>(entity.typeId00 < 0 ? -entity.typeId00
                                                                              : entity.typeId00));
    tables_.write<std::uint16_t>(base + record::kFlags06, entity.descriptorFlags02);
    tables_.write<std::uint16_t>(base + record::kFlags08, entity.halfword04);
    tables_.write<std::uint16_t>(base + record::kState0a, entity.state60);
    tables_.write<std::uint16_t>(base + record::kSubState0c, entity.fadeRamp62);
    tables_.write<std::uint8_t>(base + record::kAnim0e, entity.spawnParam94);
    tables_.write<std::uint8_t>(base + record::kPartySlot0f, entity.byte95);
    setEntitySlotAt(base + record::kEntity10, poolSlot);
  }

  void BattleParty::FUN_00245978_fill_control_block(std::int32_t member,
                                                    const orphen::ported::entity::OriginalEntity &entity,
                                                    std::int32_t poolSlot)
  {
    const std::uint32_t base = BattleTables::controlBlock(static_cast<std::uint32_t>(member));
    tables_.write<std::uint8_t>(base + control::kPartySlot00, entity.byte95);
    setEntitySlotAt(base + control::kEntity08, poolSlot);

    // FUN_0030bd20 is the EE float-to-int truncation; the block keeps the
    // position at ten units per world unit, twice.
    const auto tenths = [](float value) {
      return static_cast<std::int16_t>(static_cast<std::int32_t>(value * 10.0f));
    };
    tables_.write<std::int16_t>(base + control::kPosX14, tenths(entity.positionX20));
    tables_.write<std::int16_t>(base + control::kPosY16, tenths(entity.positionZ24));
    tables_.write<std::int16_t>(base + control::kPosZ18, tenths(entity.positionY28));
    tables_.write<std::int16_t>(base + control::kPosX26, tenths(entity.positionX20));
    tables_.write<std::int16_t>(base + control::kPosY28, tenths(entity.positionZ24));
    tables_.write<std::int16_t>(base + control::kPosZ2a, tenths(entity.positionY28));
  }

  std::uint32_t BattleParty::FUN_00243f80_start_battle(const Environment &environment)
  {
    // FUN_00243f80:9-11. Method 2 builds the party itself if method 3 has not
    // already run, which is why a scene can call either order.
    if ((sGpffffb052_ & 2) == 0)
    {
      FUN_002432d8_build_battle_party(environment, 0, 0);
    }
    sGpffffb052_ = static_cast<std::uint16_t>(sGpffffb052_ | 1);

    // :14-59. Every member's entity goes to state 0x4078 -- 0x4000 is the
    // restart modifier and 0x78 is state 120, the battle-ready idle -- and its
    // control block records where it stood.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::uint32_t base = BattleTables::controlBlock(static_cast<std::uint32_t>(member));
      const std::int32_t slot = entitySlotAt(base + control::kEntity08);
      if (slot == kNoEntity || environment.pool == nullptr)
      {
        continue;
      }
      auto &entity = environment.pool->slot(static_cast<std::size_t>(slot));
      entity.state60 = 0x4078;
      const auto tenths = [](float value) {
        return static_cast<std::int16_t>(static_cast<std::int32_t>(value * 10.0f));
      };
      tables_.write<std::int16_t>(base + control::kPosX14, tenths(entity.positionX20));
      tables_.write<std::int16_t>(base + control::kPosY16, tenths(entity.positionZ24));
      tables_.write<std::int16_t>(base + control::kPosZ18, tenths(entity.positionY28));
      tables_.write<std::int16_t>(base + control::kPosX26, tenths(entity.positionX20));
      tables_.write<std::int16_t>(base + control::kPosY28, tenths(entity.positionZ24));
      tables_.write<std::int16_t>(base + control::kPosZ2a, tenths(entity.positionY28));
    }

    FUN_00242df0_spawn_spell_entities(environment);

    // :62-84. Every member starts with no target and action 6, the neutral
    // idle FUN_002462c8 tests for before it will accept a button. Members
    // other than 0 also get an AI script installed at control +0x30
    // (PTR_DAT_0031d1a8, or PTR_DAT_0031d1f0 for class 3) -- that is the
    // FUN_0023fd30 VM, which this slice does not run, so the pointer is left
    // null and the member simply stands still.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::uint32_t base = BattleTables::controlBlock(static_cast<std::uint32_t>(member));
      tables_.write<std::int16_t>(base + control::kTarget2c, -1);
      tables_.write<std::uint8_t>(base + control::kCurrentAction0f, 6);
    }
    return 0;
  }

  void BattleParty::FUN_00242df0_spawn_spell_entities(const Environment &environment)
  {
    if (environment.pool == nullptr)
    {
      return;
    }

    // :40-64. One type-399 marker per member, bound back to the member's
    // entity through +0x198.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::uint32_t control = BattleTables::controlBlock(static_cast<std::uint32_t>(member));
      const std::int16_t characterClass = tables_.read<std::int16_t>(
          BattleTables::partyRecord(static_cast<std::uint32_t>(member)) + record::kClass00);
      if (characterClass == 0x16 || characterClass == -0x16)
      {
        continue;
      }
      const std::int32_t owner = entitySlotAt(control + control::kEntity08);
      if (owner == kNoEntity)
      {
        continue;
      }
      const std::int32_t marker = FUN_002d6c68_spawn(environment, 399);
      if (marker != kNoEntity)
      {
        auto &entity = environment.pool->slot(static_cast<std::size_t>(marker));
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1);
        entity.animationA0 = 0;
        entity.byte95 = 0;
      }
      setEntitySlotAt(kDAT_0031da8c_slotEntity + static_cast<std::uint32_t>(member) * 4, marker);
    }

    // :66-86. The aura, type 0x118, one per member.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::int16_t characterClass = tables_.read<std::int16_t>(
          BattleTables::partyRecord(static_cast<std::uint32_t>(member)) + record::kClass00);
      if (characterClass == 0x16 || characterClass == -0x16)
      {
        continue;
      }
      const std::int32_t aura = FUN_002d6c68_spawn(environment, 0x118);
      if (aura != kNoEntity)
      {
        auto &entity = environment.pool->slot(static_cast<std::size_t>(aura));
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1);
        entity.animationA0 = 0;
      }
      setEntitySlotAt(kDAT_0031da7c_auraEntity + static_cast<std::uint32_t>(member) * 4, aura);
    }

    // :88-172. The two spell entities: the first slot whose kind byte is zero
    // goes to DAT_0031da9c, the first whose kind byte is *not* zero goes to
    // DAT_0031daac. With Orphen's shipped loadout that is Cross (Sword of the
    // Fallen Devil, kind 0) and Triangle (Hand of Pyro, kind 12) -- Circle's
    // Bite of Lightning has a kind of -2 and is reached through the state
    // handlers rather than held here.
    const auto spawnFirstMatching = [&](std::uint32_t destination, bool wantZeroKind) {
      for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
      {
        const std::uint32_t memberU = static_cast<std::uint32_t>(member);
        tables_.write<std::int32_t>(destination + memberU * 4, 0);
        const std::int16_t characterClass =
            tables_.read<std::int16_t>(BattleTables::partyRecord(memberU) + record::kClass00);
        if (characterClass == 0x16 || characterClass == -0x16)
        {
          continue;
        }
        for (std::uint32_t slot = 0; slot < kLoadoutSlots; ++slot)
        {
          const std::int8_t kind =
              tables_.read<std::int8_t>(kDAT_0031da2e_kinds + memberU * 3 + slot);
          const std::uint16_t effectType =
              tables_.read<std::uint16_t>(kDAT_0031da3a_effectTypes + memberU * 6 + slot * 2);
          if ((kind == 0) != wantZeroKind || effectType == 0)
          {
            continue;
          }
          const std::int32_t spawned = FUN_002d6c68_spawn(environment, effectType);
          if (spawned != kNoEntity)
          {
            auto &entity = environment.pool->slot(static_cast<std::size_t>(spawned));
            entity.animationA0 = 1;
            entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1);
            entity.parentSlot192 = -1;
            entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
            entity.attachBone194 = 0;
            if (!wantZeroKind)
            {
              entity.state60 = 0;
            }
          }
          setEntitySlotAt(destination + memberU * 4, spawned);
          break;
        }
      }
    };
    spawnFirstMatching(kDAT_0031da9c_attackEntity, true);
    spawnFirstMatching(kDAT_0031daac_shieldEntity, false);

    // :173-200. The guard, type 0x1C7 -- Square's shield, the same for every
    // member and independent of the loadout.
    for (std::int32_t member = 0; member < DAT_00354ebc_; ++member)
    {
      const std::uint32_t memberU = static_cast<std::uint32_t>(member);
      tables_.write<std::int32_t>(kDAT_0031dabc_guardEntity + memberU * 4, 0);
      const std::int16_t characterClass =
          tables_.read<std::int16_t>(BattleTables::partyRecord(memberU) + record::kClass00);
      if (characterClass == 0x16 || characterClass == -0x16)
      {
        continue;
      }
      const std::int32_t guard = FUN_002d6c68_spawn(environment, 0x1C7);
      if (guard != kNoEntity)
      {
        auto &entity = environment.pool->slot(static_cast<std::size_t>(guard));
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1);
        entity.animationA0 = 1;
        entity.state60 = 0;
        entity.parentSlot192 = -1;
        entity.attachBone194 = 0;
      }
      setEntitySlotAt(kDAT_0031dabc_guardEntity + memberU * 4, guard);
    }
  }

  std::uint32_t BattleParty::FUN_00244cc0_equip_spell(std::uint32_t packed, std::int64_t spellId)
  {
    // FUN_00244cc0. `packed & 0xF` is the slot and `(packed >> 8) & 0xF` the
    // loadout row; s14_e012 calls it with 0x300..0x302, 0x400..0x402 and
    // 0x500..0x502, i.e. all three slots of rows 3, 4 and 5.
    const std::uint32_t slot = packed & 0xFu;
    const std::uint32_t row = (packed >> 8) & 0xFu;
    if (slot >= kLoadoutSlots || row >= kLoadoutRows)
    {
      return 0;
    }
    const std::size_t at = row * kLoadoutSlots + slot;

    // The high-bits-clear case: `param_3` is the item id outright.
    if ((static_cast<std::uint64_t>(spellId) & 0xFFFFFFFFFFFF0000ull) == 0)
    {
      const std::uint8_t itemId = static_cast<std::uint8_t>(spellId & 0xFFFF);
      const std::uint8_t previous = DAT_003437a0_[at];
      if (previous != itemId)
      {
        if (DAT_003437b8_itemCounts_[itemId] != 0 && row != 2)
        {
          DAT_003437b8_itemCounts_[itemId] =
              static_cast<std::uint8_t>(DAT_003437b8_itemCounts_[itemId] - 1);
        }
        DAT_003437a0_[at] = itemId;
        DAT_003437b8_itemCounts_[previous] =
            static_cast<std::uint8_t>(DAT_003437b8_itemCounts_[previous] + 1);
      }
      return 0;
    }

    // The negative case swaps within the row rather than pulling from stock.
    // Not reached by s14_e012; recorded rather than modelled so a scene that
    // does reach it says so instead of silently equipping the wrong spell.
    return 0xFFFFFFFFu;
  }

} // namespace orphen::ported::battle
