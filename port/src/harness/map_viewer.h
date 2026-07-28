#pragma once

#include "harness/disc_resource_loader.h"
#include "platform/sdl_gl_window.h"
#include "ported/psm2/psm2_runtime.h"

#include <filesystem>
#include <optional>
#include <string>

namespace orphen::harness
{

  class MapViewer
  {
  public:
    void loadDecodedPsm2(const std::filesystem::path &path);
    void loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection);
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;
    const std::string &loadedSourceDescription() const { return loadedSourceDescription_; }

  private:
    std::optional<orphen::ported::psm2::Psm2RuntimeState> map_;
    std::string loadedSourceDescription_ = "none";
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    bool wireframe_ = false;
  };

} // namespace orphen::harness