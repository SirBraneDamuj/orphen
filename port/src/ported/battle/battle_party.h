#pragma once

// Battle setup: everything script opcode 0xBD's low methods reach.
//
//   src/FUN_00242a18.c   the method table itself, which is opcode 0xBD's whole
//                        body once FUN_00263e80 has evaluated the operands
//   src/FUN_002432d8.c   method 3 -- build the battle party from the roster
//   src/FUN_00243f80.c   method 2 -- start the battle
//   src/FUN_00242de0.c   method 1 -- stop it
//   src/FUN_00243d18.c   the teardown method 3 runs with param_1 == 1
//   src/FUN_00242df0.c   spawn the per-member spell effect entities
//   src/FUN_00244cc0.c   method 0x78 -- equip a spell into a loadout slot
//
// **Battle entry is script-driven.** docs/battle_mode_activation.md concluded
// the opposite -- that no SCR opcode enters battle mode and only the debug menu
// FUN_00268e20 writes cGpffffb663. Both halves of that are wrong:
//
//   - FUN_0022a418:49 raises DAT_003555d3 for any scene reached with bit
//     0x20000 of the scene request set, which is every MCB0 section-14 scene.
//     Section 14 holds 63 scenes and is the battle section.
//   - s14_e012's script calls 0xBD method 3 at blob 0x0816 and 0x0a2a, and
//     method 2 at 0x09a5 and 0x0bb0. Those are the battle.
//
// The pair FUN_002239c8:117 tests is DAT_003555d3 and bit 0 of sGpffffb052,
// and bit 0 is exactly what method 2 sets and method 1 clears.

#include "ported/battle/battle_tables.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/resource/elf_data_reader.h"
#include "ported/resource/character_stats.h"
#include "ported/resource/item_database.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::battle
{

  // --------------------------------------------------------------- the loadout

  // DAT_003437a0: seven characters by three assignable slots, one item id each.
  // Slot 0 is Triangle, 1 Circle, 2 Cross -- the order DAT_0031d168 is indexed
  // in, not a guess.
  //
  // The row is chosen by FUN_002298d0's *class* index, not by pool slot:
  // class 1 -> 0, 3 -> 1, 4 -> 2, 5 -> 3, 6 -> 4, 7 -> 5, 0x16 -> 6, else 7.
  // Orphen is class 1, so he is row 0.
  inline constexpr std::size_t kLoadoutRows = 7;
  inline constexpr std::size_t kLoadoutSlots = 3;

  // FUN_002294b8 copies 21 bytes from here into DAT_003437a0 on a new game.
  // Read out of the executable rather than transcribed:
  //
  //   row 0: 05 07 01   row 3: 18 1c 14   row 5: 2d 31 35
  //   row 1: 00 00 00   row 4: 25 2a 21   row 6: 00 00 00
  //   row 2: 05 07 01
  //
  // Row 0 is Hand of Pyro / Bite of Lightning / Sword of the Fallen Devil.
  // battle_logo_loaded.p2s, a save state taken inside a real battle, holds
  // exactly that at 0x003437A0.
  inline constexpr std::uint32_t kDAT_00318b50_defaultLoadout = 0x00318B50;

  // ------------------------------------------------------------ the spell table

  // DAT_00324fc8, stride 0x12, terminated by a zero item id: item id -> the
  // effect entity type FUN_00242df0 spawns, plus up to six more type ids the
  // per-spell state handlers reach for. DAT_00325230 is a parallel u32 per row.
  //
  //   item 0x01 Sword of the Fallen Devil -> type 0x0139
  //   item 0x05 Hand of Pyro              -> type 0x013D, extras 0x15B 0x173
  //   item 0x07 Bite of Lightning         -> type 0x0174, extras 0x15C x2,
  //                                          0x13E, 0x28, 0x27, 0x26
  inline constexpr std::uint32_t kDAT_00324fc8_spellTable = 0x00324FC8;
  inline constexpr std::uint32_t kDAT_00325230_spellFamily = 0x00325230;
  inline constexpr std::uint32_t kSpellTableStride = 0x12;

  struct SpellTableRow
  {
    std::uint16_t itemId = 0;
    std::uint16_t effectType = 0;
    std::array<std::uint16_t, 7> extraTypes{};
    std::uint32_t family = 0;
  };

  // --------------------------------------------------------------- the module

  class BattleParty
  {
  public:
    // What the setup path needs from the rest of the runtime. Passed per call
    // rather than stored, the way ActorEnvironment is.
    struct Environment
    {
      orphen::ported::entity::EntityPool *pool = nullptr;
      const orphen::ported::entity::EntityDescriptorTable *descriptors = nullptr;
      const orphen::ported::resource::ItemDatabase *items = nullptr;
      // SceneScriptState::DAT_00343692_partySlots -- the roster's +0x0A
      // halfword, which is the only roster field the battle setup reads. 0 or
      // 0x100 mean "this character is not in the party".
      const std::uint16_t *DAT_00343692_partySlots = nullptr;
      // The rest of the same records. FUN_002432d8:190-194 takes the member's
      // hit points from +0x02 and its attack power from +0x07, both signed
      // bytes -- the battle path does not go through FUN_00251dc0 for anyone
      // but the lead.
      const orphen::ported::resource::StatRecord *DAT_00343688_partyRecords = nullptr;
      std::size_t partySlotCount = 0;
    };

    // Reads DAT_00318b50 and DAT_00324fc8 out of SLUS_200.11. Safe to call
    // again; a second call re-seeds nothing already equipped.
    void loadStaticTables(const orphen::ported::resource::ElfDataReader &elf);

    // FUN_0023f288, called from FUN_0022a418 on every scene load: wipe the
    // battle module. DAT_00354ebe is *not* one of the globals it clears, and
    // nothing in src/ writes it -- battle_logo_loaded.p2s reads 1 there, which
    // is the only value consistent with FUN_002432d8 writing the player's
    // button masks at (DAT_00354ebe - 1) * 0x28 in the same pass that fills
    // party record 0. So it is seeded to 1 here.
    void FUN_0023f288_reset();

    // FUN_0022a418:269-275, at scene load: rows 1, 2 and 6 take row 0's items.
    // Then FUN_002239c8:35-67 fills any slot still empty from the defaults.
    void FUN_0022a418_propagate_loadout();
    void FUN_002239c8_fill_empty_loadout_slots(std::int32_t iGpffffb284);

    // The 0xBD methods, by their handler addresses.
    std::uint32_t FUN_002432d8_build_battle_party(const Environment &environment,
                                                  std::int32_t param1,
                                                  std::int32_t param2);
    std::uint32_t FUN_00243f80_start_battle(const Environment &environment);
    std::uint32_t FUN_00242de0_end_battle();
    std::uint32_t FUN_00243d18_teardown(const Environment &environment);
    std::uint32_t FUN_00244cc0_equip_spell(std::uint32_t packed, std::int64_t spellId);

    // FUN_002458a8 / FUN_00245978: stamp one entity into its party record and
    // its control block. Both are called from FUN_002432d8 and nowhere else in
    // this slice; the entity pointer they store becomes a pool slot here.
    void FUN_002458a8_fill_party_record(std::int32_t member,
                                        const orphen::ported::entity::OriginalEntity &entity,
                                        std::int32_t poolSlot);
    void FUN_00245978_fill_control_block(std::int32_t member,
                                         const orphen::ported::entity::OriginalEntity &entity,
                                         std::int32_t poolSlot);

    // FUN_00242df0: allocate the four effect entities each member carries --
    // the aura (type 0x118), the first kind-0 spell, the first non-kind-0
    // spell, and the guard (type 0x1C7).
    void FUN_00242df0_spawn_spell_entities(const Environment &environment);

    // ---------------------------------------------------- small shared helpers
    //
    // The handful of one- and two-line functions FUN_002462c8 and FUN_00249610
    // call. They live here rather than beside their callers because every one
    // of them addresses a table this class owns.

    // FUN_002494e0 / FUN_00249218: how long the member has been in its current
    // action, as party record +0x3C divided by `divisor`. Returns 0 while the
    // entity's state carries the 0x4000 restart bit, which is what stops a
    // just-entered action from reading the previous one's elapsed time.
    std::int32_t FUN_002494e0_elapsed(const orphen::ported::entity::EntityPool *pool,
                                      std::uint32_t member,
                                      std::int16_t divisor) const;
    // FUN_00249108: reset that timer. Called on the frame a charge begins.
    void FUN_00249108_clear_turn_flags(std::uint32_t member);
    // FUN_002493b8: the member's current target index, with the original's
    // sign fixup for values below -3.
    std::int32_t FUN_002493b8_target(std::uint32_t member) const;
    // FUN_00249388: write it back, but only when the caller passes 0x4000.
    void FUN_00249388_set_target(std::uint32_t member, std::uint32_t gate, std::int16_t target);

    // The +0xA0 animation id of one of the per-member effect entities, which is
    // how FUN_002462c8 asks whether the shield has finished playing out.
    std::int32_t effectAnimation(const orphen::ported::entity::EntityPool *pool,
                                 std::uint32_t tableBase,
                                 std::uint32_t member) const;

    // DAT_0031da65 + partySlot: which of the three assignable buttons last
    // fired. Indexed by the *1-based* slot, not by member -- that asymmetry is
    // the original's.
    std::uint8_t selectedSlot(std::int16_t partySlot) const;
    void setSelectedSlot(std::int16_t partySlot, std::uint8_t slot);

    // DAT_00354f80 and DAT_00354e96, the two target-cycle timers. FUN_002462c8
    // steps the first at its very top and clears both on any face-button press.
    void stepTargetTimers(std::uint16_t frameTicks);
    void clearTargetTimers();
    // Counts the frames the target-cycle block and the turn-toward-target block
    // were reachable. Both are enemy-side work this slice defers, so the report
    // says how often a run would have needed them rather than leaving them as
    // silent gaps.
    void recordTargetCycleReached() { ++targetCycleFrames_; }
    void recordTargetFacingReached() { ++targetFacingFrames_; }
    std::uint32_t targetCycleFrames() const { return targetCycleFrames_; }
    std::uint32_t targetFacingFrames() const { return targetFacingFrames_; }

    // FUN_0023f620: the battle statistics counters at DAT_0031dbf8. Nothing in
    // this slice reads them back -- they feed the post-battle results screen
    // (FUN_00245098, "Depending on your battle performance,") -- so they are
    // counted rather than modelled.
    void FUN_0023f620_count(std::int16_t which, std::int8_t member);

    std::uint32_t DAT_00354ecc() const { return DAT_00354ecc_; }
    void setDAT_00354ecc(std::uint32_t value) { DAT_00354ecc_ = value; }

    // FUN_002239c8:117. Both halves have to be true for FUN_00249610 to run in
    // place of FUN_00251ed8.
    bool battleActive(bool DAT_003555d3) const
    {
      return DAT_003555d3 && sGpffffb052_ != 0;
    }
    // FUN_0023fd30's own gate, which is bit 0 rather than the whole word.
    bool battleRunning() const { return (sGpffffb052_ & 1) != 0; }

    BattleTables &tables() { return tables_; }
    const BattleTables &tables() const { return tables_; }

    std::int16_t DAT_00354ebc_memberCount() const { return DAT_00354ebc_; }
    std::int16_t DAT_00354ebe_playerSlot() const { return DAT_00354ebe_; }
    std::uint16_t sGpffffb052() const { return sGpffffb052_; }
    std::uint32_t DAT_00354fc2() const { return DAT_00354fc2_; }

    std::span<const std::uint8_t> DAT_003437a0_loadout() const { return DAT_003437a0_; }
    const std::vector<SpellTableRow> &spellTable() const { return spellTable_; }
    // Linear scan of DAT_00324fc8 for an item id, as FUN_002432d8:115-122 does.
    const SpellTableRow *spellRowForItem(std::uint16_t itemId) const;

    // Pool slots stored where the original stored a pointer. The tables are
    // memset to zero and the original read that back as a null pointer, so a
    // stored slot is biased by one: 0 stays "none" for free and slot 0 (the
    // lead player) is not mistaken for it.
    std::int32_t entitySlotAt(std::uint32_t address) const;
    void setEntitySlotAt(std::uint32_t address, std::int32_t slot);

  private:
    // FUN_002432d8:67-150. Member 0, the player: its loadout row comes from
    // party record 0's class and its masks are written at DAT_00354ebe - 1.
    void buildPlayerSlots(const Environment &environment);
    // The same body for a roster member, FUN_002432d8:199-285.
    void buildMemberSlots(const Environment &environment, std::int32_t member, std::int32_t classIndex);
    // Shared by both: read the item record and bind one slot.
    void bindSlot(const Environment &environment,
                  std::int32_t member,
                  std::int32_t maskMember,
                  std::int32_t slot,
                  std::uint8_t itemId);

    BattleTables tables_;

    // uGpffffb052 == sGpffffb052 == 0x00355022. A bitfield, not a boolean: bit
    // 0 is "battle running" (FUN_00243f80 / FUN_00242de0), bit 1 is "the party
    // has been built" (FUN_002432d8 sets DAT_00354fc2 bit 1 and FUN_00243f80
    // tests b052 bit 1 to decide whether to build first), bit 2 is the pause
    // gate FUN_00244bf0 toggles and bit 3 the surrender path FUN_0023fd30
    // takes. battle_logo_loaded.p2s reads 0x12F.
    std::uint16_t sGpffffb052_ = 0;

    // sGpffffaf4c, which is 0x00359F70 - 0x50B4 = DAT_00354ebc: the number of
    // members built, and the fill cursor while FUN_002432d8 is running.
    std::int16_t DAT_00354ebc_ = 0;
    // sGpffffaf4e = DAT_00354ebe: which member the player drives, 1-based.
    std::int16_t DAT_00354ebe_ = 1;
    // The enemy side, deliberately left empty in this slice. cGpffffaf4a is
    // DAT_00354eba and iGpffffaf44 is DAT_00354eb4; with the count at zero
    // every control block keeps target -1 and FUN_00249610 takes its no-target
    // branch, which is the mode this slice runs in.
    std::int16_t DAT_00354eba_enemyCount_ = 0;

    std::uint32_t DAT_00354fc2_ = 0;
    // DAT_00354f80 / DAT_00354e96: the target-cycle repeat timer and the
    // pentagon's on-screen timer. Both stepped by FUN_00248e58.
    std::uint16_t DAT_00354f80_ = 0;
    std::uint16_t DAT_00354e96_ = 0;
    std::uint32_t targetCycleFrames_ = 0;
    std::uint32_t targetFacingFrames_ = 0;
    std::array<std::uint32_t, 12> DAT_0031dbf8_counters_{};
    std::uint32_t DAT_00354ecc_ = 0;
    std::int32_t DAT_0031dad0_ = -1; // FUN_002d6c68(0x1E3), the shared hit effect

    std::array<std::uint8_t, kLoadoutRows * kLoadoutSlots> DAT_003437a0_{};
    std::array<std::uint8_t, 256> DAT_003437b8_itemCounts_{};
    std::array<std::uint8_t, kLoadoutRows * kLoadoutSlots> defaultLoadout_{};
    std::vector<SpellTableRow> spellTable_;
  };

} // namespace orphen::ported::battle
