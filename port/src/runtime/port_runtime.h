#pragma once

#include "harness/disc_resource_loader.h"
#include "runtime/input_state.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
#include "ported/camera/original_field_camera.h"
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
    std::uint32_t headlessFrameCount = 0;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride;
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
    OriginalLeadPlayer leadPlayer_;
    orphen::ported::camera::OriginalFieldCamera fieldCamera_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride_;
    float previousStickMagnitude_ = 0.0f;

    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
    orphen::ported::camera::CameraGroundSampler cameraGroundSampler();
    void updateHud(const InputSnapshot &input, std::uint32_t frameTicks);
  };

} // namespace orphen::port
