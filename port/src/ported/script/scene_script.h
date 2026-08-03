#pragma once

#include "ported/script/scene_command_interpreter.h"
#include "ported/script/script_trace.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace orphen::ported::script
{

  // Native counterpart of src/FUN_0025b390.c (scene_loader_and_initializer) and
  // the five entrypoint drivers it sets up. See
  // analyzed/map_bootstrap_sequence.c.
  //
  // The blob starts with eleven dwords. Words 0..4 are entry offsets, each
  // invoked from its own function in the original; words 5 and 6 point at the
  // string offset table and the scene's texture page id list.
  constexpr std::size_t kSceneScriptHeaderWordCount = 11;

  // Cleared by FUN_0025b390 as 65 dwords immediately after the script body.
  // The table itself lives in SceneScriptState, next to the rest of the
  // script-visible globals.
  constexpr std::size_t kObjectScriptSlotCount = 65;

  // FUN_0025b600's divisor. Deliberately not kScriptCoordinateScale: the scene
  // defaults block is scaled by 1000, the coordinate opcodes by 100000.
  inline constexpr float kSceneDefaultScale = 1000.0f;

  // Passed to runAtOffset when no entity should be pre-selected.
  constexpr std::size_t kNoSelectedEntity = static_cast<std::size_t>(-1);

  // DAT_00355060, cleared as 128 dwords.
  // DAT_00355060 is 128 words; see SceneScriptState::kWorkWordCount.

  enum class SceneScriptEntry : std::size_t
  {
    Init = 0,             // FUN_0025b6d0, at load
    Start = 1,            // FUN_0025b728, at load
    Tick = 2,             // FUN_0025b778, every frame
    ActorStatePrimary = 3,   // FUN_0025b978
    ActorStateSecondary = 4, // FUN_0025b9a8
  };

  const char *sceneScriptEntryName(SceneScriptEntry entry);

  class SceneScript
  {
  public:
    // Returns false when the blob is too small to hold a header.
    bool load(std::span<const std::uint8_t> decodedScript);

    bool loaded() const { return !blob_.empty(); }
    std::span<const std::uint8_t> blob() const { return blob_; }
    std::uint32_t headerWord(std::size_t index) const;
    std::uint32_t entryOffset(SceneScriptEntry entry) const;

    // The scene's texture page ids, from header word 6: a zero-terminated list
    // of halfwords. Read for the report; the port already gets its textures from
    // the MCB bundle.
    const std::vector<std::uint16_t> &texturePageIds() const { return texturePageIds_; }

    // FUN_0025b600's per-scene defaults, parsed at load. The spawn is the one
    // the game uses when you arrive without an explicit warp target -- which is
    // what loading a map from the debug menu does. Empty when the scene carries
    // no such block.
    const std::optional<orphen::ported::psm2::Vec3> &sceneSpawn() const { return sceneSpawn_; }
    const std::optional<float> &sceneDrawDistance() const { return sceneDrawDistance_; }

    // FUN_0025b6d0: clears the 0x4E lookup counter and runs header word 0.
    // FUN_0025b728: runs header word 1.
    //
    // These are the two the engine runs at load. The other three exist and are
    // reachable through runEntry, but nothing calls them yet -- see
    // port/README.md for what turning them on pulls in.
    bool FUN_0025b6d0_run_init(const ScriptEnvironment &environment, ScriptTrace &trace);
    bool FUN_0025b728_run_start(const ScriptEnvironment &environment, ScriptTrace &trace);

    bool runEntry(SceneScriptEntry entry, const ScriptEnvironment &environment, ScriptTrace &trace);

    // Header word 3 is the interaction hook, and FUN_00252828 sets
    // psGpffffb79c to the probed entity before running it -- so unlike every
    // other entry, this one needs an entity pre-selected.
    bool runEntryForEntity(SceneScriptEntry entry,
                           const ScriptEnvironment &environment,
                           ScriptTrace &trace,
                           std::size_t selectedEntity);

    // FUN_0025b778: the per-frame path. Runs header word 2, then every occupied
    // general slot 0x00..0x3D in order, then the lead-bound slot 0x40 with the
    // entity selection pointed at pool slot 0.
    //
    // The slots are not coroutines: each holds a fixed entry offset and is
    // re-entered from the top every frame, running to a block end. Nothing in
    // the original ever suspends the interpreter mid-stream. See
    // analyzed/scene_script_frame_entry.c.
    //
    // Two things FUN_0025b778 does that the port deliberately does not: the
    // letterbox bars (FUN_0025cfb8, a GS packet) and the debug work-flag dump.
    // A third, FUN_0025ce30's deferred trigger queue, is not modelled at all --
    // no scene the port runs fills it, and a silent stub would be worse than a
    // named absence.
    bool FUN_0025b778_run_tick(const ScriptEnvironment &environment, ScriptTrace &trace);

    // FUN_0025b918: slots 0x3E and 0x3F, which the frame function runs later,
    // after the entity updates rather than before them.
    bool FUN_0025b918_run_late_slots(const ScriptEnvironment &environment, ScriptTrace &trace);

    // The two actor-state entries. Header word 3 is the player's interaction
    // hook (FUN_00252828) and word 4 is the entity teardown hook (FUN_00265ec0);
    // neither is per-frame, so neither is driven by the tick above. Reachable
    // through runEntry when those paths are ported.

    // Reported by the last runEntry call.
    bool lastRunOverran() const { return lastRunOverran_; }
    bool lastRunHaltedOnUnimplemented() const { return lastRunHaltedOnUnimplemented_; }
    std::uint16_t lastHaltOpcode() const { return lastHaltOpcode_; }
    std::uint32_t lastHaltOffset() const { return lastHaltOffset_; }

    // How many of the 65 object-script slots currently hold an entry, so the
    // report can say how much the per-frame path is carrying.
    std::size_t occupiedObjectScriptSlots() const;

    // The script-visible globals, so callers can hand them to the interpreter
    // and inspect them afterwards.
    SceneScriptState &state() { return state_; }
    const SceneScriptState &state() const { return state_; }

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t headerWords_[kSceneScriptHeaderWordCount]{};
    std::vector<std::uint16_t> texturePageIds_;
    std::optional<orphen::ported::psm2::Vec3> sceneSpawn_;
    std::optional<float> sceneDrawDistance_;
    void FUN_0025b600_read_scene_defaults();
    SceneScriptState state_;

    // Shared by runEntry and the slot loops.
    bool runAtOffset(std::uint32_t offset,
                     const ScriptEnvironment &environment,
                     ScriptTrace &trace,
                     std::size_t selectedEntity);

    bool lastRunOverran_ = false;
    bool lastRunHaltedOnUnimplemented_ = false;
    std::uint16_t lastHaltOpcode_ = 0;
    std::uint32_t lastHaltOffset_ = 0;
  };

} // namespace orphen::ported::script
