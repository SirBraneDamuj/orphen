/*
 * Manual / Fixed Camera Modes
 * Original function: FUN_00218710
 * Address: 0x00218710
 *
 * Handles camera sub-modes 0x1b..0x1e, dispatched from the field camera driver
 * (FUN_00216aa0 -> analyzed/update_field_camera.c) when cGpffffb6e1 falls in
 * that range. Unlike the field camera, these modes place the eye directly from
 * the target position each frame with no smoothing at all -- the eye is simply
 * "target minus direction * distance".
 *
 * Ghidra lost the argument at the call site in FUN_00216aa0, which appears as
 * `FUN_00218710()`. The guard there is `0x1a < cGpffffb6e1 && cGpffffb6e1 < 0x1f`
 * and this function switches on param_1 == 0x1b/0x1c/0x1d/0x1e, so param_1 is
 * cGpffffb6e1.
 *
 * Modes:
 *   0x1b  distance DAT_00354c88 (3.0), pitch DAT_00354c90 (0.0) -- both are
 *         writable at runtime, so this is the script-tunable orbit mode.
 *   0x1c  distance 3.0, pitch DAT_00352290 (-0.97738421, -56 deg): looking down.
 *   0x1e  distance 5.0, same steep downward pitch.
 *   0x1d  a fixed close-up: direction is taken straight from the stored yaw
 *         DAT_0058bf0c with a hardcoded -0.5 Z component, and the eye is placed
 *         two units back along it. Does not use the orbit path at all.
 *
 * Manual yaw stepping (shared by 0x1b/0x1c/0x1e):
 *   R1 (raw bit 0x08): DAT_00354c8c -= DAT_003555bc * DAT_00352294
 *   L1 (raw bit 0x04): DAT_00354c8c += DAT_003555bc * DAT_003555bc * DAT_00352298
 *   Both step constants are 0.0015625, so at the nominal 0x20 ticks the camera
 *   turns 0.05 rad (2.86 deg) per frame, about 172 deg/sec.
 *   FUN_00216690 wraps the result back into range.
 *
 * Note the raw pad low byte is byte-swapped relative to the usual PS2 constants
 * (see FUN_0023b5d8): 0x04 is L1 and 0x08 is R1, not d-pad bits.
 *
 * Constants (read from eeMemory.bin):
 *   DAT_00352290  -0.97738421  steep downward pitch for 0x1c / 0x1e
 *   DAT_00352294   0.0015625   yaw step per tick, R1
 *   DAT_00352298   0.0015625   yaw step per tick, L1
 *   DAT_0035229c   0.8         look-at height above the target origin
 *   DAT_003522a0   0.4         eye Z sits this far below the published height
 *   DAT_003522a4   0.8         same, for mode 0x1d
 *   DAT_003522a8   0.4         same, for mode 0x1d
 *   DAT_00354c88   3.0         mode 0x1b distance   (runtime-writable)
 *   DAT_00354c90   0.0         mode 0x1b pitch      (runtime-writable)
 *
 * Reads:  DAT_0058bed0/d4/d8 (target), DAT_0058bf0c (stored yaw),
 *         DAT_003555f4 (raw held pad), DAT_003555bc (frame ticks)
 * Writes: DAT_0058be80/84/88 (published eye), DAT_0058be90/94/98 (look-at),
 *         DAT_0058bea0/a4/a8 (view direction), DAT_0058c0a8/ac/b0 (eye state),
 *         DAT_00354c8c (manual yaw), DAT_00355644/48 (published yaw/pitch)
 *
 * Callees: FUN_00305130 cos, FUN_00305218 sin, FUN_00305408 atan2,
 *          FUN_00216608 hypot, FUN_00216690 angle wrap, FUN_00216510 publish view
 */

void update_manual_camera(long mode)
{
  float pitch;
  float distance;
  float horizontal;
  float cosYaw;
  float viewHorizontal;

  distance = DAT_00354c88; /* 3.0 */
  pitch = DAT_00354c90;    /* 0.0 */

  if (mode != 0x1b)
  {
    pitch = DAT_00352290; /* -0.97738421, looking down */

    if (mode == 0x1c)
    {
      distance = 3.0f;
    }
    else if (mode == 0x1e)
    {
      distance = 5.0f;
    }
    else if (mode == 0x1d)
    {
      /* Fixed close-up. Direction comes straight from the stored yaw with a
       * constant downward component; the eye is two units back along it. */
      DAT_0058bea0 = FUN_00305130(DAT_0058bf0c); /* cos(storedYaw) */
      DAT_0058bea4 = FUN_00305218(DAT_0058bf0c); /* sin(storedYaw) */
      DAT_0058bea8 = -0.5f;
      FUN_00216510(&DAT_0058bea0);

      DAT_0058be98 = DAT_0058bed8 + DAT_003522a4; /* target Z + 0.8 */
      DAT_0058be88 = DAT_0058be98 - (DAT_0058bea8 + DAT_0058bea8);
      DAT_0058be80 = DAT_0058bed0 - (DAT_0058bea0 + DAT_0058bea0);
      DAT_0058be84 = DAT_0058bed4 - (DAT_0058bea4 + DAT_0058bea4);
      DAT_0058be90 = DAT_0058bed0;
      DAT_0058be94 = DAT_0058bed4;
      DAT_0058c0b0 = DAT_0058be88 - DAT_003522a8; /* eye Z tracks 0.4 lower */
      DAT_0058c0a8 = DAT_0058be80;
      DAT_0058c0ac = DAT_0058be84;

      horizontal = FUN_00216608(DAT_0058bea0, DAT_0058bea4);
      goto publish;
    }
    else
    {
      return; /* not a mode this function owns */
    }
  }

  /* --- manual yaw stepping, L1 / R1 -------------------------------------- */
  if ((DAT_003555f4 & 8) != 0) /* R1 */
  {
    DAT_00354c8c = FUN_00216690(DAT_00354c8c - (float)DAT_003555bc * DAT_00352294);
  }
  else if ((DAT_003555f4 & 4) != 0) /* L1 */
  {
    DAT_00354c8c = FUN_00216690(DAT_00354c8c + (float)DAT_003555bc * DAT_00352298);
  }

  /* --- place the eye on the orbit ---------------------------------------- */
  cosYaw = FUN_00305130(pitch);
  DAT_0058bea8 = FUN_00305218(pitch);
  DAT_0058bea0 = cosYaw * FUN_00305130(DAT_00354c8c);
  DAT_0058bea4 = cosYaw * FUN_00305218(DAT_00354c8c);
  FUN_00216510(&DAT_0058bea0);

  DAT_0058be98 = DAT_0058bed8 + DAT_0035229c; /* look-at Z = target + 0.8 */
  DAT_0058be88 = DAT_0058be98 - DAT_0058bea8 * distance;
  DAT_0058be80 = DAT_0058bed0 - DAT_0058bea0 * distance;
  DAT_0058be84 = DAT_0058bed4 - DAT_0058bea4 * distance;
  DAT_0058be90 = DAT_0058bed0;
  DAT_0058be94 = DAT_0058bed4;
  DAT_0058c0b0 = DAT_0058be88 - DAT_003522a0;
  DAT_0058c0a8 = DAT_0058be80;
  DAT_0058c0ac = DAT_0058be84;

  horizontal = FUN_00216608(DAT_0058bea0, DAT_0058bea4);

publish:
  DAT_00355644 = FUN_00305408(DAT_0058bea4, DAT_0058bea0); /* yaw   */
  DAT_00355648 = FUN_00305408(DAT_0058bea8, horizontal);   /* pitch */
}
