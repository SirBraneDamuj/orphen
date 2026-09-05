#pragma once

// src/FUN_00249610.c (0x00249610) and src/FUN_0024a360.c (0x0024A360): the
// battle counterpart of the field player update, and the action-to-state
// translation in front of it.
//
// FUN_002239c8:117 chooses between them:
//
//   if ((cGpffffb663 == 0) || (sGpffffb052 == 0))  FUN_00251ed8(0x58beb0, ...);
//   else                                           FUN_00249610(0x58beb0);
//
// **FUN_0024a360 is the whole of "a button becomes a state".** For a pending
// action byte in 0x84..0x92 it computes `state = action + 0x3FE5` -- states
// 105..119 -- and copies the byte into control block +0x0F. Bit 0x4000 rides on
// the state as a restart marker: a handler that sees it is being entered, and
// clears it itself.
//
// FUN_00249610 then dispatches through a table of 24 handlers per character
// class, selected by party record +0x00 and indexed by `(state & 0xBFFF) - 100`:
//
//   class 1 (Orphen) 0x0031DD60   class 5 0x0031DDC0   class 3 0x0031DE20
//   class 4          0x0031DE80   class 6 0x0031DEE0   class 7 0x0031DF40
//
// Only class 1 is ported. The handler is called with the entity and the
// character's charge halfword (+0x62) and its return value is written back
// there, so that round trip is load-bearing: break it and charging looks like
// it does nothing.
//
// **Facing the target.** FUN_00249610:166-310 is the block between the state
// translation and the dispatch, and it is what turns a character onto whatever
// control block +0x2C names. Four things switch it off, all of them the
// original's: action 0x8E, a target below 3 (pool slots 0..2 are the party), the
// suspend flag DAT_00354ECC, and bit 0x400 of the member's DAT_0031DA6C flags.
//
// DAT_00354ECC is **not** a pre-battle lock, which is what the port's comments
// used to call it. FUN_002432D8 clears it, and the only things that raise it
// are five effect-entity behaviours in the 0x2Exxxx block -- FUN_002E01F8,
// FUN_002E1320, FUN_002E23E8, FUN_002E34B8 and FUN_002E65D0 -- each setting it
// on entry and clearing it on the way out. It means "a cinematic spell owns the
// screen", and it holds the whole party still while one plays.
// A fifth stops the turn without stopping the block -- a non-zero charge timer
// at party record +0x3C, read through FUN_002494E0, so the aim is pinned once a
// spell starts building.
//
// **Only an idle character turns at all.** The wide branch is gated on the
// current action being 0x06 or 0x96; inside five degrees the snap branch runs
// whatever the action is. So a spell released mid-swing goes where the swing
// left the facing, which is the original's behaviour and not a bug to smooth
// over.
//
// The upper body is separate, and class 1 only: the spine (bone 0x20) carries
// the yaw the legs have not caught up on, clamped to fifty degrees, and during
// animation 0x14 at timeline cursor 4 -- the beat the cast's arms come up --
// bones 10 and 9 take the target's elevation.

#include "ported/battle/battle_party.h"
#include "ported/battle/battle_trace.h"
#include "ported/entity/entity_path_follow.h"
#include "ported/entity/entity_pool.h"
#include "ported/model/psc3_skeleton.h"

#include <cstdint>
#include <functional>
#include <span>

namespace orphen::ported::battle
{

  struct BattleUpdateEnvironment
  {
    BattleParty *party = nullptr;
    orphen::ported::entity::EntityPool *pool = nullptr;
    const orphen::ported::entity::EntityDescriptorTable *descriptors = nullptr;
    BattleTrace *trace = nullptr;
    // FUN_00244318's kind-2 array. State 108 walks the character back to its
    // mark through the same follower a scripted path uses.
    orphen::ported::entity::PathFollowerTable *paths = nullptr;
    std::uint16_t frameTicks = 0x20;
    // The face-the-target block drives three bones directly, and reads two of
    // them back first: DAT_004A7E00 is the override table FUN_0020D8C0 writes
    // and DAT_003FFE00 the smoothed pose FUN_0020D9D8 reads.
    std::span<orphen::ported::model::EntityBoneOverrides> boneOverrides;
    std::span<const orphen::ported::model::EntityPoseFilter> poseFilters;
    // FUN_00267d38: play a cue at an entity. The battle states ask for the
    // charge loop (0xD7..0xDA), the guard raise (0xE7) and the guard hit
    // (0xE8).
    std::function<void(std::uint16_t cue, std::size_t slot)> FUN_00267d38_play_at_entity;
    // FUN_00216868, through the runtime's seeded LCG.
    std::function<std::uint32_t()> FUN_00216868_random;

    // == The spell voice ==
    //
    // Casting a spell speaks two lines: an incantation as the charge builds and
    // a shout on release. They are not sound cues -- they are VOICE.BIN clips,
    // played through the same reserved streaming voice a line of dialogue uses,
    // and the whole thing is driven by a four-step state machine per loadout
    // slot in DAT_0031DA60:
    //
    //   1  ask for the bank (FUN_00206ae0), and on acceptance -> 2
    //   2  poll the load (FUN_00206c28), and when it settles -> 3
    //   3  at the charge marker, if nothing else is speaking, play clip 0 -> 100
    //   100 on release, play clip 1 -> 101, or -> 102 if something is speaking
    //
    // The bank id is DAT_0031DA54, which FUN_002432d8 took from the spell
    // table's family column at 0x00325230. Hand of Pyro is bank 7.
    //
    // Only FUN_00249348 characters speak: party slot 1, and not classes 4 or 7.

    // FUN_00206ae0(bankId, channel, 0). True for the original's 0 -- the bank is
    // cached or a load has been started.
    std::function<bool(std::uint32_t bankId, std::uint32_t channel)> FUN_00206ae0_cache_voice;
    // FUN_00206c28. True for its 1: nothing is loading.
    std::function<bool()> FUN_00206c28_voice_load_idle;
    // FUN_00206a90, i.e. DAT_00356788: something is already speaking.
    std::function<bool()> FUN_00206a90_voice_busy;
    // FUN_00206f08(channel, clipIndex). True for its 0, meaning it started.
    std::function<bool(std::uint32_t channel, std::uint32_t clipIndex)> FUN_00206f08_play_voice;
  };

  // FUN_0024a360: spend the pending action byte. Returns the original's three
  // codes -- 0 normal, 1 staggered, 2 dead, 3 knocked down.
  std::uint32_t FUN_0024a360_take_pending_action(const BattleUpdateEnvironment &environment,
                                                 std::size_t entitySlot);

  // FUN_00249610. Runs for one member; this slice only ever calls it for the
  // lead, pool slot 0.
  void FUN_00249610_battle_character_update(const BattleUpdateEnvironment &environment,
                                            std::size_t entitySlot);

  // FUN_002493f0 (0x002493f0): **where an elemental spell lands**, and the
  // caster's target, in one call. Both of its callers are on the entity side --
  // FUN_002deae8 asks every frame to place the growing circle, and again on the
  // launch frame to place the damage box -- so the runtime hands it to the
  // actor layer as a callback.
  //
  //   control +0x2C >= 2   the party record's +0x28, which state 113 has been
  //                        tracking onto the target every frame
  //   control +0x2C <  2   **two world units straight ahead of the caster**:
  //                        2*cos(+0x5C) + x, 2*sin(+0x5C) + z, y
  //
  // The no-target case is not a fallback, it is the rule the game plays by when
  // nothing is locked on -- it only looks like it varies by battlefield because
  // it varies with which way the caster is facing. Note the threshold is `< 2`;
  // FUN_00249610's face-the-target block uses `< 3`, and they are deliberately
  // different numbers.
  std::int32_t FUN_002493f0_spell_landing(const BattleParty &party,
                                          const orphen::ported::entity::OriginalEntity &caster,
                                          orphen::ported::psm2::Vec3 &out);

  // The class-1 table at 0x0031DD60, exposed so --battle-report can say whether
  // a state it saw has a handler.
  bool class1StateIsPorted(std::uint16_t state);

} // namespace orphen::ported::battle
