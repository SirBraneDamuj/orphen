#include "runtime/player_debug_probe.h"

#include <cmath>

namespace orphen::port
{
  namespace
  {

    constexpr float kProbeMoveSpeed = 6.875f;
    constexpr float kJumpVelocity = 8.0f;
    constexpr float kGravity = -24.0f;
    constexpr float kLandingTolerance = 0.05f;

    void snapToGround(PlayerDebugProbeState &state, const orphen::ported::psm2::Psm2RuntimeState &map)
    {
      state.groundHit = queryPsm2GroundAt(map, state.position.x, state.position.y, state.position.z);
      if (state.groundHit.has_value())
      {
        state.position.z = state.groundHit->height;
      }
    }

  } // namespace

  void PlayerDebugProbe::resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map)
  {
    state_ = {};
    verticalVelocity_ = 0.0f;
    snapToGround(state_, map);
  }

  void PlayerDebugProbe::update(float deltaSeconds, float moveX, float moveY, bool jumpRequested, const orphen::ported::psm2::Psm2RuntimeState *map)
  {
    if (map == nullptr)
    {
      state_ = {};
      verticalVelocity_ = 0.0f;
      return;
    }

    const float magnitude = std::sqrt(moveX * moveX + moveY * moveY);
    if (magnitude > 0.0f)
    {
      const float scale = kProbeMoveSpeed * deltaSeconds / std::max(magnitude, 1.0f);
      state_.position.x += moveX * scale;
      state_.position.y += moveY * scale;
      state_.facingRadians = std::atan2(moveY, moveX);
    }

    const bool wasGrounded = state_.groundHit.has_value();
    if (jumpRequested && wasGrounded)
    {
      verticalVelocity_ = kJumpVelocity;
      state_.groundHit.reset();
    }

    if (!state_.groundHit.has_value())
    {
      verticalVelocity_ += kGravity * deltaSeconds;
      state_.position.z += verticalVelocity_ * deltaSeconds;

      auto groundHit = queryPsm2GroundAt(*map, state_.position.x, state_.position.y, state_.position.z);
      if (verticalVelocity_ <= 0.0f && groundHit.has_value() && state_.position.z <= groundHit->height + kLandingTolerance)
      {
        state_.groundHit = std::move(groundHit);
        state_.position.z = state_.groundHit->height;
        verticalVelocity_ = 0.0f;
      }
      return;
    }

    verticalVelocity_ = 0.0f;
    snapToGround(state_, *map);
  }

} // namespace orphen::port
