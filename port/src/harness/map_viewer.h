#pragma once

#include "harness/disc_resource_loader.h"
#include "harness/scene_resource_provider.h"
#include "platform/sdl_gl_window.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/psc3/psc3_runtime.h"
#include "runtime/camera_view.h"
#include "runtime/player_debug_probe.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <iosfwd>
#include <string>
#include <vector>

namespace orphen::harness
{

  struct LoadedSceneModel
  {
    orphen::ported::psc3::Psc3RuntimeModel model;
    orphen::ported::psm2::Vec3 galleryOffset{};
    std::uint16_t category = 0;
    std::uint16_t resourceId = 0;
  };

  class MapViewer
  {
  public:
    ~MapViewer();

    void loadDecodedPsm2(const std::filesystem::path &path);
    void loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection);
    bool cycleDiscScene(int direction);
    void setDebugPlayerProbe(std::optional<orphen::port::PlayerDebugProbeState> probe);
    void setRuntimeCameraView(std::optional<orphen::port::RuntimeCameraView> view);
    void printLoadedSceneTree(std::ostream &output) const;
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;

    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;
    const std::string &loadedSourceDescription() const { return loadedSourceDescription_; }
    std::size_t loadedTexturePageCount() const { return texturePages_.size(); }
    std::uint64_t loadedMapGeneration() const { return loadedMapGeneration_; }

  private:
    std::optional<orphen::ported::psm2::Psm2RuntimeState> map_;
    std::string loadedSourceDescription_ = "none";
    std::uint64_t loadedMapGeneration_ = 0;
    std::filesystem::path discRoot_;
    std::vector<McbSceneSelection> discScenes_;
    std::size_t currentDiscSceneIndex_ = 0;
    std::optional<SceneResourceProvider> sceneResources_;
    std::vector<LoadedDiscTexturePage> texturePages_;
    std::vector<LoadedSceneModel> sceneModels_;
    std::optional<orphen::port::PlayerDebugProbeState> debugPlayerProbe_;
    std::optional<orphen::port::RuntimeCameraView> runtimeCameraView_;
    mutable std::vector<unsigned int> uploadedTextureIds_;
    mutable bool textureUploadDirty_ = false;
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    bool wireframe_ = false;

    void loadDiscSceneAtIndex(std::size_t sceneIndex);
    void loadSceneModels();
    void layoutSceneModelGallery();
    void setTexturePages(std::vector<LoadedDiscTexturePage> texturePages);
    void releaseUploadedTextures() const;
    void ensureTexturesUploaded() const;
  };

} // namespace orphen::harness
