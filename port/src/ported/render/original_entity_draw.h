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
// The bias at ctx+0x140 **is** reproduced: FUN_0020c810:216 fills it from
// entity +0x133 scaled by fGpffff80c4, and an effect that must draw over the
// character it surrounds is nothing but that byte. The shield barriers set
// -10, the summon veil -48 while pushing the caster +48 the other way, and the
// ground rings and markers -12. Without it a barrier sorts at the caster's own
// depth and loses the tie, which is why Orphen stood in front of his own
// shield instead of inside it.
//
// One detail is still not reproduced: the blend flag lives in the render
// context at ctx+0x1F0, not on the entity, so nothing here can set it
// honestly -- no entity in s01_e024 is blended, and when one is it will sort
// as opaque until that context is ported.

#include "ported/render/original_view_projection.h"
#include "runtime/scene_object_view.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orphen::ported::render
{

  namespace entityDraw
  {
    // FUN_0020eec0 lines 193-201. The map's own buckets run 1..0xFFF; entities
    // start at 2, so a map primitive in bucket 1 is always behind them.
    inline constexpr int kMinimumBucket = 2;
    inline constexpr int kMaximumBucket = 0xFFF;
    // FUN_0020EEC0:203, for an entity with +0x08 bit 0x40: past the end of the
    // depth table, among the 2D overlays. The world pass stops at
    // kMaximumBucket; MapViewer draws these between the 0x1004 and 0x1005
    // sprites.
    inline constexpr int kOverlayBucket = 0x1005;
    // fGpffff80c4, 0x00352034. Entity +0x133 is a signed byte of view-space
    // units at this scale, added to the depth the bucket is keyed on --
    // negative pulls the entity toward the camera. The sprite pass reads the
    // same 0.08 out of its own copy at DAT_003520a0.
    inline constexpr float kfGpffff80c4_depthBiasScale = 0.0799999982f;
    // fGpffff811c, 0x0035208c. FUN_0020eec0:182 rejects a biased depth below
    // this to the far end of the table rather than dropping the entity.
    inline constexpr float kfGpffff811c_minimumDepth = 0.100000001f;
  } // namespace entityDraw

  struct EntityDrawItem
  {
    std::size_t viewIndex = 0;
    int depthBucket = 0;
  };

  // FUN_0020eec0 lines 181-205 for one entity. Split out because
  // FUN_0020e840's motion trails need the same number: they submit into
  // `bucket + 1`, one step in front of the model they belong to.
  //
  // `depthBias133` is the entity's own +0x133, not yet scaled.
  int FUN_0020eec0_depthBucket(const Vec3 &worldOrigin,
                               const ViewProjection &viewProjection,
                               std::int8_t depthBias133 = 0);

  // FUN_0020c5a8's walk reduced to what the port has: the view list is already
  // the set of live, drawable entities, so this is FUN_0020eec0's sorting half.
  // The result is ordered by bucket, far to near, matching the map draw list.
  std::vector<EntityDrawItem> FUN_0020eec0_buildEntityDrawList(
      const orphen::port::SceneObjectViewList &objects,
      const ViewProjection &viewProjection);

} // namespace orphen::ported::render
