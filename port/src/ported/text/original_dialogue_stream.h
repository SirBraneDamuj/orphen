#pragma once

// The message-window driver, as much of it as a cutscene needs to keep time.
//
//   src/FUN_00237b38.c  start or terminate a stream       (analyzed/dialogue_start_stream.c)
//   src/FUN_00237c60.c  "the window is up"                 -- script opcode 0x34
//   src/FUN_00237c70.c  "the stream has finished"          -- script opcode 0x35
//   src/FUN_00237de8.c  the per-frame stream walk          -- NOT ported here
//
// **This is not the dialogue system**, and it is not trying to be. It reproduces
// the part a scene's *timing* depends on -- the two event flags a stream raises
// and lowers, which is what the event scheduler's gates wait on -- and reads the
// record's text well enough to print it. The 31-entry control-code table at
// PTR_FUN_0031c640 is not walked: half its entries have no analysis yet, and
// guessing their operand widths would be exactly the kind of invention the
// opcode VM refuses to make.
//
// The consequence is that the window closes on a **timer** rather than on the
// player pressing Cross. That is deliberate for an animatic pass: it lets the
// whole opening chain play unattended. A real dialogue port replaces this file
// and `original_item_window.*` together.
//
// Where the text comes from: the scheduler hands over a blob offset that is one
// of the entries in header word 5's pointer table, and the *next* entry bounds
// it. So a record's extent is known exactly without parsing a single control
// code -- which is why the printed lines are trustworthy even though the walk is
// not.

#include "ported/script/scene_command_interpreter.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::text
{

  class DialogueStream
  {
  public:
    // How long a line stays up, as a stand-in for the player's Cross press.
    // Frame-driven so `--frames` stays deterministic.
    static constexpr std::uint32_t kBaseHoldFrames = 60;
    static constexpr std::uint32_t kFramesPerCharacter = 2;

    // FUN_00237b38 with a non-zero pointer. `recordEnd` is the next
    // pointer-table entry, or the blob end for the last record.
    void FUN_00237b38_start(std::span<const std::uint8_t> blob,
                            std::uint32_t recordBegin,
                            std::uint32_t recordEnd,
                            orphen::ported::script::SceneScriptState &state);

    // FUN_00237b38(0). Raises flags 0x8FE / 0x8FF and the 0x6000 bits, which is
    // what a scheduler gate waiting on "the text finished" is watching.
    void FUN_00237b38_terminate(orphen::ported::script::SceneScriptState &state);

    // FUN_00237c60 / FUN_00237c70. The original's completion test is "the mode
    // is 8 and the cursor sits on a NUL"; here the timer stands in for the
    // cursor reaching the end.
    bool FUN_00237c60_busy() const { return active_; }
    bool FUN_00237c70_complete() const { return !active_; }

    // Burns the hold down and terminates when it runs out.
    void update(std::uint32_t frameTicks, orphen::ported::script::SceneScriptState &state);

    void reset();

    const std::string &speaker() const { return speaker_; }
    const std::string &line() const { return line_; }

    struct LoggedLine
    {
      std::uint32_t frame = 0;
      std::uint32_t recordOffset = 0;
      std::string speaker;
      std::string line;
    };
    const std::vector<LoggedLine> &log() const { return log_; }
    void setFrame(std::uint32_t frame) { frame_ = frame; }

  private:
    bool active_ = false;
    std::uint32_t holdTicks_ = 0;
    std::uint32_t frame_ = 0;
    std::string speaker_;
    std::string line_;
    // Event flags the record's 0x1B codes ask for, applied when it closes.
    std::vector<std::uint32_t> pendingFlags_;
    std::vector<LoggedLine> log_;
  };

} // namespace orphen::ported::text
