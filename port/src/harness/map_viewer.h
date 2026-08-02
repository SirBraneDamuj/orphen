#pragma once

#include "harness/disc_resource_loader.h"
#include "harness/scene_resource_provider.h"
#include "harness/debug_text.h"
#include "runtime/input_state.h"
#include "ported/camera/original_camera_state.h"
#include "ported/psm2/psm2_runtime.h"
#include "runtime/player_view_state.h"
#include "runtime/scene_object_view.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <iosfwd>
#include <string>
#include <vector>

namespace orphen::harness
{

  class MapViewer
  {
  public:
    ~MapViewer();

    void loadDecodedPsm2(const std::filesystem::path &path);
    void loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection);
    bool cycleDiscScene(int direction);
    void setLeadPlayerView(std::optional<orphen::port::PlayerViewState> playerView);
    void setSceneObjectViews(orphen::port::SceneObjectViewList objects);
    void setFollowCameraPose(const orphen::ported::camera::CameraPose &pose);
    orphen::ported::psm2::Vec3 freeViewerMovement(float strafe, float forward) const;
    bool hasLeadPlayerView() const { return leadPlayerView_.has_value(); }
    float freeViewerYawDegrees() const { return cameraYawDegrees_; }
    void printLoadedSceneTree(std::ostream &output) const;
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;
    void setHudLines(std::vector<std::string> lines);
    void toggleHud() { hudVisible_ = !hudVisible_; }

    orphen::ported::psm2::Psm2RuntimeState *loadedMap();
    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;
    const SceneResourceProvider *loadedSceneResources() const;
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
    std::optional<orphen::port::PlayerViewState> leadPlayerView_;
    orphen::port::SceneObjectViewList sceneObjectViews_;
    mutable std::vector<unsigned int> uploadedTextureIds_;
    mutable bool textureUploadDirty_ = false;
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    orphen::ported::camera::CameraPose followCameraPose_;
    DebugTextRenderer debugText_;
    std::vector<std::string> hudLines_;
    bool hudVisible_ = true;
    bool wireframe_ = false;

    void loadDiscSceneAtIndex(std::size_t sceneIndex);
    void setTexturePages(std::vector<LoadedDiscTexturePage> texturePages);
    void releaseUploadedTextures() const;
    void ensureTexturesUploaded() const;
  };

} // namespace orphen::harness
