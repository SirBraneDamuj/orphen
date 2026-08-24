#pragma once

// The dialogue system's glyph slot array and the stream walk that fills it --
// the part that puts a cutscene's subtitles on the screen.
//
//   src/FUN_00237de8.c  the per-frame stream walk: control dispatch, word wrap
//   src/FUN_00238a08.c  one glyph -> one slot of the 300-entry array
//   src/FUN_00238f98.c  end of line: step the row, or scroll the window
//   src/FUN_00238f18.c  clear the array, reset the pen and the row
//   src/FUN_00239760.c  control code 0x13, the speaker's name
//   src/FUN_00237fc0.c  the budget loop that paces the walk, and the prompt
//   src/FUN_00239020.c  one slot -> one GS sprite
//
// == Why the US release shows no subtitles ==
//
// `FUN_00238a08` opens with a gate, and so does the prompt builder
// `FUN_002391d0`:
//
//   lVar1 = FUN_00266368(0x509);
//   if ((lVar1 == 0) && (lVar1 = FUN_00266368(0x50a), lVar1 == 0)) return;
//
// -- no glyph is enqueued unless event flag 0x509 or 0x50A is set. Nothing in
// the executable sets 0x50A. `FUN_00237b38` *clears* 0x509 on every start, and
// the only code that sets it is `FUN_002452f0`, an unrelated full-screen
// caption. So a stream is visible only if it turns the flag back on itself,
// with control code `1B 09 05` -- which is exactly how the chest windows in
// `SCR.BIN` resource 1 begin.
//
// The 84 cutscene records in `scr2.out` do not. Their six `0x1B` codes set
// 0x6A, 0x79 and 0x6E, all scheduler gates; not one sets 0x509. The Japanese
// build has no gate at all -- its glyph enqueue, `src-jp/FUN_0023ade8.c`, is
// otherwise the same function with those four lines absent -- so the subtitles
// were removed for the US release by adding this test, not by editing the
// scripts.
//
// **The port drops the gate**, which is a deliberate divergence and the only
// one in this file. Everything downstream of it is the original's.
//
// == The two divergences the port's structure forces ==
//
// `DialogueStream` owns a record's audio and its hold: it pre-scans for the
// 0x16/0x18 pair, reads the clip length out of `VOICE.BIN`, and closes the
// record when that runs out. This walk therefore *skips* the audio codes by
// their operand width rather than running them, and 0x1A -- "block until the
// clip has played out" -- advances instead of blocking, because the hold it
// would be waiting on is already being counted elsewhere. Both walks are over
// the same bytes at the same time and neither can desync the other.
//
// `0x01` raises the book prompt and, in the original, holds the record until
// Cross. 28 of the 84 records use it. The port still spawns the sprite, but the
// record closes on its clip: a cutscene that stopped for input at every fourth
// line would not be the same scene, and the port has no such input model.
//
// == Layout ==
//
// `FUN_00237b38` opens a window at entry (-0x130, -0x78) -- screen (16, 344) --
// 600 units wide and `uGpffffbce0 = 2` rows deep, so rows 0..2 with the speaker
// on row 0 and the line on rows 1 and 2. Past row 2 `FUN_00238f98` scrolls:
// every slot's row index drops by one, a slot leaving row 1 is retired, and the
// rest move 22 units up the screen. Row 0 is never touched, which is what keeps
// the name in place while the line under it scrolls.
//
// Wrapping is a word lookahead, not a break at the margin: at a space
// `FUN_00237de8` measures the run up to the next space or control code and
// wraps first if it would not fit in what is left of the 600.

#include "ported/text/original_dialogue_text.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::text
{

  // The advance each control code applies to the cursor, opcode byte included,
  // read off each handler's own stores to -0x5140(gp) rather than inferred from
  // the data. Where a handler stores more than once the largest wins, since the
  // smaller stores are the partial advances on the way there.
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
  // Two entries could not be read off a single straight-line pass and are set
  // to 1, the safe value: a width that is too small re-reads a payload byte as
  // a control code, which garbles the printed line, while a width that is too
  // large can step over a 0x16 or a 0x18 and lose a clip. Both 0x01 and 0x09
  // only ever appear at a record's tail in s01_e012's data, after the 0x1A, so
  // neither can affect a hold.
  std::size_t FUN_00237de8_controlWidth(std::uint8_t code);

  // One entry of the array at iGpffffaed4: 300 of them, stride 0x3C. Only the
  // fields FUN_00239020 reads plus the two the scroll needs are kept; the
  // original's int indices are in the comments.
  struct GlyphSlot
  {
    bool active = false;              // +0x3A
    std::uint8_t layer = 0;           // +0x3B, cGpffffaec4 at the time
    int textureSlot = kFontSlotLow;   // [0x00]
    int x = 0;                        // [2]
    int y = 0;                        // [3], negated by FUN_00239020
    int width = 0;                    // [4]
    int height = 0;                   // [5]
    int u = 0;                        // [6]
    int v = 0;                        // [7]
    int sourceWidth = 0;              // [8]
    int sourceHeight = 0;             // [9]
    std::uint32_t color = kColorDefault; // [0xC]
    std::int16_t pen = 0;             // +0x36, sGpffffbcdc when it was placed
    std::int16_t line = 0;            // +0x34, sGpffffbcd8 -- the scroll counter
  };

  class DialogueWindow
  {
  public:
    // iGpffffaed4's capacity. FUN_00238a08 gives up and reports once it has
    // walked all of them.
    static constexpr std::size_t kSlotCount = 300;

    // FUN_00237b38:16-17 and the width at :18.
    static constexpr int kMaxLineIndex = 2;   // uGpffffbce0
    static constexpr int kLineWidth = 600;    // uGpffffbce4

    // FUN_00237fc0:120. The budget the walk is paced against, deducted once per
    // glyph step; at 60 fps DAT_003555bc is also 0x20, so exactly one step runs
    // per frame and the text types at one character a frame.
    static constexpr std::uint32_t kStepTicks = 0x20;

    void setFont(const DialogueFont *font) { font_ = font; }

    // DAT_00356788, which FUN_00206a90 returns and control code 0x1A blocks on.
    // The port has no streaming voice to poll, so `DialogueStream` publishes
    // what is left of the clip's length here instead.
    void setVoiceBusy(bool busy) { voiceBusy_ = busy; }

    // FUN_00237b38 with a non-zero pointer, taking the branch where the window
    // was closed. `end` bounds the record -- the next dialogue pointer-table
    // entry -- and stands in for the terminator the original trusts.
    void FUN_00237b38_open(std::span<const std::uint8_t> blob, std::uint32_t begin, std::uint32_t end);

    // The end of a record's walk -- FUN_00239178's outermost branch, control
    // code 0x00. It stops the walk and nothing else: the original leaves
    // pcGpffffaec0 pointing at the terminator, so the *window* is still up and
    // every glyph on it stays drawn.
    void FUN_00239178_end_record();

    // FUN_00237b38(0) while the window is up, which is what an explicit script
    // terminate and FUN_00237fc0's Cross press on a 0x01 prompt both are. The
    // pointer goes to zero, so nothing is drawn any more -- but the slot array
    // is left exactly as it was, because FUN_00237b38's clear is behind its
    // window-was-already-closed test and the confirm branch never calls it.
    void FUN_00237b38_close();

    // LAB_00239328, control code 0x02: `pcGpffffaec0 = 0` **then**
    // FUN_00237b38(0). Nulling the pointer first is what puts that test the
    // other way round, so this is the one close that also wipes the slots.
    // s01_e012 reaches it through records whose body ends `02 00`, and through
    // four whose entire body is a bare 0x02 -- one between each group of lines
    // by the same speaker.
    void LAB_00239328_close();

    // pcGpffffaec0 != 0, which is FUN_00237c60's whole test. Distinct from
    // `open()`: a record that has finished leaves the window up.
    bool windowUp() const { return pcGpffffaec0_windowUp_; }

    // FUN_00237fc0's tail: age the budget, burn the two wait counters, and step
    // the walk once per 0x20 of budget.
    void FUN_00237fc0_update(std::uint32_t frameTicks);

    // The walk has reached a 0x00 or a 0x02 -- everything the record had to say
    // is on the screen. A record whose clip is shorter than its text is still
    // typing when its hold runs out, and `DialogueStream` waits for this.
    bool complete() const { return complete_; }
    bool open() const { return open_; }

    // FUN_00237fc0's layer walk, 3 down to 0, as flat sprites.
    std::vector<DialogueSprite> sprites() const;

    void reset();

    // iGpffffb0e4, which lives on the `Letterbox` the script and the renderer
    // share. Published every frame rather than latched, because the nudge is a
    // live read in FUN_00238a08 -- a glyph enqueued while the bars are up moves,
    // one enqueued after they come down does not, and the two can belong to the
    // same line.
    void setMovieMode(int mode) { movieMode_ = mode; }

    // Control codes the walk skipped by width without acting on them, for the
    // report. The audio codes are excluded -- DialogueStream runs those.
    const std::vector<std::uint8_t> &unhandledCodes() const { return unhandledCodes_; }

  private:
    void FUN_00237de8_advance();          // one step of the walk
    void dispatchControl(std::uint8_t code);
    void FUN_00238a08_enqueue(std::uint8_t character);
    void FUN_00238f98_newLine();
    void FUN_00238f18_clearSlots();
    void FUN_00239760_speaker();
    void FUN_002391d0_prompt();
    std::size_t findFreeSlot(); // kNoSlot when all 300 are taken
    int glyphAdvance(std::uint8_t character) const;
    void noteUnhandled(std::uint8_t code);

    const DialogueFont *font_ = nullptr;

    std::span<const std::uint8_t> blob_;
    std::size_t cursor_ = 0;   // pcGpffffaec0
    std::size_t end_ = 0;
    bool open_ = false;
    bool complete_ = false;
    // pcGpffffaec0 != 0 -- whether there is a window on screen at all, as
    // opposed to whether a record is being walked. FUN_00237b38 only resets the
    // window when this was false on the way in, which is how a record with no
    // 0x13 keeps the previous record's speaker name.
    bool pcGpffffaec0_windowUp_ = false;

    std::array<GlyphSlot, kSlotCount> slots_{};

    int originX_ = kWindowOriginX;   // uGpffffbcc8
    int originY_ = kWindowOriginY;   // uGpffffbccc
    std::int16_t pen_ = 0;           // sGpffffbcdc
    std::int16_t line_ = 0;          // sGpffffbcd8
    std::uint32_t color_ = kColorDefault; // uGpffffbcd4
    std::uint8_t layer_ = 0;         // cGpffffaec4
    // iGpffffbcd0, set by the speaker handler. Once a name has been drawn every
    // glyph on a row below it is indented ten units.
    bool speakerDrawn_ = false;
    // iGpffffb0e4, the mode opcode 0x6D raises alongside the cinematic bars.
    // While it is positive FUN_00238a08:36-45 moves a glyph clear of whichever
    // bar its window would run into: the bottom window (origin y -0x78) rises
    // 0x1E so its third line ends at 380, eight units above the 388 the bottom
    // bar starts at, and the top window (origin y 0xD0) drops 0x2D for the same
    // reason at the other edge. Without it s01_e012's subtitles are drawn
    // underneath the bar.
    int movieMode_ = 0;

    std::uint32_t budget_ = 0;   // iGpffffbce8
    std::int32_t waitA_ = 0;     // iGpffffbcf8
    std::int32_t waitB_ = 0;     // iGpffffbcfc, what control code 0x0C sets
    std::int32_t defaultWait_ = 0; // uGpffffaec8, zeroed when a window opens

    bool voiceBusy_ = false;

    // FUN_00237fc0:77-95. uGpffffaecc, the prompt's four-frame cycle, and
    // puGpffffbcf0, the slot it steps. An index rather than the original's
    // pointer so the window stays copyable.
    static constexpr std::size_t kNoSlot = kSlotCount;
    std::int32_t promptTicks_ = 0;
    std::size_t promptSlot_ = kNoSlot;

    std::vector<std::uint8_t> unhandledCodes_;
  };

} // namespace orphen::ported::text
