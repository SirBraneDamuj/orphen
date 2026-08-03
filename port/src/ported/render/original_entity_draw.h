#pragma once

// Entity depth sorting, so models and map primitives interleave the way the
// original draws them.
//
//   src/FUN_0020c5a8.c  walks the 256 pool slots and builds the draw list
//   src/FUN_0020eec0.c  computes each entity's bucket and links it into the
//                       same 4096-entry table at DAT_7000000C that
//                       FUN_00209140 fills for the map
//
// The two passes share one bucket table, which is the point: an entity is not
// drawn "after the map", it is drawn at its depth among the map's primitives.
// FUN_0020eec0's key, from lines 181-205:
//
//     depth  = ctx+0x68 + ctx+0x140          view-space depth plus a bias
//     bucket = (int)(ctx+0x144 / depth + ctx+0x148) >> 4
//     clamped to [2, 0xFFF], or 0x1005 when the blend flag is set
//
// `ctx+0x144 / depth + ctx+0x148` is the same a/z + b projected depth the map
// pass runs through ViewProjection::screenDepth, so the port reuses that rather
// than keeping a second copy of the projection terms.
//
// Two details are deliberately not reproduced. The bias at ctx+0x140 is
// `(char)something * fGpffff80c4`, a per-entity sort nudge whose source byte
// FUN_0020c810 fills from a field the port does not model. And the blend flag
// lives in the render context at ctx+0x1F0, not on the entity, so nothing here
// can set it honestly -- no entity in s01_e024 is blended, and when one is it
// will sort as opaque until that context is ported.

#include "ported/render/original_view_projection.h"
#include "runtime/scene_object_view.h"

#include <cstddef>
#include <vector>

namespace orphen::ported::render
{

  namespace entityDraw
  {
    // FUN_0020eec0 lines 193-201. The map's own buckets run 1..0xFFF; entities
    // start at 2, so a map primitive in bucket 1 is always behind them.
    inline constexpr int kMinimumBucket = 2;
    inline constexpr int kMaximumBucket = 0xFFF;
    // Past the end of the shared table, so blended entities draw after every
    // opaque thing regardless of depth.
    inline constexpr int kBlendedBucket = 0x1005;
  } // namespace entityDraw

  struct EntityDrawItem
  {
    std::size_t viewIndex = 0;
    int depthBucket = 0;
  };

  // FUN_0020c5a8's walk reduced to what the port has: the view list is already
  // the set of live, drawable entities, so this is FUN_0020eec0's sorting half.
  // The result is ordered by bucket, far to near, matching the map draw list.
  std::vector<EntityDrawItem> FUN_0020eec0_buildEntityDrawList(
      const orphen::port::SceneObjectViewList &objects,
      const ViewProjection &viewProjection);

} // namespace orphen::ported::render
