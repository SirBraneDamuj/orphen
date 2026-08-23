#include "ported/render/original_screen_fade.h"

namespace orphen::ported::render
{

  void ScreenFade::FUN_0025d0e0_apply(const Block &block)
  {
    // `colour + ((int)(level << 16) >> 0x15) * 0x1000000`: sign-extend the
    // level through 16 bits, then shift down 5 so 0x1FE0 lands on 0xFF.
    const int level = static_cast<std::int16_t>(block.DAT_00571dd0_level);
    DAT_0025d0e0_applied_.colour = block.DAT_00571dd4_colour;
    DAT_0025d0e0_applied_.alpha = static_cast<std::uint8_t>((level >> 5) & 0xFF);
  }

  void ScreenFade::FUN_0025d1c0_arm(bool fadeOut, std::uint16_t speed, std::uint32_t colour)
  {
    Block &block = fadeOut ? DAT_00571dd0_fadeOut_ : DAT_00571dc0_fadeIn_;

    // The two blocks differ only in which end of the ramp they start at.
    block.DAT_00571dd0_level = fadeOut ? 0 : kFullCoverage;
    block.DAT_00571dda_hold = kHoldTicks;
    block.DAT_00571dd2_speed = speed;
    block.DAT_00571dd4_colour = colour;
    FUN_0025d0e0_apply(block);
    block.DAT_00571dd8_done = 0;
  }

  bool ScreenFade::FUN_0025d238_step_fade_out(std::uint32_t frameTicks)
  {
    Block &block = DAT_00571dd0_fadeOut_;
    block.DAT_00571dd8_done = 0;

    if (static_cast<std::int16_t>(block.DAT_00571dd0_level) < static_cast<std::int16_t>(kFullCoverage))
    {
      const int stepped = static_cast<int>(block.DAT_00571dd0_level) +
                          static_cast<int>(block.DAT_00571dd2_speed) * static_cast<int>(frameTicks);
      block.DAT_00571dd0_level = static_cast<std::uint16_t>(stepped);
      if (static_cast<std::int16_t>(block.DAT_00571dd0_level) > static_cast<std::int16_t>(kFullCoverage))
      {
        block.DAT_00571dd0_level = kFullCoverage;
      }
    }
    else if (block.DAT_00571dda_hold < 1)
    {
      // Fully covered and the hold has run out: this is the frame the caller
      // is waiting for.
      block.DAT_00571dd8_done = 1;
    }
    else
    {
      block.DAT_00571dda_hold =
          static_cast<std::int16_t>(block.DAT_00571dda_hold - static_cast<int>(frameTicks));
    }

    FUN_0025d0e0_apply(block);
    return block.DAT_00571dd8_done != 0;
  }

  bool ScreenFade::FUN_0025d2f8_step_fade_in(std::uint32_t frameTicks)
  {
    Block &block = DAT_00571dc0_fadeIn_;

    const int stepped = static_cast<int>(block.DAT_00571dd0_level) -
                        static_cast<int>(block.DAT_00571dd2_speed) * static_cast<int>(frameTicks);
    // The sign test is on the truncated 16-bit result, not on `stepped`.
    const bool stillFading = static_cast<std::int16_t>(static_cast<std::uint16_t>(stepped)) >= 0;
    block.DAT_00571dd0_level = static_cast<std::uint16_t>(stepped);
    if (!stillFading)
    {
      block.DAT_00571dd0_level = 0;
    }
    block.DAT_00571dd8_done = stillFading ? 0 : 1;

    FUN_0025d0e0_apply(block);
    return block.DAT_00571dd8_done != 0;
  }

  void ScreenFade::FUN_0025d0e0_set_overlay(std::uint32_t colour, std::uint8_t alpha)
  {
    DAT_0025d0e0_applied_.colour = colour;
    DAT_0025d0e0_applied_.alpha = alpha;
  }

  void ScreenFade::reset()
  {
    DAT_00571dc0_fadeIn_ = Block{};
    DAT_00571dd0_fadeOut_ = Block{};
    DAT_0025d0e0_applied_ = Overlay{};
  }

} // namespace orphen::ported::render
