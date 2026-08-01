/*
 * Camera Orientation Recompute and Normal-Field Defaults
 * Original functions: FUN_00217a70, FUN_00216930, FUN_00216968
 * Addresses: 0x00217a70, 0x00216930, 0x00216968
 *
 * Three small helpers that sit under the field camera driver
 * (FUN_00216aa0 -> analyzed/update_field_camera.c):
 *
 *   FUN_00217a70  recompute yaw/pitch and the view vector from eye and look-at
 *   FUN_00216930  install the normal-field camera defaults
 *   FUN_00216968  recompute the derived follow geometry for a given distance
 *
 * gp = 0x00359F70. Constants read from eeMemory.bin.
 */

/* =========================================================================
 * FUN_00217a70 -- recompute orientation from the current eye and look-at.
 *
 * Called by script camera opcodes after they move the eye or target. Unlike
 * the tail of FUN_00216aa0, which always snaps yaw, this one eases yaw toward
 * the goal when the camera is close to its subject, so a script camera swinging
 * past its target does not whip around.
 *
 * Constants:
 *   fGpffff82e8  DAT_00352258  0.40000004   eye height offset
 *   fGpffff82ec  DAT_0035225c  0.30000001   minimum horizontal distance;
 *                                           below this, orientation is left
 *                                           untouched to avoid atan2 noise
 *   fGpffff82f0  DAT_00352260  0.052359868  yaw ease rate, 3 deg
 *
 * Reads:  DAT_0058be90/94/98 (look-at), DAT_0058c0a8/ac/b0 (eye),
 *         cGpffffb6e5 (enables the close-range ease)
 * Writes: fGpffffb6d4 (yaw), uGpffffb6d8 (pitch), DAT_0058bea0/a4/a8 (view dir)
 * Callees: FUN_00216608 hypot, FUN_00305408 atan2, FUN_0023a320 angular lerp,
 *          FUN_00216510 publish view
 * ========================================================================= */
void recompute_camera_orientation(void)
{
  float dirX = DAT_0058be90 - DAT_0058c0a8;
  float dirY = DAT_0058be94 - DAT_0058c0ac;
  float dirZ = DAT_0058be98 - (DAT_0058c0b0 + fGpffff82e8);
  float horizontal;

  horizontal = FUN_00216608(dirX, dirY);

  /* Below 0.3 units of horizontal separation the angles are meaningless, so
   * the previous yaw and pitch are kept. */
  if (horizontal > fGpffff82ec)
  {
    uGpffffb6d8 = FUN_00305408(dirZ, horizontal); /* pitch always snaps */

    if (horizontal > 1.5f || cGpffffb6e5 == 0)
    {
      fGpffffb6d4 = FUN_00305408(dirY, dirX); /* far away: snap yaw */
    }
    else
    {
      /* Close in, and the ease is enabled: turn toward the goal at a rate
       * proportional to the remaining distance, so the swing damps out as the
       * camera closes on its subject. This is the branch the port was missing.
       */
      float goal = FUN_00305408(dirY, dirX);
      fGpffffb6d4 += FUN_0023a320(fGpffffb6d4, goal, horizontal * fGpffff82f0);
    }
  }

  DAT_0058bea0 = dirX;
  DAT_0058bea4 = dirY;
  DAT_0058bea8 = dirZ;
  FUN_00216510(&DAT_0058bea0);
}

/* =========================================================================
 * FUN_00216930 -- install the normal-field camera defaults.
 *
 * Distance 3.0 and pitch fGpffff8224 = 0.36651909 rad (exactly 21 degrees).
 * FUN_002169c0 applies the pitch; FUN_00216968 derives the follow geometry.
 *
 * Writes: uGpffffad28 (3.0), uGpffffad24 (pitch)
 * ========================================================================= */
void install_normal_field_camera_defaults(void)
{
  uGpffffad28 = 3.0f;         /* 0x40400000 */
  uGpffffad24 = uGpffff8224;  /* 0.36651909 = 21 deg */
  FUN_00216968(3.0f);
  FUN_002169c0(uGpffffad24);
}

/* =========================================================================
 * FUN_00216968 -- derive the follow geometry for a camera distance.
 *
 * This is where the field camera's two follow targets come from. The driver in
 * FUN_00216aa0 never uses distance and pitch directly; it uses these two
 * derived values:
 *
 *   fGpffffbaf8 = distance * cos(pitch)              horizontal follow distance
 *   fGpffffbafc = distance * sin(pitch) + fGpffff8228   height above the target
 *
 * With the defaults (distance 3.0, pitch 21 deg, fGpffff8228 = -0.2):
 *   fGpffffbaf8 = 3 * cos(0.36651909)        = 2.800741
 *   fGpffffbafc = 3 * sin(0.36651909) - 0.2  = 0.875104
 *
 * So the normal field camera trails 2.80 units behind the player and sits 0.88
 * above the point it follows, which is itself 0.8 above the entity origin
 * (iGpffff8230). Setting uGpffffb6e3 = 1 requests a snap on the next update, so
 * a distance change takes effect immediately rather than easing.
 *
 * Also called by the script opcode that sets camera distance -- see
 * analyzed/ops/0xB8_set_camera_distance.c and FUN_0023a860.
 *
 * Writes: fGpffffad28, fGpffffb6ec (distance), fGpffffbaf8, fGpffffbafc,
 *         uGpffffb6e3 (snap request)
 * ========================================================================= */
void set_camera_follow_distance(float distance)
{
  fGpffffad28 = distance;
  fGpffffb6ec = distance;

  fGpffffbaf8 = distance * FUN_00305130(uGpffffad24); /* cos(pitch) */
  fGpffffbafc = distance * FUN_00305218(uGpffffad24)  /* sin(pitch) */
                + fGpffff8228;                        /* -0.2 */

  uGpffffb6e3 = 1; /* snap on the next FUN_00216aa0 */
}
