#pragma once

#include "harness/disc_resource_loader.h"
#include "runtime/input_state.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
#include "ported/camera/original_field_camera.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/resource/elf_data_reader.h"
#include "ported/script/scene_script.h"
#include "runtime/ps2_memory.h"
#include "ported/original_frame_timing.h"

#include <cstdint>
#include <filesystem>
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
    std::uint32_t headlessFrameCount = 0;
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

  private:
    Ps2Memory memory_;
    orphen::harness::MapViewer mapViewer_;
    orphen::ported::entity::EntityPool entityPool_;
    orphen::ported::script::SceneScript sceneScript_;
    orphen::ported::script::ScriptTrace scriptTrace_;
    OriginalLeadPlayer leadPlayer_;
    orphen::ported::camera::OriginalFieldCamera fieldCamera_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride_;
    float previousStickMagnitude_ = 0.0f;
    std::optional<orphen::ported::resource::ElfDataReader> executable_;
    orphen::ported::entity::EntityDescriptorTable descriptorTable_;

    void loadExecutable(const PortRuntimeConfig &config);
    void runSceneScript();
    void printScriptReport() const;
    orphen::ported::script::ScriptEnvironment scriptEnvironment();
    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
    orphen::ported::camera::CameraGroundSampler cameraGroundSampler();
    void updateHud(const InputSnapshot &input, std::uint32_t frameTicks);
  };

} // namespace orphen::port
