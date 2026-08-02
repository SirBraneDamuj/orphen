#pragma once

#include "ported/script/scene_command_interpreter.h"
#include "ported/script/script_trace.h"

#include <cstdint>
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
  constexpr std::size_t kObjectScriptSlotCount = 65;

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

    // FUN_0025b6d0: clears the 0x4E lookup counter and runs header word 0.
    // FUN_0025b728: runs header word 1.
    //
    // These are the two the engine runs at load. The other three exist and are
    // reachable through runEntry, but nothing calls them yet -- see
    // port/README.md for what turning them on pulls in.
    bool FUN_0025b6d0_run_init(const ScriptEnvironment &environment, ScriptTrace &trace);
    bool FUN_0025b728_run_start(const ScriptEnvironment &environment, ScriptTrace &trace);

    // FUN_0025b778 and the two actor-state entries. Present so the extension
    // point is obvious and so the per-frame path is a call away rather than a
    // rewrite; nothing in the runtime drives them yet.
    bool runEntry(SceneScriptEntry entry, const ScriptEnvironment &environment, ScriptTrace &trace);

    // Reported by the last runEntry call.
    bool lastRunOverran() const { return lastRunOverran_; }
    bool lastRunHaltedOnUnimplemented() const { return lastRunHaltedOnUnimplemented_; }
    std::uint16_t lastHaltOpcode() const { return lastHaltOpcode_; }
    std::uint32_t lastHaltOffset() const { return lastHaltOffset_; }

    // DAT_00355cf4: the 65-slot object-script pointer table, and DAT_00355060.
    // FUN_0025d380 registers scripts into the first free slot. The port records
    // registrations without ticking them, so the report can say how much is
    // waiting behind the per-frame path.
    const std::vector<std::uint32_t> &registeredObjectScripts() const { return objectScriptSlots_; }

    // The script-visible globals, so callers can hand them to the interpreter
    // and inspect them afterwards.
    SceneScriptState &state() { return state_; }
    const SceneScriptState &state() const { return state_; }

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t headerWords_[kSceneScriptHeaderWordCount]{};
    std::vector<std::uint16_t> texturePageIds_;
    std::vector<std::uint32_t> objectScriptSlots_;
    SceneScriptState state_;

    bool lastRunOverran_ = false;
    bool lastRunHaltedOnUnimplemented_ = false;
    std::uint16_t lastHaltOpcode_ = 0;
    std::uint32_t lastHaltOffset_ = 0;
  };

} // namespace orphen::ported::script
