#include "ported/text/original_dialogue_text.h"

namespace orphen::ported::text
{
  namespace
  {
    // FUN_00207938's screen mapping, from the header.
    constexpr int kScreenHalfWidth = 320;
    constexpr int kScreenHalfHeight = 224;

    int screenX(int entryX) { return entryX + kScreenHalfWidth; }
    int screenY(int entryY) { return kScreenHalfHeight - entryY; }

    // FUN_00238c90:56. A texel counts as ink when its palette alpha clears 100
    // on the 0..0x80 scale, which the decoder has already doubled into 0..0xFF.
    // The original also requires the palette index to be below 0x10; on both
    // font sheets every entry above that is transparent anyway, so the alpha
    // test alone reproduces the shipped table exactly.
    constexpr std::uint8_t kInkAlpha = 200;
  } // namespace

  void DialogueFont::FUN_00238c90_measure(int bank,
                                          const orphen::ported::resource::BmpaTexture &sheet)
  {
    if (sheet.rgbaPixels.size() < static_cast<std::size_t>(sheet.width) * sheet.height * 4)
    {
      return;
    }

    // The bank offsets the table by one sheet's worth of characters, which is
    // 0x79 -- eleven rows of eleven cells.
    const int base = kFirstCharacter + bank * (kColumns * kColumns);

    for (int row = 0; row < kColumns; ++row)
    {
      for (int column = 0; column < kColumns; ++column)
      {
        int rightmost = 0;
        for (int y = 0; y < kCellSize; ++y)
        {
          const int texelY = row * kCellSize + y;
          if (texelY >= sheet.height)
          {
            break;
          }
          for (int x = kCellSize - 1; x > rightmost; --x)
          {
            const int texelX = column * kCellSize + x;
            if (texelX >= sheet.width)
            {
              continue;
            }
            const std::size_t at =
                (static_cast<std::size_t>(texelY) * sheet.width + texelX) * 4 + 3;
            if (sheet.rgbaPixels[at] > kInkAlpha)
            {
              rightmost = x;
              break;
            }
          }
        }

        const int character = base + row * kColumns + column;
        if (character < 0 || character > 0xFF)
        {
          continue;
        }
        // FUN_00238c90:64-69: an empty cell is a space, six units wide.
        width_[static_cast<std::size_t>(character)] =
            static_cast<std::uint8_t>(rightmost == 0 ? 6 : rightmost + 2);
      }
    }

    measured_ = true;
  }

  int DialogueFont::advance(unsigned char character) const
  {
    return (static_cast<int>(width_[character]) * kAdvancePercent) / 100;
  }

  std::vector<DialogueSprite> FUN_00238a08_layoutLine(std::string_view line,
                                                      const DialogueFont &font,
                                                      int originX,
                                                      int originY,
                                                      int &penX)
  {
    std::vector<DialogueSprite> sprites;
    sprites.reserve(line.size());

    for (const char raw : line)
    {
      const auto character = static_cast<unsigned char>(raw);
      if (character < kFirstCharacter)
      {
        continue; // a control code; the port's caller has none
      }

      const int index = character - kFirstCharacter;
      int v = (index / kColumns) * kCellSize;
      DialogueSprite sprite;
      sprite.textureSlot = kFontSlotLow;
      sprite.u = (index % kColumns) * kCellSize;

      // FUN_00238a08:52-60. Past the bottom of the low sheet the cell continues
      // at the top of the high one; the original's `+ 0xE` and modulo dance is
      // exactly "subtract 256, keeping the 14-unit remainder of the last row".
      if (v > kLowSheetLastV)
      {
        sprite.textureSlot = kFontSlotHigh;
        v = (v + 14) % 256;
      }
      sprite.v = v;

      const int width = font.FUN_00238e50_width(character);
      sprite.sourceWidth = width;
      sprite.sourceHeight = kCellSize;
      sprite.width = (width * kAdvancePercent) / 100;
      sprite.height = kDrawnCellHeight;
      sprite.x = screenX(originX + penX + kGlyphOriginBias);
      sprite.y = screenY(originY);

      penX += sprite.width;
      sprites.push_back(sprite);
    }

    return sprites;
  }

  DialogueSprite promptSprite(int originX, int originY, int penX, int animationTicks)
  {
    const int frame = (animationTicks / kPromptTicksPerFrame) % static_cast<int>(kPromptFrames.size());

    DialogueSprite sprite;
    sprite.textureSlot = kPromptSlot;
    sprite.u = kPromptFrames[static_cast<std::size_t>(frame)].u;
    sprite.v = kPromptFrames[static_cast<std::size_t>(frame)].v;
    sprite.sourceWidth = kPromptSourceSize;
    sprite.sourceHeight = kPromptSourceSize;
    sprite.width = kPromptDrawWidth;
    sprite.height = kPromptDrawHeight;
    sprite.x = screenX(originX + penX + kPromptOriginBias);
    sprite.y = screenY(originY);
    return sprite;
  }

  int FUN_00238e68_measure(std::string_view text, const DialogueFont &font, int cellWidth)
  {
    const int scale = (cellWidth * 100) / kCellSize;
    int width = 0;
    for (const char raw : text)
    {
      if (raw < 0)
      {
        // The escape characters are not measured; they bill a fixed cell.
        width += 0x20;
        continue;
      }
      width += (static_cast<int>(font.FUN_00238e50_width(static_cast<unsigned char>(raw))) * scale) / 100;
    }
    return width;
  }

  std::vector<DialogueSprite> FUN_00238608_layout(int x,
                                                  int y,
                                                  std::string_view text,
                                                  std::uint32_t color,
                                                  int cellWidth,
                                                  int cellHeight,
                                                  const DialogueFont &font)
  {
    std::vector<DialogueSprite> sprites;
    // FUN_00238608:22 and :30. Both are percentages of the 22-texel cell, and
    // the vertical one is rounded through the cell before it is used.
    const int horizontalScale = (cellWidth * 100) / kCellSize;
    const int drawnHeight = (((cellHeight * 100) / kCellSize) * kCellSize) / 100;

    int penX = x;
    for (const char raw : text)
    {
      const auto character = static_cast<unsigned char>(raw);
      if (character >= 0xFC || character < kFirstCharacter)
      {
        continue;
      }

      const int index = character - kFirstCharacter;
      int v = (index / kColumns) * kCellSize;
      DialogueSprite sprite;
      sprite.textureSlot = kFontSlotLow;
      sprite.u = (index % kColumns) * kCellSize;
      // FUN_00238608:44-47, the same low/high sheet split FUN_00238a08 makes.
      if (character > 0x98)
      {
        sprite.textureSlot = kFontSlotHigh;
        const int highIndex = character - 0x99;
        sprite.u = (highIndex % kColumns) * kCellSize;
        v = (highIndex / kColumns) * kCellSize;
      }
      sprite.v = v;

      const int width = font.FUN_00238e50_width(character);
      sprite.sourceWidth = width;
      sprite.sourceHeight = kCellSize;
      sprite.width = (width * horizontalScale) / 100;
      sprite.height = drawnHeight;
      sprite.x = screenX(penX);
      sprite.y = screenY(y);
      sprite.color = color;

      penX += sprite.width;
      sprites.push_back(sprite);
    }
    return sprites;
  }

} // namespace orphen::ported::text
