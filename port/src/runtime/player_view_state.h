#pragma once

#include "runtime/psm2_ground_query.h"

#include <optional>

namespace orphen::port
{

  struct PlayerViewState
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::optional<Psm2GroundHit> groundHit;
  };

} // namespace orphen::port
