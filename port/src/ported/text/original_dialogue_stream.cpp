#include "ported/text/original_dialogue_stream.h"

#include <algorithm>

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

    // uGpffffb0f4 bits 0x6000, the scheduler's other gate. FUN_00239178 raises
    // only 0x2000 of it; FUN_00237b38(0) raises both bits.
    constexpr std::uint32_t kGateMaskText = 0x6000;
    constexpr std::uint32_t kGateBitRecordEnded = 0x2000;

    constexpr std::uint8_t kControlTerminate = 0x02;   // LAB_00239328
    constexpr std::uint8_t kControlSpeaker = 0x13;     // FUN_00239760
    constexpr std::uint8_t kControlArmVoice = 0x16;    // LAB_00239990
    constexpr std::uint8_t kControlPlayVoice = 0x18;   // LAB_00239a30
    constexpr std::uint8_t kControlPlayVoiceExtra = 0x19;
    constexpr std::uint8_t kControlSetFlag = 0x1B;     // LAB_00239aa0
    constexpr std::uint8_t kFirstTextByte = 0x1F;      // FUN_00237de8's own test

    bool printable(std::uint8_t byte) { return byte >= 0x20 && byte < 0x7F; }
  } // namespace

  // The cursor advances live with the walk that uses them; see
  // FUN_00237de8_controlWidth in original_dialogue_window.h.
  std::size_t DialogueStream::controlWidth(std::uint8_t code)
  {
    return FUN_00237de8_controlWidth(code);
  }

  void DialogueStream::FUN_00237b38_start(std::span<const std::uint8_t> blob,
                                          std::uint32_t recordBegin,
                                          std::uint32_t recordEnd,
                                          orphen::ported::script::SceneScriptState &state)
  {
    speaker_.clear();
    line_.clear();
    pendingFlags_.clear();
    heldByTypewriter_ = false;

    std::uint32_t playedVoice = 0;

    // A target that is not itself a pointer-table entry -- opcode 0x33's inline
    // text is the case -- has no next entry to bound it, so cap the record.
    // Without the cap one such record printed a kilobyte of the code segment
    // read as ASCII.
    constexpr std::size_t kMaxRecordBytes = 512;
    const std::size_t end = std::min<std::size_t>(
        {static_cast<std::size_t>(recordEnd), blob.size(), recordBegin + kMaxRecordBytes});

    if (recordBegin < blob.size())
    {
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

      std::size_t at = recordBegin;
      while (at < end)
      {
        const std::uint8_t byte = blob[at];
        if (byte >= kFirstTextByte)
        {
          if (printable(byte))
          {
            run.push_back(static_cast<char>(byte));
          }
          ++at;
          continue;
        }
        flushRun();

        switch (byte)
        {
        case kControlTerminate:
          at = end;
          continue;

        case kControlSpeaker:
          sawSpeakerCode = true;
          break;

        case kControlArmVoice:
          // FUN_00206ae0(id, channel, wait): cache the clip against the
          // channel. The load is instantaneous here, so `wait` at +2 and the
          // 0x17 that follows are both no-ops.
          if (at + 7 <= end && blob[at + 1] < 3)
          {
            voiceCache_[blob[at + 1]] = static_cast<std::uint32_t>(blob[at + 3]) |
                                        (static_cast<std::uint32_t>(blob[at + 4]) << 8) |
                                        (static_cast<std::uint32_t>(blob[at + 5]) << 16) |
                                        (static_cast<std::uint32_t>(blob[at + 6]) << 24);
          }
          break;

        case kControlPlayVoice:
        case kControlPlayVoiceExtra:
          // FUN_00206d98(channel). The clip must already be cached; the
          // original returns -2 and stays silent when it is not.
          if (at + 1 < end && blob[at + 1] < 3)
          {
            playedVoice = voiceCache_[blob[at + 1]];
          }
          break;

        case kControlSetFlag:
          // 0x1B is the one control code that has to actually *run*: it sets an
          // event flag, and the cutscene scheduler gates on those. s01_e012's
          // opening waits on flag 0x6A, which nothing in the script sets -- it
          // is written from the tail of Volcan's line. 0x1C shares the handler
          // but fails its own opcode test and sets nothing.
          if (at + 2 < end)
          {
            pendingFlags_.push_back(static_cast<std::uint32_t>(blob[at + 1]) |
                                    (static_cast<std::uint32_t>(blob[at + 2]) << 8));
          }
          break;

        default:
          break;
        }
        at += controlWidth(byte);
      }
      flushRun();
    }

    // 0x1A holds the record open for exactly as long as the clip 0x18 started.
    const std::uint32_t measured =
        voiceIndex_ != nullptr ? voiceIndex_->holdTicks(playedVoice) : 0;
    if (measured != 0)
    {
      holdTicks_ = measured;
      ++measuredLines_;
    }
    else if (line_.empty() && speaker_.empty())
    {
      // A record that is nothing but a terminate. s01_e012 dispatches four of
      // these, all to the same 0x33 site, whose inline text is a bare 0x02: the
      // scene is closing the window, not saying anything. There is no 0x18 to
      // wait on and no glyphs to read, so the original is done with it on the
      // next pass -- holding it would insert a second of dead air per use.
      holdTicks_ = 0;
      ++emptyLines_;
    }
    else
    {
      // Text with no clip behind it. The original waits for the player here
      // rather than for audio, so a timer is the honest stand-in -- but it is
      // still invented, and the report says so.
      holdTicks_ = (kBaseHoldFrames + kFramesPerCharacter * static_cast<std::uint32_t>(line_.size())) *
                   orphen::ported::kNominalFrameTicks;
      ++estimatedLines_;
    }
    active_ = true;

    // The rendering walk runs over the same bytes, on its own cursor, and its
    // 0x1A blocks on the clip this record just started.
    window_.FUN_00237b38_open(blob, recordBegin, static_cast<std::uint32_t>(end));
    window_.setVoiceBusy(holdTicks_ != 0);

    state.FUN_002663d8_clearEventFlag(kFlagWindowVisible);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleA);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask &= ~kGateMaskText;

    log_.push_back(LoggedLine{frame_, recordBegin, playedVoice,
                              holdTicks_ / orphen::ported::kNominalFrameTicks, measured != 0,
                              speaker_, line_});
  }

  void DialogueStream::FUN_00237b38_terminate(orphen::ported::script::SceneScriptState &state)
  {
    active_ = false;
    holdTicks_ = 0;
    // A walk that stopped on 0x01 or 0x02 has already taken the window down
    // its own way; this is the explicit script terminate, FUN_00237b38(0) with
    // the window still up, which does not clear the slots either.
    window_.FUN_00237b38_close();
    applyPendingFlags(state);

    state.FUN_002663d8_clearEventFlag(kFlagWindowVisible);
    state.FUN_002663a0_setEventFlag(kFlagTextIdleA);
    state.FUN_002663a0_setEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask |= kGateMaskText;
  }

  void DialogueStream::FUN_00239178_end_record(orphen::ported::script::SceneScriptState &state)
  {
    active_ = false;
    holdTicks_ = 0;
    window_.FUN_00239178_end_record();
    applyPendingFlags(state);

    // FUN_00239178 raises exactly these two and nothing else -- no 0x8FF, no
    // 0x4000, and it does not touch 0x509. Every gate in s01_e012's scheduler
    // stream that waits on text waits on 0x8FE, so a record ending normally
    // opens them without pretending the window went away.
    state.FUN_002663a0_setEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask |= kGateBitRecordEnded;
  }

  // The original executes 0x1B as the walk reaches it, so a flag at the tail of
  // a record lands when the line finishes reading. The port scans the whole
  // record up front, so the sets are held and applied when the record ends --
  // which puts them at the right moment for every site this scene uses, all of
  // which sit at the end of their record.
  void DialogueStream::applyPendingFlags(orphen::ported::script::SceneScriptState &state)
  {
    for (std::uint32_t flagId : pendingFlags_)
    {
      state.FUN_002663a0_setEventFlag(flagId);
    }
    pendingFlags_.clear();
  }

  void DialogueStream::update(std::uint32_t frameTicks,
                              orphen::ported::script::SceneScriptState &state)
  {
    if (!active_)
    {
      return;
    }

    // Burn the clip down. This is DAT_00356788's whole role here: while it is
    // set, the walk's 0x1A refuses to advance.
    holdTicks_ = holdTicks_ > frameTicks ? holdTicks_ - frameTicks : 0;
    window_.setVoiceBusy(holdTicks_ != 0);

    // **The record ends where its bytes say it ends**, not when the clip runs
    // out. The two usually coincide, because 0x1A is the last thing before the
    // terminator -- but a record can put codes after it, and s01_e012's does:
    // Dortin's line ends `1A 0C 3C 02`, a full second of held text after the
    // audio has stopped. Closing on the clip alone dropped that second.
    if (!window_.complete())
    {
      if (holdTicks_ == 0)
      {
        if (!heldByTypewriter_)
        {
          heldByTypewriter_ = true;
          ++typewriterHeldLines_;
        }
        typewriterHeldTicks_ += frameTicks;
      }
      return;
    }
    heldByTypewriter_ = false;

    // Which terminator the walk stopped on decides whether the window survives.
    // A 0x02 has already run LAB_00239328 and taken it down; anything else is
    // FUN_00239178's ordinary record end.
    if (window_.windowUp())
    {
      FUN_00239178_end_record(state);
    }
    else
    {
      FUN_00237b38_terminate(state);
    }
  }

  void DialogueStream::reset()
  {
    active_ = false;
    holdTicks_ = 0;
    frame_ = 0;
    speaker_.clear();
    line_.clear();
    voiceCache_[0] = voiceCache_[1] = voiceCache_[2] = 0;
    pendingFlags_.clear();
    log_.clear();
    measuredLines_ = 0;
    estimatedLines_ = 0;
    emptyLines_ = 0;
    typewriterHeldLines_ = 0;
    typewriterHeldTicks_ = 0;
    heldByTypewriter_ = false;
    window_.reset();
  }

} // namespace orphen::ported::text
