#pragma once

#include "ported/entity/original_entity.h"
#include "ported/entity/original_entity_sound.h"
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
  // Entity +0x60 state 10, whose handler in PTR_FUN_0031e0e8 is `jr ra; nop`.
  // A cutscene parks the lead here (opcode 0x6D, or 0xA8 installing the
  // lead-bound script slot) and drives it from script; the controller must do
  // nothing at all while it is set.
  constexpr std::uint16_t kStateScriptDriven = 10;

  // FUN_00256bb8's attack branch, weapon class 0 -- which is what
  // FUN_002298d0 answers for type id 1, the lead player. Circle grounded puts
  // the entity in state 0x1C with animation 0x33, and PTR_FUN_0031e160[0]
  // (FUN_00256130) owns the frame from there until the animation ends.
  constexpr std::uint16_t kStateSwordAttack = 0x1c;

  constexpr std::uint16_t kAnimationStand = 0x01;
  constexpr std::uint16_t kAnimationWalk = 0x0b;
  constexpr std::uint16_t kAnimationRun = 0x0e;
  constexpr std::uint16_t kAnimationJumpRise = 0x0c;
  constexpr std::uint16_t kAnimationJumpFall = 0x0d;
  constexpr std::uint16_t kAnimationLand = 0x10;
  constexpr std::uint16_t kAnimationIdleFidget = 0x17;
  constexpr std::uint16_t kAnimationSwordAttack = 0x33;

  // The type id FUN_00256130 spawns for the blade, and the animations
  // FUN_002d21b8 drives it through: 1 is the swing, 2 the dissipate.
  constexpr std::int32_t kSwordEffectTypeId = 0x42;

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

    // The surface's stored slope, record78 +0x70 + subTriangle*4. FUN_00227840
    // stages it in the scan workspace's +0x54, FUN_00227390 copies it to +0x08
    // for whichever corner wins, and FUN_002262c0 tests it against entity +0x80.
    // A corner that found nothing reads pi/2.
    float slopeAngle = 1.570796012878418f;
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

  // The states FUN_00251ed8 dispatches through PTR_FUN_0031e0e8 that the
  // controller does not own itself. Installed rather than called directly
  // because the chest cutscene needs the pool, the camera and the fade, none
  // of which belong to a controller bound to one slot.
  //
  // Returns true when it handled the state, which is what tells the controller
  // to skip its own field branch this frame -- the original's table dispatch
  // is exclusive.
  using OriginalScriptedStateStep = std::function<bool(std::uint32_t frameTicks)>;

  // FUN_00256130's two pool-side operations. The controller owns pool slot 0
  // and nothing else, so the blade -- which needs the pool, the type
  // descriptors, the player's bone palette and the DAT_00343888 light table --
  // is reached the same way the chest cutscene is.
  struct OriginalSwordEffectHooks
  {
    // FUN_00265e28(0x42) and the setup block that follows it. Returns the pool
    // slot it landed in, or -1 when the pool is full -- which the original
    // treats as "return to idle", not as an error.
    std::function<std::int32_t()> spawn;
    // FUN_00225bc8(effect, 2): start the blade's dissipate animation. The
    // original guards this with `*effect == 0x42`, so a slot that has since
    // been recycled is ignored; the callback carries that test because it is
    // the side that can see the pool.
    std::function<void(std::int32_t slot)> retire;
  };


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
                const OriginalInteractionProbe &interactionProbe = {});

    OriginalPlayerSnapshot snapshot() const;

    // The lead player is entity pool slot 0. Bind the controller to that slot so
    // there is one copy of the entity rather than two, which is what makes
    // DAT_0058bed0 (slot 0's +0x20) mean what the camera and the script opcodes
    // think it means. Unbound, the controller falls back to its own storage so
    // it stays usable on its own.
    void bindEntity(orphen::ported::entity::OriginalEntity &slot) { entityStorage_ = &slot; }

    void setScriptedStateStep(OriginalScriptedStateStep step) { scriptedStateStep_ = std::move(step); }

    void setSwordEffectHooks(OriginalSwordEffectHooks hooks) { swordEffect_ = std::move(hooks); }

    // FUN_00267d38. The controller reaches the sound engine the same way an
    // actor behaviour does -- see ported/entity/original_entity_sound.h -- so
    // the footstep path here is the generic one, not a player-specific hook.
    void setSoundPlayer(orphen::ported::entity::EntitySoundPlayer play)
    {
      FUN_00267d38_playSound_ = std::move(play);
    }

  private:
    orphen::ported::entity::OriginalEntity ownedEntity_;
    orphen::ported::entity::OriginalEntity *entityStorage_ = &ownedEntity_;
    OriginalScriptedStateStep scriptedStateStep_;
    OriginalSwordEffectHooks swordEffect_;
    orphen::ported::entity::EntitySoundPlayer FUN_00267d38_playSound_;

    orphen::ported::entity::OriginalEntity &entity() { return *entityStorage_; }
    const orphen::ported::entity::OriginalEntity &entity() const { return *entityStorage_; }

    // cGpffffb6e1 == 0x1D. Only in that camera sub-mode does FUN_00256ab0 ease
    // facing through FUN_0023a320; every other path assigns it outright.
    bool input0x1dTurnSmoothing_ = false;

    // The D-record word FUN_00255d88 reads the material out of. The original
    // looks it up through the cached primitive index at entity +0x0A; the port
    // keeps the settled surface's own word in +0x6C, which is the same word.
    // Nullopt when the player is not standing on anything, which is the
    // original's FUN_00227798-failed path.
    std::optional<std::uint32_t> currentSurfaceTerrainFlags() const;

    void FUN_00225bf0_set_entity_state(std::uint16_t state, std::uint16_t substate);
    void FUN_00252d88_return_to_idle_state();
    void FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                  const OriginalPlayerFrameInput &input,
                                                  const OriginalInteractionProbe &interactionProbe);
    void FUN_002534d8_update_airborne_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    // PTR_FUN_0031e160[0], state 0x1C: the grounded sword swing.
    void FUN_00256130_update_sword_attack();
    // FUN_002560e8. Returns true when it ended the state.
    bool FUN_002560e8_end_on_animation_complete();
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
                                        const OriginalTerrainSampler &terrainSampler);
  };

} // namespace orphen::ported::player
