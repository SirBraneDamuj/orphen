#pragma once

// Contact and damage.
//
//   src/FUN_002148a8.c  the swept hit test a weapon effect runs every frame
//   src/FUN_00216140.c  what one contact costs the victim
//   src/FUN_00215e48.c  clearing a swing's already-hit set
//   src/FUN_00216078.c  the attack's four parameter bytes (see the resource)
//   src/FUN_002206a8.c  the hit sparks (original_hit_sparks.h)
//
// See analyzed/sword_hit_test_and_damage.c.
//
// **Nothing here decides that anything dies.** FUN_00216140 only accumulates
// into the victim's +0xBE; the victim's own behaviour drains it against its
// hit points on its next frame -- FUN_002cd0a0 for the type 0x62 flyer,
// FUN_00273610 for the party, and one such function per enemy type. That split
// is why a hit landed after a victim has already been dispatched this frame
// still registers: +0xBE is a mailbox, not a subtraction.

#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"
#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/resource/character_stats.h"
#include "ported/resource/hit_parameter_table.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace orphen::ported::entity
{

  // DAT_003151c8: the slots the last hit test connected with, 0xFFFF
  // terminated. Written by every hit test, read by the enemy behaviours that
  // want to know whether their own swing landed. The port keeps it as a vector
  // and drops the terminator.
  using HitList = std::vector<std::uint16_t>;

  // Everything FUN_002148a8 and FUN_00216140 reach outside the two entities.
  struct HitTestEnvironment
  {
    EntityPool *entityPool = nullptr;

    // FUN_0020cdc0 for one slot. Built by the runtime because the attached
    // branches read the parent's persistent bone palette, which is the previous
    // frame's -- exactly as the original reads DAT_00357E00.
    std::function<std::optional<orphen::ported::model::Matrix4>(std::size_t slot)>
        FUN_0020cdc0_entity_matrix;

    // The attacker's loaded model, for the hit-volume sections at PSC3 header
    // +0x30/+0x34. Null when the slot has no model, which makes the test a
    // no-op the way a model with no +0x30 section does.
    std::function<const orphen::ported::model::Psc3Model *(std::size_t slot)> modelForSlot;

    // uGpffffadf8 and uGpffffadfc, the two resource blobs FUN_00216140 reads.
    // A null stats table means the resistance lookup falls back to the
    // all-hundreds table the original uses for a victim with neither +0x02 bit
    // 3 nor bits 0-1 -- not to no damage at all.
    const orphen::ported::resource::CharacterStats *DAT_00354d68_stats = nullptr;

    // FUN_002206a8: the hit sparks (original_hit_sparks.h). Called for every
    // contact that gets past the guard test, with the victim and uVar12 -- the
    // "player-side victim" flag, which the burst stores in each spark's +0x28
    // and which picks one of the two texture rectangles.
    std::function<void(const OriginalEntity &victim, std::int16_t sourceSide)>
        FUN_002206a8_spawn_hit_sparks;

    // FUN_002d5630: the on-screen HP bar (original_health_bar.h). Only fires
    // for a victim whose +0x02 carries 0x4B and whose +0x96 does not carry
    // 0x20, and the widget itself wants a battle-section scene, a maximum above
    // zero and damage above zero -- so it never draws in a field scene like
    // s01_e024. `upperBank` is the victim's `(+0x02 & 0x48) != 0`, which every
    // enemy descriptor satisfies and no party one does. The hit points are the
    // victim's *pre-hit* total, because the type wrapper drains +0x12A a frame
    // later; the bar bands both that and the total minus `damage` into five
    // pips and walks between them.
    std::function<void(bool upperBank, std::int32_t hitPoints, std::int32_t maxHitPoints,
                       std::int32_t damage)>
        FUN_002d5630_damage_bar;

    // DAT_003555bc / iGpffffb64c.
    std::uint32_t frameTicks = 0x20;

    // DAT_003151c8. Cleared and refilled by each test.
    HitList *DAT_003151c8_hitList = nullptr;
  };

  // FUN_00215e48: forget everything this attacker has already hit, and drop the
  // "the swept endpoints are valid" latch at +0x06 bit 0x40.
  //
  // Reproduces the original's window exactly, including the one-word gap: it
  // clears eight words from +0xD0 while the tests read words 1..8 of the same
  // array, so slots 224..255 keep their bit. See OriginalEntity::alreadyHitD0.
  void FUN_00215e48_clear_hit_set(OriginalEntity &attacker);

  // The scratch block every hit test hands FUN_00216140, and the reason both
  // tests appear to return a constant when you read the decompiler's output.
  //
  // The buffer is the test's own stack frame, and the fields the decompiler
  // names as separate locals -- `cStack_161`, `cStack_162` -- are the *same
  // bytes* FUN_00216140 writes through the pointer it is handed. Ghidra never
  // connects the two, so the return looks like a variable nothing assigns.
  // Confirmed by hand: FUN_002148a8's frame is 0x9A0 and its scratch starts at
  // sp+0, so its `cStack_161` is sp+0x83F -- and 0x002164D0 is
  // `sb v0, 2111(s2)`, FUN_00216140 storing to exactly that offset.
  //
  // So **the hit tests return the number of contacts**, which is what makes
  // FUN_002d21b8 play its hit cue and what makes the magic projectile detonate
  // on something rather than only on a wall or a timeout.
  struct HitScratch
  {
    std::int16_t damage834 = 0;   // +0x834: what the last contact charged
    std::int16_t defence838 = 0;  // +0x838: what it was reduced by
    std::int8_t negate83e = 0;    // +0x83E: negates the return; never set here
    std::int8_t contacts83f = 0;  // +0x83F: the return value
  };

  // FUN_00216140: charge one contact to the victim.
  //
  // `attacker` is the effect entity carrying the hit -- the sword blade, not
  // the swinger -- because +0x12C, +0x02 and +0x96 are all read off it.
  void FUN_00216140_apply_hit(OriginalEntity &attacker,
                              const orphen::ported::resource::HitParameters &parameters,
                              OriginalEntity &victim,
                              std::size_t attackerSlot,
                              const HitTestEnvironment &environment,
                              HitScratch &scratch);

  // FUN_002148a8: sweep the attacker's animated hit volume against the pool.
  // Returns the contact count.
  std::int8_t FUN_002148a8_swept_hit_test(OriginalEntity &attacker,
                                          std::size_t attackerSlot,
                                          const orphen::ported::resource::HitParameters &parameters,
                                          const HitTestEnvironment &environment);

  // FUN_00215ac8: the *unswept* form, one caller-supplied axis-aligned box
  // against the pool. Same candidate filter, same already-hit set, same
  // FUN_00216140 -- it simply skips everything FUN_002148a8 does to build a
  // swept volume out of animation data, because its callers already know the
  // box they want tested. The magic projectile's is a 0.15 cube in the
  // horizontal plane running from its feet to its full height.
  //
  // `box` is the original's `float[6]`: {minX, maxX, minY, maxY, minZ, maxZ} in
  // the entity axis order. Returns the contact count.
  std::int8_t FUN_00215ac8_box_hit_test(OriginalEntity &attacker,
                                        std::size_t attackerSlot,
                                        const std::array<float, 6> &box,
                                        const orphen::ported::resource::HitParameters &parameters,
                                        const HitTestEnvironment &environment);

  // Diagnostics. Not part of the original -- the report needs a way to tell
  // "the test never ran" from "it ran and nothing was in range", which are very
  // different bugs.
  struct HitTestStats
  {
    std::uint32_t tests = 0;      // times FUN_002148a8 built a sweep
    std::uint32_t boxes = 0;      // boxes those sweeps laid down, after subdivision
    std::uint32_t contacts = 0;   // entities the sweeps touched
    std::uint32_t damage = 0;     // points those contacts charged, before any guard
    // The union of every box the last sweep laid down, in world space. Reading
    // it against an enemy's own position is how you tell "the swing is too
    // short" from "the swing is at the wrong height".
    std::array<float, 6> lastSweepBounds{};
    bool lastSweepValid = false;
  };
  const HitTestStats &hitTestStats();
  void resetHitTestStats();

} // namespace orphen::ported::entity
