# Orphen Native Port Harness

This directory is the start of a native PC runtime for `Orphen: Scion of Sorcery`. The goal is not to run the original PS2 executable directly. The early goal is a host process that can load original game data, reproduce verified game systems one at a time, and compare behavior against PCSX2 traces.

The first scaffold uses SDL2 for the platform layer and an OpenGL compatibility context for simple diagnostic rendering.

## Layout

- `src/ported/` - faithful native counterparts to specific original functions. Files here should keep the original `FUN_*` identity visible and avoid harness concerns.
- `src/harness/` - PC-only viewer/debug code plus host-side disc resource indexing for extracted game data.
- `src/platform/` - SDL window, input, OpenGL context, frame presentation.
- `src/runtime/` - portable game/runtime state that should eventually host analyzed systems from `../analyzed/`.
- `src/runtime/ps2_memory.h` - a small fake EE RAM helper for systems that still depend on PS2-style absolute addresses.

## Current Milestone

The current executable opens a resizable SDL window, creates an OpenGL context, and can load one map either from an already-decoded PSM2 file or directly from `MCB0.BIN`/`MCB1.BIN` in an extracted disc directory. PSM2 files still flow through the ported `loadDecodedPsm2` (`src/FUN_0022b5a8.c`) / `buildPsm2DerivedGeometry` (`src/FUN_0022c6e8.c`) path. Disc-loaded scenes flow through `SceneResourceProvider`, which owns the selected MCB scene bundle, indexes resource records by category/id, and lets the map loader decode the first PSM2 plus adjacent BMPA texture pages. Runtime update now owns an original-shaped lead player entity that starts at the scene origin, maps keyboard input into original action bits, and runs a ported slice of the native field movement/jump/collision path (`FUN_00225bf0`, `FUN_00252d88`, `FUN_00256bb8`, `FUN_002534d8`, `FUN_00227390`, `FUN_00227840`, `FUN_002262c0`). Collision samples the PSM2 `0x78` terrain records using the original `0x800` sample bit, terrain reject masks, required footprint flag overlap, four-corner radius sampling, step-height acceptance, and simple axis fallback for sliding. Disc scenes also copy the first category `0x0001` script blob into runtime-owned trace state, parse the five SCR header entry offsets used by `FUN_0025b6d0`, `FUN_0025b728`, `FUN_0025b778`, `FUN_0025b978`, and `FUN_0025b9a8`, and run a bounded side-effect-free bootstrap trace over structural opcodes and known setup-op operand shapes. The follow camera uses a runtime over-the-shoulder rig feeding the ported original camera eye/target core (`FUN_00217d40`, `FUN_00217d10`, `FUN_00217a70`).

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

The scene tree currently groups MCB bundle records by category and prints record ids, bundle offsets, packed/decoded sizes, and known decoded signatures such as PSM2 and BMPA. At runtime, the category `0x0001` script record reports its decoded size, four-byte signature, five entry offsets, and a bounded bootstrap trace. The trace now carries a minimal VM state for work variables, flags, object registers, and script slots; evaluates conditionals and expression operands; and applies terrain opcodes `0xA4`, `0xA5`, and `0xA6` to the loaded PSM2 state. For `s01_e012`, all five bootstrap entrypoints complete and entry1 applies `0xA4` to `DRecord80` terrain flags. Object methods, spawned entities, and coroutine slots are still modeled only enough to consume operands and keep bootstrap flow moving.

Controls:

- `W/A/S/D` moves the runtime lead player placeholder relative to the current camera view.
- `Space` jumps when the lead player is grounded.
- `J/L` rotates the over-the-shoulder camera around the lead player; when released, it eases back toward the player's facing direction. `I/K` adjusts pitch.
- Left/right arrows cycle maps when running from `--disc-root`.
- `Q/E` zoom out/in.
- `R` resets the camera.
- `F` toggles wireframe.

The origin axis indicator uses red for game +X, blue for game +Y, and green for game +Z. The viewer currently maps game `(x, y, z)` to viewer `(x, z, -y)`.

The current lead player placeholder is drawn in magenta, and its current ground triangle is highlighted in yellow. The console prints the primitive index, triangle index, height, leading word, and terrain flags when it enters a new ground triangle.

Disc-loaded scenes also render every decoded PSC3 model resource as a wireframe debug gallery near the origin. This shows the loaded object/model resources, but it is not faithful scene placement yet; script/entity spawn transforms still need to be decoded.

## Suggested Next Slices

1. Promote the provider-backed loaded scene into a runtime-owned `SceneState` rather than letting `MapViewer` own the active scene.
2. Port the directional entity/body blocker helpers (`FUN_00228380`, `FUN_002285d8`, `FUN_00228838`, `FUN_00228a90`) and dynamic entity support helper `FUN_00228cf0`.
3. Replace keyboard-derived movement vectors with the original controller globals and analog smoothing path from `FUN_0023b5d8`/`FUN_00256ab0`.
4. Port the original camera interpolation helpers (`FUN_00217b88`, `FUN_00217f38`, `FUN_00217fe8`) and let scripts drive the same eye/target camera state.
5. Validate script-driven terrain activation across more scenes, especially paths that require scheduled coroutine slots or object-method side effects before hitting `0xA5`/`0xA6`.
