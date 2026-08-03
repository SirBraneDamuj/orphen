#pragma once

#include "harness/disc_resource_loader.h"
#include "runtime/input_state.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
#include "ported/camera/original_field_camera.h"
#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/resource/elf_data_reader.h"
#include "ported/script/scene_script.h"
#include "runtime/ps2_memory.h"
#include "ported/original_frame_timing.h"
#include "ported/render/original_map_visibility.h"
#include "ported/render/original_view_projection.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>

namespace orphen::port
{

  struct PortRuntimeConfig
  {
    std::filesystem::path decodedPsm2Path;
    std::filesystem::path discRoot;
    orphen::harness::McbSceneSelection discScene;
    bool hasDiscScene = false;
    bool exitAfterUsage = false;
    bool loadOnly = false;
    bool printSceneTree = false;
    bool printScriptReport = false;
    bool printActorReport = false;
    bool printRenderReport = false;
    // --model-report: parse every grp record in the bundle and print its counts.
    bool printModelReport = false;
    // DAT_00355628 override, for experimenting before the script opcode that
    // normally sets it (FUN_00263cb8) is wired up.
    std::optional<float> drawDistanceOverride;
    // --probe: dump the primitives around a world point and stop.
    std::optional<orphen::ported::psm2::Vec3> probeCentre;
    float probeRadius = 2.0f;
    // Drive the scene script's per-frame entry and its object-script slots.
    // **On by default.** It used to be off to protect the determinism baseline,
    // but the per-frame entry is what evaluates the terrain triggers, so with it
    // off the floor panels are inert and the scene looks broken in a way that
    // has nothing to do with the panels. --no-scr-tick restores the old
    // behaviour; runs are deterministic either way.
    bool runScriptTick = true;
    std::uint32_t headlessFrameCount = 0;
    // 1-based frame on which --frames should press Cross, or 0 for never.
    std::uint32_t pressConfirmFrame = 0;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride;
    // Retail executable, read for static tables such as the entity descriptors.
    // Optional: when empty, SLUS_200.11 is looked for in the disc root, and when
    // that is missing too the port runs without descriptor data.
    std::filesystem::path executablePath;
  };

  class PortRuntime
  {
  public:
    void initialize(const PortRuntimeConfig &config);
    void reset();
    // One fixed 60 Hz simulation step. frameTicks is DAT_003555bc; the harness
    // always passes the nominal 0x20 because main() drives a fixed accumulator.
    bool update(const InputSnapshot &input, std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks);
    void render(int framebufferWidth, int framebufferHeight) const;

    // Reports that are only meaningful after frames have run. Called once at
    // shutdown; a no-op unless the matching flag was passed.
    void printExitReports() const;

  private:
    Ps2Memory memory_;
    orphen::harness::MapViewer mapViewer_;
    orphen::ported::entity::EntityPool entityPool_;
    orphen::ported::script::SceneScript sceneScript_;
    orphen::ported::script::ScriptTrace scriptTrace_;
    OriginalLeadPlayer leadPlayer_;
    orphen::ported::camera::OriginalFieldCamera fieldCamera_;
    orphen::ported::render::ViewProjection renderCamera_;
    orphen::ported::render::MapVisibilityReport visibilityReport_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride_;
    const char *spawnSourceLabel_ = "map centre";
    float previousStickMagnitude_ = 0.0f;
    std::optional<orphen::ported::resource::ElfDataReader> executable_;
    orphen::ported::entity::EntityDescriptorTable descriptorTable_;
    orphen::ported::entity::ActorDispatchTable actorDispatchTable_;
    orphen::ported::entity::ActorTrace actorTrace_;
    bool runScriptTick_ = false;
    bool drawDistanceOverridden_ = false;
    // FUN_00216868 stand-in. Seeded to a constant so --frames is reproducible.
    std::uint32_t actorRandomState_ = 0x12345678u;
    // Rising-edge state for the live trigger log, so stepping on a panel says
    // so once rather than 60 times a second.
    std::map<std::uint32_t, bool> triggerWasPassing_;
    std::uint32_t reportedFadeArms_ = 0;
    std::uint32_t reportedPlayerLocks_ = 0;
    std::uint32_t reportedBattleBoots_ = 0;
    void reportPanelActivity();
    bool printActorReport_ = false;
    bool printScriptReport_ = false;
    bool printRenderReport_ = false;
    // Set once, the first time a per-frame script run stops on an unimplemented
    // opcode, so a halting tick says so instead of failing silently every frame.
    mutable bool reportedTickHalt_ = false;

    void loadExecutable(const PortRuntimeConfig &config);
    void runSceneScript();
    void publishSceneObjectViews();
    void applySceneMarkerSpawn();
    void printScriptReport() const;
    void printActorReport() const;
    void printRenderReport() const;
    void printModelReport() const;
    void printPrimitiveProbe(const orphen::ported::psm2::Vec3 &centre, float radius) const;
    void updateMapVisibility(orphen::ported::psm2::Psm2RuntimeState &map, const PlayerViewState &leadState);
    void reportTickHalt(const char *what) const;
    bool runInteractionProbe();
    orphen::ported::script::ScriptEnvironment scriptEnvironment(
        std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks);
    orphen::ported::entity::ActorEnvironment actorEnvironment(std::uint32_t frameTicks);
    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
    orphen::ported::camera::CameraGroundSampler cameraGroundSampler();
    void updateHud(const InputSnapshot &input, std::uint32_t frameTicks);
  };

} // namespace orphen::port
