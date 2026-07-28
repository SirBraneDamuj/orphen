# Orphen Native Port Harness

This directory is the start of a native PC runtime for `Orphen: Scion of Sorcery`. The goal is not to run the original PS2 executable directly. The early goal is a host process that can load original game data, reproduce verified game systems one at a time, and compare behavior against PCSX2 traces.

The first scaffold uses SDL2 for the platform layer and an OpenGL compatibility context for simple diagnostic rendering.

## Layout

- `src/ported/` - faithful native counterparts to specific original functions. Files here should keep the original `FUN_*` identity visible and avoid harness concerns.
- `src/harness/` - PC-only viewer/debug code that consumes ported runtime data.
- `src/platform/` - SDL window, input, OpenGL context, frame presentation.
- `src/runtime/` - portable game/runtime state that should eventually host analyzed systems from `../analyzed/`.
- `src/runtime/ps2_memory.h` - a small fake EE RAM helper for systems that still depend on PS2-style absolute addresses.

## Current Milestone

The current executable opens a resizable SDL window, creates an OpenGL context, and can load one map either from an already-decoded PSM2 file or directly from `MCB0.BIN`/`MCB1.BIN` in an extracted disc directory. PSM2 files still flow through the ported `FUN_0022b5a8` / `FUN_0022c6e8` path.

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

Controls:

- `W/A/S/D` moves the camera target across the map.
- `I/J/K/L` rotates the camera around the map.
- Left/right arrows cycle maps when running from `--disc-root`.
- `Q/E` zoom out/in.
- `R` resets the camera.
- `F` toggles wireframe.

## Suggested Next Slices

1. Add a resource root option and load original game files without redistributing them.
2. Port the script VM core into `src/runtime/` with trace logging.
3. Compare native script traces against PCSX2 logs for one selected scene.
4. Add debug draw for script/entity positions before attempting faithful model rendering.
