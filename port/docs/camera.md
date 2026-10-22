# Camera

### The zone scripts do not move the camera — module 32 does

Walk onto the `0x20` panel and Orphen froze there for good. The zone takes
control with `0x6D` and then waits, and what it waits for is not in the script
at all.

`PTR_LAB_003252B8` has **33 entries, not 28**, and s01_e014 names **module 32**
— `FUN_0026D838`, whose mode-4 hook is a three-flag handshake:

| flag | meaning |
| --- | --- |
| 718 `0x2CE` | a camera zone is active |
| 719 `0x2CF` | build the move — the hook clears it once built |
| 720 `0x2D0` | run the move — the hook clears it when it finishes |

The script's half is a four-state machine on `work[0x16]`: state 0 takes
control, pins the actor (`+0x04 |= 0x4100`, `+0x06 |= 0x10`) and sets 719;
state 1 waits for 719 to come down and sets 720; state 2 waits for 720; state 3
undoes all of it and releases. **Nothing in the script ever clears either
flag** — scanning the blob for all four flag opcodes across every literal width
finds 719 set once and read once, and that is all. With no hook the wait never
ends.

The move is a two-knot eye curve from wherever the camera is to
`(15.0, lead+0x24, min(lead+0x28 + 1.0, 3.5))` against a one-knot look-at curve
holding the current look point, stepped over `0x3C0` ticks — 30 frames at the
nominal `0x20`. Locked at frame 2, released at frame 35, and the camera is left
pinned afterwards, which is what makes it a fixed-camera region: neither the
script nor the hook ever calls `FUN_00217E18`. Stepping onto the `0x10` panel
next door clears `work[0x14]` and re-arms the chain.

Sibling module 31 (`FUN_0026D640`) is the same handshake with a longer `0x780`
move and a destination chosen by `work[58]`. No scene the port runs names it,
and s01_e014 writes `work[16]` and `work[20]` rather than `work[58]`, so it is
deliberately not guessed at.

Fixing this also moved the hook: `FUN_002239C8:129-130` is
`(*DAT_0032536c)(4); FUN_0025b778();`, so the module runs **before** the script
tick. The port had it a step later, by the player update, which puts a whole
frame between the script raising a flag and the module answering it. All three
regression scenes are byte-identical at 1500 frames either way, but the order is
the original's. Still not gated the way the original gates it —
`FUN_002239C8:126` leaves for `FUN_002241E0` whenever `DAT_00354D2C` is non-zero
at all, where the port's `cutsceneFrame` tests only for the cutscene mode.

## Camera

`src/ported/camera/original_field_camera.*` ports `FUN_00216aa0` with the follow
geometry from `FUN_00216968`. `PortRuntime` owns it; `MapViewer` only consumes a
read-only pose. What it reproduces:

- The derived follow geometry, which is what the original actually follows.
  `FUN_00216968` turns distance 3.0 and pitch 21 degrees into
  `fGpffffbaf8 = 3*cos(21) = 2.800741` horizontal trail and
  `fGpffffbafc = 3*sin(21) - 0.2 = 0.875104` height. The resulting view pitch is
  about -9.6 degrees, not -21; the 21 is only an input to the derivation.
- Both rate-limited vertical follow ladders, which step in fixed increments
  (0.01 / 0.02 / 0.04 and 0.02 / 0.03 / 0.05) and go proportional past the
  outermost band.
- Horizontal distance follow with the 0.016 deadzone and 0.04 / 0.08
  accel/decel limiting, so the camera trails further while you run and closes
  back in when you stop.
- The six-case mode switch. L1 and R1 (raw pad 0x04 / 0x08) orbit at up to
  8 deg/frame, ramping at 0.375 deg/frame; on release the yaw speed decays over
  about 16 frames rather than stopping dead. Idle auto-focus eases behind the
  player at 0.05 deg/frame up to 1.5 deg/frame.
- The ground clamp that lifts the eye out of the floor, via the PSM2 terrain
  query standing in for `FUN_00227798`.

`bGpffffb6e0` is per-frame: `FUN_00251ed8` clears it every frame and
`FUN_00216aa0` only raises it while a shoulder button is held, so what persists
after a release is `cGpffffad08` plus the yaw speed accumulator.

Not ported yet: the idle auto-camera handoff at `uGpffffad0c > 0x1c200`
(reported through `idleTimedOut()`), free-look (`FUN_00218270`), the script
camera's interpolators (`FUN_00217b88`), submode 0x20, and camera collision
beyond the ground clamp. The manual modes 0x1B..0x1E (`FUN_00218710`) are
ported; see the `s05_e018` section.

