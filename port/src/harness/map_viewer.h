#pragma once

#include "harness/disc_resource_loader.h"
#include "ported/resource/texture_slot_cache.h"
#include "harness/scene_resource_provider.h"
#include "harness/debug_text.h"
#include "runtime/input_state.h"
#include "ported/camera/original_camera_state.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_map_visibility.h"
#include "ported/render/original_view_projection.h"
#include "runtime/player_view_state.h"
#include "runtime/scene_object_view.h"

#include <array>
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
    // The entity texture slots, owned by the model store. Uploaded lazily on
    // the same schedule as the map's pages so headless runs never touch GL.
    void setTextureSlotCache(const orphen::ported::resource::TextureSlotCache *slots);
    void setFollowCameraPose(const orphen::ported::camera::CameraPose &pose);

    // The ported render pipeline's output for this frame. PortRuntime owns
    // both because the visibility pass mutates per-primitive fade state and so
    // has to run on the fixed simulation step, not on the render rate.
    void setRenderCamera(const orphen::ported::render::ViewProjection &viewProjection);
    void setMapDrawList(std::vector<orphen::ported::render::MapDrawItem> drawList);
    // DAT_00355628. Bounds the GL far plane and the fog band.
    void setDrawDistance(float drawDistance);
    float drawDistance() const { return drawDistance_; }

    // The scene environment block. DAT_00355674 is the fog colour, packed
    // 0xRRGGBB; DAT_0035567c and DAT_00355680 are the band it ramps across.
    // FUN_0022a418 seeds all three at scene load and the scene script may
    // overwrite them through opcodes 0xB9 and 0xBB -- so these are set per
    // scene by PortRuntime rather than derived from the draw distance here.
    void setFogColour(std::uint32_t packedRgb);
    void setFogBand(float nearDistance, float farDistance);
    std::uint32_t fogColour() const { return fogColourPacked_; }
    float fogNear() const { return fogNear_; }
    float fogFar() const { return fogFar_; }
    // Last framebuffer size seen by render(), so the next update() can widen
    // the cull frustum to whatever the window actually shows. Zero until the
    // first render, which is what headless --frames runs stay at.
    int lastFramebufferWidth() const { return lastFramebufferWidth_; }
    int lastFramebufferHeight() const { return lastFramebufferHeight_; }
    orphen::ported::psm2::Vec3 freeViewerMovement(float strafe, float forward) const;
    bool hasLeadPlayerView() const { return leadPlayerView_.has_value(); }
    float freeViewerYawDegrees() const { return cameraYawDegrees_; }
    void printLoadedSceneTree(std::ostream &output) const;
    void resetCamera();
    void update(float deltaSeconds, const orphen::port::InputSnapshot &input);
    void render(int framebufferWidth, int framebufferHeight) const;
    void setHudLines(std::vector<std::string> lines);
    void toggleHud() { hudVisible_ = !hudVisible_; }
    // Left click: report every entity triangle under this pixel, drawn or not.
    void probeAt(int pixelX, int pixelY) const;

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
    const orphen::ported::resource::TextureSlotCache *textureSlots_ = nullptr;
    mutable std::vector<unsigned int> slotTextureIds_;
    mutable bool slotTextureUploadDirty_ = true;
    orphen::ported::psm2::Vec3 cameraTarget_{};
    float cameraDistance_ = 12.0f;
    float cameraYawDegrees_ = 35.0f;
    float cameraPitchDegrees_ = -55.0f;
    orphen::ported::camera::CameraPose followCameraPose_;
    std::optional<orphen::ported::render::ViewProjection> renderCamera_;
    std::vector<orphen::ported::render::MapDrawItem> mapDrawList_;
    // Captured each frame so a click can build its ray in exactly the space the
    // frame was drawn in, rather than re-deriving the camera and hoping.
    mutable std::array<float, 16> probeModelView_{};
    mutable std::array<float, 16> probeProjection_{};
    mutable bool probeMatricesValid_ = false;
    mutable int lastFramebufferWidth_ = 0;
    mutable int lastFramebufferHeight_ = 0;
    // DAT_00355628, seeded from DAT_0032538c's 32.0 default (FUN_0022a360).
    float drawDistance_ = 32.0f;
    // FUN_0022a418's own defaults, for the case where no scene has been loaded
    // and nothing has pushed the block through. 0x505050 over 8..32.
    std::uint32_t fogColourPacked_ = 0x505050;
    float fogColour_[3] = {0x50 / 255.0f, 0x50 / 255.0f, 0x50 / 255.0f};
    float fogNear_ = 8.0f;
    float fogFar_ = 32.0f;
    DebugTextRenderer debugText_;
    std::vector<std::string> hudLines_;
    bool hudVisible_ = true;
    // B: the in-world debug drawing -- magenta collision boxes, entity labels,
    // the lead player's box and ground triangle, and the origin axes. Separate
    // from the HUD, which is screen-space text.
    bool debugOverlayVisible_ = true;
    bool wireframe_ = false;

    void loadDiscSceneAtIndex(std::size_t sceneIndex);
    void setTexturePages(std::vector<LoadedDiscTexturePage> texturePages);
    void releaseUploadedTextures() const;
    void ensureTexturesUploaded() const;
    void ensureSlotTexturesUploaded() const;
    void applyFogState(bool enabled) const;
  };

} // namespace orphen::harness
