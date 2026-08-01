/*
 * Field Camera Update
 * Original function: FUN_00216aa0
 * Address: 0x00216aa0
 *
 * The per-frame camera driver, called from the main game loop (FUN_002239c8 ->
 * analyzed/main_game_loop.c). It owns the camera eye position, the look-at
 * target, and the yaw/pitch derived from them. Everything else that touches the
 * camera (script opcodes, the manual-rotate mode, the battle camera) either
 * writes the same globals or is dispatched from here.
 *
 * Structure, in order:
 *   1. Allocate a scratchpad frame for locals.
 *   2. Load the follow target from DAT_0058bed0/d4/d8 plus a height offset.
 *   3. Idle timeout -> hand off to the auto-camera (FUN_002184e8).
 *   4. Manual free-look acquisition/release (FUN_00218270).
 *   5. Sub-mode dispatch on cGpffffb6e1 (manual / script / snap).
 *   6. Camera mode selection from the raw pad.
 *   7. Rate-limited follow of the target position, per axis.
 *   8. Yaw error toward the target, smoothed with accel and max-speed limits.
 *   9. A six-case mode switch that picks the yaw accel/max/goal for this frame.
 *  10. Apply the yaw rotation to the eye, orbiting at the current distance.
 *  11. Rate-limited vertical follow, with an optional ground clamp.
 *  12. Recompute the view direction and the yaw/pitch globals.
 *
 * PS2 notes:
 * - piVar2 is NOT persistent state. DAT_70000000 is the EE scratchpad (SPR,
 *   16 KB at 0x70000000); the function bump-allocates a 0x60-byte frame from it
 *   on entry and releases it at LAB_00217a38. FUN_0026bf90(0) is the overflow
 *   trap when the bump pointer passes 0x70003fff. So every piVar2[N] below is a
 *   local, and all persistent camera state lives in the DAT_/gp globals.
 * - Every per-frame rate is scaled by `(float)iGpffffb64c * 0.03125`, i.e. the
 *   elapsed-frame tick count over 0x20. See DAT_003555bc in FUN_002000c0. The
 *   constants below are therefore per-60Hz-frame quantities.
 * - Angles are radians. The constant block at 0x00352194-0x00352254 is almost
 *   entirely round degree values: 0.36651909 = 21 deg, 0.52359867 = 30 deg,
 *   1.0471973 = 60 deg, 2.0943947 = 120 deg, 0.0065449835 = 0.375 deg,
 *   0.0043633222 = 0.25 deg, 0.00087266444 = 0.05 deg, 0.026179934 = 1.5 deg.
 *
 * Constant block (gp-relative; gp = 0x00359F70), values read from eeMemory.bin:
 *   fGpffff8224  DAT_00352194   0.36651909    default pitch, 21 deg
 *   fGpffff8228  DAT_00352198  -0.2           eye height offset
 *   fGpffff822c  DAT_0035219c  -0.2           eye height offset (second copy)
 *   iGpffff8230  DAT_003521a0   0.8           target height above entity origin
 *   fGpffff8234  DAT_003521a4   0.1           free-look release blend
 *   fGpffff8238  DAT_003521a8   0.0043633222  default yaw accel, 0.25 deg
 *   iGpffff823c  DAT_003521ac   0.069813155   default yaw max speed, 4 deg
 *   fGpffff8240  DAT_003521b0   1.0471973     yaw deadzone low, 60 deg
 *   fGpffff8244  DAT_003521b4   2.0943947     yaw deadzone high, 120 deg
 *   fGpffff8248..fGpffff8258   target-Z follow steps, negative error
 *                  -0.01, -0.6, -0.02, -0.04, 0.04
 *   fGpffff825c..fGpffff826c   target-Z follow steps, positive error
 *                   0.01,  0.6,  0.02,  0.04, 0.04
 *   fGpffff8270..fGpffff8280   eye-Z follow steps, negative error
 *                  -0.02, -0.3, -0.03, -0.05, 0.05
 *   fGpffff8284..fGpffff8294   eye-Z follow steps, positive error
 *                   0.02,  0.3,  0.03,  0.05, 0.05
 *   fGpffff8298  DAT_00352208   0.016         horizontal follow deadzone
 *   fGpffff829c  DAT_0035220c   0.04          horizontal follow accel
 *   fGpffff82a0  DAT_00352210   0.08          horizontal follow decel
 *   fGpffff82a4  DAT_00352214   0.0065449835  mode 1 yaw accel, 0.375 deg
 *   fGpffff82a8  DAT_00352218  -0.52359867    mode 1 yaw max, -30 deg
 *   fGpffff82ac  DAT_0035221c   0.0065449835  mode 2 yaw accel, 0.375 deg
 *   fGpffff82b0  DAT_00352220   0.52359867    mode 2 yaw max, 30 deg
 *   fGpffff82b4  DAT_00352224   0.13962631    mode 1/2 yaw cap, 8 deg
 *   fGpffff82b8  DAT_00352228  -0.017453289   mode 5 yaw step, -1 deg
 *   fGpffff82bc  DAT_0035222c   0.017453289   mode 6 deadzone, 1 deg
 *   fGpffff82c0  DAT_00352230   0.69813156    mode 6 max, 40 deg
 *   fGpffff82c4  DAT_00352234   0.0043633222  auto-focus accel, 0.25 deg
 *   fGpffff82c8  DAT_00352238   0.10471974    auto-focus max, 6 deg
 *   fGpffff82cc  DAT_0035223c   0.52359867    auto-focus goal +30 deg
 *   fGpffff82d0  DAT_00352240  -0.52359867    auto-focus goal -30 deg
 *   fGpffff82d4  DAT_00352244   0.00087266444 idle yaw accel, 0.05 deg
 *   fGpffff82d8  DAT_00352248   0.026179934   idle yaw max, 1.5 deg
 *   fGpffff82dc  DAT_0035224c   0.03          vertical follow accel
 *   fGpffff82e0  DAT_00352250   0.06          vertical follow decel
 *   fGpffff82e4  DAT_00352254   0.40000004    eye height offset for orientation
 *
 * Persistent camera state:
 *   DAT_0058bed0/d4/d8  follow target source position (lead entity)
 *   DAT_0058c0a8/ac/b0  camera eye position X / Y / Z
 *   DAT_0055f8c8/cc/d0  smoothed target work position
 *   DAT_0058be90/94/98  smoothed look-at target
 *   DAT_0058be80/84/88  published eye position
 *   DAT_0058bea0/a4/a8  view direction vector (fed to FUN_00216510)
 *   DAT_0058c0d4        ground height under the eye (FUN_00227798)
 *   fGpffffb6d4         camera yaw
 *   fGpffffb6d8         camera pitch
 *   bGpffffb6e0         camera mode, 1..6; the switch subtracts 1
 *   cGpffffb6e1         sub-mode: 0x1b-0x1e manual, 0x20 snap, 0x23 script
 *   cGpffffb6e2         cutscene/battle gate; nonzero suppresses field logic
 *   cGpffffb6e3         snap flag: 2 = apply instantly, cleared each frame
 *   cGpffffb6e4         manual free-look active
 *   cGpffffb6e6         disables the ground clamp
 *   cGpffffad08         auto-focus direction: -1, 0, +1
 *   fGpffffacfc         horizontal follow speed accumulator
 *   fGpffffad00         vertical follow speed accumulator
 *   fGpffffad04         yaw speed accumulator
 *   uGpffffad0c         idle timer, ticks; threshold 0x1c200
 *   uGpffffad09         idle-camera sub-state
 *
 * Input globals:
 *   uGpffffb684  raw held pad bits    (DAT_003555f4)
 *   uGpffffb686  raw pressed pad bits (DAT_003555f6)
 *   fGpffffb674  analog stick angle
 *   fGpffffb678  analog stick magnitude
 *   fGpffffb680  previous analog magnitude
 *   iGpffffb64c  elapsed frame ticks  (DAT_003555bc)
 *
 * Callees:
 *   FUN_002184e8  idle auto-camera
 *   FUN_00218270  manual free-look update
 *   FUN_00218710  manual camera modes 0x1b-0x1e (see analyzed/update_manual_camera.c)
 *   FUN_00217b88  script camera apply
 *   FUN_00216608  hypot(x, y)
 *   FUN_00305408  atan2(y, x)
 *   FUN_00305130  cos
 *   FUN_00305218  sin
 *   FUN_002166e8  signed shortest angular difference
 *   FUN_0023a320  angular lerp toward a goal, clamped by a rate
 *   FUN_00227798  terrain height query at (x, y, z)
 *   FUN_00216510  publish view direction / rebuild the view matrix
 *
 * Button bits used here (raw pad, post-CONCAT11 inversion in FUN_0023b5d8):
 *   0x0004 = L1    rotate the camera one way   (uGpffffb684 & 4  -> mode 2)
 *   0x0008 = R1    rotate the camera the other (uGpffffb684 & 8  -> mode 1)
 *   0x0400 = R3    press toggles manual free-look (uGpffffb686 & 0x400)
 * Note the low byte is byte-swapped relative to the usual PS2 constants, so
 * 0x04/0x08 are the shoulder buttons rather than d-pad bits.
 *
 * UNVERIFIED / open:
 * - DAT_0058bebc bit 0, DAT_0058bf10 (== 10 and == 0 are both tested), and
 *   DAT_0058c0ea (a countdown that forces the yaw deadzone branch) are gates
 *   whose owners have not been traced.
 * - Modes 3..6 are reachable only through paths not yet analyzed; only the
 *   default branch and modes 1/2 are exercised by normal field movement.
 */

/* ---- scratchpad frame layout (locals, not persistent) -------------------- */
/* [0x00] clampYawBySpeed   nonzero => limit yaw step by the speed accumulator */
/* [0x01] eyeDeltaX                                                            */
/* [0x02] eyeDeltaY                                                            */
/* [0x03] eyeDeltaZ                                                            */
/* [0x04] targetToEyeX                                                         */
/* [0x05] targetToEyeY                                                         */
/* [0x06] horizontalDistance                                                   */
/* [0x07] distanceError, then the smoothed signed step                         */
/* [0x08] |distanceError| clamped by accel/decel                               */
/* [0x09] |verticalError| clamped by accel/decel                               */
/* [0x0a] desiredEyeX                                                          */
/* [0x0b] desiredEyeY                                                          */
/* [0x0c] currentYaw = atan2(targetToEyeY, targetToEyeX)                       */
/* [0x0d] yawStep                                                              */
/* [0x0e] |yawStep| clamped                                                    */
/* [0x0f] yawAccelThisFrame                                                    */
/* [0x10] yawMaxSpeedThisFrame                                                 */
/* [0x11] targetX                                                              */
/* [0x12] targetY                                                              */
/* [0x13] targetZ                                                              */
/* [0x14] targetHeightOffset                                                   */

void update_field_camera(void)
{
  camera_frame *f; /* scratchpad frame, released on every exit path */

  f = (camera_frame *)DAT_70000000;
  DAT_70000000 += 0x18;
  if (DAT_70000000 > 0x70003fff)
  {
    FUN_0026bf90(0); /* scratchpad exhausted */
  }

  f->clampYawBySpeed = 0;
  f->targetX = DAT_0058bed0;
  f->targetY = DAT_0058bed4;
  f->targetZ = DAT_0058bed8;
  f->targetHeightOffset = iGpffff8230; /* 0.8 above the entity origin */
  f->eyeDeltaX = 0;
  f->eyeDeltaY = 0;
  f->eyeDeltaZ = 0;

  /* --- 1. idle timeout -> auto-camera ------------------------------------ */
  /* Only while not in a cutscene/script sub-mode. The timer accumulates raw
   * frame ticks, so 0x1c200 / 0x20 = 3600 frames = 60 s at a steady 60 fps. */
  if (cGpffffb6e2 == 0 && cGpffffb6e1 == 0)
  {
    if (uGpffffb684 == 0 && fGpffffb680 == 0.0f && fGpffffb678 == 0.0f)
    {
      uGpffffad0c += iGpffffb64c; /* no buttons held and the stick is centred */
    }
    else
    {
      uGpffffad0c = 0;
    }

    if (uGpffffad0c > 0x1c200)
    {
      FUN_002184e8(&uGpffffad09);
      goto release_frame;
    }
  }
  else
  {
    uGpffffad0c = 0;
  }
  uGpffffad09 = 0;

  /* --- 2. manual free-look acquire / release ----------------------------- */
  if (cGpffffb6e2 == 0 && cGpffffb6e1 < 0x1f && iGpffffb0e4 == 0 && (DAT_0058bebc & 1) != 0)
  {
    if (cGpffffb6e4 == 0)
    {
      if ((uGpffffb686 & 0x400) != 0 && DAT_0058bf10 == 0)
      {
        cGpffffb6e4 = 1;
        DAT_0058beb8 |= 1;
        DAT_0058bf0c = fGpffffb6d4; /* remember the yaw we entered free-look at */
      }
    }
    else if ((uGpffffb686 & 0x400) != 0 || cGpffffb6e4 == 2)
    {
      /* Release: nudge the eye back along the current view direction by 0.1. */
      DAT_0058c0a8 -= DAT_0058bea0 * fGpffff8234;
      DAT_0058c0ac -= DAT_0058bea4 * fGpffff8234;
      DAT_0058c0b0 -= DAT_0058bea8 * fGpffff8234;
      cGpffffb6e4 = 0;
      DAT_0058beb8 &= 0xfffe;
    }

    if (cGpffffb6e4 != 0)
    {
      FUN_00218270();
      goto release_frame;
    }
  }
  else
  {
    if (cGpffffb6e4 != 0 && DAT_0058bf10 != 10)
    {
      DAT_0058beb8 &= 0xfffe;
    }
    cGpffffb6e4 = 0;
  }

  /* --- 3. sub-mode dispatch ---------------------------------------------- */
  if (cGpffffb6e1 != 0)
  {
    if (cGpffffb6e1 == 0x20)
    {
      /* Snap: publish the raw target and skip all smoothing. */
      DAT_0058be90 = f->targetX;
      DAT_0058be94 = f->targetY;
      DAT_0058be98 = f->targetZ + f->targetHeightOffset;
      goto publish_orientation;
    }

    if (cGpffffb6e1 < 0x21)
    {
      if (cGpffffb6e1 > 0x1a && cGpffffb6e1 < 0x1f)
      {
        FUN_00218710(); /* manual camera modes 0x1b..0x1e */
      }
    }
    else if (cGpffffb6e1 == 0x23)
    {
      FUN_00217b88(1); /* script-driven camera */
    }
    goto release_frame;
  }

  /* --- 4. camera mode from the raw pad ----------------------------------- */
  f->yawAccelThisFrame = iGpffff8238;    /* 0.25 deg */
  f->yawMaxSpeedThisFrame = iGpffff823c; /* 4 deg    */
  if (cGpffffb6e2 == 0 && iGpffffb0e4 == 0 && DAT_0058bf10 != 10)
  {
    if ((uGpffffb684 & 8) != 0)
    {
      bGpffffb6e0 = 1; /* rotate one way while held */
    }
    else if ((uGpffffb684 & 4) != 0)
    {
      bGpffffb6e0 = 2; /* rotate the other way */
    }
  }

  /* --- 5. yaw deadzone ---------------------------------------------------
   * With the stick barely deflected, or deflected within 60..120 degrees of
   * the camera, fall into the speed-limited branch. DAT_0058c0ea is a
   * countdown that forces that branch for a few frames after some event. */
  if (DAT_0058c0ea < 1)
  {
    if (fGpffffb678 < 40.0f)
    {
      f->clampYawBySpeed = 1;
    }
    else if (ABS(fGpffffb674) > fGpffff8240 && ABS(fGpffffb674) < fGpffff8244)
    {
      f->clampYawBySpeed = 1;
    }
  }
  else
  {
    DAT_0058c0ea -= 1;
  }
  DAT_0055f8c8 = f->targetX;
  DAT_0055f8cc = f->targetY;

  /* --- 6. rate-limited vertical follow of the target ----------------------
   * Both ladders below are the same shape: take the signed error, then pick
   * the largest step whose threshold the error clears, so the follow speed
   * rises in fixed increments with distance instead of continuously. The
   * outermost band (|error| > 3 for the target, > 2 for the eye) falls back
   * to a proportional term. cGpffffb6e3 == 0 means "smooth"; anything else
   * snaps and zeroes the accumulators. */
  if (cGpffffb6e3 == 0)
  {
    float error = (f->targetZ + fGpffffbafc) - DAT_0055f8d0;
    float step = error;

    if (error < 0.0f)
    {
      if (error <= fGpffff8248) /* -0.01 */
      {
        step = fGpffff8248;
        if (error <= fGpffff824c) /* -0.6 */
        {
          step = fGpffff8250; /* -0.02 */
          if (error <= -2.0f)
          {
            step = fGpffff8254; /* -0.04 */
            if (error <= -3.0f)
            {
              step = (error + 3.0f) - fGpffff8258; /* proportional beyond -3 */
            }
          }
        }
      }
      DAT_0055f8d0 += step;
      step = f->targetHeightOffset;
    }
    else if (error > 0.0f)
    {
      if (error >= fGpffff825c) /* 0.01 */
      {
        step = fGpffff825c;
        if (error >= fGpffff8260) /* 0.6 */
        {
          step = fGpffff8264; /* 0.02 */
          if (error >= 2.0f)
          {
            step = fGpffff8268; /* 0.04 */
            if (error >= 3.0f)
            {
              step = (error - 3.0f) + fGpffff826c;
            }
          }
        }
      }
      DAT_0055f8d0 += step;
      step = f->targetHeightOffset;
    }
    else
    {
      step = f->targetHeightOffset;
    }

    /* Same ladder again, now moving the published look-at height. */
    error = (f->targetZ + step) - DAT_0058be98;
    step = error;
    if (error < 0.0f)
    {
      if (error <= fGpffff8270) /* -0.02 */
      {
        step = fGpffff8270;
        if (error <= fGpffff8274) /* -0.3 */
        {
          step = fGpffff8278; /* -0.03 */
          if (error <= -1.0f)
          {
            step = fGpffff827c; /* -0.05 */
            if (error <= -2.0f)
            {
              step = (error + 2.0f) - fGpffff8280;
            }
          }
        }
      }
      DAT_0058be98 += step;
    }
    else if (error > 0.0f)
    {
      if (error >= fGpffff8284) /* 0.02 */
      {
        step = fGpffff8284;
        if (error >= fGpffff8288) /* 0.3 */
        {
          step = fGpffff828c; /* 0.03 */
          if (error >= 1.0f)
          {
            step = fGpffff8290; /* 0.05 */
            if (error >= 2.0f)
            {
              step = (error - 2.0f) + fGpffff8294;
            }
          }
        }
      }
      DAT_0058be98 += step;
    }
  }
  else
  {
    fGpffffacfc = 0.0f;
    fGpffffad00 = 0.0f;
    DAT_0055f8d0 = f->targetZ + fGpffffbafc;
    DAT_0058be98 = f->targetZ + f->targetHeightOffset;
  }

  /* --- 7. horizontal distance and its smoothing -------------------------- */
  f->targetToEyeX = DAT_0055f8c8 - DAT_0058c0a8;
  f->targetToEyeY = DAT_0055f8cc - DAT_0058c0ac;
  DAT_0058be90 = DAT_0055f8c8;
  DAT_0058be94 = DAT_0055f8cc;
  f->horizontalDistance = FUN_00216608(f->targetToEyeX, f->targetToEyeY);
  f->currentYaw = FUN_00305408(f->targetToEyeY, f->targetToEyeX);
  f->distanceError = f->horizontalDistance - fGpffffbaf8; /* desired distance */

  if (f->distanceError != 0.0f)
  {
    if (cGpffffb6e3 == 2)
    {
      /* snap: use the raw error */
    }
    else
    {
      /* Quarter of the error per nominal frame, then accel/decel limited. */
      f->distanceError = f->distanceError * (float)iGpffffb64c * 0.03125f * 0.25f;
      f->distanceStep = ABS(f->distanceError);
      if (f->distanceStep < fGpffff8298) /* 0.016 deadzone */
      {
        f->distanceStep = 0.0f;
      }

      if (fGpffffacfc < f->distanceStep)
      {
        if (fGpffffacfc + fGpffff829c < f->distanceStep) /* accel 0.04 */
        {
          f->distanceStep = fGpffffacfc + fGpffff829c;
        }
        if (f->distanceStep > 0.25f)
        {
          f->distanceStep = 0.25f;
        }
      }
      else if (f->distanceStep < fGpffffacfc)
      {
        if (f->distanceStep < fGpffffacfc - fGpffff82a0) /* decel 0.08 */
        {
          f->distanceStep = fGpffffacfc - fGpffff82a0;
        }
        if (f->distanceStep < 0.0f)
        {
          f->distanceStep = 0.0f;
        }
      }

      fGpffffacfc = f->distanceStep;
      f->distanceError = (f->distanceError < 0.0f) ? -fGpffffacfc : fGpffffacfc;
    }

    if (f->distanceError != 0.0f)
    {
      f->eyeDeltaX = f->distanceError * FUN_00305130(f->currentYaw); /* cos */
      f->eyeDeltaY = f->distanceError * FUN_00305218(f->currentYaw); /* sin */
    }
  }

  /* --- 8. mode switch: choose this frame's yaw accel / max / goal --------- */
  switch (bGpffffb6e0 - 1)
  {
  case 0: /* held rotate one way */
    cGpffffad08 = -1;
    f->yawAccelThisFrame = (float)iGpffffb64c * fGpffff82a4 * 0.03125f; /* 0.375 deg */
    f->yawStep = (float)iGpffffb64c * fGpffff82a8 * 0.03125f;           /* -30 deg   */
    f->yawMaxSpeedThisFrame = (float)iGpffffb64c * fGpffff82b4 * 0.03125f;
    f->clampYawBySpeed = 0;
    break;

  case 1: /* held rotate the other way */
    cGpffffad08 = 1;
    f->yawAccelThisFrame = (float)iGpffffb64c * fGpffff82ac * 0.03125f;
    f->yawStep = (float)iGpffffb64c * fGpffff82b0 * 0.03125f; /* +30 deg */
    f->yawMaxSpeedThisFrame = (float)iGpffffb64c * fGpffff82b4 * 0.03125f;
    f->clampYawBySpeed = 0;
    break;

  case 2:
    f->yawStep = FUN_002166e8(f->currentYaw, fGpffffb6d4);
    cGpffffad08 = 0;
    break;

  case 3:
    fGpffffad04 -= f->yawAccelThisFrame + f->yawAccelThisFrame;
    if (fGpffffad04 < 0.0f)
    {
      fGpffffad04 = 0.0f;
    }
    /* fallthrough */

  default:
    if (cGpffffad08 == 0)
    {
      /* Idle auto-focus: ease the camera behind the entity very slowly. */
      float toEntity = FUN_002166e8(f->currentYaw, DAT_0058bf0c);
      f->yawAccelThisFrame = (float)iGpffffb64c * fGpffff82d4 * 0.03125f; /* 0.05 deg */
      f->yawMaxSpeedThisFrame = (float)iGpffffb64c * fGpffff82d8 * 0.03125f; /* 1.5 deg */
      f->yawStep = toEntity * 0.125f * (float)iGpffffb64c * 0.03125f;
    }
    else if (fGpffffad04 == 0.0f)
    {
      cGpffffad08 = 0;
      f->yawStep = 0.0f;
    }
    else
    {
      f->yawAccelThisFrame = (float)iGpffffb64c * fGpffff82c4 * 0.03125f;    /* 0.25 deg */
      f->yawMaxSpeedThisFrame = (float)iGpffffb64c * fGpffff82c8 * 0.03125f; /* 6 deg */
      f->yawStep = (float)iGpffffb64c * ((cGpffffad08 >= 0) ? fGpffff82cc : fGpffff82d0) * 0.03125f;
      f->clampYawBySpeed = 1;
    }
    break;

  case 4:
    f->yawStep = (float)iGpffffb64c * fGpffff82b8 * 0.03125f; /* -1 deg */
    cGpffffad08 = -1;
    f->clampYawBySpeed = 0;
    break;

  case 5:
    /* Ease toward uGpffffbb00 with a rate proportional to the error. */
    {
      float toGoal = ABS(FUN_002166e8(fGpffffb6d4, uGpffffbb00));
      f->yawStep = toGoal;
      if (toGoal < fGpffff82bc) /* 1 deg deadzone */
      {
        uGpffffacf8 = 0;
      }
      else if (toGoal > fGpffff82c0) /* 40 deg cap */
      {
        f->yawStep = fGpffff82c0;
      }
      f->yawStep = fGpffffb6d4 + FUN_0023a320(fGpffffb6d4, uGpffffbb00, f->yawStep * 0.25f);
      f->desiredEyeX = f->targetX - f->horizontalDistance * FUN_00305130(f->yawStep);
      f->desiredEyeY = f->targetY - f->horizontalDistance * FUN_00305218(f->yawStep);
      f->eyeDeltaX += f->desiredEyeX - DAT_0058c0a8;
      f->eyeDeltaY += f->desiredEyeY - DAT_0058c0ac;
      f->yawStep = 0.0f;
      cGpffffad08 = 0;
    }
    break;
  }

  /* --- 9. apply the yaw step, accel- and max-speed-limited ---------------- */
  if (f->yawStep == 0.0f)
  {
    fGpffffad04 = 0.0f;
  }
  else
  {
    f->yawStepMagnitude = ABS(f->yawStep);

    if (f->clampYawBySpeed != 0)
    {
      /* Leave enough room to decelerate to a stop from the current speed. */
      float decelBudget = fGpffffad04 - 2.0f * (f->yawAccelThisFrame * (float)iGpffffb64c * 0.03125f);
      if (decelBudget < f->yawStepMagnitude)
      {
        f->yawStepMagnitude = (decelBudget < 0.0f) ? 0.0f : decelBudget;
      }
    }

    if (fGpffffad04 < f->yawStepMagnitude)
    {
      fGpffffad04 += f->yawAccelThisFrame * (float)iGpffffb64c * 0.03125f;
      if (fGpffffad04 < f->yawStepMagnitude)
      {
        f->yawStepMagnitude = fGpffffad04;
      }
      if (f->yawMaxSpeedThisFrame * (float)iGpffffb64c * 0.03125f < f->yawStepMagnitude)
      {
        f->yawStepMagnitude = f->yawMaxSpeedThisFrame * (float)iGpffffb64c * 0.03125f;
      }
    }

    fGpffffad04 = f->yawStepMagnitude;
    f->yawStep = (f->yawStep < 0.0f) ? -fGpffffad04 : fGpffffad04;

    if (f->yawStep != 0.0f)
    {
      /* Orbit the eye to the new yaw at the current horizontal distance. */
      f->yawStep += f->currentYaw;
      f->desiredEyeX = f->targetX - f->horizontalDistance * FUN_00305130(f->yawStep);
      f->desiredEyeY = f->targetY - f->horizontalDistance * FUN_00305218(f->yawStep);
      f->eyeDeltaX += f->desiredEyeX - DAT_0058c0a8;
      f->eyeDeltaY += f->desiredEyeY - DAT_0058c0ac;
    }
  }

  if (f->eyeDeltaX != 0.0f || f->eyeDeltaY != 0.0f)
  {
    DAT_0058c0a8 += f->eyeDeltaX;
    DAT_0058c0ac += f->eyeDeltaY;
    DAT_0058c0d4 = FUN_00227798(DAT_0058c0a8, DAT_0058c0ac, DAT_0058c0b0);
  }

  /* --- 10. vertical follow, with a ground clamp --------------------------- */
  {
    float goalZ = DAT_0055f8d0;

    /* Lift the eye to the terrain height when it is within 2 units below it. */
    if (fGpffffad24 > 0.0f && cGpffffb6e6 == 0 && DAT_0055f8d0 < DAT_0058c0d4 &&
        DAT_0058c0d4 - 2.0f < DAT_0055f8d0)
    {
      goalZ = DAT_0058c0d4;
    }

    f->eyeDeltaZ = goalZ - DAT_0058c0b0;
    if (f->eyeDeltaZ == 0.0f)
    {
      fGpffffad00 = 0.0f;
    }
    else if (cGpffffb6e3 == 2)
    {
      fGpffffad00 = 0.0f;
      DAT_0058c0b0 += f->eyeDeltaZ;
    }
    else
    {
      f->eyeDeltaZ = f->eyeDeltaZ * (float)iGpffffb64c * 0.03125f * 0.25f;
      f->verticalStep = ABS(f->eyeDeltaZ);

      if (fGpffffad00 < f->verticalStep)
      {
        if (fGpffffad00 + fGpffff82dc < f->verticalStep) /* accel 0.03 */
        {
          f->verticalStep = fGpffffad00 + fGpffff82dc;
        }
        if (f->verticalStep > 0.25f)
        {
          f->verticalStep = 0.25f;
        }
      }
      else if (f->verticalStep < fGpffffad00)
      {
        if (f->verticalStep < fGpffffad00 - fGpffff82e0) /* decel 0.06 */
        {
          f->verticalStep = fGpffffad00 - fGpffff82e0;
        }
        if (f->verticalStep < 0.0f)
        {
          f->verticalStep = 0.0f;
        }
      }

      fGpffffad00 = f->verticalStep;
      f->eyeDeltaZ = (f->eyeDeltaZ < 0.0f) ? -fGpffffad00 : fGpffffad00;
      if (f->eyeDeltaZ != 0.0f)
      {
        DAT_0058c0b0 += f->eyeDeltaZ;
      }
    }
  }

publish_orientation:
  /* --- 11. rebuild the view direction and the yaw/pitch globals ----------- */
  {
    float dirX = DAT_0058be90 - DAT_0058c0a8;
    float dirY = DAT_0058be94 - DAT_0058c0ac;
    float dirZ = DAT_0058be98 - (DAT_0058c0b0 + fGpffff82e4); /* eye sits 0.4 low */
    float horizontal;
    float pitchGoal;

    fGpffffb6d4 = FUN_00305408(dirY, dirX);

    horizontal = FUN_00216608(dirX, dirY);
    pitchGoal = FUN_00305408(dirZ, horizontal);
    fGpffffb6d8 += FUN_002166e8(fGpffffb6d8, pitchGoal); /* pitch eases, yaw snaps */

    fGpffffb6d4 = FUN_00305408(dirY, dirX);

    DAT_0058bea0 = dirX;
    DAT_0058bea4 = dirY;
    DAT_0058bea8 = dirZ;
    FUN_00216510(&DAT_0058bea0);

    DAT_0058be88 = DAT_0058c0b0 + fGpffff82e4;
    DAT_0058be80 = DAT_0058c0a8;
    DAT_0058be84 = DAT_0058c0ac;
    cGpffffb6e3 = 0; /* the snap request is one-shot */
  }

release_frame:
  DAT_70000000 -= 0x18;
}
