# Orphen Native Port Harness

This directory is the start of a native PC runtime for `Orphen: Scion of Sorcery`. The goal is not to run the original PS2 executable directly. The early goal is a host process that can load original game data, reproduce verified game systems one at a time, and compare behavior against PCSX2 traces.

The first scaffold uses SDL2 for the platform layer and an OpenGL compatibility context for simple diagnostic rendering.

## Layout

- `src/ported/` - faithful native counterparts to specific original functions. Files here should keep the original `FUN_*` identity visible and avoid harness concerns.
- `src/harness/` - PC-only viewer/debug code plus host-side disc resource indexing for extracted game data.
- `src/platform/` - SDL window, input, OpenGL context, frame presentation.
- `src/runtime/` - portable game/runtime state that should eventually host analyzed systems from `../analyzed/`.
- `src/runtime/ps2_memory.h` - a small fake EE RAM helper for systems that still depend on PS2-style absolute addresses.
- `attic/` - written but deliberately disconnected from the build. Nothing in `src/` includes it and `CMakeLists.txt` does not reference it. See `attic/README.md`.

## Current Milestone

The current executable opens a resizable SDL window, creates an OpenGL context, and can load one map either from an already-decoded PSM2 file or directly from `MCB0.BIN`/`MCB1.BIN` in an extracted disc directory. PSM2 files still flow through the ported `loadDecodedPsm2` (`src/FUN_0022b5a8.c`) / `buildPsm2DerivedGeometry` (`src/FUN_0022c6e8.c`) path. Disc-loaded scenes flow through `SceneResourceProvider`, which owns the selected MCB scene bundle, indexes resource records by category/id, and lets the map loader decode the first PSM2 plus adjacent BMPA texture pages.

Runtime update owns an original-shaped lead player entity that starts at the scene origin and runs a ported slice of the native field movement/jump/collision path (`FUN_00225bf0`, `FUN_00252d88`, `FUN_00256bb8`, `FUN_002534d8`, `FUN_00253468`, `FUN_00253488`, `FUN_00256ab0`, `FUN_00227390`, `FUN_002262c0`). Collision samples the PSM2 `0x78` terrain records using the original `0x800` sample bit, terrain reject masks, required footprint flag overlap, four-corner radius sampling, step-height acceptance, and simple axis fallback for sliding. Player movement is now camera-relative using the `FUN_00256ab0` camera/input angle relationship, with grounded movement using the original normal run scalar (`fGpffff8a4c = 0.045` per nominal frame) and jump startup applying the original vertical seed (`DAT_00355000 = 0.053`) from airborne substate `0x0C`; the lead entity gravity field uses the debug `JUMP TEST` `G_FORCE 00075` value (`0.00075` at the menu's x100000 scale). The camera is now a port of the original driver rather than a harness approximation -- see Camera below.

The SCR interpreter/probe work has been removed from the normal load/update path. Script blobs can still be inspected through the scene tree, but the executable no longer runs a bootstrap SCR VM trace, mutates terrain from scripts, or installs a script-driven camera at startup. The previous PSC3 wireframe gallery was also removed from the active harness; PSC3 records are still visible in the resource tree, but they are not rendered as placeholder scene objects.

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
camera (`FUN_00217b88`), manual modes 0x1b-0x1e (`FUN_00218710`, analyzed), and
camera collision beyond the ground clamp.

## Timing Model

The simulation runs on a fixed 60 Hz step, decoupled from the render rate. `main`
accumulates wall-clock time and calls `PortRuntime::update` once per whole step,
capping at 5 steps to skip rather than spiral after a stall. Edge-triggered input
(jump, map cycle, wireframe) fires on exactly one step per render frame.

This matters because every ported constant is per-frame, not per-second, and
because state counters such as the entity `+0xA8` substate frame advance once per
update. The previous wall-clock scaling changed the 4-frame jump startup from
66 ms to 27 ms on a 144 Hz display.

The original is not literally fixed-step either. `FUN_002000c0` recomputes
`DAT_003555bc` every frame from the EE performance counter, rounds it to whole
60 Hz frames at `0x20` ticks each, and clamps it to `[0x20, 0x80]`:

```c
DAT_003555bc = (PCR0 << 5) / 0x4b125c + 0x10U & 0xffffffe0;
```

Both axes scale by it — horizontal as `iGpffffb64c * fGpffff8a4c * 0.03125`
(`FUN_00256bb8`) and vertical as `dt = (float)DAT_003555bc * 0.125` feeding
`+0x38 += v*dt - (g*dt)*dt*0.5; v -= g*dt` (`FUN_002262c0`). So the tick count is
carried through the port as a parameter (`ported/original_frame_timing.h`) rather
than folded into the constants, and the harness passes the nominal `0x20`. A later
slice can reproduce dropped-frame behavior by widening the tick count, which is
what the original does — one longer update, not extra sub-steps.

`--frames N` is exactly deterministic: two runs produce byte-identical output.

## Build

Prerequisites:

- CMake 3.24 or newer
- A C++20 compiler
- OpenGL development libraries
- SDL2, or network access so CMake can download SDL2 via `FetchContent`

From the repository root:

```sh
cmake -S port -B port/build -DORPHEN_PORT_FETCH_SDL2=ON
cmake --build port/build
```

On Windows with a multi-config generator, the executable is usually under `port/build/Debug/orphen_port.exe` or `port/build/Release/orphen_port.exe`.

The MSVC wrapper uses `NMake Makefiles`, so its executable is under `port/build/msvc-Debug/orphen_port.exe` or `port/build/msvc-Release/orphen_port.exe`.

If using Visual Studio/MSVC from Git Bash, use the repo-local wrapper so MSYS path conversion does not mangle `cmd.exe /c` flags or quoted Visual Studio paths:

```sh
cmd.exe //c port\\check-msvc.bat
cmd.exe //c port\\build-msvc.bat Debug
```

From Command Prompt or PowerShell, use the normal Windows path form:

```bat
port\check-msvc.bat
port\build-msvc.bat Debug
```

## Running The PSM2 Slice

From the repository root after building:

```sh
port/build/msvc-Debug/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2
```

Or load from extracted disc files in a directory containing `MCB0.BIN` and `MCB1.BIN`:

```sh
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e012
```

To validate the loader without opening a window:

```sh
port/build/msvc-Debug/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2 --load-only
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e012 --load-only
```

To inspect the resources loaded by a disc scene:

```sh
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e012 --scene-tree --load-only
```

The scene tree currently groups MCB bundle records by category and prints record ids, bundle offsets, packed/decoded sizes, and known decoded signatures such as PSM2, BMPA, SCR, and PSC3. `s01_e024` is a useful early exploratory scene because it is much smaller than `s01_e012` and appears to be a debug scene:

```sh
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e024 --load-only
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e024 --frames 60
```

`--frames` runs the runtime update loop without opening a window. The old `--script-frames` spelling is still accepted as a compatibility alias, but it no longer executes script frames.

A gamepad is used when one is present: left stick moves, shoulder buttons orbit
the camera, A jumps. The keyboard remains available as a digital fallback.

Controls:

- `W/A/S/D` moves the runtime lead player relative to the current camera yaw.
- `Space` jumps when the lead player is grounded.
- `J/L` orbits the player camera, mapped onto the original's L1/R1 raw pad bits (0x04/0x08). With no player active they rotate the free viewer camera instead.
- `I/K` adjusts pitch in free viewer mode.
- Left/right arrows cycle maps when running from `--disc-root`.
- `Q/E` zoom out/in in free viewer mode. The player follow camera currently uses the original normal field camera distance.
- `R` resets the viewer camera.
- `F` toggles wireframe.

The origin axis indicator uses red for game +X, blue for game +Y, and green for game +Z. The viewer currently maps game `(x, y, z)` to viewer `(x, z, -y)`.

The current lead player is drawn in magenta, and its current ground triangle is highlighted in yellow. The console prints the primitive index, triangle index, height, leading word, and terrain flags when it enters a new ground triangle.

## Suggested Next Slices

1. Promote the provider-backed loaded scene into a runtime-owned `SceneState` rather than letting `MapViewer` own the active scene.
2. Replace keyboard-derived movement vectors with the original controller globals and analog smoothing path from `FUN_0023b5d8`/`FUN_00256ab0`.
3. Port the directional entity/body blocker helpers (`FUN_00228380`, `FUN_002285d8`, `FUN_00228838`, `FUN_00228a90`) and dynamic entity support helper `FUN_00228cf0`.
4. Rebuild camera behavior from the original camera state/update functions before adding a new follow camera.
5. Bring SCR behavior back only as narrow, verified opcode slices tied to a concrete game behavior, not as a broad bootstrap VM requirement.
