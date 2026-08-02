#include "runtime/port_runtime.h"

#include "runtime/psm2_ground_query.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>
#include <iostream>

namespace orphen::port
{
  namespace
  {

    constexpr std::uint32_t kHarnessFrameCounterAddress = 0x00001000;
    constexpr float kRadiansToDegrees = 57.2957795f;

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
    // Bind before reset: the lead player is pool slot 0, so the controller must
    // already be writing there when resetToMap places it.
    leadPlayer_.bindEntity(entityPool_.leadPlayer());

    reset();
    spawnOverride_ = config.spawnOverride;
    if (spawnOverride_.has_value())
    {
      spawnSourceLabel_ = "--spawn";
    }
    runScriptTick_ = config.runScriptTick;
    printActorReport_ = config.printActorReport;
    printRenderReport_ = config.printRenderReport;
    if (config.drawDistanceOverride.has_value())
    {
      mapViewer_.setDrawDistance(*config.drawDistanceOverride);
    }
    printScriptReport_ = config.printScriptReport;
    loadExecutable(config);

    if (!config.decodedPsm2Path.empty())
    {
      mapViewer_.loadDecodedPsm2(config.decodedPsm2Path);
    }
    else if (!config.discRoot.empty())
    {
      mapViewer_.loadDiscSceneMap(config.discRoot, config.discScene);
    }

    if (mapViewer_.loadedMap() != nullptr)
    {
      resetLeadPlayerForLoadedMap();
      runSceneScript();
      if (config.printScriptReport)
      {
        printScriptReport();
      }
      if (config.printSceneTree)
      {
        mapViewer_.printLoadedSceneTree(std::cout);
      }
      const auto &stats = mapViewer_.loadedMap()->stats;
      std::cout << "[psm2] loaded " << mapViewer_.loadedSourceDescription()
                << " positions=" << stats.positionRecordCount
                << " sectionB=" << stats.sectionBRecordCount
                << " primitives=" << stats.primitiveRecordCount
                << " triangles=" << stats.triangleCount
                << " skipped=" << stats.skippedPrimitiveCount
                << " textures=" << mapViewer_.loadedTexturePageCount() << '\n';

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

  orphen::ported::entity::ActorEnvironment PortRuntime::actorEnvironment(std::uint32_t frameTicks)
  {
    orphen::ported::entity::ActorEnvironment environment;
    environment.entityPool = &entityPool_;
    environment.dispatchTable = &actorDispatchTable_;
    environment.frameTicks = frameTicks;
    // FUN_00266368 reads the flag bank that lives in the script state, so the
    // actor tick borrows it rather than owning a second copy.
    environment.eventFlag = [this](std::uint32_t flagId)
    { return sceneScript_.state().FUN_00266368_eventFlag(flagId); };
    return environment;
  }

  orphen::ported::script::ScriptEnvironment PortRuntime::scriptEnvironment()
  {
    orphen::ported::script::ScriptEnvironment environment;
    environment.entityPool = &entityPool_;
    environment.descriptors = &descriptorTable_;
    environment.state = &sceneScript_.state();
    environment.map = mapViewer_.loadedMap();

    // FUN_00227070 stands in as the existing PSM2 ground query, the same one the
    // camera uses. The lax overload is right here: script placements are
    // authored, not walked to, so a strict walkability test would reject valid
    // spots.
    const auto *loadedMap = mapViewer_.loadedMap();
    if (loadedMap != nullptr)
    {
      environment.terrainHeight = [loadedMap](float x, float y) -> std::optional<float>
      {
        const auto hit = queryPsm2GroundAt(*loadedMap, x, y, 0.0f);
        if (!hit.has_value())
        {
          return std::nullopt;
        }
        return hit->height;
      };
    }

    environment.teleportLead = [this](float x, float y, float z)
    {
      const auto *map = mapViewer_.loadedMap();
      if (map == nullptr)
      {
        return;
      }
      leadPlayer_.resetToMap(*map, orphen::ported::psm2::Vec3{x, y, z});
      fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
    };

    return environment;
  }

  void PortRuntime::runSceneScript()
  {
    scriptTrace_.reset();
    sceneScript_ = {};

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
    if (!sceneScript_.load(decoded))
    {
      std::cout << "[scr] script blob too small to hold a header (" << decoded.size() << " bytes)\n";
      return;
    }

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
    applySceneMarkerSpawn();
    publishSceneObjectViews();

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
  // Standing them in for the spawn point is an inference, not something read out
  // of the original, and the console says which was used. --spawn still wins.
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

    for (const auto &record : loadedMap->DAT_003556e8_objectPlacements)
    {
      if (record.group != 2)
      {
        continue;
      }
      leadPlayer_.resetToMap(*loadedMap, record.position);
      fieldCamera_.FUN_00216930_install_normal_field_defaults();
      fieldCamera_.snapToTarget(leadPlayer_.viewState().position);
      mapViewer_.setLeadPlayerView(leadPlayer_.viewState());
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
      spawnSourceLabel_ = "scene marker";
      return;
    }
  }

  void PortRuntime::publishSceneObjectViews()
  {
    SceneObjectViewList views;
    entityPool_.forEachScriptSpawned(
        [&views](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity)
        {
          SceneObjectView view;
          view.slot = slot;
          view.typeId = entity.typeId00;
          view.modelIndex = entity.modelIndex;
          view.position = {entity.positionX20, entity.positionZ24, entity.positionY28};
          view.facingRadians = entity.facingRadians5c;
          view.radius = entity.radius54;
          view.height = entity.height58;
          view.groundHeight = entity.groundHeight4c;
          view.descriptorResolved = entity.modelIndex >= 0;
          views.push_back(view);
        });
    mapViewer_.setSceneObjectViews(std::move(views));
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
                << (entry.second.implemented ? "  implemented" : "  UNIMPLEMENTED") << '\n';
    }
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
      std::cout << "stream overran the blob\n";
    }
    std::cout << "[scr] further per-frame halts are not reported\n";
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
    if (printActorReport_)
    {
      printActorReport();
    }
    if (printRenderReport_)
    {
      printRenderReport();
    }
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

    std::cout << "[render] draw distance " << mapViewer_.drawDistance()
              << " fog " << (mapViewer_.drawDistance() * 0.25f < 5.0f ? "on" : "off") << '\n'
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

    std::size_t live = 0;
    entityPool_.forEachScriptSpawned(
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
                    << " type=0x" << std::hex << entity.typeId00 << std::dec
                    << " " << actorHandlerSourceName(handler.source);
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
      std::cout << "skipped: hidden=" << actorTrace_.hiddenCount()
                << " suspended=" << actorTrace_.suspendedCount()
                << " fading=" << actorTrace_.fadingCount() << '\n';
      std::cout << "unimplemented behaviors: " << actorTrace_.unimplementedTypeCount()
                << " distinct types, " << actorTrace_.unimplementedEntityCount() << " entities\n";
    }
    std::cout << "=== end actor report ===\n\n";
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
    // The viewer's free camera and the follow-yaw easing still think in seconds.
    // One simulation step is one nominal frame, so hand them that directly
    // rather than wall-clock time -- this is what makes --frames deterministic.
    const float stepSeconds = orphen::ported::kNominalFrameSeconds *
                              (static_cast<float>(frameTicks) / static_cast<float>(orphen::ported::kNominalFrameTicks));
    mapViewer_.update(stepSeconds, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      resetLeadPlayerForLoadedMap();
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
      if (runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b778_run_tick(scriptEnvironment(), scriptTrace_);
        reportTickHalt("tick");
      }

      // Raw pad 0x0020 is Circle. Held, it gates the debug mid-air jump.
      constexpr std::uint16_t kRawPadCircle = 0x0020;
      leadPlayer_.update(frameTicks,
                         movementRequest,
                         input.stickMagnitude,
                         input.jumpRequested,
                         (input.rawHeldPad & kRawPadCircle) != 0,
                         loadedMap);

      orphen::ported::entity::FUN_00239ce0_update_actors(actorEnvironment(frameTicks), actorTrace_);

      if (runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b918_run_late_slots(scriptEnvironment(), scriptTrace_);
        reportTickHalt("late slots");
      }

      // Behaviors can move and turn entities, so the render views are rebuilt
      // every frame now rather than only at load.
      publishSceneObjectViews();

      const auto &leadState = leadPlayer_.viewState();
      orphen::ported::camera::FieldCameraInput cameraInput;
      cameraInput.rawHeldPad = input.rawHeldPad;
      cameraInput.rawPressedPad = input.rawPressedPad;
      cameraInput.stickAngle = input.stickAngle;
      cameraInput.stickMagnitude = input.stickMagnitude;
      cameraInput.previousStickMagnitude = previousStickMagnitude_;
      cameraInput.autoFocusGoalYaw = leadState.facingRadians;
      previousStickMagnitude_ = input.stickMagnitude;

      fieldCamera_.FUN_00216aa0_update(frameTicks, cameraInput, leadState.position, cameraGroundSampler());

      mapViewer_.setLeadPlayerView(leadState);
      mapViewer_.setFollowCameraPose(fieldCamera_.pose());
      updateMapVisibility(*loadedMap, leadState);
    }
    else
    {
      mapViewer_.setLeadPlayerView(std::nullopt);
      mapViewer_.setMapDrawList({});
    }
    reportLeadPlayerGroundChange();
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
                                        const PlayerViewState &leadState)
  {
    orphen::ported::render::FieldCameraView cameraView;
    cameraView.eye = fieldCamera_.pose().eye;
    cameraView.yawRadians = fieldCamera_.yawRadians();
    cameraView.pitchRadians = fieldCamera_.pitchRadians();
    renderCamera_ = orphen::ported::render::FUN_0020bec8_build(cameraView);

    orphen::ported::render::MapVisibilityInput visibilityInput;
    visibilityInput.DAT_0058bed0_playerPosition = leadState.position;
    visibilityInput.DAT_0058bf08_playerHeadOffset = leadState.bodyHeight;
    visibilityInput.drawDistance = mapViewer_.drawDistance();

    // The original culls at a fixed 90 degrees, wider than the 67.4 it draws.
    // A window wider than about 17:9 needs more than that, so widen rather
    // than pop geometry out at the edges. Headless runs have no framebuffer
    // yet and stay on the original's value, which keeps --frames deterministic.
    const int width = mapViewer_.lastFramebufferWidth();
    const int height = mapViewer_.lastFramebufferHeight();
    if (width > 0 && height > 0)
    {
      const auto camera = orphen::ported::render::glCameraFor(
          renderCamera_, width, height, orphen::ported::render::constants::kNearPlane, visibilityInput.drawDistance);
      visibilityInput.horizontalCullHalfTangent = std::max(1.0f, camera.horizontalHalfTangent);
      visibilityInput.verticalCullHalfTangent = std::max(1.0f, camera.verticalHalfTangent);
    }

    mapViewer_.setRenderCamera(renderCamera_);
    mapViewer_.setMapDrawList(orphen::ported::render::FUN_00209140_buildDrawList(
        map, renderCamera_, visibilityInput, &visibilityReport_));
  }

  void PortRuntime::render(int framebufferWidth, int framebufferHeight) const
  {
    mapViewer_.render(framebufferWidth, framebufferHeight);
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
    mapViewer_.setSceneObjectViews({});
    leadPlayer_.bindEntity(entityPool_.leadPlayer());
    leadPlayer_.resetToMap(*loadedMap, spawnOverride_);
    previousStickMagnitude_ = 0.0f;
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
