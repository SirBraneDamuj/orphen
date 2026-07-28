#pragma once

#include "platform/sdl_gl_window.h"
#include "ported/psm2/psm2_runtime.h"

#include <filesystem>
#include <optional>

namespace orphen::harness
{

  class MapViewer
  {
  public:
    void loadDecodedPsm2(const std::filesystem::path &path);
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;

  private:
    std::optional<orphen::ported::psm2::Psm2RuntimeState> map_;
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    bool wireframe_ = false;
  };

} // namespace orphen::harness