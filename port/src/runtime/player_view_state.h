#pragma once

#include "runtime/psm2_ground_query.h"

#include <cstdint>
#include <optional>

namespace orphen::port
{

  struct PlayerViewState
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::optional<Psm2GroundHit> groundHit;

    // Entity +0x60 (0 idle, 1 moving, 2 airborne) and +0xA0. FUN_00256bb8
    // shows +0xA0 is the animation id, not an abstract substate: the grounded
    // path writes 0x0E run, 0x0B walk, 0x01 stand, 0x17/0x6A idle fidget, so
    // the airborne 0x0C/0x0D/0x10 are jump-rise/jump-fall/land animations.
    std::uint16_t state = 0;
    std::uint16_t animationId = 0;
    std::uint16_t substateFrame = 0;
    std::uint32_t collisionFlags = 0;
    float verticalVelocity = 0.0f;
    bool grounded = false;
    bool running = false;
  };

} // namespace orphen::port
