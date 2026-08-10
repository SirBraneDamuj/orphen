#include "ported/text/original_dialogue_stream.h"

namespace orphen::ported::text
{
  namespace
  {
    // FUN_00237b38's two resource ids. A stream *clears* them while it is up and
    // *sets* them when it ends, so "flag 0x8FE is set" means "no text on
    // screen" -- which reads backwards until you notice the scheduler gates are
    // written as waits.
    constexpr std::uint32_t kFlagTextIdleA = 0x8FF;
    constexpr std::uint32_t kFlagTextIdleB = 0x8FE;

    // The window-visible flag FUN_00237b38 clears on every path.
    constexpr std::uint32_t kFlagWindowVisible = 0x509;

    // uGpffffb0f4 bits 0x6000, the scheduler's other gate.
    constexpr std::uint32_t kGateMaskText = 0x6000;

    // Control code 0x13 introduces the speaker name; the run after it is the
    // name and everything printable later in the record is the line. Both are
    // NUL-terminated inside the record.
    constexpr std::uint8_t kControlSpeaker = 0x13;

    // 0x02 ends the stream (LAB_00239328 calls FUN_00237b38(0)), so it is also
    // where the text ends.
    constexpr std::uint8_t kControlTerminate = 0x02;

    // 0x1B / FUN_00239aa0: set the event flag named by the next two bytes.
    constexpr std::uint8_t kControlSetFlag = 0x1B;

    bool printable(std::uint8_t byte) { return byte >= 0x20 && byte < 0x7F; }

    // How many bytes follow a control code, for the handful whose payload would
    // otherwise be mistaken for text. These are the codes whose operands are
    // *documented* in analyzed/text_ops/; the rest carry none that collide with
    // printable ASCII in the records this scene uses.
    //
    // Getting one of these wrong only garbles a printed line -- it cannot desync
    // anything, because the record's extent comes from the pointer table rather
    // than from walking these codes.
    std::size_t controlPayload(std::uint8_t code)
    {
      switch (code)
      {
      case 0x0C: return 1; // set scaled parameter
      case 0x16: return 6; // voice trigger: channel, wait flag, u32 id
      case 0x18: return 1; // conditional control byte
      case 0x19: return 1;
      case 0x1A: return 0; // wait on the audio flag -- no payload
      case 0x1B: return 2; // set an event flag: the next two bytes, little-endian
      case 0x1C: return 2;
      default: return 0;
      }
    }
  } // namespace

  void DialogueStream::FUN_00237b38_start(std::span<const std::uint8_t> blob,
                                          std::uint32_t recordBegin,
                                          std::uint32_t recordEnd,
                                          orphen::ported::script::SceneScriptState &state)
  {
    speaker_.clear();
    line_.clear();
    pendingFlags_.clear();

    if (recordBegin < blob.size())
    {
      // A target that is not itself a pointer-table entry -- opcode 0x33's
      // inline text is the case -- has no next entry to bound it, so cap the
      // scan. Without the cap one such record printed a kilobyte of the code
      // segment read as ASCII.
      constexpr std::size_t kMaxRecordBytes = 512;
      const std::size_t end = std::min<std::size_t>(
          {static_cast<std::size_t>(recordEnd), blob.size(), recordBegin + kMaxRecordBytes});
      bool sawSpeakerCode = false;
      bool speakerTaken = false;
      std::string run;

      const auto flushRun = [&]() {
        if (run.empty())
        {
          return;
        }
        if (sawSpeakerCode && !speakerTaken)
        {
          speaker_ = run;
          speakerTaken = true;
        }
        else
        {
          if (!line_.empty())
          {
            line_ += ' ';
          }
          line_ += run;
        }
        run.clear();
      };

      for (std::size_t at = recordBegin; at < end; ++at)
      {
        const std::uint8_t byte = blob[at];
        if (printable(byte))
        {
          run.push_back(static_cast<char>(byte));
          continue;
        }
        flushRun();
        if (byte == kControlTerminate)
        {
          break;
        }
        if (byte == kControlSpeaker)
        {
          sawSpeakerCode = true;
          continue;
        }
        // 0x1B is the one control code that has to actually *run*: it sets an
        // event flag, and the cutscene scheduler gates on those. s01_e012's
        // opening waits on flag 0x6A, which nothing in the script sets -- it is
        // written from the tail of Volcan's line.
        if (byte == kControlSetFlag && at + 2 < end)
        {
          pendingFlags_.push_back(static_cast<std::uint32_t>(blob[at + 1]) |
                                  (static_cast<std::uint32_t>(blob[at + 2]) << 8));
        }
        at += controlPayload(byte);
      }
      flushRun();
    }

    // The hold stands in for the player's Cross press, so it scales with how
    // much there is to read.
    holdTicks_ = (kBaseHoldFrames + kFramesPerCharacter * static_cast<std::uint32_t>(line_.size())) *
                 orphen::ported::kNominalFrameTicks;
    active_ = true;

    state.FUN_002663d8_clearEventFlag(kFlagWindowVisible);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleA);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask &= ~kGateMaskText;

    log_.push_back(LoggedLine{frame_, recordBegin, speaker_, line_});
  }

  void DialogueStream::FUN_00237b38_terminate(orphen::ported::script::SceneScriptState &state)
  {
    active_ = false;
    holdTicks_ = 0;

    // The original executes 0x1B as the walk reaches it, so a flag at the tail
    // of a record lands when the line finishes reading. The port scans the
    // whole record up front, so the sets are held here and applied on close --
    // which puts them at the right moment for every site this scene uses, all
    // of which sit at the end of their record.
    for (std::uint32_t flagId : pendingFlags_)
    {
      state.FUN_002663a0_setEventFlag(flagId);
    }
    pendingFlags_.clear();

    state.FUN_002663d8_clearEventFlag(kFlagWindowVisible);
    state.FUN_002663a0_setEventFlag(kFlagTextIdleA);
    state.FUN_002663a0_setEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask |= kGateMaskText;
  }

  void DialogueStream::update(std::uint32_t frameTicks,
                              orphen::ported::script::SceneScriptState &state)
  {
    if (!active_)
    {
      return;
    }
    if (holdTicks_ > frameTicks)
    {
      holdTicks_ -= frameTicks;
      return;
    }
    FUN_00237b38_terminate(state);
  }

  void DialogueStream::reset()
  {
    active_ = false;
    holdTicks_ = 0;
    frame_ = 0;
    speaker_.clear();
    line_.clear();
    pendingFlags_.clear();
    log_.clear();
  }

} // namespace orphen::ported::text
