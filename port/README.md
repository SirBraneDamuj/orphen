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

The current executable opens a resizable SDL window, creates an OpenGL context, and can load one map either from an already-decoded PSM2 file or directly from `MCB0.BIN`/`MCB1.BIN` in an extracted disc directory. PSM2 files still flow through the ported `loadDecodedPsm2` (`src/FUN_0022b5a8.c`) / `buildPsm2DerivedGeometry` (`src/FUN_0022c6e8.c`) path. Disc-loaded scenes flow through `SceneResourceProvider`, which owns the selected MCB scene bundle, indexes resource records by category/id, and lets the map loader decode the first PSM2 plus adjacent BMPA texture pages. Runtime update now owns a debug player probe that snaps to a rough PSM2 ground query and reports terrain flags as it crosses triangles. The probe camera uses a runtime over-the-shoulder rig feeding the ported original camera eye/target core (`FUN_00217d40`, `FUN_00217d10`, `FUN_00217a70`).

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

The scene tree currently groups MCB bundle records by category and prints record ids, bundle offsets, packed/decoded sizes, and known decoded signatures such as PSM2 and BMPA. It is a resource-level view; script object/spawn semantics come next.

Controls:

- `W/A/S/D` moves the runtime debug player probe relative to the current camera view.
- `Space` jumps when the probe is grounded.
- `J/L` rotates the over-the-shoulder camera around the probe; when released, it eases back toward the probe's facing direction. `I/K` adjusts pitch.
- Left/right arrows cycle maps when running from `--disc-root`.
- `Q/E` zoom out/in.
- `R` resets the camera.
- `F` toggles wireframe.

The origin axis indicator uses red for game +X, blue for game +Y, and green for game +Z. The viewer currently maps game `(x, y, z)` to viewer `(x, z, -y)`.

The debug probe is drawn in magenta, and its current ground triangle is highlighted in yellow. The console prints the primitive index, triangle index, height, leading word, and terrain flags when the probe enters a new ground triangle.

Disc-loaded scenes also render every decoded PSC3 model resource as a wireframe debug gallery near the origin. This shows the loaded object/model resources, but it is not faithful scene placement yet; script/entity spawn transforms still need to be decoded.

## Suggested Next Slices

1. Promote the provider-backed loaded scene into a runtime-owned `SceneState` rather than letting `MapViewer` own the active scene.
2. Replace the debug probe's direct movement with a native lead-entity/controller field layout that mirrors the original `DAT_0058beb0` entity offsets.
3. Refine the PSM2 ground query into a collision/step-height pass closer to `FUN_002262c0` and `FUN_00227840`.
4. Port the original camera interpolation helpers (`FUN_00217b88`, `FUN_00217f38`, `FUN_00217fe8`) and let scripts drive the same eye/target camera state.
5. Load the scene script record into runtime memory and start a trace-only VM bootstrap.
