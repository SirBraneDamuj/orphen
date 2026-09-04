#pragma once

// Native counterpart of src/FUN_0023b3f0.c (0x0023b3f0): converts one raw
// analog stick into the magnitude/angle pair the rest of the game reads.
//
// Called twice from FUN_0023b5d8, once per stick. The left stick writes
// DAT_003555e8 (fGpffffb678, magnitude) and DAT_003555e4 (fGpffffb674, angle);
// those are what FUN_00256bb8 tests against 100.0 for walk versus run, what
// FUN_00216aa0 tests against 40.0 for its yaw deadzone, and what FUN_00253488
// multiplies air control by.

#include <cstdint>

namespace orphen::ported::input
{

  // FUN_0023b3f0 works on raw pad bytes: each axis is 0..255 centred on 0x80,
  // giving a component range of -128..127.
  constexpr float kRawAxisRange = 128.0f;

  // The deadzone is 60 out of 128, so nothing moves below roughly 47 percent
  // deflection. This is large, and it is the reason there is a perceptible
  // delay between pushing the stick and the character starting to move: the
  // stick has to physically travel almost halfway first.
  constexpr float kRawDeadzone = 60.0f;

  // Above the deadzone the remaining 68 raw units are rescaled to 0..128, so
  // full deflection reports 128 and the walk/run threshold of 100 lands at
  // about 88 percent deflection.
  constexpr float kRawSpan = 68.0f;
  constexpr float kMagnitudeRange = 128.0f;

  struct AnalogStickState
  {
    float magnitude = 0.0f; // DAT_003555e8 / fGpffffb678, 0 or 60..128 rescaled.
    float angle = 0.0f;     // DAT_003555e4 / fGpffffb674, atan2(up, right).
  };

  // rawRight and rawUp are the centred axis components in -128..127, i.e.
  // (x & 0xff) - 0x80 and 0x80 - (y & 0xff) as the original computes them.
  AnalogStickState FUN_0023b3f0_read_analog_stick(float rawRight, float rawUp);

  // src/FUN_0023b4e8.c (0x0023b4e8): the movement stick quantised onto the
  // same four direction bits the D-pad occupies in DAT_003555f4.
  //
  // FUN_0023b5d8 calls it on the pair it just read into DAT_003555e8 /
  // DAT_003555e4 -- the *movement* stick, the one FUN_00256bb8 walks on, not
  // the camera stick -- and parks the result in DAT_003555fe. DAT_00355600 is
  // then that word's newly-pressed edge, and FUN_002462c8 ORs it into
  // DAT_003555f6 so target cycling answers to the stick exactly as it answers
  // to the D-pad.
  //
  // Sector layout, measured from +X counter-clockwise, with a magnitude gate of
  // 100 out of 128 -- well above FUN_0023b3f0's 60 deadzone, so a nudge that
  // walks the character does not cycle a target:
  //
  //   [330,30)  right          [30,60)   up|right     [60,120)  up
  //   [120,150) up|left        [150,210) left         [210,240) down|left
  //   [240,300) down           [300,330) down|right
  // The four direction bits, high nibble of DAT_003555f4. Same word the D-pad
  // occupies on hardware, which is why one test in FUN_002462c8 covers both.
  constexpr std::uint16_t kPadUp = 0x1000;
  constexpr std::uint16_t kPadRight = 0x2000;
  constexpr std::uint16_t kPadDown = 0x4000;
  constexpr std::uint16_t kPadLeft = 0x8000;

  // The literals FUN_0023b4e8 compares against, dumped from SLUS_200.11. They
  // are radians: 2pi, then the eight sector edges.
  constexpr float kDirectionMagnitudeGate = 100.0f;
  constexpr float kDAT_00352624_twoPi = 6.2831841f;
  constexpr float kDAT_00352628_deg210 = 3.6651907f;
  constexpr float kDAT_0035262c_deg120 = 2.0943947f;
  constexpr float kDAT_00352630_deg30 = 0.5235987f;
  constexpr float kDAT_00352634_deg60 = 1.0471973f;
  constexpr float kDAT_00352638_deg150 = 2.6179934f;
  constexpr float kDAT_0035263c_deg300 = 5.2359867f;
  constexpr float kDAT_00352640_deg240 = 4.1887894f;
  constexpr float kDAT_00352644_deg330 = 5.7595854f;

  std::uint16_t FUN_0023b4e8_stick_direction_bits(float magnitude, float angle);

} // namespace orphen::ported::input
