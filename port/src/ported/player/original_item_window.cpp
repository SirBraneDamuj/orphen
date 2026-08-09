#include "ported/player/original_item_window.h"

namespace orphen::ported::player
{
  namespace
  {
    // FUN_00237fc0's inner loop drains the accumulator 0x20 at a time and
    // calls FUN_00237de8 once per drain, so one nominal frame is one step of
    // the stream. FUN_00237de8 then emits characters until the pen has moved
    // more than one unit, which for a caption is one character a frame.
    constexpr std::uint32_t kTicksPerCharacter = 0x20;

    // The control codes the chest's two streams use, from the handler table at
    // 0x0031C640. Everything below 0x1F is a code; 0x1F and up is a glyph.
    constexpr std::uint8_t kCodeEnd = 0x01;        // FUN_002391d0, then close
    constexpr std::uint8_t kCodeNewLine = 0x07;    // FUN_00239368
    constexpr std::uint8_t kCodeItemName = 0x14;   // FUN_002397f0
    constexpr std::uint8_t kCodeSetFlag = 0x1B;    // FUN_00239aa0
    constexpr std::uint8_t kCodeClearFlag = 0x1C;  // the same handler's tail
    constexpr std::uint8_t kFirstGlyph = 0x1F;
  } // namespace

  void ItemWindow::FUN_00237b38_open(std::size_t messageIndex,
                                     std::int32_t itemId,
                                     const orphen::ported::resource::ItemDatabase &items)
  {
    lines_.clear();
    characters_ = 0;
    revealed_ = 0;
    iGpffffbce8_accumulator_ = 0;
    promptTicks_ = 0;
    unhandledCode_ = 0;
    open_ = true;

    // FUN_00238f18 clears the glyph list and the line index on open.
    int line = 0;
    const auto append = [&](const std::string &text) {
      if (text.empty())
      {
        return;
      }
      if (lines_.empty() || lines_.back().index != line)
      {
        lines_.push_back({line, {}});
      }
      lines_.back().text += text;
      characters_ += text.size();
    };

    const std::span<const std::uint8_t> stream = items.FUN_0025b9e8_message(messageIndex);
    std::string run;
    for (std::size_t at = 0; at < stream.size();)
    {
      const std::uint8_t code = stream[at];
      if (code >= kFirstGlyph)
      {
        run.push_back(static_cast<char>(code));
        ++at;
        continue;
      }

      append(run);
      run.clear();

      if (code == kCodeEnd)
      {
        break;
      }
      if (code == kCodeNewLine)
      {
        ++line;
        ++at;
        continue;
      }
      if (code == kCodeSetFlag || code == kCodeClearFlag)
      {
        // Two operand bytes, a little-endian event flag id. 0x0509 here, the
        // flag FUN_002391d0 tests before it draws.
        at += 3;
        continue;
      }
      if (code == kCodeItemName)
      {
        // FUN_002397f0 repoints the cursor at the item's name and prints it
        // inline. The operand is the id state 0x0F wrote in through
        // FUN_00237ca0, which the port passes here instead.
        if (itemId >= 0)
        {
          std::string name = items.FUN_00229688_name(itemId);
          if (name.empty())
          {
            name = "ITEM " + std::to_string(itemId);
          }
          append(name);
        }
        at += 2;
        continue;
      }

      // A code this reader does not know. Stopping is the honest thing: the
      // rest of the stream is positional state we would be guessing at.
      unhandledCode_ = code;
      break;
    }
    append(run);
  }

  std::string ItemWindow::text() const
  {
    std::string joined;
    for (const Line &line : lines_)
    {
      if (!joined.empty())
      {
        joined += " / ";
      }
      joined += line.text;
    }
    return joined;
  }

  void ItemWindow::FUN_00237fc0_update(std::uint32_t frameTicks, bool confirmPressed)
  {
    if (!open_)
    {
      return;
    }

    if (revealed_ < characters_)
    {
      iGpffffbce8_accumulator_ += frameTicks;
      while (iGpffffbce8_accumulator_ >= kTicksPerCharacter && revealed_ < characters_)
      {
        iGpffffbce8_accumulator_ -= kTicksPerCharacter;
        ++revealed_;
      }
      // FUN_00237fc0 only reaches its Cross test once the stream has stopped,
      // but a press while it is still typing skips to the end -- which is what
      // the original's own accumulator does when a caption is short.
      if (confirmPressed)
      {
        revealed_ = characters_;
      }
      return;
    }

    promptTicks_ += frameTicks;
    if (confirmPressed)
    {
      // Control code 0x01: pcGpffffaec0 = 0.
      close();
    }
  }

  void ItemWindow::close()
  {
    open_ = false;
    revealed_ = 0;
    promptTicks_ = 0;
  }

} // namespace orphen::ported::player
