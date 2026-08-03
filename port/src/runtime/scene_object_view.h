#pragma once

#include "ported/model/psc3_model.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <vector>

namespace orphen::port
{

  // A script-spawned entity, flattened for rendering. Built from the entity pool
  // each time the scene reloads, the same way PlayerViewState is built from the
  // lead player.
  //
  // Each draws as its PSC3 model when one resolved, and always as a labelled
  // debug box sized from the type descriptor's collision radius and height --
  // the box is deliberately kept, not a fallback.
  struct SceneObjectView
  {
    std::size_t slot = 0;
    std::int32_t typeId = 0;
    std::int32_t modelIndex = -1; // -1 when the descriptor was not resolvable
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    float radius = 0.0f;
    float height = 0.0f;
    float groundHeight = 0.0f;
    bool descriptorResolved = false;

    // Null when the type has no static descriptor (the map-streamed ids from
    // 0x272) or the grp record is not in any open bundle.
    const orphen::ported::model::Psc3Model *model = nullptr;
    // Slot in the texture cache, or kNoTextureSlot.
    int textureSlot = -1;
    // Entity +0xAC, the pose column every bone's track is indexed by.
    std::uint16_t poseColumn = 0;
    // Entity +0x14C.
    float scale = 1.0f;
  };

  using SceneObjectViewList = std::vector<SceneObjectView>;

} // namespace orphen::port
