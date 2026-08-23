#pragma once

// The full-screen fade.
//
//   src/FUN_0025d1c0.c  arm one of the two fade blocks
//   src/FUN_0025d238.c  step the fade-*out* block; returns "done"
//   src/FUN_0025d2f8.c  step the fade-*in* block; returns "done"
//   src/FUN_0025d0e0.c  push the resulting colour to the overlay quad
//
// There are two independent blocks, at DAT_00571DC0 and DAT_00571DD0, and
// which one a caller means is chosen by FUN_0025d1c0's first argument:
//
//   arg != 0   the out block. Level starts at 0 and climbs to 0x1FE0, so the
//              screen darkens. FUN_0025d238 steps it, and once it is fully
//              covered it holds for a further 0xA0 ticks before reporting
//              done -- five frames of solid colour, which is where the work
//              behind the fade happens.
//   arg == 0   the in block. Level starts at 0x1FE0 and falls to 0, so arming
//              it covers the screen immediately and FUN_0025d2f8 reveals it.
//
// Level is a 13-bit ramp and the overlay alpha is `level >> 5`, so 0x1FE0
// is 0xFF. Both blocks feed the same sink, FUN_0025d0e0, which is why this
// exposes one overlay rather than two: whichever block stepped most recently
// owns the screen, and the state machines only ever run one at a time.
//
// Not ported: FUN_0025d0e0's GS packet itself (a screen-sized sprite through
// FUN_00207de8) and the DAT_0035505c bits it raises for the frame's draw list.
// The port hands the colour to the renderer instead.

#include <cstdint>

namespace orphen::ported::render
{

  class ScreenFade
  {
  public:
    // The level at full coverage, and the hold FUN_0025d1c0 arms.
    static constexpr std::uint16_t kFullCoverage = 0x1FE0;
    static constexpr std::int16_t kHoldTicks = 0xA0;

    // The speed every caller in the chest cutscene passes.
    static constexpr std::uint16_t kCutsceneSpeed = 0x0C;

    struct Overlay
    {
      std::uint32_t colour = 0; // 0x00BBGGRR, FUN_0025d1c0's third argument
      std::uint8_t alpha = 0;
    };

    // FUN_0025d1c0. Applies once immediately, so arming the in block blacks
    // the screen on the same frame.
    void FUN_0025d1c0_arm(bool fadeOut, std::uint16_t speed, std::uint32_t colour);

    // FUN_0025d238 / FUN_0025d2f8. frameTicks is DAT_003555bc.
    bool FUN_0025d238_step_fade_out(std::uint32_t frameTicks);
    bool FUN_0025d2f8_step_fade_in(std::uint32_t frameTicks);

    const Overlay &overlay() const { return DAT_0025d0e0_applied_; }

    // FUN_0025d0e0 called directly, which is what opcode 0x89 (FUN_00260ce0)
    // does: it bypasses both ramp blocks and hands the sink a colour and an
    // alpha of its own. The original packs `expr0 | (expr1 << 24)` into the
    // same argument the steppers build, so the split here is the same one
    // FUN_0025d0e0_apply makes.
    void FUN_0025d0e0_set_overlay(std::uint32_t colour, std::uint8_t alpha);

    // Not an original entry point: a map load leaves no fade behind, and the
    // port has no equivalent of the boot path that clears these.
    void reset();

  private:
    // One block. Field names carry the address of the out block's copy.
    struct Block
    {
      std::uint16_t DAT_00571dd0_level = 0;
      std::uint16_t DAT_00571dd2_speed = 0;
      std::uint32_t DAT_00571dd4_colour = 0;
      std::uint16_t DAT_00571dd8_done = 0;
      std::int16_t DAT_00571dda_hold = 0;
    };

    Block DAT_00571dc0_fadeIn_;
    Block DAT_00571dd0_fadeOut_;
    Overlay DAT_0025d0e0_applied_;

    void FUN_0025d0e0_apply(const Block &block);
  };

} // namespace orphen::ported::render
