#include "ported/debug/original_position_display.h"

namespace orphen::ported::debug
{
  namespace
  {

    // FUN_0030bd20: truncate toward zero, saturating at the int32 ends. The
    // callers all pass value * 1000.0f.
    int FUN_0030bd20_toInt(float value)
    {
      if (!(value > -2147483648.0f))
      {
        return -2147483647 - 1;
      }
      if (!(value < 2147483648.0f))
      {
        return 2147483647;
      }
      return static_cast<int>(value);
    }

  } // namespace

  void FUN_0026a048_printPosition(DebugTextBuffer &buffer, float x, float y, float z)
  {
    // 0x0034D838. FUN_00269fa8 and FUN_0026a048 both sprintf into a 256-byte
    // stack buffer and hand that to FUN_002681c0 as the format string, which
    // is a detail the port can drop -- the text is identical either way.
    buffer.FUN_002681c0_printf("(%d, %d, %d)\n",
                               FUN_0030bd20_toInt(x * 1000.0f),
                               FUN_0030bd20_toInt(y * 1000.0f),
                               FUN_0030bd20_toInt(z * 1000.0f));
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
                               FUN_0030bd20_toInt(state.DAT_0058bed0_playerX * 1000.0f),
                               FUN_0030bd20_toInt(state.DAT_0058bed4_playerY * 1000.0f),
                               FUN_0030bd20_toInt(state.DAT_0058bed8_playerZ * 1000.0f));

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
