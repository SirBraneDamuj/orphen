#pragma once

#include "ported/camera/original_field_camera.h"
#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/follower_navmesh.h"
#include "ported/entity/original_entity.h"
#include "ported/entity/player_bandana.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/render/original_light_table.h"

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
      std::array<std::int32_t, 4> cornerPrimitives{{-1, -1, -1, -1}};
      bool sampledFourCorners = false;

      // The winning corner's stored slope, in radians. FUN_002262c0 gates the
      // whole upward-step branch on it against the entity's +0x80, which is what
      // stops a walker ratcheting up a wall.
      float slopeAngle = 1.570796012878418f;
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

    // FUN_00227390's corner fill, which is what the embedded-corner push-out
    // in FUN_002262c0 actually reads. Deliberately **not** gated on "found":
    // a corner over a hole stores the 128 sentinel, and `feet < 128` is what
    // sets that corner's mask bit. Routing the push-out through
    // `terrainSurface` instead loses exactly that case, because FUN_00227070
    // returns the max of the four corners -- so one no-ground corner makes the
    // whole sample read as no-ground and the mask collapses to 0.
    std::function<std::optional<TerrainSurface>(float x,
                                                float y,
                                                float feetHeight,
                                                float bodyHeight,
                                                float radius,
                                                std::uint16_t entityFlags04,
                                                std::uint32_t rejectTerrainMask)>
        FUN_00227390_corner_sample;

    // iGpffffb650, the slot FUN_00239ce0 is currently ticking. Behaviors deeper
    // in the tree read it; the clone loop needs it to point a clone back at its
    // leader.
    std::size_t currentSlot = 0;

    // FUN_0025bf20, type 0x38. The behaviour is one call: run the scene script
    // body at blob offset +0x130 with this entity selected and in focus. It
    // arrives as a callback because the script interpreter lives a layer up --
    // the same reason everything else here does.
    std::function<void(std::size_t slot, std::int16_t bodyOffset)> FUN_0025bf20_run_npc_body;

    // DAT_00343888, the sixteen dynamic light slots. FUN_002d21b8 drives one of
    // them from the sword blade's position every frame it lives; it is the only
    // actor behaviour that owns a light rather than reading one.
    orphen::ported::render::LightTable *DAT_00343888_lights = nullptr;

    // FUN_0020dc88(entity, bone, localOffset, out): a point in one of an
    // entity's own bones' space, in world space. It reads the matrix palette,
    // which the entity layer has no view of, so it arrives as a callback. Falls
    // back to the root of the attachment chain's own position when the slot has
    // no palette -- the original's "+0x0C has no 0x2000 bit" branch.
    std::function<orphen::ported::psm2::Vec3(std::size_t slot, std::size_t bone,
                                             const orphen::ported::psm2::Vec3 &localOffset)>
        FUN_0020dc88_bone_point;

    // FUN_0020dd78: the bone carrying a semantic role on an entity's model.
    // FUN_002d2f40 needs it three times to hang its rig together, and the
    // lookup reads the loaded PSC3, which the entity layer has no view of.
    std::function<std::size_t(std::size_t slot, std::uint8_t role)> FUN_0020dd78_bone_for_role;

    // DAT_004a7e00, indexed by pool slot. Behaviors that drive bones directly
    // rather than through the animation -- FUN_002cdb28 is the one this scene
    // exercises -- write their override here. Empty when the runtime has none.
    std::span<orphen::ported::model::EntityBoneOverrides> boneOverrides;

    // FUN_002d2470:0x002d2818's detonation burst, which fills the global
    // particle pool at DAT_00355620. The pool is one array shared by the whole
    // frame and lives above the entity layer, so it arrives as a callback the
    // way the light table would if it were not already a pointer.
    std::function<void(const OriginalEntity &source, std::size_t slot)>
        FUN_002d2470_spawn_impact_burst;

    // DAT_003555bc / iGpffffb64c, the per-frame tick count. Nominally 0x20.
    std::uint32_t frameTicks = 0x20;

    // DAT_003555d0. FUN_00208450 clears it at the top of every frame and raises
    // it for any collision group whose dirty byte is live -- i.e. "movable
    // collision moved this frame". FUN_002262c0:112 reads it, and it is the
    // only thing that lets a *stationary* actor resample the floor. See the
    // embedded-corner push-out in the .cpp.
    bool DAT_003555d0_collisionGroupMoved = false;

    // Diagnostics for the push-out above: how many times it has produced a
    // request. Non-owning, may be null. It is the only way to tell "the gate
    // never opened" from "the gate opened and the mask was empty".
    std::uint32_t *pushOutCounter = nullptr;

    // Diagnostics only: the frame this pass belongs to, so the `[push]` probe
    // can be lined up against a PCSX2 breakpoint log.
    std::uint32_t frameNumber = 0;


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

    // DAT_00343692, the seven party slots, each holding the pool index of the
    // entity bound to it. Type 0x37 reads it three times -- to pick its side of
    // the lead, to avoid walking through another follower, and to notice it is
    // standing inside one. Empty when the runtime has no script state.
    std::span<const std::uint16_t> DAT_00343692_partySlots;

    // DAT_003555e8, the analog stick's magnitude, 0..128. A follower close
    // enough to the lead matches its gait from this rather than from the lead's
    // measured speed: under 100 it walks, at or over it runs.
    float DAT_003555e8_stickMagnitude = 0.0f;

    // DAT_00355704 / DAT_00355708: the lead's own breadcrumb trail and the
    // cursor into it. FUN_00224060 appends the lead's position once per frame
    // whenever it has moved a quarter unit from the last entry, wrapping at 512.
    //
    // It is not map data -- it is where the lead has actually been -- which is
    // what makes it usable as a recovery path: a party follower wedged against
    // geometry walks the ring backwards for somewhere off camera the lead
    // reached, and teleports onto that primitive.
    struct LeadTrailPoint
    {
      float x = 0.0f;                // +0x00
      float z = 0.0f;                // +0x04
      float groundHeight = 0.0f;     // +0x08, the lead's +0x4C at the time
      std::int32_t primitive = -1;   // +0x0C, the lead's +0x0A, packed
    };
    std::span<const LeadTrailPoint> DAT_00355704_leadTrail;
    std::uint16_t DAT_00355708_leadTrailCursor = 0;

    // FUN_0023ae60: is this world point inside the camera's forward cone? The
    // follower's recovery refuses to teleport anywhere the player can see.
    std::function<bool(float x, float z)> FUN_0023ae60_on_camera_axis;

    // One map primitive by its packed index (the form entity +0x0A carries):
    // DAT_003556AC's centre at +0x60 and DAT_003556B0's terrain word at +0x04.
    struct MapPrimitive
    {
      float centerX = 0.0f;
      float centerZ = 0.0f;
      float centerY = 0.0f;
      std::uint32_t terrainFlags = 0;
    };
    std::function<std::optional<MapPrimitive>(std::int32_t packedPrimitive)> mapPrimitive;

    // The follower's navigation graph, and the two things it needs to run:
    // FUN_00227798 (the single-point ground query, which is what discovers
    // adjacency and locates an actor in the graph) and the loaded map.
    //
    // DAT_00355030 lives here too. FUN_0025a500 raises it around its own
    // FUN_00259378 call so the step out of a stuck spot is the plain
    // neighbour rather than a corner cut past it, and FUN_00258c70 clears it
    // on the way through -- so it is one flag shared by the whole actor pass,
    // not per entity.
    FollowerNavmesh *followerNavmesh = nullptr;
    const orphen::ported::psm2::Psm2RuntimeState *psm2Map = nullptr;
    NavProbeFn FUN_00227798_probe;
    bool *DAT_00355030_skipCornerCut = nullptr;

    // FUN_0020da68: one bone's pose out of an animation, in FUN_0020d8c0's
    // field order (rotation xyz, translation xyz, scale). The look-at reads the
    // rest pose before twisting it, so it needs the model the entity layer has
    // no view of. Empty when the entity has no model loaded.
    std::function<std::optional<std::array<float, orphen::ported::model::kPoseFieldCount>>(
        std::size_t slot, std::size_t bone, std::uint16_t animation)>
        FUN_0020da68_sample_bone_pose;

    // FUN_0020d9d8's third field: how much yaw a bone's *filtered* pose is
    // currently carrying. The look-at reads it to decide whether there is a
    // twist left to unwind. Zero when the runtime has no filter for the slot.
    std::function<float(std::size_t slot, std::size_t bone)> FUN_0020d9d8_bone_yaw;
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

  // FUN_0023a320: one capped step of `from` toward `to`, zero once inside the
  // dead zone. Every turning behaviour goes through it.
  float FUN_0023a320_approach_angle(float from, float to, float maxStep);

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

  // FUN_00256130's spawn block, type 0x42: the glowing sword blade, attached to
  // the swinging entity's role-5 bone and carrying a dynamic light. Exposed
  // because the player controller -- which owns pool slot 0 and nothing else --
  // is what triggers it. Returns the pool slot, or -1 when the pool is full.
  std::int32_t FUN_00256130_spawn_sword_effect(const OriginalEntity &owner,
                                               std::size_t ownerSlot,
                                               const ActorEnvironment &environment);

  // FUN_002d2e00's spawn block, type 0x44: the homing magic projectile, placed
  // at a world point the caller has already resolved off the caster's role-4
  // bone. Exposed for the same reason the sword blade's is -- the player
  // controller owns pool slot 0 and nothing else. Returns the pool slot, or -1
  // when the pool is full or the floor is above the hand.
  std::int32_t FUN_002d2e00_spawn_magic_projectile(const OriginalEntity &owner,
                                                   const orphen::ported::psm2::Vec3 &handPoint,
                                                   const ActorEnvironment &environment);

  // A readable name for a handler address, for the report. Returns nullptr for
  // addresses with no name yet.
  const char *actorHandlerName(std::uint32_t handlerAddress);

  // FUN_00239ce0: slots 2..255, three guards, then the type dispatch.
  void FUN_00239ce0_update_actors(const ActorEnvironment &environment, ActorTrace &trace);

  // FUN_002261e0, the physics walk. Must run **after** FUN_00208450 and in the
  // same frame -- see the comment on the definition.
  // Diagnostics: --push-probe logs every embedded-corner push-out.
  extern bool gPushProbe;

  void FUN_002261e0_update_physics(const ActorEnvironment &environment);

} // namespace orphen::ported::entity
