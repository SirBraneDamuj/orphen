#pragma once

#include "harness/disc_resource_loader.h"
#include "platform/sdl_gl_window.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
#include "runtime/probe_follow_camera.h"
#include "runtime/ps2_memory.h"
#include "runtime/scene_script_state.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_set>

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
    std::uint32_t scriptFrameCount = 0;
  };

  class PortRuntime
  {
  public:
    void initialize(const PortRuntimeConfig &config);
    void reset();
    bool update(float deltaSeconds, const InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

  private:
    Ps2Memory memory_;
    orphen::harness::MapViewer mapViewer_;
    OriginalLeadPlayer leadPlayer_;
    ProbeFollowCamera probeCamera_;
    SceneScriptState sceneScript_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::uint64_t trackedScriptGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<bool> reportedScriptCameraOverride_;
    std::optional<float> reportedScriptCameraDistance_;
    std::optional<std::uint64_t> reportedScriptFrameStop_;
    std::unordered_set<std::uint64_t> reportedScriptRegisterTraces_;
    std::optional<std::uint64_t> reportedScriptCoroutineState_;

    void syncSceneScriptForLoadedMap();
    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
  };

} // namespace orphen::port
