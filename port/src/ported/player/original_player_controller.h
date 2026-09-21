#pragma once

#include "ported/entity/original_entity.h"
#include "ported/entity/original_entity_sound.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/player/original_game_over.h"
#include "ported/render/original_light_table.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace orphen::ported::player
{

  constexpr std::uint32_t kOriginalMappedActionJump = 0x80;
  constexpr std::uint32_t kOriginalMappedActionAttack = 0x20;
  // Mapped action 0x10 is Triangle. FUN_00256bb8 calls this branch the *use*
  // path; it is not the interact button, which is Cross (0x40) and is tested
  // out of uGpffffb68a a step earlier.
  constexpr std::uint32_t kOriginalMappedActionUse = 0x10;

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

  // FUN_00256bb8's *use* branch, mapped action 0x10 -- Triangle. Weapon class 0
  // again, so for the lead player it is state 0x1D with animation 0x14:
  // PTR_FUN_0031e160[1], FUN_002562b0, the homing magic projectile.
  constexpr std::uint16_t kStateMagicCast = 0x1d;

  constexpr std::uint16_t kAnimationStand = 0x01;
  constexpr std::uint16_t kAnimationWalk = 0x0b;
  constexpr std::uint16_t kAnimationRun = 0x0e;
  constexpr std::uint16_t kAnimationJumpRise = 0x0c;
  constexpr std::uint16_t kAnimationJumpFall = 0x0d;
  constexpr std::uint16_t kAnimationLand = 0x10;
  constexpr std::uint16_t kAnimationIdleFidget = 0x17;
  constexpr std::uint16_t kAnimationSwordAttack = 0x33;
  constexpr std::uint16_t kAnimationMagicCast = 0x14;

  // FUN_00251ED8:97-225, the hit reaction. `FUN_00225bf0(entity, state,
  // animation)` -- three reactions and a death, picked by the hit record's
  // +0xBC and by where the player was standing.
  //
  //   0x16 / 0x1F  the ordinary stagger, FUN_002554D8
  //   0x17 / 0x22  the flatten, FUN_002555A8 -- +0xBC == 0x13
  //   0x18 / 0x20  the knockback, FUN_002555D8 -- +0xBC == 0x12, airborne,
  //                **or dead**: the death branch ends in this one too
  //   0x19 / 0x0D  the terrain hazard, FUN_002557A0 -- lava or a pit, not death
  constexpr std::uint16_t kStateHitStagger = 0x16;
  constexpr std::uint16_t kStateHitFlatten = 0x17;
  constexpr std::uint16_t kStateHitKnockback = 0x18;
  // PTR_FUN_0031E0E8[0x19], FUN_002557A0. Written only by FUN_00251ED8:60-71,
  // the `+0x6C & 0x1000000` terrain test -- the player stepped into something
  // that kills on contact. The handler sinks the body and hands it to
  // FUN_00255E40, the respawn. Nothing in the port writes it yet, because the
  // terrain entry is not ported; the handler is kept so that when it is, the
  // state it lands in already works.
  constexpr std::uint16_t kStateTerrainHazard = 0x19;
  // PTR_FUN_0031E0E8[0x1A] and [0x1B]: the game over. See original_game_over.h.
  constexpr std::uint16_t kStateGameOverEnter = 0x1A;
  constexpr std::uint16_t kStateGameOverHold = 0x1B;
  constexpr std::uint16_t kAnimationHitStagger = 0x1f;
  constexpr std::uint16_t kAnimationHitKnockback = 0x20;
  constexpr std::uint16_t kAnimationKnockdown = 0x21;
  constexpr std::uint16_t kAnimationHitFlatten = 0x22;
  constexpr std::uint16_t kAnimationGetUp = 0x23;
  // State 0x19's animation, not the death's -- the death plays 0x20.
  constexpr std::uint16_t kAnimationTerrainHazard = 0x0d;

  // FUN_00216140's +0xBC values FUN_00251ED8 branches on.
  constexpr std::uint8_t kHitReactionKnockback = 0x12;
  constexpr std::uint8_t kHitReactionFlatten = 0x13;

  // The type id FUN_00256130 spawns for the blade, and the animations
  // FUN_002d21b8 drives it through: 1 is the swing, 2 the dissipate.
  constexpr std::int32_t kSwordEffectTypeId = 0x42;

  // The type FUN_002d2e00 spawns for the magic projectile.
  constexpr std::int32_t kMagicProjectileTypeId = 0x44;

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

  // The pool-side operations of the two action states, 0x1C and 0x1D. The
  // controller owns pool slot 0 and nothing else, so everything that needs the
  // pool, the type descriptors, the player's bone palette or the DAT_00343888
  // light table is reached the same way the chest cutscene is.
  struct OriginalActionEffectHooks
  {
    // -- state 0x1C, FUN_00256130 ----------------------------------------
    // FUN_00265e28(0x42) and the setup block that follows it. Returns the pool
    // slot it landed in, or -1 when the pool is full -- which the original
    // treats as "return to idle", not as an error.
    std::function<std::int32_t()> spawnSwordBlade;
    // FUN_00225bc8(effect, 2): start the blade's dissipate animation. The
    // original guards this with `*effect == 0x42`, so a slot that has since
    // been recycled is ignored; the callback carries that test because it is
    // the side that can see the pool.
    std::function<void(std::int32_t slot)> retireSwordBlade;

    // -- state 0x1D, FUN_002562b0 ----------------------------------------
    // FUN_002d2e00 at the caster's role-4 bone. -1 when the pool is full *or*
    // when the floor is above the hand, and the original treats both the same
    // way: drop the cast and return to idle.
    std::function<std::int32_t()> spawnMagicProjectile;
    // The launch. Sets the projectile's +0x60 to 1 and clears +0x04 bit 0x100,
    // which is what hands it to the physics. False when the slot no longer
    // holds the projectile, which is the original's `+0x198 == 0` case and also
    // ends the cast.
    std::function<bool(std::int32_t slot)> launchMagicProjectile;
    // Every frame the projectile is still charging, FUN_002562b0 writes the
    // hand point straight into its +0x20. That -- not a parent link -- is how
    // it stays in the caster's palm.
    std::function<void(std::int32_t slot)> holdMagicProjectileAtHand;
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

    // uGpffffb688 / uGpffffb09c -- DAT_003555F8 and DAT_003555FA, **this
    // frame's** mapped held and newly-pressed words, not the eight-frame OR
    // above. FUN_00251ED8 is handed these two directly and FUN_002534D8's moon
    // jump reads the second; an eight-frame window there would hold the boost
    // on for eight frames per tap instead of one.
    std::uint32_t uGpffffb688_heldThisFrame = 0;
    std::uint32_t uGpffffb09c_pressedThisFrame = 0;

    // cGpffffb66a, DAT_003555DA -- the debug-active byte. It is the only gate on
    // the moon jump, and the port holds it on the way updateOriginalDebugOverlay
    // does.
    bool cGpffffb66a_debugActive = false;
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

    // FUN_002261E0's body for slot 0, without FUN_00251ED8 in front of it.
    //
    // The original runs the state machine and the physics from two different
    // places: FUN_00251ED8 (or FUN_00249610 in battle) decides what the player
    // is doing, and FUN_002261E0 then walks **all 0x100 pool slots** -- slot 0
    // included -- handing each to FUN_002262C0. So the lead's physics is not
    // part of the field controller and does not stop when battle replaces it.
    //
    // `update()` above runs both halves because outside battle they are always
    // wanted together. This is the second half on its own, for the frames where
    // something else is driving the player: the battle module's own controller,
    // which still needs +0x30/+0x34 spent, the ground followed and the entity
    // settled.
    void FUN_002261e0_step_physics(std::uint32_t frameTicks,
                                   const OriginalTerrainSampler &terrainSampler);

    OriginalPlayerSnapshot snapshot() const;

    // The lead player is entity pool slot 0. Bind the controller to that slot so
    // there is one copy of the entity rather than two, which is what makes
    // DAT_0058bed0 (slot 0's +0x20) mean what the camera and the script opcodes
    // think it means. Unbound, the controller falls back to its own storage so
    // it stays usable on its own.
    void bindEntity(orphen::ported::entity::OriginalEntity &slot) { entityStorage_ = &slot; }

    void setScriptedStateStep(OriginalScriptedStateStep step) { scriptedStateStep_ = std::move(step); }

    void setActionEffectHooks(OriginalActionEffectHooks hooks) { actionEffect_ = std::move(hooks); }

    // FUN_00267d38. The controller reaches the sound engine the same way an
    // actor behaviour does -- see ported/entity/original_entity_sound.h -- so
    // the footstep path here is the generic one, not a player-specific hook.
    void setSoundPlayer(orphen::ported::entity::EntitySoundPlayer play)
    {
      FUN_00267d38_playSound_ = std::move(play);
    }

    // DAT_00343888. FUN_00251ED8's hit reaction lights the player red for
    // fifteen frames out of the same sixteen-slot table the script opcodes
    // allocate from, so the controller needs a view of it. Nothing consumes the
    // table's falloff yet -- see original_light_table.h -- so this writes state
    // rather than pixels.
    void setLightTable(orphen::ported::render::LightTable *lights) { DAT_00343888_lights_ = lights; }

    // FUN_00265EC0(0x58CD70) plus the death sting and the two music ramps --
    // everything FUN_00251ED8's death branch does outside slot 0.
    void setDeathHook(std::function<void()> hook) { onDeath_ = std::move(hook); }
    // FUN_00255E40, the respawn. Left uninstalled means the body stays down.
    void setDeathRespawnHook(std::function<void()> hook) { onDeathRespawn_ = std::move(hook); }
    // Everything states 0x1A and 0x1B reach outside slot 0. Left uninstalled,
    // the two states still run -- the player keeps its own fields -- but the
    // room, the lights and the camera stay as they were.
    void setGameOverHooks(GameOverHooks hooks) { gameOver_ = std::move(hooks); }
    // FUN_002262C0:577-601, the landing dust. The pool lives outside the
    // controller, so the touchdown test stays here and the ring goes out
    // through this. `lit` is the original's `kind != 0`, which picks 0xFFFFFF
    // over 0.
    void setLandingDustHook(std::function<void(float x, float y, float z, float radius, bool lit)> hook)
    {
      FUN_00219af0_landingDust_ = std::move(hook);
    }
    // FUN_00217E18(0), the manual camera release FUN_002536A8 ends with.
    void setCameraReleaseHook(std::function<void()> hook)
    {
      FUN_00217e18_releaseCamera_ = std::move(hook);
    }

    // FUN_00216140's mailbox, for a caller that lands a hit on the lead
    // outside the hit test: +0xBE is the damage, +0xBC the reaction, +0xC0 the
    // reaction's length in frames and +0xC4 the direction it came from.
    // `FUN_00251ed8_apply_pending_damage` spends all four on its next frame.
    void FUN_00216140_stamp_hit(std::uint16_t damage,
                                std::uint8_t reaction,
                                std::uint16_t reactionFrames,
                                float fromDirection);

  private:
    orphen::ported::entity::OriginalEntity ownedEntity_;
    orphen::ported::entity::OriginalEntity *entityStorage_ = &ownedEntity_;
    OriginalScriptedStateStep scriptedStateStep_;
    OriginalActionEffectHooks actionEffect_;
    orphen::ported::entity::EntitySoundPlayer FUN_00267d38_playSound_;
    orphen::ported::render::LightTable *DAT_00343888_lights_ = nullptr;
    // bGpffffbd58 / uGpffffbd5a: the light slot the last hit took, -1 for none,
    // and the frames it has left. FUN_00251ED8 owns both.
    std::int32_t bGpffffbd58_hitLightSlot_ = -1;
    std::int32_t uGpffffbd5a_hitLightFrames_ = 0;
    // uGpffffbd54. FUN_00251ED8:21-23 sets it from the attack button held, and
    // clears it outright unless the debug byte is up. FUN_002534D8 is its only
    // reader -- this is the arm half of the moon jump.
    std::uint32_t uGpffffbd54_moonJumpArmed_ = 0;
    std::function<void()> onDeath_;
    std::function<void()> onDeathRespawn_;
    GameOverHooks gameOver_;
    std::function<void()> FUN_00217e18_releaseCamera_;
    std::function<void(float x, float y, float z, float radius, bool lit)> FUN_00219af0_landingDust_;

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
    // FUN_00251ED8:97-227, the +0xBE block and the +0xC0 countdown under it.
    void FUN_00251ed8_apply_pending_damage(std::uint32_t frameTicks);
    // FUN_002536A8: the shove a reaction taken in states 3..6 starts with.
    void FUN_002536a8_break_out_of_state();
    // PTR_FUN_0031E0E8[0x16] / [0x17] / [0x18] / [0x19].
    void FUN_002554d8_update_hit_stagger(std::uint32_t frameTicks);
    void FUN_002555a8_update_hit_flatten();
    void FUN_002555d8_update_hit_knockback(std::uint32_t frameTicks);
    void FUN_002557a0_update_terrain_hazard(std::uint32_t frameTicks);
    // FUN_00251C80 + FUN_00267D38, the three hurt cues.
    void playCharacterCue(int soundIndex);
    void FUN_00252d88_return_to_idle_state();
    void FUN_00256bb8_update_grounded_field_state(std::uint32_t frameTicks,
                                                  const OriginalPlayerFrameInput &input,
                                                  const OriginalInteractionProbe &interactionProbe);
    void FUN_002534d8_update_airborne_state(std::uint32_t frameTicks, const OriginalPlayerFrameInput &input);
    // PTR_FUN_0031e160[0], state 0x1C: the grounded sword swing.
    void FUN_00256130_update_sword_attack();
    // PTR_FUN_0031e160[1], state 0x1D: the magic cast.
    void FUN_002562b0_update_magic_cast();
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
