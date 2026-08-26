#pragma once

#include "harness/disc_resource_loader.h"
#include "runtime/input_state.h"
#include "harness/map_viewer.h"
#include "runtime/original_lead_player.h"
#include "ported/camera/original_field_camera.h"
#include "ported/debug/original_debug_text.h"
#include "ported/debug/original_position_display.h"
#include "ported/player/original_chest_cutscene.h"
#include "ported/player/original_item_window.h"
#include "ported/sound/original_sound_engine.h"
#include "ported/sound/original_voice_index.h"
#include "ported/text/original_dialogue_text.h"
#include "ported/text/original_dialogue_stream.h"
#include "ported/resource/item_database.h"
#include "ported/render/original_letterbox.h"
#include "ported/render/original_screen_fade.h"
#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_path_follow.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/player_bandana.h"
#include "runtime/entity_model_store.h"
#include "ported/resource/elf_data_reader.h"
#include "ported/script/scene_script.h"
#include "runtime/ps2_memory.h"
#include "ported/original_frame_timing.h"
#include "ported/render/original_map_visibility.h"
#include "ported/render/original_view_projection.h"

#include <array>
#include <memory>
#include <cstdint>
#include <utility>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orphen::port
{

  struct PortRuntimeConfig
  {
    std::filesystem::path decodedPsm2Path;
    std::filesystem::path discRoot;
    // --voice-index: VOICE.BIN, or an EE dump holding the boot-loaded copy of
    // its table. Left empty, the disc root is searched for either.
    std::filesystem::path voiceIndexPath;
    orphen::harness::McbSceneSelection discScene;
    bool hasDiscScene = false;
    bool exitAfterUsage = false;
    bool loadOnly = false;
    bool printSceneTree = false;
    bool printScriptReport = false;
    bool printActorReport = false;
    bool printRenderReport = false;
    // --sound-report: every cue the frame loop asked for, and what happened.
    bool printSoundReport = false;
    // --model-report: parse every grp record in the bundle and print its counts.
    bool printModelReport = false;
    // --scr-trace-range <lo>-<hi>: log every SCR opcode executed at a blob
    // offset inside the range, with the frame it ran on.
    //
    // The aggregate report answers "was this opcode reached"; this answers "in
    // what order, and did the branch go the way I think". It is the only way to
    // read a body the port has no disassembler for -- feed it the offsets the
    // report already prints. Off unless a range is given; `hasScrTraceRange`
    // rather than a sentinel because offset 0 is a legal bound.
    bool hasScrTraceRange = false;
    std::uint32_t scrTraceRangeLow = 0;
    std::uint32_t scrTraceRangeHigh = 0;
    // --arm-stream <hex>[:<frame>]: arm scheduler channel 0 with a stream, the
    // same way opcode 0xA1 does, at the given frame.
    //
    // Most of a scene's cutscenes are not in the opening chain -- they are armed
    // by floor panels the player walks onto, and a panel is a two-triangle
    // square that no constant stick input will reliably find. This reaches them
    // without solving navigation, which is the only way to exercise the second
    // half of a scene's choreography headlessly.
    bool hasArmStream = false;
    std::uint32_t armStreamOffset = 0;
    std::uint32_t armStreamFrame = 1;
    // --hide-slots: pool slots to drop from the published draw list. Triage
    // only -- it answers "which entity is that" for on-screen geometry, which
    // no report can, because a report names entities and a screenshot names
    // pixels. Nothing else reads it, so it cannot affect simulation.
    std::vector<int> hideSlots;
    // Lighting behaviours derived from VU1 this session. Off by default so the
    // port renders the last visually confirmed state; each can be enabled alone
    // so a regression is attributable. --gleam-report measures the specular
    // pass without drawing it.
    bool applyLightFloor = false;
    bool applyUnlitFlag = false;
    // The dynamic point lights default ON, unlike the two above: the VU0 list
    // they are built from was read back out of a save state and matched the
    // script's table exactly, so this is a confirmed path rather than a derived
    // one. --lighting-no-points is here to A/B it against the previous look.
    bool suppressPointLights = false;
    // --map-no-blend: draw every map primitive opaque, the way the port did
    // before FUN_00211230's ABE block was ported. Diagnostic only.
    bool suppressMapBlend = false;
    // --map-base-slot: draw only material slot 0, the way the port did before
    // FUN_00211230's slot loop was ported. Diagnostic only.
    bool mapBaseSlotOnly = false;
    // --entity-bound-texture: ignore each PSC3 subdraw's texture selector.
    bool entityBoundTextureOnly = false;
    // --dump-map-textures <dir>: write the decoded map texture pages out as PAM
    // and stop caring about them. Empty means no dump.
    std::string dumpMapTexturesPath;
    // --pose-report <slot>: dump one entity's bone palette next to an
    // unfiltered rebuild of it, bone by bone, and name the first divergence.
    int poseReportSlot = -1;
    bool printGleamReport = false;
    // --frame-stats: accumulate a per-phase render breakdown and print it
    // every kFrameStatsWindow frames. Windowed runs only -- headless never
    // calls render(), which is where all of it is collected.
    bool printFrameStats = false;
    // --no-vsync. Read by main() when it opens the window, not by the runtime.
    bool vsync = true;
    // --no-audio. Read by main(): headless and capture runs never open a
    // device anyway, so this only matters for a normal windowed run.
    bool audio = true;
    // --window <w>x<h>. The default is 4:3, which is the shape the game was
    // displayed at and so has no letterbox bars; anything else does, which is
    // the only way to see whether something respects them.
    int windowWidth = 960;
    int windowHeight = 720;
    // --sound-dump <path>: mix the frame loop's cues into a WAV. Headless only;
    // it is how the mixer gets checked without a speaker.
    std::string soundDumpPath;
    // --music-solo: mute the effect pool and voice line in the mixer, so a
    // --sound-dump holds only the sequence slots. Diagnostic only.
    bool musicSolo = false;
    // --no-scr-subproc-disp: clear DAT_003555dd bit 7, which the port otherwise
    // holds set so the subproc lines ride along with the position readout the
    // same overlay already draws. 'P' toggles it at runtime.
    bool noSubprocDisplay = false;
    // --screenshot <path>[:<frame>]. Runs the window on a fixed one-simulation-
    // step-per-frame schedule so the captured frame is reproducible, writes a
    // PPM and exits. Read by main().
    std::string screenshotPath;
    std::uint32_t screenshotFrame = 120;
    // --render-bench N: draw the frame N times before presenting it. The DWM
    // paces a windowed surface to the refresh whether or not the swap interval
    // is zero, so a single render per present can never measure more than
    // "fits in 16.6 ms". Amortising one present over N renders puts the real
    // per-render cost back above the floor. Benchmarking only -- the extra
    // renders are identical and land in the same back buffer.
    std::uint32_t rendersPerFrame = 1;
    // DAT_00355628 override, for experimenting before the script opcode that
    // normally sets it (FUN_00263cb8) is wired up.
    std::optional<float> drawDistanceOverride;
    // --probe: dump the primitives around a world point and stop.
    std::optional<orphen::ported::psm2::Vec3> probeCentre;
    float probeRadius = 2.0f;
    // Drive the scene script's per-frame entry and its object-script slots.
    // **On by default.** It used to be off to protect the determinism baseline,
    // but the per-frame entry is what evaluates the terrain triggers, so with it
    // off the floor panels are inert and the scene looks broken in a way that
    // has nothing to do with the panels. --no-scr-tick restores the old
    // behaviour; runs are deterministic either way.
    bool runScriptTick = true;
    std::uint32_t headlessFrameCount = 0;
    // 1-based frames on which a headless or capture run should press Cross.
    // A list, because the chest cutscene needs two: one to open the chest and
    // one to dismiss the item caption.
    std::vector<std::uint32_t> pressConfirmFrames;
    // --hold-stick <angle>,<magnitude>: drive the analog stick for every
    // headless or capture frame, so movement-driven behaviour -- footsteps
    // above all -- is reachable without a pad. Magnitude is the original's
    // 0..128; above 100 is a run.
    std::optional<std::pair<float, float>> holdStick;
    // Fires the next-map request every N headless frames, so the map-cycle
    // scene reload can be exercised without a window.
    std::uint32_t cycleMapEveryFrames = 0;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride;
    // Retail executable, read for static tables such as the entity descriptors.
    // Optional: when empty, SLUS_200.11 is looked for in the disc root, and when
    // that is missing too the port runs without descriptor data.
    std::filesystem::path executablePath;
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

    // Reports that are only meaningful after frames have run. Called once at
    // shutdown; a no-op unless the matching flag was passed.
    void printExitReports() const;

    // --frame-stats. main() owns the loop, so it owns the simulation and
    // buffer-swap timings; the render breakdown is collected in here and read
    // back out through this.
    bool frameStatsEnabled() const { return printFrameStats_; }
    orphen::harness::RenderStats &frameStats() { return frameStats_; }

  private:
    Ps2Memory memory_;
    orphen::harness::MapViewer mapViewer_;
    orphen::ported::entity::EntityPool entityPool_;
    orphen::ported::script::SceneScript sceneScript_;
    orphen::ported::script::ScriptTrace scriptTrace_;
    OriginalLeadPlayer leadPlayer_;
    orphen::ported::camera::OriginalFieldCamera fieldCamera_;
    orphen::ported::render::ViewProjection renderCamera_;
    orphen::ported::render::MapVisibilityReport visibilityReport_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride_;
    // Kept so a map cycle can rebind the model store against the new scene's
    // bundle the same way initialize does.
    std::filesystem::path discRoot_;
    orphen::ported::entity::MapPropDescriptorTable mapPropTable_;
    // Opcode 0xBD's path-follow slots. Ticked just before the actor loop.
    //
    // Heap-held on purpose: PortRuntime is a stack local in main() and already
    // carries the 688 KB of bone-palette banks, so it sits close enough to the
    // default 1 MB stack that even a couple of KB more kills the process before
    // it prints anything.
    std::unique_ptr<orphen::ported::entity::PathFollowerTable> pathFollowers_ =
        std::make_unique<orphen::ported::entity::PathFollowerTable>();
    const char *spawnSourceLabel_ = "map centre";
    float previousStickMagnitude_ = 0.0f;
    std::optional<orphen::ported::resource::ElfDataReader> executable_;
    orphen::ported::entity::EntityDescriptorTable descriptorTable_;
    orphen::ported::entity::ActorDispatchTable actorDispatchTable_;
    orphen::ported::entity::ActorTrace actorTrace_;
    EntityModelStore modelStore_;
    // FUN_0020c5a8 lines 74-75: 0x003FFE00 + slot * 0xA80, one block per pool
    // slot. FUN_0020d188's two filters read and write it across frames, so it
    // is runtime state and not something the pose sampler can derive.
    //
    // Heap, not an array member: the original's bank is 688 KB of BSS, and
    // PortRuntime is a stack local in main().
    std::vector<orphen::ported::model::EntityPoseFilter> DAT_003ffe00_poseFilters_ =
        std::vector<orphen::ported::model::EntityPoseFilter>(
            orphen::ported::entity::kEntitySlotCount);
    // DAT_004a7e00 + slot * 0x540, the scripted bone override table. It starts
    // exactly where the pose filter bank ends.
    std::vector<orphen::ported::model::EntityBoneOverrides> DAT_004a7e00_boneOverrides_ =
        std::vector<orphen::ported::model::EntityBoneOverrides>(
            orphen::ported::entity::kEntitySlotCount);
    // DAT_00357e00 + slot * 0xA80, the matrix palette itself. The views carry a
    // copy for the renderer, but they are rebuilt from scratch every frame and
    // an attached entity needs *last* frame's -- FUN_00213720 runs in the actor
    // loop, before FUN_0020c5a8 has rebuilt anything.
    std::vector<std::vector<orphen::ported::model::Matrix4>> DAT_00357e00_bonePalettes_ =
        std::vector<std::vector<orphen::ported::model::Matrix4>>(
            orphen::ported::entity::kEntitySlotCount);
    // DAT_0054EE00, the bandana's two rope chains. One block for the whole game,
    // exactly as in the original -- there is only ever one bandana.
    orphen::ported::entity::BandanaState DAT_0054ee00_bandana_;
    // DAT_003555b4 / DAT_003555b8: the frame counter and the tick accumulator.
    // Wave phases are read off them, so they advance with the simulation step
    // and not with wall-clock time.
    std::uint32_t DAT_003555b4_frameCounter_ = 0;
    std::uint32_t DAT_003555b8_tickCounter_ = 0;
    bool runScriptTick_ = false;
    bool drawDistanceOverridden_ = false;
    bool suppressPointLights_ = false;
    int poseReportSlot_ = -1;
    void printPoseReport(std::size_t slot) const;

    // 'G' in play. One frame's worth of everything the pose pipeline decided,
    // for a glitch that only reproduces by playing the scene rather than at a
    // frame number a capture run can be pointed at.
    void writeDiagnosticSnapshot();

  public:
    // Set for one frame after writeDiagnosticSnapshot ran, so main() can
    // photograph the same frame next to the text. Returns the .ppm path to
    // write, or empty when no snapshot is pending.
    std::string consumePendingSnapshotImagePath();

  private:
    std::string pendingSnapshotImagePath_;
    // FUN_00216868 stand-in. Seeded to a constant so --frames is reproducible.
    std::uint32_t actorRandomState_ = 0x12345678u;
    // Rising-edge state for the live trigger log, so stepping on a panel says
    // so once rather than 60 times a second.
    std::map<std::uint32_t, bool> triggerWasPassing_;
    std::uint32_t reportedFadeArms_ = 0;
    std::uint32_t reportedPlayerLocks_ = 0;
    std::uint32_t reportedBattleBoots_ = 0;
    void reportPanelActivity();
    bool printActorReport_ = false;
    orphen::ported::text::DialogueStream dialogueStream_;
    bool printScriptReport_ = false;
    bool printModelReport_ = false;
    bool armStreamPending_ = false;
    std::uint32_t armStreamOffset_ = 0;
    std::uint32_t armStreamFrame_ = 1;
    std::vector<int> hideSlots_;
    bool printRenderReport_ = false;
    bool printGleamReport_ = false;
    std::vector<orphen::harness::GleamProbe> gleamProbes_;
    bool printFrameStats_ = false;
    orphen::harness::RenderStats frameStats_;
    // Set once, the first time a per-frame script run stops on an unimplemented
    // opcode, so a halting tick says so instead of failing silently every frame.
    mutable bool reportedTickHalt_ = false;

    void loadExecutable(const PortRuntimeConfig &config);
    // The whole per-scene load: model bindings, player reset, scene script.
    // Shared by initialize and the map-cycle path so they cannot drift.
    void loadSceneForCurrentMap();
    // FUN_0022a178's ten-entry table at DAT_00325394; the loop runs slots 0..9.
    static constexpr std::size_t kMapTextureSlotCount = 10;
    void FUN_0022a178_bind_map_textures();
    // FUN_00228e28: the map-streamed prop banks, built once from SCR.BIN.
    void loadMapPropDescriptors();
    void runSceneScript();
    // FUN_0022a418's environment defaults, then the values the script left
    // behind. Called either side of the init/start entries.
    void seedSceneEnvironmentDefaults();
    void applySceneEnvironment();
    void reportSceneEnvironment() const;
    void publishSceneObjectViews(std::uint32_t frameTicks);
    // One slot of that walk. Split out so FUN_0020c5a8's deferral queue can
    // call it in dependency order rather than slot order.
    void publishOneSceneObjectView(orphen::port::SceneObjectViewList &views,
                                   std::size_t slot,
                                   orphen::ported::entity::OriginalEntity &entity,
                                   std::uint32_t frameTicks);
    void advanceEntityAnimations(std::uint32_t frameTicks);
    void attachModel(SceneObjectView &view,
                     orphen::ported::entity::OriginalEntity &entity,
                     std::uint32_t frameTicks);
    void applySceneMarkerSpawn();
    void printScriptReport() const;
    void printActorReport() const;
    void printRenderReport() const;
    void printModelReport() const;
    void printEntityModelBindings() const;
    void printPrimitiveProbe(const orphen::ported::psm2::Vec3 &centre, float radius) const;
    void updateMapVisibility(orphen::ported::psm2::Psm2RuntimeState &map, const PlayerViewState &leadState);
    void reportTickHalt(const char *what) const;
    bool runInteractionProbe();
    orphen::ported::script::ScriptEnvironment scriptEnvironment(
        std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks);
    orphen::ported::entity::ActorEnvironment actorEnvironment(std::uint32_t frameTicks);
    void resetLeadPlayerForLoadedMap();
    void reportLeadPlayerGroundChange();
    orphen::ported::camera::CameraGroundSampler cameraGroundSampler();
    void updateHud(const InputSnapshot &input, std::uint32_t frameTicks);
    // FUN_002239c8's POSITION_DISP block, then FUN_00268270's layout pass.
    void updateOriginalDebugOverlay();
    // The player-state handler for the chest cutscene, installed on the
    // controller. Returns true when it owned the frame.
    bool stepScriptedPlayerState(std::uint32_t frameTicks);

    // DAT_00354d2c / iGpffffadbc. 0 is the field frame (FUN_00224218 plus the
    // rest of FUN_002239c8); 6 is the cutscene frame (FUN_002245d8), which
    // runs the player and the actors but neither the scene script nor the
    // field camera.
    std::uint32_t DAT_00354d2c_gameMode_ = orphen::ported::player::kGameModeField;
    orphen::ported::render::ScreenFade DAT_00571dc0_screenFade_;
    // DAT_00355054 / DAT_00355CFC, the cinematic bars. Opcode 0x6D arms them,
    // FUN_0025b778's tail steps them, the renderer draws them and the dialogue
    // window reads the mode to move its text clear -- one object, the way the
    // fade is one object.
    orphen::ported::render::Letterbox DAT_00355054_letterbox_;
    // DAT_00355700. FUN_00209140 hands it to VU1 as the cap on every map
    // primitive's fade byte, against the 0x80 = x1.0 scale -- so a small
    // non-zero value renders the world nearly black. Zero is "no cap".
    std::uint8_t DAT_00355700_globalFadeCap_ = 0;
    // FUN_002342c0's render-state block: while the item scene is up, the two
    // VU1 light colours drop and the fog colour goes to black. Held as a flag
    // rather than as three overwritten globals so applySceneEnvironment stays
    // the single place the scene's own values are read.
    bool itemSceneRenderState_ = false;
    void setItemSceneRenderState(bool enable);
    // FUN_00254f60's item branch, and the caption it ends with.
    bool buildChestItemEntity(std::size_t chestSlot, std::int16_t itemId);
    orphen::ported::player::ItemWindow itemWindow_;
    orphen::ported::resource::ItemDatabase itemDatabase_;
    // The proportional width table FUN_00238c90 measures out of slots 0x2E and
    // 0x2F at boot, and this frame's glyph list built against it.
    orphen::ported::text::DialogueFont dialogueFont_;
    std::vector<orphen::ported::text::DialogueSprite> buildDialogueSprites() const;

    // FUN_00228e28:81's cue table lives in SCR.BIN resource 199, alongside the
    // item names in resource 1.
    static constexpr std::uint32_t kScrSoundCueResource = 199;
    orphen::ported::sound::SoundEngine soundEngine_;
    bool printSoundReport_ = false;
    void loadSoundData();
    // FUN_0025b2f0 + FUN_00206840: the eight music requests in scene header
    // word 10.
    static constexpr std::size_t kSceneMusicRequests = 8;
    void startSceneMusic();

    // VOICE.BIN's table of contents, which is what gives a line of dialogue its
    // real length. Optional: without it every hold falls back to an estimate and
    // the report says so.
    orphen::ported::sound::VoiceIndex voiceIndex_;
    void loadVoiceIndex(const PortRuntimeConfig &config);
    // Only decode a clip when something will actually mix it.
    bool voiceAudioEnabled_ = false;
    void FUN_0022a418_stamp_lead_player_flags();
    void printSoundReport() const;

  public:
    // main() owns the audio device, because only a windowed run has one.
    orphen::ported::sound::SoundEngine &soundEngine() { return soundEngine_; }

  private:

    // DAT_00572c38 / DAT_003551dc, the debug overlay's text buffer.
    orphen::ported::debug::DebugTextBuffer DAT_00572c38_debugText_;
    // cGpffffb128 / DAT_00355098, the debug menu's POSITION_DISP toggle. The
    // original defaults it off and FUN_00268d30 turns it on; the port holds it
    // on because there is no debug menu to reach it through yet.
    bool DAT_00355098_positionDisplay_ = true;
    // DAT_003555dd, the debug display byte. Bit 7 is SCR SUBPROC DISP.
    //
    // Held set by default, the same way DAT_00355098_positionDisplay_ is: the
    // debug menu that writes this byte in the original has no way in here, and
    // these lines belong to the same readout as the position display.
    std::uint8_t DAT_003555dd_debugDisplay_ =
        orphen::ported::script::ScriptEnvironment::kSubprocDisplayBit;

  public:
    void setDAT_003555dd_debugDisplay(std::uint8_t value) { DAT_003555dd_debugDisplay_ = value; }
    std::uint8_t DAT_003555dd_debugDisplay() const { return DAT_003555dd_debugDisplay_; }
    void toggleSubprocDisplay()
    {
      DAT_003555dd_debugDisplay_ ^= orphen::ported::script::ScriptEnvironment::kSubprocDisplayBit;
    }
    bool subprocDisplayEnabled() const
    {
      return (DAT_003555dd_debugDisplay_ &
              orphen::ported::script::ScriptEnvironment::kSubprocDisplayBit) != 0;
    }

  private:
  };

} // namespace orphen::port
