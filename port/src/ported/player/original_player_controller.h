#pragma once

#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <functional>
#include <limits>
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
    // The AND of all four footprint corners' terrain flags. Only the require
    // test (entity +0x78) reads it; it is deliberately not the same thing as
    // terrainFlags, which is the settled surface's own word.
    std::uint32_t commonFootprintFlags = 0;
  };

  // FUN_00227390's workspace +0x2C and +0x30: the actor's feet and the top of
  // its head. Terrain above the head is not ground, and a ceiling between the
  // two means there is no ground answer at all.
  struct OriginalTerrainBody
  {
    float feetHeight = 0.0f;
    float headHeight = 0.0f;
  };

  struct OriginalTerrainQuery
  {
    std::uint32_t rejectTerrainMask = 0;
    bool requireOriginalTerrainSample = true;
    std::optional<OriginalTerrainBody> body;
  };

  using OriginalTerrainSampler = std::function<std::optional<OriginalTerrainSample>(float originalX,
                                                                                    float originalZ,
                                                                                    float referenceY,
                                                                                    const OriginalTerrainQuery &query)>;

  // FUN_00252cc0. Returns true when the probe consumed the button press, which
  // makes FUN_00256bb8 return before locomotion -- so you cannot walk and
  // interact on the same frame. Supplied as a callback because the probe needs
  // the whole entity pool and the controller only owns slot 0.
  using OriginalInteractionProbe = std::function<bool()>;

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

    // uGpffffb68a is DAT_003555fa, the newly-pressed mapped button word
    // (gp 0x00359F70 - 0x4976). FUN_00256bb8 tests its 0x40 bit -- Cross, the
    // confirm button -- before running the interaction probe.
    bool interactPressed = false;

    // fGpffffb678. FUN_00256bb8 walks at or below 100.0 and runs above it;
    // FUN_00253488 scales air control by it directly. Full deflection is 128.
    float stickMagnitude = 0.0f;

    // Circle held (raw pad 0x0020). Not an input the original's field movement
    // reads; it gates the harness's debug mid-air jump, below.
    bool debugMidairJumpHeld = false;
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
    // Entity +0x58. The original reads it as DAT_0058bf08 -- pool slot 0's
    // collision height -- for the renderer's occlusion probe.
    float bodyHeight = 0.0f;
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
                const OriginalMovementBlocker &movementBlocker = {},
                const OriginalInteractionProbe &interactionProbe = {});

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
    void FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                  const OriginalPlayerFrameInput &input,
                                                  const OriginalInteractionProbe &interactionProbe);
    void FUN_002534d8_update_airborne_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00253468_finish_landing();
    void FUN_00253488_apply_airborne_control(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    void FUN_00256ab0_apply_movement_impulse(float movementStep,
                                             const orphen::ported::psm2::Vec3 &cameraRelativeMove);
    // bodyBaseHeight is the entity +0x28 the query should be posed from. It is
    // a parameter rather than a read of the entity because FUN_002262c0 raises
    // +0x28 before re-querying and hands the *raised* height to FUN_00227390.
    OriginalTerrainQuery terrainQueryForEntity(float bodyBaseHeight) const;
    std::optional<OriginalTerrainSample> FUN_00227390_validate_destination(float originalX,
                                                                           float originalZ,
                                                                           float bodyBaseHeight,
                                                                           const OriginalTerrainSampler &terrainSampler) const;
    void FUN_002262c0_integrate_physics(std::uint32_t frameTicks,
                                        const OriginalTerrainSampler &terrainSampler,
                                        const OriginalMovementBlocker &movementBlocker);
  };

} // namespace orphen::ported::player
