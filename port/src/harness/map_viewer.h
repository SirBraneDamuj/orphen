#pragma once

#include "harness/disc_resource_loader.h"
#include "platform/sdl_gl_window.h"
#include "ported/psm2/psm2_runtime.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace orphen::harness
{

  class MapViewer
  {
  public:
    void loadDecodedPsm2(const std::filesystem::path &path);
    void loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection);
    bool cycleDiscScene(int direction);
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;
    const std::string &loadedSourceDescription() const { return loadedSourceDescription_; }

  private:
    std::optional<orphen::ported::psm2::Psm2RuntimeState> map_;
    std::string loadedSourceDescription_ = "none";
    std::filesystem::path discRoot_;
    std::vector<McbSceneSelection> discScenes_;
    std::size_t currentDiscSceneIndex_ = 0;
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    bool wireframe_ = false;

    void loadDiscSceneAtIndex(std::size_t sceneIndex);
  };

} // namespace orphen::harness
