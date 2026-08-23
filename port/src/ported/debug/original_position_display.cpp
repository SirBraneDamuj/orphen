#include "ported/debug/original_position_display.h"

namespace orphen::ported::debug
{
  namespace
  {

    // FUN_0030bd20: truncate toward zero, saturating at the int32 ends. It
    // takes a float in the original; the argument is a double here so that
    // millimetres() below can hand it an exact product.
    int FUN_0030bd20_toInt(double value)
    {
      if (!(value > -2147483648.0))
      {
        return -2147483647 - 1;
      }
      if (!(value < 2147483648.0))
      {
        return 2147483647;
      }
      return static_cast<int>(value);
    }

    // `value * 1000.0f` as the EE computes it. The EE core's FPU has no
    // rounding-mode control -- every result is truncated toward zero, not
    // rounded to nearest -- so a product that lands a hair under a whole
    // millimetre stays under it there and rounds up to it here. That is a
    // whole unit in this display: the camera resting at -0.792 (0xBF4AC083)
    // gives an exact product of -791.9999957, which the EE keeps below -791
    // and a round-to-nearest host snaps to -792.
    //
    // Truncating the *exact* product reproduces the EE exactly. Truncating to
    // float and then to int can only ever move toward zero twice, and the
    // second step lands on the same integer as truncating the exact product
    // once, so the double here is not an approximation of the hardware -- it
    // is the same answer with one step instead of two.
    int millimetres(float value)
    {
      return FUN_0030bd20_toInt(static_cast<double>(value) * 1000.0);
    }

  } // namespace

  void FUN_0026a048_printPosition(DebugTextBuffer &buffer, float x, float y, float z)
  {
    // 0x0034D838. FUN_00269fa8 and FUN_0026a048 both sprintf into a 256-byte
    // stack buffer and hand that to FUN_002681c0 as the format string, which
    // is a detail the port can drop -- the text is identical either way.
    buffer.FUN_002681c0_printf("(%d, %d, %d)\n",
                               millimetres(x),
                               millimetres(y),
                               millimetres(z));
  }

  void emitDetailedPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state)
  {
    // FUN_00269fa8(0x58beb0): the lead player's position, unlabelled.
    FUN_0026a048_printPosition(buffer,
                               state.DAT_0058bed0_playerX,
                               state.DAT_0058bed4_playerY,
                               state.DAT_0058bed8_playerZ);

    // 0x0034BE80.
    buffer.FUN_002681c0_printf("MF:%08X AF:%04X SF:%04X NF:%04X\n",
                               state.DAT_0058bebc_moveFlags,
                               state.DAT_0058beb4_attrFlags,
                               state.DAT_0058beb8_stateFlags,
                               state.DAT_0058beb6_nowFlags);

    // 0x00354D40, then the camera's look-at point.
    buffer.FUN_002681c0_printf("tPOS>");
    FUN_0026a048_printPosition(buffer,
                               state.DAT_0058be90_targetX,
                               state.DAT_0058be94_targetY,
                               state.DAT_0058be98_targetZ);

    // 0x00354D48, then FUN_00269fa8(0x58c088) -- the camera entity's +0x20.
    buffer.FUN_002681c0_printf("cPOS>");
    FUN_0026a048_printPosition(buffer,
                               state.DAT_0058c0a8_cameraX,
                               state.DAT_0058c0ac_cameraY,
                               state.DAT_0058c0b0_cameraZ);

    // 0x0034BEA8.
    buffer.FUN_002681c0_printf("MAP>(MP%02d%02d)\n",
                               state.iGpffffb284_mapSection,
                               state.uGpffffb280_mapEntry);
  }

  void emitCompactPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state)
  {
    const bool previousGate = buffer.DAT_003555dc_outputEnabled();
    buffer.setDAT_003555dc_outputEnabled(true);

    if (!state.cGpffffb663_backgroundScene)
    {
      // 0x0034BE60.
      buffer.FUN_002681c0_printf("~MP%02d%02d",
                                 state.iGpffffb284_mapSection,
                                 state.uGpffffb280_mapEntry);
    }
    else
    {
      // 0x00354D38.
      buffer.FUN_002681c0_printf("~BG%03d", state.backgroundId);
    }

    // 0x0034BE70. Note the tighter format: no spaces after the commas.
    buffer.FUN_002681c0_printf("(%d,%d,%d)\n",
                               millimetres(state.DAT_0058bed0_playerX),
                               millimetres(state.DAT_0058bed4_playerY),
                               millimetres(state.DAT_0058bed8_playerZ));

    // The original writes a literal 0 here rather than restoring, but it only
    // reaches this branch when the gate was already off.
    buffer.setDAT_003555dc_outputEnabled(previousGate);
  }

  void FUN_002239c8_emitPositionDisplay(DebugTextBuffer &buffer, const PositionDisplayState &state)
  {
    if (!buffer.DAT_003555da_debugActive() || buffer.DAT_003555dc_outputEnabled())
    {
      emitDetailedPositionDisplay(buffer, state);
    }
    else
    {
      emitCompactPositionDisplay(buffer, state);
    }
  }

} // namespace orphen::ported::debug
