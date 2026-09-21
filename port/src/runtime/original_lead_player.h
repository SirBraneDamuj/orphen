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
    // The lead player is entity pool slot 0 in the original. Binding here means
    // the controller writes into that slot directly rather than into a private
    // copy, so script opcodes that address entity 0 see the real player.
    void bindEntity(orphen::ported::entity::OriginalEntity &slot) { controller_.bindEntity(slot); }

    // The player states FUN_00251ed8 dispatches that the controller does not
    // implement itself -- currently the chest cutscene, 0x0C..0x15.
    void setSoundPlayer(orphen::ported::entity::EntitySoundPlayer play)
    {
      controller_.setSoundPlayer(std::move(play));
    }

    void setScriptedStateStep(orphen::ported::player::OriginalScriptedStateStep step)
    {
      controller_.setScriptedStateStep(std::move(step));
    }

    // The blade of state 0x1C and the projectile of state 0x1D. See
    // OriginalActionEffectHooks.
    void setLightTable(orphen::ported::render::LightTable *lights)
    {
      controller_.setLightTable(lights);
    }

    void setDeathHook(std::function<void()> hook) { controller_.setDeathHook(std::move(hook)); }

    void setLandingDustHook(
        std::function<void(float x, float y, float z, float radius, bool lit)> hook)
    {
      controller_.setLandingDustHook(std::move(hook));
    }

    void setGameOverHooks(orphen::ported::player::GameOverHooks hooks)
    {
      controller_.setGameOverHooks(std::move(hooks));
    }

    void setCameraReleaseHook(std::function<void()> hook)
    {
      controller_.setCameraReleaseHook(std::move(hook));
    }

    // FUN_00216140's mailbox, for the harness damage keys.
    void FUN_00216140_stamp_hit(std::uint16_t damage,
                                std::uint8_t reaction,
                                std::uint16_t reactionFrames,
                                float fromDirection)
    {
      controller_.FUN_00216140_stamp_hit(damage, reaction, reactionFrames, fromDirection);
    }

    void setActionEffectHooks(orphen::ported::player::OriginalActionEffectHooks hooks)
    {
      controller_.setActionEffectHooks(std::move(hooks));
    }

    void resetToMap(const orphen::ported::psm2::Psm2RuntimeState &map,
                    const std::optional<orphen::ported::psm2::Vec3> &spawnOverride = std::nullopt);
    void update(std::uint32_t frameTicks,
                const orphen::ported::psm2::Vec3 &movementRequest,
                float stickMagnitude,
                // FUN_0023b890(8): the last eight frames of mapped actions
                // ORed together, held in the high half and newly-pressed in
                // the low half. This is one word rather than a jump flag
                // because FUN_00256bb8 reads three different bits out of it.
                std::uint32_t recentMappedActions,
                // FUN_0023B890(1): this frame's packed mapped word, held in the
                // high half and newly-pressed in the low half -- uGpffffb688 and
                // uGpffffb09c, which FUN_00251ED8 is handed directly. Distinct
                // from the eight-frame OR above, which would stretch a single
                // press across eight frames.
                std::uint32_t currentMappedActions,
                // cGpffffb66a, the debug byte. The moon jump is its only reader
                // on this path.
                bool debugActive,
                bool interactPressed,
                const orphen::ported::psm2::Psm2RuntimeState *map,
                const orphen::ported::player::OriginalInteractionProbe &interactionProbe = {});

    // FUN_002261E0's pass over slot 0, for the frames the battle module owns
    // the player: the field controller is replaced but the physics loop is not,
    // so +0x30/+0x34 still has to be spent and the ground still followed.
    void FUN_002261e0_step_physics(std::uint32_t frameTicks,
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
