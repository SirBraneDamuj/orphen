#include "ported/input/original_analog_stick.h"

#include <cmath>

namespace orphen::ported::input
{

  AnalogStickState FUN_0023b3f0_read_analog_stick(float rawRight, float rawUp)
  {
    // iVar2 = (param_1 & 0xff) - 0x80;  iVar1 = 0x80 - (param_2 & 0xff);
    // fVar3 = SQRT((float)(iVar2 * iVar2 + iVar1 * iVar1));
    const float rawMagnitude = std::sqrt(rawRight * rawRight + rawUp * rawUp);

    // if (fVar3 < 60.0) { *param_3 = 0.0; *param_4 = 0; }
    if (rawMagnitude < kRawDeadzone)
    {
      return {};
    }

    // fVar3 = ((fVar3 - 60.0) * 128.0) / 68.0;  if (128.0 < fVar3) fVar3 = 128.0;
    float magnitude = ((rawMagnitude - kRawDeadzone) * kMagnitudeRange) / kRawSpan;
    if (magnitude > kMagnitudeRange)
    {
      magnitude = kMagnitudeRange;
    }

    // *param_4 = FUN_00305408((float)iVar1, (float)iVar2);
    return {magnitude, std::atan2(rawUp, rawRight)};
  }

  std::uint16_t FUN_0023b4e8_stick_direction_bits(float magnitude, float angle)
  {
    // if (100.0 < param_1) -- and nothing else; below the gate the word is 0
    // and DAT_00355600 never sees an edge.
    if (magnitude <= kDirectionMagnitudeGate)
    {
      return 0;
    }

    // if (param_2 < 0.0) param_2 = param_2 + DAT_00352624;  (atan2 returns
    // -pi..pi, the sector table is written for 0..2pi)
    if (angle < 0.0f)
    {
      angle += kDAT_00352624_twoPi;
    }

    if (angle < kDAT_00352628_deg210)
    {
      if (angle < kDAT_0035262c_deg120)
      {
        if (angle < kDAT_00352630_deg30)
        {
          return kPadRight;
        }
        if (angle < kDAT_00352634_deg60)
        {
          return kPadUp | kPadRight;
        }
        return kPadUp;
      }
      if (angle < kDAT_00352638_deg150)
      {
        return kPadUp | kPadLeft;
      }
      return kPadLeft;
    }
    if (angle < kDAT_0035263c_deg300)
    {
      if (angle < kDAT_00352640_deg240)
      {
        return kPadDown | kPadLeft;
      }
      return kPadDown;
    }
    if (angle < kDAT_00352644_deg330)
    {
      return kPadDown | kPadRight;
    }
    return kPadRight;
  }

} // namespace orphen::ported::input
