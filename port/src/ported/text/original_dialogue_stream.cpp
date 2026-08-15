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

    // uGpffffb0f4 bits 0x6000, the scheduler's other gate.
    constexpr std::uint32_t kGateMaskText = 0x6000;

    constexpr std::uint8_t kControlTerminate = 0x02;   // LAB_00239328
    constexpr std::uint8_t kControlSpeaker = 0x13;     // FUN_00239760
    constexpr std::uint8_t kControlArmVoice = 0x16;    // LAB_00239990
    constexpr std::uint8_t kControlPlayVoice = 0x18;   // LAB_00239a30
    constexpr std::uint8_t kControlPlayVoiceExtra = 0x19;
    constexpr std::uint8_t kControlSetFlag = 0x1B;     // LAB_00239aa0
    constexpr std::uint8_t kFirstTextByte = 0x1F;      // FUN_00237de8's own test

    bool printable(std::uint8_t byte) { return byte >= 0x20 && byte < 0x7F; }
  } // namespace

  // Each handler's advance, read off its stores to -0x5140(gp). Where a handler
  // stores more than once the largest wins, since the smaller stores are the
  // partial advances on the way there.
  //
  //   0x00 00239178   0x01/03/04/05 002391d0   0x02 00239328   0x06 00239338
  //   0x07 00239368   0x08 00239390   0x09 00239c60   0x0A 002393e0
  //   0x0B 00239408   0x0C 00239428   0x0D 00239450   0x0E 00239478
  //   0x0F 002394f8   0x10 00239548   0x11 002395c0   0x12 00239750
  //   0x13 00239760   0x14 002397f0   0x15 00239848   0x16 00239990
  //   0x17 00239a00   0x18/0x19 00239a30   0x1A 00239a70   0x1B/0x1C 00239aa0
  //   0x1D 00239b00   0x1E 00239c78
  //
  // The handlers that consume only themselves are the ones whose payload is
  // rendered rather than parsed -- 0x13's speaker name is emitted as ordinary
  // glyphs by a recursive FUN_00237de8 call and ended by a 0x00.
  //
  // Two entries could not be read off a single straight-line pass and are set to
  // 1, the safe value: a width that is too small re-reads a payload byte as a
  // control code, which garbles the printed line, while a width that is too
  // large can step over a 0x16 or a 0x18 and lose a clip. Both 0x01 and 0x09
  // only ever appear at a record's tail in this scene's data, after the 0x1A, so
  // neither can affect a hold.
  std::size_t DialogueStream::controlWidth(std::uint8_t code)
  {
    switch (code)
    {
    case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
    case 0x14: case 0x18: case 0x1D:
      return 2;
    case 0x19: case 0x1B: case 0x1C: case 0x1E:
      return 3;
    case 0x0F: case 0x15:
      return 4;
    case 0x11: case 0x16:
      return 7;
    default:
      return 1;
    }
  }

  void DialogueStream::FUN_00237b38_start(std::span<const std::uint8_t> blob,
                                          std::uint32_t recordBegin,
                                          std::uint32_t recordEnd,
                                          orphen::ported::script::SceneScriptState &state)
  {
    speaker_.clear();
    line_.clear();
    pendingFlags_.clear();

    std::uint32_t playedVoice = 0;

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
    voiceCache_[0] = voiceCache_[1] = voiceCache_[2] = 0;
    pendingFlags_.clear();
    log_.clear();
    measuredLines_ = 0;
    estimatedLines_ = 0;
    emptyLines_ = 0;
  }

} // namespace orphen::ported::text
