#pragma once

// The field attacks of the party characters who are not Orphen.
//
//   src/FUN_00256bb8.c  0x00256BB8  the grounded state's attack/magic dispatch
//   src/FUN_002d06b0.c  0x002D06B0  spawn Sephy's orb, type 0x4F
//   src/FUN_002d05e8.c  0x002D05E8  type 0x4E, Sephy's held weapon
//   src/FUN_002d07d0.c  0x002D07D0  type 0x4F, Sephy's orb
//   (no src/ file)      0x002D09B8  type 0x50, class 4's held weapon
//   (no src/ file)      0x002D09F0  type 0x51, class 4's hand prop
//   src/FUN_002d0a30.c  0x002D0A30  type 0x52, class 4's thrown projectile
//   src/FUN_002d0c00.c  0x002D0C00  type 0x56, class 5's drifting projectile
//
// **What a character can do is decided by FUN_002298D0's class of the lead's
// type**, in FUN_00256BB8, and nothing else. There is no weapon inventory
// behind it. The whole table:
//
//   class (type)     attack (0x20)                  magic (0x10)
//   0 (1, Orphen)    state 0x1C, anim 0x33          state 0x1D, anim 0x14
//   1 (3)            --                             --
//   2 (4)            --                             --
//   3 (5)            state 0x1E, anim 0x33 + 0x4E   state 0x1F, anim 0x14
//   4 (6)            state 0x20, anim 199  + 0x50   state 0x21, anim 0x14
//   5 (7)            --                             state 0x23, anim 0x14
//   6 (0x16)         --                             --
//
// A "--" is not an unported case: the press falls through to the idle and
// locomotion block below it, exactly as if nothing had been pressed.
//
// The two held weapons are spawned by FUN_00256BB8 itself, on the press, and
// both kill themselves the moment the lead leaves the state that made them.
// Everything else is spawned by the state handler partway through its
// animation. The state handlers are OriginalPlayerController's; this file
// holds the pool side -- the spawns and the six entity behaviours.
//
// FUN_0023BBD8(0, 3), which several of these call on a hit or a launch, is
// skipped here for the same reason the sword and Orphen's magic skip it: the
// port reaches the engine only through FUN_00267D38.

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  inline constexpr std::int16_t kPartyHeldWeapon4eTypeId = 0x4E;
  inline constexpr std::int16_t kPartyOrb4fTypeId = 0x4F;
  inline constexpr std::int16_t kPartyHeldWeapon50TypeId = 0x50;
  inline constexpr std::int16_t kPartyHandProp51TypeId = 0x51;
  inline constexpr std::int16_t kPartyThrow52TypeId = 0x52;
  inline constexpr std::int16_t kPartyDrift56TypeId = 0x56;

  // DAT_0031e0b8 and DAT_0031e0c8: the bone-local offsets FUN_002567c0 and
  // FUN_002569d8 hand FUN_0020dc88 for the role-4 (hand) bone. The second is
  // all zeros in the executable, so class 5's projectile starts at the bone's
  // origin.
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0031e0b8_throwHandOffset{
      -0.119999997f, -0.0599999987f, 0.139999986f};
  inline constexpr orphen::ported::psm2::Vec3 kDAT_0031e0c8_driftHandOffset{0.0f, 0.0f, 0.0f};

  // FUN_00256BB8's spawn block for the two held weapons, 0x4E and 0x50.
  // Returns the pool slot, or -1 when the pool is full -- in which case the
  // original has already put the lead in the attack state and leaves it there.
  std::int32_t FUN_00256bb8_spawn_held_weapon(const OriginalEntity &owner,
                                              std::size_t ownerSlot,
                                              std::int16_t typeId,
                                              const ActorEnvironment &environment);

  // FUN_002D06B0: Sephy's orb, type 0x4F, at `point`. -1 when the pool is full.
  std::int32_t FUN_002d06b0_spawn_orb(const OriginalEntity &owner,
                                      std::size_t ownerSlot,
                                      const orphen::ported::psm2::Vec3 &point,
                                      const ActorEnvironment &environment);

  // FUN_002567C0's two spawns: the 0x51 prop on the hand and the 0x52 it throws.
  std::int32_t FUN_002567c0_spawn_hand_prop(const OriginalEntity &owner,
                                            std::size_t ownerSlot,
                                            const ActorEnvironment &environment);
  std::int32_t FUN_002567c0_spawn_throw(const OriginalEntity &owner,
                                        std::size_t ownerSlot,
                                        const ActorEnvironment &environment);

  // FUN_002569D8's spawn: one 0x56 at the hand. -1 when the pool is full.
  std::int32_t FUN_002569d8_spawn_drift(const OriginalEntity &owner,
                                        std::size_t ownerSlot,
                                        const ActorEnvironment &environment);

  // FUN_0020DC88 at the owner's role-4 bone plus `offset`, written to +0x20 and
  // mirrored into +0x4C -- the "hold it in the hand" both class 4 and class 5
  // do.
  void holdAtHand(OriginalEntity &held,
                  std::size_t ownerSlot,
                  const orphen::ported::psm2::Vec3 &offset,
                  const ActorEnvironment &environment);

  void FUN_002d05e8_held_weapon_4e(OriginalEntity &weapon, std::size_t slot,
                                   const ActorEnvironment &environment);
  void FUN_002d07d0_orb_4f(OriginalEntity &orb, std::size_t slot,
                           const ActorEnvironment &environment);
  void FUN_002d09b8_held_weapon_50(OriginalEntity &weapon, std::size_t slot,
                                   const ActorEnvironment &environment);
  void FUN_002d09f0_hand_prop_51(OriginalEntity &prop, std::size_t slot,
                                 const ActorEnvironment &environment);
  void FUN_002d0a30_throw_52(OriginalEntity &shot, std::size_t slot,
                             const ActorEnvironment &environment);
  void FUN_002d0c00_drift_56(OriginalEntity &shot, std::size_t slot,
                             const ActorEnvironment &environment);

  // FUN_002148A8 with a parameter record the caller names, since these types
  // keep theirs at +0x198 or +0x1AA rather than one fixed place. Zero with no
  // hit-test environment.
  std::int8_t partyWeaponHitTest(OriginalEntity &attacker, std::size_t slot,
                                 std::uint32_t packedParameters,
                                 const ActorEnvironment &environment);

} // namespace orphen::ported::entity
