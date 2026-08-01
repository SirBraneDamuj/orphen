#pragma once

#include "harness/disc_resource_loader.h"
#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
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
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;

    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
  };

} // namespace orphen::port
