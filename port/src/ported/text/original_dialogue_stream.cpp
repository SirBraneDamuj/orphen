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

    // FUN_00239178. It is the RETURN from a 0x10 call first and the end of the
    // record only at the outermost level -- see the call stack in
    // FUN_00237b38_start.
    constexpr std::uint8_t kControlReturn = 0x00;
    // LAB_00239548, the CALL: a signed 32-bit displacement from the byte after
    // the opcode, five bytes in all.
    constexpr std::uint8_t kControlCall = 0x10;
    constexpr std::uint8_t kControlTerminate = 0x02;   // LAB_00239328
    constexpr std::uint8_t kControlSpeaker = 0x13;     // FUN_00239760
    constexpr std::uint8_t kControlArmVoice = 0x16;    // LAB_00239990
    constexpr std::uint8_t kControlPlayVoice = 0x18;   // LAB_00239a30
    constexpr std::uint8_t kControlPlayVoiceExtra = 0x19;
    constexpr std::uint8_t kControlSetFlag = 0x1B;     // LAB_00239aa0
    constexpr std::uint8_t kControlChoice = 0x15;      // FUN_00239848
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
    options_.clear();
    choiceWorkIndex_ = 0;
    heldByTypewriter_ = false;

    std::uint32_t playedVoice = 0;
    holdIsEstimate_ = false;

    // A target that is not itself a pointer-table entry -- opcode 0x33's inline
    // text is the case -- has no next entry to bound it, so cap the record.
    // Without the cap one such record printed a kilobyte of the code segment
    // read as ASCII.
    constexpr std::size_t kMaxRecordBytes = 512;
    std::size_t end = std::min<std::size_t>(
        {static_cast<std::size_t>(recordEnd), blob.size(), recordBegin + kMaxRecordBytes});
    // The pre-scan below re-aims `end` as it follows a 0x10 call, and a called
    // record that closes on 0x02 never unwinds it -- so the window has to be
    // opened on the record's own bound, kept here. s14_e004's lines are all
    // `0x33 -> 10 <rel32> 00` calls into shared records ending `0C nn 02`, and
    // handing the window the callee's end (below its own cursor) completed
    // every one of them before a glyph was placed.
    const std::size_t windowEnd = end;

    // LAB_00239548's call stack, the same eight frames DialogueWindow keeps.
    // This pre-scan has to follow a 0x10 as well, and for a second reason: the
    // 0x16/0x18 pair that names the clip a record waits on can live inside the
    // *called* record, and s14_e031's two narrator lines are exactly that --
    // nothing but a chain of voice codes behind a call, with no glyphs at all.
    // Without following it the hold falls back to an estimate and the line
    // comes out as the displacement bytes read as ASCII.
    constexpr std::size_t kCallDepth = 8;
    struct ScanFrame
    {
      std::size_t at = 0;
      std::size_t end = 0;
    };
    std::array<ScanFrame, kCallDepth> callStack{};
    std::size_t callDepth = kCallDepth;

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
        case kControlReturn:
          // FUN_00239178 returns before it ends: a frame on the stack pops and
          // the scan carries on behind the call. At the outermost level it is
          // the end of the record.
          if (callDepth < kCallDepth)
          {
            at = callStack[callDepth].at;
            end = callStack[callDepth].end;
            ++callDepth;
            continue;
          }
          at = end;
          continue;

        case kControlTerminate:
          // LAB_00239328 is the hard close and does not unwind.
          at = end;
          continue;

        case kControlCall:
        {
          // LAB_00239548. A signed 32-bit displacement from the byte after the
          // opcode; the return address is five past it.
          if (at + 5 > blob.size() || callDepth == 0)
          {
            break;
          }
          const std::uint32_t displacement =
              static_cast<std::uint32_t>(blob[at + 1]) |
              (static_cast<std::uint32_t>(blob[at + 2]) << 8) |
              (static_cast<std::uint32_t>(blob[at + 3]) << 16) |
              (static_cast<std::uint32_t>(blob[at + 4]) << 24);
          const std::size_t target =
              static_cast<std::size_t>(static_cast<std::uint32_t>(at + 1) + displacement);
          if (target >= blob.size())
          {
            break;
          }
          --callDepth;
          callStack[callDepth] = ScanFrame{at + 5, end};
          at = target;
          end = std::min<std::size_t>(blob.size(), target + kMaxRecordBytes);
          continue;
        }

        case kControlSpeaker:
        {
          // FUN_00239760 consumes the name **itself** -- a recursive
          // FUN_00237DE8 loop that stops on a 0x00 and then steps past it -- so
          // the outer walk never sees that terminator. Doing the same here is
          // what lets the 0x00 below keep its real meaning: return from a 0x10
          // call, or end the record. Treating a name's terminator as the end of
          // the record swallowed every line in s01_e012's opening.
          ++at;
          std::string name;
          while (at < end && blob[at] != kControlReturn)
          {
            const std::uint8_t glyph = blob[at];
            if (glyph >= kFirstTextByte)
            {
              if (printable(glyph))
              {
                name.push_back(static_cast<char>(glyph));
              }
              ++at;
              continue;
            }
            at += controlWidth(glyph);
          }
          if (at < end)
          {
            ++at;
          }
          sawSpeakerCode = true;
          if (!name.empty())
          {
            speaker_ = name;
            speakerTaken = true;
          }
          continue;
        }

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
          //
          // **A record can carry more than one 0x18, and only the first one
          // that finds a cached clip does anything.** Both FUN_00206d98 and
          // FUN_00206f08 open with `if (DAT_00356788 == 0)`, and FUN_00207010
          // sets that flag as it starts the voice, so a later 0x18 in the same
          // record returns -1 before it even reads its channel. It cannot
          // interrupt the clip and it cannot replace it. A 0x18 on an *unarmed*
          // channel leaves the flag alone -- it returns -2 from the
          // `DAT_00356480[channel] == 0` test above the start -- so "the first
          // one that finds a clip" is the whole rule, which `playedVoice == 0`
          // says exactly.
          //
          // s01_e014's last record is why this matters. It arms channel 0 with
          // clip 115 and plays it, and then, after the 0x13 speaker name, does
          // a stray `18 01` on a channel the scene never arms. Hardware drops
          // it. The port took the last 0x18 instead of the first, so it played
          // whatever channel 1 was left holding -- clip 2540 armed by
          // s14_e001's crab fight, three scenes earlier, because DAT_00356480
          // is global and survives a scene load on hardware too. Orphen said
          // "Where were you two?" over "Get to the deck! Fast!".
          if (playedVoice == 0 && at + 1 < end && blob[at + 1] < 3)
          {
            playedVoice = voiceCache_[blob[at + 1]];
          }
          break;

        case kControlChoice:
        {
          // FUN_00239848. The width table calls this a flat 4, which is only
          // the header: `[workIndex][initialSelection][optionCount]` is
          // followed by that many NUL-terminated option strings, and the
          // handler *renders* them rather than skipping them -- the same shape
          // 0x13's speaker name has. Walking them as ordinary bytes is what
          // glued "Yes" onto the end of the question and then read its NUL as
          // the end of the record, which is where "No" went.
          if (at + 4 > end)
          {
            at = end;
            continue;
          }
          choiceWorkIndex_ = blob[at + 1];
          const std::uint32_t count = blob[at + 3];
          at += 4;
          for (std::uint32_t option = 0; option < count && at < end; ++option)
          {
            std::string text;
            while (at < end && blob[at] != kControlReturn)
            {
              const std::uint8_t glyph = blob[at];
              if (glyph >= kFirstTextByte)
              {
                if (printable(glyph))
                {
                  text.push_back(static_cast<char>(glyph));
                }
                ++at;
                continue;
              }
              at += controlWidth(glyph);
            }
            ++at; // past the NUL
            options_.push_back(text);
          }
          continue;
        }

        case kControlSetFlag:
          // 0x1B sets an event flag, and the cutscene scheduler gates on those.
          // It runs where the *walk* reaches it -- DialogueWindow does that --
          // not here: s14_e004's "Who's there!?" sets 0x32B fifteen frames into
          // the record, ahead of its text, and the beat after it is gated on
          // the flag.
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
    if (!options_.empty())
    {
      // A menu waits for the player. FUN_00237fc0's choice block returns before
      // the glyph walk, so `window_.complete()` stays false until Cross and
      // `update` below holds the record with no hold of its own -- neither a
      // measured clip nor an invented timer.
      holdTicks_ = 0;
      ++choiceLines_;
    }
    else if (measured != 0)
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
      // Text with no clip behind it. The timer is invented and the report says
      // so; it is kept for the log only. It does not hold 0x1A -- see
      // voiceBusy() -- because the original's 0x1A waits on DAT_00356788,
      // which only a real voice raises. A battle scene voices its lines with
      // opcode 0xBD method 0x77 instead of 0x18, and that clip is the one the
      // record's 0x1A has to wait out.
      holdTicks_ = (kBaseHoldFrames + kFramesPerCharacter * static_cast<std::uint32_t>(line_.size())) *
                   orphen::ported::kNominalFrameTicks;
      ++estimatedLines_;
      holdIsEstimate_ = true;
    }
    active_ = true;

    // The rendering walk runs over the same bytes, on its own cursor, and its
    // 0x1A blocks on the clip this record just started.
    window_.FUN_00237b38_open(blob, recordBegin, static_cast<std::uint32_t>(windowEnd));
    window_.setVoiceBusy(voiceBusy());

    state.FUN_002663d8_clearEventFlag(kFlagWindowVisible);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleA);
    state.FUN_002663d8_clearEventFlag(kFlagTextIdleB);
    state.uGpffffb0f4_gateMask &= ~kGateMaskText;

    log_.push_back(LoggedLine{frame_, recordBegin, playedVoice,
                              holdTicks_ / orphen::ported::kNominalFrameTicks, measured != 0,
                              speaker_, line_, options_, choiceWorkIndex_});
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

  // LAB_00239AA0 executes 0x1B as the walk reaches it. The window records each
  // one it steps over and this spends them, once after every walk step and
  // again as a record ends. The two are the same frame in the original, which
  // sets the flag from inside FUN_00237FC0.
  //
  // This used to collect every 0x1B in the up-front scan and set them all when
  // the record closed. That is right for a flag at a record's tail, which is
  // every site s01_e012 has, and wrong for s14_e004's "Who's there!?", whose
  // 0x32B comes before the text: setting it at the close held the preamble 104
  // frames longer than hardware does.
  void DialogueStream::applyPendingFlags(orphen::ported::script::SceneScriptState &state)
  {
    for (std::uint32_t flagId : window_.takeWalkedFlags())
    {
      state.FUN_002663a0_setEventFlag(flagId);
    }
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
    window_.setVoiceBusy(voiceBusy());

    // **The record ends where its bytes say it ends**, not when the clip runs
    // out. The two usually coincide, because 0x1A is the last thing before the
    // terminator -- but a record can put codes after it, and s01_e012's does:
    // Dortin's line ends `1A 0C 3C 02`, a full second of held text after the
    // audio has stopped. Closing on the clip alone dropped that second.
    if (!window_.complete())
    {
      // A menu is not the typewriter running long; it is the record waiting for
      // an answer, and it can wait indefinitely.
      if (holdTicks_ == 0 && !window_.choiceActive())
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

  // FUN_00237fc0's glyph walk, plus the answer a choice hands back on the frame
  // Cross closed it. The original writes that straight into iGpffffb0f0 --
  // DAT_00355060, the script work array -- from inside the walk; the port hands
  // it out here because the window has no reference to the script state.
  void DialogueStream::FUN_00237fc0_update(std::uint32_t frameTicks,
                                           DialogueWindow::PadState pad,
                                           orphen::ported::script::SceneScriptState &state)
  {
    window_.FUN_00237fc0_update(frameTicks, pad);
    applyPendingFlags(state);
    if (const auto answer = window_.takeChoiceAnswer())
    {
      if (answer->workIndex < orphen::ported::script::SceneScriptState::kWorkWordCount)
      {
        state.DAT_00355060_work[answer->workIndex] = answer->value;
      }
    }
  }

  void DialogueStream::reset()
  {
    active_ = false;
    holdTicks_ = 0;
    frame_ = 0;
    speaker_.clear();
    line_.clear();
    options_.clear();
    choiceWorkIndex_ = 0;
    voiceCache_[0] = voiceCache_[1] = voiceCache_[2] = 0;
    log_.clear();
    measuredLines_ = 0;
    estimatedLines_ = 0;
    emptyLines_ = 0;
    choiceLines_ = 0;
    typewriterHeldLines_ = 0;
    typewriterHeldTicks_ = 0;
    heldByTypewriter_ = false;
    window_.reset();
  }

} // namespace orphen::ported::text
