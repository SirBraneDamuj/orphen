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
#include "ported/render/scene_lighting.h"
#include "ported/text/original_dialogue_text.h"
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

  // One entry per model drawn while a diagnostic run is collecting. Enough to
  // answer the two questions the specular pass raises without another
  // build-and-look cycle: does a given model reach the pass at all, and are the
  // dot products it computes in range.
  struct GleamProbe
  {
    std::size_t slot = 0;
    std::int32_t typeId = 0;
    std::size_t primitivesTested = 0;  // passed the fogByte / flag gate
    std::size_t cornersEvaluated = 0;
    std::size_t cornersLit = 0;        // produced an opacity above zero
    float maxDot = -2.0f;              // dot(N, H); above 1 means N is not unit
    float maxOpacity = 0.0f;
    float minNormalLength = 0.0f;      // 1.0 unless the bone transform scales
    float maxNormalLength = 0.0f;
  };

  // Where a frame's render time goes, accumulated across a window of frames so
  // the numbers are steady enough to compare two builds.
  //
  // The phases are timed and everything finer is counted, deliberately. A
  // clock read per primitive would be a large fraction of what it is measuring
  // -- the map path draws 1630 primitives per frame in s01_e024, so timing each
  // one costs more than some of them do. The counters are what make the timings
  // interpretable: microseconds alone cannot tell you whether a phase is slow
  // because of the work or because of the number of times it asks GL to change
  // state.
  struct RenderStats
  {
    std::uint32_t frames = 0;

    // Microseconds, summed over `frames`.
    std::uint64_t entityListMicros = 0; // the entity depth sort
    std::uint64_t mapDrawMicros = 0;    // map primitives only
    std::uint64_t entityDrawMicros = 0; // model draw, base passes and specular
    std::uint64_t overlayMicros = 0;    // debug boxes, labels, axes
    std::uint64_t hudMicros = 0;
    // A glFinish at the end of the frame, so everything the driver deferred is
    // billed to one bucket instead of surfacing inside whichever later call
    // happens to fill its queue. Only collected while --frame-stats is on --
    // the finish itself is a stall and does not belong in a normal frame.
    std::uint64_t gpuDrainMicros = 0;
    // Everything before the first draw: viewport, the fog-colour clear, the
    // camera matrices, the fog state.
    std::uint64_t prologueMicros = 0;
    // The two glGetFloatv calls that snapshot the frame's matrices for the
    // click probe. A get is a sync point on many drivers.
    std::uint64_t matrixReadMicros = 0;

    // Counts, summed over `frames`.
    std::uint64_t mapPrimitives = 0;
    std::uint64_t mapTriangles = 0;
    std::uint64_t mapBatches = 0;      // glBegin/glEnd pairs
    std::uint64_t mapTextureBinds = 0; // glBindTexture calls
    std::uint64_t entityModels = 0;
    std::uint64_t entityPrimitives = 0;
    std::uint64_t entityTriangles = 0;
    std::uint64_t entityBatches = 0;
    std::uint64_t gleamTriangles = 0;
    // posedWorldNormal + modulator, the per-corner lighting evaluation.
    std::uint64_t lightingEvaluations = 0;
    // posedViewerVertex, the per-corner bone transform.
    std::uint64_t vertexTransforms = 0;
  };

  class MapViewer
  {
  public:
    ~MapViewer();

    void loadDecodedPsm2(const std::filesystem::path &path);
    void loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection);
    bool cycleDiscScene(int direction);
    void setLeadPlayerView(std::optional<orphen::port::PlayerViewState> playerView);
    void setSceneObjectViews(orphen::port::SceneObjectViewList objects);
    // What the renderer will actually pose with, as opposed to a palette a
    // report rebuilds for itself.
    const orphen::port::SceneObjectViewList &sceneObjectViews() const { return sceneObjectViews_; }
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
    // The VU1 per-vertex lighting block. Pushed per scene by PortRuntime for
    // the same reason the fog is: FUN_0022a418 seeds it and the scene script
    // may overwrite it through opcodes 0x96 and 0x97.
    void setSceneLighting(const orphen::ported::render::SceneLighting &lighting);
    const orphen::ported::render::SceneLighting &sceneLighting() const { return sceneLighting_; }
    // Rebuilds the specular half-vector, which moves with the camera and so has
    // to be refreshed every frame rather than only when the scene changes.
    void setGleamDirection(float yawRadians, float pitchRadians);
    // Non-null makes render() measure the specular pass into the sink, clearing
    // it each frame, whether or not the pass is drawing.
    void setGleamProbeSink(std::vector<GleamProbe> *sink) { gleamProbeSink_ = sink; }
    // Non-null makes render() accumulate its phase timings and counters into
    // the sink. Null -- the default -- costs one predicted branch per counted
    // event and nothing else.
    void setRenderStatsSink(RenderStats *sink) { renderStatsSink_ = sink; }
    // --map-no-blend. Draws every map primitive opaque, the way the port did
    // before FUN_00211230's ABE block was ported. Diagnostic only.
    void setMapBlendDisabled(bool disabled) { mapBlendDisabled_ = disabled; }
    // Which of the derived-but-unconfirmed lighting behaviours are live.
    orphen::ported::render::SceneLighting &mutableSceneLighting() { return sceneLighting_; }
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
    // FUN_0025d0e0's output: the full-screen fade quad's colour and coverage.
    // Drawn over the scene and under both debug overlays, which is where the
    // original's own overlay sits relative to the debug text.
    void setScreenFadeOverlay(std::uint32_t packedRgb, std::uint8_t alpha);

    // The dialogue system's sprite list for this frame -- FUN_00237fc0's
    // 300-entry glyph array, reduced to what is actually placed. Drawn over the
    // fade, out of the texture slots, in the order given. Empty hides it.
    void setDialogueSprites(std::vector<orphen::ported::text::DialogueSprite> sprites);

    void setHudLines(std::vector<std::string> lines);
    // FUN_00268270's placed glyphs for this frame, drained from the ported
    // debug text buffer by PortRuntime on the simulation step.
    void setOriginalDebugGlyphs(std::vector<orphen::ported::debug::DebugGlyph> glyphs);
    void toggleHud() { hudVisible_ = !hudVisible_; }
    // Left click: report every entity triangle under this pixel, drawn or not.
    void probeAt(int pixelX, int pixelY) const;

    orphen::ported::psm2::Psm2RuntimeState *loadedMap();
    const orphen::ported::psm2::Psm2RuntimeState *loadedMap() const;
    const SceneResourceProvider *loadedSceneResources() const;
    const std::string &loadedSourceDescription() const { return loadedSourceDescription_; }
    // The scene currently loaded from the disc, or nullopt when the map came
    // from --psm2. `section` is the stage number, which FUN_0022a418:50 uses as
    // the map-streamed prop bank.
    std::optional<McbSceneSelection> loadedDiscScene() const
    {
      if (discScenes_.empty() || currentDiscSceneIndex_ >= discScenes_.size())
      {
        return std::nullopt;
      }
      return discScenes_[currentDiscSceneIndex_];
    }
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
    // Which generation of the slot cache slotTextureIds_ was built from. The
    // cache is refilled in place on a scene reload, so its address cannot tell
    // us the pixels changed.
    mutable std::uint64_t uploadedSlotGeneration_ = 0;
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
    orphen::ported::render::SceneLighting sceneLighting_;
    std::vector<GleamProbe> *gleamProbeSink_ = nullptr;
    RenderStats *renderStatsSink_ = nullptr;
    DebugTextRenderer debugText_;
    std::vector<std::string> hudLines_;
    std::vector<orphen::ported::debug::DebugGlyph> originalDebugGlyphs_;
    std::uint32_t screenFadeRgb_ = 0;
    std::uint8_t screenFadeAlpha_ = 0;
    std::vector<orphen::ported::text::DialogueSprite> dialogueSprites_;
    // H: the harness's own screen-space text. Off by default -- it covers the
    // game's picture, and the game has a debug overlay of its own.
    bool hudVisible_ = false;
    // B: the in-world debug drawing -- magenta collision boxes, entity labels,
    // the lead player's box and ground triangle, and the origin axes. Separate
    // from the HUD, which is screen-space text. Off by default for the same
    // reason.
    bool debugOverlayVisible_ = false;
    bool wireframe_ = false;
    bool mapBlendDisabled_ = false;

    void loadDiscSceneAtIndex(std::size_t sceneIndex);
    void setTexturePages(std::vector<LoadedDiscTexturePage> texturePages);
    void releaseUploadedTextures() const;
    void ensureTexturesUploaded() const;
    void ensureSlotTexturesUploaded() const;
    // The 4:3 box the game's picture occupies inside the window.
    struct ViewportRect
    {
      int x = 0;
      int y = 0;
      int width = 0;
      int height = 0;
    };
    static ViewportRect gameViewportRect(int framebufferWidth, int framebufferHeight);
    // FUN_00239020's sprites, fitted to the framebuffer the same way the
    // ported debug overlay is.
    void drawDialogueSprites(int framebufferWidth, int framebufferHeight) const;
    void applyFogState(bool enabled) const;
  };

} // namespace orphen::harness
