#pragma once

// The cinematic black bars.
//
//   src/FUN_0025cfb8.c  the two sprites, and the slide
//   src/FUN_0025fd10.c  opcode 0x6D, the only thing that arms them
//   src/FUN_00238a08.c  where the subtitles move to while they are up
//
// FUN_0025cfb8 runs at the end of every script tick (FUN_0025b778:58). It is
// two flat black sprites, full width, one hard against the top edge and one
// hard against the bottom, both the same height -- so the bars are part of the
// game's picture, not a border around it.
//
// == The two globals ==
//
// With gp = 0x00359F70:
//
//   iGpffffb0e4 == DAT_00355054   mode: 0 off, 1 opening/open, -1 closing
//   iGpffffbd8c == DAT_00355CFC   level, a 0..0x780 ramp
//
// Opcode 0x6D is the only writer of either. A negative operand raises mode to
// 1 and seeds the level -- -1 from 0, so the bars slide in, and -2 from 0x780,
// so they are already there. Operand 1 drops mode to -1 and the level runs back
// down, clearing mode when it reaches the bottom. Nothing else in the
// executable sets mode, and FUN_00271220 / FUN_0022b300 / FUN_00225340 clear it
// on a scene change.
//
// The level steps by `DAT_003555bc * 8` per frame, so at the nominal 0x20 ticks
// a bar takes 0x780 / (0x20 * 8) = 7.5 frames to travel.
//
// == Height ==
//
// The drawn height is `level >> 5`, so 0x780 is 60. That is 60 units of the
// 640x448 virtual screen FUN_00207938 addresses -- 30 of the field's 224
// scanlines -- leaving a 328-unit picture between the bars.
//
// The two sprites both use FUN_00239020's untextured path (entry word 0 is
// -1) at entry x = -320 and width 640, which is the full width, and:
//
//   bottom   entry y = height - 224  ->  screen y = 448 - height
//   top      entry y = 224           ->  screen y = 0
//
// Colour is 0xFF000000: opaque black, with blending off (entry +0x2C is 0).
//
// == Draw order ==
//
// FUN_00207938 sorts into 0x1010 buckets chained in ascending order, and the
// entry's word 1 picks the bucket: -0x1007 here. That is the *same* bucket the
// full-screen fade uses (FUN_0025d0e0 calls FUN_00207de8(0x1007)), and both
// FUN_002239c8 and FUN_00224320 submit the fade first -- insertion is LIFO
// within a bucket, so the bars draw first and the fade tints them.
//
// The dialogue glyphs (FUN_00237b38 seeds every slot's word 1 with -0x1009)
// and the debug text (FUN_00268410 passes -0x1009) are a later bucket, so both
// draw over the bars.

#include <cstdint>

namespace orphen::ported::render
{

  class Letterbox
  {
  public:
    // FUN_0025cfb8's clamp, and the shift that turns the ramp into pixels.
    static constexpr int kFullLevel = 0x780;
    static constexpr int kLevelShift = 5;
    static constexpr int kLevelStepPerTick = 8;

    // The sprite geometry, in the 640x448 virtual screen FUN_00207938
    // addresses. See original_dialogue_text.h for the same units.
    static constexpr int kScreenWidth = 640;
    static constexpr int kScreenHeight = 448;

    // FUN_0025fd10's -1 and -2 arms. `alreadyOpen` is the -2 operand, which
    // seeds the level at full so the bars do not slide.
    void FUN_0025fd10_open(bool alreadyOpen);

    // FUN_0025fd10's operand-1 arm: start the bars closing, if they are up.
    void FUN_0025fd10_close();

    // FUN_0025cfb8. Publishes the height for this frame from the level as it
    // stands, then advances the level -- that order is the original's, and it
    // is why the first frame after an operand of -1 draws nothing.
    // `frameTicks` is DAT_003555bc.
    void FUN_0025cfb8_step(std::uint32_t frameTicks);

    // iGpffffb0e4. Non-zero while the bars are on screen at all; the subtitle
    // nudge in FUN_00238a08 tests `> 0`, so it wants the raw value.
    int DAT_00355054_mode() const { return DAT_00355054_mode_; }

    // The height of each bar this frame, 0..60. Zero means nothing to draw.
    int barHeight() const { return barHeight_; }

    // FUN_00271220's `_DAT_00355054 = 0` at a scene change. The level is not
    // cleared there in the original, but mode 0 makes it unreachable.
    void reset();

  private:
    int DAT_00355054_mode_ = 0;
    int DAT_00355cfc_level_ = 0;
    int barHeight_ = 0;
  };

} // namespace orphen::ported::render
