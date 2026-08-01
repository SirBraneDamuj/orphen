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

} // namespace orphen::ported::input
