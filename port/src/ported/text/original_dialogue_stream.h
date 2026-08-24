#pragma once

// The message-window driver, as much of it as a cutscene needs to keep time.
//
//   src/FUN_00237b38.c  start or terminate a stream       (analyzed/dialogue_start_stream.c)
//   src/FUN_00237c60.c  "the window is up"                 -- script opcode 0x34
//   src/FUN_00237c70.c  "the stream has finished"          -- script opcode 0x35
//   src/FUN_00237de8.c  the per-frame stream walk          -- NOT ported here
//
// This file owns a record's *timing*: the two event flags a stream raises and
// lowers, and how long a line stays up. It also reads the record's text into
// `speaker()` / `line()` for the report, which is a flat scan with none of the
// original's layout in it.
//
// Rendering is `DialogueWindow`, which this class owns and steps -- the real
// glyph slot array, word wrap, typewriter pacing and scroll, ported from
// FUN_00237de8 and friends. See `original_dialogue_window.h`, which also
// explains why the US release shows no subtitles and what the port does about
// it. Player input is still absent: a record closes on its clip.
//
// ## How a line keeps time
//
// A record's hold is the length of its voice clip, and the record says so
// explicitly. The relevant control codes, with the cursor advance taken from
// each handler's own stores to -0x5140(gp) rather than inferred from the data:
//
//   0x16 ch wait id32   (+7)  j FUN_00206ae0 -- cache clip `id` on channel `ch`
//   0x17                (+1)  block until the load finishes
//   0x18 ch             (+2)  j FUN_00206d98 -- *start* the clip cached on `ch`
//   0x19 ch byte        (+3)  the same, and store a byte to gp-0x4999
//   0x1A                (+1)  block until DAT_00356788 falls back to zero,
//                             i.e. until the clip has played out
//   0x02                (+1)  j FUN_00237b38(0) -- end the stream
//
// The handler for 0x18 was documented as a "conditional control byte set" that
// consumed a byte and returned. It does consume the byte, but it then tail-calls
// the voice player with it:
//
//   00239a64: j     0x00206d98
//   00239a68: daddu a0, a2, zero      ; delay slot: a0 = the operand byte
//
// The stream double-buffers: a record's 0x16 arms the clip for the *next* line
// into the other channel, so what a record plays was armed a record earlier.
// That is not a guess -- s01_e012's opening sits on record 1 in `eeMemory.bin`,
// and DAT_00356480 there reads {50, 79, 0}: channel 1 holds the clip record 1
// plays, channel 0 the one record 2 will. Walking all 83 records of `scr2.out`
// this way, every one has exactly one 0x18/0x19 and exactly one 0x1A, and the
// channel it starts is always already armed.
//
// Clip lengths come from `VoiceIndex`. Without one the hold falls back to a
// per-character estimate, which is what this file did for every line before --
// it kept the chain moving but bore no relation to the original's pacing.
//
// The window still closes on that timer rather than on the player pressing
// Cross. In a cutscene that is what the original does too, since 0x1A is the
// only thing gating the record's end.
//
// Where the text comes from: the scheduler hands over a blob offset that is one
// of the entries in header word 5's pointer table, and the *next* entry bounds
// it. So a record's extent is known exactly, and a control code whose width is
// still unknown can garble one printed line but cannot desync anything.

#include "ported/original_frame_timing.h"
#include "ported/script/scene_command_interpreter.h"
#include "ported/sound/original_voice_index.h"
#include "ported/text/original_dialogue_window.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::text
{

  class DialogueStream
  {
  public:
    // The stand-in used when no voice table is available. Frame-driven so
    // `--frames` stays deterministic either way.
    static constexpr std::uint32_t kBaseHoldFrames = 60;
    static constexpr std::uint32_t kFramesPerCharacter = 2;

    // Borrowed, not owned; may be null or invalid, in which case every line
    // falls back to the estimate.
    void setVoiceIndex(const orphen::ported::sound::VoiceIndex *index) { voiceIndex_ = index; }

    // The measured width table, needed before a glyph can be placed. Borrowed.
    void setFont(const DialogueFont *font) { window_.setFont(font); }

    // FUN_00237b38 with a non-zero pointer. `recordEnd` is the next
    // pointer-table entry, or the blob end for the last record.
    void FUN_00237b38_start(std::span<const std::uint8_t> blob,
                            std::uint32_t recordBegin,
                            std::uint32_t recordEnd,
                            orphen::ported::script::SceneScriptState &state);

    // FUN_00237b38(0). Takes the window down -- nothing is drawn from it any
    // more, and the next record that opens will clear it. Raises flags 0x8FE and
    // 0x8FF and the 0x6000 gate bits. Reached from an explicit script terminate,
    // and from a walk that ended on the 0x01 prompt or on 0x02.
    void FUN_00237b38_terminate(orphen::ported::script::SceneScriptState &state);

    // FUN_00239178's outermost branch, control code 0x00: the ordinary end of a
    // record. It raises flag 0x8FE and gate bit 0x2000 -- so a scheduler gate
    // waiting on "the text finished" opens either way -- and leaves the window
    // standing with its glyphs on it. That is the difference the speaker name
    // rides on; see DialogueWindow::FUN_00237b38_open.
    void FUN_00239178_end_record(orphen::ported::script::SceneScriptState &state);

    // FUN_00237c60 / FUN_00237c70. The original's completion test is "the mode
    // is 8 and the cursor sits on a NUL"; here the hold stands in for the cursor
    // reaching the end, and the hold is the clip.
    bool FUN_00237c60_busy() const { return active_; }
    bool FUN_00237c70_complete() const { return !active_; }

    // Burns the hold down and terminates when it runs out.
    void update(std::uint32_t frameTicks, orphen::ported::script::SceneScriptState &state);

    // FUN_00237fc0's glyph walk. It has its own slot in the frame -- the
    // original runs it *after* the script, so a record the script opened this
    // frame gets its first character this frame -- which is why it is not
    // folded into `update` above.
    void FUN_00237fc0_update(std::uint32_t frameTicks) { window_.FUN_00237fc0_update(frameTicks); }

    // iGpffffb0e4, published from the shared `Letterbox` before the walk runs.
    // FUN_00238a08 reads the global as it enqueues, so this has to be current
    // for the frame rather than latched when the record opened.
    void setMovieMode(int mode) { window_.setMovieMode(mode); }

    // FUN_00223698 caches per channel and never clears, so this survives across
    // records by design; only a scene load resets it.
    void reset();

    const std::string &speaker() const { return speaker_; }
    const std::string &line() const { return line_; }

    // FUN_00237fc0's glyph list for this frame, ready to blit.
    std::vector<DialogueSprite> sprites() const { return window_.sprites(); }
    const DialogueWindow &window() const { return window_; }

    struct LoggedLine
    {
      std::uint32_t frame = 0;
      std::uint32_t recordOffset = 0;
      std::uint32_t voiceId = 0;   // 0 when the record started nothing
      std::uint32_t holdFrames = 0;
      bool measured = false;       // false when the hold is the estimate
      std::string speaker;
      std::string line;
    };
    const std::vector<LoggedLine> &log() const { return log_; }
    void setFrame(std::uint32_t frame) { frame_ = frame; }

    // Report totals: how many lines got a real clip length, and how many fell
    // back. A scene where these disagree is a scene whose pacing is invented.
    std::uint32_t measuredLines() const { return measuredLines_; }
    std::uint32_t estimatedLines() const { return estimatedLines_; }
    // Records with neither a clip nor any text -- a bare terminate. They hold
    // for nothing, which is not the same as being invented.
    std::uint32_t emptyLines() const { return emptyLines_; }
    // Records that stayed up after their clip had finished, because the walk
    // had not reached the terminator yet, and what that cost in frames. Every
    // texted record does this by at least the two steps the walk needs to
    // consume its 0x1A and then its terminator; the *total* is the number that
    // says something -- it is text held on screen that closing on the clip
    // alone would have thrown away.
    std::uint32_t typewriterHeldLines() const { return typewriterHeldLines_; }
    std::uint32_t typewriterHeldFrames() const
    {
      return typewriterHeldTicks_ / orphen::ported::kNominalFrameTicks;
    }

  private:
    // The 0x1B flags this record collected, spent when it ends. Shared by both
    // terminators.
    void applyPendingFlags(orphen::ported::script::SceneScriptState &state);

    // The advance each control code applies to the cursor, opcode byte
    // included. Recovered from the handlers themselves.
    static std::size_t controlWidth(std::uint8_t code);

    const orphen::ported::sound::VoiceIndex *voiceIndex_ = nullptr;

    // The glyph slots and the walk that fills them.
    DialogueWindow window_;

    bool active_ = false;
    std::uint32_t holdTicks_ = 0;
    std::uint32_t frame_ = 0;
    std::string speaker_;
    std::string line_;
    // DAT_00356480: the clip cached on each of FUN_00206ae0's three channels.
    std::uint32_t voiceCache_[3] = {0, 0, 0};
    // Event flags the record's 0x1B codes ask for, applied when it closes.
    std::vector<std::uint32_t> pendingFlags_;
    std::vector<LoggedLine> log_;
    std::uint32_t measuredLines_ = 0;
    std::uint32_t estimatedLines_ = 0;
    std::uint32_t emptyLines_ = 0;
    std::uint32_t typewriterHeldLines_ = 0;
    std::uint32_t typewriterHeldTicks_ = 0;
    // Set while a record is past its clip and waiting on the walk, so the
    // counter above is one per record rather than one per frame.
    bool heldByTypewriter_ = false;
  };

} // namespace orphen::ported::text
