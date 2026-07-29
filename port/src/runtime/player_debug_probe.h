#pragma once

#include "ported/psm2/psm2_runtime.h"
#include "runtime/psm2_ground_query.h"

#include <optional>

namespace orphen::port
{

  struct PlayerDebugProbeState
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::optional<Psm2GroundHit> groundHit;
  };

  class PlayerDebugProbe
  {
  public:
    void resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map);
    void update(float deltaSeconds, float moveX, float moveY, bool jumpRequested, const orphen::ported::psm2::Psm2RuntimeState *map);

    const PlayerDebugProbeState &state() const { return state_; }

  private:
    PlayerDebugProbeState state_;
    float verticalVelocity_ = 0.0f;
  };

} // namespace orphen::port
