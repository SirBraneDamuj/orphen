#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <vector>

namespace orphen::port
{

  // A script-spawned entity, flattened for rendering. Built from the entity pool
  // each time the scene reloads, the same way PlayerViewState is built from the
  // lead player.
  //
  // There are no models yet: these draw as labelled boxes sized from the type
  // descriptor's collision radius and height, or a default size when the
  // descriptor could not be resolved.
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
  };

  using SceneObjectViewList = std::vector<SceneObjectView>;

} // namespace orphen::port
