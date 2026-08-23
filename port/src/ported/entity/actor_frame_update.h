#pragma once

#include "ported/camera/original_field_camera.h"
#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"
#include "ported/entity/player_bandana.h"
#include "ported/model/psc3_skeleton.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace orphen::ported::entity
{

  // Native counterpart of src/FUN_00239ce0.c (0x00239ce0), the per-frame actor
  // update loop, and the two helpers it depends on. See
  // analyzed/actor_frame_dispatch.c.
  //
  // Only the behaviors that have been ported actually run; everything else is
  // counted by the ActorTrace and left alone. An unported behavior that silently
  // did *something* would be worse than one that does nothing, because the port
  // has no reference trace to catch it.

  // Everything a behavior is allowed to reach outside its own entity. Kept as
  // callbacks so this stays free of script and runtime dependencies, the same
  // way ScriptEnvironment does it.
  struct ActorEnvironment
  {
    EntityPool *entityPool = nullptr;
    const ActorDispatchTable *dispatchTable = nullptr;

    // FUN_00266368: read one bit of the event-flag bank at DAT_00342b70. The
    // bank lives in the script state, so it arrives as a callback rather than as
    // a dependency on the script namespace.
    std::function<bool(std::uint32_t flagId)> eventFlag;

    // Needed by behaviors that spawn: FUN_002cd210's clone loop allocates
    // through FUN_00265e28, which initialises from the type descriptor.
    const EntityDescriptorTable *descriptors = nullptr;

    // FUN_00216868: the engine RNG. Behaviors use it for repath timing and
    // attack rolls, so it is supplied rather than reached for -- a port that
    // called rand() directly would lose determinism.
    std::function<std::uint32_t()> random;

    // The floor under a world point. The shared non-player movement step uses
    // it both to keep an actor above the ground and to publish the terrain word
    // into +0x6C/+0x70, which is what a chase target is judged on.
    //
    // The band matters: FUN_00227070 stages the entity's +0x28 and
    // +0x28 + +0x58 into the scan workspace, and FUN_00227840 will not settle
    // on a surface above the head. Asked without it, a query on a map with
    // stacked floors answers whichever storey happens to be nearest sea level.
    struct TerrainSurface
    {
      float height = 0.0f;

      // FUN_00227070 writes two different flag words. +0x6C is the winning
      // sample's, +0x70 is the AND across all four corners -- they are not
      // interchangeable, and opcode 0x61 reads them as separate registers.
      std::uint32_t terrainFlags = 0;     // entity +0x6C
      std::uint32_t terrainFlagsAll = 0;  // entity +0x70

      // Which map primitive answered, packed as the original packs entity +0x0A:
      // `primitive | (half << 14)`. -1 when nothing was found.
      std::int32_t primitiveIndex = -1;

      // entity +0x84..+0x90, written only when the four-corner path ran.
      std::array<float, 4> cornerHeights{};
      bool sampledFourCorners = false;
    };

    // The arguments FUN_00227070 reads off the entity: the sample centre, the
    // feet, the body height (+0x58), the collision radius (+0x54), the flag
    // halfword (+0x04, whose bit 1 selects single-point sampling) and the reject
    // mask (+0x74).
    std::function<std::optional<TerrainSurface>(float x,
                                                float y,
                                                float feetHeight,
                                                float bodyHeight,
                                                float radius,
                                                std::uint16_t entityFlags04,
                                                std::uint32_t rejectTerrainMask)>
        terrainSurface;

    // iGpffffb650, the slot FUN_00239ce0 is currently ticking. Behaviors deeper
    // in the tree read it; the clone loop needs it to point a clone back at its
    // leader.
    std::size_t currentSlot = 0;

    // FUN_0025bf20, type 0x38. The behaviour is one call: run the scene script
    // body at blob offset +0x130 with this entity selected and in focus. It
    // arrives as a callback because the script interpreter lives a layer up --
    // the same reason everything else here does.
    std::function<void(std::size_t slot, std::int16_t bodyOffset)> FUN_0025bf20_run_npc_body;

    // FUN_0020dd78: the bone carrying a semantic role on an entity's model.
    // FUN_002d2f40 needs it three times to hang its rig together, and the
    // lookup reads the loaded PSC3, which the entity layer has no view of.
    std::function<std::size_t(std::size_t slot, std::uint8_t role)> FUN_0020dd78_bone_for_role;

    // DAT_004a7e00, indexed by pool slot. Behaviors that drive bones directly
    // rather than through the animation -- FUN_002cdb28 is the one this scene
    // exercises -- write their override here. Empty when the runtime has none.
    std::span<orphen::ported::model::EntityBoneOverrides> boneOverrides;

    // DAT_003555bc / iGpffffb64c, the per-frame tick count. Nominally 0x20.
    std::uint32_t frameTicks = 0x20;

    // Everything type 0x19 -- the player's bandana -- needs. Supplied by the
    // runtime because the rope reads a matrix palette and two frame counters,
    // none of which the entity carries. Null state means no bandana this scene.
    BandanaState *bandanaState = nullptr;
    std::function<BandanaEnvironment(std::size_t slot)> bandanaEnvironment;

    // The camera globals. Only one behaviour reaches them -- the treasure
    // chest, which swings the camera round itself while its lid opens -- but it
    // reaches them the way the original does, by reading cGpffffb6e1 and then
    // calling FUN_00217e18 / FUN_00217fe8 directly. Null in harnesses that have
    // no camera, which just skips the flourish.
    orphen::ported::camera::OriginalFieldCamera *camera = nullptr;

    // FUN_00267d38(cue, entity). Behaviours reach the sound engine through
    // small wrappers -- FUN_002d59e0 is the chest's -- so this is the shape
    // they all have: a cue number and the entity to place it at.
    std::function<void(std::uint16_t cue, const OriginalEntity &at)> FUN_00267d38_playSound;

    // DAT_003555b4, the global frame counter. Type 0x62's wing cue fires when
    // it divides by the entity's own period, so the sound is phase-locked to
    // the frame number rather than to anything the entity tracks.
    std::uint32_t DAT_003555b4_frameCounter = 0;
  };

  // FUN_0023a068: the freeze gate every behavior opens with. Advances the
  // countdown at +0xBD and the state timer at +0xA4, and returns true when the
  // caller should return early. The last frozen frame still runs, so a behavior
  // resumes on the frame the counter reaches zero.
  bool FUN_0023a068_freeze_gate(OriginalEntity &entity, std::uint32_t frameTicks);

  // FUN_00225bc8: the shared animation-state setter.
  void FUN_00225bc8_set_animation(OriginalEntity &entity, std::uint16_t animation);

  // FUN_00225bf0: the same, plus the movement state at +0x60. Script opcode 0xA8
  // uses it to put the lead player into the state that runs its object script.
  void FUN_00225bf0_set_state_and_animation(OriginalEntity &entity,
                                            std::uint16_t state,
                                            std::uint16_t animation);

  // FUN_0023a568: the fade path, taken instead of the type handler when +0x04
  // has bit 0x800. Fades in, then out, then releases the slot.
  void FUN_0023a568_fade(EntityPool &pool, std::size_t slot, std::uint32_t frameTicks);

  // FUN_002d1ea8: type 0x3A, the treasure chest. See
  // analyzed/actor_behaviors/type_0x3A_treasure_chest.c.
  void FUN_002d1ea8_treasure_chest(OriginalEntity &entity, const ActorEnvironment &environment);

  // True when the port has a body for the behavior at this PS2 address. Used by
  // the loop and by the report; kFUN_00239e78_noOp counts as implemented,
  // because it really is a no-op and listing it as missing would drown the
  // report in noise.
  // Type 0x62's tuning, read out of SLUS_200.11 rather than guessed. The clone
  // scale is confirmed by the EE dump: the leader's descriptor radius is 0.180
  // and its clones measure 0.126, which is exactly 0.7 of it.
  inline constexpr float kDAT_0035450c_enemyGravity = 0.00025f;
  inline constexpr float kDAT_00354510_cloneScale = 0.7f;
  inline constexpr float kDAT_00354514_turnRate = 0.00545415f;
  inline constexpr float kDAT_00354518_attackConeMin = -0.785398f; // -45 degrees
  inline constexpr float kDAT_0035451c_attackConeMax = 0.785398f;
  inline constexpr float kDAT_00354520_attackRangeMin = 0.6f;
  inline constexpr float kDAT_00354524_moveSpeed = 0.00125f;
  inline constexpr float kDAT_00354528_hoverHigh = 0.005f;
  inline constexpr float kDAT_0035452c_hoverDown = 0.004f;
  inline constexpr float kDAT_00354530_hoverLow = -0.005f;
  inline constexpr float kDAT_00354534_hoverUp = 0.004f;
  // DAT_003525f0 / DAT_003525f4: FUN_0023a320's dead zone, half a degree.
  inline constexpr float kAngleDeadZone = 0.00872664f;

  // Integrates +0x30/+0x34/+0x38 into position for a non-player actor. See the
  // definition: this is deliberately not the full FUN_002262c0.
  void integrateNonPlayerMovement(OriginalEntity &entity, const ActorEnvironment &environment);

  bool actorHandlerIsImplemented(std::uint32_t handlerAddress);

  // FUN_002d2f40, type 0x28: allocate the close-up rig (types 0x26, 0x27, 0x19)
  // and hang it together by bone role. Exposed because opcode 0x13F calls it
  // directly rather than waiting for the actor loop.
  void FUN_002d2f40_build_closeup_rig(OriginalEntity &entity,
                                      std::size_t slot,
                                      const ActorEnvironment &environment);

  // A readable name for a handler address, for the report. Returns nullptr for
  // addresses with no name yet.
  const char *actorHandlerName(std::uint32_t handlerAddress);

  // FUN_00239ce0: slots 2..255, three guards, then the type dispatch.
  void FUN_00239ce0_update_actors(const ActorEnvironment &environment, ActorTrace &trace);

} // namespace orphen::ported::entity
