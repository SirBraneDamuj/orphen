#include "ported/render/original_entity_draw.h"

#include <algorithm>

namespace orphen::ported::render
{

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

      // The entity's origin in view space. FUN_0020c810 stores this at
      // ctx+0x68 after running the world transform through VU0 -- and for an
      // entity attached to a parent bone it copies the *parent's* instead, which
      // is what `worldOrigin` carries. Sorting the bandana by its own +0x20 put
      // it at the world origin, thirteen units from where it draws.
      const Vec3 viewPosition = viewProjection.toViewSpace(object.worldOrigin);

      int bucket = 0;
      if (viewPosition.z <= 0.0f)
      {
        // FUN_0020eec0 line 184's near reject, which pushes the entity to the
        // far end rather than dropping it.
        bucket = entityDraw::kMaximumBucket;
      }
      else
      {
        const float key = viewProjection.screenDepth(viewPosition.z);
        bucket = key > 0.0f ? static_cast<int>(key) : 0;
        bucket >>= 4;
        bucket = std::clamp(bucket, entityDraw::kMinimumBucket, entityDraw::kMaximumBucket);
      }

      drawList.push_back({index, bucket});
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
