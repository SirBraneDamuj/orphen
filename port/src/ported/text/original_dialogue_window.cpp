#include "ported/text/original_dialogue_window.h"

#include <algorithm>

namespace orphen::ported::text
{
  namespace
  {
    // FUN_00207938's screen mapping. Same pair original_dialogue_text.cpp uses;
    // a slot holds entry coordinates and only turns into screen ones on the way
    // out, because the scroll in FUN_00238f98 works on the entry y.
    constexpr int kScreenHalfWidth = 320;
    constexpr int kScreenHalfHeight = 224;

    // FUN_00237de8:12. Anything below this dispatches through PTR_FUN_0031c640.
    constexpr std::uint8_t kFirstGlyph = 0x1F;
    constexpr std::uint8_t kSpace = 0x20;

    // FUN_00239760's inner loop and FUN_00238a08's slot search both run
    // unbounded in the original, guarded only by data that is known good. The
    // port bounds them so a malformed record cannot hang the frame.
    constexpr int kMaxSpeakerSteps = 256;
  } // namespace

  std::size_t FUN_00237de8_controlWidth(std::uint8_t code)
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

  void DialogueWindow::FUN_00237b38_open(std::span<const std::uint8_t> blob,
                                         std::uint32_t begin,
                                         std::uint32_t end)
  {
    blob_ = blob;
    cursor_ = begin;
    end_ = std::min<std::size_t>(end, blob.size());
    open_ = true;
    complete_ = false;

    // FUN_00237b38:14-30, the block that only runs when the window was closed.
    // Between two cutscene records it always does: the scheduler terminates the
    // old stream before it starts the new one.
    originX_ = kWindowOriginX;
    originY_ = kWindowOriginY;
    defaultWait_ = 0;
    FUN_00238f18_clearSlots();
    color_ = kColorDefault;
    budget_ = 0;

    // The two wait counters are deliberately *not* cleared here -- the original
    // does not clear them either, so a 0x0C pause left over from the tail of a
    // record still holds off the first glyph of the next one.
    promptSlot_ = kNoSlot;
    promptTicks_ = 0;
    unhandledCodes_.clear();

    if (cursor_ >= end_)
    {
      complete_ = true;
    }
  }

  void DialogueWindow::FUN_00237b38_close()
  {
    open_ = false;
    complete_ = true;
    promptSlot_ = kNoSlot;
  }

  void DialogueWindow::reset()
  {
    const DialogueFont *font = font_;
    *this = DialogueWindow{};
    font_ = font; // the measured widths belong to the scene load, not the scene
  }

  void DialogueWindow::FUN_00237fc0_update(std::uint32_t frameTicks)
  {
    if (!open_)
    {
      return;
    }

    budget_ += frameTicks;

    // FUN_00237fc0:77-91. The prompt's animation counter runs whether or not a
    // prompt slot exists; the sprite only samples it.
    promptTicks_ += static_cast<std::int32_t>(frameTicks & 0xFFFF);
    if (promptTicks_ > kPromptCycleTicks)
    {
      promptTicks_ = 0;
    }
    if (promptSlot_ != kNoSlot && slots_[promptSlot_].active)
    {
      const int frame = (promptTicks_ / kPromptTicksPerFrame) % static_cast<int>(kPromptFrames.size());
      slots_[promptSlot_].u = kPromptFrames[static_cast<std::size_t>(frame)].u;
      slots_[promptSlot_].v = kPromptFrames[static_cast<std::size_t>(frame)].v;
    }

    // FUN_00237fc0:119-137. One step per 0x20 of budget; the two wait counters
    // consume a step each without advancing the walk, which is how 0x0C pauses.
    while (budget_ > kStepTicks - 1)
    {
      bool waiting = false;
      std::int32_t *counters[2] = {&waitA_, &waitB_};
      for (std::int32_t *counter : counters)
      {
        if (*counter != 0)
        {
          *counter -= static_cast<std::int32_t>(frameTicks);
          if (*counter < 1)
          {
            *counter = 0;
          }
          waiting = true;
        }
      }

      if (!waiting)
      {
        FUN_00237de8_advance();
        if (waitA_ == 0)
        {
          waitA_ = defaultWait_;
        }
      }
      budget_ -= kStepTicks;
    }
  }

  int DialogueWindow::glyphAdvance(std::uint8_t character) const
  {
    if (font_ == nullptr)
    {
      return 0;
    }
    return (static_cast<int>(font_->FUN_00238e50_width(character)) * kAdvancePercent) / 100;
  }

  void DialogueWindow::FUN_00237de8_advance()
  {
    if (complete_ || cursor_ >= end_)
    {
      complete_ = true;
      return;
    }

    if (blob_[cursor_] < kFirstGlyph)
    {
      dispatchControl(blob_[cursor_]);
      return;
    }

    // FUN_00237de8:17-56. The loop normally emits one glyph and returns on the
    // `1 < iVar6` test, since every printable character advances by more than
    // one unit; it only runs on through a space.
    int emitted = 0;
    for (;;)
    {
      const std::uint8_t character = blob_[cursor_];

      if (character == kSpace)
      {
        // Measure the word that follows and wrap before the space if it will
        // not fit in what is left of the line.
        int projected = 0;
        std::size_t scan = cursor_ + 1;
        if (scan < end_ && blob_[scan] > kFirstGlyph - 1 && blob_[scan] != kSpace)
        {
          while (scan < end_)
          {
            projected += glyphAdvance(blob_[scan]);
            ++scan;
            if (scan >= end_ || blob_[scan] < kFirstGlyph || blob_[scan] == kSpace)
            {
              break;
            }
          }
        }
        if (kLineWidth <= pen_ + projected)
        {
          FUN_00238f98_newLine();
          ++cursor_;
          return;
        }
      }

      ++cursor_;
      FUN_00238a08_enqueue(character);

      const int advance = glyphAdvance(character);
      pen_ = static_cast<std::int16_t>(pen_ + advance);
      if (kLineWidth <= pen_)
      {
        FUN_00238f98_newLine();
        return;
      }

      emitted += advance;
      if (character == kSpace || emitted > 1)
      {
        return;
      }
      if (cursor_ >= end_ || blob_[cursor_] < kSpace)
      {
        return;
      }
    }
  }

  void DialogueWindow::dispatchControl(std::uint8_t code)
  {
    switch (code)
    {
    case 0x00:
      // FUN_00239178. Its live branch raises flag 0x8FE and sets the 0x2000
      // gate bit -- "the record has finished" -- and leaves the cursor where it
      // is, so the handler re-runs harmlessly every frame after. DialogueStream
      // owns those two pieces of state; here it is just the end of the walk.
      complete_ = true;
      return;

    case 0x01:
    case 0x03:
    case 0x04:
    case 0x05:
      // FUN_002391d0. Raises the book prompt and does not advance the cursor;
      // the original then waits for Cross. See the header for why the port
      // spawns the sprite and finishes the record anyway.
      FUN_002391d0_prompt();
      complete_ = true;
      return;

    case 0x02:
      // LAB_00239328: FUN_00237b38(0), the stream's own terminator.
      complete_ = true;
      return;

    case 0x07:
      // FUN_00239368.
      FUN_00238f98_newLine();
      ++cursor_;
      return;

    case 0x0C:
      // LAB_00239428: the operand byte, scaled by 32, into iGpffffbcfc. At 0x20
      // ticks a step that is one frame per unit -- 0x3C is a one-second pause.
      if (cursor_ + 1 < end_)
      {
        waitB_ = static_cast<std::int32_t>(blob_[cursor_ + 1]) << 5;
      }
      cursor_ += 2;
      return;

    case 0x13:
      // FUN_00239760.
      FUN_00239760_speaker();
      return;

    // The audio codes. DialogueStream ran all four when it scanned the record;
    // stepping over them keeps the two walks on the same bytes.
    case 0x16:
      cursor_ += 7;
      return;
    case 0x17:
      cursor_ += 1;
      return;
    case 0x18:
      cursor_ += 2;
      return;
    case 0x19:
      cursor_ += 3;
      return;
    case 0x1A:
      // LAB_00239a70: advance only once DAT_00356788 has fallen back to zero,
      // i.e. once the clip has played out. This is what holds a record open,
      // and what makes the codes *after* it -- the tail 0x0C pauses -- run at
      // the end of the line rather than in parallel with it.
      if (!voiceBusy_)
      {
        cursor_ += 1;
      }
      return;

    // 0x1B sets an event flag and 0x1C is the same handler failing its own
    // opcode test. DialogueStream applies the sets when the record closes.
    case 0x1B:
    case 0x1C:
      cursor_ += 3;
      return;

    default:
      noteUnhandled(code);
      cursor_ += FUN_00237de8_controlWidth(code);
      return;
    }
  }

  void DialogueWindow::noteUnhandled(std::uint8_t code)
  {
    if (std::find(unhandledCodes_.begin(), unhandledCodes_.end(), code) == unhandledCodes_.end())
    {
      unhandledCodes_.push_back(code);
    }
  }

  void DialogueWindow::FUN_00239760_speaker()
  {
    // FUN_00239760. The name is not parsed -- it is walked as ordinary glyphs
    // by a recursive FUN_00237de8 in a tight loop, so the whole name lands in
    // one frame, and a 0x00 ends it.
    ++cursor_;

    const std::uint32_t saved = color_;
    FUN_00238f18_clearSlots();
    color_ = kColorSpeaker;

    for (int step = 0; step < kMaxSpeakerSteps; ++step)
    {
      if (cursor_ >= end_ || blob_[cursor_] == 0x00)
      {
        break;
      }
      const std::size_t before = cursor_;
      FUN_00237de8_advance();
      if (cursor_ == before)
      {
        break; // a blocking code inside a name; nothing advances, so stop
      }
    }

    ++cursor_; // past the 0x00
    // iGpffffbcd0: from here on every glyph on a row below the name is indented.
    speakerDrawn_ = true;
    color_ = saved;
    complete_ = false; // a 0x00 inside the name ends the name, not the record
    FUN_00238f98_newLine();
  }

  std::size_t DialogueWindow::findFreeSlot()
  {
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
      if (!slots_[index].active)
      {
        return index;
      }
    }
    return kNoSlot; // FUN_00238a08:20 reports and gives up
  }

  void DialogueWindow::FUN_00238a08_enqueue(std::uint8_t character)
  {
    if (font_ == nullptr || character < kFirstCharacter)
    {
      return;
    }
    const std::size_t index = findFreeSlot();
    if (index == kNoSlot)
    {
      return;
    }
    GlyphSlot *slot = &slots_[index];

    slot->active = true;
    slot->pen = pen_;
    slot->line = line_;
    if (speakerDrawn_ && line_ != 0)
    {
      slot->pen = static_cast<std::int16_t>(pen_ + 10);
    }

    slot->textureSlot = kFontSlotLow;
    slot->x = originX_ + slot->pen + kGlyphOriginBias;
    slot->y = originY_ + slot->line * -kCellSize;
    if (movieMode_ > 0)
    {
      if (originY_ == 0xD0)
      {
        slot->y -= 0x2D;
      }
      else if (originY_ == kWindowOriginY)
      {
        slot->y += 0x1E;
      }
    }

    slot->layer = layer_;
    slot->color = color_;

    const int cell = character - kFirstCharacter;
    int v = (cell / kColumns) * kCellSize;
    slot->u = (cell % kColumns) * kCellSize;
    if (v > kLowSheetLastV)
    {
      slot->textureSlot += 1;
      v = (v + 14) % 256;
    }
    slot->v = v;

    const int width = font_->FUN_00238e50_width(character);
    slot->width = (width * kAdvancePercent) / 100;
    slot->height = kDrawnCellHeight;
    slot->sourceWidth = width;
    slot->sourceHeight = kCellSize;
  }

  void DialogueWindow::FUN_00238f98_newLine()
  {
    pen_ = 0;

    if (line_ < kMaxLineIndex)
    {
      ++line_;
      return;
    }

    // The window is full: every glyph below row 0 moves up one row, and the one
    // that was on row 1 is retired. Row 0 -- the speaker -- is left alone.
    for (GlyphSlot &slot : slots_)
    {
      if (!slot.active || slot.layer != layer_ || slot.line == 0)
      {
        continue;
      }
      if (slot.line == 1)
      {
        slot.active = false;
      }
      else
      {
        slot.y += kCellSize;
      }
      --slot.line;
    }
  }

  void DialogueWindow::FUN_00238f18_clearSlots()
  {
    // FUN_00238f18(0): the selector is negative, so every slot is cleared
    // regardless of layer.
    for (GlyphSlot &slot : slots_)
    {
      slot.active = false;
    }
    pen_ = 0;
    line_ = 0;
    speakerDrawn_ = false;
    promptSlot_ = kNoSlot;
  }

  void DialogueWindow::FUN_002391d0_prompt()
  {
    const std::size_t index = findFreeSlot();
    if (index == kNoSlot)
    {
      return;
    }
    GlyphSlot *slot = &slots_[index];

    // FUN_002391d0:22-60, with the 0x509/0x50A gate dropped.
    slot->active = true;
    slot->pen = pen_;
    slot->line = line_;
    if (speakerDrawn_ && line_ != 0)
    {
      slot->pen = static_cast<std::int16_t>(pen_ + kPromptDrawWidth);
    }
    slot->x = originX_ + slot->pen + kPromptOriginBias;
    slot->y = originY_ + slot->line * -kCellSize;
    if (movieMode_ > 0)
    {
      if (originY_ == 0xD0)
      {
        slot->y -= 0x2D;
      }
      else if (originY_ == kWindowOriginY)
      {
        slot->y += 0x1E;
      }
    }
    slot->layer = layer_;
    slot->textureSlot = kPromptSlot;
    slot->u = kPromptFrames[0].u;
    slot->v = kPromptFrames[0].v;
    slot->width = kPromptDrawWidth;
    slot->height = kPromptDrawHeight;
    slot->sourceWidth = kPromptSourceSize;
    slot->sourceHeight = kPromptSourceSize;
    // :57-63. The tinted colour is for the "more text follows" prompt; a stream
    // sitting on a 0x01 gets the plain one.
    slot->color = blob_[cursor_] != 0x01 ? 0x80608060u : kColorDefault;
    promptSlot_ = index;
  }

  std::vector<DialogueSprite> DialogueWindow::sprites() const
  {
    std::vector<DialogueSprite> out;
    if (!open_)
    {
      return out;
    }

    // FUN_00237fc0:29-40 walks layer 3 down to 0, so a lower layer draws last
    // and therefore on top.
    for (int layer = 3; layer >= 0; --layer)
    {
      for (const GlyphSlot &slot : slots_)
      {
        if (!slot.active || slot.layer != layer)
        {
          continue;
        }
        DialogueSprite sprite;
        sprite.textureSlot = slot.textureSlot;
        sprite.x = slot.x + kScreenHalfWidth;
        sprite.y = kScreenHalfHeight - slot.y;
        sprite.width = slot.width;
        sprite.height = slot.height;
        sprite.u = slot.u;
        sprite.v = slot.v;
        sprite.sourceWidth = slot.sourceWidth;
        sprite.sourceHeight = slot.sourceHeight;
        sprite.color = slot.color;
        out.push_back(sprite);
      }
    }
    return out;
  }

} // namespace orphen::ported::text
