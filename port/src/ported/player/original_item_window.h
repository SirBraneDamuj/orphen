#pragma once

// The chest cutscene's two text windows: the item-get line, and "The chest is
// empty."
//
//   src/FUN_00237b38.c  open: point pcGpffffaec0 at a text stream and reset
//                       the layout state
//   src/FUN_00237fc0.c  the per-frame driver -- typewriter cadence, the
//                       prompt cursor, and the wait for uGpffffb68a & 0x40
//   src/FUN_00237de8.c  the stream interpreter: bytes below 0x1F dispatch
//                       through PTR_FUN_0031c640, everything else is a glyph
//   src/FUN_00237c60.c  "is the window still up" -- pcGpffffaec0 != 0
//   src/FUN_0025b9e8.c  message index -> stream, out of SCR.BIN resource 1
//   src/FUN_002397f0.c  control code 0x14, which splices in the name of the
//                       item whose id is the next byte
//
// == The two streams ==
//
// FUN_00254f60 picks between them on whether the chest had anything in it:
//
//   index 0   1B 09 05 | 14 <id> | 01     the item-get line
//   index 1   1B 09 05 | 07 | "The chest is empty." | 01
//
// so both are three control codes, a body, and a terminator:
//
//   0x1B  set the event flag in the next two bytes -- 0x0509 here, which is
//         what FUN_002391d0 tests before it will draw anything
//   0x07  FUN_00238f98, a new line
//   0x14  splice in an item name, one operand byte. FUN_00237ca0 finds this
//         code and state 0x0F writes the chest's id into its operand
//   0x01  raise the prompt, wait for Cross, close
//
// == What this is and is not ==
//
// The original's caption is one path through the *general* dialogue system: a
// bytecode stream with 31 control codes, a 300-entry glyph sprite list drawn
// in four priority layers, word wrap and a typewriter metered against a tick
// accumulator.
//
// This reads the real streams and expands the codes those two use, but it is
// not the dialogue system: the other 27 codes stop the reader and are reported
// through `unhandledCode()`, there is no wrapping against FUN_00237b38's
// 600-unit width, and the glyph list is rebuilt from scratch each frame rather
// than accumulated. A real dialogue port should replace this file.
//
// The prompt after the text -- the small book that flips while the game waits
// for Cross -- is a sprite rather than a character, so it is not in `lines()`.
// FUN_00237fc0:77-107 animates it out of the four-cell table at 0x0031C630;
// `promptTicks()` is that timer and ported/text/original_dialogue_text.h turns
// it into the sprite.

#include "ported/resource/item_database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orphen::ported::player
{

  class ItemWindow
  {
  public:
    // FUN_00237b38(FUN_0025b9e8(messageIndex)). `itemId` is what state 0x0F
    // patches into control code 0x14's operand; pass -1 for a stream that has
    // no 0x14, which is what the empty-chest line is.
    void FUN_00237b38_open(std::size_t messageIndex,
                           std::int32_t itemId,
                           const orphen::ported::resource::ItemDatabase &items);

    // FUN_00237c60: pcGpffffaec0 != 0.
    bool FUN_00237c60_isOpen() const { return open_; }

    // FUN_00237fc0. `confirmPressed` is uGpffffb68a & 0x40 -- Cross.
    void FUN_00237fc0_update(std::uint32_t frameTicks, bool confirmPressed);

    void close();

    // One entry per line the stream opened, in order. iGpffffbcd8 starts at 0
    // -- FUN_00238f18 clears it when the window opens -- and 0x07 steps it, so
    // a stream that leads with 0x07 puts its text on the second row.
    struct Line
    {
      int index = 0; // iGpffffbcd8 when the text was emitted
      std::string text;
    };
    const std::vector<Line> &lines() const { return lines_; }
    // The whole caption as one string, for logging.
    std::string text() const;
    std::size_t revealedCharacters() const { return revealed_; }
    std::size_t characterCount() const { return characters_; }
    bool fullyRevealed() const { return revealed_ >= characters_; }
    // iGpffffbcec >= 0: the caption is complete and the prompt is animating.
    bool awaitingConfirm() const { return open_ && fullyRevealed(); }
    // A phase for the prompt, in ticks since the caption completed.
    std::uint32_t promptTicks() const { return promptTicks_; }
    // Non-zero when the reader stopped on a control code it does not know.
    std::uint8_t unhandledCode() const { return unhandledCode_; }

  private:
    bool open_ = false;
    std::vector<Line> lines_;
    std::size_t characters_ = 0;
    std::size_t revealed_ = 0;
    // iGpffffbce8, the tick accumulator FUN_00237fc0 drains 0x20 at a time.
    std::uint32_t iGpffffbce8_accumulator_ = 0;
    std::uint32_t promptTicks_ = 0;
    std::uint8_t unhandledCode_ = 0;
  };

} // namespace orphen::ported::player
