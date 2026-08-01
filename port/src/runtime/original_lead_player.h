#pragma once

#include "ported/player/original_player_controller.h"
#include "ported/psm2/psm2_runtime.h"
#include "runtime/player_view_state.h"

#include <cstdint>
#include <optional>

namespace orphen::port
{

  class OriginalLeadPlayer
  {
  public:
    void resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map,
                    const std::optional<orphen::ported::psm2::Vec3> &spawnOverride = std::nullopt);
    void update(std::uint32_t frameTicks,
                const orphen::ported::psm2::Vec3 &movementRequest,
                float stickMagnitude,
                bool jumpRequested,
                const orphen::ported::psm2::Psm2RuntimeState *map);

    const PlayerViewState &viewState() const { return viewState_; }
    const orphen::ported::player::OriginalPlayerSnapshot &originalState() const { return originalState_; }

  private:
    orphen::ported::player::OriginalPlayerController controller_;
    orphen::ported::player::OriginalPlayerSnapshot originalState_;
    PlayerViewState viewState_;

    void refreshViewState(const orphen::ported::psm2::Psm2RuntimeState &map);
  };

} // namespace orphen::port
