#pragma once

// Per-frame animation advance.
//
//   src/FUN_00225c90.c  walks the selected animation's timeline
//   src/FUN_00225c60.c  turns a timeline duration into a tick countdown
//   src/FUN_00225bc8.c  selects an animation
//
// The timeline lives inside the model. Each 8-byte record in the table at PSC3
// header +0x0C holds, in its first dword, an offset to that animation's
// timeline; entries there are six bytes:
//
//     +0x00 u16 pose column, what every bone's track is indexed by
//     +0x02 u16 duration, bit 15 marking the last entry
//     +0x04 u16 carried into entity +0xAA
//
// FUN_00225c90 keeps the cursor in entity +0xA8, stepping it by 2 per entry
// while the entry itself is 6 bytes wide -- the read is `timeline + (cursor/2)
// * 3` over halfwords. Durations become countdowns through FUN_00225c60, which
// is `duration << 5` saturated at 0x7FFE, with 9999 reserved as the "never
// expire" value 0x7FFF.
//
// Verified against the EE dump before any of this was written: the chest's
// animation 4 reads [12, 0x8001, 0] and its +0xAC is 12 with +0xA6 at 32; the
// lead player's animation 1 reads [1, 6, 0] then [3, 60, 0] and its +0xAE /
// +0xAC are 1 and 3 with +0xA6 at 1920 -- 60 << 5.
//
// Not ported: the `entity +0x02 & 0x200` branch, which walks a different
// timeline layout (four-byte records, twelve-bit columns) for a class of model
// nothing in s01_e024 uses, and the FUN_002681c0 diagnostics.

#include "ported/entity/original_entity.h"
#include "ported/model/psc3_model.h"

#include <cstdint>

namespace orphen::ported::model
{

  // FUN_00225c60.
  std::int16_t FUN_00225c60_duration_to_ticks(int duration);

  // FUN_00225c90's `entity +0x02 & 0x200 == 0` branch. `frameTicks` is
  // uGpffffb64c, the same per-frame tick count the rest of the port passes
  // around as kNominalFrameTicks.
  //
  // Returns false when the animation could not be walked -- no table, an
  // animation id past the count at PSC3 header +0x06, or a timeline offset of
  // zero. The original reports those through FUN_002681c0 and leaves the pose
  // where it was, which is what the caller should do too.
  // FUN_00225c90's sprite-strip half, entity +0x02 bit 0x200. Exposed for the
  // tests; FUN_00225c90_advance_animation routes to it on its own.
  bool FUN_00225c90_advance_sprite_strip(orphen::ported::entity::OriginalEntity &entity,
                                         const Psc3Model &model,
                                         std::uint32_t frameTicks);

  bool FUN_00225c90_advance_animation(orphen::ported::entity::OriginalEntity &entity,
                                      const Psc3Model &model,
                                      std::uint32_t frameTicks);

} // namespace orphen::ported::model
