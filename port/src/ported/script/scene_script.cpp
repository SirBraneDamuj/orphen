#include "ported/script/scene_script.h"

namespace orphen::ported::script
{
  namespace
  {
    std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      if (offset + 4 > bytes.size())
      {
        return 0;
      }
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }
  } // namespace

  const char *sceneScriptEntryName(SceneScriptEntry entry)
  {
    switch (entry)
    {
    case SceneScriptEntry::Init: return "init";
    case SceneScriptEntry::Start: return "start";
    case SceneScriptEntry::Tick: return "tick";
    case SceneScriptEntry::ActorStatePrimary: return "actor-primary";
    case SceneScriptEntry::ActorStateSecondary: return "actor-secondary";
    default: return "?";
    }
  }

  bool SceneScript::load(std::span<const std::uint8_t> decodedScript)
  {
    blob_.clear();
    texturePageIds_.clear();
    objectScriptSlots_.clear();
    // FUN_0025b390 clears the work array and the object-script slots at load.
    state_ = SceneScriptState{};

    if (decodedScript.size() < kSceneScriptHeaderWordCount * 4)
    {
      return false;
    }

    blob_.assign(decodedScript.begin(), decodedScript.end());
    for (std::size_t index = 0; index < kSceneScriptHeaderWordCount; ++index)
    {
      headerWords_[index] = readU32(blob_, index * 4);
    }

    // Header word 6: a zero-terminated list of texture page ids.
    const std::uint32_t texturePageOffset = headerWords_[6];
    if (texturePageOffset != 0 && texturePageOffset < blob_.size())
    {
      for (std::size_t offset = texturePageOffset; offset + 1 < blob_.size(); offset += 2)
      {
        const std::uint16_t pageId =
            static_cast<std::uint16_t>(blob_[offset] | (blob_[offset + 1] << 8));
        if (pageId == 0)
        {
          break;
        }
        texturePageIds_.push_back(pageId);
      }
    }

    return true;
  }

  std::uint32_t SceneScript::headerWord(std::size_t index) const
  {
    return index < kSceneScriptHeaderWordCount ? headerWords_[index] : 0;
  }

  std::uint32_t SceneScript::entryOffset(SceneScriptEntry entry) const
  {
    return headerWord(static_cast<std::size_t>(entry));
  }

  bool SceneScript::runEntry(SceneScriptEntry entry,
                             const ScriptEnvironment &environment,
                             ScriptTrace &trace)
  {
    lastRunOverran_ = false;
    lastRunHaltedOnUnimplemented_ = false;
    lastHaltOpcode_ = 0;
    lastHaltOffset_ = 0;

    if (!loaded())
    {
      return false;
    }

    const std::uint32_t offset = entryOffset(entry);
    SceneCommandInterpreter interpreter(blob_, environment, trace);
    const bool completed = interpreter.FUN_0025bc68_run(offset);

    lastRunOverran_ = interpreter.overran();
    lastRunHaltedOnUnimplemented_ = interpreter.haltedOnUnimplemented();
    lastHaltOpcode_ = interpreter.haltOpcode();
    lastHaltOffset_ = interpreter.haltOffset();

    // An entry whose body is a single block end is legitimately empty -- header
    // word 4 in s01_e024 is exactly that.
    const bool empty = offset < blob_.size() && blob_[offset] == 0x04;
    trace.noteEntryRun(sceneScriptEntryName(entry), offset, empty);

    return completed;
  }

  bool SceneScript::FUN_0025b6d0_run_init(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    // FUN_0025b6d0 also clears 0x30 bytes at 0x571E40 and resets DAT_0035504c,
    // the 0x4E lookup counter, before running the entry.
    state_.DAT_0035504c_lookupCount = 0;
    return runEntry(SceneScriptEntry::Init, environment, trace);
  }

  bool SceneScript::FUN_0025b728_run_start(const ScriptEnvironment &environment, ScriptTrace &trace)
  {
    return runEntry(SceneScriptEntry::Start, environment, trace);
  }

} // namespace orphen::ported::script
