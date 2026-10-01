#pragma once

// Climbing: the wall faces a character can hang on, and how they join up.
//
//   src/FUN_00257610.c   build DAT_00355020, once per map load (FUN_0022A418:328)
//   src/FUN_00257160.c   the face in front of the character, for FUN_00252DE0
//   src/FUN_002573D8.c   the neighbouring face in one of four directions
//
// **A climbable wall is a map primitive, not an entity.** Its D-record terrain
// word (0x78 record +0x04) carries a top nibble of 0xE or 0xF. 0xF is plain
// wall; 0xE is a face along the top edge, the one state 3 climbs over the lip
// from. Nothing is spawned for it and nothing marks it on screen.
//
// FUN_00257610 links every such face to the others it shares an edge with --
// exactly two coincident vertices -- and lays the result out as 5-halfword
// records `{ primitive, n0, n1, n2, n3 }` ended by a -1, from DAT_0035572C's
// bump arena. The climbing states walk that list rather than the map.

#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <vector>

namespace orphen::ported::entity
{

  struct ClimbFaceRecord
  {
    std::int16_t primitive = -1;
    std::array<std::int16_t, 4> neighbours{{-1, -1, -1, -1}};
  };

  // DAT_00355020, without its -1 terminator.
  using ClimbGraph = std::vector<ClimbFaceRecord>;

  // `0xDFFFFFFF < (word & 0xF0000000)`, FUN_00257610's test.
  inline bool isClimbableTerrain(std::uint32_t terrainFlags)
  {
    return (terrainFlags & 0xF0000000u) > 0xDFFFFFFFu;
  }

  // FUN_002537A0's climb-over test: `(word & 0xF0000000) == 0xE0000000`.
  inline bool isClimbLipTerrain(std::uint32_t terrainFlags)
  {
    return (terrainFlags & 0xF0000000u) == 0xE0000000u;
  }

  ClimbGraph FUN_00257610_build_climb_graph(const orphen::ported::psm2::Psm2RuntimeState &map);

  // The face the entity is standing at, or -1. Faces the entity must be looking
  // into (its facing turned by pi within +-60 degrees of the face normal), with
  // the middle of its body inside the face's height span and its centre within
  // 1.0 of the face's centre across the ground. The last face in list order
  // that passes wins.
  std::int16_t FUN_00257160_find_climb_face(const OriginalEntity &entity,
                                            const ClimbGraph &graph,
                                            const orphen::ported::psm2::Psm2RuntimeState &map);

  // `direction`: 0 left, 1 up, 2 right, 3 down, relative to the entity facing
  // the wall. -1 when `primitive` has no neighbour that way.
  std::int16_t FUN_002573d8_find_climb_neighbour(const OriginalEntity &entity,
                                                 const ClimbGraph &graph,
                                                 const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 std::int16_t primitive,
                                                 int direction);

} // namespace orphen::ported::entity
