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
#include "ported/resource/character_stats.h"
#include "ported/resource/hit_parameter_table.h"
#include "ported/resource/item_database.h"
#include "ported/render/original_frame_feedback.h"
#include "ported/render/original_letterbox.h"
#include "ported/render/original_screen_fade.h"
#include "ported/input/mapped_action_history.h"
#include "ported/battle/battle_encounter.h"
#include "ported/battle/battle_party.h"
#include "ported/battle/battle_character_update.h"
#include "ported/battle/battle_command_input.h"
#include "ported/battle/battle_trace.h"
#include "ported/entity/actor_dispatch_table.h"
#include "ported/entity/actor_frame_update.h"
#include "ported/entity/actor_trace.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_path_follow.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_hit_sparks.h"
#include "ported/render/original_entity_draw.h"
#include "ported/render/original_weapon_trail.h"
#include "ported/entity/original_particles.h"
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
    // --battle-report: the loadout -> button -> mask binding, the party the
    // scene built, and every frame the player's action byte or state changed.
    bool printBattleReport = false;
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
    // --snapshot-at <frame>: fire the 'G' diagnostic snapshot from a headless
    // run instead of a keypress, so the same report can be produced at a frame
    // number and diffed between builds. 0 means never.
    std::uint32_t snapshotFrame = 0;
    // --hide-slots: pool slots to drop from the published draw list. Triage
    // only -- it answers "which entity is that" for on-screen geometry, which
    // no report can, because a report names entities and a screenshot names
    // pixels. Nothing else reads it, so it cannot affect simulation.
    std::vector<int> hideSlots;
    // Lighting behaviours read out of VU1. The unlit flag is ON -- a GS dump of
    // s01_e012's shop proved it, see SceneLighting -- and --lighting-no-unlit
    // A/Bs it. The light floor is still derived-only and stays off behind
    // --lighting-floor, so a regression is attributable to exactly one of them.
    // --gleam-report measures the specular pass without drawing it.
    bool applyLightFloor = false;
    bool applyUnlitFlag = true;
    // The dynamic point lights default ON, unlike the two above: the VU0 list
    // they are built from was read back out of a save state and matched the
    // script's table exactly, so this is a confirmed path rather than a derived
    // one. --lighting-no-points is here to A/B it against the previous look.
    bool suppressPointLights = false;
    // --map-no-blend: draw every map primitive opaque, the way the port did
    // before FUN_00211230's ABE block was ported. Diagnostic only.
    bool suppressMapBlend = false;
    // --no-screen-smear: skip FUN_00201a38's quad while still capturing
    // the source, so two captures of the same frame isolate exactly what
    // the smear contributes.
    bool suppressScreenSmear = false;
    // --map-base-slot: draw only material slot 0, the way the port did before
    // FUN_00211230's slot loop was ported. Diagnostic only.
    bool mapBaseSlotOnly = false;
    // --entity-bound-texture: ignore each PSC3 subdraw's texture selector.
    bool entityBoundTextureOnly = false;
    // --scr-dump <path>: write the scene script blob out exactly as the
    // interpreter sees it, so its bytecode can be read outside the runtime.
    std::string scrDumpPath;
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
    // The same, for Circle: --press-attack. FUN_00256bb8's attack branch is not
    // reachable from a headless run any other way.
    std::vector<std::uint32_t> pressAttackFrames;
    // And for Triangle: --press-magic, the cast.
    std::vector<std::uint32_t> pressMagicFrames;
    // --hold-triangle / --hold-circle / --hold-cross / --hold-square
    // <first>-<last>: hold one face button across an inclusive 1-based frame
    // range, with the pressed edge on the first frame only.
    //
    // The battle module cannot be exercised any other way. Its five action
    // pairs are all press-and-release -- FUN_002462c8 emits the press action
    // when the trigger mask catches a newly pressed bit and the release action
    // when the *held* word it latched goes clear -- so a one-frame pulse enters
    // a charge and leaves it the same frame. The charge level a spell fires at
    // is how many ticks separated the two.
    // Four face buttons, then the four D-pad directions -- the target cycler
    // reads the pad's newly-pressed word, so a "hold" is one step at the start
    // of the range, which is exactly one cycle.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> holdFaceButtons[8];
    // --hold-stick <angle>,<magnitude>: drive the analog stick for every
    // headless or capture frame, so movement-driven behaviour -- footsteps
    // above all -- is reachable without a pad. Magnitude is the original's
    // 0..128; above 100 is a run.
    std::optional<std::pair<float, float>> holdStick;
    // Fires the next-map request every N headless frames, so the map-cycle
    // scene reload can be exercised without a window.
    std::uint32_t cycleMapEveryFrames = 0;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride;
    // --place-slot: park one pool entity at a fixed world point every frame, so
    // a behaviour that depends on two entities being next to each other can be
    // exercised without waiting for the scene to arrange it. Debug scaffolding,
    // not a ported feature; nothing reads it unless the flag is given.
    struct PlacedSlot
    {
      int slot = -1;
      orphen::ported::psm2::Vec3 position;
    };
    std::vector<PlacedSlot> placedSlots;
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
    // fGpffffb6d4, kept beside the matrices it went into. The hit sparks turn
    // their fan by its negation, so it has to be the same yaw FUN_0020bec8 was
    // given or the two no longer cancel.
    float renderCameraYaw_ = 0.0f;
    // fGpffffb6f4 / uGpffffb6f8. Opcode 0x94 arms it, FUN_0020bec8_build
    // spends it, and FUN_0022a418:287 clears it on scene load.
    orphen::ported::render::CameraShake DAT_00355664_cameraShake_;
    orphen::ported::render::MapVisibilityReport visibilityReport_;
    std::uint32_t frameCount_ = 0;
    std::uint64_t trackedMapGeneration_ = 0;
    std::optional<std::size_t> reportedGroundPrimitive_;
    std::optional<orphen::ported::psm2::Vec3> spawnOverride_;
    std::vector<PortRuntimeConfig::PlacedSlot> placedSlots_;
    // Kept so a map cycle can rebind the model store against the new scene's
    // bundle the same way initialize does.
    std::filesystem::path discRoot_;

    // == The scene-change request, FUN_0022a418's four inputs ==
    //
    // DAT_003551ec is the request word and the rest are its operands. Zero means
    // "stay"; anything else is spent by FUN_002239c8:22 at the top of a frame.
    // Opcode 0x8E writes 0x20001 and nothing else in the port writes it yet.
    std::uint32_t DAT_003551ec_sceneRequest_ = 0;
    // DAT_003551f4 / DAT_003551f0: the section and entry of the scene the game
    // considers itself to be in. FUN_0022a418:50 copies the section into
    // DAT_00355208, the map-prop bank -- and note that 0x8E leaves *both* of
    // these alone, so a group-0xE scene keeps drawing its props out of the bank
    // belonging to the stage that sent it there.
    int DAT_003551f4_sceneSection_ = -1;
    int DAT_003551f0_sceneEntry_ = -1;
    // DAT_003551f8: the entry within the group-0xE list, which is what 0x8E's
    // operand actually is.
    int DAT_003551f8_groupEntry_ = 0;
    // DAT_00355208 (iGpffffb298), the map-prop bank FUN_00229980 resolves type
    // ids 0x272..0x371 against. Seeded from DAT_003551f4 at load and then
    // writable by opcode 0x3C, which is how a group-0xE scene replaces the bank
    // it inherited from whichever stage sent it there.
    int DAT_00355208_mapPropBank_ = -1;
    // DAT_003555d3, set from bit 0x20000 of the request. Sticky for the whole
    // time a group-0xE scene is loaded: FUN_0022a238 and FUN_0022a288 both read
    // it to decide which descriptor list they are walking.
    bool DAT_003555d3_groupEScene_ = false;
    // MCB0 section 14. FUN_0022a418:102 passes it as a literal.
    static constexpr std::uint16_t kGroupEScene = 14;
    // DAT_00354d78 / DAT_00354d7c, written at FUN_0022a418:409. The scene that
    // was current when the load started, which the *next* load compares against.
    int DAT_00354d78_previousSection_ = -1;
    int DAT_00354d7c_previousEntry_ = -1;
    // DAT_0031e668. FUN_002610a8 copies the lead's +0x20..+0x28 here on the way
    // out; FUN_00261068 and FUN_0026bc10 are the two that copy it back into the
    // spawn point DAT_00325340. Neither is reached yet, so this is written and
    // held rather than read.
    orphen::ported::psm2::Vec3 DAT_0031e668_departurePosition_{};
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
    // DAT_00355704 / DAT_00355708: the lead's breadcrumb trail, 512 entries
    // wide, and the cursor into it. FUN_0022a418 seeds every entry with the
    // lead's spawn position at scene load and FUN_00224060 appends to it once
    // per frame. A wedged party follower teleports back onto one of these.
    static constexpr std::size_t kLeadTrailCapacity = 0x200;
    std::array<orphen::ported::entity::ActorEnvironment::LeadTrailPoint, kLeadTrailCapacity>
        DAT_00355704_leadTrail_{};
    std::uint16_t DAT_00355708_leadTrailCursor_ = 0;

    // DAT_00342a70, the 64-frame ring of mapped action words FUN_0023b5d8
    // fills and FUN_0023b890 reads back. It is the game's input buffer: the
    // grounded player state asks for the last eight frames, so a button
    // pressed slightly early still fires.
    orphen::ported::input::MappedActionHistory DAT_00342a70_mappedActions_;
    void FUN_0022a418_reset_lead_trail();
    void FUN_00224060_record_lead_trail();

    // DAT_003555e8, this frame's analog magnitude. FUN_0023b5d8 publishes it
    // at the top of the frame; behaviours downstream of the pad read it, and
    // the party follower is the first of them.
    float DAT_003555e8_stickMagnitude_ = 0.0f;
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
    std::string scrDumpPath_;

    // DAT_00355038 and DAT_00355030: the follower navigation graph, and the
    // one-shot "no corner cut on this step" flag FUN_0025a500 raises around
    // its FUN_00259378 call.
    orphen::ported::entity::FollowerNavmesh followerNavmesh_;
    bool DAT_00355030_skipCornerCut_ = false;

    // FUN_002582d0. Called once per map load with the lead's spawn point, and
    // again from opcode 0xAB, which is how a scene that has opened a door or
    // swapped an area gets the new geometry into the graph.
    void FUN_002582d0_build_follower_navmesh();
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
    // Whether one is waiting. Fast forward skips most presents, and a snapshot
    // has to be photographed on the frame it describes.
    bool hasPendingSnapshotImage() const { return !pendingSnapshotImagePath_.empty(); }

  private:
    std::string pendingSnapshotImagePath_;
    // DAT_00355620, the one global particle pool -- 1536 entries shared by the
    // whole frame. FUN_002d3290 clears it when the scene loads, FUN_002d3218
    // steps it after the actor loop, and publishSpriteQuads draws whatever is
    // alive into the same display list the billboards use.
    orphen::ported::entity::ParticlePool DAT_00355620_particles_;

    // DAT_00355B74, the hit sparks -- a thousand entries in ten fixed groups.
    // FUN_002205d0 carves it out at boot, FUN_00216140 fires bursts into it,
    // and FUN_00220910 steps and draws it in the *draw* phase, which is why the
    // step lives in publishSpriteQuads rather than beside the particle one.
    orphen::ported::entity::HitSparkPool DAT_00355b74_hitSparks_;

    // DAT_004FBC7C, FUN_0020e840's 32 motion-trail slots, and the world-space
    // ribbons this frame's walk produced. The step runs per entity inside the
    // pose walk -- FUN_0020c810 calls FUN_0020e840 last, after the bone palette
    // is composed -- and publishSpriteQuads turns the ribbons into quads once
    // the camera is known.
    orphen::ported::render::WeaponTrailPool DAT_004fbc7c_weaponTrails_;
    struct PendingTrailRibbon
    {
      orphen::ported::render::TrailQuad quad;
      // FUN_0020e840 submits into `ctx+0x1D8 + 1`: one bucket in front of the
      // entity the trail belongs to, so the ribbon draws over its own model.
      int displayListBucket = 2;
    };
    std::vector<PendingTrailRibbon> pendingTrailRibbons_;

    // FUN_00216868 stand-in. Seeded to a constant so --frames is reproducible.
    std::uint32_t actorRandomState_ = 0x12345678u;
    // FUN_00216868, the one draw the three call sites share. A plain LCG rather
    // than the original's generator, which has not been analysed; what matters
    // is that it is seeded once and stepped deterministically.
    std::uint32_t FUN_00216868_random()
    {
      actorRandomState_ = actorRandomState_ * 1103515245u + 12345u;
      return (actorRandomState_ >> 16) & 0x7FFFu;
    }
    // Rising-edge state for the live trigger log, so stepping on a panel says
    // so once rather than 60 times a second.
    std::map<std::uint32_t, bool> triggerWasPassing_;
    std::uint32_t reportedFadeArms_ = 0;
    std::uint32_t reportedPlayerLocks_ = 0;
    std::uint32_t reportedBattleBoots_ = 0;
    void reportPanelActivity();
    bool printActorReport_ = false;
    bool printBattleReport_ = false;
    orphen::ported::text::DialogueStream dialogueStream_;
    bool printScriptReport_ = false;
    bool printModelReport_ = false;
    std::uint32_t snapshotFrame_ = 0;
    // DAT_003555d0, republished into ActorEnvironment each frame.
    bool DAT_003555d0_collisionGroupMoved_ = false;

    // The spell voice. DAT_00356480, the bank cached on each of FUN_00206ae0's
    // channels, and DAT_00356788 -- "something is speaking" -- which the port
    // models as a *tick countdown taken from the clip's own length* rather than
    // as a mixer query. The original's flag is simulation state cleared when the
    // stream ends, and reading the mixer instead would make `--frames` output
    // depend on whether audio was enabled.
    std::uint32_t DAT_00356480_voiceBankCache_[4] = {0, 0, 0, 0};
    std::uint32_t DAT_00356788_voiceHoldTicks_ = 0;

    // DAT_00355588. The shared hit effect's one-frame request word: FUN_002f1380
    // raises bit 0, FUN_002f13d0 (type 0x1E3) consumes it. Nothing raises it yet
    // -- the damage paths that call FUN_002f1380 are not ported -- so it stays 0
    // and the effect stays hidden, which is what the original does between hits.
    std::uint16_t DAT_00355588_hitEffectRequest_ = 0;
    // DAT_0031D178. FUN_0023f8b8 hands this back in place of an actor record
    // when no encounter group names the entity's id, with its pending-action
    // byte poisoned to 0xFF; every unbound enemy shares the one block, exactly
    // as they do on hardware.
    orphen::ported::entity::ActorEnvironment::BattleActorView DAT_0031d178_unboundActor_;
    // Frames DAT_003555d0 has been up, and times the embedded-corner push-out
    // has produced a request. Both go in the 'G' snapshot: they separate "the
    // gate never opened" from "it opened and found nothing embedded".
    std::uint32_t DAT_003555d0_liveFrames_ = 0;
    std::uint32_t pushOutCount_ = 0;
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
    // FUN_002239c8:22-33: spend a pending scene-change request, if this frame is
    // allowed to. Runs at the top of the frame, before the pad is published.
    void FUN_002239c8_service_scene_change();
    // Push DAT_00355208 into the two places that answer with it. Both setters
    // are plain assignments, so the write is safe mid-scene -- which it has to
    // be, because opcode 0x3C makes it from inside the init.
    void applyMapPropBank(int bank);
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
    // FUN_0020f3e0: the billboard pass's collect half.
    void publishSpriteQuads(std::uint32_t frameTicks);
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
    void updateMapVisibility(orphen::ported::psm2::Psm2RuntimeState &map,
                             const PlayerViewState &leadState,
                             std::uint32_t frameTicks);
    void reportTickHalt(const char *what) const;
    bool runInteractionProbe();
    orphen::ported::script::ScriptEnvironment scriptEnvironment(
        std::uint32_t frameTicks = orphen::ported::kNominalFrameTicks);
    orphen::ported::entity::ActorEnvironment actorEnvironment(std::uint32_t frameTicks);
    // Built alongside actorEnvironment and pointed at by it. Separate because
    // FUN_002148a8 is also the projectile's test, and both call sites want the
    // same one.
    orphen::ported::entity::HitTestEnvironment hitTestEnvironment(std::uint32_t frameTicks);
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

    // DAT_00355661 / DAT_00354B88 / DAT_00343878.., the screen smear that
    // FUN_002000c0:214 draws through FUN_00201a38. Opcodes 0xC8 and 0xC9 write
    // it, so it lives on the simulation side and the renderer only reads the
    // quad the step produced -- render() can run more than once per step
    // (--render-bench) and the ramp must not advance with it.
    orphen::ported::render::FrameFeedback DAT_00343878_frameFeedback_;
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

    // The battle module. FUN_002239c8:117 picks FUN_00249610 over FUN_00251ed8
    // when DAT_003555d3 and sGpffffb052 are both set, and script opcode 0xBD's
    // low methods are what set the second of those. See
    // ported/battle/battle_party.h.
    orphen::ported::battle::BattleParty battleParty_;
    // The scene's own encounter data: every actor the player can target.
    // Loaded by FUN_0023f318 out of the scene script, not out of the
    // executable, so it lives beside the script rather than in BattleParty.
    orphen::ported::battle::BattleEncounter battleEncounter_;
    bool battleMasterHaltReported_ = false;
    // FUN_0023fd30's second loop, reported the same way and separately: an
    // actor script reaches further into PTR_LAB_0031d118 than a master one.
    bool battleActorHaltReported_ = false;
    // How many orders FUN_00244248 took and how many it bounced, for
    // --battle-report. A bounce is not a fault -- it is the busy handshake
    // doing its job -- but the ratio is what says the AI is running at all.
    std::uint32_t battleActionsRequested_ = 0;
    std::uint32_t battleActionsRefused_ = 0;
    orphen::ported::battle::BattleParty::Environment battleEnvironment();
    orphen::ported::battle::BattleUpdateEnvironment battleUpdateEnvironment(std::uint16_t frameTicks);
    void sampleBattleTrace(std::uint32_t heldPad);
    orphen::ported::battle::BattleTrace battleTrace_;
    void printBattleReport() const;
    void printTargetDisplayReport() const;
    void printEncounterReport() const;
    // uGpffffadf8, the character stat table. DAT_00343688's seven party
    // records come out of it; see FUN_002294d0_load_party_records.
    orphen::ported::resource::CharacterStats characterStats_;
    // uGpffffadf4, SCR.BIN 0xBD. FUN_00228e28 loads it beside 0xBF; the only
    // reader ported so far is FUN_002334E8's object type ranges.
    orphen::ported::resource::CharacterStats DAT_00354d64_objectStats_;
    // DAT_0058B970: the per-type elemental damage rows FUN_002F0608 fills as it
    // retypes a tagged placement. Lives here because it is keyed by type id,
    // not by entity -- two objects of one type share a row.
    orphen::ported::entity::ElementDamageTable DAT_0058b970_elementDamage_;
    // uGpffffadfc, SCR.BIN resource 0xBE: the attack parameter table.
    orphen::ported::resource::HitParameterTable DAT_00354d6c_hitParameters_;
    // DAT_003151c8, the slot list the last hit test filled.
    orphen::ported::entity::HitList DAT_003151c8_hitList_;
    // Rebuilt every time actorEnvironment is, and pointed at by it. A member
    // rather than a temporary because ActorEnvironment holds a pointer to it.
    orphen::ported::entity::HitTestEnvironment hitTestEnvironment_;
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
