#pragma once

// The battle *actor* table -- the list every target the player can aim at is
// drawn from, and the one thing the battle module reads that does not live in
// the executable.
//
//   src/FUN_0023f318.c   loads it, from the scene script, at scene load
//   src/FUN_0023fc08.c   counts it and validates every entity binding
//   src/FUN_0023fb50.c   clears the per-battle fields back out
//   src/FUN_0023eba0.c   the lookup: id -> record
//   src/FUN_00247d80.c   the reverse: id -> index, which is what target
//                        cycling steps through
//   src/FUN_0023eff8.c   how many records hold a live *enemy*
//   src/FUN_0023f080.c   the same count, but only ones still alive
//
// **It is not the control-block array.** Both are 0x3C-byte records with an
// entity at +0x08, a pending/current action pair at +0x0E/+0x0F, a target at
// +0x2C and a flag word at +0x38 -- which is exactly why the two are easy to
// confuse -- but `0x0031D7B0` holds the ten *party* control blocks and this one
// holds every battle participant the encounter data names. FUN_0023eba0 picks
// between them on the id: below 10 is a party member, 10 and up is an actor
// here.
//
// Ids 0x50 and up are the party side; FUN_0023eff8 and FUN_0023f080 both count
// `id < 0x50` only, and FUN_00241a88's AI skips anything above 'O' (0x4F). So
// "an enemy" is a record with an id under 0x50 and a bound, living entity.
//
// ---------------------------------------------------------------- where it is
//
// FUN_0023f318(0) -> FUN_0025ba28(0), which is the scene script's own section
// table: header word 7 points at an array of blob-relative offsets and entry 0
// is the battle data. s14_e012 has word 7 = 0x2B4C and entry 0 = 0x1880.
//
// The blob is then relocated in place, which the port does not have to
// reproduce as pointer arithmetic: every "pointer" the original fixes up
// becomes an offset from the start of the decoded script, so a value printed
// here can be looked up directly in a --scr-dump.
//
//   +0x00  flags; bit 31 is "already relocated"
//   +0x04  offset to the camera/placement sub-blob (DAT_00354FA8)
//   +0x10  the number of encounter groups
//   +0x14  two words per group: the actor array, then the master battle script
//
// FUN_0023f318 takes group 0 unconditionally. Its first word becomes
// DAT_00354EB4 -- this table -- and its second becomes DAT_0031DBD8, the master
// script the FUN_0023FD30 VM starts on.

#include "ported/battle/battle_tables.h"
#include "ported/entity/entity_pool.h"
#include "ported/resource/hit_parameter_table.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace orphen::ported::battle
{

  // Record field offsets. The names that also exist on a control block are
  // spelled the same way on purpose -- FUN_0023eba0 hands both kinds back
  // through one pointer and the callers do not distinguish.
  namespace actor
  {
    inline constexpr std::uint32_t kId00 = 0x00;      // == the entity's +0x95
    inline constexpr std::uint32_t kGroup02 = 0x02;   // FUN_00242660 sub-op 3 marks these
    inline constexpr std::uint32_t kEntity08 = 0x08;  // pool slot, -1 for none
    inline constexpr std::uint32_t kAlive0c = 0x0C;   // FUN_0023fc08 counts up while bound
    inline constexpr std::uint32_t kCursor0d = 0x0D;  // the type 0x192 cursor's pool slot
    inline constexpr std::uint32_t kPendingAction0e = 0x0E;
    inline constexpr std::uint32_t kCurrentAction0f = 0x0F;
    inline constexpr std::uint32_t kFlags10 = 0x10;
    inline constexpr std::uint32_t kSpawnX14 = 0x14; // s16, world * 10
    inline constexpr std::uint32_t kSpawnY16 = 0x16;
    inline constexpr std::uint32_t kSpawnZ18 = 0x18;
    inline constexpr std::uint32_t kRingCursor1c = 0x1C; // FUN_00247b68 walks +0x1D..+0x23 from here
    inline constexpr std::uint32_t kRing1d = 0x1D;
    inline constexpr std::uint32_t kStat24 = 0x24;
    inline constexpr std::uint32_t kStat25 = 0x25;
    inline constexpr std::uint32_t kTarget2c = 0x2C;   // s16 pool slot, -1 = none
    inline constexpr std::uint32_t kYield2e = 0x2E;    // what the VM handler returned
    inline constexpr std::uint32_t kScriptPc30 = 0x30; // script offset, 0 = none
    inline constexpr std::uint32_t kTriggers34 = 0x34; // FUN_00240870's watch table
    inline constexpr std::uint32_t kFlags38 = 0x38;
  } // namespace actor

  inline constexpr std::uint32_t kActorRecordStride = 0x3C;

  // The id at and above which a record is on the player's side. FUN_0023eff8
  // and FUN_0023f080 both stop counting here.
  inline constexpr std::uint8_t kFirstFriendlyId = 0x50;

  class BattleEncounter
  {
  public:
    // FUN_0023f288: forget everything. Called on every scene load, before the
    // new script is handed over.
    void FUN_0023f288_reset();

    // FUN_0023f318. `script` is the decoded scene script blob; `headerWord7` is
    // its section table offset. Returns true when the scene ships battle data.
    bool FUN_0023f318_load(const std::vector<std::uint8_t> &script, std::uint32_t headerWord7);

    bool available() const { return actorArray_ != 0; }

    // DAT_00354EB4 and DAT_00354EBA, as an offset and a count. The count is a
    // signed char in the original and is recomputed by FUN_0023fc08 every
    // frame the VM ticks, not cached at load.
    std::uint32_t DAT_00354eb4_actorArray() const { return actorArray_; }
    std::int8_t DAT_00354eba_actorCount() const { return actorCount_; }
    std::uint32_t record(std::size_t index) const
    {
      return actorArray_ + static_cast<std::uint32_t>(index) * kActorRecordStride;
    }

    // DAT_0031DBD8: the master battle script's offset, and DAT_00354EAC, the
    // VM's program counter. Both are offsets into the same window.
    std::uint32_t DAT_0031dbd8_masterScript() const { return masterScript_; }

    // FUN_0023fb50: clear the per-battle fields (+0x02 unless it is -1, +0x03,
    // +0x04/+0x05, +0x06/+0x07) out of every record, then recount. Runs once
    // when the encounter data is installed.
    void FUN_0023fb50_reset_records();

    // FUN_0023fc08: recount the table and drop any binding that has gone
    // stale. A record keeps its entity only while the entity is live, has a
    // type, and still carries the record's own id in +0x95 -- which is what
    // makes +0x95 the join between the two halves of the battle module.
    void FUN_0023fc08_bind(const orphen::ported::entity::EntityPool &pool);

    // FUN_0023eba0. Returns 0 when there is no such record. `allowUnbound`
    // is the original's second parameter: with it clear, a record whose entity
    // has gone is invisible.
    std::uint32_t FUN_0023eba0_find(std::uint16_t id, bool allowUnbound) const;

    // FUN_00247d80: the *index* of the record carrying `id`, or -1. Target
    // cycling steps this index, not the id.
    std::int32_t FUN_00247d80_index_of(std::uint8_t id) const;

    // FUN_002476c0: step the target one place round the table.
    //
    //   `currentTarget` is the entity the player is aimed at *now* -- a pool
    //   slot, because that is what the control block's +0x2C holds.
    //   `direction` is +1, -1, or 0 for "just validate what we have".
    //   `halfWrap` is the original's third parameter: start the search half a
    //   table away instead of one step, which is what the Up+Down chord does.
    //
    // Returns a record *index*, or -1 when nothing in the table is targetable.
    // A record is targetable while it has an entity with a type and hit points
    // left, its flag word does not carry bit 0x40 (out of the fight), and
    // neither action byte is 0x11 (mid-spawn).
    std::int32_t FUN_002476c0_cycle(const orphen::ported::entity::EntityPool &pool,
                                    std::int32_t currentTarget, std::int16_t direction,
                                    bool halfWrap) const;

    // FUN_0023eff8 / FUN_0023f080: how many records hold an enemy, and how many
    // of those are still standing. The pair is what a "wipe out the enemies"
    // script condition tests.
    std::int32_t FUN_0023eff8_enemy_count(const orphen::ported::entity::EntityPool &pool) const;
    std::int32_t FUN_0023f080_living_enemy_count(const orphen::ported::entity::EntityPool &pool) const;

    // Raw access to the relocated script window. Offsets are from the start of
    // the decoded scene script.
    template <typename T> T read(std::uint32_t offset) const
    {
      T value{};
      if (offset + sizeof(T) <= memory_.size())
      {
        std::memcpy(&value, memory_.data() + offset, sizeof(T));
      }
      return value;
    }
    template <typename T> void write(std::uint32_t offset, T value)
    {
      if (offset + sizeof(T) <= memory_.size())
      {
        std::memcpy(memory_.data() + offset, &value, sizeof(T));
      }
    }

    // The entity bound to a record, as a pool slot. -1 when there is none --
    // the port's standing substitution for the original's null pointer.
    std::int32_t entitySlot(std::uint32_t recordOffset) const
    {
      return read<std::int32_t>(recordOffset + actor::kEntity08);
    }
    void setEntitySlot(std::uint32_t recordOffset, std::int32_t slot)
    {
      write<std::int32_t>(recordOffset + actor::kEntity08, slot);
    }

    // FUN_0023f8b8: **the binder.** An enemy entity registers itself into the
    // actor table; the table never goes looking for entities.
    //
    // This is the function that makes a target exist. It walks *every*
    // encounter group -- uGpffffb02c groups at iGpffffb030, not just the one
    // FUN_0023f318 selected -- and takes the first record whose id byte equals
    // the entity's +0x95. That record's +0x02, +0x03..+0x07, +0x0E, +0x0F and
    // +0x38 are wiped (all but +0x10 only when +0x0E is not already 0x11, the
    // mid-spawn marker), the entity goes into +0x08, +0x2C and +0x2D go to -1,
    // and the entity's +0x4B bit 0 is raised. The original returns
    // `record + 0x0C`, which every caller parks in its own +0x198.
    //
    // Two paths reach it, and neither is the battle VM:
    //
    //   - each enemy type's **state 0**, e.g. FUN_0028ae10 for type 0x8A and
    //     FUN_0027f978 for type 0x80 -- thirty of them, all calling it right
    //     after FUN_0025bae8 has stamped the stat record on
    //   - FUN_0025e7c0's placement path, via the FUN_0023fb20 wrapper, for a
    //     placement record whose +0x0F byte is negative
    //
    // FUN_00240870 -- the trigger-table walker the VM runs -- is the *third*
    // way a record gets an entity, and it is the one that spawns reinforcement
    // waves. It is not how the enemies standing in the arena at the start of a
    // battle got there.
    //
    // The port has no per-type enemy state machines yet, so the call site is
    // the spawn itself; see FUN_0025eb48_set_pw_all. `entitySlot` is the pool
    // slot and `id` is the +0x95 it was stamped with. Returns the record
    // offset, or 0 when no group names that id.
    std::uint32_t FUN_0023f8b8_bind_entity(orphen::ported::entity::EntityPool &pool,
                                           std::size_t entitySlot,
                                           const orphen::ported::resource::HitParameterTable *hits);

    // The group table, from FUN_0023f318's relocation pass. Group 0 is the one
    // in play; FUN_0023f8b8 and VM opcode 6 are the only things that look past
    // it.
    std::size_t groupCount() const { return groupActorArrays_.size(); }
    std::uint32_t groupActorArray(std::size_t group) const { return groupActorArrays_[group]; }

    // sGpffffb054, the pre-battle countdown. FUN_0023fb50 seeds it with half of
    // the halfword at DAT_00354FA4 -- the word immediately past the group table
    // -- floored at 0x5A, and FUN_0023fd30 spends it three times a frame. The
    // frame it reaches zero is the frame every bound record gets its 0x192
    // cursor, so this is the clock the whole target display hangs off.
    std::int16_t sGpffffb054_countdown() const { return countdown_; }
    void setSGpffffb054_countdown(std::int16_t value) { countdown_ = value; }

    // DAT_00354FAC / DAT_00354FB0, from FUN_00246fc0: the battle camera's
    // spline pairs, taken from the placement sub-blob's +0x5C count and +0x60
    // array. FUN_0023c340 returns immediately when the array is null, and that
    // gate is what decides whether the battle camera -- and the target display
    // and the freeze behind it -- runs at all.
    // ------------------------------------------------------------- the VM
    //
    // Everything FUN_0023FD30 reaches for that does not live in the encounter
    // blob. The party half is passed in rather than linked against, because
    // BattleParty already depends on this class and the VM only needs three
    // things from it.
    struct VmEnvironment
    {
      const orphen::ported::entity::EntityPool *pool = nullptr;
      std::uint32_t *scriptVars = nullptr;    // DAT_00355060
      std::size_t scriptVarCount = 0;
      std::int32_t frameTicks = 0;            // iGpffffb64c / DAT_003555BC
      std::uint32_t DAT_00354ecc_suspended = 0;
      std::int16_t sGpffffaf4c_memberCount = 0; // DAT_00354EBC
      std::function<std::uint32_t()> FUN_00216868_random;
      // (&DAT_0031D7B8)[member * 0xF] -- control block +0x08 -- as a pool slot
      // or -1. The retarget block below is the only thing that reads it.
      std::function<std::int32_t(std::int32_t member)> DAT_0031d7b8_memberEntity;
      // FUN_00244248's party half. An entity whose +0x95 is below 11 lands on
      // a control block, not on a record here, and only BattleParty owns
      // those. 1 = accepted, -1 = busy, 0 = no such block.
      std::function<std::int32_t(std::uint8_t partySlot, std::uint8_t action, bool force)>
          FUN_00244248_party;
    };

    // One block the VM can be running on: the master pseudo-record DAT_0031DBA8
    // or one 0x3C actor record. The original keeps the master's three fields in
    // globals and an actor's in the record, but the handlers cannot tell them
    // apart -- they address everything through the PC global DAT_00354EAC and
    // the `param_1` block pointer -- so one struct serves both.
    struct VmBlock
    {
      std::uint32_t record = 0; // 0 for the master pseudo-record
      std::uint32_t pc = 0;     // +0x30 / DAT_0031DBD8
      std::int16_t yield = 0;   // +0x2E / DAT_0031DBD6
      std::uint32_t triggers = 0; // +0x34 / DAT_0031DBDC
    };

    struct VmStepResult
    {
      bool halted = false;
      std::uint8_t haltOpcode = 0;
      std::uint32_t haltOffset = 0;
      int actorScriptsInstalled = 0;
      bool triggerTableInstalled = false;
      std::int16_t cameraMode = 0;
      int actionsRequested = 0;
      int actionsRefused = 0;
    };
    using MasterStepResult = VmStepResult;

    // FUN_0023fd30's master-script loop. Steps the VM at PTR_LAB_0031d118 on
    // the master pseudo-record DAT_0031DBA8 until a handler yields.
    MasterStepResult FUN_0023fd30_step_master_script(const VmEnvironment &environment);

    // FUN_0023fd30:57-130, **the enemy AI**. Two things per record, in this
    // order and both unconditional on the other:
    //
    //   1. step the record's own +0x30 script, when it has an entity, a script
    //      and bit 0x20 of +0x38 -- the bit opcode 18 sets when it installs one
    //   2. validate +0x2C and, when it has gone negative, walk the +0x1D..+0x1F
    //      preference ring for a new party member to aim at
    //
    // Step 2 is why an enemy under AI aims at a *chosen* member rather than
    // falling back on pool slot 0 the way an idle one does.
    VmStepResult FUN_0023fd30_step_actor_scripts(const VmEnvironment &environment);
    static std::int32_t FUN_00248f18_find_by_tag(const orphen::ported::entity::EntityPool &pool,
                                                 std::uint8_t id);
    bool masterHalted() const { return masterHalted_; }
    bool actorHalted() const { return actorHalted_; }
    std::uint8_t actorHaltOpcode() const { return actorHaltOpcode_; }
    std::uint32_t actorHaltOffset() const { return actorHaltOffset_; }
    std::uint8_t masterHaltOpcode() const { return masterHaltOpcode_; }
    std::uint32_t masterHaltOffset() const { return masterHaltOffset_; }
    std::uint32_t DAT_0031dbd8_pc() const { return masterPc_; }

    std::uint32_t DAT_00354fac_cameraSplines() const { return cameraSplines_; }
    std::uint32_t DAT_00354fb0_cameraSplineCount() const { return cameraSplineCount_; }

    // One 0xC-byte entry of that array. FUN_00246fc0 relocates the two words by
    // iGpffffb0e8, the *scene script* base -- not the encounter blob's own --
    // so in the port they are already script offsets and need no fixup. Both
    // name a list shaped `{u32 count, then count script-encoded x/y/z triples}`,
    // which FUN_0025d618 decodes in place.
    //
    // The order is the one FUN_00217e88's argument shuffle produces, and it is
    // the wrong way round from the obvious reading: word 0 is the **look-at**
    // list and word 1 the **eye** list.
    struct CameraSpline
    {
      std::uint32_t lookAtList = 0; // +0x00
      std::uint32_t eyeList = 0;    // +0x04
      std::int32_t duration = 0;    // +0x08, in ticks
    };
    CameraSpline cameraSpline(std::uint32_t index) const
    {
      CameraSpline entry;
      if (cameraSplines_ == 0 || index >= cameraSplineCount_)
      {
        return entry;
      }
      const std::uint32_t at = cameraSplines_ + index * 0x0Cu;
      entry.lookAtList = read<std::uint32_t>(at + 0);
      entry.eyeList = read<std::uint32_t>(at + 4);
      entry.duration = read<std::int32_t>(at + 8);
      return entry;
    }

    std::size_t windowSize() const { return memory_.size(); }

  private:
    // A copy of the decoded scene script. The battle data is relocated inside
    // it and the VM writes into the records, so it cannot be a view of
    // SceneScript's own const blob.
    std::vector<std::uint8_t> memory_;
    std::uint32_t blob_ = 0;         // FUN_0025ba28(0)
    std::uint32_t actorArray_ = 0;   // DAT_00354EB4
    std::uint32_t masterScript_ = 0; // DAT_0031DBD8
    std::uint32_t placement_ = 0;    // DAT_00354FA8
    std::int8_t actorCount_ = 0;     // DAT_00354EBA

    // uGpffffb02c / iGpffffb030: the group count and one actor-array offset per
    // group. FUN_0023f8b8 searches all of them.
    std::vector<std::uint32_t> groupActorArrays_;
    std::uint32_t DAT_00354fa4_ = 0; // DAT_00354F98 + count * 2 + 1
    std::uint32_t cameraSplines_ = 0;      // DAT_00354FAC
    std::uint32_t cameraSplineCount_ = 0;  // DAT_00354FB0

    // The master pseudo-record DAT_0031DBA8's own three fields: +0x2E the
    // yield, +0x30 the program counter, +0x34 the trigger table.
    std::uint32_t masterPc_ = 0;       // DAT_0031DBD8
    std::int16_t masterYield_ = 0;     // DAT_0031DBD6
    std::uint32_t masterTriggers_ = 0; // DAT_0031DBDC
    bool masterHalted_ = false;
    std::uint8_t masterHaltOpcode_ = 0;
    std::uint32_t masterHaltOffset_ = 0;
    std::int16_t countdown_ = 0;     // sGpffffb054
    bool actorHalted_ = false;
    std::uint8_t actorHaltOpcode_ = 0;
    std::uint32_t actorHaltOffset_ = 0;

    // One step of the VM on `block`, running opcodes until a handler yields a
    // value that is not negative. False means an opcode this port does not
    // implement; `result` names it and the caller parks the script.
    bool stepVmBlock(VmBlock &block, const VmEnvironment &environment, VmStepResult &result);

    // FUN_00244248. `entityId` is the entity's +0x95: below 11 it is a party
    // slot and the request goes through the environment, otherwise it is an
    // actor id and FUN_00247d80 finds the record here. 1 = accepted, -1 = the
    // actor is busy and the caller should retry, 0 = no such block.
    std::int32_t FUN_00244248_request_action(const VmEnvironment &environment,
                                             std::int32_t entityId,
                                             std::uint8_t action,
                                             bool force);

    // FUN_00247d80: the index of the record carrying this id, or -1.
    std::int32_t FUN_00247d80_index_for_id(std::uint8_t id) const;

    // FUN_0023eff8: how many records hold a bound *enemy* entity -- id below
    // 0x50, and the entity still carrying that id in +0x95. VM opcode 16's
    // sub-ops 2 and 3 scale their delay by it, so a crowded fight makes every
    // enemy wait proportionally longer between attacks.
    std::int32_t FUN_0023eff8_bound_enemy_count(
        const orphen::ported::entity::EntityPool &pool) const;

    // The three-way test the retarget block applies to a control block's
    // entity before it will aim an enemy at that member.
    bool memberIsTargetable(const VmEnvironment &environment, std::int32_t slot) const;
  };

} // namespace orphen::ported::battle
