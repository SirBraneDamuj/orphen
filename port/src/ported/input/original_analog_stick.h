#pragma once

// Native counterpart of src/FUN_0023b3f0.c (0x0023b3f0): converts one raw
// analog stick into the magnitude/angle pair the rest of the game reads.
//
// Called twice from FUN_0023b5d8, once per stick. The left stick writes
// DAT_003555e8 (fGpffffb678, magnitude) and DAT_003555e4 (fGpffffb674, angle);
// those are what FUN_00256bb8 tests against 100.0 for walk versus run, what
// FUN_00216aa0 tests against 40.0 for its yaw deadzone, and what FUN_00253488
// multiplies air control by.

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

} // namespace orphen::ported::input
