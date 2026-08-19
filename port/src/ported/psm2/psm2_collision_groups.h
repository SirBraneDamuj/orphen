#pragma once

// The map's movable sub-objects.
//
//   src/FUN_0022b5a8.c:443-517  loads the groups and takes their rest pose
//   src/FUN_00260738.c          opcodes 0x7D / 0x7E write the transform
//   src/FUN_00208450.c          spends it, once per frame
//   src/FUN_00227840.c:86-102   the terrain scan's second loop reads the result
//
// A collision group is a block of primitives with a pivot and a transform. It
// is how a door works, and nothing else in the port moves map geometry: the
// group's vertices are the same DAT_0035569c records the renderer draws from,
// so moving them moves the drawn door and its collision together.
//
// The dirty byte at +0x5A is the whole state machine, and it is signed:
//
//   0     nothing to do
//   1/2   a translation / rotation channel was written; apply it
//   0xFF  applied last frame; clear and stop
//
// FUN_00208450 writes 0xFF after it applies, so a group settles one frame after
// the script stops driving it. But 0x7D's update is `status < 2 ? 2 : status|2`
// and 0xFF is negative, so a fresh write resets it to 2 rather than ORing into
// it -- which clears bit 7 and re-arms the pass. A door swings for exactly as
// long as something writes to it.

#include "ported/psm2/psm2_runtime.h"

#include <cstddef>

namespace orphen::ported::psm2
{

  // FUN_00208450. Runs every simulation frame, before anything queries the
  // ground: the script tick writes the channels and this spends them, which is
  // the order the original's frame function uses.
  //
  // Returns the number of groups it moved, which is DAT_003555D0 in the
  // original -- "something moved this frame".
  std::size_t FUN_00208450_update_collision_groups(Psm2RuntimeState &state);

  // FUN_00260738's two writes. `channel` is 0..2; the original diagnoses
  // anything higher and then indexes with it anyway, so the port clamps.
  void FUN_00260738_set_group_rotation(Psm2RuntimeState &state,
                                       std::size_t group,
                                       std::size_t channel,
                                       float radians);
  void FUN_00260738_set_group_translation(Psm2RuntimeState &state,
                                          std::size_t group,
                                          std::size_t channel,
                                          float distance);

} // namespace orphen::ported::psm2
