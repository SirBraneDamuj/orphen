#include "ported/render/original_entity_draw.h"

#include <algorithm>

namespace orphen::ported::render
{

  int FUN_0020eec0_depthBucket(const Vec3 &worldOrigin,
                               const ViewProjection &viewProjection,
                               std::int8_t depthBias133)
  {
    // The entity's origin in view space. FUN_0020c810 stores this at ctx+0x68
    // after running the world transform through VU0 -- and for an entity
    // attached to a parent bone it copies the *parent's* instead, which is what
    // `worldOrigin` carries. Sorting the bandana by its own +0x20 put it at the
    // world origin, thirteen units from where it draws.
    const Vec3 viewPosition = viewProjection.toViewSpace(worldOrigin);
    // FUN_0020eec0:181. ctx+0x140 is FUN_0020c810:216's
    // `(char)(entity + 0x133) * fGpffff80c4`, and the sort is keyed on the sum,
    // not on the depth alone.
    const float depth = viewPosition.z + static_cast<float>(depthBias133) *
                                             entityDraw::kfGpffff80c4_depthBiasScale;
    if (depth < entityDraw::kfGpffff811c_minimumDepth)
    {
      // FUN_0020eec0 line 184's near reject: the key is pinned at 0x7FFFFFFF,
      // which survives the shift and clamps to the far end rather than dropping
      // the entity.
      return entityDraw::kMaximumBucket;
    }
    const float key = viewProjection.screenDepth(depth);
    int bucket = key > 0.0f ? static_cast<int>(key) : 0;
    bucket >>= 4;
    return std::clamp(bucket, entityDraw::kMinimumBucket, entityDraw::kMaximumBucket);
  }

  std::vector<EntityDrawItem> FUN_0020eec0_buildEntityDrawList(
      const orphen::port::SceneObjectViewList &objects,
      const ViewProjection &viewProjection)
  {
    std::vector<EntityDrawItem> drawList;
    drawList.reserve(objects.size());

    for (std::size_t index = 0; index < objects.size(); ++index)
    {
      const auto &object = objects[index];
      if (object.model == nullptr)
      {
        continue;
      }

      drawList.push_back(
          {index,
           FUN_0020eec0_depthBucket(object.worldOrigin, viewProjection, object.depthBias133)});
    }

    // Low bucket to high is far to near, the same order the map draw list is
    // emitted in, so the two can be merged by a single comparison.
    std::stable_sort(drawList.begin(), drawList.end(),
                     [](const EntityDrawItem &left, const EntityDrawItem &right) {
                       return left.depthBucket < right.depthBucket;
                     });
    return drawList;
  }

} // namespace orphen::ported::render
