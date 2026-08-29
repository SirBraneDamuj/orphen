#pragma once

// The follower's navigation mesh: the graph a party member walks when it cannot
// reach the lead in a straight line.
//
//   src/FUN_00257fc0.c   reset every record
//   src/FUN_00258080.c   probe one edge of one primitive for its neighbour
//   src/FUN_002582d0.c   the build -- a flood fill from the lead's spawn that
//                        discovers the reachable primitives and links them
//   src/FUN_002584b0.c   the per-request BFS: distances to a chosen target
//   src/FUN_00258c70.c   one step down the gradient, plus the corner cut
//   src/FUN_00258b80.c   the "pick a waypoint instead" fallback
//
// **This is a graph over map primitives, not a separate navmesh.** There is one
// 0x34-byte record per collision primitive (`DAT_00355038`, allocated in
// FUN_0022a418:324 as `DAT_00355688 * 0x34`), and adjacency is discovered at
// load time by standing just outside each edge and asking the ordinary ground
// query what is there. Two primitives are neighbours when that probe lands on
// the other one and the surface is walkable.
//
// The build runs from FUN_0022a418:374 with the lead's spawn point, and again
// from opcode 0xAB (FUN_00263148) whenever a script teleports the lead --
// which is how a scene that opens up new geometry gets it into the graph.

#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // What FUN_00227798 leaves behind: the height it found and `DAT_00354d4e`,
  // the primitive it settled on packed as `index | (half << 14)`.
  struct NavGroundProbe
  {
    float height = 128.0f;
    std::int16_t DAT_00354d4e_packedPrimitive = -1;
  };

  using NavProbeFn = std::function<NavGroundProbe(float x, float y, float z)>;

  // One entry of DAT_00355038. FUN_00257fc0 is the authority on the layout: it
  // walks the array as `undefined2 *` with a stride of 0x1a halfwords and seeds
  // every field below.
  struct NavRecord
  {
    std::int16_t ownIndex = -1;                  // +0x00
    std::int16_t packedPrimitive = -1;           // +0x02, -1 until the build reaches it
    std::array<std::int16_t, 4> neighbour{};     // +0x04..+0x0A, packed, -1 for none
    std::array<std::int16_t, 4> laneDepth{};     // +0x0C..+0x12, per follower lane
    std::array<std::int16_t, 16> edgeDepth{};    // +0x14..+0x32, [lane * 4 + edge]
  };

  class FollowerNavmesh
  {
  public:
    // FUN_00257fc0: every record back to "unreached, unlinked".
    void FUN_00257fc0_reset(std::size_t primitiveCount);

    // FUN_002582d0: flood the graph outward from the primitive under
    // (x, y, z), linking each primitive to whatever the edge probes find.
    void FUN_002582d0_build(const orphen::ported::psm2::Psm2RuntimeState &map,
                            const NavProbeFn &probe,
                            float x,
                            float y,
                            float z);

    // FUN_002584b0: breadth-first distances from the primitive under
    // (x, y, z) out across the graph, written into `lane`'s slice of every
    // record. Each follower owns a lane (entity +0x1C6) so two of them can
    // path to different places in the same frame.
    void FUN_002584b0_flood(const orphen::ported::psm2::Psm2RuntimeState &map,
                            const NavProbeFn &probe,
                            float x,
                            float y,
                            float z,
                            int lane);

    // FUN_00258c70: from the primitive under `position`, the next record to
    // walk toward, or nullptr when there is no way down the gradient. `self`
    // and the pool are what let it refuse a cell another actor is standing in;
    // pass nullptr for the plain geometric answer.
    //
    // `skipCornerCut` is DAT_00355030, which FUN_0025a500 raises around its own
    // call so the first step out of a stuck spot is the plain neighbour rather
    // than a shortcut past it.
    const NavRecord *FUN_00258c70_step(const orphen::ported::psm2::Psm2RuntimeState &map,
                                       const NavProbeFn &probe,
                                       const OriginalEntity *self,
                                       EntityPool *pool,
                                       float positionX,
                                       float positionY,
                                       float positionZ,
                                       int lane,
                                       bool skipCornerCut);

    bool built() const { return !DAT_00355038_records_.empty(); }
    std::size_t size() const { return DAT_00355038_records_.size(); }

    // How many records the build reached. Reported once per load, because a
    // number far below the primitive count means the graph does not cover the
    // room and every follower in it will fall back to the recovery state.
    std::size_t reachedCount() const;

    const NavRecord *recordFor(std::int32_t packedPrimitive) const;

  private:
    NavRecord *at(std::int32_t packedPrimitive);

    // FUN_00258080. Returns the packed primitive to enqueue, or -1; writes the
    // neighbour slot either way.
    std::int16_t FUN_00258080_probe_edge(const orphen::ported::psm2::Psm2RuntimeState &map,
                                         const NavProbeFn &probe,
                                         NavRecord &record,
                                         std::size_t primitive,
                                         int edge,
                                         std::int16_t &neighbourSlot);

    std::vector<NavRecord> DAT_00355038_records_;
  };

} // namespace orphen::ported::entity
