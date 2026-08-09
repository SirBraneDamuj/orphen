#include "ported/debug/original_debug_text.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace orphen::ported::debug
{
  namespace
  {

    // FUN_002685e8: the length of the rest of the buffer from here. The '~'
    // escape right-aligns on it, and it counts everything left in the buffer
    // rather than the token that follows -- so "~MP0124" followed by another
    // line is aligned on both. That is the original's behaviour, not a
    // simplification: FUN_00268270 hands it the raw buffer pointer.
    int FUN_002685e8_strlen(const char *text)
    {
      int length = 0;
      while (*text != '\0')
      {
        ++length;
        ++text;
      }
      return length;
    }

  } // namespace

  GlyphWindow FUN_00268410_glyphWindow(char character)
  {
    // The original's `movn` picks c - 0x20 when that is non-negative and
    // c - 1 otherwise; only the first case can reach here.
    const int cell = static_cast<int>(static_cast<unsigned char>(character)) - 0x20;

    GlyphWindow window;
    window.u = ((cell & 0x1f) << 3) | text::kWindowInsetU;
    window.v = (cell >> 5) * text::kAtlasCellHeight + text::kWindowInsetV;
    return window;
  }

  void DebugTextBuffer::FUN_002681c0_printf(const char *format, ...)
  {
    if (!DAT_003555dc_outputEnabled_)
    {
      return;
    }
    if (!DAT_003555da_debugActive_)
    {
      // The original latches the gate off rather than just dropping the text.
      DAT_003555dc_outputEnabled_ = false;
      return;
    }

    std::array<char, text::kFormatScratchSize> scratch{};
    std::va_list arguments;
    va_start(arguments, format);
    const int formatted = std::vsnprintf(scratch.data(), scratch.size(), format, arguments);
    va_end(arguments);

    if (formatted < 0)
    {
      return;
    }

    // FUN_0030e0f8 returns the character count and FUN_002681c0 tests the
    // running total against 0x800 *before* appending, dropping the whole
    // append when it would not fit.
    const std::size_t length = static_cast<std::size_t>(formatted);
    if (length >= scratch.size() || DAT_003551dc_length_ + length >= text::kBufferSize)
    {
      return;
    }

    std::memcpy(DAT_00572c38_text_.data() + DAT_003551dc_length_, scratch.data(), length + 1);
    DAT_003551dc_length_ += length;
  }

  std::vector<DebugGlyph> DebugTextBuffer::FUN_00268270_layoutAndDrain()
  {
    std::vector<DebugGlyph> glyphs;
    if (DAT_003551dc_length_ == 0)
    {
      return glyphs;
    }

    DAT_00572c38_text_[DAT_003551dc_length_] = '\0';

    int x = text::kLeftMargin;
    int y = text::kFirstLine;

    const char *cursor = DAT_00572c38_text_.data();
    unsigned int character = static_cast<unsigned char>(*cursor);
    ++cursor;

    while (character != 0)
    {
      if (character == 0x0d || character == 0x0a)
      {
        x = text::kLeftMargin;
        y -= text::kLineStep;
      }
      else if (character == 0x7e) // '~'
      {
        // Jump to the escape line and right-align what is left.
        y = text::kEscapeLine;
        x = FUN_002685e8_strlen(cursor) * -text::kAdvance + text::kEscapeRight;
      }
      else if (character - 0x20u < 0x60u)
      {
        if (character != 0x20)
        {
          // FUN_00268410(character, x, y), whose sprite's top-left is
          // (320 + x, 224 - y) on the 640x448 virtual screen.
          glyphs.push_back(DebugGlyph{static_cast<char>(character),
                                      text::kScreenWidth / 2 + x,
                                      text::kScreenHeight / 2 - y});
        }
        x += text::kAdvance;
        if (x > text::kRightLimit)
        {
          x = text::kLeftMargin;
          y -= text::kLineStep;
        }
      }
      // Anything outside 0x20..0x7F is skipped without advancing.

      character = static_cast<unsigned char>(*cursor);
      ++cursor;
    }

    DAT_003551dc_length_ = 0;
    return glyphs;
  }

} // namespace orphen::ported::debug
