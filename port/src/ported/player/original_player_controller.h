#pragma once

#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::player
{

  constexpr std::uint32_t kOriginalMappedActionJump = 0x80;
  constexpr std::uint32_t kOriginalMappedActionAttack = 0x20;
  constexpr std::uint32_t kOriginalMappedActionInteract = 0x10;

  // FUN_00256bb8 animation ids written by the grounded path, and the airborne
  // ids from FUN_002534d8. Entity +0xA0 is an animation id, not a substate.
  constexpr std::uint16_t kAnimationStand = 0x01;
  constexpr std::uint16_t kAnimationWalk = 0x0b;
  constexpr std::uint16_t kAnimationRun = 0x0e;
  constexpr std::uint16_t kAnimationJumpRise = 0x0c;
  constexpr std::uint16_t kAnimationJumpFall = 0x0d;
  constexpr std::uint16_t kAnimationLand = 0x10;
  constexpr std::uint16_t kAnimationIdleFidget = 0x17;

  struct OriginalTerrainSample
  {
    float height = 0.0f;
    std::uint32_t leadingWord = 0;
    std::uint32_t terrainFlags = 0;
    bool sampledByOriginalTerrain = false;
  };

  struct OriginalTerrainQuery
  {
    std::uint32_t rejectTerrainMask = 0;
    bool requireOriginalTerrainSample = true;
  };

  using OriginalTerrainSampler = std::function<std::optional<OriginalTerrainSample>(float originalX,
                                                                                    float originalZ,
                                                                                    float referenceY,
                                                                                    const OriginalTerrainQuery &query)>;

  using OriginalMovementBlocker = std::function<bool(float originalStartX,
                                                     float originalStartZ,
                                                     float originalEndX,
                                                     float originalEndZ,
                                                     float baseY,
                                                     float height,
                                                     float radius)>;

  struct OriginalPlayerFrameInput
  {
    orphen::ported::psm2::Vec3 cameraRelativeMove{};
    std::uint32_t mappedHeldActions = 0;
    std::uint32_t mappedPressedActions = 0;

    // fGpffffb678. FUN_00256bb8 walks at or below 100.0 and runs above it;
    // FUN_00253488 scales air control by it directly. Full deflection is 128.
    float stickMagnitude = 0.0f;
  };

  struct OriginalPlayerSnapshot
  {
    orphen::ported::psm2::Vec3 position{};
    float facingRadians = 0.0f;
    std::uint16_t state = 0;
    std::uint16_t animationId = 0;
    std::uint16_t substateFrame = 0;
    std::uint32_t collisionFlags = 0;
    float verticalVelocity = 0.0f;
    bool grounded = false;
    bool running = false;
  };

  class OriginalPlayerController
  {
  public:
    void resetAtOrigin(const OriginalTerrainSampler &terrainSampler);
    void resetAt(const orphen::ported::psm2::Vec3 &spawn, const OriginalTerrainSampler &terrainSampler);

    // frameTicks is DAT_003555bc: elapsed time in units of 0x20 per 60 Hz frame.
    // See ported/original_frame_timing.h.
    void update(std::uint32_t frameTicks,
                const OriginalPlayerFrameInput &input,
                const OriginalTerrainSampler &terrainSampler,
                const OriginalMovementBlocker &movementBlocker = {});

    OriginalPlayerSnapshot snapshot() const;

    // The lead player is entity pool slot 0. Bind the controller to that slot so
    // there is one copy of the entity rather than two, which is what makes
    // DAT_0058bed0 (slot 0's +0x20) mean what the camera and the script opcodes
    // think it means. Unbound, the controller falls back to its own storage so
    // it stays usable on its own.
    void bindEntity(orphen::ported::entity::OriginalEntity &slot) { entityStorage_ = &slot; }

  private:
    orphen::ported::entity::OriginalEntity ownedEntity_;
    orphen::ported::entity::OriginalEntity *entityStorage_ = &ownedEntity_;

    orphen::ported::entity::OriginalEntity &entity() { return *entityStorage_; }
    const orphen::ported::entity::OriginalEntity &entity() const { return *entityStorage_; }

    // cGpffffb6e1 == 0x1D. Only in that camera sub-mode does FUN_00256ab0 ease
    // facing through FUN_0023a320; every other path assigns it outright.
    bool input0x1dTurnSmoothing_ = false;

    void FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate);
    void FUN_00252d88_return_to_idle_state();
    void FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_002534d8_update_airborne_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00253468_finish_landing();
    void FUN_00253488_apply_airborne_control(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00256ab0_apply_movement_impulse(float movementStep,
                                             const orphen::ported::psm2::Vec3 &cameraRelativeMove);
    OriginalTerrainQuery terrainQueryForEntity() const;
    std::optional<OriginalTerrainSample> FUN_00227390_validate_destination(float originalX,
                                                                           float originalZ,
                                                                           const OriginalTerrainSampler &terrainSampler) const;
    void FUN_002262c0_integrate_physics(std::uint32_t frameTicks,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalMovementBlocker &movementBlocker);
  };

} // namespace orphen::ported::player
