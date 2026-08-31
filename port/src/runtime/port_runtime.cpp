#include "runtime/port_runtime.h"

#include <cstdio>

#include "harness/flat_bin_archive.h"

#include "runtime/psm2_ground_query.h"
#include "ported/entity/entity_collision.h"
#include "ported/entity/entity_path_follow.h"
#include "ported/model/psc3_model.h"
#include "ported/psm2/psm2_collision_groups.h"
#include "ported/psm2/psm2_uv_animation.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/model/entity_animation.h"
#include "ported/script/object_registers.h"
#include "ported/player/original_interaction.h"

#include <cmath>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>
#include <iostream>
#include <map>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;
    constexpr float kRadiansToDegrees = 57.2957795f;
    // FUN_0022a360:22 seeds DAT_0032538c to 0x42000000. The scene block and
    // opcode 0xB8 both override it; a scene carrying neither runs at this.
    constexpr float kDAT_0032538c_defaultDrawDistance = 32.0f;

    std::string formatNumber(float value, int precision = 2)
    {
      std::ostringstream stream;
      stream << std::fixed << std::setprecision(precision) << value;
      return stream.str();
    }

    // FUN_00256bb8 / FUN_002534d8 animation ids written into entity +0xA0.
    const char *animationName(std::uint16_t animationId)
    {
      switch (animationId)
      {
      case orphen::ported::player::kAnimationStand: return "STAND";
      case orphen::ported::player::kAnimationWalk: return "WALK";
      case orphen::ported::player::kAnimationRun: return "RUN";
      case orphen::ported::player::kAnimationJumpRise: return "RISE";
      case orphen::ported::player::kAnimationJumpFall: return "FALL";
      case orphen::ported::player::kAnimationLand: return "LAND";
      case orphen::ported::player::kAnimationIdleFidget: return "FIDGET";
      default: return "OTHER";
      }
    }

    // Entity +0x60.
    const char *stateName(std::uint16_t state)
    {
      switch (state)
      {
      case 0: return "IDLE";
      case 1: return "MOVING";
      case 2: return "AIR";
      // The chest cutscene, in the order it runs them.
      case 0x0C: return "CHEST-FADEOUT";
      case 0x0D: return "CHEST-PLACE";
      case 0x0E: return "CHEST-FADEIN";
      case 0x0F: return "CHEST-OPEN";
      case 0x10: return "CHEST-ITEM";
      case 0x11: return "CHEST-ITEMFADE";
      case 0x12: return "CHEST-WHITEOUT";
      case 0x13: return "CHEST-RESTORE";
      case 0x14: return "CHEST-LAND";
      case 0x15: return "CHEST-WHITEIN";
      default: return "OTHER";
      }
    }

    const char *cameraModeName(orphen::ported::camera::FieldCameraMode mode)
    {
      using Mode = orphen::ported::camera::FieldCameraMode;
      switch (mode)
      {
      case Mode::None: return "AUTO";
      case Mode::RotateNegative: return "R1";
      case Mode::RotatePositive: return "L1";
      case Mode::FaceTarget: return "FACE";
      case Mode::DecayFast: return "DECAY";
      case Mode::FixedStep: return "FIXED";
      case Mode::EaseToGoal: return "EASE";
      default: return "?";
      }
    }

    const char *actorHandlerSourceName(orphen::ported::entity::ActorHandlerSource source)
    {
      using Source = orphen::ported::entity::ActorHandlerSource;
      switch (source)
      {
      case Source::Streamed: return "streamed";
      case Source::Shared: return "shared";
      case Source::Secondary: return "secondary";
      case Source::Tertiary: return "tertiary";
      case Source::Primary: return "primary";
      case Source::None:
      default: return "none";
      }
    }

  } // namespace

  void PortRuntime::initialize(const PortRuntimeConfig &config)
  {
    // +0x168 lives inside the 0x1D8-byte slot in the original, so every clear of
    // an entity clears it. The port stores it beside the pool rather than in it;
    // handing the pool the table restores that shared lifetime.
    entityPool_.setBoneOverrideTable(DAT_004a7e00_boneOverrides_.data(),
                                     DAT_004a7e00_boneOverrides_.size());

    // Bind before reset: the lead player is pool slot 0, so the controller must
    // already be writing there when resetToMap places it.
    leadPlayer_.bindEntity(entityPool_.leadPlayer());
    leadPlayer_.setScriptedStateStep(
        [this](std::uint32_t frameTicks) { return stepScriptedPlayerState(frameTicks); });
    // The player reaches the sound engine through the same callback an actor
    // behaviour does, so FUN_00256ff8's footsteps are not a special case.
    leadPlayer_.setSoundPlayer(
        [this](std::uint16_t cue, const orphen::ported::entity::OriginalEntity &at)
        { soundEngine_.FUN_00267d38_play_at(cue, at.positionX20, at.positionZ24, at.positionY28); });

    // FUN_00256130's two pool-side operations. They live here rather than in
    // the controller because the blade needs the entity pool, the descriptor
    // table, the player's bone palette and the light table, and the controller
    // is bound to slot 0 alone.
    orphen::ported::player::OriginalActionEffectHooks actionHooks;
    actionHooks.spawnSwordBlade = [this]() -> std::int32_t
    {
      // The spawn reads no timing, only the pool, the descriptors, the bone
      // roles and the light table, so the nominal tick is enough here.
      return orphen::ported::entity::FUN_00256130_spawn_sword_effect(
          entityPool_.leadPlayer(), 0, actorEnvironment(orphen::ported::kNominalFrameTicks));
    };
    actionHooks.retireSwordBlade = [this](std::int32_t slot)
    {
      // The original's guard is `*effect == 0x42` on the raw pointer at +0x198:
      // a slot that has been recycled since is left alone.
      if (slot < 0 || static_cast<std::size_t>(slot) >= entityPool_.slotCount())
      {
        return;
      }
      auto &effect = entityPool_.slot(static_cast<std::size_t>(slot));
      if (effect.typeId00 != orphen::ported::player::kSwordEffectTypeId)
      {
        return;
      }
      // FUN_00225bc8(effect, 2): the dissipate animation.
      effect.animationA0 = 2;
      effect.stateResetA4 = 999;
      effect.previousSubstateA2 = 0xffff;
      effect.flags06 = static_cast<std::uint16_t>(effect.flags06 & 0xff38);
      effect.timelineCursorA8 = 0;
    };

    // FUN_002562b0's three. The hand point is the lead's role-4 bone -- the
    // right hand -- plus DAT_0031e0a8, and all three share it.
    const auto castHandPoint = [this]() -> orphen::ported::psm2::Vec3
    {
      // DAT_0031e0a8, read out of the EE dump.
      constexpr orphen::ported::psm2::Vec3 kDAT_0031e0a8{0.0709999949f, -0.00999999978f,
                                                         -0.105999976f};
      const auto environment = actorEnvironment(orphen::ported::kNominalFrameTicks);
      const std::size_t bone =
          environment.FUN_0020dd78_bone_for_role ? environment.FUN_0020dd78_bone_for_role(0, 4) : 0;
      return environment.FUN_0020dc88_bone_point
                 ? environment.FUN_0020dc88_bone_point(0, bone, kDAT_0031e0a8)
                 : orphen::ported::psm2::Vec3{};
    };

    actionHooks.spawnMagicProjectile = [this, castHandPoint]() -> std::int32_t
    {
      return orphen::ported::entity::FUN_002d2e00_spawn_magic_projectile(
          entityPool_.leadPlayer(), castHandPoint(),
          actorEnvironment(orphen::ported::kNominalFrameTicks));
    };

    actionHooks.launchMagicProjectile = [this](std::int32_t slot) -> bool
    {
      if (slot < 0 || static_cast<std::size_t>(slot) >= entityPool_.slotCount())
      {
        return false;
      }
      auto &projectile = entityPool_.slot(static_cast<std::size_t>(slot));
      if (projectile.typeId00 != orphen::ported::player::kMagicProjectileTypeId)
      {
        return false;
      }
      projectile.state60 = 1;
      // +0x04 bit 0x100 off: the physics pass owns it from here.
      projectile.halfword04 = static_cast<std::uint16_t>(projectile.halfword04 & 0xfeffu);
      return true;
    };

    actionHooks.holdMagicProjectileAtHand = [this, castHandPoint](std::int32_t slot)
    {
      if (slot < 0 || static_cast<std::size_t>(slot) >= entityPool_.slotCount())
      {
        return;
      }
      auto &projectile = entityPool_.slot(static_cast<std::size_t>(slot));
      if (projectile.typeId00 != orphen::ported::player::kMagicProjectileTypeId ||
          projectile.state60 != 0)
      {
        return;
      }
      const orphen::ported::psm2::Vec3 hand = castHandPoint();
      projectile.positionX20 = hand.x;
      projectile.positionZ24 = hand.y;
      projectile.positionY28 = hand.z;
    };

    leadPlayer_.setActionEffectHooks(std::move(actionHooks));

    reset();
    spawnOverride_ = config.spawnOverride;
    placedSlots_ = config.placedSlots;
    if (spawnOverride_.has_value())
    {
      spawnSourceLabel_ = "--spawn";
    }
    runScriptTick_ = config.runScriptTick;
    printActorReport_ = config.printActorReport;
    printSoundReport_ = config.printSoundReport;
    printRenderReport_ = config.printRenderReport;
    printGleamReport_ = config.printGleamReport;
    mapViewer_.mutableSceneLighting().applyLightFloor = config.applyLightFloor;
    mapViewer_.mutableSceneLighting().applyUnlitFlag = config.applyUnlitFlag;
    suppressPointLights_ = config.suppressPointLights;
    poseReportSlot_ = config.poseReportSlot;
    scrDumpPath_ = config.scrDumpPath;
    mapViewer_.setMapBlendDisabled(config.suppressMapBlend);
    mapViewer_.setScreenSmearDisabled(config.suppressScreenSmear);
    mapViewer_.setMapBaseSlotOnly(config.mapBaseSlotOnly);
    mapViewer_.setEntityBoundTextureOnly(config.entityBoundTextureOnly);
    if (printGleamReport_)
    {
      mapViewer_.setGleamProbeSink(&gleamProbes_);
    }
    printFrameStats_ = config.printFrameStats;
    if (printFrameStats_)
    {
      mapViewer_.setRenderStatsSink(&frameStats_);
    }
    if (config.drawDistanceOverride.has_value())
    {
      mapViewer_.setDrawDistance(*config.drawDistanceOverride);
      drawDistanceOverridden_ = true;
    }
    printScriptReport_ = config.printScriptReport;
    printModelReport_ = config.printModelReport;
    hideSlots_ = config.hideSlots;
    snapshotFrame_ = config.snapshotFrame;
    armStreamPending_ = config.hasArmStream;
    armStreamOffset_ = config.armStreamOffset;
    armStreamFrame_ = config.armStreamFrame;
    if (config.hasScrTraceRange)
    {
      scriptTrace_.setTraceRange(config.scrTraceRangeLow, config.scrTraceRangeHigh);
    }
    loadExecutable(config);

    if (!config.decodedPsm2Path.empty())
    {
      mapViewer_.loadDecodedPsm2(config.decodedPsm2Path);
    }
    else if (!config.discRoot.empty())
    {
      mapViewer_.loadDiscSceneMap(config.discRoot, config.discScene);
    }

    discRoot_ = config.discRoot;
    loadMapPropDescriptors();

    // FUN_00228e28:119. Optional, like the executable: without it the item
    // caption falls back to naming the id.
    if (!discRoot_.empty() && !itemDatabase_.load(discRoot_))
    {
      std::cout << "[items] SCR.BIN resource 1 not readable; item names unavailable\n";
    }

    // FUN_00228e28:145, then FUN_002294d0's record loop. The original runs that
    // loop on new game, keyed by DAT_0031c1f0; the port has no new-game path, so
    // it fills the table once here. Opcode 0xAC hands the record to
    // FUN_0023a518, which is where a follower's radius and height come from.
    if (!discRoot_.empty())
    {
      characterStats_.load(discRoot_);
      // FUN_00228e28:155, the blob loaded right after it. Nothing warns when it
      // is missing: an attack whose record cannot be read deals the floor of
      // one point, which is what the original's `return 0` leaves it doing.
      DAT_00354d6c_hitParameters_.load(discRoot_);
    }
    sceneScript_.state().FUN_002294d0_load_party_records(characterStats_);
    if (!sceneScript_.state().partyRecordsLoaded)
    {
      std::cout << "[party] SCR.BIN resource 0xBF not readable; followers keep their"
                   " descriptor size\n";
    }

    loadSoundData();
    loadVoiceIndex(config);

    if (mapViewer_.loadedMap() != nullptr)
    {
      loadSceneForCurrentMap();
      if (config.printScriptReport)
      {
        printScriptReport();
      }
      if (config.printSceneTree)
      {
        mapViewer_.printLoadedSceneTree(std::cout);
      }
      if (config.printModelReport)
      {
        printModelReport();
      }
      const auto &stats = mapViewer_.loadedMap()->stats;
      std::cout << "[psm2] loaded " << mapViewer_.loadedSourceDescription()
                << " positions=" << stats.positionRecordCount
                << " sectionB=" << stats.sectionBRecordCount
                << " primitives=" << stats.primitiveRecordCount
                << " triangles=" << stats.triangleCount
                << " skipped=" << stats.skippedPrimitiveCount
                << " textures=" << mapViewer_.loadedTexturePageCount() << '\n';
      // The collision broadphase, so a map that silently failed to supply one
      // is visible rather than quietly falling back to the unordered scan.
      std::cout << "[psm2] collision cells=" << stats.occupiedCollisionCells
                << "/" << orphen::ported::psm2::kCollisionGridCells
                << " cellList=" << stats.collisionCellListLength
                << " descriptors=" << mapViewer_.loadedMap()->DAT_003556d8_collisionDescriptors.size()
                << " groups=" << mapViewer_.loadedMap()->DAT_003556e0_collisionGroups.size() << '\n';
      if (orphen::ported::psm2::gGroupProbe)
      {
        std::size_t index = 0;
        for (const auto &group : mapViewer_.loadedMap()->DAT_003556e0_collisionGroups)
        {
          std::cout << "[group] " << index++ << " type=" << group.type
                    << " firstVertex=" << group.firstVertex
                    << " vertexCount=" << group.vertexCount
                    << " rest=" << group.restVertices.size()
                    << " firstPrim=" << group.firstPrimitive
                    << " primCount=" << group.primitiveCount << '\n';
        }
      }

      // Section G. Worth printing because a map that silently parsed no tracks
      // looks exactly like a map with no animated textures.
      {
        const auto &tracks = mapViewer_.loadedMap()->DAT_003556f4_uvAnimation;
        if (!tracks.empty())
        {
          std::cout << "[psm2] uv animation tracks=" << tracks.size();
          for (std::size_t index = 0; index < tracks.size(); ++index)
          {
            const auto &track = tracks[index];
            std::cout << "  #" << index << ":";
            if (track.frames.size() == 1 && track.frames[0].duration < 0)
            {
              std::cout << "scroll(" << track.frames[0].u << "," << track.frames[0].v << ")";
            }
            else
            {
              std::cout << track.frames.size() << "f";
            }
          }
          std::cout << '\n';
        }
      }

      if (!config.dumpMapTexturesPath.empty())
      {
        mapViewer_.dumpTexturePages(config.dumpMapTexturesPath);
      }

      if (config.probeCentre.has_value())
      {
        printPrimitiveProbe(*config.probeCentre, config.probeRadius);
      }

      const auto &leadState = leadPlayer_.viewState();
      std::cout << "[player] spawn=(" << leadState.position.x << ", " << leadState.position.y
                << ", " << leadState.position.z << ")"
                << " (" << spawnSourceLabel_ << ")"
                << " grounded=" << (leadState.grounded ? 1 : 0) << '\n';
    }
  }

  void PortRuntime::loadExecutable(const PortRuntimeConfig &config)
  {
    // The entity descriptors the spawn path needs are static tables inside the
    // retail executable, not disc resources. This is optional: without it the
    // port still runs, it just cannot report a spawned object's collision size.
    std::filesystem::path path = config.executablePath;
    if (path.empty() && !config.discRoot.empty())
    {
      const std::filesystem::path candidate = config.discRoot / "SLUS_200.11";
      if (std::filesystem::exists(candidate))
      {
        path = candidate;
      }
    }

    if (path.empty())
    {
      std::cout << "[elf] no executable given; entity descriptors unavailable\n";
      return;
    }

    executable_ = orphen::ported::resource::ElfDataReader::tryOpen(path.string());
    if (!executable_.has_value())
    {
      std::cout << "[elf] could not read " << path.string()
                << "; entity descriptors unavailable\n";
      return;
    }

    descriptorTable_ = orphen::ported::entity::EntityDescriptorTable(&executable_.value());
    actorDispatchTable_ = orphen::ported::entity::ActorDispatchTable(&executable_.value());
    std::cout << "[elf] loaded " << path.string() << " for static tables\n";
  }

  // FUN_002148a8's world. The two callbacks are here rather than in the entity
  // layer because both read state the entity layer has no view of: the loaded
  // models, and the persistent matrix palette bank at DAT_00357E00.
  //
  // The palette is **last frame's**, and that is correct: FUN_0020cdc0's
  // attached branches read the same bank, which the original fills during the
  // draw, so a blade riding a hand is swept from where that hand was when it
  // was last drawn.
  orphen::ported::entity::HitTestEnvironment
  PortRuntime::hitTestEnvironment(std::uint32_t frameTicks)
  {
    orphen::ported::entity::HitTestEnvironment environment;
    environment.entityPool = &entityPool_;
    environment.frameTicks = frameTicks;
    environment.DAT_00354d68_stats = &characterStats_;
    environment.DAT_003151c8_hitList = &DAT_003151c8_hitList_;

    environment.modelForSlot = [this](std::size_t slot) -> const orphen::ported::model::Psc3Model *
    {
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        return nullptr;
      }
      const EntityModelBinding *binding =
          modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
      return binding == nullptr ? nullptr : binding->model;
    };

    environment.FUN_002206a8_spawn_hit_sparks =
        [this](const orphen::ported::entity::OriginalEntity &victim, std::int16_t sourceSide)
    {
      DAT_00355b74_hitSparks_.FUN_002206a8_spawn(victim, sourceSide,
                                                 [this] { return FUN_00216868_random(); });
    };

    environment.FUN_0020cdc0_entity_matrix =
        [this](std::size_t slot) -> std::optional<orphen::ported::model::Matrix4>
    {
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        return std::nullopt;
      }
      const auto &entity = entityPool_.slot(slot);
      const orphen::ported::psm2::Vec3 position{entity.positionX20, entity.positionZ24,
                                                entity.positionY28};
      if (entity.parentSlot192 >= 0)
      {
        const std::size_t parentSlot = static_cast<std::size_t>(entity.parentSlot192);
        if (parentSlot >= DAT_00357e00_bonePalettes_.size() ||
            DAT_00357e00_bonePalettes_[parentSlot].empty())
        {
          return std::nullopt;
        }
        const auto &parent = entityPool_.slot(parentSlot);
        if (entity.attachBone194 < 0)
        {
          return orphen::ported::model::FUN_0020cdc0_attached_root(
              DAT_00357e00_bonePalettes_[parentSlot],
              static_cast<std::size_t>(-static_cast<int>(entity.attachBone194)), position,
              {parent.positionX20, parent.positionZ24,
               parent.positionY28 + parent.height58 * 0.5f},
              entity.facingRadians5c, entity.rotationX154, entity.rotationY158, entity.scale14c,
              entity.scaleZ150);
        }
        return orphen::ported::model::FUN_0020cdc0_rigid_attached_root(
            DAT_00357e00_bonePalettes_[parentSlot],
            static_cast<std::size_t>(entity.attachBone194), position, entity.facingRadians5c,
            entity.rotationX154, entity.rotationY158, entity.scale14c, entity.scaleZ150);
      }
      return orphen::ported::model::FUN_0020cdc0_entity_root(
          position, entity.facingRadians5c, entity.rotationX154, entity.rotationY158,
          entity.scale14c, entity.scaleZ150);
    };

    return environment;
  }

  orphen::ported::entity::ActorEnvironment PortRuntime::actorEnvironment(std::uint32_t frameTicks)
  {
    orphen::ported::entity::ActorEnvironment environment;
    environment.entityPool = &entityPool_;
    environment.DAT_00354d6c_hitParameters = &DAT_00354d6c_hitParameters_;
    hitTestEnvironment_ = hitTestEnvironment(frameTicks);
    environment.hitTest = &hitTestEnvironment_;
    environment.dispatchTable = &actorDispatchTable_;
    environment.frameTicks = frameTicks;
    environment.DAT_003555d0_collisionGroupMoved = DAT_003555d0_collisionGroupMoved_;
    environment.pushOutCounter = &pushOutCount_;
    environment.frameNumber = frameCount_;
    environment.boneOverrides = DAT_004a7e00_boneOverrides_;
    // FUN_00266368 reads the flag bank that lives in the script state, so the
    // actor tick borrows it rather than owning a second copy.
    environment.eventFlag = [this](std::uint32_t flagId)
    { return sceneScript_.state().FUN_00266368_eventFlag(flagId); };
    environment.descriptors = &descriptorTable_;
    environment.camera = &fieldCamera_;
    environment.DAT_003555b4_frameCounter = DAT_003555b4_frameCounter_;
    environment.DAT_003555e8_stickMagnitude = DAT_003555e8_stickMagnitude_;
    environment.DAT_00343692_partySlots = sceneScript_.state().DAT_00343692_partySlots;
    environment.DAT_00355704_leadTrail = DAT_00355704_leadTrail_;
    environment.DAT_00355708_leadTrailCursor = DAT_00355708_leadTrailCursor_;

    // FUN_0023ae60: the point's bearing from pool slot 1 against the camera's
    // yaw (uGpffffb6d4), inside the cone at DAT_00352600/604 -- +/- 40 degrees.
    environment.FUN_0023ae60_on_camera_axis = [this](float x, float z) {
      // 0x00352600 / 0x00352604: -pi/4 and +pi/4. The port had +-0.6981316
      // (40 degrees) here, which is a narrower cone, so a follower counted as
      // off camera sooner and took state 6's teleport more readily than the
      // original allows.
      constexpr float kfGpffff8690_coneLow = -0.785398006f;
      constexpr float kfGpffff8694_coneHigh = 0.785398006f;
      const auto &reference = entityPool_.slot(1);
      const float bearing = std::atan2(z - reference.positionZ24, x - reference.positionX20);
      const float delta =
          orphen::ported::model::FUN_002166e8_angle_delta(fieldCamera_.yawRadians(), bearing);
      return delta > kfGpffff8690_coneLow && delta < kfGpffff8694_coneHigh;
    };

    // FUN_00227798 and the graph it feeds. The probe is the single-point form
    // of the ground query -- entity flags 2, no body band, no reject mask --
    // and it hands back DAT_00354d4e as well as the height, because the graph
    // is built out of *which primitive answered*, not out of how high it was.
    environment.followerNavmesh = &followerNavmesh_;
    environment.psm2Map = mapViewer_.loadedMap();
    environment.DAT_00355030_skipCornerCut = &DAT_00355030_skipCornerCut_;
    if (const auto *loadedMap = mapViewer_.loadedMap(); loadedMap != nullptr)
    {
      environment.FUN_00227798_probe =
          [loadedMap](float x, float y, float z) -> orphen::ported::entity::NavGroundProbe
      {
        const auto sample = FUN_00227070_sample_ground(*loadedMap, x, y, z, 0.0f, 0.0f, 2u, 0u);
        orphen::ported::entity::NavGroundProbe probe;
        probe.height = sample.height;
        probe.DAT_00354d4e_packedPrimitive =
            static_cast<std::int16_t>(sample.found ? sample.packedPrimitive : -1);
        return probe;
      };
    }

    environment.mapPrimitive = [this](std::int32_t packedPrimitive)
        -> std::optional<orphen::ported::entity::ActorEnvironment::MapPrimitive>
    {
      const auto *loadedMap = mapViewer_.loadedMap();
      if (loadedMap == nullptr || packedPrimitive < 0)
      {
        return std::nullopt;
      }
      const std::size_t index = static_cast<std::size_t>(packedPrimitive) & 0x3FFFu;
      if (index >= loadedMap->DAT_003556ac_dRecords80.size() ||
          index >= loadedMap->DAT_003556b0_dRecords78.size())
      {
        return std::nullopt;
      }
      orphen::ported::entity::ActorEnvironment::MapPrimitive primitive;
      primitive.centerX = loadedMap->DAT_003556ac_dRecords80[index].center.x;
      primitive.centerZ = loadedMap->DAT_003556ac_dRecords80[index].center.y;
      primitive.centerY = loadedMap->DAT_003556ac_dRecords80[index].center.z;
      primitive.terrainFlags = loadedMap->DAT_003556b0_dRecords78[index].terrainFlags;
      return primitive;
    };

    // FUN_0020da68: the entity layer has no model store, so the sampler comes
    // in the same way the role lookup above does. `firstPoseColumnForAnimation`
    // is FUN_0020da68's own `*(short *)(param_4 * 4 + animationTable[anim])`
    // with param_4 == 0 -- the animation's first key column.
    environment.FUN_0020da68_sample_bone_pose =
        [this](std::size_t slot, std::size_t bone, std::uint16_t animation)
        -> std::optional<std::array<float, orphen::ported::model::kPoseFieldCount>>
    {
      if (slot >= entityPool_.slotCount())
      {
        return std::nullopt;
      }
      const EntityModelBinding *binding =
          modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
      if (binding == nullptr || binding->model == nullptr)
      {
        return std::nullopt;
      }
      const auto &model = *binding->model;
      const std::uint16_t column =
          orphen::ported::model::firstPoseColumnForAnimation(model, model.blob, animation);
      const orphen::ported::model::BonePose pose =
          orphen::ported::model::FUN_0020d378_sample_bone(model, model.blob, bone, column);
      // FUN_0020d8c0's field order: rotation xyz, translation xyz, scale.
      return std::array<float, orphen::ported::model::kPoseFieldCount>{
          pose.rotationRadians.x, pose.rotationRadians.y, pose.rotationRadians.z,
          pose.translation.x,     pose.translation.y,     pose.translation.z,
          pose.scale};
    };

    environment.FUN_0020d9d8_bone_yaw = [this](std::size_t slot, std::size_t bone) -> float
    {
      if (slot >= DAT_003ffe00_poseFilters_.size())
      {
        return 0.0f;
      }
      return orphen::ported::model::FUN_0020d9d8_read_bone_pose(DAT_003ffe00_poseFilters_[slot],
                                                                bone)[2];
    };

    // DAT_00343888. The blade of type 0x42 owns a slot for as long as it
    // lives, so the actor loop needs write access to the same table the script
    // opcodes and the renderer read.
    environment.DAT_00343888_lights = &sceneScript_.state().DAT_00343888_lights;

    // FUN_0020dc88(entity, 0, DAT_003266f8, out): 0.65 up the entity's own bone
    // 0, in world space. The palette is last frame's -- the pose pass runs
    // after the actor loop, exactly as FUN_0020c810 does after FUN_00239ce0 --
    // and on the first frame there is none at all, which is the case the
    // original answers by walking +0x192 to the root and using its position.
    environment.FUN_0020dc88_bone_point =
        [this](std::size_t slot, std::size_t bone,
               const orphen::ported::psm2::Vec3 &localOffset) -> orphen::ported::psm2::Vec3
    {
      if (slot >= entityPool_.slotCount())
      {
        return {};
      }
      std::size_t root = slot;
      while (entityPool_.slot(root).parentSlot192 >= 0)
      {
        root = static_cast<std::size_t>(entityPool_.slot(root).parentSlot192);
      }
      const auto &rootEntity = entityPool_.slot(root);
      const orphen::ported::psm2::Vec3 fallback{
          rootEntity.positionX20, rootEntity.positionZ24,
          rootEntity.positionY28 + rootEntity.height58 * 0.5f};
      if (slot >= DAT_00357e00_bonePalettes_.size())
      {
        return fallback;
      }
      return orphen::ported::model::FUN_0020dc88_bone_point(DAT_00357e00_bonePalettes_[slot], bone,
                                                            localOffset, fallback);
    };

    // The same lookup the script environment gets. FUN_002d2f40 hangs its rig
    // together by role, so type 0x28 needs it inside the actor loop.
    environment.FUN_0020dd78_bone_for_role = [this](std::size_t slot, std::uint8_t role) -> std::size_t
    {
      if (slot >= entityPool_.slotCount())
      {
        return 0;
      }
      const EntityModelBinding *binding =
          modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
      if (binding == nullptr || binding->model == nullptr)
      {
        return 0;
      }
      return orphen::ported::model::FUN_0020dd78_bone_for_role(*binding->model, role);
    };

    // FUN_0025bf20, type 0x38. Re-entering the interpreter from inside the actor
    // loop is safe because the scene tick has already finished by then --
    // FUN_00239ce0 runs between FUN_0025b778 and FUN_0025b918, exactly as here.
    environment.FUN_0025bf20_run_npc_body =
        [this, frameTicks](std::size_t slot, std::int16_t bodyOffset)
    {
      sceneScript_.FUN_0025bf20_run_npc_body(bodyOffset, scriptEnvironment(frameTicks),
                                             scriptTrace_, slot);
    };
    // FUN_002d2470's detonation burst. The pool is the runtime's, so the
    // behaviour reaches it the same way it reaches the light table.
    environment.FUN_002d2470_spawn_impact_burst =
        [this](const orphen::ported::entity::OriginalEntity &source, std::size_t slot)
    {
      DAT_00355620_particles_.FUN_002d2470_spawn_impact_burst(
          source, slot, [this] { return FUN_00216868_random(); });
    };
    environment.FUN_00267d38_playSound =
        [this](std::uint16_t cue, const orphen::ported::entity::OriginalEntity &at)
    { soundEngine_.FUN_00267d38_play_at(cue, at.positionX20, at.positionZ24, at.positionY28); };

    // FUN_00216868. A plain LCG rather than the original's generator, which has
    // not been analysed; what matters here is that it is seeded once and stepped
    // deterministically, so --frames stays reproducible.
    environment.random = [this]() -> std::uint32_t
    {
      actorRandomState_ = actorRandomState_ * 1103515245u + 12345u;
      return (actorRandomState_ >> 16) & 0x7FFFu;
    };

    if (const auto *loadedMap = mapViewer_.loadedMap(); loadedMap != nullptr)
    {
      environment.terrainSurface =
          [loadedMap](float x, float y, float feetHeight, float bodyHeight, float radius,
                      std::uint16_t entityFlags04, std::uint32_t rejectTerrainMask)
          -> std::optional<orphen::ported::entity::ActorEnvironment::TerrainSurface>
      {
        const auto sample = FUN_00227070_sample_ground(*loadedMap, x, y, feetHeight, bodyHeight,
                                                       radius, entityFlags04, rejectTerrainMask);
        if (!sample.found)
        {
          return std::nullopt;
        }

        orphen::ported::entity::ActorEnvironment::TerrainSurface surface;
        surface.height = sample.height;
        surface.terrainFlags = sample.terrainFlagsWinning;
        surface.terrainFlagsAll = sample.terrainFlagsAll;
        surface.primitiveIndex = sample.packedPrimitive;
        surface.cornerHeights = sample.cornerHeights;
        surface.cornerPrimitives = sample.cornerPrimitives;
        surface.sampledFourCorners = sample.sampledFourCorners;
        surface.slopeAngle = sample.slopeAngle;
        return surface;
      };

      // The same scan without the `found` gate. See the declaration.
      environment.FUN_00227390_corner_sample =
          [loadedMap](float x, float y, float feetHeight, float bodyHeight, float radius,
                      std::uint16_t entityFlags04, std::uint32_t rejectTerrainMask)
          -> std::optional<orphen::ported::entity::ActorEnvironment::TerrainSurface>
      {
        const auto sample = FUN_00227070_sample_ground(*loadedMap, x, y, feetHeight, bodyHeight,
                                                       radius, entityFlags04, rejectTerrainMask);
        orphen::ported::entity::ActorEnvironment::TerrainSurface surface;
        surface.height = sample.height;
        surface.terrainFlags = sample.terrainFlagsWinning;
        surface.terrainFlagsAll = sample.terrainFlagsAll;
        surface.primitiveIndex = sample.packedPrimitive;
        surface.cornerHeights = sample.cornerHeights;
        surface.cornerPrimitives = sample.cornerPrimitives;
        surface.sampledFourCorners = sample.sampledFourCorners;
        surface.slopeAngle = sample.slopeAngle;
        return surface;
      };
    }

    environment.bandanaState = &DAT_0054ee00_bandana_;
    environment.bandanaEnvironment =
        [this](std::size_t slot) -> orphen::ported::entity::BandanaEnvironment
    {
      orphen::ported::entity::BandanaEnvironment bandana;
      bandana.selfPalette = DAT_00357e00_bonePalettes_[slot];
      bandana.frameCounter003555b4 = DAT_003555b4_frameCounter_;
      bandana.tickCounter003555b8 = DAT_003555b8_tickCounter_;
      bandana.random = [this]() -> std::uint32_t
      {
        actorRandomState_ = actorRandomState_ * 1103515245u + 12345u;
        return (actorRandomState_ >> 16) & 0x7FFFu;
      };

      // FUN_00213720's walk up +0x192 to the root of the attachment chain. The
      // bandana hangs directly off the lead player, so one hop; the loop is
      // here because the original has it and a second attachment would need it.
      const auto &self = entityPool_.slot(slot);
      std::size_t root = slot;
      while (entityPool_.slot(root).parentSlot192 >= 0)
      {
        root = static_cast<std::size_t>(entityPool_.slot(root).parentSlot192);
      }
      const auto &rootEntity = entityPool_.slot(root);
      bandana.rootFacingRadians = rootEntity.facingRadians5c;
      bandana.rootFadeLevel = rootEntity.fadeLevel134;
      // FUN_0020dc88's fallback, taken on the first frame before any palette
      // exists: the root's world X with this entity's own +0x24 and +0x28, plus
      // half its height. The mixture is the original's, not a simplification.
      bandana.anchorFallback = {rootEntity.positionX20, self.positionZ24,
                                self.positionY28 + self.height58 * 0.5f};
      return bandana;
    };
    return environment;
  }

  // FUN_002582d0, driven from FUN_0022a418:374 and from opcode 0xAB.
  //
  // The graph is discovered by probing, so it costs one ground query per
  // primitive edge per side -- about eight per primitive. That is the 65537
  // FUN_00227840 calls the recompiler spike sees during a map load, and it is
  // why this runs once rather than per frame.
  void PortRuntime::FUN_002582d0_build_follower_navmesh()
  {
    const auto *map = mapViewer_.loadedMap();
    if (map == nullptr)
    {
      return;
    }
    const auto probe = [map](float x, float y, float z) -> orphen::ported::entity::NavGroundProbe
    {
      const auto sample = FUN_00227070_sample_ground(*map, x, y, z, 0.0f, 0.0f, 2u, 0u);
      orphen::ported::entity::NavGroundProbe result;
      result.height = sample.height;
      result.DAT_00354d4e_packedPrimitive =
          static_cast<std::int16_t>(sample.found ? sample.packedPrimitive : -1);
      return result;
    };

    const auto &lead = entityPool_.leadPlayer();
    followerNavmesh_.FUN_00257fc0_reset(
        std::min(map->DAT_003556b0_dRecords78.size(), map->DAT_003556ac_dRecords80.size()));
    followerNavmesh_.FUN_002582d0_build(*map, probe, lead.positionX20, lead.positionZ24,
                                        lead.positionY28);
    std::cout << "[nav] follower graph " << followerNavmesh_.reachedCount() << "/"
              << followerNavmesh_.size() << " primitives reachable from ("
              << lead.positionX20 << ", " << lead.positionZ24 << ", " << lead.positionY28
              << ")\n";
  }

  // FUN_0022a418:318-321. Every entry starts at the lead's spawn position; only
  // the primitive is different here, because the original leaves that halfword
  // holding whatever was in the arena and the port cannot read uninitialised
  // memory on purpose. -1 is what an entity with no cached surface carries, and
  // the follower's scan skips it.
  void PortRuntime::FUN_0022a418_reset_lead_trail()
  {
    const auto &lead = entityPool_.slot(0);
    orphen::ported::entity::ActorEnvironment::LeadTrailPoint seed;
    seed.x = lead.positionX20;
    seed.z = lead.positionZ24;
    seed.groundHeight = lead.positionY28;
    seed.primitive = -1;
    DAT_00355704_leadTrail_.fill(seed);
    DAT_00355708_leadTrailCursor_ = 0;
  }

  // FUN_00224060, called once per frame from FUN_002239c8 between the physics
  // pass and the late script slots.
  void PortRuntime::FUN_00224060_record_lead_trail()
  {
    const auto &lead = entityPool_.slot(0);

    // The gates, in the original's order: +0x04 bit 8, then six states that do
    // not lay a trail, then a cached surface, then that surface's terrain word.
    if ((lead.halfword04 & 0x0008u) != 0)
    {
      return;
    }
    const std::uint16_t state = lead.state60;
    if (state == 0 || state == 10 || state == 0x16 || state == 0x17 || state == 0x18 ||
        state == 0x19)
    {
      return;
    }
    if (lead.groundPrimitive0a < 0)
    {
      return;
    }
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap == nullptr)
    {
      return;
    }
    const std::size_t recordIndex = static_cast<std::size_t>(lead.groundPrimitive0a) & 0x3FFFu;
    if (recordIndex >= loadedMap->DAT_003556b0_dRecords78.size())
    {
      return;
    }
    if ((loadedMap->DAT_003556b0_dRecords78[recordIndex].terrainFlags & 0x01000000u) != 0)
    {
      return;
    }

    // The newest entry is one behind the cursor, wrapping to the top.
    const std::size_t newest = DAT_00355708_leadTrailCursor_ == 0
                                   ? kLeadTrailCapacity - 1
                                   : static_cast<std::size_t>(DAT_00355708_leadTrailCursor_) - 1;
    const auto &last = DAT_00355704_leadTrail_[newest];
    const float dx = last.x - lead.positionX20;
    const float dz = last.z - lead.positionZ24;
    if (std::sqrt(dx * dx + dz * dz) <= 0.25f)
    {
      return;
    }

    auto &entry = DAT_00355704_leadTrail_[DAT_00355708_leadTrailCursor_];
    entry.x = lead.positionX20;
    entry.z = lead.positionZ24;
    entry.groundHeight = lead.groundHeight4c;
    entry.primitive = lead.groundPrimitive0a;
    ++DAT_00355708_leadTrailCursor_;
    if (DAT_00355708_leadTrailCursor_ > kLeadTrailCapacity - 1)
    {
      DAT_00355708_leadTrailCursor_ = 0;
    }
  }

  orphen::ported::script::ScriptEnvironment PortRuntime::scriptEnvironment(std::uint32_t frameTicks)
  {
    orphen::ported::script::ScriptEnvironment environment;
    environment.frameNumber = frameCount_;
    environment.DAT_003555d0_collisionGroupMoved = DAT_003555d0_collisionGroupMoved_;
    environment.frameTicks = frameTicks;
    environment.DAT_003555b8_tickCounter = DAT_003555b8_tickCounter_;
    environment.entityPool = &entityPool_;
    environment.descriptors = &descriptorTable_;
    environment.state = &sceneScript_.state();
    environment.DAT_00571dc0_screenFade = &DAT_00571dc0_screenFade_;
    environment.DAT_00343878_frameFeedback = &DAT_00343878_frameFeedback_;
    environment.DAT_00355054_letterbox = &DAT_00355054_letterbox_;
    environment.map = mapViewer_.loadedMap();

    // Opcode 0xBD methods 0x70 / 0x72.
    environment.FUN_002443f8_start_path =
        [this](std::size_t entitySlot,
               std::span<const orphen::ported::psm2::Vec3> waypoints,
               std::uint32_t duration)
    { return pathFollowers_->FUN_002443f8_start(entitySlot, waypoints, duration); };
    environment.FUN_002445c8_path_progress = [this](std::size_t entitySlot)
    { return pathFollowers_->FUN_002445c8_progress(entitySlot); };

    // FUN_00227070 / FUN_00227798 stand in as the existing PSM2 ground query,
    // the same one the camera uses. `requireOriginalTerrainSample` stays off:
    // script placements are authored, not walked to, so a strict walkability
    // test would reject valid spots. The body, on the other hand, is not
    // optional -- it is the scan band the originals stage, and without it a
    // stacked map answers with the wrong storey.
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      environment.terrainHeight =
          [loadedMap](float x, float y, float feetHeight, float headHeight) -> std::optional<float>
      {
        const Psm2TerrainQueryOptions options{0, false, Psm2ActorBody{feetHeight, headHeight}};
        const auto hit = queryPsm2GroundAt(*loadedMap, x, y, feetHeight, options);
        if (!hit.has_value())
        {
          return std::nullopt;
        }
        return hit->height;
      };

      environment.FUN_00227070_sample_ground =
          [loadedMap](float x, float y, float feetHeight, float bodyHeight, float radius,
                      std::uint16_t entityFlags04, std::uint32_t rejectTerrainMask) -> std::optional<float>
      {
        const auto sample = orphen::port::FUN_00227070_sample_ground(
            *loadedMap, x, y, feetHeight, bodyHeight, radius, entityFlags04, rejectTerrainMask);
        if (!sample.found)
        {
          return std::nullopt;
        }
        return sample.height;
      };
    }

    // FUN_002589c0's pair of FUN_0020d9c8 calls. Roles 2 and 1 are the bust and
    // the head, the two bones FUN_00257c78 drives when a follower looks at the
    // lead. The role lookup runs against the entity's *restored* type, which is
    // the order the original has it in.
    environment.FUN_0020d9c8_release_look_bones = [this](std::size_t slot)
    {
      if (slot >= DAT_004a7e00_boneOverrides_.size() || slot >= entityPool_.slotCount())
      {
        return;
      }
      const EntityModelBinding *binding =
          modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
      if (binding == nullptr || binding->model == nullptr)
      {
        return;
      }
      auto &overrides = DAT_004a7e00_boneOverrides_[slot];
      for (const std::uint8_t role : {std::uint8_t{2}, std::uint8_t{1}})
      {
        orphen::ported::model::FUN_0020d9c8_clear_bone_override(
            overrides, orphen::ported::model::FUN_0020dd78_bone_for_role(*binding->model, role));
      }
    };

    environment.teleportLead = [this](float x, float y, float z)
    {
      const auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      leadPlayer_.resetToMap(*map, orphen::ported::psm2::Vec3{x, y, z});
      FUN_0022a418_stamp_lead_player_flags();
      fieldCamera_.snapToTarget(leadPlayer_.viewState().position);

      // FUN_00263148:16-18. The teleport is followed by a graph rebuild from
      // wherever the lead has landed -- with a full reset first when the mode
      // byte is 0. Both run every time here, because the port's build already
      // resets when the primitive count changes and a second pass over the same
      // seed is idempotent.
      FUN_002582d0_build_follower_navmesh();
    };

    environment.FUN_002661a8_preload_model = [this](std::uint16_t typeId)
    {
      modelStore_.bindingForTypeId(typeId);
    };

    // FUN_002610a8's three statements. Nothing here loads anything: the request
    // is published and FUN_002239c8_service_scene_change spends it at the top of
    // the next frame.
    environment.FUN_002610a8_request_scene_change = [this](std::int32_t destination)
    {
      const auto &lead = entityPool_.leadPlayer();
      DAT_0031e668_departurePosition_ = {lead.positionX20, lead.positionZ24, lead.positionY28};
      DAT_003551f8_groupEntry_ = destination;
      DAT_003551ec_sceneRequest_ = 0x20001;
    };

    // FUN_0025daf8, opcode 0x3C. One assignment in the original; here it has to
    // reach both readers of DAT_00355208.
    environment.FUN_0025daf8_set_map_prop_bank = [this](std::int32_t bank)
    {
      applyMapPropBank(static_cast<int>(bank));
      std::cout << "[props] scene script set the map-prop bank to " << bank << " (0x3C)\n";
    };

    environment.FUN_00217e18_release_camera = [this](bool restore)
    {
      fieldCamera_.FUN_00217e18_release_manual_camera(restore);
    };

    environment.FUN_00217d70_set_manual_camera = [this](const orphen::ported::psm2::Vec3 &eye,
                                                       const orphen::ported::psm2::Vec3 &lookAt)
    {
      fieldCamera_.FUN_00217d70_set_manual_camera(eye, lookAt);
    };

    // FUN_00261fd8's loop, opcode 0xA7. iGpffffb718 is the record count and
    // iGpffffb740 the base, so this is every 0x78 record in the loaded map.
    // FUN_0020dc88's palette lookup for opcode 0x64. DAT_00357e00 is the port's
    // per-slot palette store, rebuilt by publishSceneObjectViews, so the point
    // resolves against the pose the parent was drawn at last frame -- which is
    // the same one-frame lag the original has.
    environment.FUN_0020dc88_bone_point =
        [this](std::size_t parentSlot, int bone,
               orphen::ported::psm2::Vec3 localPoint) -> std::optional<orphen::ported::psm2::Vec3>
    {
      if (parentSlot >= DAT_00357e00_bonePalettes_.size())
      {
        return std::nullopt;
      }
      const auto &palette = DAT_00357e00_bonePalettes_[parentSlot];
      if (palette.empty())
      {
        return std::nullopt;
      }
      const auto &parent = entityPool_.slot(parentSlot);
      return orphen::ported::model::FUN_0020dc88_bone_point(
          palette, bone < 0 ? 0u : static_cast<std::size_t>(bone), localPoint,
          {parent.positionX20, parent.positionZ24, parent.positionY28});
    };

    environment.FUN_00261fd8_retag_primitives = [this](std::uint32_t mask, std::uint32_t topNibble)
    {
      auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      for (auto &record78 : map->DAT_003556b0_dRecords78)
      {
        if ((record78.terrainFlags & mask) != 0)
        {
          record78.terrainFlags = (record78.terrainFlags & 0x0FFFFFFFu) | (topNibble << 28);
        }
      }
    };

    environment.FUN_00217f38_step_camera_path = [this](int elapsedFrames, int durationFrames)
    {
      fieldCamera_.FUN_00217f38_step_camera_path(elapsedFrames, durationFrames);
    };

    environment.FUN_00218158_step_camera_path = [this](int elapsedFrames, int durationFrames)
    {
      fieldCamera_.FUN_00218158_step_camera_path(elapsedFrames, durationFrames);
    };

    environment.FUN_00217fe8_set_camera_path =
        [this](std::span<const orphen::ported::psm2::Vec3> eyePoints,
               std::span<const float> rollValues,
               std::span<const float> zoomScales,
               std::span<const orphen::ported::psm2::Vec3> lookAtPoints)
    {
      fieldCamera_.FUN_00217fe8_set_camera_path(eyePoints, rollValues, zoomScales, lookAtPoints);
    };

    environment.FUN_00237b38_start_dialogue = [this](std::uint32_t blobOffset)
    {
      if (blobOffset == 0)
      {
        dialogueStream_.FUN_00237b38_terminate(sceneScript_.state());
        return;
      }
      const auto bounds = sceneScript_.dialogueRecordBounds(blobOffset);
      dialogueStream_.FUN_00237b38_start(sceneScript_.blob(), bounds.first, bounds.second,
                                         sceneScript_.state());
      const auto &entry = dialogueStream_.log().back();
      std::cout << "[dialogue] " << dialogueStream_.speaker() << ": \"" << dialogueStream_.line()
                << "\"  (voice " << entry.voiceId << ", " << entry.holdFrames << 'f'
                << (entry.measured ? "" : (entry.holdFrames == 0 ? ", empty" : ", estimated"))
                << ")\n";

      // What the record's 0x18 started. Only worth reading when something is
      // going to mix it -- decoding is otherwise a megabyte of work per line
      // thrown away, and a headless run does not want it.
      if (voiceAudioEnabled_ && entry.voiceId != 0 && voiceIndex_.hasAudio())
      {
        const std::vector<std::uint8_t> adpcm = voiceIndex_.readClipAdpcm(entry.voiceId);
        if (!adpcm.empty())
        {
          soundEngine_.FUN_00207010_play_voice_line(
              orphen::ported::sound::decodePsAdpcm(adpcm).samples,
              static_cast<float>(orphen::ported::sound::kVoiceSampleRate));
        }
      }
    };

    environment.FUN_00237c60_dialogue_busy = [this]() { return dialogueStream_.FUN_00237c60_busy(); };
    environment.FUN_00237c70_dialogue_complete = [this]() {
      return dialogueStream_.FUN_00237c70_complete();
    };

    environment.cameraPose = [this]() {
      orphen::ported::script::ScriptEnvironment::CameraPose pose;
      pose.eye = fieldCamera_.pose().eye;
      pose.lookAt = fieldCamera_.pose().target;
      pose.subMode = fieldCamera_.cGpffffb6e1_subMode();
      return pose;
    };

    environment.FUN_00218230_set_zoom = [this](float zoomLog2)
    { fieldCamera_.setZoomLog2(zoomLog2); };

    environment.set_uGpffffb6dc_roll = [this](float radians) { fieldCamera_.setRoll(radians); };

    environment.FUN_00216868_random = [this]() -> std::uint32_t
    {
      actorRandomState_ = actorRandomState_ * 1103515245u + 12345u;
      return (actorRandomState_ >> 16) & 0x7FFFu;
    };

    environment.FUN_00267d38_play_at_entity = [this](std::uint16_t cue, std::size_t slot)
    {
      const auto &entity = entityPool_.slot(slot);
      soundEngine_.FUN_00267d38_play_at(cue, entity.positionX20, entity.positionZ24, entity.positionY28);
    };

    // FUN_0025b778's two debug lines. The gate byte travels with the
    // environment so the check stays where the original makes it.
    environment.DAT_003555dd_debugDisplay = DAT_003555dd_debugDisplay_;
    environment.DAT_003555d3_groupEScene = DAT_003555d3_groupEScene_;
    environment.FUN_002681c0_subprocLine = [this](int slot, std::int32_t subprocId)
    { DAT_00572c38_debugText_.FUN_002681c0_printf("Subproc:%3d [%5d]\n", slot, subprocId); };
    // FUN_0022dcf0 by way of opcode 0x94.
    environment.FUN_0022dcf0_shake_camera = [this](float magnitude, std::int16_t durationTicks)
    { DAT_00355664_cameraShake_.FUN_0022dcf0_request(magnitude, durationTicks); };

    environment.FUN_002681c0_sceneWorkLine = [this](int index, std::uint32_t value)
    {
      DAT_00572c38_debugText_.FUN_002681c0_printf(" %02d:%d(%X)\n", index,
                                                  static_cast<int>(value), value);
    };

    environment.FUN_00205d90_play_music_slot = [this](std::size_t slot, int fader)
    { soundEngine_.FUN_00205d90_play_slot(slot, fader); };
    environment.FUN_002063c8_ramp_music_up = [this](std::size_t slot, int speed, int fader)
    { soundEngine_.FUN_002063c8_ramp_up_slot(slot, speed, fader); };
    environment.FUN_00206260_ramp_music_down = [this](std::size_t slot, int speed, int fader)
    { soundEngine_.FUN_00206260_ramp_down_slot(slot, speed, fader); };

    environment.FUN_00213640_set_bandana = [this](std::int32_t mode)
    {
      orphen::ported::entity::FUN_00213640_set_bandana_mode(
          entityPool_, DAT_004a7e00_boneOverrides_[orphen::ported::entity::kBandanaSlot], mode);
    };

    // FUN_0020dd78 for opcode 0x140, which needs the parsed submesh table.
    environment.FUN_0020dd78_bone_for_role = [this](std::size_t slot, std::uint8_t role) -> std::size_t
    {
      if (slot >= entityPool_.slotCount())
      {
        return 0;
      }
      const EntityModelBinding *binding =
          modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
      if (binding == nullptr || binding->model == nullptr)
      {
        return 0;
      }
      // The original returns 0 on a miss too, so a model with no role 6 attaches
      // at bone 0 rather than not attaching.
      return orphen::ported::model::FUN_0020dd78_bone_for_role(*binding->model, role);
    };

    // Opcode 0x13F calls FUN_002d2f40 itself rather than waiting for the actor
    // loop, so the script reaches it through the same actor environment the
    // loop would have used.
    environment.FUN_002d2f40_build_closeup_rig =
        [this](std::size_t slot) -> std::optional<std::pair<std::int32_t, std::int32_t>>
    {
      if (slot >= entityPool_.slotCount())
      {
        return std::nullopt;
      }
      auto &entity = entityPool_.slot(slot);
      orphen::ported::entity::FUN_002d2f40_build_closeup_rig(
          entity, slot, actorEnvironment(orphen::ported::kNominalFrameTicks));
      if (entity.rigBust19c < 0 || entity.rigHair198 < 0)
      {
        return std::nullopt;
      }
      return std::make_pair(entity.rigBust19c, entity.rigHair198);
    };

    // FUN_0020dc38, run `count` times. Entity +0x168 lives in the runtime's
    // override table, alongside the DAT_004a7e00 block it shares a struct with.
    environment.FUN_0020dc38_hide_bones = [this](std::size_t slot, int firstBone, int count)
    {
      if (slot >= DAT_004a7e00_boneOverrides_.size())
      {
        return;
      }
      auto &state = DAT_004a7e00_boneOverrides_[slot];
      for (int index = 0; index < count; ++index)
      {
        const int bone = firstBone + index;
        if (bone < 0)
        {
          continue;
        }
        orphen::ported::model::FUN_0020dc38_hide_bone(state, static_cast<std::size_t>(bone));
      }
    };

    // FUN_0022dbc8 / FUN_0022dc68, opcodes 0xA4 and 0xA6. Both walk the two
    // primitive tables in lockstep against the 0x78 record's +0x04 group mask.
    environment.FUN_0022dbc8_show_map_primitives = [this](std::uint32_t groupMask, bool visible)
    {
      auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      const std::size_t count =
          std::min(map->DAT_003556ac_dRecords80.size(), map->DAT_003556b0_dRecords78.size());
      for (std::size_t index = 0; index < count; ++index)
      {
        if ((map->DAT_003556b0_dRecords78[index].terrainFlags & groupMask) == 0)
        {
          continue;
        }
        auto &flags = map->DAT_003556ac_dRecords80[index].primitiveFlags;
        // The byte-zero branch ORs the bit in, and 0x20 is the *hidden* bit, so
        // "off" sets it. FUN_00209140 skips the primitive outright once it is.
        flags = visible ? (flags & ~orphen::ported::render::visibility::kHiddenBit)
                        : (flags | orphen::ported::render::visibility::kHiddenBit);
      }
    };

    environment.FUN_0022dc68_enable_map_terrain = [this](std::uint32_t groupMask, bool solid)
    {
      auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      for (auto &record78 : map->DAT_003556b0_dRecords78)
      {
        if ((record78.terrainFlags & groupMask) == 0)
        {
          continue;
        }
        // 0x800 is kOriginalTerrainSampleBit: both loops of FUN_00227840 skip a
        // primitive without it, so clearing it takes the surface out of the
        // ground scan entirely. That is what an opening door does -- and while
        // it stayed set, walking into the closed door's panel found it as ground
        // and lifted the actor up it.
        record78.leadingWord = solid ? (record78.leadingWord | 0x800u)
                                     : (record78.leadingWord & ~0x800u);
      }
    };

    // FUN_00260738's two writes, opcodes 0x7D and 0x7E, and the same pair
    // FUN_002676d8 reaches through opcode 0xBE. The map has to be mutable
    // here, which ScriptEnvironment::map is not, so it goes through a callback.
    environment.FUN_00260738_move_collision_group =
        [this](std::uint32_t group, std::uint8_t channel, float value, bool rotation)
    {
      auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      if (rotation)
      {
        orphen::ported::psm2::FUN_00260738_set_group_rotation(*map, group, channel, value);
        return;
      }
      orphen::ported::psm2::FUN_00260738_set_group_translation(*map, group, channel, value);
    };

    // FUN_002606d0's body, opcode 0x142.
    environment.FUN_002606d0_detach_children = [this](std::size_t slot)
    {
      if (slot >= entityPool_.slotCount())
      {
        return;
      }

      // FUN_00265f70: every slot whose +0x192 names this one, released through
      // FUN_00265ec0 -- which runs FUN_00265f70 again on its way out, so the
      // whole subtree comes down with its root. s01_e012 needs that: the head is
      // parented to the character and a second layer is parented to the head.
      // The original's status test is `> 0`, so a slot merely reserved by
      // FUN_00265dc0 is not swept up.
      std::vector<std::size_t> pending{slot};
      while (!pending.empty())
      {
        const std::size_t parent = pending.back();
        pending.pop_back();
        for (std::size_t child = 0; child < entityPool_.slotCount(); ++child)
        {
          if (child == parent ||
              entityPool_.status(child) != orphen::ported::entity::SlotStatus::ScriptSpawned ||
              entityPool_.slot(child).parentSlot192 != static_cast<std::int16_t>(parent))
          {
            continue;
          }
          entityPool_.releaseSlot(child);
          // FUN_00229c40 zeroes the whole 0x1D8-byte entity when the slot is
          // reused, and +0x168 is inside it, so a freed slot must not carry its
          // hides into whatever lands there next.
          DAT_004a7e00_boneOverrides_[child].reset();
          pending.push_back(child);
        }
      }

      // FUN_002298d0 maps type 1 to 0 and everything else to non-zero, so only
      // the lead player reaches FUN_00251e40 -- which rebuilds the bandana the
      // sweep above just destroyed.
      if (entityPool_.slot(slot).typeId00 == 1)
      {
        const EntityModelBinding *leaderBinding =
            modelStore_.bindingForTypeId(entityPool_.slot(slot).effectiveTypeId());
        if (orphen::ported::entity::FUN_00251e40_attach_bandana(
                entityPool_, descriptorTable_,
                leaderBinding != nullptr ? leaderBinding->model : nullptr))
        {
          DAT_0054ee00_bandana_ = orphen::ported::entity::BandanaState{};
          DAT_004a7e00_boneOverrides_[orphen::ported::entity::kBandanaSlot].reset();
        }
      }

      // FUN_0020dc48(entity, -1): all 42 bytes of +0x168 back to zero. The
      // override poses in the DAT_004a7e00 half are not touched -- the original
      // clears 0x168..0x191 and nothing else.
      orphen::ported::model::FUN_0020dc48_clear_bone(DAT_004a7e00_boneOverrides_[slot], -1);
    };

    return environment;
  }

  // FUN_0022a418:339-354, the scene environment block. Seeded into the
  // interpreter's own state rather than held separately, because the original
  // has exactly one set of globals here: FUN_0022a418 writes the defaults and
  // the scene script's opcodes overwrite the same words. Keeping one copy is
  // what makes "did the script set this?" a question we never have to ask.
  //
  //   0x35566c  light colour 1   0x606060 (FUN_0022a360 via DAT_00325390)
  //   0x355670  light colour 2   0xffffff
  //   0x355674  fog colour       0x505050
  //   0x355678  fog colour 2     0x505050
  //   0x35567c  fog near         drawDistance * 0.25
  //   0x355680  fog far          drawDistance
  //
  // The script reaches them through 0x96 (light 1), 0x97/0x98 (light 2 and its
  // direction), 0x99 (second direction), 0xB9/0xBA (fog colour) and 0xBB (fog
  // band). All eight are already implemented in the interpreter.
  void PortRuntime::seedSceneEnvironmentDefaults()
  {
    auto &state = sceneScript_.state();
    const float drawDistance = mapViewer_.drawDistance();

    // Seeded from the scene block that FUN_0022a418:185 has already applied, so
    // that a script 0xB8 overwrites a live value rather than a zero.
    state.DAT_0032538c_cameraDistance = drawDistance;
    state.uGpffffb6fc_globalRgb = 0x606060;
    state.uGpffffb700_vectorRgb = 0xFFFFFF;
    state.uGpffffb704_color1 = 0x505050;
    state.uGpffffb708_color2 = 0x505050;
    state.fGpffffb70c_fadeNear = drawDistance * 0.25f;
    state.fGpffffb710_fadeFar = drawDistance;
  }

  // Push whatever survived the script into the renderer: the fog band, and the
  // light block that FUN_00200e38:121-167 uploads to VU1 memory 0x2a0..0x2a7.
  // FUN_00254f60's item branch. The original builds the entity in place at
  // 0x0058C260 -- pool slot 2 -- and hangs it off the chest's role-1 bone.
  bool PortRuntime::buildChestItemEntity(std::size_t chestSlot, std::int16_t itemId)
  {
    constexpr std::size_t kItemSlot = 2;
    // FUN_00229c40(0x58c260, chest[+0x130] + 0x1F1). The 0x1F1 band shares one
    // descriptor and indexes its model record by the raw type id, so the item
    // id picks the model directly.
    const std::int32_t typeId = static_cast<std::int32_t>(itemId) + 0x1F1;

    auto &item = entityPool_.slot(kItemSlot);
    entityPool_.FUN_00229c40_initialize(kItemSlot, typeId, descriptorTable_);

    // The 0x1F1 band's meshes come out of ITM.BIN, which is not in every
    // extracted disc root. Say so rather than cross-fading to an empty box --
    // the caption still works, so the cutscene is not dropped for it.
    const EntityModelBinding *binding = modelStore_.bindingForTypeId(typeId);
    if (binding == nullptr || binding->model == nullptr)
    {
      std::cout << "[chest] item id " << itemId << " (type 0x" << std::hex << typeId << std::dec
                << ") has no model -- ITM.BIN missing from the disc root?\n";
    }

    orphen::ported::entity::FUN_00225bc8_set_animation(item, 4);

    auto &chest = entityPool_.slot(chestSlot);
    item.facingRadians5c = chest.facingRadians5c;
    // 0x4000 is the bit FUN_00239ce0:17 and FUN_00251ed8 both read as "do not
    // run this entity's behavior". Without it the item runs its *usable* handler
    // -- for a lantern that is FUN_002d4cd8, which lights the player and then
    // deletes itself.
    item.halfword04 = static_cast<std::uint16_t>(item.halfword04 | 0x4100);
    item.halfword08 = static_cast<std::uint16_t>(item.halfword08 | 0x0001);

    // FUN_0020dd78(chest, 1) then FUN_0020dc88: the item sits on the chest's
    // role-1 bone. Falling back to the chest's own origin keeps it in frame
    // when the palette is not built yet.
    orphen::ported::psm2::Vec3 anchor{chest.positionX20, chest.positionZ24, chest.positionY28};
    const auto &palette = DAT_00357e00_bonePalettes_[chestSlot];
    const EntityModelBinding *chestBinding = modelStore_.bindingForTypeId(chest.typeId00);
    if (chestBinding != nullptr && chestBinding->model != nullptr && !palette.empty())
    {
      const std::size_t bone =
          orphen::ported::model::FUN_0020dd78_bone_for_role(*chestBinding->model, 1);
      if (bone < palette.size())
      {
        const auto &matrix = palette[bone];
        anchor = {matrix[12], matrix[13], matrix[14]};
      }
    }
    item.positionX20 = anchor.x;
    item.positionZ24 = anchor.y;
    item.positionY28 = anchor.z;
    item.groundHeight4c = item.positionY28;

    // The ramp the cross-fade reads, and the level it starts at.
    item.fadeRamp62 = 0x60;
    item.fadeLevel134 = 3;
    return true;
  }

  // FUN_002342c0:39-47 and the half of FUN_00233eb8 that undoes it. The item
  // scene replaces the scene's own lighting and fog for as long as it is up.
  void PortRuntime::setItemSceneRenderState(bool enable)
  {
    itemSceneRenderState_ = enable;
    applySceneEnvironment();
  }

  void PortRuntime::applySceneEnvironment()
  {
    const auto &state = sceneScript_.state();

    // FUN_002342c0's block, in its own order:
    //   DAT_0035566c = 0x404040   the ambient
    //   DAT_00355670 = 0x808080   light 0's colour
    //   DAT_00355674 = 0          the fog colour
    //   DAT_003439c8 = (0, 0, -1) the light direction, straight down
    //
    // The fog colour is the one that shows. The global fade cap makes every
    // map primitive nearly transparent rather than nearly black, so what the
    // room reads as is whatever is behind it -- which is the fog-colour clear.
    constexpr std::uint32_t kItemSceneAmbient = 0x404040;
    constexpr std::uint32_t kItemSceneLight = 0x808080;
    constexpr std::uint32_t kItemSceneFog = 0x000000;

    // 0xB8 writes DAT_0032538c and fGpffffb6b8 = DAT_00355628 together, and
    // DAT_00355628 is what the visibility pass culls against -- so a script
    // that sets it mid-load moves the draw distance, not a camera parameter.
    // --draw-distance still wins over both the scene block and the script.
    if (!drawDistanceOverridden_ && state.DAT_0032538c_cameraDistance > 0.0f)
    {
      mapViewer_.setDrawDistance(state.DAT_0032538c_cameraDistance);
    }

    mapViewer_.setFogColour(itemSceneRenderState_ ? kItemSceneFog : state.uGpffffb704_color1);
    mapViewer_.setFogBand(state.fGpffffb70c_fadeNear, state.fGpffffb710_fadeFar);

    // FUN_00200e38:121-167 builds the two VIF unpacks that feed VU1's lighting:
    //
    //   0x6c0302a0  three quadwords, only .x set, from -DAT_003439c8/cc/d0
    //   0x6e0542a3  five quadwords of bytes -- uGpffffb700 into light 0's
    //               colour at 0x2a3, uGpffffb6fc into the ambient at 0x2a7,
    //               and zero into 0x2a4..0x2a6
    //
    // Only slot 0 ever gets a direction on this path, so slots 1..3 sit at zero
    // colour and contribute nothing regardless of their intensity.
    orphen::ported::render::SceneLighting lighting;
    orphen::ported::render::SceneLighting::unpack(
        itemSceneRenderState_ ? kItemSceneAmbient : state.uGpffffb6fc_globalRgb,
        lighting.ambient);
    orphen::ported::render::SceneLighting::unpack(
        itemSceneRenderState_ ? kItemSceneLight : state.uGpffffb700_vectorRgb,
        lighting.lightColour[0]);
    // The EE negates the vector on upload, so the microprogram's dot product is
    // against -D and this holds the already-negated form.
    lighting.lightDirection[0] =
        itemSceneRenderState_
            ? orphen::ported::psm2::Vec3{0.0f, 0.0f, 1.0f}
            : orphen::ported::psm2::Vec3{-state.DAT_003439c8_vector[0],
                                         -state.DAT_003439c8_vector[1],
                                         -state.DAT_003439c8_vector[2]};
    lighting.active = true;
    // VU1 0x0206 reads the specular pass's colour straight from 0x2a3, the same
    // quadword as light 0, so the sheen is always tinted by the scene's
    // directional light. The direction is filled in per frame by
    // setGleamDirection(), which needs the camera.
    orphen::ported::render::SceneLighting::unpack(state.uGpffffb700_vectorRgb,
                                                  lighting.gleamColour);
    lighting.gleamActive = true;

    // FUN_0020b430, which compacts the live slots of DAT_00343888 into the VU0
    // light list. Table order is preserved, so the entries from slots 0..2 --
    // the ones that reach VU1 as directional lights 1..3 -- are always a prefix.
    // The item scene replaces the scene's lighting wholesale, so it drops these
    // too.
    if (!itemSceneRenderState_ && !suppressPointLights_)
    {
      const auto &table = state.DAT_00343888_lights;
      for (std::uint32_t index = 0;
           index < orphen::ported::render::LightTable::kSlotCount &&
           lighting.pointLightCount < orphen::ported::render::SceneLighting::kPointLightCapacity;
           ++index)
      {
        const auto &source = table.slot(index);
        if (source.radius == 0.0f)
        {
          continue;
        }
        auto &light = lighting.pointLights[lighting.pointLightCount++];
        light.position = {source.x, source.y, source.z};
        light.radius = source.radius;
        light.radiusSquared = source.radius * source.radius;
        light.inverseRadiusSquared = 1.0f / light.radiusSquared;
        light.colour[0] = static_cast<float>(source.red) / 255.0f;
        light.colour[1] = static_cast<float>(source.green) / 255.0f;
        light.colour[2] = static_cast<float>(source.blue) / 255.0f;
        light.tableSlot = static_cast<int>(index);
        if (index < static_cast<std::uint32_t>(
                        orphen::ported::render::SceneLighting::kDirectionalTableSlots))
        {
          ++lighting.directionalPointLights;
        }
      }
    }
    // setSceneLighting replaces the block wholesale, so the toggles have to be
    // carried across or a scene change would silently turn them back off.
    lighting.applyLightFloor = mapViewer_.sceneLighting().applyLightFloor;
    lighting.applyUnlitFlag = mapViewer_.sceneLighting().applyUnlitFlag;
    mapViewer_.setSceneLighting(lighting);
  }

  // Everything a scene needs once its map is in place. Called from initialize
  // and again whenever MapViewer reports a new map generation, so loading the
  // first scene and cycling to another go through the same code -- the two
  // paths having diverged is what left cycled maps rendering the first scene's
  // fog, model bindings and entity set.
  // FUN_00228e28:150-193. Boot loads SCR.BIN resource 0xBD once and derives the
  // map-streamed prop banks from it; nothing per-scene about it, so this runs at
  // initialize rather than per scene load.
  void PortRuntime::loadMapPropDescriptors()
  {
    mapPropTable_.reset();
    if (discRoot_.empty())
    {
      return;
    }

    orphen::harness::FlatBinArchive scrArchive;
    if (!scrArchive.open(discRoot_ / "SCR.BIN"))
    {
      std::cout << "[props] SCR.BIN not found; map-streamed props unavailable\n";
      return;
    }

    constexpr std::uint32_t kDAT_00228e28_descriptorResource = 0xBD;
    const std::vector<std::uint8_t> blob = scrArchive.decode(kDAT_00228e28_descriptorResource);
    if (blob.empty() || !mapPropTable_.FUN_00228e28_build(blob))
    {
      std::cout << "[props] SCR.BIN resource 0xBD did not yield prop banks\n";
      return;
    }

    std::cout << "[props] SCR.BIN 0xBD " << blob.size() << " bytes -> "
              << mapPropTable_.bankCount() << " banks\n";
  }

  // FUN_0022a178. The map's own texture pages are not a private list -- they go
  // into the *global* texture slots 0..9, from a ten-entry table at
  // DAT_00325394, and every slot index in the game refers to that one array.
  // That is what makes a PSC3 subdraw able to name a map page (see
  // drawObjectModel's passTexture) and what makes a PSM2 material slot's type
  // byte a slot index rather than a page index.
  //
  // eeMemory.bin confirms both the table and the result for s01_e012:
  // DAT_00325394 = {0286, 0002, 02c8, 0253, 000d, 02c9, 000f} and
  // DAT_003429a8[0..6] holds the same ids in the same order. The port's own
  // page list is the same sequence, so it stands in for the table.
  void PortRuntime::FUN_0022a178_bind_map_textures()
  {
    const auto &pages = mapViewer_.loadedTexturePages();
    for (std::size_t index = 0; index < pages.size() && index < kMapTextureSlotCount; ++index)
    {
      modelStore_.mutableTextureSlots().FUN_00210280_load_into_slot(
          static_cast<int>(index), pages[index].resourceId);
    }
  }

  // FUN_002239c8:18-33, the top of the field frame:
  //
  //   if ((uGpffffb27c != 0) && (1 < iGpffffadbc - 9U) &&
  //      (((uGpffffb27c & 2) == 0 || (FUN_0025d238(0) != 0)))) {
  //     FUN_002f9308(0,0); FUN_00305110(); FUN_0022a418(); ...
  //   }
  //
  // `uGpffffb27c` is DAT_003551ec and `iGpffffadbc` is DAT_00354d2c, the game
  // mode -- gp 0xffffadbc against the 0x00359F70 base. The unsigned `1 < mode -
  // 9` excludes modes 9 and 10 and nothing else, and bit 1 of the request defers
  // the load until the fade-out block has bottomed out.
  //
  // Opcode 0x8E writes 0x20001, so neither gate defers it. The waiting is done
  // by the script instead: s01_e012's subproc 0x1187 spins on `0x86` -- the same
  // FUN_0025d238 -- before it ever reaches the 0x8E. Modelling the bit anyway
  // costs one branch and keeps the two readings of "the fade is done" from
  // drifting apart when a scene does set it.
  void PortRuntime::FUN_002239c8_service_scene_change()
  {
    if (DAT_003551ec_sceneRequest_ == 0)
    {
      return;
    }
    if (DAT_00354d2c_gameMode_ - 9u <= 1u)
    {
      return;
    }
    if ((DAT_003551ec_sceneRequest_ & 2u) != 0 &&
        !DAT_00571dc0_screenFade_.FUN_0025d238_step_fade_out(0))
    {
      return;
    }

    // FUN_0022a418:49. Sticky for as long as the scene stays loaded, because
    // FUN_0022a238 keeps reading it to pick a descriptor list.
    DAT_003555d3_groupEScene_ = (DAT_003551ec_sceneRequest_ & 0x20000u) != 0;

    // FUN_0022a418:98-103. Two calls, one selector each: the ordinary scene
    // takes (DAT_003551f4, DAT_003551f0) and the group-0xE scene takes
    // (0xE, DAT_003551f8).
    const orphen::harness::McbSceneSelection destination{
        static_cast<std::uint16_t>(DAT_003555d3_groupEScene_ ? kGroupEScene : DAT_003551f4_sceneSection_),
        static_cast<std::uint16_t>(DAT_003555d3_groupEScene_ ? DAT_003551f8_groupEntry_ : DAT_003551f0_sceneEntry_)};

    if (discRoot_.empty())
    {
      std::cout << "[scene] change to " << orphen::harness::sceneName(destination)
                << " ignored: no disc root to load it from\n";
      DAT_003551ec_sceneRequest_ = 0;
      DAT_003555d3_groupEScene_ = false;
      return;
    }

    std::cout << "[scene] " << (mapViewer_.loadedDiscScene().has_value()
                                    ? orphen::harness::sceneName(*mapViewer_.loadedDiscScene())
                                    : std::string("<none>"))
              << " -> " << orphen::harness::sceneName(destination) << " (request 0x" << std::hex
              << DAT_003551ec_sceneRequest_ << std::dec << ", frame " << frameCount_ << ")\n";

    try
    {
      mapViewer_.loadDiscSceneMap(discRoot_, destination);
    }
    catch (const std::exception &error)
    {
      // The original walks off the end of the table into FUN_0026c088 and stops
      // the machine. Staying on the scene that is already loaded is the more
      // useful failure here, and clearing the request stops it retrying every
      // frame.
      std::cerr << "[scene] " << orphen::harness::sceneName(destination)
                << " could not be loaded: " << error.what() << '\n';
      DAT_003551ec_sceneRequest_ = 0;
      DAT_003555d3_groupEScene_ = false;
      return;
    }

    // FUN_0022a418's body. The port's share of it is loadSceneForCurrentMap,
    // which reads DAT_003555d3 and DAT_003551f4 as they stand now.
    loadSceneForCurrentMap();

    // FUN_0022a418:409-411. The scene that was current becomes the previous
    // one, and the request is spent. Note the ordering: 0x8E left
    // DAT_003551f4/f0 naming the *departing* scene, and that is deliberately
    // what gets recorded -- a group-0xE scene has no coordinates of its own in
    // that pair.
    DAT_00354d7c_previousEntry_ = DAT_003551f0_sceneEntry_;
    DAT_00354d78_previousSection_ = DAT_003551f4_sceneSection_;
    DAT_003551ec_sceneRequest_ = 0;
  }

  // DAT_00355208's two readers in the port: FUN_00229980's streamed branch,
  // which the descriptor table owns, and the model binding that follows it.
  void PortRuntime::applyMapPropBank(int bank)
  {
    DAT_00355208_mapPropBank_ = bank;
    modelStore_.setMapPropTable(&mapPropTable_, bank);
    // The same banks reach the descriptor path, which is what lets a streamed
    // type id spawn from a real descriptor instead of struct defaults. Must be
    // before the script runs: the scene bootstrap spawns props immediately.
    descriptorTable_.setMapPropTable(&mapPropTable_, bank);
  }

  void PortRuntime::loadSceneForCurrentMap()
  {
    // Models bind before the script runs, so the spawn path can report a
    // missing model at the moment it spawns the entity that wanted it.
    modelStore_.initialize(mapViewer_.loadedSceneResources(), discRoot_, &descriptorTable_);

    // FUN_0022a418:50 sets DAT_00355208 from DAT_003551f4, the stage number of
    // the scene being entered -- s01_e024 is stage 1. That is the bank
    // FUN_00229980 uses for the 0x272 type range.
    //
    // DAT_003551f4 is not simply "the section the bundle came from". For every
    // load the port used to do they are the same, so this used to read the
    // section off the loaded scene -- but a group-0xE load does not touch
    // DAT_003551f4 at all (FUN_002610a8 writes only DAT_003551f8), so an
    // s01_e012 handoff pulls its bundle out of section 14 while its props still
    // come from stage 1's bank. Adopting the loaded section here instead would
    // ask for bank 14, which does not exist.
    if (!DAT_003555d3_groupEScene_)
    {
      const auto loadedScene = mapViewer_.loadedDiscScene();
      DAT_003551f4_sceneSection_ = loadedScene.has_value() ? static_cast<int>(loadedScene->section) : -1;
      DAT_003551f0_sceneEntry_ = loadedScene.has_value() ? static_cast<int>(loadedScene->entry) : -1;
    }
    //
    // Only the seed, though: opcode 0x3C writes DAT_00355208 outright, and a
    // group-0xE scene uses it to replace exactly this inheritance.
    applyMapPropBank(DAT_003551f4_sceneSection_);

    modelStore_.FUN_00221fd8_bind_boot_textures();
    FUN_0022a178_bind_map_textures();
    // FUN_00238c90, which the original runs inline in FUN_00221fd8 on the
    // decoded buffer before it hands it to the slot. Same measurement, one
    // step later.
    dialogueFont_.FUN_00238c90_measure(
        0, modelStore_.textureSlots().slot(orphen::ported::text::kFontSlotLow).texture);
    dialogueFont_.FUN_00238c90_measure(
        1, modelStore_.textureSlots().slot(orphen::ported::text::kFontSlotHigh).texture);
    // The cutscene subtitle walk needs the same width table before it can place
    // a glyph, and the sheets have only just been decoded.
    dialogueStream_.setFont(&dialogueFont_);
    // FUN_0022a418 lines 190-197 bind the lead player's model before the
    // scene script runs, straight through FUN_00221d20 with the default bank
    // rather than through FUN_00266118. It reads the party leader from
    // DAT_0058beb0 and *forces it to 1 when it is zero*, which is what
    // happens here: the pool's lead slot has not been given its type yet at
    // this point in the load, so without that default the preload list would
    // claim slot 10 and every scene texture would land one slot early.
    //
    // On a reload the pool still holds the previous scene's leader, which is
    // also what the original does -- DAT_0058beb0 is party state and survives
    // the scene change.
    constexpr std::uint32_t kDAT_0058beb0_defaultLeader = 1;
    const std::uint32_t leader = entityPool_.leadPlayer().typeId00 != 0
                                     ? entityPool_.leadPlayer().typeId00
                                     : kDAT_0058beb0_defaultLeader;
    const EntityModelBinding *leaderBinding = modelStore_.bindingForTypeId(leader);

    resetLeadPlayerForLoadedMap();

    // FUN_0022a418:206, `FUN_00251dc0(0x58beb0)`: the lead player's hit points,
    // attack power and defence, off the party record loaded above. Without it
    // +0x12C stays at zero and every hit lands on FUN_00216140's "at least one
    // point" floor -- the right answer for a flyer by accident, and the wrong
    // one for anything with armour.
    sceneScript_.state().FUN_00251dc0_load_player_stats(entityPool_.leadPlayer());

    // FUN_0022a418:287, `DAT_00355668 = 0`. Only the tick count is cleared --
    // the magnitude is left standing, exactly as the original leaves it, and
    // is overwritten by the next FUN_0022dcf0 that gets past the guard.
    DAT_00355664_cameraShake_.uGpffffb6f8_remaining = 0;

    // FUN_0022a418:256, immediately after the pool clear and before the scene
    // script runs. The lead player's model has to be bound first because the
    // anchor bone is looked up by role out of grp_0001.
    if (orphen::ported::entity::FUN_00251e40_attach_bandana(
            entityPool_, descriptorTable_,
            leaderBinding != nullptr ? leaderBinding->model : nullptr))
    {
      DAT_0054ee00_bandana_ = orphen::ported::entity::BandanaState{};
      const auto &bandana = entityPool_.slot(orphen::ported::entity::kBandanaSlot);
      std::cout << "[bandana] slot " << orphen::ported::entity::kBandanaSlot << " type 0x"
                << std::hex << bandana.typeId00 << std::dec << " on bone "
                << -static_cast<int>(bandana.attachBone194) << " (role "
                << static_cast<int>(orphen::ported::entity::kBandanaAnchorBoneRole)
                << ") of the lead player\n";
    }

    runSceneScript();
  }

  void PortRuntime::reportSceneEnvironment() const
  {
    const auto &state = sceneScript_.state();
    std::cout << "[env] " << mapViewer_.loadedSourceDescription()
              << " draw=" << mapViewer_.drawDistance()
              << " fog=" << mapViewer_.fogNear() << ".." << mapViewer_.fogFar()
              << " 0x" << std::hex << mapViewer_.fogColour()
              << " light1=0x" << state.uGpffffb6fc_globalRgb
              << " light2=0x" << state.uGpffffb700_vectorRgb << std::dec << '\n';

    // The cache half of the texture slot table, which s01_e24.bin pins to
    // 0206 01bf 01bc 0204 0203 0200 0202 0201 in slots 10..17. Printed per
    // scene load so a reload can be checked against the same oracle a cold
    // start is.
    const auto &slots = modelStore_.textureSlots();
    std::cout << "[env] slots";
    for (std::size_t slot = orphen::ported::resource::kDefaultBankStart;
         slot < orphen::ported::resource::kDefaultBankStart +
                    orphen::ported::resource::kDefaultBankCount;
         ++slot)
    {
      if (slots.slot(slot).occupied())
      {
        std::cout << ' ' << slot << ':' << std::hex << std::setw(4) << std::setfill('0')
                  << slots.slot(slot).DAT_003429a8_residentId << std::dec << std::setfill(' ');
      }
    }
    std::cout << "  gen=" << slots.generation() << '\n';
  }

  void PortRuntime::runSceneScript()
  {
    scriptTrace_.reset();
    sceneScript_ = {};

    // FUN_0022a360's per-load seed, applied before anything scene specific.
    // Every early return below then leaves the renderer on these rather than on
    // the previous scene's -- which is what cycling maps used to do, carrying
    // one scene's fog onto every map after it.
    if (!drawDistanceOverridden_)
    {
      mapViewer_.setDrawDistance(kDAT_0032538c_defaultDrawDistance);
    }
    seedSceneEnvironmentDefaults();
    applySceneEnvironment();

    const auto *resources = mapViewer_.loadedSceneResources();
    if (resources == nullptr)
    {
      return;
    }

    // MCB category 1 is the scene script. Its resource id is scene-private and
    // is not an index into the flat SCR.BIN archive.
    const auto *record = resources->findFirst(1);
    if (record == nullptr)
    {
      std::cout << "[scr] scene has no script record\n";
      return;
    }

    const std::vector<std::uint8_t> decoded = resources->decodeRecord(*record);
    if (!scrDumpPath_.empty())
    {
      std::ofstream dump(scrDumpPath_, std::ios::binary);
      dump.write(reinterpret_cast<const char *>(decoded.data()),
                 static_cast<std::streamsize>(decoded.size()));
      std::cout << "[scr] dumped " << decoded.size() << " bytes to " << scrDumpPath_ << "\n";
    }
    if (!sceneScript_.load(decoded))
    {
      std::cout << "[scr] script blob too small to hold a header (" << decoded.size() << " bytes)\n";
      return;
    }

    // FUN_0022a418:185 applies the scene block's draw distance before it seeds
    // the environment and before either script entry runs, so the whole chain
    // has to happen in that order: distance, then the defaults derived from it,
    // then the script, which may override any of it.
    if (const auto &scriptDrawDistance = sceneScript_.sceneDrawDistance();
        scriptDrawDistance.has_value() && !drawDistanceOverridden_)
    {
      mapViewer_.setDrawDistance(*scriptDrawDistance);
    }
    seedSceneEnvironmentDefaults();

    // FUN_0025b2f0 copies the scene's eight music requests out of header word
    // 10, and FUN_0022a418 reaches FUN_00206840 to act on them. Both run before
    // either script entry.
    startSceneMusic();

    // FUN_0022a418 runs header word 0 and word 1 at load, from different points
    // in the bootstrap. The per-frame entry and the actor-state entries exist
    // but are not driven yet.
    const auto environment = scriptEnvironment();
    sceneScript_.FUN_0025b6d0_run_init(environment, scriptTrace_);
    const bool initClean = !sceneScript_.lastRunOverran() && !sceneScript_.lastRunHaltedOnUnimplemented();
    const std::uint16_t initHaltOpcode = sceneScript_.lastHaltOpcode();
    const std::uint32_t initHaltOffset = sceneScript_.lastHaltOffset();
    const bool initHaltedOnUnimplemented = sceneScript_.lastRunHaltedOnUnimplemented();

    sceneScript_.FUN_0025b728_run_start(environment, scriptTrace_);

    applySceneEnvironment();
    reportSceneEnvironment();

    applySceneMarkerSpawn();

    // FUN_0022a418:372-375, right after the start entry has run and the lead is
    // wherever the scene put it.
    FUN_002582d0_build_follower_navmesh();

    advanceEntityAnimations(orphen::ported::kNominalFrameTicks);
    publishSceneObjectViews(orphen::ported::kNominalFrameTicks);

    std::cout << "[scr] script " << decoded.size() << " bytes, init 0x" << std::hex
              << sceneScript_.entryOffset(orphen::ported::script::SceneScriptEntry::Init)
              << " start 0x" << sceneScript_.entryOffset(orphen::ported::script::SceneScriptEntry::Start)
              << std::dec << ", spawned " << entityPool_.scriptSpawnedCount() << " entities\n";

    if (!initClean)
    {
      std::cout << "[scr] init halted: ";
      if (initHaltedOnUnimplemented)
      {
        std::cout << "unimplemented opcode 0x" << std::hex << initHaltOpcode
                  << " at 0x" << initHaltOffset << std::dec << '\n';
      }
      else
      {
        std::cout << "stream overran the blob\n";
      }
    }
    if (sceneScript_.lastRunOverran() || sceneScript_.lastRunHaltedOnUnimplemented())
    {
      std::cout << "[scr] start halted: ";
      if (sceneScript_.lastRunHaltedOnUnimplemented())
      {
        std::cout << "unimplemented opcode 0x" << std::hex << sceneScript_.lastHaltOpcode()
                  << " at 0x" << sceneScript_.lastHaltOffset() << std::dec << '\n';
      }
      else
      {
        std::cout << "stream overran the blob\n";
      }
    }
  }

  // A scene has no spawn point of its own: FUN_0022a418 copies whatever the
  // previous map's warp staged into DAT_00325340, and a cold boot has nothing.
  // The nearest thing the scene itself carries is a group 2 placement record.
  //
  // The evidence that these are markers rather than props: the init script
  // registers the lookup entry (id 1, type 0x55) and then runs 0x51 with group
  // 2, and FUN_0025eb48 explicitly breaks out without spawning when the looked
  // up type is 0x55. Type 0x55's descriptor is 0.1 by 0.1 with flags 0x400 --
  // too small to be an object. So group 2 records are authored positions that
  // deliberately produce no entity.
  //
  // Standing them in for the spawn point is an inference, and on s01_e024 it is
  // now a *disproven* one. An EE memory dump taken right after loading the map
  // from the debug menu has DAT_00325340 = (-3.25, -12.75) and pool slot 0 at
  // that position; the group 2 record is at (-5.5, -12). So this picks the wrong
  // spot by about 2.3 units.
  //
  // The real value is not derivable from anything the scene ships. It is not in
  // SLUS_200.11, and it is not in the SCR as a script coordinate pair (checked
  // for the 100000-scaled ints). FUN_0022b2c0 -- the only writer of
  // DAT_00325340 -- is called from opcodes 0x8B/0x8C alone, so the position is
  // authored by whatever *sends you here*: the previous map's warp, or the debug
  // menu's own map-select. A cold boot into a scene genuinely has nothing to
  // read, which is what FUN_0022a418 assumes.
  //
  // So this stays as the fallback, but it is a guess and the console says so.
  // Use --spawn for anything that needs to match the game.
  void PortRuntime::applySceneMarkerSpawn()
  {
    if (spawnOverride_.has_value() || scriptTrace_.leadTeleported())
    {
      return;
    }

    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap == nullptr)
    {
      return;
    }

    // The scene's own spawn, out of the SCR header block FUN_0025b600 reads.
    // This is what the game itself uses when you arrive without a warp target,
    // so it beats every guess below it.
    if (const auto &scriptSpawn = sceneScript_.sceneSpawn(); scriptSpawn.has_value())
    {
      leadPlayer_.resetToMap(*loadedMap, *scriptSpawn);
      FUN_0022a418_stamp_lead_player_flags();
      fieldCamera_.FUN_00216930_install_normal_field_defaults();
      fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
      mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
      spawnSourceLabel_ = "scene script (FUN_0025b600)";
      return;
    }

    for (const auto &record : loadedMap->DAT_003556e8_objectPlacements)
    {
      if (record.group != 2)
      {
        continue;
      }
      leadPlayer_.resetToMap(*loadedMap, record.position);
      FUN_0022a418_stamp_lead_player_flags();
      fieldCamera_.FUN_00216930_install_normal_field_defaults();
      fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
      mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
      spawnSourceLabel_ = "group 2 placement record (a guess; see --spawn)";
      return;
    }
  }

  // Resolves an entity's model and builds its bone palette for this frame.
  //
  // The column is entity +0xAC, which FUN_00225c90 walks along the animation's
  // timeline; the palette is FUN_0020c810's bone walk with FUN_0020d188's
  // filter, whose state lives in DAT_003ffe00_poseFilters_ and is why this
  // belongs to the simulation step rather than the draw.
  void PortRuntime::attachModel(SceneObjectView &view,
                                orphen::ported::entity::OriginalEntity &entity,
                                std::uint32_t frameTicks)
  {
    const EntityModelBinding *binding = modelStore_.bindingForTypeId(entity.effectiveTypeId());
    if (binding == nullptr || binding->model == nullptr)
    {
      return;
    }
    view.model = binding->model;
    view.textureSlot = binding->textureSlot;
    view.poseColumn = entity.poseColumnAc;

    orphen::ported::model::PoseFilterInputs inputs;
    // FUN_0020d378 line 51 takes the blend ratio straight off entity +0x13C.
    inputs.blendRatio1c8 = entity.animationBlend13c;
    inputs.smoothRate1cc = orphen::ported::model::FUN_0020c810_smoothing_rate(
        *binding->model, binding->model->blob, entity.animationA0);
    inputs.frameTicks = frameTicks;

    // FUN_0020c810 lines 120-128. Bit 0x10 of entity +0x08 means the entity was
    // not drawn last frame, so there is no previous pose worth easing out of:
    // both the smoothing and the override countdown snap, and the bit is
    // consumed. Otherwise the smoothing is gated on bit 0x200 alone.
    if ((entity.halfword08 & 0x0010) != 0)
    {
      inputs.skipSmoothing1fe = true;
      inputs.wasCulled1fd = true;
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 & 0xFFEFu);
    }
    else
    {
      inputs.skipSmoothing1fe = (entity.halfword08 & 0x0200) != 0;
    }

    // FUN_0020cdc0's branch on +0x192. An attached entity's +0x20..+0x28 is a
    // bone-local offset rather than a world position, so it never reaches the
    // standalone path -- feeding it there would put the bandana 8 cm above the
    // world origin.
    orphen::ported::model::Matrix4 root;
    if (entity.parentSlot192 >= 0 && entity.attachBone194 < 0)
    {
      const std::size_t parentSlot = static_cast<std::size_t>(entity.parentSlot192);
      const auto &parent = entityPool_.slot(parentSlot);
      root = orphen::ported::model::FUN_0020cdc0_attached_root(
          DAT_00357e00_bonePalettes_[parentSlot],
          static_cast<std::size_t>(-static_cast<int>(entity.attachBone194)),
          {view.position.x, view.position.y, view.position.z},
          {parent.positionX20, parent.positionZ24, parent.positionY28 + parent.height58 * 0.5f},
          view.facingRadians, view.rotationX154, view.rotationY158, view.scale, view.scaleZ150);
    }
    else if (entity.parentSlot192 >= 0)
    {
      // FUN_0020cdc0's third branch, the rigid one. This is a character's head:
      // opcode 0x140/0x141 hangs a head model off the neck bone and hides the
      // body's own head bones, and the face animation rides on the head entity.
      // The parent has already been through this pass, because
      // publishSceneObjectViews runs FUN_0020c5a8's deferral queue and defers
      // any slot whose parent has not been posed yet.
      const std::size_t parentSlot = static_cast<std::size_t>(entity.parentSlot192);
      root = orphen::ported::model::FUN_0020cdc0_rigid_attached_root(
          DAT_00357e00_bonePalettes_[parentSlot],
          static_cast<std::size_t>(entity.attachBone194),
          {view.position.x, view.position.y, view.position.z}, view.facingRadians,
          view.rotationX154, view.rotationY158, view.scale, view.scaleZ150);
    }
    else
    {
      root = orphen::ported::model::FUN_0020cdc0_entity_root(
          {view.position.x, view.position.y, view.position.z}, view.facingRadians,
          view.rotationX154, view.rotationY158, view.scale, view.scaleZ150);
    }

    // Row 3 of the root matrix, which for an attached entity is the bone point
    // rather than anything in +0x20..+0x28.
    view.worldOrigin = {root[12], root[13], root[14]};

    view.bonePalette = orphen::ported::model::FUN_0020d618_build_palette(
        *binding->model, binding->model->blob, entity.poseColumnAc, root,
        DAT_003ffe00_poseFilters_[view.slot], inputs,
        &DAT_004a7e00_boneOverrides_[view.slot]);
    // 0x00357E00 + slot * 0xA80. Kept past the frame so an attached entity, and
    // the rope simulation behind it, can read the bone it rides. This is the
    // *unhidden* palette, the way the original's bank is: FUN_0020eec0 applies
    // +0x168 on the way to VU1, not on the way into the bank, which is what lets
    // a replacement head ride a bone the same opcode just hid.
    DAT_00357e00_bonePalettes_[view.slot] = view.bonePalette;
    orphen::ported::model::FUN_0020eec0_apply_hidden_bones(
        view.bonePalette, &DAT_004a7e00_boneOverrides_[view.slot]);

    // The same +0x168 bytes the draw needs, because a zero matrix alone does
    // not make a primitive disappear off the GS. See SceneObjectView::hiddenBones.
    const auto &mode168 = DAT_004a7e00_boneOverrides_[view.slot].mode168;
    for (std::size_t bone = 0; bone < view.bonePalette.size(); ++bone)
    {
      if (bone < mode168.size() && mode168[bone] < 0)
      {
        view.hiddenBones.assign(view.bonePalette.size(), 0);
        break;
      }
    }
    if (!view.hiddenBones.empty())
    {
      for (std::size_t bone = 0; bone < view.hiddenBones.size(); ++bone)
      {
        view.hiddenBones[bone] = (bone < mode168.size() && mode168[bone] < 0) ? 1u : 0u;
      }
    }
  }

  // FUN_00225c90 for every entity that has a model, run before the views are
  // published so the pose column the renderer reads is this frame's.
  void PortRuntime::advanceEntityAnimations(std::uint32_t frameTicks)
  {
    const auto step = [&](orphen::ported::entity::OriginalEntity &entity) {
      const EntityModelBinding *binding = modelStore_.bindingForTypeId(entity.effectiveTypeId());
      if (binding == nullptr || binding->model == nullptr)
      {
        return;
      }
      orphen::ported::model::FUN_00225c90_advance_animation(entity, *binding->model, frameTicks);
    };

    step(entityPool_.leadPlayer());
    entityPool_.forEachActiveMutable(
        [&](std::size_t slot, orphen::ported::entity::OriginalEntity &entity) {
          if (entityPool_.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
          {
            return;
          }
          step(entity);
        });
  }

  // FUN_00228e28's cue table and FUN_00205118's three banks. Both are optional:
  // a disc root without SND.BIN still runs, it just reports every cue as
  // "bank not loaded" rather than playing it.
  void PortRuntime::loadSoundData()
  {
    if (discRoot_.empty())
    {
      return;
    }

    orphen::harness::FlatBinArchive scr;
    if (scr.open(discRoot_ / "SCR.BIN"))
    {
      const std::vector<std::uint8_t> resource = scr.decode(kScrSoundCueResource);
      if (resource.empty() || !soundEngine_.FUN_00228e28_load_cue_table(resource))
      {
        std::cout << "[snd] SCR.BIN resource " << kScrSoundCueResource
                  << " unreadable: " << soundEngine_.diagnostic() << '\n';
      }
      // The same resource carries the three music category tables; FUN_00228e28
      // builds both out of it in one pass.
      else if (!soundEngine_.loadMusicTables(resource))
      {
        std::cout << "[snd] music tables unreadable: " << soundEngine_.diagnostic() << '\n';
      }
    }

    orphen::harness::FlatBinArchive snd;
    if (!snd.open(discRoot_ / "SND.BIN"))
    {
      std::cout << "[snd] SND.BIN missing from the disc root -- cues resolve but stay silent\n";
      return;
    }
    for (std::size_t bank = 0; bank < orphen::ported::sound::kBankCount; ++bank)
    {
      // SND resources are stored uncompressed; see FlatBinArchive::raw.
      const std::vector<std::uint8_t> resource =
          snd.raw(orphen::ported::sound::kBootBankResources[bank]);
      if (resource.empty() || !soundEngine_.FUN_00205310_load_bank(bank, resource))
      {
        std::cout << "[snd] bank " << bank << " (SND.BIN resource "
                  << orphen::ported::sound::kBootBankResources[bank]
                  << ") did not load: " << soundEngine_.diagnostic() << '\n';
      }
    }
  }

  // FUN_0025b2f0 and FUN_00206840 together: the scene's own music.
  //
  // FUN_0025b2f0 copies 16 bytes -- eight u16 requests -- from the scene script
  // header's word 10 into DAT_0031e678. FUN_00206840 then walks them: slot i
  // draws from music category min(i, 2), the low 15 bits index that category's
  // table, and bit 15 means "and start it now". Everything else is loaded ready
  // for a later opcode 0x129 to play.
  //
  // In s01_e012 slot 2 is 0x801A: category 2, index 26, play -- SND.BIN resource
  // 112, whose sequence is a single held note between a pair of loop markers.
  // That is the wind that runs under the whole scene.
  void PortRuntime::startSceneMusic()
  {
    if (discRoot_.empty() || !sceneScript_.loaded())
    {
      return;
    }

    const std::uint32_t table = sceneScript_.headerWord(10);
    const std::span<const std::uint8_t> blob = sceneScript_.blob();
    if (table == 0 || table + kSceneMusicRequests * 2 > blob.size())
    {
      return;
    }

    orphen::harness::FlatBinArchive snd;
    const bool haveArchive = snd.open(discRoot_ / "SND.BIN");

    for (std::size_t slot = 0; slot < kSceneMusicRequests; ++slot)
    {
      const std::size_t at = table + slot * 2;
      const auto request = static_cast<std::uint16_t>(blob[at] | (blob[at + 1] << 8));
      const auto index = static_cast<std::uint16_t>(request & 0x7FFF);
      const bool autoPlay = (request & 0x8000) != 0;
      const std::size_t category = orphen::ported::sound::musicCategoryForSlot(slot);

      orphen::ported::sound::SoundEngine::MusicSlotLog log;
      log.slot = slot;
      log.category = category;
      log.request = request;
      log.index = index;
      log.autoPlayed = autoPlay;

      // DAT_00356a18. FUN_00205778 searches these to resolve a cue's
      // alternate bank, so it has to be recorded even for a slot that fails to
      // load -- and before that failure can `continue` past it.
      soundEngine_.setSlotRequestIndex(slot, index);

      const auto *record = soundEngine_.musicRecord(category, index);
      if (record == nullptr)
      {
        // FUN_0025b2f0:18 treats this as fatal; here it is worth reporting and
        // carrying on, because it means the tables did not parse.
        log.outcome = "no such music record";
        soundEngine_.logMusicSlot(log);
        continue;
      }
      log.sndResource = record->sndResource;
      log.volume = record->volume;

      if (!haveArchive)
      {
        log.outcome = "SND.BIN missing";
        soundEngine_.logMusicSlot(log);
        continue;
      }

      // SND resources are stored uncompressed; see FlatBinArchive::raw.
      const std::vector<std::uint8_t> resource = snd.raw(record->sndResource);
      if (resource.empty())
      {
        log.outcome = "resource unreadable";
        soundEngine_.logMusicSlot(log);
        continue;
      }
      if (!soundEngine_.FUN_00205938_load_slot(slot, record->sndResource, record->volume, resource))
      {
        // A bank whose section 2 is "NSEQ" carries no sequence at all. That is
        // normal for a sound-effect bank and not an error.
        log.outcome = soundEngine_.slotHasSequence(slot) ? "bank unreadable" : "no sequence";
        soundEngine_.logMusicSlot(log);
        continue;
      }

      if (autoPlay)
      {
        // FUN_00206840:53 passes 1000 -- the fader wide open on the record's own
        // volume.
        soundEngine_.FUN_00205d90_play_slot(
            slot, orphen::ported::sound::SequencePlayer::kFaderFull);
        log.outcome = "loaded and playing";
      }
      else
      {
        log.outcome = "loaded";
      }
      soundEngine_.logMusicSlot(log);
    }
  }

  // FUN_00221b90's table, without which a line of dialogue has no length. Tried
  // in the order the data is most trustworthy: the real file first, then an EE
  // dump, which holds the same table because the game loads it at boot.
  //
  // VOICE.BIN is 142 MiB and is usually the archive people leave behind when
  // they pull the disc apart; the table inside it is 13 KB. Falling back to a
  // dump means a repo with no VOICE.BIN still gets exact cutscene timing, and
  // only loses the audio -- which is not played yet in any case.
  void PortRuntime::loadVoiceIndex(const PortRuntimeConfig &config)
  {
    std::vector<std::filesystem::path> candidates;
    if (!config.voiceIndexPath.empty())
    {
      candidates.push_back(config.voiceIndexPath);
    }
    else if (!discRoot_.empty())
    {
      candidates.push_back(discRoot_ / "VOICE.BIN");
      candidates.push_back(discRoot_ / "eeMemory.bin");
      candidates.push_back(discRoot_ / "s01_e24.bin");
    }

    for (const std::filesystem::path &candidate : candidates)
    {
      if (!std::filesystem::exists(candidate))
      {
        continue;
      }
      const bool ok = voiceIndex_.loadFromVoiceBin(candidate) || voiceIndex_.loadFromEeDump(candidate);
      if (!ok)
      {
        std::cout << "[voice] " << candidate.filename().string()
                  << " holds no readable voice table\n";
        continue;
      }
      std::cout << "[voice] " << voiceIndex_.FUN_00221c40_entryCount() << " clips from "
                << voiceIndex_.source() << " at " << orphen::ported::sound::kVoiceSampleRate
                << " Hz\n";
      if (!voiceIndex_.diagnostic().empty())
      {
        std::cout << "[voice] " << voiceIndex_.diagnostic() << '\n';
      }
      break;
    }

    if (!voiceIndex_.valid())
    {
      std::cout << "[voice] no voice table found -- dialogue holds fall back to a "
                   "per-character estimate\n";
    }
    else if (!voiceIndex_.hasAudio())
    {
      std::cout << "[voice] lengths only; VOICE.BIN itself is needed to hear a line\n";
    }
    dialogueStream_.setVoiceIndex(&voiceIndex_);
    voiceAudioEnabled_ = config.audio || !config.soundDumpPath.empty();
    soundEngine_.setMusicSolo(config.musicSolo);
    if (config.noSubprocDisplay)
    {
      DAT_003555dd_debugDisplay_ &=
          static_cast<std::uint8_t>(~orphen::ported::script::ScriptEnvironment::kSubprocDisplayBit);
    }
  }

  namespace
  {
    // FUN_0020c5a8's *first* pass, the one that decides which pool slots even
    // become draw candidates:
    //
    //   if ((char)(&DAT_005a96b0)[slot] < 1)      -> 0xff, not drawn
    //   else if ((puVar11[1] & 0x200) == 0)       -> enqueued
    //   else                                      -> 0xff, not drawn
    //
    // `puVar11` walks the pool as `undefined2*` with a 0xEC-halfword stride, so
    // `puVar11[1]` is the halfword at **+0x02** -- descriptorFlags02, seeded
    // from type descriptor +0x04. Bit 0x200 means "never submit this entity's
    // model".
    //
    // The port had only the second pass's +0x08 bit 0 test, so every slot the
    // descriptors mark undrawable was being drawn. In s01_e012 that is a stack
    // of effect and reserve entities parked at the world origin, which the
    // Dortin/Volcan shot looks straight through.
    //
    // Unlike the +0x08 path this raises no bit: the skip happens before the
    // pass that maintains +0x08, so a 0x200 slot simply never appears.
    bool FUN_0020c5a8_isUndrawable(const orphen::ported::entity::OriginalEntity &entity)
    {
      return (entity.descriptorFlags02 & 0x0200u) != 0;
    }

    // FUN_0020c5a8:66-99, the +0x08 bit 0 branch. A hidden slot gets three
    // writes and **no status byte**, which is the part that matters: the
    // original leaves it on 0 ("queued"), so every child of it keeps deferring
    // and is never drawn. Writing 1 here instead would publish the children of
    // something that is not on screen.
    //
    // The `+0xB0 = 0` the original also does is the pose blend's "no previous
    // column" -- FUN_0020e840:60 reads it beside +0xAA -- which the port's pose
    // filter tracks in its own state, so there is no field to write here.
    void FUN_0020c5a8_hide(orphen::ported::entity::OriginalEntity &entity)
    {
      entity.collisionFlags0c &= 0xFFFFCFFFu;
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0010u);
    }
  } // namespace

  void PortRuntime::publishSceneObjectViews(std::uint32_t frameTicks)
  {
    SceneObjectViewList views;

    // Slot 0 is not in forEachActive's range, and the lead player had no model
    // for the same reason it has no entry in the actor report. FUN_0020c5a8
    // walks all 256 slots; this list is what stands in for that walk, so the
    // lead player belongs in it.
    //
    // **Slot 0 must be posed before the walk**, because slot 4 -- the bandana --
    // reads slot 0's palette out of DAT_00357e00_bonePalettes_ to find the
    // neck, and this is the pass that writes it. That is a port detail; in the
    // original slot 0 is simply the first entry the first pass queues, so it is
    // posed first anyway. What it must *not* do is skip the status array -- see
    // the branches below.
    constexpr std::uint8_t kNotQueued = 0xFF;
    constexpr std::uint8_t kQueued = 0;
    constexpr std::uint8_t kPosed = 1;
    std::array<std::uint8_t, orphen::ported::entity::kEntitySlotCount> drawStatus;
    drawStatus.fill(kNotQueued);

    {
      auto &lead = entityPool_.leadPlayer();
      SceneObjectView view;
      view.slot = 0;
      view.typeId = lead.typeId00;
      view.modelIndex = lead.modelIndex;
      view.position = {lead.positionX20, lead.positionZ24, lead.positionY28};
      view.worldOrigin = view.position;
      view.facingRadians = lead.facingRadians5c;
      view.radius = lead.radius54;
      view.height = lead.height58;
      view.groundHeight = lead.groundHeight4c;
      view.descriptorResolved = lead.modelIndex >= 0;
      view.fadeLevel = lead.fadeLevel134;
      view.scale = lead.scale14c;
      view.scaleZ150 = lead.scaleZ150;
      view.rotationX154 = lead.rotationX154;
      view.rotationY158 = lead.rotationY158;
      view.drawDebugBox = false;
      // The palette is built either way: slot 4, the bandana, reads slot 0's to
      // find the neck, and FUN_0020c5a8's hidden test is a *draw* skip -- the
      // pose work has already happened by then.
      attachModel(view, lead, frameTicks);
      if (FUN_0020c5a8_isUndrawable(lead))
      {
        // No +0x08 bit 0x10 here: the 0x200 skip happens in the first pass,
        // before the pass that raises it. See FUN_0020c5a8_isUndrawable.
        //
        // **And slot 0 keeps that status**, which is the whole point of writing
        // it down rather than assuming. FUN_0020c5a8 gives the lead no special
        // treatment: it is queue entry 0 like anything else, and a child whose
        // parent reads 0xFF is dropped with it. Slot 4 is such a child. When
        // the doorway scene swaps Orphen for the close-up rig it hides the
        // field model by ORing 0x200 into its +0x02 -- and the port, which
        // pinned slot 0 to `kPosed`, went on drawing the bandana alone in the
        // doorway for the rest of the cutscene.
        drawStatus[0] = kNotQueued;
      }
      else if ((lead.halfword08 & 1) != 0)
      {
        FUN_0020c5a8_hide(lead);
        // The original's `& 1` branch never writes the status byte, so the slot
        // stays *queued* -- children keep deferring against it until the byte
        // counter wraps and the walk ends. Not drawn, by a different route.
        drawStatus[0] = kQueued;
      }
      else
      {
        // LAB_0020c73c, on the way into FUN_0020c810.
        lead.collisionFlags0c &= 0xFFFFCFFFu;
        views.push_back(view);
        drawStatus[0] = kPosed;
      }
    }

    // **FUN_0020c5a8's deferral queue**, which the port used to do without.
    //
    // The original does not walk the pool in slot order. Its first pass fills a
    // work queue with every live slot whose +0x02 bit 0x200 is clear and marks
    // each 0 ("queued"); every other slot is marked 0xFF. The second pass then
    // walks that queue *while it grows*:
    //
    //   parent < 0                  -> pose and draw, mark 1
    //   status[parent] == 0         -> push this slot on the back and move on
    //   status[parent] == 1         -> pose and draw, mark 1
    //   status[parent] == 0xFF      -> neither; the slot is dropped this frame
    //
    // so a child whose parent has not been posed yet is deferred until it has.
    // Slot order happens to be right for the head-on-neck attachments in
    // s01_e012's opening, which is why ascending order held up this long, but it
    // is wrong the moment a parent sits above its child. FUN_002d2f40 does
    // exactly that: it allocates the head (0x27) *before* the body (0x26), so
    // the head takes the lower slot and was being posed against a palette its
    // parent had not built yet -- falling back to the parent's own +0x20, which
    // for a bone-local attachment is (0, 0, 0). That is the close-up head
    // sitting at the world origin.
    std::vector<std::size_t> queue;
    queue.reserve(orphen::ported::entity::kEntitySlotCount);
    entityPool_.forEachActiveMutable(
        [&](std::size_t slot, orphen::ported::entity::OriginalEntity &entity)
        {
          if (FUN_0020c5a8_isUndrawable(entity))
          {
            return;
          }
          drawStatus[slot] = kQueued;
          queue.push_back(slot);
        });

    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor)
    {
      const std::size_t slot = queue[cursor];
      auto &entity = entityPool_.slot(slot);

      // FUN_0020c5a8:66. The hidden test is the outer `if`, ahead of the parent
      // logic, and it leaves the status byte alone -- so this has to happen
      // here rather than inside the publish helper, where the loop was writing
      // "posed" over it afterwards. That is what put slot 61, the sack bound to
      // Dortin's hand, in the middle of s01_e012 at the world origin while its
      // parent was hidden.
      if ((entity.halfword08 & 1) != 0)
      {
        FUN_0020c5a8_hide(entity);
        continue;
      }

      // The parent test comes before everything else the original does per
      // slot, because a deferred slot must not be hidden or drawn yet.
      const std::int16_t parent = entity.parentSlot192;
      if (parent >= 0)
      {
        const std::uint8_t parentStatus = drawStatus[static_cast<std::size_t>(parent)];
        if (parentStatus == kQueued)
        {
          // Defer: the original pushes the slot on the back of the same queue.
          // Guarded against a parent cycle, which the original's byte-sized
          // queue length would simply wrap through.
          if (queue.size() < queue.capacity())
          {
            queue.push_back(slot);
          }
          continue;
        }
        if (parentStatus == kNotQueued)
        {
          // The parent is not drawable this frame, so neither branch of the
          // original fires and the child is dropped with it.
          continue;
        }
      }

      publishOneSceneObjectView(views, slot, entity, frameTicks);
      drawStatus[slot] = kPosed;
    }

    mapViewer_.setSceneObjectViews(std::move(views));
    mapViewer_.setTextureSlotCache(&modelStore_.textureSlots());

    // FUN_0020f3e0, the second pass. It runs off the same poses this one just
    // published, so it belongs here rather than beside the draw.
    publishSpriteQuads(frameTicks);
  }

  // FUN_0020f3e0: the *other* pass over the pool, for the entities
  // FUN_0020c5a8 refused.
  //
  // Its guards are the mirror image of the first pass's -- a live type id,
  // +0x02 bit 0x200 **set**, and +0x08 bit 0 clear -- so between them the two
  // passes partition all 256 slots. It also clears +0x0C bit 0x1000 on the way
  // in, which FUN_0020f510 sets back on anything it actually submits; nothing
  // else reads that bit, so it is a "was drawn this frame" latch.
  //
  // Ordering is the pool's, not a depth sort. The original submits into a
  // depth-bucketed display list at the tail of FUN_0020f510 rather than sorting
  // here, and this port leans on the depth buffer plus back-to-front within a
  // strip instead -- which is the same answer for the additive sprites that are
  // all this scene has.
  void PortRuntime::publishSpriteQuads(std::uint32_t frameTicks)
  {
    std::vector<orphen::ported::render::SpriteQuad> quads;
    const auto &viewProjection = renderCamera_;

    // FUN_0020f3e0:0x0020f42c stages `max(DAT_0035566c, DAT_00355670)` per
    // channel, reversed -- the scene ambient against light 0's colour. Both are
    // GS bytes where 0x80 is 1.0, and the port keeps them as floats in the same
    // units, so they scale back by 128.
    const auto &lighting = mapViewer_.sceneLighting();
    std::uint8_t ambient[3];
    for (int channel = 0; channel < 3; ++channel)
    {
      const float ambientChannel = lighting.ambient[channel];
      const float lightChannel = lighting.lightColour[0][channel];
      const float best = ambientChannel > lightChannel ? ambientChannel : lightChannel;
      // SceneLighting::unpack keeps the raw GS bytes, so this is already in the
      // units FUN_0020f510 averages in.
      const int level = static_cast<int>(best);
      ambient[channel] = static_cast<std::uint8_t>(level < 0 ? 0 : (level > 0xFF ? 0xFF : level));
    }

    entityPool_.forEachActiveMutable(
        [&](std::size_t slot, orphen::ported::entity::OriginalEntity &entity)
        {
          if (entity.typeId00 == 0 || (entity.descriptorFlags02 & 0x0200u) == 0 ||
              (entity.halfword08 & 1) != 0)
          {
            return;
          }
          entity.collisionFlags0c &= 0xFFFFEFFFu;

          const EntityModelBinding *binding =
              modelStore_.bindingForTypeId(entity.effectiveTypeId());
          if (binding == nullptr || binding->model == nullptr || !binding->model->spriteStrip)
          {
            return;
          }

          // FUN_0020f510:0x0020f594. Bit 0x1000 takes the screen-space branch
          // and bit 0x400 the rotated-corner one; neither is ported, and no
          // descriptor either scene spawns sets them. Rejecting here rather
          // than drawing the wrong thing.
          if ((entity.halfword08 & 0x1400u) != 0)
          {
            return;
          }

          // FUN_0020f510:0x0020f56c. The near clip is DAT_0035209c and the far
          // one is the scene's draw distance; both are strict.
          const orphen::ported::psm2::Vec3 world{entity.positionX20, entity.positionZ24,
                                                 entity.positionY28};
          const auto viewSpace = viewProjection.toViewSpace(world);
          if (viewSpace.z <= orphen::ported::render::kDAT_0035209c_spriteNearClip ||
              viewSpace.z >= mapViewer_.drawDistance())
          {
            return;
          }

          const float projectionScaleX = viewProjection.projection.at(0, 0);
          const float projectionScaleY = viewProjection.projection.at(1, 1);
          const float screenCentreX = viewProjection.projection.at(2, 0);
          const float screenCentreY = viewProjection.projection.at(2, 1);

          // FUN_0020b600 (0x0020b600): the VU0 transform-and-project, whose
          // vftoi0 truncates the result to integer GS 12.4 units. This is the
          // origin every corner of every quad is built on top of.
          const int gsOriginX = static_cast<int>(
              viewSpace.x * projectionScaleX / viewSpace.z + screenCentreX);
          const int gsOriginY = static_cast<int>(
              viewSpace.y * projectionScaleY / viewSpace.z + screenCentreY);

          // 0x0020f594-0x0020f5cc: a generous window about the GS centre, so a
          // sprite whose origin has left the screen but whose quad has not is
          // still drawn.
          if (gsOriginX < orphen::ported::render::kSpriteCullMinX ||
              gsOriginX > orphen::ported::render::kSpriteCullMaxX ||
              gsOriginY < orphen::ported::render::kSpriteCullMinY ||
              gsOriginY > orphen::ported::render::kSpriteCullMaxY)
          {
            return;
          }

          orphen::ported::render::SpriteBuildInputs inputs;
          inputs.scaleX14c = entity.scale14c;
          inputs.scaleY150 = entity.scaleZ150;
          inputs.gsOriginX = gsOriginX;
          inputs.gsOriginY = gsOriginY;
          inputs.projectionScaleX = projectionScaleX;
          inputs.projectionScaleY = projectionScaleY;
          inputs.screenCentreX = screenCentreX;
          inputs.screenCentreY = screenCentreY;
          // FUN_0020f3e0:0x0020f4a0 stages G = 2 * DAT_00355658 at workspace
          // +0x84 -- the camera zoom, not a constant.
          inputs.projectionG = 2.0f * fieldCamera_.fGpffffb6e8_zoomLog2();
          inputs.viewZ = viewSpace.z;
          // 0x0020f6f8: entity +0x133 scaled by DAT_003520a0, staged at +0x88.
          inputs.entityDepthBias =
              static_cast<float>(entity.depthBias133) *
              orphen::ported::render::kDAT_003520a0_entityDepthBiasScale;
          // FUN_0020f510:0x0020f544 latches +0x08 bit 0x40 into the workspace;
          // the record loop reads it back to pin the GS z at 0xFFFF.
          inputs.forceFront = (entity.halfword08 & 0x0040u) != 0;
          inputs.textureSlot = binding->textureSlot;
          inputs.ambient[0] = ambient[0];
          inputs.ambient[1] = ambient[1];
          inputs.ambient[2] = ambient[2];
          // +0x08 bit 0x4000 takes FUN_0020f510's flat-0x80 branch and skips
          // the lighting entirely.
          inputs.flatColour = (entity.halfword08 & 0x4000u) != 0;
          if (!inputs.flatColour)
          {
            // The VU0 point-light contribution at the sprite's own position,
            // the same call the map draw makes per vertex.
            const auto &sceneLighting = mapViewer_.sceneLighting();
            float additive[3] = {0.0f, 0.0f, 0.0f};
            if (sceneLighting.pointLightCount != 0)
            {
              sceneLighting.FUN_0020b430_pointLightBytes(world, 0, additive);
            }
            for (int channel = 0; channel < 3; ++channel)
            {
              const int level = static_cast<int>(additive[channel]);
              inputs.dynamic[channel] =
                  static_cast<std::uint8_t>(level < 0 ? 0 : (level > 0xFF ? 0xFF : level));
            }
          }

          orphen::ported::render::FUN_0020f510_build_quads(*binding->model, entity.poseColumnAc,
                                                           inputs, quads);
          entity.collisionFlags0c |= 0x1000u;
        });

    // FUN_002d3218:11 draws each live particle through FUN_002d3058 in the same
    // walk that steps it, in pool order. The port steps during the sim and
    // collects here instead, which puts the same particles in the same order in
    // the same bucket -- FUN_002d3058's own guard is only FUN_0020b600's clip
    // flags, with no near, far or window test of the kind the sprites get.
    if (DAT_00355620_particles_.DAT_00355e0c_behaviour() !=
        orphen::ported::entity::ParticleBehaviour::None)
    {
      const float projectionScaleX = viewProjection.projection.at(0, 0);
      const float projectionScaleY = viewProjection.projection.at(1, 1);
      const float screenCentreX = viewProjection.projection.at(2, 0);
      const float screenCentreY = viewProjection.projection.at(2, 1);

      for (const auto &particle : DAT_00355620_particles_.particles())
      {
        if (particle.alive1c == 0)
        {
          continue;
        }
        const orphen::ported::psm2::Vec3 world{particle.x00, particle.y04, particle.z08};
        const auto viewSpace = viewProjection.toViewSpace(world);
        // FUN_0020b600 clamps w rather than rejecting it, and FUN_002d3058 only
        // drops the particle on the clip flags. Behind the eye is the one case
        // the clamp cannot save, and drawing it would smear a spark across the
        // screen, so it is the one thing rejected here.
        if (viewSpace.z <= orphen::ported::render::kDAT_0035209c_spriteNearClip)
        {
          continue;
        }

        orphen::ported::render::ParticleQuadInputs inputs;
        inputs.gsOriginX = static_cast<int>(
            viewSpace.x * projectionScaleX / viewSpace.z + screenCentreX);
        inputs.gsOriginY = static_cast<int>(
            viewSpace.y * projectionScaleY / viewSpace.z + screenCentreY);
        inputs.viewZ = viewSpace.z;
        inputs.projectionScaleX = projectionScaleX;
        inputs.projectionScaleY = projectionScaleY;
        inputs.screenCentreX = screenCentreX;
        inputs.screenCentreY = screenCentreY;
        inputs.widthUnits = particle.widthUnits1e;
        inputs.heightUnits = particle.heightUnits20;
        inputs.colour = particle.colour18;

        quads.push_back(orphen::ported::render::FUN_002d3058_build_particle_quad(inputs));
      }
    }

    // FUN_00220910, the hit sparks, in the slot FUN_002192c0 gives it: the
    // *draw* phase, after both entity passes. That is why they step here and
    // the DAT_00355620 pool steps beside the actor loop -- the original runs
    // them in different halves of the frame. It also steps and draws one spark
    // at a time in a single walk, so a spark that ages out this frame is not
    // drawn; stepping the whole pool first and then collecting the survivors
    // leaves the same set in the same order, because the ten groups own
    // contiguous slices in index order.
    DAT_00355b74_hitSparks_.FUN_00220910_step(frameTicks);
    if (DAT_00355b74_hitSparks_.DAT_00355b7c_activeGroups() > 0)
    {
      for (const auto &spark : DAT_00355b74_hitSparks_.sparks())
      {
        if (!spark.alive())
        {
          continue;
        }

        const auto streak =
            orphen::ported::entity::FUN_00220c00_build_quad(spark, renderCameraYaw_);

        orphen::ported::render::SpriteQuad quad;
        quad.oriented = true;
        bool visible = true;
        for (int corner = 0; corner < 4; ++corner)
        {
          const auto view = viewProjection.toViewSpace(streak.corners[corner]);
          // FUN_00218ee0. One corner too near the eye drops the whole quad --
          // the original tests all four W lanes and rejects on any of them.
          if (!(view.z >= orphen::ported::entity::kHitSparkMinViewDepth))
          {
            visible = false;
            break;
          }
          quad.cornerX[corner] = view.x;
          quad.cornerY[corner] = view.y;
          quad.cornerZ[corner] = view.z;
          quad.cornerU[corner] =
              orphen::ported::entity::kHitSparkTexels[streak.texelRectangle][corner][0];
          quad.cornerV[corner] =
              orphen::ported::entity::kHitSparkTexels[streak.texelRectangle][corner][1];
        }
        if (!visible)
        {
          continue;
        }

        // FUN_002190f8's param_4, the same 0xF0F0F0F0 on all four vertices.
        for (int channel = 0; channel < 4; ++channel)
        {
          quad.colour[channel] =
              static_cast<float>((orphen::ported::entity::kHitSparkColour >> (channel * 8)) & 0xFFu) /
              128.0f;
        }
        quad.blendMode = orphen::ported::entity::kHitSparkBlendMode;
        quad.textureSlot = orphen::ported::entity::kHitSparkTextureSlot;
        quad.displayListBucket = orphen::ported::entity::kHitSparkDisplayListBucket;
        quad.depthTest = true;
        quads.push_back(quad);
      }
    }

    // FUN_0020e840's ribbons, built during the pose walk. They reach the GS
    // through the same display list as everything else here, so they are
    // converted once the camera is in hand and then sorted with the rest.
    for (const auto &ribbon : pendingTrailRibbons_)
    {
      orphen::ported::render::SpriteQuad quad;
      quad.oriented = true;
      quad.untextured = true;
      bool visible = true;
      for (int corner = 0; corner < 4; ++corner)
      {
        const auto view = viewProjection.toViewSpace(ribbon.quad.corner[corner]);
        // FUN_0020d820's per-point test, `z < 0xFFFE`. The projection puts the
        // near plane at exactly 65534, so this is "in front of the near plane"
        // written in screen units, and one corner failing drops the quad.
        if (!(view.z > 0.0f) ||
            !(viewProjection.screenDepth(view.z) <
              static_cast<float>(orphen::ported::render::weaponTrail::kScreenZAtNearPlane)))
        {
          visible = false;
          break;
        }
        quad.cornerX[corner] = view.x;
        quad.cornerY[corner] = view.y;
        quad.cornerZ[corner] = view.z;
        for (int channel = 0; channel < 4; ++channel)
        {
          quad.cornerColour[corner][channel] = ribbon.quad.colour[corner][channel];
        }
      }
      if (!visible)
      {
        continue;
      }
      quad.blendMode = orphen::ported::render::weaponTrail::kBlendMode;
      quad.textureSlot = -1;
      quad.displayListBucket = ribbon.displayListBucket;
      quad.depthTest = true;
      quads.push_back(quad);
    }
    pendingTrailRibbons_.clear();

    // FUN_0020f510:0x00210170 pushes each finished packet into one of 4096
    // depth buckets, or into 0x1005 when the entity asked to be drawn over
    // everything, and the list is walked in ascending bucket order -- back to
    // front. A stable sort on the bucket reproduces that, and keeps the
    // back-to-front order the record loop already established within a strip.
    //
    // The one thing it does not reproduce is that the original links each
    // packet in at the *head* of its bucket, so co-bucketed sprites come out
    // reversed among themselves. That is invisible for additive blending, which
    // is what every sprite this port draws so far uses.
    std::stable_sort(quads.begin(), quads.end(),
                     [](const orphen::ported::render::SpriteQuad &a,
                        const orphen::ported::render::SpriteQuad &b)
                     { return a.displayListBucket < b.displayListBucket; });

    mapViewer_.setSpriteQuads(std::move(quads));
  }

  void PortRuntime::publishOneSceneObjectView(SceneObjectViewList &views,
                                              std::size_t slot,
                                              orphen::ported::entity::OriginalEntity &entity,
                                              std::uint32_t frameTicks)
  {
          if (!hideSlots_.empty() &&
              std::find(hideSlots_.begin(), hideSlots_.end(), static_cast<int>(slot)) !=
                  hideSlots_.end())
          {
            return;
          }

          // The +0x08 bit 0 test used to live here. It belongs in the walk --
          // see FUN_0020c5a8_hide -- because the status byte it must *not*
          // write is the walk's, not this helper's.
          //
          // LAB_0020c73c, immediately before FUN_0020c810.
          entity.collisionFlags0c &= 0xFFFFCFFFu;

          SceneObjectView view;
          view.slot = slot;
          view.typeId = entity.typeId00;
          view.modelIndex = entity.modelIndex;
          view.position = {entity.positionX20, entity.positionZ24, entity.positionY28};
          view.worldOrigin = view.position;
          view.facingRadians = entity.facingRadians5c;
          view.radius = entity.radius54;
          view.height = entity.height58;
          view.groundHeight = entity.groundHeight4c;
          view.descriptorResolved = entity.modelIndex >= 0;
          view.fadeLevel = entity.fadeLevel134;
          view.scale = entity.scale14c;
          view.scaleZ150 = entity.scaleZ150;
          view.rotationX154 = entity.rotationX154;
          view.rotationY158 = entity.rotationY158;
          attachModel(view, entity, frameTicks);

          // FUN_0020c810's last call, after the bone palette is composed. The
          // ribbon is built here rather than in the draw so the history
          // advances once per simulation step, which is what the original does
          // -- FUN_0020c5a8 runs inside the frame function -- and what keeps
          // --frames reproducible.
          if (view.model != nullptr && !view.model->trails.empty())
          {
            std::vector<orphen::ported::render::TrailQuad> ribbons;
            DAT_004fbc7c_weaponTrails_.FUN_0020e840_step(*view.model,
                                                         view.bonePalette,
                                                         entity.flagsAa,
                                                         entity.previousTrailMaskB0,
                                                         entity.trailSlotsB1,
                                                         ribbons);
            if (!ribbons.empty())
            {
              const int bucket = orphen::ported::render::FUN_0020eec0_depthBucket(
                  view.worldOrigin, renderCamera_);
              for (auto &ribbon : ribbons)
              {
                pendingTrailRibbons_.push_back({ribbon, bucket + 1});
              }
            }
          }

          views.push_back(view);
  }

  void PortRuntime::printScriptReport() const
  {
    std::cout << "\n=== scene script report ===\n";
    if (!sceneScript_.loaded())
    {
      std::cout << "no script loaded\n";
      return;
    }

    std::cout << "header:";
    for (std::size_t index = 0; index < orphen::ported::script::kSceneScriptHeaderWordCount; ++index)
    {
      std::cout << " [" << index << "]=0x" << std::hex << sceneScript_.headerWord(index) << std::dec;
    }
    std::cout << "\nentries run:";
    for (const std::string &entry : scriptTrace_.entriesRun())
    {
      std::cout << ' ' << entry;
    }
    std::cout << '\n';

    std::cout << "texture pages:";
    for (std::uint16_t pageId : sceneScript_.texturePageIds())
    {
      std::cout << " 0x" << std::hex << pageId << std::dec;
    }
    std::cout << "\npreloaded resources:";
    for (std::uint16_t resourceId : scriptTrace_.preloadedResources())
    {
      std::cout << " 0x" << std::hex << resourceId << std::dec;
    }
    std::cout << '\n';

    std::cout << "opcodes reached:\n";
    for (const auto &entry : scriptTrace_.opcodes())
    {
      std::cout << "  0x" << std::hex << entry.first << std::dec
                << "  hits=" << entry.second.hitCount
                << "  first=0x" << std::hex << entry.second.firstOffset << std::dec
                << "  " << orphen::ported::script::opcodeSupportName(entry.second.support) << '\n';
    }
    // Two different claims, so two different counters. "operands-only" means the
    // stream stayed in sync and the scene kept running past an effect the port
    // does not reproduce; "unimplemented" means the stream stopped.
    std::cout << "operands-only: " << scriptTrace_.operandsOnlyOpcodeCount() << " distinct, "
              << scriptTrace_.operandsOnlyHitCount() << " hits\n";
    std::cout << "unimplemented: " << scriptTrace_.unimplementedOpcodeCount() << " distinct, "
              << scriptTrace_.unimplementedHitCount() << " hits\n";

    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      std::cout << "map object placements: " << loadedMap->DAT_003556e8_objectPlacements.size() << '\n';
      for (std::size_t index = 0; index < loadedMap->DAT_003556e8_objectPlacements.size(); ++index)
      {
        const auto &record = loadedMap->DAT_003556e8_objectPlacements[index];
        std::cout << "  #" << index << " pos=(" << record.position.x << ", " << record.position.y
                  << ", " << record.position.z << ")"
                  << " angle=" << static_cast<int>(record.angle)
                  << " group=" << static_cast<int>(record.group)
                  << " id=" << static_cast<int>(record.id)
                  << " param=" << static_cast<int>(record.param) << '\n';
      }
    }

    // DAT_00355060. A cutscene keeps its cross-frame state here -- most often
    // the pool index of something it spawned, which later opcodes address it by
    // -- so a wrong value shows up as a script waiting on the wrong entity.
    {
      const auto &work = sceneScript_.state().DAT_00355060_work;
      std::cout << "work memory (non-zero):";
      bool any = false;
      for (std::size_t index = 0; index < orphen::ported::script::SceneScriptState::kWorkWordCount; ++index)
      {
        if (work[index] != 0)
        {
          std::cout << "  [" << index << "]=" << work[index];
          any = true;
        }
      }
      std::cout << (any ? "" : "  (all zero)") << '\n';
    }

    // Every line the scene put up, and what set its length. `estimated` lines
    // are the ones with no clip behind them: their pacing is invented, so a
    // scene that reports any is a scene whose cutscene timing is not faithful.
    {
      const auto &lines = dialogueStream_.log();
      std::cout << "dialogue lines: " << lines.size() << "  (" << dialogueStream_.measuredLines()
                << " timed by their voice clip, " << dialogueStream_.estimatedLines()
                << " estimated, " << dialogueStream_.emptyLines() << " empty)\n";
      if (dialogueStream_.typewriterHeldLines() != 0)
      {
        std::cout << "  " << dialogueStream_.typewriterHeldLines()
                  << " held open past the clip until the walk reached the terminator, "
                  << dialogueStream_.typewriterHeldFrames() << " frames in total\n";
      }
      const auto &unhandled = dialogueStream_.window().unhandledCodes();
      if (!unhandled.empty())
      {
        std::cout << "  glyph walk skipped control codes:";
        for (std::uint8_t code : unhandled)
        {
          std::cout << " 0x" << std::hex << static_cast<int>(code) << std::dec;
        }
        std::cout << '\n';
      }
      std::uint32_t heldFrames = 0;
      for (const auto &entry : lines)
      {
        heldFrames += entry.holdFrames;
        std::cout << "  frame " << entry.frame << "  @0x" << std::hex << entry.recordOffset
                  << std::dec << "  voice " << entry.voiceId << "  " << entry.holdFrames << 'f'
                  << (entry.measured ? "" : " (estimated)") << "  " << entry.speaker << ": \""
                  << entry.line << "\"\n";
      }
      if (!lines.empty())
      {
        std::cout << "  total hold " << heldFrames << " frames\n";
      }
    }

    // The flags are the cutscene's own record of what it has finished. Listed in
    // order because that order is the plot.
    std::cout << "event flag changes (opcodes 0x3E..0x40): " << scriptTrace_.eventFlagChanges().size() << '\n';
    for (const auto &change : scriptTrace_.eventFlagChanges())
    {
      std::cout << "  frame " << change.frame << "  flag 0x" << std::hex << change.flagId
                << (change.set ? " set" : " cleared") << "  at 0x" << change.scriptOffset << std::dec
                << '\n';
    }

    if (!scriptTrace_.objectMethods().empty())
    {
      std::cout << "object methods called (opcode 0xBD -> FUN_00242a18):\n";
      for (const auto &entry : scriptTrace_.objectMethods())
      {
        std::cout << "  method 0x" << std::hex << entry.first << std::dec
                  << "  calls=" << entry.second;
        if (entry.first >= 0x70 && entry.first <= 0x72)
        {
          std::cout << "  (voice: VOICE.BIN is not in the disc root)";
        }
        std::cout << '\n';
      }
    }

    // FUN_0025ce30. The order and the frame each record lands on are the
    // cutscene's timing, so they are listed rather than totalled.
    std::cout << "event streams armed (opcode 0xA1): " << scriptTrace_.eventStreamsArmed().size() << '\n';
    for (const auto &armed : scriptTrace_.eventStreamsArmed())
    {
      std::cout << "  frame " << armed.frame << " channel " << static_cast<int>(armed.channel)
                << " stream 0x" << std::hex << armed.streamOffset << std::dec << '\n';
    }
    std::cout << "event records dispatched: " << scriptTrace_.eventDispatches().size() << '\n';
    for (const auto &dispatch : scriptTrace_.eventDispatches())
    {
      std::cout << "  frame " << dispatch.frame << " ch" << static_cast<int>(dispatch.channel)
                << " delay=" << dispatch.delayUnits
                << " gate=0x" << std::hex << dispatch.gate
                << " -> 0x" << dispatch.targetOffset << std::dec;
      if (dispatch.toDialogue)
      {
        std::cout << "  dialogue";
      }
      else if (dispatch.slot >= 0)
      {
        std::cout << "  slot " << dispatch.slot;
      }
      else
      {
        std::cout << "  NO FREE SLOT";
      }
      std::cout << '\n';
    }

    std::cout << "spawns: " << scriptTrace_.spawns().size() << '\n';
    for (const auto &spawn : scriptTrace_.spawns())
    {
      std::cout << "  type=0x" << std::hex << spawn.typeId << std::dec
                << " slot=" << (spawn.allocated ? std::to_string(spawn.slot) : std::string("none"))
                << (spawn.descriptorResolved ? " descriptor" : " NO-DESCRIPTOR")
                << (spawn.positioned ? " placed" : " unplaced")
                << (spawn.grounded ? " grounded" : "")
                << " at (" << spawn.x << ", " << spawn.y << ", " << spawn.z << ")\n";
    }

    if (scriptTrace_.leadTeleported())
    {
      std::cout << "lead teleported by script to (" << scriptTrace_.leadTeleportX() << ", "
                << scriptTrace_.leadTeleportY() << ", " << scriptTrace_.leadTeleportZ() << ")\n";
    }
    else
    {
      std::cout << "lead not teleported by script; spawn source is " << spawnSourceLabel_ << '\n';
    }

    std::cout << "per-frame entry: " << scriptTrace_.tickRunCount() << " runs, "
              << scriptTrace_.slotRunCount() << " object-script slot runs\n";
    std::cout << "object script slots occupied: " << sceneScript_.occupiedObjectScriptSlots()
              << " of " << orphen::ported::script::kObjectScriptSlotCount
              << (runScriptTick_ ? " (ticked)" : " (not ticked; pass --scr-tick)") << '\n';

    if (!scriptTrace_.terrainTriggers().empty())
    {
      std::cout << "terrain triggers (opcode 0x61, the floor panels):\n";
      for (const auto &entry : scriptTrace_.terrainTriggers())
      {
        std::cout << "  @0x" << std::hex << entry.first
                  << " mask=0x" << entry.second.mask
                  << " word=+0x" << ((entry.second.selector & 0x80u) != 0 ? 0x70 : 0x6C)
                  << " (selector 0x" << static_cast<unsigned>(entry.second.selector) << ")"
                  << " lastSeen=0x" << entry.second.observedWord << std::dec
                  << " tests=" << entry.second.tests
                  << " passes=" << entry.second.passes << '\n';
      }

      // Which panel bits the map actually carries, so a panel that never fires
      // can be told apart from one the port cannot see. A `passes=0` line means
      // nothing on its own -- the player has to be standing on the surface --
      // but a mask with no matching triangle anywhere is a port bug.
      if (const auto *map = mapViewer_.loadedMap(); map != nullptr)
      {
        std::map<std::uint32_t, std::size_t> wordCounts;
        for (const auto &triangle : map->derivedTriangles)
        {
          if (triangle.primitiveIndex < map->DAT_003556b0_dRecords78.size())
          {
            ++wordCounts[map->DAT_003556b0_dRecords78[triangle.primitiveIndex].terrainFlags];
          }
        }
        std::uint32_t unionWord = 0;
        for (const auto &entry : wordCounts)
        {
          unionWord |= entry.first;
        }
        std::cout << "  map terrain words: " << wordCounts.size() << " distinct, union=0x"
                  << std::hex << unionWord << std::dec << '\n';

        // Where each panel lives, so a trigger can be walked onto on purpose.
        // The centroid is enough: a panel is a handful of triangles.
        //
        // Grouped by the whole low byte rather than a bit at a time, because
        // that is how the script reads them. s01_e012's exit script is eight
        // branches that each spell out all four of bits 0x10..0x80 -- the
        // doorway it wants is `!0x10 && 0x20 && 0x40 && !0x80`, so the panels
        // are a code and not four independent switches. Averaging per bit
        // mixes tiles from different codes and lands the centroid on a spot
        // that satisfies none of them.
        std::map<std::uint32_t, std::pair<std::size_t, std::pair<float, float>>> panelCentroids;
        for (const auto &triangle : map->derivedTriangles)
        {
          if (triangle.primitiveIndex >= map->DAT_003556b0_dRecords78.size())
          {
            continue;
          }
          const std::uint32_t code =
              map->DAT_003556b0_dRecords78[triangle.primitiveIndex].terrainFlags & 0xFFu;
          if (code == 0)
          {
            continue;
          }
          auto &accumulator = panelCentroids[code];
          for (const auto vertexIndex : triangle.vertexIndices)
          {
            const auto &position = map->DAT_0035569c_sectionCRecords.at(vertexIndex).position;
            accumulator.second.first += position.x;
            accumulator.second.second += position.y;
          }
          accumulator.first += triangle.vertexIndices.size();
        }
        for (const auto &entry : panelCentroids)
        {
          const float count = static_cast<float>(entry.second.first);
          std::cout << "    panel code 0x" << std::hex << entry.first << std::dec
                    << " centred near (" << std::fixed << std::setprecision(2)
                    << entry.second.second.first / count << ", "
                    << entry.second.second.second / count << ")"
                    << std::defaultfloat << '\n';
        }
      }
    }

    if (!scriptTrace_.playerLocks().empty() || scriptTrace_.battleBootCount() != 0 ||
        !scriptTrace_.fadesArmed().empty())
    {
      std::cout << "panel outcomes reached:\n";
      for (const auto &event : scriptTrace_.fadesArmed())
      {
        std::cout << "  0x85/0x87 fullscreen fade armed: bank=" << event.bank
                  << " rate=" << event.rate
                  << " rgb=0x" << std::hex << event.packedRgb << std::dec
                  << " hits=" << event.hits
                  << (event.bank == 0 ? "  (bank 0 uncovers; 0x88 steps it)"
                                      : "  (bank 1 covers; 0x86 steps it)")
                  << '\n';
      }
      for (const auto &entry : scriptTrace_.playerLocks())
      {
        std::cout << "  0x6D player lock mode=" << entry.first << " hits=" << entry.second
                  << (entry.first < 1 ? "  (state 10; the controller does nothing while it is set)" : "  (release)")
                  << '\n';
      }
      if (scriptTrace_.battleBootCount() != 0)
      {
        std::cout << "  0xE1 save/menu mode hits=" << scriptTrace_.battleBootCount()
                  << "  (flag 0x8EE cleared, mode 0x10 raised; no menu to hand off to)\n";
      }
      if (scriptTrace_.sceneChangeCount() != 0)
      {
        std::cout << "  0x8E scene change requested hits=" << scriptTrace_.sceneChangeCount()
                  << " last destination=" << scriptTrace_.lastSceneChange()
                  << "  (FUN_0022b300's map load is not ported; the scene stays put)\n";
      }
    }

    {
      // A slot with radius 0 is a free slot: FUN_00266050 allocates on it and
      // FUN_0020b430 skips it, so a scene that never gives one a radius is a
      // scene with no dynamic lighting at all. Both EE dumps read all sixteen
      // radii as 0.0, so this line is how the port says whether the VU0 falloff
      // is worth porting rather than assuming either way.
      const auto &lights = sceneScript_.state().DAT_00343888_lights;
      if (lights.everLiveMask() != 0)
      {
        std::cout << "dynamic lights (0xBF/0xC0 alloc, 0xC4 radius): peak radius "
                  << lights.peakRadius() << '\n';
        for (std::uint32_t index = 0; index < orphen::ported::render::LightTable::kSlotCount; ++index)
        {
          if ((lights.everLiveMask() & (1u << index)) == 0)
          {
            continue;
          }
          const auto &light = lights.slot(index);
          std::cout << "  slot " << index
                    << " pos=(" << std::fixed << std::setprecision(2) << light.x << ","
                    << light.y << "," << light.z << ")" << std::defaultfloat
                    << " rgb=(" << static_cast<int>(light.red) << ","
                    << static_cast<int>(light.green) << "," << static_cast<int>(light.blue) << ")"
                    << " radius=" << light.radius
                    << (light.radius == 0.0f ? "  (released)" : "  (live)") << '\n';
        }
      }
      else
      {
        std::cout << "dynamic lights: none ever given a radius"
                     "  (the whole table stays free, as both EE dumps read it)\n";
      }
    }

    if (!scriptTrace_.fadeTracksArmed().empty())
    {
      std::cout << "colour ramps (0x9A arm / 0x9B step / 0x9C read):\n";
      for (const auto &entry : scriptTrace_.fadeTracksArmed())
      {
        const auto &state = sceneScript_.state().DAT_00572078_fadeTracks.track(entry.first);
        std::cout << "  track " << entry.first
                  << " arms=" << entry.second.arms
                  << " lastDuration=" << entry.second.lastDurationFrames << "f"
                  << " now=(" << static_cast<int>(state.current[0]) << ","
                  << static_cast<int>(state.current[1]) << ","
                  << static_cast<int>(state.current[2]) << ")"
                  << " -> (" << static_cast<int>(state.end[0]) << ","
                  << static_cast<int>(state.end[1]) << ","
                  << static_cast<int>(state.end[2]) << ")\n";
      }
    }

    if (const auto &smear = scriptTrace_.frameFeedback(); smear.alphaWrites != 0)
    {
      // Alpha is a GS blend factor over 128, not 255, so 0x80 is a full
      // replacement of the frame by its predecessor.
      std::cout << "screen smear (0xC8 alpha / 0xC9 alpha+transform, FUN_00201a38):\n"
                << "  writes=" << smear.alphaWrites
                << " non-zero=" << smear.nonZeroAlphaWrites
                << " peak alpha=0x" << std::hex << smear.peakAlpha << std::dec
                << " (" << (smear.peakAlpha * 100) / 128 << "% of the previous frame)\n";
      if (smear.transformWrites != 0)
      {
        std::cout << "  0xC9 transform writes=" << smear.transformWrites
                  << " last: offset=(" << smear.lastTransform[0] << ","
                  << smear.lastTransform[1] << ")"
                  << " scale=(" << smear.lastTransform[2] << ","
                  << smear.lastTransform[3] << ")"
                  << " rotation=" << smear.lastTransform[4] << " tenths of a degree\n";
      }
      else
      {
        std::cout << "  no 0xC9: the copy is drawn 1:1, so this is a ghost and not a zoom\n";
      }
    }

    if (!scriptTrace_.objectRegisters().empty())
    {
      std::cout << "object registers touched (opcodes 0x76..0x7C reach entity fields):\n";
      for (const auto &entry : scriptTrace_.objectRegisters())
      {
        const char *name = orphen::ported::script::objectRegisterFieldName(entry.first);
        std::cout << "  reg 0x" << std::hex << entry.first << std::dec << " -> "
                  << (name ? name : "no case in the original (reads 0)")
                  << "  reads=" << entry.second.reads << " writes=" << entry.second.writes;
        if (entry.second.lastSlot >= 0)
        {
          std::cout << "  last: slot " << entry.second.lastSlot << " = " << entry.second.lastValue;
        }
        if (entry.second.unmodelledHits != 0)
        {
          std::cout << "  UNMODELLED=" << entry.second.unmodelledHits;
          if (entry.second.noEntityHits != 0)
          {
            std::cout << " (" << entry.second.noEntityHits << " with no object selected)";
          }
        }
        std::cout << '\n';
      }
      std::cout << "unmodelled object register writes: " << scriptTrace_.unmodelledObjectRegisterHits() << '\n';
    }
    std::cout << "=== end scene script report ===\n\n";
  }

  // A per-frame script entry that halts does so on every frame, so this reports
  // the first occurrence and then stays quiet. Silence here would be the worst
  // outcome: the tick would look like it was running when it was stopping on its
  // second opcode.
  void PortRuntime::reportTickHalt(const char *what) const
  {
    if (reportedTickHalt_)
    {
      return;
    }
    if (!sceneScript_.lastRunOverran() && !sceneScript_.lastRunHaltedOnUnimplemented())
    {
      return;
    }
    reportedTickHalt_ = true;
    std::cout << "[scr] " << what << " halted on frame " << frameCount_ << ": ";
    if (sceneScript_.lastRunHaltedOnUnimplemented())
    {
      std::cout << "unimplemented opcode 0x" << std::hex << sceneScript_.lastHaltOpcode()
                << " at 0x" << sceneScript_.lastHaltOffset() << std::dec << '\n';
    }
    else
    {
      std::cout << "stream overran the blob at 0x" << std::hex << sceneScript_.lastHaltOffset()
                << " (body 0x" << sceneScript_.lastOverrunEntry() << ")" << std::dec << '\n';
    }
    std::cout << "[scr] further per-frame halts are not reported\n";
  }

  void PortRuntime::printSoundReport() const
  {
    std::cout << "\n=== sound report ===\n";
    std::cout << "cue table: " << soundEngine_.cueCount() << " entries\n";
    for (std::size_t bank = 0; bank < orphen::ported::sound::kBankCount; ++bank)
    {
      std::cout << "bank " << bank << " (SND.BIN resource "
                << orphen::ported::sound::kBootBankResources[bank] << "): ";
      if (!soundEngine_.bankLoaded(bank))
      {
        std::cout << "not loaded\n";
        continue;
      }
      std::cout << soundEngine_.bank(bank).usedProgramCount() << " programs\n";
    }

    // The eight music slots, in the order FUN_00206840 walks them.
    std::cout << "music tables:";
    for (std::size_t category = 0; category < orphen::ported::sound::kMusicCategoryCount; ++category)
    {
      std::cout << " cat" << category << "=" << soundEngine_.musicRecordCount(category);
    }
    std::cout << '\n';
    for (const auto &entry : soundEngine_.musicLog())
    {
      std::cout << "slot " << entry.slot << " (cat " << entry.category << ") request 0x" << std::hex
                << entry.request << std::dec << " index " << entry.index << " -> SND.BIN resource "
                << entry.sndResource << " vol " << static_cast<int>(entry.volume) << "  "
                << entry.outcome << '\n';
    }
    for (std::size_t slot = 0; slot < orphen::ported::sound::kMusicSlotCount; ++slot)
    {
      const auto &player = soundEngine_.musicSlot(slot);
      if (!player.hasSequence())
      {
        continue;
      }
      std::cout << "slot " << slot << " track: loops taken " << player.loopsTaken()
                << ", end of track " << (player.reachedEndOfTrack() ? "reached" : "not reached")
                << (player.desynced() ? ", DESYNCED" : "")
                << ", fader " << player.fader() << ", "
                << (player.playing() ? "playing" : "stopped") << '\n';
    }
    for (const auto &entry : soundEngine_.musicEventLog())
    {
      std::cout << "frame " << entry.frame << " music slot " << entry.slot << " " << entry.action
                << " speed " << entry.speed << " fader " << entry.fader
                << " (SND.BIN resource " << entry.sndResource << ")\n";
    }

    if (soundEngine_.cueLog().empty())
    {
      std::cout << "no cues were requested\n";
    }
    for (const auto &entry : soundEngine_.cueLog())
    {
      std::cout << "frame " << entry.frame << " cue " << entry.cue
                << " -> bank " << static_cast<int>(entry.bank)
                << " program " << static_cast<int>(entry.program)
                << " note " << static_cast<int>(entry.note)
                << (entry.distance >= 0.0f
                        ? " dist " + std::to_string(entry.distance) + " src(" +
                              std::to_string(entry.sourceX) + "," + std::to_string(entry.sourceY) +
                              "," + std::to_string(entry.sourceZ) + ") ear(" +
                              std::to_string(entry.listenerX) + "," + std::to_string(entry.listenerY) +
                              "," + std::to_string(entry.listenerZ) + ")"
                        : std::string())
                << " vol " << static_cast<int>(entry.volumeLeft) << '/'
                << static_cast<int>(entry.volumeRight)
                << " -> waveform " << entry.waveform << ", " << entry.samples
                << " samples at " << static_cast<int>(entry.sampleRate) << " Hz"
                << "  " << entry.outcome << '\n';
    }
    std::cout << "=== end sound report ===\n";
  }

  void PortRuntime::printExitReports() const
  {
    // The load-time script report is a load inventory. When frames have run it
    // is worth printing again, because the per-frame entry and the object-script
    // slots reach opcodes the load-time entries never do.
    if (printScriptReport_ && frameCount_ > 0)
    {
      std::cout << "(after " << frameCount_ << " frames)";
      printScriptReport();
    }
    // Same reasoning as the script report: the load-time listing only covers
    // what the scene bootstrap spawned, and a cutscene spawns more as it runs.
    if (printModelReport_ && frameCount_ > 0)
    {
      std::cout << "(after " << frameCount_ << " frames)\n";
      printEntityModelBindings();
    }
    // The pose report's other call site runs at load time, which for a slot a
    // cutscene fills much later prints whatever the slot held before -- or
    // nothing at all. What the pose looks like *after* the frames have run is
    // the question it exists to answer.
    if (poseReportSlot_ >= 0 && frameCount_ > 0)
    {
      std::cout << "(after " << frameCount_ << " frames)\n";
      printPoseReport(static_cast<std::size_t>(poseReportSlot_));
    }
    if (printSoundReport_)
    {
      printSoundReport();
    }
    if (printActorReport_)
    {
      printActorReport();
    }
    if (printRenderReport_)
    {
      printRenderReport();
    }
  }

  // Parses every grp record in the loaded bundle and prints what came out.
  //
  // This exists because the counts are externally checkable: the offline
  // extractor writes them into the second line of each
  // out/target_all/<scene>/grp_*.obj, so an agreeing report means the port and
  // a tool written from a different reading of the format independently landed
  // on the same numbers.
  void PortRuntime::printModelReport() const
  {
    const auto *resources = mapViewer_.loadedSceneResources();
    if (resources == nullptr)
    {
      std::cout << "[models] no scene bundle loaded\n";
      return;
    }

    std::cout << "[models] grp records in " << mapViewer_.loadedSourceDescription() << '\n';
    std::size_t parsed = 0;
    std::size_t failed = 0;
    for (const auto &record : resources->records())
    {
      if (record.category != orphen::harness::kGrpCategory)
      {
        continue;
      }

      const std::vector<std::uint8_t> decoded = resources->decodeRecord(record);
      if (!orphen::ported::model::hasPsc3Magic(decoded))
      {
        std::cout << "  grp_" << std::hex << std::setw(4) << std::setfill('0') << record.resourceId
                  << std::dec << std::setfill(' ') << "  not PSC3, skipped\n";
        continue;
      }

      const orphen::ported::model::Psc3Model model = orphen::ported::model::loadPsc3Model(decoded);
      std::cout << "  grp_" << std::hex << std::setw(4) << std::setfill('0') << record.resourceId
                << std::dec << std::setfill(' ');
      if (!model.valid)
      {
        ++failed;
        std::cout << "  PARSE FAILED: " << model.diagnostic << '\n';
        continue;
      }

      ++parsed;
      std::cout << "  submeshes=" << model.submeshes.size()
                << " verts=" << model.vertices.size()
                << " prims=" << model.primitives.size()
                << " (skip " << model.skippedPrimitives << ")"
                << " subdraws=" << model.subdraws.size()
                << " normals=" << model.normals.size()
                << " passes=" << model.texturedPasses << "/" << model.untexturedPasses
                << " roots=" << model.rootBones.size()
                << " boned=" << model.boneOrder.size()
                << " orphans=" << model.unreachableBones
                << " bounds=(" << model.bounds.min.x << "," << model.bounds.min.y << ","
                << model.bounds.min.z << ")..(" << model.bounds.max.x << "," << model.bounds.max.y
                << "," << model.bounds.max.z << ")\n";
    }
    std::cout << "[models] parsed=" << parsed << " failed=" << failed << '\n';

    printEntityModelBindings();
    if (poseReportSlot_ >= 0)
    {
      printPoseReport(static_cast<std::size_t>(poseReportSlot_));
    }
  }

  // One entity's palette, bone by bone, against an unfiltered rebuild of it.
  //
  // Three palettes are in play and only one of them is drawn:
  //
  //   live        SceneObjectView::bonePalette, built by attachModel through
  //               FUN_0020d188's stateful filter at entity +0xAC. This is what
  //               the renderer poses with.
  //   unfiltered  the same pose column, composed straight from the sampled
  //               keys with no filter and no override.
  //   first       the column FUN_00225c90 would pick for the entity's *animation
  //               id*, which is what --model-report's bbox uses.
  //
  // live vs unfiltered isolates the filter. unfiltered vs first isolates the
  // column. Reporting both separately is the point -- a divergence that shows up
  // in one and not the other says which half to go and read.

  std::string PortRuntime::consumePendingSnapshotImagePath()
  {
    std::string path;
    path.swap(pendingSnapshotImagePath_);
    return path;
  }

  // 'G'. Everything one frame of the pose pipeline decided, in a form that can
  // be pasted into a bug report.
  //
  // This exists because the interesting failures in this port are the ones that
  // only appear when the scene is *played*: the script reaches them carrying
  // state an armed stream never builds, so there is no frame number to point
  // --screenshot at. What goes in follows the same rule as the other reports --
  // values the original also has, named by the field they came from, so a line
  // can be checked against an EE dump rather than believed.
  void PortRuntime::writeDiagnosticSnapshot()
  {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "=== orphen snapshot: frame " << frameCount_ << " ===\n";

    out << "camera eye=(" << fieldCamera_.pose().eye.x << "," << fieldCamera_.pose().eye.y << ","
        << fieldCamera_.pose().eye.z << ") target=(" << fieldCamera_.pose().target.x << ","
        << fieldCamera_.pose().target.y << "," << fieldCamera_.pose().target.z << ")"
        << " yaw=" << fieldCamera_.yawRadians() << " pitch=" << fieldCamera_.pitchRadians()
        << " roll=" << fieldCamera_.uGpffffb6dc_roll()
        << " zoom=" << fieldCamera_.fGpffffb6e8_zoomLog2() << "\n";

    // The same ids the SCR SUBPROC DISP overlay prints, so a snapshot lines up
    // against a screenshot of the overlay.
    {
      const auto &state = sceneScript_.state();
      out << "subprocs:";
      for (std::size_t slot = 0; slot < orphen::ported::script::SceneScriptState::kGeneralSlotCount;
           ++slot)
      {
        const std::uint32_t offset = state.DAT_00355cf4_objectScriptSlots[slot];
        if (offset != 0)
        {
          out << "  " << slot << "[" << sceneScript_.subprocIdAt(offset) << "]@0x" << std::hex
              << offset << std::dec;
        }
      }
      out << "\n";
    }

    EntityModelStore &store = const_cast<EntityModelStore &>(modelStore_);

    // `span` is the posed mesh's bounding box and `bind` the same model's
    // unposed one. Their ratio is what separates an animation from a palette
    // that has come apart, and sorting on it puts the broken entity at the top
    // of the report instead of somewhere in the middle of sixty static props.
    struct Row
    {
      std::size_t slot = 0;
      float ratio = 0.0f;
      std::string line;
      std::string bones;
    };
    std::vector<Row> rows;

    for (const auto &view : mapViewer_.sceneObjectViews())
    {
      const auto &entity = entityPool_.slot(view.slot);
      Row row;
      row.slot = view.slot;

      std::ostringstream line;
      line << std::fixed << std::setprecision(3);
      line << "  slot=" << view.slot << " type=0x" << std::hex << entity.typeId00 << std::dec;

      const EntityModelBinding *binding = store.bindingForTypeId(entity.effectiveTypeId());
      if (binding != nullptr)
      {
        line << " grp_" << std::hex << std::setw(4) << std::setfill('0') << binding->meshId
             << std::dec << std::setfill(' ');
      }

      line << " anim=" << entity.animationA0 << " col=" << entity.poseColumnAc
           << " prev=" << entity.previousPoseColumnAe << " blend=" << entity.animationBlend13c
           << " keyTicks=" << entity.keyframeTicksA6 << std::hex << " +04=0x" << entity.halfword04
           << " +06=0x" << entity.flags06 << " +08=0x" << entity.halfword08 << std::dec
           << " parent=" << entity.parentSlot192 << " bone="
           << static_cast<int>(entity.attachBone194) << " state=" << entity.state60 << " pos=("
           << view.worldOrigin.x << "," << view.worldOrigin.y << "," << view.worldOrigin.z << ")";

      if (binding != nullptr && binding->model != nullptr && !view.bonePalette.empty())
      {
        const auto &model = *binding->model;
        orphen::ported::psm2::Bounds3 posed;
        orphen::ported::psm2::Bounds3 bind;
        for (const auto &vertex : model.vertices)
        {
          const std::size_t bone =
              vertex.boneIndex < view.bonePalette.size() ? vertex.boneIndex : 0u;
          orphen::ported::psm2::includePoint(
              posed, orphen::ported::model::transformPoint(vertex.position, view.bonePalette[bone]));
          orphen::ported::psm2::includePoint(bind, vertex.position);
        }
        const float posedSpan = std::max({posed.max.x - posed.min.x, posed.max.y - posed.min.y,
                                          posed.max.z - posed.min.z});
        const float bindSpan = std::max({bind.max.x - bind.min.x, bind.max.y - bind.min.y,
                                         bind.max.z - bind.min.z});
        line << " span=" << posedSpan << " bind=" << bindSpan;
        if (bindSpan > 0.0f)
        {
          row.ratio = posedSpan / bindSpan;
          line << " ratio=" << row.ratio;
          // Skinning moves a mesh around; a legitimate pose in this game reaches
          // about 2x its bind box (slot 16's grp_00a6 sits at 1.95). Past three
          // is not an animation.
          //
          // The bind span has to be real for the ratio to mean anything. A rope
          // (grp_001e, the bandana) is authored with every bone stacked at the
          // origin and takes its shape from the simulation, so its bind box is
          // 0.044 and the ratio reads 6 while the pose is correct to 4 mm
          // against hardware. Sort by it, but do not accuse it.
          if (row.ratio > 3.0f && bindSpan > 0.1f)
          {
            line << "  <<< DEFORMED";
          }
        }

        // The bone table, for the entities where this class of bug lives: an
        // attached one, whose root comes out of another entity's palette, and
        // whatever it is attached to. Origins are relative to bone 0 so they can
        // be diffed against 0x00357E00 + slot * 0xA80 in an EE dump without
        // having to subtract a world position first.
        const bool attached = entity.parentSlot192 >= 0;
        bool isParent = false;
        for (const auto &other : mapViewer_.sceneObjectViews())
        {
          if (entityPool_.slot(other.slot).parentSlot192 == static_cast<std::int16_t>(view.slot))
          {
            isParent = true;
            break;
          }
        }
        if (attached || isParent || row.ratio > 3.0f)
        {
          std::ostringstream bones;
          bones << std::fixed << std::setprecision(3);
          bones << "    bones (origin relative to bone 0, then axis length):\n";
          for (std::size_t bone = 0; bone < view.bonePalette.size(); ++bone)
          {
            const auto &m = view.bonePalette[bone];
            const auto &r = view.bonePalette[0];
            const float scale =
                std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            bones << "      " << std::setw(2) << bone << " (" << std::setw(7) << m[12] - r[12]
                  << "," << std::setw(7) << m[13] - r[13] << "," << std::setw(7) << m[14] - r[14]
                  << ")  scale=" << scale << "\n";
          }
          row.bones = bones.str();
        }
      }
      line << "\n";
      row.line = line.str();
      rows.push_back(std::move(row));
    }

    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row &a, const Row &b) { return a.ratio > b.ratio; });

    out << "draw list (" << rows.size()
        << " entries, worst posed/bind ratio first; a rope bind box is collapsed"
           " so its ratio runs high while posed correctly):\n";
    for (const auto &row : rows)
    {
      out << row.line;
    }
    for (const auto &row : rows)
    {
      if (!row.bones.empty())
      {
        out << "  slot " << row.slot << " bone detail:\n" << row.bones;
      }
    }

    // A live entity that is *not* in the draw list was dropped by
    // FUN_0020c5a8's queue -- hidden, or deferred behind a parent that never
    // posed. Which of those it is matters, so the flags come along.
    out << "live but not drawn:";
    for (std::size_t slot = 0; slot < orphen::ported::entity::kEntitySlotCount; ++slot)
    {
      const auto &entity = entityPool_.slot(slot);
      if (entity.typeId00 <= 0)
      {
        continue;
      }
      bool drawn = false;
      for (const auto &view : mapViewer_.sceneObjectViews())
      {
        if (view.slot == slot)
        {
          drawn = true;
          break;
        }
      }
      if (!drawn)
      {
        out << "  " << slot << "(0x" << std::hex << entity.typeId00 << " +04=0x"
            << entity.halfword04 << " +08=0x" << entity.halfword08 << std::dec
            << " parent=" << entity.parentSlot192 << ")";
      }
    }
    // ---- the entity pool, laid out for a byte-level diff against an EE dump --
    //
    // Every field here is read straight out of `DAT_0058beb0 + slot * 0x1D8` in
    // a dump by `scripts/dump_ee_entities.py`, which prints these exact columns
    // in this exact order. `diff` the two and the divergence is the answer.
    //
    // Two columns are worth a caveat. `+6C` and `+70` carry the ground query's
    // winning / ANDed terrain flags in the original, but the port also uses
    // them as opcode 0x61's flag words, so they agree only for an entity whose
    // last write came from FUN_00227070. `+0A` is `primitive | (half << 14)`
    // and is the sharpest single field in the row: if it matches, the query
    // walked the same cell run to the same place.
    out << "\nentities (offsets are into the original's 0x1D8 record):\n";
    out << "slot  +00    +02    +04    +08  +0C        +0A     "
           "pos                              +4C      +54    +58    +5C      "
           "+60 +62  +64  +6C        +70        +74        +A0  +192\n";
    for (std::size_t slot = 0; slot < orphen::ported::entity::kEntitySlotCount; ++slot)
    {
      const auto &entity = entityPool_.slot(slot);
      if (entity.typeId00 == 0 && entity.halfword08 == 0 && entity.halfword04 == 0)
      {
        continue;
      }
      std::ostringstream row;
      row << std::fixed << std::setprecision(4);
      row << std::setw(4) << slot << " 0x" << std::hex << std::setw(4) << std::setfill('0')
          << static_cast<std::uint16_t>(entity.typeId00) << " 0x" << std::setw(4)
          << entity.descriptorFlags02 << " 0x" << std::setw(4) << entity.halfword04 << " 0x"
          << std::setw(4) << entity.halfword08 << " 0x" << std::setw(8) << entity.collisionFlags0c
          << std::dec << std::setfill(' ') << std::setw(7) << entity.groundPrimitive0a << " ("
          << std::setw(9) << entity.positionX20 << "," << std::setw(9) << entity.positionZ24 << ","
          << std::setw(9) << entity.positionY28 << ")" << std::setw(9) << entity.groundHeight4c
          << std::setw(7) << entity.radius54 << std::setw(7) << entity.height58 << std::setw(9)
          << entity.facingRadians5c << std::setw(4) << entity.state60 << std::setw(4)
          << entity.fadeRamp62 << std::setw(5) << entity.blockedBy64 << " 0x" << std::hex
          << std::setw(8) << std::setfill('0') << entity.flagWord6c << " 0x" << std::setw(8)
          << entity.flagWord70 << " 0x" << std::setw(8) << entity.rejectTerrainMask74 << std::dec
          << std::setfill(' ') << std::setw(5) << entity.animationA0 << std::setw(5)
          << entity.parentSlot192 << "\n";
      out << row.str();
    }

    // The two pieces of state that date a moment exactly, so a snapshot can be
    // matched to an EE dump instead of guessed at by eye. See "eeMemory.bin is
    // frame ~1091" in the README -- eyeballing the camera cost a whole session.
    {
      const auto &state = sceneScript_.state();
      out << "scheduler:";
      for (std::size_t channel = 0;
           channel < orphen::ported::script::SceneScriptState::kEventChannelCount; ++channel)
      {
        const auto &record = state.DAT_00571e40_eventChannels[channel];
        out << "  " << channel << "{cursor=0x" << std::hex << record.cursor << std::dec
            << " timer=" << record.timer << " consumed=" << record.consumed << "}";
      }
      out << "\n";
      out << "groups: DAT_003555d0=" << (DAT_003555d0_collisionGroupMoved_ ? 1 : 0)
          << " liveFrames=" << DAT_003555d0_liveFrames_ << " pushOuts=" << pushOutCount_ << "\n";
      out << "fade: in=0x" << std::hex << DAT_00571dc0_screenFade_.DAT_00571dc0_fadeInLevel()
          << " out=0x" << DAT_00571dc0_screenFade_.DAT_00571dd0_fadeOutLevel() << " overlay=0x"
          << DAT_00571dc0_screenFade_.overlay().colour << "/"
          << static_cast<unsigned>(DAT_00571dc0_screenFade_.overlay().alpha) << std::dec << "\n";
    }

    out << "\n=== end snapshot ===\n";

    const std::string text = out.str();
    std::cout << text << std::flush;

    std::ostringstream name;
    name << "orphen_snapshot_" << frameCount_;
    const std::string stem = name.str();
    // std::ofstream, the way every other writer in the port does it -- the
    // framebuffer capture beside this one included.
    if (std::ofstream file(stem + ".txt", std::ios::binary); file)
    {
      file.write(text.data(), static_cast<std::streamsize>(text.size()));
      std::cout << "[snapshot] wrote " << stem << ".txt" << '\n';
    }
    pendingSnapshotImagePath_ = stem + ".ppm";
  }

  void PortRuntime::printPoseReport(std::size_t slot) const
  {
    const auto &entity = entityPool_.slot(slot);
    EntityModelStore &store = const_cast<EntityModelStore &>(modelStore_);
    const EntityModelBinding *binding = store.bindingForTypeId(entity.effectiveTypeId());
    if (binding == nullptr || binding->model == nullptr)
    {
      std::cout << "[pose] slot " << slot << " has no model\n";
      return;
    }
    const auto &model = *binding->model;

    const orphen::port::SceneObjectView *view = nullptr;
    for (const auto &candidate : mapViewer_.sceneObjectViews())
    {
      if (candidate.slot == slot)
      {
        view = &candidate;
        break;
      }
    }
    if (view == nullptr || view->bonePalette.empty())
    {
      std::cout << "[pose] slot " << slot << " is not in the published view list\n";
      return;
    }

    const std::uint16_t firstColumn = orphen::ported::model::firstPoseColumnForAnimation(
        model, model.blob, entity.animationA0);
    std::cout << "[pose] slot " << slot << " type=0x" << std::hex << entity.typeId00 << std::dec
              << " grp_" << std::hex << std::setw(4) << std::setfill('0') << binding->meshId
              << std::dec << std::setfill(' ') << "  bones=" << model.submeshes.size()
              << "  anim=" << entity.animationA0 << " poseColumn(+0xAC)=" << entity.poseColumnAc
              << " prev(+0xAE)=" << entity.previousPoseColumnAe
              << " firstColumnForAnim=" << firstColumn
              << "  blend(+0x13C)=" << entity.animationBlend13c
              << "  hidden(+0x168)="
              << [&] {
                   std::string list;
                   const auto &m = DAT_004a7e00_boneOverrides_[slot].mode168;
                   for (std::size_t b = 0; b < m.size(); ++b)
                   {
                     if (m[b] != 0)
                     {
                       list += std::to_string(b) + "(" + std::to_string(m[b]) + ") ";
                     }
                   }
                   return list.empty() ? std::string("none") : list;
                 }()
              << "  flags08=0x" << std::hex << entity.halfword08 << std::dec
              << std::fixed << std::setprecision(4) << "\n  root basis: x=("
              << view->bonePalette[0][0] << "," << view->bonePalette[0][1] << ","
              << view->bonePalette[0][2] << ") y=(" << view->bonePalette[0][4] << ","
              << view->bonePalette[0][5] << "," << view->bonePalette[0][6] << ") z=("
              << view->bonePalette[0][8] << "," << view->bonePalette[0][9] << ","
              << view->bonePalette[0][10] << ")" << std::defaultfloat << '\n';

    const auto root = orphen::ported::model::FUN_0020cdc0_entity_root(
        {view->position.x, view->position.y, view->position.z}, view->facingRadians,
        view->rotationX154, view->rotationY158, view->scale, view->scaleZ150);
    const auto unfiltered =
        orphen::ported::model::FUN_0020d618_build_palette(model, model.blob, entity.poseColumnAc, root);
    const auto atFirstColumn =
        orphen::ported::model::FUN_0020d618_build_palette(model, model.blob, firstColumn, root);

    const auto rowLength = [](const orphen::ported::model::Matrix4 &m, int row) {
      return std::sqrt(m[row * 4] * m[row * 4] + m[row * 4 + 1] * m[row * 4 + 1] +
                       m[row * 4 + 2] * m[row * 4 + 2]);
    };
    const auto originGap = [](const orphen::ported::model::Matrix4 &a,
                              const orphen::ported::model::Matrix4 &b) {
      const float dx = a[12] - b[12];
      const float dy = a[13] - b[13];
      const float dz = a[14] - b[14];
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    int firstFilterDivergence = -1;
    int firstColumnDivergence = -1;
    constexpr float kTolerance = 1e-3f;

    std::cout << "  bone  parent  live-origin                    scale    axes(x,y,z)          "
                 "keys(place/rot)   track\n";
    for (std::size_t bone = 0; bone < model.submeshes.size() && bone < view->bonePalette.size();
         ++bone)
    {
      const auto &live = view->bonePalette[bone];
      const auto &plain = unfiltered[bone];
      const auto &first = atFirstColumn[bone];
      const float filterGap = originGap(live, plain);
      const float columnGap = originGap(plain, first);
      if (firstFilterDivergence < 0 && filterGap > kTolerance)
      {
        firstFilterDivergence = static_cast<int>(bone);
      }
      if (firstColumnDivergence < 0 && columnGap > kTolerance)
      {
        firstColumnDivergence = static_cast<int>(bone);
      }
      // Three axis lengths, not one. FUN_0020cf28 composes a uniform scale, so
      // any spread between them is shear arriving from somewhere else -- which
      // a single "scale" column cannot show.
      const auto probe = orphen::ported::model::FUN_0020d378_probe_bone(
          model, model.blob, bone, entity.poseColumnAc);
      std::cout << "  " << std::setw(4) << bone << "  " << std::setw(6)
                << model.submeshes[bone].parentIndex << std::fixed << std::setprecision(3)
                << "  (" << std::setw(8) << live[12] << "," << std::setw(8) << live[13] << ","
                << std::setw(8) << live[14] << ")  " << std::setw(6) << rowLength(live, 0)
                << "  " << std::setw(6) << rowLength(live, 0) << "," << std::setw(6)
                << rowLength(live, 1) << "," << std::setw(6) << rowLength(live, 2)
                << std::defaultfloat << "   0x" << std::hex << probe.placementKey << "/0x"
                << probe.rotationKey << std::dec << (probe.sentinel ? " SENTINEL" : "")
                << (probe.trackInRange ? "" : " OFF-END") << "   0x" << std::hex
                << probe.trackOffset << std::dec << std::fixed << std::setprecision(3)
                << "  rot=(" << probe.rotationRadians.x << "," << probe.rotationRadians.y
                << "," << probe.rotationRadians.z << ")" << std::defaultfloat
                << '\n';
    }
    std::cout << std::defaultfloat;

    // What the passes ask the rasteriser for. A model that poses correctly but
    // looks wrong is being drawn wrong, and this is the next place to look:
    // FUN_00212058:137-141's mode nibble and alpha, per subdraw.
    {
      std::array<std::size_t, 4> modeCounts{};
      std::array<std::size_t, 4> modeAlphaSum{};
      for (const auto &subdraw : model.subdraws)
      {
        const std::size_t mode = subdraw.blendMode() & 3u;
        ++modeCounts[mode];
        modeAlphaSum[mode] += subdraw.alpha();
      }
      std::cout << "  subdraw blend modes (0 opaque / 1 alpha / 2 additive / 3 subtract):";
      for (std::size_t mode = 0; mode < 4; ++mode)
      {
        if (modeCounts[mode] == 0)
        {
          continue;
        }
        std::cout << "  " << mode << "=" << modeCounts[mode] << " (mean alpha "
                  << modeAlphaSum[mode] / modeCounts[mode] << ")";
      }
      std::cout << '\n';
    }

    std::cout << "  first bone where the filter diverges: "
              << (firstFilterDivergence < 0 ? std::string("none") : std::to_string(firstFilterDivergence))
              << "\n  first bone where the column diverges: "
              << (firstColumnDivergence < 0 ? std::string("none") : std::to_string(firstColumnDivergence))
              << '\n';
  }

  // The entity half: which model and texture each live entity resolved to, and
  // the resulting texture slot table.
  //
  // The slot table is the strongest check in the port, because it is a table
  // nobody here designed: FUN_00221d20 fills DAT_00315a98 in bundle order and
  // FUN_00210280 fills DAT_003429a8, and both are readable in the EE dump. If
  // the port's bank ranges, free-slot rule or static-bind handling are wrong,
  // the numbers move.
  void PortRuntime::printEntityModelBindings() const
  {
    EntityModelStore &store = const_cast<EntityModelStore &>(modelStore_);

    std::cout << "[models] entity bindings"
              << (store.bootBundleLoaded() ? "" : "  (no s00_e000 boot bundle)") << '\n';

    // The bbox beside it is rebuilt from a *fresh* palette. The renderer does
    // not use that one -- it uses `SceneObjectView::bonePalette`, which
    // PortRuntime::attachModel builds through FUN_0020d188's filter, and that
    // filter carries state across frames. When the two disagree, the model is
    // parsed fine and the pose is what is wrong, which is a completely
    // different bug from the one a garbled model implies.
    const auto livePaletteExtent = [&](std::size_t slot,
                                       const orphen::ported::model::Psc3Model &model) {
      std::ostringstream text;
      for (const auto &view : mapViewer_.sceneObjectViews())
      {
        if (view.slot != slot || view.bonePalette.empty())
        {
          continue;
        }
        orphen::ported::psm2::Bounds3 live;
        for (const auto &vertex : model.vertices)
        {
          const std::size_t bone =
              vertex.boneIndex < view.bonePalette.size() ? vertex.boneIndex : 0u;
          orphen::ported::psm2::includePoint(
              live, orphen::ported::model::transformPoint(vertex.position,
                                                          view.bonePalette[bone]));
        }
        const float span = std::max({live.max.x - live.min.x, live.max.y - live.min.y,
                                     live.max.z - live.min.z});
        text << std::fixed << std::setprecision(2) << "  live=" << span;
        // An order of magnitude past the fresh pose is not an animation, it is
        // a palette that has diverged.
        if (span > 8.0f)
        {
          text << " EXPLODED";
        }
        break;
      }
      return text.str();
    };

    const auto describe = [&](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity) {
      const EntityModelBinding *binding = store.bindingForTypeId(entity.effectiveTypeId());
      std::cout << "  slot=" << std::setw(3) << slot
                << " type=0x" << std::hex << entity.typeId00 << std::dec;
      if (binding == nullptr)
      {
        std::cout << "  no model record (streamed or unresolved descriptor)\n";
        return;
      }
      std::cout << "  grp_" << std::hex << std::setw(4) << std::setfill('0') << binding->meshId
                << " tex_" << std::setw(4) << binding->textureId << std::dec << std::setfill(' ');
      if (binding->textureSlot == orphen::ported::resource::kNoTextureSlot)
      {
        std::cout << " slot=--";
      }
      else
      {
        std::cout << " slot=" << std::setw(2) << binding->textureSlot;
      }
      if (binding->model == nullptr)
      {
        std::cout << "  NO MODEL: " << binding->diagnostic;
      }
      else
      {
        // Posed extents, with the root left at identity. The raw parse bounds
        // are meaningless -- PSC3 vertex positions are bone-local -- so this is
        // the first number that can be sanity-checked, and the descriptor's
        // collision size is printed beside it to check against.
        const auto &model = *binding->model;
        const std::uint16_t column = orphen::ported::model::firstPoseColumnForAnimation(
            model, model.blob, entity.animationA0);
        // Same root matrix the renderer builds, so the printed bounds are in
        // world space and can be checked against the entity's own position.
        // This is what caught the models being drawn a basis change away from
        // where they belong.
        const auto root = orphen::ported::model::FUN_0020cdc0_entity_root(
            {entity.positionX20, entity.positionZ24, entity.positionY28}, entity.facingRadians5c,
            entity.rotationX154, entity.rotationY158, entity.scale14c, entity.scaleZ150);
        const auto palette =
            orphen::ported::model::FUN_0020d618_build_palette(model, model.blob, column, root);
        orphen::ported::psm2::Bounds3 posed;
        for (const auto &vertex : model.vertices)
        {
          const std::size_t bone = vertex.boneIndex < palette.size() ? vertex.boneIndex : 0u;
          orphen::ported::psm2::includePoint(
              posed, orphen::ported::model::transformPoint(vertex.position, palette[bone]));
        }
        std::cout << "  submeshes=" << model.submeshes.size()
                  << " verts=" << model.vertices.size()
                  << " anim=" << entity.animationA0 << " col=" << column
                  // +0x06 bit 0x10 is FUN_00225c90's early return: this entity's
                  // animation never advances. For map props it comes straight
                  // from the prop record's 0x4000 bit.
                  << ((entity.flags06 & 0x0010u) != 0 ? " HELD" : "")
                  << std::fixed << std::setprecision(2)
                  << "  posed=" << (posed.max.x - posed.min.x) << "x"
                  << (posed.max.y - posed.min.y) << "x" << (posed.max.z - posed.min.z)
                  << livePaletteExtent(slot, model)
                  << "  at=(" << (posed.min.x + posed.max.x) * 0.5f << ","
                  << (posed.min.y + posed.max.y) * 0.5f << "," << posed.min.z << ".."
                  << posed.max.z << ")"
                  << "  entity=(" << entity.positionX20 << "," << entity.positionZ24 << ","
                  << entity.positionY28 << ")"
                  << "  descriptor=" << entity.radius54 << "r/" << entity.height58 << "h"
                  << std::defaultfloat;

        // FUN_00212058:180-208, per *pass*, because the selector's meaning
        // depends on the owning primitive's flags. texFlags bits 10..7 are a
        // texture selector; primitive flag 0x800 decides how the original reads
        // it. With 0x800 clear the packet's byte 6 becomes the selector itself,
        // which on the map path (FUN_00211230:186) is `globalSlot + 1` -- so
        // selector N means global texture slot N-1, not the entity's bound one.
        // With 0x800 set, byte 6 goes to 0x3F (the bound slot) and the selector
        // rides in byte 5 instead. Selector 0 is the plain bound-slot case and
        // 0xF is the untextured/special mode.
        //
        // The port draws every textured pass with the entity's one bound
        // texture, so a `sel` entry below is a pass reaching for a sheet the
        // port never gives it.
        std::map<std::string, std::size_t> selectors;
        for (const auto &primitive : model.primitives)
        {
          if (primitive.skipped())
          {
            continue;
          }
          const bool boundSlotForm = (primitive.flags & 0x0800u) != 0;
          for (const std::int16_t index : primitive.subdrawIndices)
          {
            if (index < 0 || static_cast<std::size_t>(index) >= model.subdraws.size())
            {
              continue;
            }
            const std::uint16_t selector = model.subdraws[index].textureSlot();
            std::ostringstream key;
            if (selector == 0)
            {
              key << "bound";
            }
            else if (selector == 0xF)
            {
              key << "none";
            }
            else if (boundSlotForm)
            {
              key << "bound+" << selector;
            }
            else
            {
              key << "gslot" << (selector - 1);
            }
            ++selectors[key.str()];
          }
        }
        if (selectors.size() > 1 || (!selectors.empty() && selectors.begin()->first != "bound"))
        {
          std::cout << "  passes={";
          bool first = true;
          for (const auto &entry : selectors)
          {
            std::cout << (first ? "" : " ") << entry.first << ":" << entry.second;
            first = false;
          }
          std::cout << "}";
        }
      }
      std::cout << '\n';
    };

    describe(0, entityPool_.leadPlayer());
    // Every live slot, not just the script-spawned ones. A slot that is
    // allocated but not initialised still draws and still animates, and hiding
    // it here is how a cutscene stalled on an entity nobody could see.
    entityPool_.forEachActive(
        [&](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity) {
          if (entityPool_.status(slot) == orphen::ported::entity::SlotStatus::Free)
          {
            return;
          }
          if (entityPool_.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
          {
            std::cout << "  slot=" << std::setw(3) << slot << " (allocated, not script-spawned)";
          }
          describe(slot, entity);
        });

    std::cout << "[models] texture slots (DAT_00315a98 cache key / DAT_003429a8 resident)\n";
    for (std::size_t slot = 0; slot < orphen::ported::resource::kTextureSlotCount; ++slot)
    {
      const auto &state = store.textureSlots().slot(slot);
      if (!state.occupied() && state.DAT_00315a98_cacheKey == 0)
      {
        continue;
      }
      std::cout << "  slot " << std::setw(2) << slot
                << "  key=" << std::setw(6) << state.DAT_00315a98_cacheKey
                << "  resident=0x" << std::hex << std::setw(4) << std::setfill('0')
                << state.DAT_003429a8_residentId << std::dec << std::setfill(' ')
                << (state.texture.rgbaPixels.empty() ? "  (no pixels)" : "") << '\n';
    }
    std::cout << "[models] loaded=" << store.loadedModelCount()
              << " slotsOccupied=" << store.textureSlots().occupiedSlots()
              << " texturesMissing=" << store.textureSlots().missingTextures() << '\n';
  }

  // Dumps every primitive whose bounds come within `radius` of a world point,
  // with the fields that decide how it draws. This is a hypothesis-testing
  // tool for questions like "why did that thing vanish", not part of the port.
  void PortRuntime::printPrimitiveProbe(const orphen::ported::psm2::Vec3 &centre, float radius) const
  {
    const auto *map = mapViewer_.loadedMap();
    if (map == nullptr)
    {
      std::cout << "[probe] no map loaded\n";
      return;
    }

    std::cout << "[probe] primitives within " << radius << " of ("
              << centre.x << ", " << centre.y << ", " << centre.z << ")\n";

    for (std::size_t index = 0; index < map->DAT_003556ac_dRecords80.size(); ++index)
    {
      const auto &record80 = map->DAT_003556ac_dRecords80[index];
      const auto &record78 = map->DAT_003556b0_dRecords78[index];
      if (orphen::ported::psm2::distance(record80.center, centre) > radius + record80.radius)
      {
        continue;
      }

      std::cout << "  #" << index
                << " flags=0x" << std::hex << record80.primitiveFlags
                // record78 +0x00. FUN_00227840:126-131 gates its ground scan on
                // bits 0x800 and 0x100 of this, not on terrainFlags, so a probe
                // that omits it cannot explain why a surface was or was not
                // stood on.
                << " lead=0x" << record78.leadingWord
                << " terrain=0x" << record78.terrainFlags << std::dec
                << " centre=(" << formatNumber(record80.center.x) << "," << formatNumber(record80.center.y)
                << "," << formatNumber(record80.center.z) << ")"
                << " r=" << formatNumber(record80.radius)
                << " n=(" << formatNumber(record78.unitNormal[0].x) << ","
                << formatNumber(record78.unitNormal[0].y) << ","
                << formatNumber(record78.unitNormal[0].z) << ")"
                << " corners=" << ((record80.primitiveFlags & 0x4000) != 0 ? 3 : 4)
                << " alpha=0x" << std::hex << static_cast<int>(record80.staticAlpha) << std::dec;

      for (std::size_t slot = 0; slot < record80.materialSlots.size(); ++slot)
      {
        const auto &material = record80.materialSlots[slot];
        if (!material.present())
        {
          continue;
        }
        std::cout << " | slot" << slot << " type=0x" << std::hex << static_cast<int>(material.type)
                  << " a=0x" << static_cast<int>(material.alpha)
                  << " f=0x" << static_cast<int>(material.flags) << std::dec;
        // The four corner (u, v) byte pairs. Which band of a 256x256 page a
        // primitive samples is the only way to tell a lantern glow from a rain
        // sheet when both are additive draws off the same effects page.
        if (material.textured())
        {
          std::cout << " uv=";
          for (std::size_t corner = 0; corner < 4; ++corner)
          {
            std::cout << (corner != 0 ? "," : "")
                      << static_cast<int>(material.textureCoordinates[corner * 2]) << ":"
                      << static_cast<int>(material.textureCoordinates[corner * 2 + 1]);
          }
        }
      }
      std::cout << '\n';
    }
  }

  // What the ported render pipeline did on the last frame, plus the one thing
  // that cannot be checked by looking at a picture: whether the winding the
  // plane normals imply agrees with the map's own ceiling flag. FUN_00227840
  // marks downward-facing primitives with 0x100 on 0x78 +0x00, so their unit
  // normal should point down. A large disagreement count means backface
  // culling is inverted, not that the map is broken.
  void PortRuntime::printRenderReport() const
  {
    const auto *map = mapViewer_.loadedMap();
    if (map == nullptr)
    {
      std::cout << "[render] no map loaded\n";
      return;
    }

    // The camera state, in the same terms the EE dump stores it, so the two can
    // be read side by side. s01_e24.bin at spawn holds:
    //   DAT_0058c0a8 eye    (-6.1011, -12.7500, 0.8751)
    //   DAT_0058be90 lookAt (-3.2500, -12.7500, 0.8000)
    //   fGpffffb6d4 yaw 0.000000   fGpffffb6d8 pitch -0.165120
    //   fGpffffad28 distance 3.000000    slot 0 at (-3.2500, -12.7500, 0.0000)
    {
      const auto &pose = fieldCamera_.pose();
      const auto &lead = entityPool_.leadPlayer();
      std::cout << "[camera] eye=(" << pose.eye.x << ", " << pose.eye.y << ", " << pose.eye.z
                << ") lookAt=(" << pose.target.x << ", " << pose.target.y << ", " << pose.target.z
                << ")\n";
      std::cout << "[camera] yaw=" << fieldCamera_.yawRadians()
                << " pitch=" << fieldCamera_.pitchRadians()
                << " distance=" << fieldCamera_.followDistance() << '\n';
      std::cout << "[camera] lookAt.z - player.z = " << (pose.target.z - lead.positionY28)
                << "   eye.z - player.z = " << (pose.eye.z - lead.positionY28) << '\n';
      // What the view matrix actually aims at: FUN_0020bec8 lifts the eye by
      // fGpffff808c before pitching, so this is the height the screen centre
      // lands on at the player's horizontal distance. The dump's is 0.8000.
      const float dx = pose.target.x - pose.eye.x;
      const float dy = pose.target.y - pose.eye.y;
      const float horizontal = std::sqrt(dx * dx + dy * dy);
      const float viewEyeZ = pose.eye.z + orphen::ported::render::constants::kEyeHeightOffset;
      std::cout << "[camera] screen centre lands at z = "
                << (viewEyeZ + horizontal * std::tan(fieldCamera_.pitchRadians()))
                << " (player z " << lead.positionY28 << ", dump 0.8000)\n";
    }

    constexpr std::uint32_t kCeilingBit = 0x100;
    constexpr std::uint32_t kTwoSidedBit = 0x1;
    // A wall's normal is horizontal, so "not pointing up" is only a winding
    // error when it points *down*. Splitting the three cases is what makes
    // this number readable.
    constexpr float kFlatThreshold = 0.001f;
    std::size_t ceilingCount = 0;
    std::size_t ceilingPointingDown = 0;
    std::size_t otherUp = 0;
    std::size_t otherDown = 0;
    std::size_t otherFlat = 0;
    std::size_t fadingNow = 0;
    std::size_t twoSided = 0;

    for (std::size_t index = 0; index < map->DAT_003556b0_dRecords78.size(); ++index)
    {
      const auto &record78 = map->DAT_003556b0_dRecords78[index];
      const float normalZ = record78.unitNormal[0].z;
      if ((record78.leadingWord & kCeilingBit) != 0)
      {
        ++ceilingCount;
        if (normalZ < 0.0f)
        {
          ++ceilingPointingDown;
        }
      }
      else if (normalZ > kFlatThreshold)
      {
        ++otherUp;
      }
      else if (normalZ < -kFlatThreshold)
      {
        ++otherDown;
      }
      else
      {
        ++otherFlat;
      }

      if (map->DAT_003556ac_dRecords80[index].dynamicFade < orphen::ported::psm2::kFadeCeiling)
      {
        ++fadingNow;
      }
      if ((map->DAT_003556ac_dRecords80[index].primitiveFlags & kTwoSidedBit) != 0)
      {
        ++twoSided;
      }
    }

    // The scene environment block, so it can be diffed against an EE dump
    // directly: s01_e24.bin has 0x35566c=0x708090, 0x355670=0x2050a0,
    // 0x355674=0x505050, 0x35567c=8, 0x355680=32, 0x355628=32.
    const auto &environment = sceneScript_.state();

    // VU1 0x01d1 floors every light intensity at `~record[+0x2D] / 320`, and
    // 0x01ba skips lighting outright on flag bit 13. Both are per primitive, so
    // the useful thing to print is the spread rather than a single value.
    const auto lightFloorSummary = [this]() {
      const auto *map = mapViewer_.loadedMap();
      if (map == nullptr || map->DAT_003556ac_dRecords80.empty())
      {
        return std::string("light floor: no map loaded");
      }
      float lowest = 1.0f;
      float highest = 0.0f;
      double total = 0.0;
      std::size_t unlit = 0;
      for (const auto &record : map->DAT_003556ac_dRecords80)
      {
        const float floorValue =
            orphen::ported::render::SceneLighting::floorFromSourceByte(record.staticAlpha);
        lowest = std::min(lowest, floorValue);
        highest = std::max(highest, floorValue);
        total += floorValue;
        if ((record.primitiveFlags & 0x2000u) != 0)
        {
          ++unlit;
        }
      }
      std::ostringstream out;
      out << "light floor: " << lowest << ".." << highest << " mean "
          << total / static_cast<double>(map->DAT_003556ac_dRecords80.size())
          << " over " << map->DAT_003556ac_dRecords80.size() << " primitives; "
          << unlit << " unlit";
      return out.str();
    };

    std::cout << "[render] draw distance " << mapViewer_.drawDistance()
              << " fog band " << mapViewer_.fogNear() << ".." << mapViewer_.fogFar()
              << " fog colour 0x" << std::hex << mapViewer_.fogColour() << std::dec << '\n'
              << "[render] light1 0x" << std::hex << environment.uGpffffb6fc_globalRgb
              << " light2 0x" << environment.uGpffffb700_vectorRgb << std::dec
              << " dir1 " << environment.DAT_003439c8_vector[0]
              << ',' << environment.DAT_003439c8_vector[1]
              << ',' << environment.DAT_003439c8_vector[2]
              << "  (ambient 0x2a7 / light0 0x2a3 / dir 0x2a0..2, VU1 0x1b2)\n"
              << "[render] " << lightFloorSummary() << '\n'
              << "[render] gleam half-vector "
              << mapViewer_.sceneLighting().gleamDirection.x << ','
              << mapViewer_.sceneLighting().gleamDirection.y << ','
              << mapViewer_.sceneLighting().gleamDirection.z
              << "  (VU1 mem[0x18], DAT_00314800)\n"
              << "[render] lighting toggles: floor="
              << (mapViewer_.sceneLighting().applyLightFloor ? "on" : "off")
              << " unlit=" << (mapViewer_.sceneLighting().applyUnlitFlag ? "on" : "off")
              << " points=" << (suppressPointLights_ ? "off" : "on") << "/"
              << mapViewer_.sceneLighting().pointLightCount
              << '\n'
              << "[render] primitives=" << visibilityReport_.primitiveCount
              << " drawn=" << visibilityReport_.drawn
              << " hidden=" << visibilityReport_.hiddenSkipped
              << " behind-near-plane=" << visibilityReport_.nearRejected
              << " outside-sides=" << visibilityReport_.sideRejected
              << " beyond-draw-distance=" << visibilityReport_.drawDistanceRejected
              << " near-clipped=" << visibilityReport_.nearClipped << '\n'
              << "[render] of the drawn set, " << visibilityReport_.drawnFrontFacing
              << " face the camera and " << visibilityReport_.drawnBackFacing
              << " face away (culled unless two-sided)\n"
              << "[render] fade: " << visibilityReport_.faded << " fading of "
              << visibilityReport_.fadeCandidates << " candidates; blocked by blend "
              << visibilityReport_.fadeBlockedByBlend << ", height "
              << visibilityReport_.fadeBlockedByHeight << ", band "
              << visibilityReport_.fadeBlockedByBand << ", overlap "
              << visibilityReport_.fadeBlockedByOverlap << '\n'
              << "[render] " << fadingNow << " primitives hold a fade below the ceiling value\n"
              << "[render] winding oracle: " << ceilingPointingDown << '/' << ceilingCount
              << " 0x100 primitives point down; others: " << otherUp << " up, "
              << otherDown << " down, " << otherFlat << " vertical\n"
              << "[render] " << twoSided << " primitives are two-sided (flag 0x1) and skip culling\n";

    if (printGleamReport_)
    {
      std::cout << "[gleam] " << gleamProbes_.size()
                << " models reached the specular pass on the final rendered frame"
                << (gleamProbes_.empty()
                        ? "  <- nothing measured. The probe only fills from render(),"
                          " so run windowed with the model on screen and close the"
                          " window to print this."
                        : "")
                << '\n';
      for (const auto &probe : gleamProbes_)
      {
        std::cout << "[gleam]  slot=" << probe.slot << " type=0x" << std::hex
                  << probe.typeId << std::dec << "  prims=" << probe.primitivesTested
                  << " corners=" << probe.cornersEvaluated << " lit=" << probe.cornersLit
                  << "  maxDot=" << probe.maxDot << " maxAlpha=" << probe.maxOpacity
                  << "  |N|=" << probe.minNormalLength << ".." << probe.maxNormalLength
                  << '\n';
      }
      // A model absent from that list never reached the pass at all, which is a
      // different failure from reaching it and never clearing the threshold.
      // |N| away from 1.0 means posedWorldNormal is scaling, which inflates the
      // dot product and would over-brighten every highlight it feeds.
    }

    // The near-plane polygon clip itself is not ported: the original hands
    // those primitives to FUN_00209ca0 -> FUN_0020b600, while the port draws
    // them whole and lets GL clip. The fade path for them *is* ported, and it
    // is the looser of the two condition sets.
    if (visibilityReport_.nearClipped > 0)
    {
      std::cout << "[render] " << visibilityReport_.nearClipped
                << " primitives straddle the near plane; GL clips them rather than FUN_0020b600\n";
    }
  }

  // Resolves every live entity's behavior straight from the pool rather than
  // from the trace, so the report is meaningful under --load-only, before a
  // single frame has run. Trace counts are appended when frames have run.
  void PortRuntime::printActorReport() const
  {
    std::cout << "\n=== actor report ===\n";
    if (!actorDispatchTable_.available())
    {
      std::cout << "no executable loaded; the actor behavior tables could not be read.\n"
                   "Pass --elf <SLUS_200.11> or put it in the disc root.\n";
    }

    // Slot 0 is not in the loop below -- FUN_00239ce0 starts at slot 2 -- but
    // where the player is standing decides every floor-panel test, so the report
    // is not usable for that question without it.
    {
      const auto &lead = entityPool_.leadPlayer();
      std::cout << "  slot=0 LEAD pos=(" << std::fixed << std::setprecision(2)
                << lead.positionX20 << "," << lead.positionZ24 << "," << lead.positionY28 << ")"
                << " facing=" << std::setprecision(3) << lead.facingRadians5c << std::defaultfloat
                << " state=" << lead.state60
                // +0x04, the debug overlay's AF. Bit 0x100 turns FUN_002262c0
                // off entirely, so it decides whether anything below is physics
                // output or a scripted pose.
                << " af=0x" << std::hex << std::setw(4) << std::setfill('0') << lead.halfword04
                << std::setfill(' ') << std::dec
                << " terrainWord=0x" << std::hex << lead.flagWord6c << std::dec
                << " (panel bits 0x" << std::hex << (lead.flagWord6c & 0xF0u) << std::dec << ")\n";
    }

    std::size_t live = 0;
    entityPool_.forEachActive(
        [&](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity)
        {
          if (entityPool_.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
          {
            return;
          }
          ++live;
          const auto handler = actorDispatchTable_.FUN_00239ce0_resolve(entity.typeId00);
          const bool implemented =
              handler.address != 0 &&
              orphen::ported::entity::actorHandlerIsImplemented(handler.address);
          const char *name = orphen::ported::entity::actorHandlerName(handler.address);

          std::cout << "  slot=" << slot
                    << " type=0x" << std::hex << entity.typeId00 << std::dec;
          // Type 0x38 is a role opcode 0x66 stamps over the real type, which it
          // parks at +0x1CE. Printing the raw id alone makes fourteen different
          // characters look like fourteen copies of one thing, and hides which
          // script body each is running.
          if (entity.typeId00 == 0x38)
          {
            std::cout << "(was 0x" << std::hex << entity.originalType1ce
                      << " body 0x" << entity.recordId130 << std::dec << ")";
          }
          std::cout
                    << " state=" << entity.state60
                    << " anim=" << entity.animationA0
                    // +0x04, so a report can be diffed straight against an EE
                    // dump's entity block. Bit 3 in particular decides whether a
                    // waypoint path is allowed to move an actor vertically.
                    << " af=0x" << std::hex << std::setw(4) << std::setfill('0')
                    << entity.halfword04 << std::dec << std::setfill(' ')
                    << " pos=(" << std::fixed << std::setprecision(2)
                    << entity.positionX20 << "," << entity.positionZ24 << "," << entity.positionY28 << ")"
                    << " floor=" << entity.groundHeight4c;
          // What the map says is under the entity right now, asked the way
          // FUN_00227070 asks it -- within the entity's own feet-to-head band.
          // Without the band a stacked map answers with the wrong storey, which
          // is how s01_e012's cast ended up on the upper deck.
          if (const auto *map = mapViewer_.loadedMap(); map != nullptr)
          {
            const Psm2TerrainQueryOptions options{
                0, false, Psm2ActorBody{entity.positionY28, entity.positionY28 + entity.height58}};
            const auto hit = queryPsm2GroundAt(*map, entity.positionX20, entity.positionZ24,
                                               entity.positionY28, options);
            std::cout << " terrain=";
            if (hit.has_value())
            {
              std::cout << hit->height;
            }
            else
            {
              std::cout << "none";
            }
          }
          std::cout << " facing=" << std::setprecision(3) << entity.facingRadians5c
                    << std::defaultfloat;
          // FUN_0020c5a8's first-pass skip. Worth printing: an entity that
          // ticks but is never submitted looks identical to a missing one in
          // every other column.
          if ((entity.descriptorFlags02 & 0x0200u) != 0)
          {
            std::cout << " NODRAW";
          }
          std::cout << " " << actorHandlerSourceName(handler.source);
          if (handler.address != 0)
          {
            std::cout << " -> 0x" << std::hex << handler.address << std::dec;
            if (name != nullptr)
            {
              std::cout << ' ' << name;
            }
            std::cout << (implemented ? "  implemented" : "  UNIMPLEMENTED");
          }
          else
          {
            std::cout << "  UNRESOLVED";
          }
          std::cout << '\n';

          // The bandana's entity position is a bone-local offset, so the line
          // above says nothing about where it actually is. Its rope is the
          // interesting state, and `s01_e24.bin` has the same numbers to check
          // against: the anchor at (-3.31224, -12.75, 0.93703) with the player
          // standing at (-3.25, -12.75, 0) and each link exactly 0.025 long.
          if (entity.typeId00 == orphen::ported::entity::kBandanaTypeId)
          {
            for (std::size_t chainIndex = 0;
                 chainIndex < orphen::ported::entity::kBandanaChainCount; ++chainIndex)
            {
              const auto &chain = DAT_0054ee00_bandana_.chains[chainIndex];
              const auto &anchor = chain.segments.front().position;
              const auto &tip = chain.segments.back().position;
              const float dx = tip.x - anchor.x;
              const float dy = tip.y - anchor.y;
              const float dz = tip.z - anchor.z;
              // The tip's bone override translation, which is what the dump
              // pins directly: (spread, DAT_003151a0[9], -0.225) at rest, i.e.
              // (-0.011, 0.067, -0.225) for chain 0 and (+0.011, ...) for chain
              // 1. It is the number the sin/cos identity gets wrong -- swap them
              // and the clamp lands in the first slot instead of the second.
              const std::size_t tipBone = chainIndex * 9u + 1u;
              const auto &pose = DAT_004a7e00_boneOverrides_[slot].overrides[tipBone].fields;
              std::cout << "    chain " << chainIndex << " gravity=" << std::fixed
                        << std::setprecision(3) << chain.gravity << " anchor=("
                        << std::setprecision(5) << anchor.x << "," << anchor.y << ","
                        << anchor.z << ") tip=(" << tip.x << "," << tip.y << "," << tip.z
                        << ") span=" << std::sqrt(dx * dx + dy * dy + dz * dz)
                        << " tipBone" << tipBone << ".t=(" << pose[0] << "," << pose[1]
                        << "," << pose[2] << ")" << std::defaultfloat << '\n';
            }
          }
        });

    std::cout << "live actors: " << live << " (slots 0 and 1 are not ticked by FUN_00239ce0)\n";

    if (!actorTrace_.types().empty())
    {
      std::cout << "dispatched over " << frameCount_ << " frames:\n";
      for (const auto &entry : actorTrace_.types())
      {
        std::cout << "  type=0x" << std::hex << entry.first << std::dec
                  << " entities=" << entry.second.entityCount
                  << " ticks=" << entry.second.tickCount
                  << " firstSlot=" << entry.second.firstSlot
                  << (entry.second.implemented ? "  implemented" : "  UNIMPLEMENTED") << '\n';
      }
      if (!actorTrace_.states().empty())
      {
        std::cout << "state handlers reached (the second dispatch, on entity +0x60):\n";
        for (const auto &entry : actorTrace_.states())
        {
          std::cout << "  type=0x" << std::hex << entry.first.first << std::dec
                    << " state=" << entry.first.second
                    << " -> 0x" << std::hex << entry.second.handlerAddress << std::dec
                    << " ticks=" << entry.second.tickCount
                    << (entry.second.implemented ? "  implemented" : "  UNIMPLEMENTED") << '\n';
        }
        std::cout << "unimplemented state handlers: " << actorTrace_.unimplementedStateCount() << '\n';
      }

      std::cout << "skipped: hidden=" << actorTrace_.hiddenCount()
                << " suspended=" << actorTrace_.suspendedCount()
                << " fading=" << actorTrace_.fadingCount() << '\n';
      std::cout << "unimplemented behaviors: " << actorTrace_.unimplementedTypeCount()
                << " distinct types, " << actorTrace_.unimplementedEntityCount() << " entities\n";
    }
    {
      const auto &collision = orphen::ported::entity::entityCollisionStats();
      std::cout << "entity collision: sweeps=" << collision.sweeps
                << " clamps=" << collision.clamps << " shoves=" << collision.shoves << '\n';
    }
    {
      const auto &hits = orphen::ported::entity::hitTestStats();
      std::cout << "hit tests: sweeps=" << hits.tests << " boxes=" << hits.boxes
                << " contacts=" << hits.contacts << " damage=" << hits.damage << "\n";
      if (hits.lastSweepValid)
      {
        std::cout << "  last sweep box: x [" << hits.lastSweepBounds[0] << ", "
                  << hits.lastSweepBounds[1] << "] y [" << hits.lastSweepBounds[2] << ", "
                  << hits.lastSweepBounds[3] << "] z [" << hits.lastSweepBounds[4] << ", "
                  << hits.lastSweepBounds[5] << "]\n";
      }
    }
    // DAT_00355620. Zero alive with a behaviour installed means every particle
    // a burst seeded has since faded out, which is the normal resting state.
    std::cout << "particles: alive=" << DAT_00355620_particles_.aliveCount()
              << " behaviour="
              << (DAT_00355620_particles_.DAT_00355e0c_behaviour() ==
                          orphen::ported::entity::ParticleBehaviour::FUN_002d2348_sparks
                      ? "FUN_002d2348"
                      : "none")
              << "\n";

    // DAT_00355B74. `groups` is cGpffffbc0c, so it is also how many hits are
    // currently showing -- ten is the ceiling and an eleventh shows nothing.
    std::cout << "hit sparks: alive=" << DAT_00355b74_hitSparks_.aliveCount()
              << " groups=" << static_cast<int>(DAT_00355b74_hitSparks_.DAT_00355b7c_activeGroups())
              << "\n";
    std::cout << "motion trails: slots=" << DAT_004fbc7c_weaponTrails_.liveSlotCount()
              << " mask=0x" << std::hex << DAT_004fbc7c_weaponTrails_.DAT_00355a3c_mask()
              << std::dec << "\n";

    std::cout << "=== end actor report ===\n\n";
  }

  // The outcome half of the confirm button. FUN_00252828 decides *what* an
  // interaction is; this applies it, because the two outcomes live in systems
  // the player controller has no business reaching -- one is an event flag, the
  // other is a scene script entry.
  bool PortRuntime::runInteractionProbe()
  {
    const auto result = orphen::ported::player::FUN_00252cc0_probe_for_interaction(
        entityPool_,
        0,
        [this](std::uint32_t flagId) { return sceneScript_.state().FUN_00266368_eventFlag(flagId); });

    switch (result.kind)
    {
    case orphen::ported::player::InteractionKind::Chest:
    {
      // FUN_00252828's chest branch: state 0xC with animation 1. Everything
      // after that is the cutscene, which runs out of the player state table
      // -- see ported/player/original_chest_cutscene.*. It is what sets the
      // event flag, on the frame animation 0x57 reaches its marked keyframe.
      //
      // FUN_00252828 has already written the chest's slot to the player's
      // +0x198 and 0x4B00 to +0x1B8; the probe reproduces both.
      auto &lead = entityPool_.leadPlayer();
      lead.state60 = 0x0C;
      lead.stateResetA4 = 999;
      lead.animationA0 = 1;
      lead.previousSubstateA2 = 0xffff;
      lead.flags06 &= 0xff38;
      lead.timelineCursorA8 = 0;
      std::cout << "[interact] chest slot=" << result.targetSlot
                << " flag=0x" << std::hex << result.chestFlagId << std::dec
                << " -> player state 0xC (chest cutscene)\n";
      return true;
    }

    case orphen::ported::player::InteractionKind::ScriptedEntity:
    {
      // Party members (types 0x03..0x07) come here. The original points
      // psGpffffb79c at the entity and runs the scene script's header word 3;
      // the port selects the same entity and runs the same entry, and if that
      // entry reaches an opcode with no implementation the existing report
      // names it rather than pretending the interaction worked.
      //
      // The actual party swap -- rebinding pool slot 0, the camera and the
      // controller to another entity -- is not implemented.
      //
      // **FUN_0025b978 is three statements, and the port only had two:**
      //
      //   psGpffffb0d4 = psGpffffb79c;                    // select the target
      //   if (*psGpffffb79c == 0x38) target[+0x1CC] = 1;  // the interact pulse
      //   FUN_0025bc68(header word 3);
      //
      // The pulse is the whole mechanism for anything the *script* owns. A
      // type-0x38 entity's behaviour, FUN_0025bf20, re-enters its body from the
      // top every frame with itself as both selection and focus, and the body
      // tests +0x1CC through 0xE9 -- read and clear. With nothing ever setting
      // it, that test read zero forever and every scripted object in the scene
      // was inert to the player.
      //
      // s01_e012's doors are exactly this. Seven type 0x293 entities, five live
      // after the opening, each converted by 0x66 to type 0x38 with its own
      // 0x78-byte body in 0x39d9..0x3d40. Each body reads:
      //
      //   if (work[14] == -1)            // no other door mid-animation
      //     if (0xE9) 0xEC(1);           // interacted with -> step 1
      //     switch (0xED) {
      //       case 1: work[14] = <door id>; 0x90 <audio>;
      //               0x9D(0xA0, 0xb348);   // queue the body at 0xb348
      //               0xEC(2);
      //       case 2: 0xF5 ...
      //     }
      //
      // and 0xb348 is subproc **4739**, the shared door driver keyed on
      // work[14] -- which is why the debug overlay names the same subproc id
      // whichever of the five doors is opened.
      //
      // FUN_0025b978 sets the selection but *not* the focus (iGpffffb0d8); the
      // focus is installed by FUN_0025bf20 when the entity's own body runs.
      // FUN_00239ce0 comes after FUN_00251ed8 in FUN_002239c8, so the pulse is
      // raised and consumed on the same frame.
      auto &scriptedTarget = entityPool_.slot(result.targetSlot);
      if (scriptedTarget.typeId00 == 0x38)
      {
        scriptedTarget.interactPulse1cc = 1;
      }
      std::cout << "[interact] scripted entity slot=" << result.targetSlot
                << " type=0x" << std::hex << result.targetType << std::dec
                << (scriptedTarget.typeId00 == 0x38 ? " pulse +0x1CC=1" : "")
                << " -> scene script header word 3\n";
      sceneScript_.runEntryForEntity(orphen::ported::script::SceneScriptEntry::ActorStatePrimary,
                                     scriptEnvironment(),
                                     scriptTrace_,
                                     result.targetSlot);
      reportTickHalt("interaction (header word 3)");
      return true;
    }

    case orphen::ported::player::InteractionKind::StreamedProp:
      std::cout << "[interact] streamed prop slot=" << result.targetSlot
                << " type=0x" << std::hex << result.targetType << std::dec
                << " (branch not ported)\n";
      return true;

    case orphen::ported::player::InteractionKind::None:
    default:
      return false;
    }
  }

  // The panels are the one thing in this scene that fires from where you stand
  // rather than from a button, so a windowed session has no way to tell whether
  // one triggered. Reported on the rising edge: once when you step on, again
  // only if you step off and back on.
  void PortRuntime::reportPanelActivity()
  {
    for (const auto &entry : scriptTrace_.terrainTriggers())
    {
      const bool passing = entry.second.passes != 0 && entry.second.observedWord != 0 &&
                           (entry.second.observedWord & entry.second.mask) != 0;
      bool &wasPassing = triggerWasPassing_[entry.first];
      if (passing && !wasPassing)
      {
        std::cout << "[trigger] terrain flag 0x" << std::hex << entry.second.mask
                  << " matched (script @0x" << entry.first << std::dec << ")\n";
      }
      wasPassing = passing;
    }

    if (scriptTrace_.fadesArmed().size() > reportedFadeArms_)
    {
      reportedFadeArms_ = static_cast<std::uint32_t>(scriptTrace_.fadesArmed().size());
      const auto &fade = scriptTrace_.fadesArmed().back();
      std::cout << "[panel] scene transition: fullscreen fade armed, bank=" << fade.bank
                << " rate=" << fade.rate
                << " rgb=0x" << std::hex << fade.packedRgb << std::dec << '\n';
    }
    if (scriptTrace_.battleBootCount() != 0 && reportedBattleBoots_ == 0)
    {
      reportedBattleBoots_ = 1;
      std::cout << "[panel] save point: mode 0x10 raised (no menu to hand off to)\n";
    }
    std::uint32_t locks = 0;
    for (const auto &entry : scriptTrace_.playerLocks())
    {
      locks += entry.second;
    }
    if (locks != 0 && reportedPlayerLocks_ == 0)
    {
      reportedPlayerLocks_ = 1;
      std::cout << "[panel] player control taken (0x6D); the lead's state-10 handler"
                   " is not ported, so it does not hold\n";
    }
  }

  void PortRuntime::reset()
  {
    memory_.clear();
    frameCount_ = 0;
    mapViewer_.resetCamera();
    resetLeadPlayerForLoadedMap();
  }

  bool PortRuntime::update(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    ++frameCount_;
    // DAT_003555b4 and DAT_003555b8. Three EE dumps read b8 == b4 * 32 exactly,
    // so b8 accumulates DAT_003555bc rather than counting frames -- a dropped
    // frame advances a wave phase by the time it actually took. Nothing writes
    // them in the decompiled sources, which is how they were pinned down.
    ++DAT_003555b4_frameCounter_;
    DAT_003555b8_tickCounter_ += frameTicks;
    soundEngine_.setFrame(frameCount_);
    // FUN_002239c8:22, ahead of the pad publish and of everything the frame
    // does. A scene change asked for last frame lands here, so no part of a
    // frame ever runs half on one scene and half on the next.
    FUN_002239c8_service_scene_change();
    // FUN_0023b5d8's slot: the pad's analog magnitude is published before
    // anything downstream of it runs.
    DAT_003555e8_stickMagnitude_ = input.stickMagnitude;
    scriptTrace_.setFrame(static_cast<std::uint32_t>(frameCount_));
    dialogueStream_.setFrame(static_cast<std::uint32_t>(frameCount_));
    // FUN_00237de8's slot in the frame: the stream ages before the script runs,
    // so a slot polling opcode 0x35 sees this frame's answer.
    dialogueStream_.update(frameTicks, sceneScript_.state());
    // The viewer's free camera and the follow-yaw easing still think in seconds.
    // One simulation step is one nominal frame, so hand them that directly
    // rather than wall-clock time -- this is what makes --frames deterministic.
    const float stepSeconds = orphen::ported::kNominalFrameSeconds *
                              (static_cast<float>(frameTicks) / static_cast<float>(orphen::ported::kNominalFrameTicks));
    mapViewer_.update(stepSeconds, input);

    if (input.captureSnapshotRequested ||
        (snapshotFrame_ != 0 && frameCount_ == snapshotFrame_))
    {
      writeDiagnosticSnapshot();
    }

    if (input.toggleSubprocDisplayRequested)
    {
      toggleSubprocDisplay();
      std::cout << "[debug] SCR SUBPROC DISP " << (subprocDisplayEnabled() ? "ON" : "OFF") << '\n';
    }

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      // A cycled map is a scene load, not just a new mesh: its script owns the
      // fog, the draw distance and the entity set, and its bundle owns the
      // models. resetLeadPlayerForLoadedMap alone left all of that on the
      // previous scene's values.
      //
      // The cycle is the port's stand-in for FUN_0022b300, which writes
      // DAT_003551ec = 0x2001 for every scene it walks to -- bit 0x20000 clear,
      // so DAT_003555d3 goes back to zero. Without that, a cycle taken after a
      // group-0xE handoff would keep the sticky flag up and go on reporting the
      // sending stage's prop bank for every scene after it.
      DAT_003555d3_groupEScene_ = false;
      loadSceneForCurrentMap();
    }
    auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      // The player moves relative to the camera's yaw from the previous frame,
      // then the camera follows the new position -- the same one-frame ordering
      // the original has, since FUN_00251ed8 runs before FUN_00216aa0.
      const float cameraYaw = fieldCamera_.yawRadians();
      const orphen::ported::psm2::Vec3 forwardBasis{std::cos(cameraYaw), std::sin(cameraYaw), 0.0f};
      const orphen::ported::psm2::Vec3 rightBasis{std::sin(cameraYaw), -std::cos(cameraYaw), 0.0f};
      const orphen::ported::psm2::Vec3 movementRequest{
          rightBasis.x * input.moveX + forwardBasis.x * input.moveY,
          rightBasis.y * input.moveX + forwardBasis.y * input.moveY,
          0.0f};

      // FUN_002239c8's order: FUN_0025b778 (the script tick) runs before the
      // lead player update, FUN_00239ce0 (the actors) after it, and
      // FUN_0025b918 (the late object-script slots) after that. The camera,
      // FUN_00216aa0, comes last.
      // FUN_002245d8, the mode 6 frame, runs FUN_00251ed8 and FUN_00239ce0 and
      // nothing else from the field frame -- no FUN_0025b778, no FUN_0025b918
      // and, crucially, no FUN_00216aa0. That last omission is what leaves a
      // cutscene camera where FUN_00217d70 put it.
      const bool cutsceneFrame = DAT_00354d2c_gameMode_ == orphen::ported::player::kGameModeCutscene;

      // --arm-stream, applied just before the tick that will first pay it out.
      // This is what a floor panel's body does with opcode 0xA1: set channel
      // 0's cursor and zero its timer.
      if (armStreamPending_ && frameCount_ >= armStreamFrame_ && sceneScript_.loaded())
      {
        armStreamPending_ = false;
        auto &channel = sceneScript_.state().DAT_00571e40_eventChannels[0];
        channel.cursor = armStreamOffset_;
        channel.timer = 0;
        std::cout << "[arm-stream] channel 0 <- 0x" << std::hex << armStreamOffset_ << std::dec
                  << " at frame " << frameCount_ << '\n';
      }

      if (!cutsceneFrame && runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b778_run_tick(scriptEnvironment(frameTicks), scriptTrace_);
        reportTickHalt("tick");
      }

      // Raw pad 0x0020 is Circle, the attack button -- keyboard `C`. It goes
      // into the mapped-action ring below as the attack bit; this separate
      // read is the *held* state, which gates the debug mid-air jump.
      constexpr std::uint16_t kRawPadCircle = 0x0020;
      // Raw pad 0x0040 is Cross, the confirm button. The original tests the
      // same bit in the *mapped* pressed word (uGpffffb68a = DAT_003555fa);
      // Cross maps through to the same position, and the port has no mapping
      // table, so the raw bit stands in.
      constexpr std::uint16_t kRawPadCross = 0x0040;
      // FUN_0023b5d8's tail. The remap table at DAT_00571A50 is the identity in
      // both EE dumps, so a mapped action bit *is* the raw pad bit and the pad
      // word can be pushed as-is: 0x80 Square is jump, 0x20 Circle is attack,
      // 0x10 Triangle is use. The push has to happen every step, including the
      // catch-up steps of a slow frame, or the eight-frame window would be
      // measured in render frames rather than in simulation ones.
      DAT_00342a70_mappedActions_.FUN_0023b5d8_push(input.rawHeldPad, input.rawPressedPad);

      leadPlayer_.update(frameTicks,
                         movementRequest,
                         input.stickMagnitude,
                         DAT_00342a70_mappedActions_.FUN_0023b890_recent(8),
                         (input.rawHeldPad & kRawPadCircle) != 0,
                         (input.rawPressedPad & kRawPadCross) != 0,
                         loadedMap,
                         [this] { return runInteractionProbe(); });

      // FUN_002446e8 must land before the actor loop: it writes the movement
      // request at +0x30/+0x34 and the physics inside that loop is what spends
      // it. Run the other way round and every path-driven step is a frame late
      // and gets cleared before it is applied.
      pathFollowers_->FUN_002446e8_update(entityPool_, frameTicks);

      // --place-slot. Debug scaffolding, applied just before the actor loop so
      // a behaviour sees the entity where the flag put it. Nothing in the
      // original does this; it exists so contact between two entities can be
      // set up on demand instead of waiting for a scene to arrange it.
      for (const auto &placed : placedSlots_)
      {
        if (placed.slot < 0 ||
            static_cast<std::size_t>(placed.slot) >= orphen::ported::entity::kEntitySlotCount)
        {
          continue;
        }
        auto &entity = entityPool_.slot(static_cast<std::size_t>(placed.slot));
        entity.positionX20 = placed.position.x;
        entity.positionZ24 = placed.position.y;
        entity.positionY28 = placed.position.z;
      }

      orphen::ported::entity::FUN_00239ce0_update_actors(actorEnvironment(frameTicks), actorTrace_);

      // FUN_002d3218, in the slot FUN_002239c8:125 gives it -- immediately
      // after FUN_00239ce0, so a burst spawned by a behaviour this frame gets
      // its first step on the next one rather than on the frame it was seeded.
      DAT_00355620_particles_.FUN_002d3218_step(frameTicks);

      // FUN_00208450, in its own slot in FUN_002239c8: after FUN_00239ce0 and
      // before FUN_0025b918's late slots. It spends whatever the tick wrote
      // into the collision groups' channels -- the doors of opcodes 0x7D/0x7E,
      // and s01_e012's sea, which opcode 0xBE rolls through FUN_002676d8.
      //
      // It has to run even on a frame nothing wrote to, because the loader
      // seeds every group's dirty byte with 3: the first pass is what applies
      // the identity transform and fills the derived bounds, normals and the
      // 0x10000 dynamic bit that FUN_00227840's second loop reads.
      if (auto *loadedMapForGroups = mapViewer_.loadedMap())
      {
        // The return value is DAT_003555d0: how many collision groups had a
        // live dirty byte this frame. FUN_002262c0:112 reads it as a flag, and
        // it is what lets a stationary actor resample the floor and be pushed
        // out of anything a moving door or the sea has swallowed.
        DAT_003555d0_collisionGroupMoved_ =
            orphen::ported::psm2::FUN_00208450_update_collision_groups(*loadedMapForGroups) != 0;
        if (DAT_003555d0_collisionGroupMoved_)
        {
          ++DAT_003555d0_liveFrames_;
        }


        // FUN_00208F28: step the map's UV animation, gated on iGpffffb788
        // (DAT_003556F8) being non-null, immediately before FUN_00209140 walks
        // the primitives. This is the rain outside the windows and the flicker
        // on the lantern glows -- an offset added to baked coordinates, which
        // is why none of the geometry or texture state ever changes.
        orphen::ported::psm2::FUN_00225940_step_uv_animation(
            loadedMapForGroups->DAT_003556f4_uvAnimation, frameTicks);
      }

      // FUN_002239c8:135. FUN_002261e0 is the very next call after
      // FUN_00208450, so the physics walk reads DAT_003555d0 on the same frame
      // it was set. It runs unconditionally -- the original does not gate it on
      // a map being present.
      orphen::ported::entity::FUN_002261e0_update_physics(actorEnvironment(frameTicks));

      // FUN_002239c8:134-138 -- FUN_00208450, the physics pass, FUN_00224060,
      // then the late slots. The lead's breadcrumb goes down after it has
      // finished moving for the frame and before anything can read it back.
      FUN_00224060_record_lead_trail();

      if (!cutsceneFrame && runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b918_run_late_slots(scriptEnvironment(frameTicks), scriptTrace_);
        reportTickHalt("late slots");
        reportPanelActivity();
      }

      // The scene's lighting and fog are script globals, and FUN_00200e38
      // rebuilds VU1's lighting VIF packets from them on *every* frame -- so a
      // script that rewrites them per tick is rewriting what the next frame is
      // lit by. This used to run only at scene load, which threw away all
      // 13,002 of s01_e012's per-frame 0x97 writes and left the scene lit by
      // whatever the load-time entry happened to leave behind.
      applySceneEnvironment();

      // Behaviors can move and turn entities, so the render views are rebuilt
      // every frame now rather than only at load.
      advanceEntityAnimations(frameTicks);
      publishSceneObjectViews(frameTicks);

      const auto &leadState = leadPlayer_.viewState();
      orphen::ported::camera::FieldCameraInput cameraInput;
      cameraInput.rawHeldPad = input.rawHeldPad;
      cameraInput.rawPressedPad = input.rawPressedPad;
      cameraInput.stickAngle = input.stickAngle;
      cameraInput.stickMagnitude = input.stickMagnitude;
      cameraInput.previousStickMagnitude = previousStickMagnitude_;
      cameraInput.autoFocusGoalYaw = leadState.facingRadians;
      previousStickMagnitude_ = input.stickMagnitude;

      if (!cutsceneFrame)
      {
        fieldCamera_.FUN_00216aa0_update(frameTicks, cameraInput, leadState.position, cameraGroundSampler());
      }

      mapViewer_.setLeadPlayerView(leadState);
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
      updateMapVisibility(*loadedMap, leadState, frameTicks);
    }
    else
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
      mapViewer_.setMapDrawList({});
    }
    reportLeadPlayerGroundChange();

    // FUN_00237fc0, which mode 6 runs after the actors. Cross is raw pad 0x40,
    // the same bit the interaction probe reads.
    itemWindow_.FUN_00237fc0_update(frameTicks, (input.rawPressedPad & 0x0040) != 0);
    // The same call, for the cutscene subtitles: this is where FUN_00237fc0
    // sits in FUN_002239c8's frame, after the script has had its turn, so a
    // record opened this frame types its first character this frame.
    //
    // iGpffffb0e4 is a live global read inside FUN_00238a08, and the walk is
    // downstream of FUN_0025cfb8 in the original's frame, so the mode published
    // here is this frame's -- a line typed on the frame the bars are armed is
    // already clear of them.
    dialogueStream_.setMovieMode(DAT_00355054_letterbox_.DAT_00355054_mode());
    dialogueStream_.FUN_00237fc0_update(frameTicks);
    mapViewer_.setDialogueSprites(buildDialogueSprites());

    // FUN_00267a80 measures against DAT_0058C0A8 and uGpffffb6d4 -- the camera,
    // not the player. Published after the camera has run for the frame.
    //
    // DAT_0058C0A8 is not a standalone global: it is **pool slot 1's +0x20**,
    // the camera entity FUN_00228e28 builds at boot. Writing the eye there is
    // what lets a script cue aimed at selector 1 play at the listener -- the
    // engine's "not positional" idiom, which s01_e012 uses for the storm's
    // thunder and Volcan's sword.
    {
      auto &camera = entityPool_.slot(orphen::ported::entity::kCameraSlot);
      camera.typeId00 = orphen::ported::entity::kCameraSlotType;
      camera.positionX20 = fieldCamera_.pose().eye.x;
      camera.positionZ24 = fieldCamera_.pose().eye.y;
      camera.positionY28 = fieldCamera_.pose().eye.z;
      camera.groundHeight4c = orphen::ported::entity::kCameraSlotGroundHeight;
      camera.previousGroundHeight50 = orphen::ported::entity::kCameraSlotGroundHeight;
    }

    soundEngine_.setListener({fieldCamera_.pose().eye.x, fieldCamera_.pose().eye.y,
                              fieldCamera_.pose().eye.z, fieldCamera_.yawRadians()});

    // FUN_002000c0:214, which runs the smear once per frame between the world
    // and the bars. The step is here rather than in render() because it owns
    // the ramp in DAT_00354B88; render() takes the quad it produced.
    if (const std::optional<int> feedbackAlpha = DAT_00343878_frameFeedback_.FUN_00201a38_step();
        feedbackAlpha.has_value())
    {
      mapViewer_.setFrameFeedbackQuad(orphen::ported::render::FUN_00201a38_build_quad(
          DAT_00343878_frameFeedback_, *feedbackAlpha));
    }
    else
    {
      mapViewer_.setFrameFeedbackQuad(std::nullopt);
    }

    mapViewer_.setScreenFadeOverlay(DAT_00571dc0_screenFade_.overlay().colour,
                                    DAT_00571dc0_screenFade_.overlay().alpha);
    mapViewer_.setLetterboxBarHeight(DAT_00355054_letterbox_.barHeight());
    updateOriginalDebugOverlay();
    updateHud(input, frameTicks);

    memory_.write(kHarnessFrameCounterAddress, frameCount_);

    return true;
  }

  // FUN_00208ee8 then FUN_00208f28: build the camera matrices, then walk the
  // map deciding what is visible, what fades and in what order it draws. This
  // runs on the simulation step rather than in render() because the fade byte
  // is per-frame state -- the original's 0x80-record +0x2E -- and stepping it
  // at the render rate would make it frame-rate dependent.
  void PortRuntime::updateMapVisibility(orphen::ported::psm2::Psm2RuntimeState &map,
                                        const PlayerViewState &leadState,
                                        std::uint32_t frameTicks)
  {
    orphen::ported::render::FieldCameraView cameraView;
    cameraView.eye = fieldCamera_.pose().eye;
    cameraView.yawRadians = fieldCamera_.yawRadians();
    cameraView.pitchRadians = fieldCamera_.pitchRadians();
    // uGpffffb6dc / fGpffffb6e8. Both stand at their defaults until a camera
    // path moves them, which for this scene is the chest's contents swing.
    cameraView.rollRadians = fieldCamera_.uGpffffb6dc_roll();
    cameraView.zoomLog2 = fieldCamera_.fGpffffb6e8_zoomLog2();
    // FUN_0020bec8 spends the shake as a side effect of building the view, so
    // this has to stay on the simulation step -- one build a frame, like the
    // original -- rather than moving to render().
    cameraView.shake = &DAT_00355664_cameraShake_;
    cameraView.DAT_003555bc_frameTicks = frameTicks;
    renderCamera_ = orphen::ported::render::FUN_0020bec8_build(cameraView);
    renderCameraYaw_ = cameraView.yawRadians;

    orphen::ported::render::MapVisibilityInput visibilityInput;
    visibilityInput.DAT_0058bed0_playerPosition = leadState.position;
    visibilityInput.DAT_0058bf08_playerHeadOffset = leadState.bodyHeight;
    visibilityInput.drawDistance = mapViewer_.drawDistance();
    visibilityInput.globalFadeCap = DAT_00355700_globalFadeCap_;

    // The original culls at a fixed 90 degrees, wider than the 67.4 it draws.
    // A window wider than about 17:9 needs more than that, so widen rather
    // than pop geometry out at the edges. Headless runs have no framebuffer
    // yet and stay on the original's value, which keeps --frames deterministic.
    const int width = mapViewer_.lastFramebufferWidth();
    const int height = mapViewer_.lastFramebufferHeight();
    if (width > 0 && height > 0)
    {
      const auto camera = orphen::ported::render::glCameraFor(
          renderCamera_, width, height, orphen::ported::render::constants::kGeometryNearClip,
          visibilityInput.drawDistance);
      visibilityInput.horizontalCullHalfTangent = std::max(1.0f, camera.horizontalHalfTangent);
      visibilityInput.verticalCullHalfTangent = std::max(1.0f, camera.verticalHalfTangent);
    }

    mapViewer_.setRenderCamera(renderCamera_);
    // VU1 mem[0x18]: the specular half-vector. It depends on where the camera
    // is looking, so FUN_00200e38 rebuilds it every frame and so does this.
    mapViewer_.setGleamDirection(cameraView.yawRadians, cameraView.pitchRadians);
    mapViewer_.setMapDrawList(orphen::ported::render::FUN_00209140_buildDrawList(
        map, renderCamera_, visibilityInput, &visibilityReport_));
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
  }

  // FUN_0022a418:204, `DAT_0058beb4 = DAT_0058beb4 & 0xffe7 | 0x3000`.
  // DAT_0058BEB4 is pool slot 0's +0x04, and 0x1000 of what it sets is the bit
  // FUN_00256ff8 tests before it will play a footstep -- the party members'
  // +0x04 never gets it, which is why only the lead player is audible. Type 1's
  // descriptor supplies the low bits (0x0024), so this lands on the 0x3024 the
  // s01_e024 dump holds.
  //
  // Called after every placement rather than once, because the port re-runs
  // resetToMap as the spawn is narrowed down and each of those clears the slot.
  void PortRuntime::FUN_0022a418_stamp_lead_player_flags()
  {
    auto &lead = entityPool_.leadPlayer();
    std::uint16_t base = lead.halfword04;
    if (const auto descriptor =
            descriptorTable_.FUN_00229980_resolve(static_cast<std::uint32_t>(lead.typeId00));
        descriptor.has_value())
    {
      base = descriptor->halfword0x18;
    }
    lead.halfword04 = static_cast<std::uint16_t>((base & 0xFFE7u) | 0x3000u);
  }

  void PortRuntime::resetLeadPlayerForLoadedMap()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    trackedMapGeneration_ = mapViewer_.loadedMapGeneration();
    reportedGroundPrimitive_.reset();
    if (loadedMap == nullptr)
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
      return;
    }

    entityPool_.reset();
    // FUN_002d3290. Clearing the pool also drops DAT_00355e0c, so nothing from
    // the previous map keeps stepping.
    DAT_00355620_particles_.FUN_002d3290_reset([this] { return FUN_00216868_random(); });
    // FUN_002205d0 runs once at boot rather than per scene, but every entry it
    // leaves behind is dead and every group empty, so re-running it here is the
    // same state and it stops a burst surviving a map change.
    DAT_00355b74_hitSparks_.FUN_002205d0_reset();
    DAT_004fbc7c_weaponTrails_.reset();
    pendingTrailRibbons_.clear();
    for (auto &filter : DAT_003ffe00_poseFilters_)
    {
      filter.reset();
    }
    for (auto &overrides : DAT_004a7e00_boneOverrides_)
    {
      overrides.reset();
    }
    // A stale palette would put an attachment on the previous scene's bone
    // positions for one frame. The original's bank survives a map load, but the
    // original also rebuilds every entity that has one before anything reads it;
    // the port drops them so an unrebuilt slot reads as "no palette" instead.
    for (auto &palette : DAT_00357e00_bonePalettes_)
    {
      palette.clear();
    }
    mapViewer_.setSceneObjectViews({});
    leadPlayer_.bindEntity(entityPool_.leadPlayer());
    leadPlayer_.resetToMap(*loadedMap, spawnOverride_);
    FUN_0022a418_stamp_lead_player_flags();
    FUN_0022a418_reset_lead_trail();

    previousStickMagnitude_ = 0.0f;
    // A map load cannot leave a cutscene half-run behind it. FUN_0022a418:293
    // clears the fade cap for the same reason.
    DAT_00354d2c_gameMode_ = orphen::ported::player::kGameModeField;
    DAT_00355700_globalFadeCap_ = 0;
    itemSceneRenderState_ = false;
    DAT_00571dc0_screenFade_.reset();
    // FUN_0022a418:294 clears both smear alphas on a scene load.
    DAT_00343878_frameFeedback_.reset();
    mapViewer_.setFrameFeedbackQuad(std::nullopt);
    // FUN_00271220 / FUN_0022b300 / FUN_00225340 all clear DAT_00355054 on a
    // scene change, so the bars never survive one.
    DAT_00355054_letterbox_.reset();
    mapViewer_.setLetterboxBarHeight(0);
    dialogueStream_.setMovieMode(0);
    fieldCamera_.FUN_00217e18_release_manual_camera(false);
    fieldCamera_.FUN_00216930_install_normal_field_defaults();
    fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
    mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
    mapViewer_.setFollowCameraPose(fieldCamera_.pose());
  }

  void PortRuntime::reportLeadPlayerGroundChange()
  {
    const auto &leadState = leadPlayer_.viewState();
    if (!leadState.groundHit.has_value())
    {
      if (reportedGroundPrimitive_.has_value())
      {
        std::cout << "[player] ground=none\n";
        reportedGroundPrimitive_.reset();
      }
      return;
    }

    const auto &groundHit = *leadState.groundHit;
    if (reportedGroundPrimitive_ == groundHit.primitiveIndex)
    {
      return;
    }

    reportedGroundPrimitive_ = groundHit.primitiveIndex;
    std::cout << "[player] primitive=" << groundHit.primitiveIndex
              << " triangle=" << groundHit.triangleIndex
              << " z=" << groundHit.height
              << " leading=0x" << std::hex << groundHit.leadingWord
              << " terrain=0x" << groundHit.terrainFlags << std::dec
              << (groundHit.sampledByOriginalTerrain ? " sampled" : " unsampled") << '\n';
  }

  // The half of FUN_00251ed8's state-table dispatch the player controller does
  // not own. Installed on the controller, so it runs in the same place the
  // table entry would.
  bool PortRuntime::stepScriptedPlayerState(std::uint32_t frameTicks)
  {
    auto &lead = entityPool_.leadPlayer();
    if (!orphen::ported::player::isChestCutsceneState(lead.state60))
    {
      return false;
    }

    orphen::ported::player::ChestCutsceneContext context;
    context.pool = &entityPool_;
    context.player = &lead;
    context.fade = &DAT_00571dc0_screenFade_;
    context.camera = &fieldCamera_;
    context.DAT_00354d2c_gameMode = &DAT_00354d2c_gameMode_;
    context.DAT_00355700_globalFadeCap = &DAT_00355700_globalFadeCap_;
    context.setItemSceneRenderState = [this](bool enable) { setItemSceneRenderState(enable); };
    context.buildItemEntity = [this](std::size_t chestSlot, std::int16_t itemId) {
      return buildChestItemEntity(chestSlot, itemId);
    };
    context.openItemWindow = [this](std::size_t messageIndex, std::int32_t itemId) {
      itemWindow_.FUN_00237b38_open(messageIndex, itemId, itemDatabase_);
      std::cout << "[chest] message " << messageIndex << " \"" << itemWindow_.text() << "\"\n";
      if (itemWindow_.unhandledCode() != 0)
      {
        std::cout << "[chest] message stream stopped on control code 0x" << std::hex
                  << static_cast<int>(itemWindow_.unhandledCode()) << std::dec << '\n';
      }
    };
    context.itemWindowOpen = [this] { return itemWindow_.FUN_00237c60_isOpen(); };
    context.FUN_00267d38_playSound =
        [this](std::uint16_t cue, const orphen::ported::entity::OriginalEntity &at)
    { soundEngine_.FUN_00267d38_play_at(cue, at.positionX20, at.positionZ24, at.positionY28); };
    context.frameTicks = frameTicks;
    context.FUN_002663a0_setEventFlag = [this](std::uint32_t flagId) {
      sceneScript_.state().FUN_002663a0_setEventFlag(flagId);
      std::cout << "[chest] event flag 0x" << std::hex << flagId << std::dec << " set\n";
    };

    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      context.FUN_00227798_groundHeight =
          [loadedMap](float x, float y, float z) -> std::optional<float> {
        const auto hit = queryPsm2GroundAt(*loadedMap, x, y, z);
        if (!hit.has_value())
        {
          return std::nullopt;
        }
        return hit->height;
      };
    }

    const std::uint16_t before = lead.state60;
    orphen::ported::player::FUN_00251ed8_step_chest_cutscene(context);
    if (lead.state60 != before)
    {
      std::cout << "[chest] player state 0x" << std::hex << before << " -> 0x" << lead.state60
                << std::dec << " (frame " << frameCount_ << ")\n";
    }
    return true;
  }

  // FUN_002239c8 lines 140-165 followed by FUN_00268270. The original runs the
  // printf side inside the frame function and the layout side later in the
  // same frame; the port does both on the simulation step so the glyph list a
  // render sees is the one that frame produced, however many times it renders.
  // What FUN_00237fc0 would have in its glyph array this frame: the revealed
  // part of the caption on line 0 of the window FUN_00237b38 opened, and the
  // book prompt after it once the line is complete.
  //
  // The original builds this incrementally -- FUN_00238a08 appends one entry
  // per character as the stream runs and FUN_00237fc0 redraws the whole array
  // every frame -- so rebuilding it from the revealed count lands in the same
  // place for a single line, which is all a caption is.
  std::vector<orphen::ported::text::DialogueSprite> PortRuntime::buildDialogueSprites() const
  {
    namespace text = orphen::ported::text;
    if (!dialogueFont_.measured())
    {
      return {};
    }

    // The cutscene subtitles. These come out of the real glyph slot array, so
    // they are already placed, paced and wrapped; nothing is rebuilt here.
    // Only one of the two windows can be up at a time -- a chest is not opened
    // mid-cutscene -- but appending rather than choosing keeps that an
    // observation about the data instead of an assumption in the code.
    std::vector<text::DialogueSprite> sprites = dialogueStream_.sprites();

    if (!itemWindow_.FUN_00237c60_isOpen())
    {
      return sprites;
    }

    // The typewriter counts characters across the whole stream, so the reveal
    // spills from one line onto the next the way FUN_00237de8 emits them.
    std::size_t remaining = itemWindow_.revealedCharacters();
    int penX = 0;
    int lastLine = 0;
    for (const auto &line : itemWindow_.lines())
    {
      if (remaining == 0)
      {
        break;
      }
      const std::size_t take = std::min(remaining, line.text.size());
      remaining -= take;
      // iGpffffbcd8 steps the row and FUN_00238f98 puts the pen back to zero.
      penX = 0;
      lastLine = line.index;
      const int originY = text::kWindowOriginY + line.index * text::kLineStep;
      const std::vector<text::DialogueSprite> row = text::FUN_00238a08_layoutLine(
          std::string_view(line.text).substr(0, take), dialogueFont_, text::kWindowOriginX,
          originY, penX);
      sprites.insert(sprites.end(), row.begin(), row.end());
    }

    if (itemWindow_.awaitingConfirm())
    {
      sprites.push_back(text::promptSprite(text::kWindowOriginX,
                                           text::kWindowOriginY + lastLine * text::kLineStep, penX,
                                           static_cast<int>(itemWindow_.promptTicks())));
    }
    return sprites;
  }

  void PortRuntime::updateOriginalDebugOverlay()
  {
    // cGpffffb66a and cGpffffb66c. Both are cheat/menu bytes in the original
    // and there is no way into either yet, so the port holds them on -- which
    // is also the state the s01_e024 EE dump was captured in. Debug active
    // plus output on is what selects the detailed readout.
    DAT_00572c38_debugText_.setDAT_003555da_debugActive(true);
    DAT_00572c38_debugText_.setDAT_003555dc_outputEnabled(true);

    if (DAT_00355098_positionDisplay_)
    {
      const auto &lead = entityPool_.leadPlayer();
      const auto &pose = fieldCamera_.pose();

      orphen::ported::debug::PositionDisplayState state;
      state.DAT_0058bed0_playerX = lead.positionX20;
      state.DAT_0058bed4_playerY = lead.positionZ24;
      state.DAT_0058bed8_playerZ = lead.positionY28;
      state.DAT_0058bebc_moveFlags = lead.collisionFlags0c;
      state.DAT_0058beb4_attrFlags = lead.halfword04;
      state.DAT_0058beb8_stateFlags = lead.halfword08;
      state.DAT_0058beb6_nowFlags = lead.flags06;
      state.DAT_0058be90_targetX = pose.target.x;
      state.DAT_0058be94_targetY = pose.target.y;
      state.DAT_0058be98_targetZ = pose.target.z;
      state.DAT_0058c0a8_cameraX = pose.eye.x;
      state.DAT_0058c0ac_cameraY = pose.eye.y;
      state.DAT_0058c0b0_cameraZ = pose.eye.z;

      // DAT_003551f4 / DAT_003551f0. The MCB selection is the same pair: the
      // dump for s01_e024 holds 1 and 0x18, and the line reads MP0124.
      if (const auto scene = mapViewer_.loadedDiscScene())
      {
        state.iGpffffb284_mapSection = scene->section;
        state.uGpffffb280_mapEntry = scene->entry;
      }

      orphen::ported::debug::FUN_002239c8_emitPositionDisplay(DAT_00572c38_debugText_, state);
    }

    mapViewer_.setOriginalDebugGlyphs(DAT_00572c38_debugText_.FUN_00268270_layoutAndDrain());
  }

  void PortRuntime::updateHud(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    const auto &lead = leadPlayer_.viewState();
    std::vector<std::string> lines;

    lines.push_back("ORPHEN PORT  " + mapViewer_.loadedSourceDescription() +
                    "  FRAME " + std::to_string(frameCount_) +
                    "  TICKS " + std::to_string(frameTicks));

    lines.push_back("POS  " + formatNumber(lead.position.x) + ", " + formatNumber(lead.position.y) +
                    ", " + formatNumber(lead.position.z) +
                    "   FACING " + formatNumber(lead.facingRadians * kRadiansToDegrees, 1));

    lines.push_back("STATE " + std::string(stateName(lead.state)) +
                    "  ANIM " + animationName(lead.animationId) +
                    " (" + std::to_string(lead.animationId) + ")" +
                    "  FRM " + std::to_string(lead.substateFrame));

    lines.push_back(std::string("GROUND ") + (lead.grounded ? "YES" : "NO") +
                    "  VZ " + formatNumber(lead.verticalVelocity, 4) +
                    "  FLAGS " + std::to_string(lead.collisionFlags));

    lines.push_back("STICK " + formatNumber(input.stickMagnitude, 1) +
                    "  GAIT " + std::string(lead.running ? "RUN" : "WALK") +
                    "  (RUN ABOVE 100)");

    lines.push_back("CAM  MODE " + std::string(cameraModeName(fieldCamera_.mode())) +
                    "  YAW " + formatNumber(fieldCamera_.yawRadians() * kRadiansToDegrees, 1) +
                    "  PITCH " + formatNumber(fieldCamera_.pitchRadians() * kRadiansToDegrees, 1) +
                    "  DIST " + formatNumber(fieldCamera_.followDistance()));

    if (lead.groundHit.has_value())
    {
      lines.push_back("TRI  PRIM " + std::to_string(lead.groundHit->primitiveIndex) +
                      "  Z " + formatNumber(lead.groundHit->height) +
                      "  TERRAIN " + std::to_string(lead.groundHit->terrainFlags));
    }

    if (sceneScript_.loaded())
    {
      lines.push_back("SCR  OBJECTS " + std::to_string(entityPool_.scriptSpawnedCount()) +
                      "  PLACEMENTS " +
                      std::to_string(mapViewer_.loadedMap() != nullptr
                                         ? mapViewer_.loadedMap()->DAT_003556e8_objectPlacements.size()
                                         : 0) +
                      "  UNIMPL " + std::to_string(scriptTrace_.unimplementedOpcodeCount()));
      lines.push_back(std::string("ACTORS TICK ") + (runScriptTick_ ? "ON" : "OFF") +
                      "  SLOTS " + std::to_string(sceneScript_.occupiedObjectScriptSlots()) +
                      "  LIVE " + std::to_string(actorTrace_.tickedEntityCount()) +
                      "  NOBEHAVIOR " + std::to_string(actorTrace_.unimplementedEntityCount()));
    }

    lines.push_back("WASD MOVE  SPACE JUMP  J/L CAMERA  F WIRE  H HUD  R RESET");

    mapViewer_.setHudLines(std::move(lines));
  }

  orphen::ported::camera::CameraGroundSampler PortRuntime::cameraGroundSampler()
  {
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap == nullptr)
    {
      return {};
    }

    // FUN_00227798, used by FUN_00216aa0 to keep the eye out of the floor.
    return [loadedMap](float x, float y, float z) -> std::optional<float>
    {
      const auto hit = queryPsm2GroundAt(*loadedMap, x, y, z);
      if (!hit.has_value())
      {
        return std::nullopt;
      }
      return hit->height;
    };
  }

} // namespace orphen::port
