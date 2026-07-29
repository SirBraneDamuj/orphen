#pragma once

#include "ported/player/original_player_controller.h"
#include "ported/psm2/psm2_runtime.h"
#include "runtime/player_debug_probe.h"

namespace orphen::port
{

  class OriginalLeadPlayer
  {
  public:
    void resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map);
    void update(float deltaSeconds,
                const orphen::ported::psm2::Vec3 &cameraRelativeMove,
                bool jumpRequested,
                const orphen::ported::psm2::Psm2RuntimeState *map);

    const PlayerDebugProbeState &viewState() const { return viewState_; }
    const orphen::ported::player::OriginalPlayerSnapshot &originalState() const { return originalState_; }

  private:
    orphen::ported::player::OriginalPlayerController controller_;
    orphen::ported::player::OriginalPlayerSnapshot originalState_;
    PlayerDebugProbeState viewState_;

    void refreshViewState(const orphen::ported::psm2::Psm2RuntimeState &map);
  };

} // namespace orphen::port