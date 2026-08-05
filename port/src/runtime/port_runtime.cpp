#include "runtime/port_runtime.h"

#include "harness/flat_bin_archive.h"

#include "runtime/psm2_ground_query.h"
#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/model/entity_animation.h"
#include "ported/script/object_registers.h"
#include "ported/player/original_interaction.h"

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
      drawDistanceOverridden_ = true;
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

    discRoot_ = config.discRoot;
    loadMapPropDescriptors();

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
    environment.boneOverrides = DAT_004a7e00_boneOverrides_;
    // FUN_00266368 reads the flag bank that lives in the script state, so the
    // actor tick borrows it rather than owning a second copy.
    environment.eventFlag = [this](std::uint32_t flagId)
    { return sceneScript_.state().FUN_00266368_eventFlag(flagId); };
    environment.descriptors = &descriptorTable_;

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
          [loadedMap](float x, float y) -> std::optional<orphen::ported::entity::ActorEnvironment::TerrainSurface>
      {
        const auto hit = queryPsm2GroundAt(*loadedMap, x, y, 0.0f);
        if (!hit.has_value())
        {
          return std::nullopt;
        }
        return orphen::ported::entity::ActorEnvironment::TerrainSurface{hit->height, hit->terrainFlags};
      };
    }
    return environment;
  }

  orphen::ported::script::ScriptEnvironment PortRuntime::scriptEnvironment(std::uint32_t frameTicks)
  {
    orphen::ported::script::ScriptEnvironment environment;
    environment.frameTicks = frameTicks;
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

    environment.FUN_002661a8_preload_model = [this](std::uint16_t typeId)
    {
      modelStore_.bindingForTypeId(typeId);
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
  void PortRuntime::applySceneEnvironment()
  {
    const auto &state = sceneScript_.state();

    // 0xB8 writes DAT_0032538c and fGpffffb6b8 = DAT_00355628 together, and
    // DAT_00355628 is what the visibility pass culls against -- so a script
    // that sets it mid-load moves the draw distance, not a camera parameter.
    // --draw-distance still wins over both the scene block and the script.
    if (!drawDistanceOverridden_ && state.DAT_0032538c_cameraDistance > 0.0f)
    {
      mapViewer_.setDrawDistance(state.DAT_0032538c_cameraDistance);
    }

    mapViewer_.setFogColour(state.uGpffffb704_color1);
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
    orphen::ported::render::SceneLighting::unpack(state.uGpffffb6fc_globalRgb,
                                                  lighting.ambient);
    orphen::ported::render::SceneLighting::unpack(state.uGpffffb700_vectorRgb,
                                                  lighting.lightColour[0]);
    // The EE negates the vector on upload, so the microprogram's dot product is
    // against -D and this holds the already-negated form.
    lighting.lightDirection[0] = orphen::ported::psm2::Vec3{
        -state.DAT_003439c8_vector[0],
        -state.DAT_003439c8_vector[1],
        -state.DAT_003439c8_vector[2]};
    lighting.active = true;
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

  void PortRuntime::loadSceneForCurrentMap()
  {
    // Models bind before the script runs, so the spawn path can report a
    // missing model at the moment it spawns the entity that wanted it.
    modelStore_.initialize(mapViewer_.loadedSceneResources(), discRoot_, &descriptorTable_);

    // FUN_0022a418:50 sets DAT_00355208 from DAT_003551f4, the stage number of
    // the scene being entered -- s01_e024 is stage 1. That is the bank
    // FUN_00229980 uses for the 0x272 type range.
    const auto scene = mapViewer_.loadedDiscScene();
    modelStore_.setMapPropTable(&mapPropTable_, scene.has_value() ? static_cast<int>(scene->section) : -1);

    modelStore_.FUN_00221fd8_bind_boot_textures();
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
    modelStore_.bindingForTypeId(leader);

    resetLeadPlayerForLoadedMap();
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
    const EntityModelBinding *binding = modelStore_.bindingForTypeId(entity.typeId00);
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

    const orphen::ported::model::Matrix4 root =
        orphen::ported::model::FUN_0020cdc0_entity_root(
            {view.position.x, view.position.y, view.position.z}, view.facingRadians,
            view.rotationX154, view.rotationY158, view.scale, view.scaleZ150);

    view.bonePalette = orphen::ported::model::FUN_0020d618_build_palette(
        *binding->model, binding->model->blob, entity.poseColumnAc, root,
        DAT_003ffe00_poseFilters_[view.slot], inputs,
        &DAT_004a7e00_boneOverrides_[view.slot]);
  }

  // FUN_00225c90 for every entity that has a model, run before the views are
  // published so the pose column the renderer reads is this frame's.
  void PortRuntime::advanceEntityAnimations(std::uint32_t frameTicks)
  {
    const auto step = [&](orphen::ported::entity::OriginalEntity &entity) {
      const EntityModelBinding *binding = modelStore_.bindingForTypeId(entity.typeId00);
      if (binding == nullptr || binding->model == nullptr)
      {
        return;
      }
      orphen::ported::model::FUN_00225c90_advance_animation(entity, *binding->model, frameTicks);
    };

    step(entityPool_.leadPlayer());
    entityPool_.forEachScriptSpawnedMutable(
        [&](std::size_t slot, orphen::ported::entity::OriginalEntity &entity) {
          if (entityPool_.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
          {
            return;
          }
          step(entity);
        });
  }

  void PortRuntime::publishSceneObjectViews(std::uint32_t frameTicks)
  {
    SceneObjectViewList views;

    // Slot 0 is not script-spawned, so forEachScriptSpawned never reaches it and
    // the lead player had no model for the same reason it has no entry in the
    // actor report. FUN_0020c5a8 walks all 256 slots; this list is what stands
    // in for that walk, so the lead player belongs in it.
    {
      auto &lead = entityPool_.leadPlayer();
      SceneObjectView view;
      view.slot = 0;
      view.typeId = lead.typeId00;
      view.modelIndex = lead.modelIndex;
      view.position = {lead.positionX20, lead.positionZ24, lead.positionY28};
      view.facingRadians = lead.facingRadians5c;
      view.radius = lead.radius54;
      view.height = lead.height58;
      view.groundHeight = lead.groundHeight4c;
      view.descriptorResolved = lead.modelIndex >= 0;
      view.scale = lead.scale14c;
      view.scaleZ150 = lead.scaleZ150;
      view.rotationX154 = lead.rotationX154;
      view.rotationY158 = lead.rotationY158;
      view.drawDebugBox = false;
      attachModel(view, lead, frameTicks);
      views.push_back(view);
    }

    entityPool_.forEachScriptSpawnedMutable(
        [&](std::size_t slot, orphen::ported::entity::OriginalEntity &entity)
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
          view.scale = entity.scale14c;
          view.scaleZ150 = entity.scaleZ150;
          view.rotationX154 = entity.rotationX154;
          view.rotationY158 = entity.rotationY158;
          attachModel(view, entity, frameTicks);
          views.push_back(view);
        });
    mapViewer_.setSceneObjectViews(std::move(views));
    mapViewer_.setTextureSlotCache(&modelStore_.textureSlots());
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
                  << " hits=" << event.hits << "  (0x86 steps it; no GS submit)\n";
      }
      for (const auto &entry : scriptTrace_.playerLocks())
      {
        std::cout << "  0x6D player lock mode=" << entry.first << " hits=" << entry.second
                  << (entry.first < 1 ? "  (state 10 written; the lead's state-10 handler is not ported, so it does not hold)" : "  (release)")
                  << '\n';
      }
      if (scriptTrace_.battleBootCount() != 0)
      {
        std::cout << "  0xE1 save/menu mode hits=" << scriptTrace_.battleBootCount()
                  << "  (flag 0x8EE cleared, mode 0x10 raised; no menu to hand off to)\n";
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

    const auto describe = [&](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity) {
      const EntityModelBinding *binding = store.bindingForTypeId(entity.typeId00);
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
                  << std::fixed << std::setprecision(2)
                  << "  posed=" << (posed.max.x - posed.min.x) << "x"
                  << (posed.max.y - posed.min.y) << "x" << (posed.max.z - posed.min.z)
                  << "  at=(" << (posed.min.x + posed.max.x) * 0.5f << ","
                  << (posed.min.y + posed.max.y) * 0.5f << "," << posed.min.z << ".."
                  << posed.max.z << ")"
                  << "  entity=(" << entity.positionX20 << "," << entity.positionZ24 << ","
                  << entity.positionY28 << ")"
                  << "  descriptor=" << entity.radius54 << "r/" << entity.height58 << "h"
                  << std::defaultfloat;
      }
      std::cout << '\n';
    };

    describe(0, entityPool_.leadPlayer());
    entityPool_.forEachScriptSpawned(
        [&](std::size_t slot, const orphen::ported::entity::OriginalEntity &entity) {
          if (entityPool_.status(slot) != orphen::ported::entity::SlotStatus::ScriptSpawned)
          {
            return;
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
    std::cout << "[render] draw distance " << mapViewer_.drawDistance()
              << " fog band " << mapViewer_.fogNear() << ".." << mapViewer_.fogFar()
              << " fog colour 0x" << std::hex << mapViewer_.fogColour() << std::dec << '\n'
              << "[render] light1 0x" << std::hex << environment.uGpffffb6fc_globalRgb
              << " light2 0x" << environment.uGpffffb700_vectorRgb << std::dec
              << " dir1 " << environment.DAT_003439c8_vector[0]
              << ',' << environment.DAT_003439c8_vector[1]
              << ',' << environment.DAT_003439c8_vector[2]
              << "  (ambient 0x2a7 / light0 0x2a3 / dir 0x2a0..2, VU1 0x1b2)\n"
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
                    << " state=" << entity.state60
                    << " anim=" << entity.animationA0
                    << " pos=(" << std::fixed << std::setprecision(2)
                    << entity.positionX20 << "," << entity.positionZ24 << "," << entity.positionY28 << ")"
                    << " facing=" << std::setprecision(3) << entity.facingRadians5c
                    << std::defaultfloat
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
      // PLACEHOLDER OUTCOME, and deliberately so.
      //
      // The original enters player state 0xC (FUN_00254d58), which starts a
      // three-state camera sequence: 0xC arms the fade and hands to 0xD
      // (FUN_00254db0), which snaps the player to a fixed offset in front of
      // the chest, installs a look-at camera through FUN_00217d70, and hands to
      // 0xE. None of that is ported.
      //
      // What is ported is the thing the sequence exists to do: set the chest's
      // event flag. FUN_002d1ea8 only ever *observes* that flag, so setting it
      // here drives the real, already-ported behavior -- the chest animates
      // 4 (closed) -> 5 (opening) -> 6 (open) on its own from the next frame.
      sceneScript_.state().FUN_002663a0_setEventFlag(result.chestFlagId);
      std::cout << "[interact] chest slot=" << result.targetSlot
                << " flag=0x" << std::hex << result.chestFlagId << std::dec
                << " opened (cutscene states 0xC-0xE not ported)\n";
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
      std::cout << "[interact] scripted entity slot=" << result.targetSlot
                << " type=0x" << std::hex << result.targetType << std::dec
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
      std::cout << "[panel] scene transition: fullscreen fade armed, rate=" << fade.rate
                << " rgb=0x" << std::hex << fade.packedRgb << std::dec
                << " (no GS submit, so nothing is drawn)\n";
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
    // The viewer's free camera and the follow-yaw easing still think in seconds.
    // One simulation step is one nominal frame, so hand them that directly
    // rather than wall-clock time -- this is what makes --frames deterministic.
    const float stepSeconds = orphen::ported::kNominalFrameSeconds *
                              (static_cast<float>(frameTicks) / static_cast<float>(orphen::ported::kNominalFrameTicks));
    mapViewer_.update(stepSeconds, input);

    if (mapViewer_.loadedMapGeneration() != trackedMapGeneration_)
    {
      // A cycled map is a scene load, not just a new mesh: its script owns the
      // fog, the draw distance and the entity set, and its bundle owns the
      // models. resetLeadPlayerForLoadedMap alone left all of that on the
      // previous scene's values.
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
      if (runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b778_run_tick(scriptEnvironment(frameTicks), scriptTrace_);
        reportTickHalt("tick");
      }

      // Raw pad 0x0020 is Circle. Held, it gates the debug mid-air jump.
      constexpr std::uint16_t kRawPadCircle = 0x0020;
      // Raw pad 0x0040 is Cross, the confirm button. The original tests the
      // same bit in the *mapped* pressed word (uGpffffb68a = DAT_003555fa);
      // Cross maps through to the same position, and the port has no mapping
      // table, so the raw bit stands in.
      constexpr std::uint16_t kRawPadCross = 0x0040;
      leadPlayer_.update(frameTicks,
                         movementRequest,
                         input.stickMagnitude,
                         input.jumpRequested,
                         (input.rawHeldPad & kRawPadCircle) != 0,
                         (input.rawPressedPad & kRawPadCross) != 0,
                         loadedMap,
                         [this] { return runInteractionProbe(); });

      orphen::ported::entity::FUN_00239ce0_update_actors(actorEnvironment(frameTicks), actorTrace_);

      if (runScriptTick_ && sceneScript_.loaded())
      {
        sceneScript_.FUN_0025b918_run_late_slots(scriptEnvironment(frameTicks), scriptTrace_);
        reportTickHalt("late slots");
        reportPanelActivity();
      }

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
    for (auto &filter : DAT_003ffe00_poseFilters_)
    {
      filter.reset();
    }
    for (auto &overrides : DAT_004a7e00_boneOverrides_)
    {
      overrides.reset();
    }
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
