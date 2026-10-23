# Orphen Native Port Harness

A native PC runtime for `Orphen: Scion of Sorcery`. It does not emulate the PS2
executable: it loads the original disc data and runs hand-ported counterparts of
the original functions, each checked against the disassembly, a PCSX2 save state
or a GS dump. SDL2 is the platform layer and the renderer is fixed-function
OpenGL.

Run with no `--scene` and it boots the title screen (`s12_e010`); START starts a
new game in `s01_e012`. From there the field, scene scripts and cutscenes, the
party, doors and scene transitions, battles (including the crab and mast bosses),
the field menu, equip and area map, music, sound effects, voice and FMV all run
through ported code. What is and is not ported is recorded per subsystem in
`docs/`.

## Layout

- `src/ported/` - faithful native counterparts to specific original functions. Files here should keep the original `FUN_*` identity visible and avoid harness concerns.
- `src/harness/` - PC-only viewer/debug code plus host-side disc resource indexing for extracted game data.
- `src/platform/` - SDL window, input, OpenGL context, frame presentation.
- `src/runtime/` - portable game/runtime state that should eventually host analyzed systems from `../analyzed/`.
- `src/runtime/ps2_memory.h` - a small fake EE RAM helper for systems that still depend on PS2-style absolute addresses.
- `attic/` - written but deliberately disconnected from the build. Nothing in `src/` includes it and `CMakeLists.txt` does not reference it. See `attic/README.md`.
- `docs/` - findings and write-ups, one file per subsystem. Index below.
- `scenarios/` - timed battle tests for `--scenario`; see [s14_e004](docs/scenes/s14_e004-gear-room.md).

## Docs

New findings go in the file for their subsystem, not in this README.

- [Running the port](docs/running.md) - command-line flags, the fly camera, fast forward
- [Debugging tools](docs/debugging-tools.md) - `G` snapshots, entity-pool diffs against EE dumps, POSITION_DISP, the debug HUD
- [Scene script](docs/scene-script.md) - the SCR VM, the cutscene sequencer, how a cutscene is built
- [Entities and the party](docs/entities-and-party.md) - the entity pool, actor behaviour, party following and navigation
- [Physics and collision](docs/physics.md) - standing on things, slopes, collision groups, push-out, jumping
- [Doors, triggers and scene transitions](docs/doors-and-transitions.md)
- [Camera](docs/camera.md)
- [Rendering](docs/rendering.md) - map draw pipeline, blending, texture slots, billboards, the backdrop
- [Lighting](docs/lighting.md) - dynamic point lights, the lantern pool
- [Effects](docs/effects.md) - rain, smoke, the spark shower, the screen smear
- [Player model, close-up rig and bandana](docs/player-model-and-bandana.md)
- [Text, dialogue and subtitles](docs/text-and-subtitles.md)
- [Voice](docs/voice.md) - VOICE.BIN, the voice handshake, cutscene timing
- [Sound effects and music](docs/audio.md)
- [The title screen](docs/title-screen.md)
- [Menus and game modes](docs/menus.md) - field menu, pause, equip, area map
- [Field combat](docs/field-combat.md) - the sword, the magic cast, contact and damage
- [Field damage and death](docs/field-damage-and-death.md) - HP gauge, game over
- Battle: [overview](docs/battle/overview.md), [sword and guard](docs/battle/sword-and-guard.md), [spells](docs/battle/spells.md), [targeting](docs/battle/targeting.md), [enemies](docs/battle/enemies.md)
- Scenes: [s01_e012 chest and flood](docs/scenes/s01_e012-chest-and-flood.md), [s03_e001](docs/scenes/s03_e001.md), [chapter 5 lifts and blocks](docs/scenes/s05-lifts-and-blocks.md), [s14_e001 crab](docs/scenes/s14_e001-crab.md), [s14_e002 mast boss](docs/scenes/s14_e002-mast-boss.md), [s14_e003](docs/scenes/s14_e003.md), [s14_e004 gear room](docs/scenes/s14_e004-gear-room.md), [s14_e031 spell reward](docs/scenes/s14_e031-spell-reward.md)

## Timing Model

The simulation runs on a fixed 60 Hz step, decoupled from the render rate. `main`
accumulates wall-clock time and calls `PortRuntime::update` once per whole step,
capping at 5 steps to skip rather than spiral after a stall. Edge-triggered input
(the newly-pressed pad word, `DAT_003555F6`) fires on exactly one step per render
frame, as the original's once-per-tick `held & ~previous` does.

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

The MSVC wrapper uses `NMake Makefiles`, so its executable is under
`port/build/msvc-<config>/orphen_port.exe`. **The examples below use the Release
build** — see "Build a Release binary to play in" for why that matters.

If using Visual Studio/MSVC from Git Bash, use the repo-local wrapper so MSYS path conversion does not mangle `cmd.exe /c` flags or quoted Visual Studio paths:

```sh
cmd.exe //c port\\check-msvc.bat
cmd.exe //c port\\build-msvc.bat Release
```

From Command Prompt or PowerShell, use the normal Windows path form:

```bat
port\check-msvc.bat
port\build-msvc.bat Release
```

The config argument is optional and defaults to `Debug`. Pass it.

### Build a Release binary to play in

`build-msvc.bat` defaults to `Debug`, which is the right build to debug in and
the wrong one to judge the frame rate by. MSVC's debug runtime turns every
`std::vector` index into a checked call and inlines nothing, and this renderer
is a tight loop over `std::vector` doing per-vertex maths on the CPU.

Measured on `s01_e024` on 2026-10-23 with `--frame-stats --render-bench 8`
(per simulation step and per render):

| | Debug | Release |
|---|---|---|
| simulation step | 7.4 ms | 0.29 ms |
| render | 29.4 ms | 6.4 ms |
| **one of each** | **36.8 ms (27 fps)** | **6.7 ms (149 fps)** |

Debug is worse in practice than even that suggests. The fixed-timestep
accumulator in `main()` runs up to `kMaxStepsPerFrame` simulation steps per
rendered frame, so once a frame costs more than 16.6 ms it starts running
several steps to catch up, which makes the frame cost more again. Without
`--render-bench` it settles at about 3.1 steps per frame and roughly 19 fps.

```bat
port\build-msvc.bat Release
port\build\msvc-Release\orphen_port.exe --disc-root disc --scene s01_e024
```

## Performance

`--frame-stats` prints where a frame's time goes, once a second:

```
[frame-stats] 64 renders / 8 frames | work 7.33 ms/frame (136.48 fps ceiling) | sim 0.89 render 6.43 swap-wait 0.05
[frame-stats]   map      0.27 ms  407.00 prim  778.00 tri  101.75 batches  101.75 binds
[frame-stats]   entities 4.31 ms  21.00 models  7041.00 prim  13003.00 tri  299.00 batches  3851.25 gleam-tri
[frame-stats]   drain    1.82 ms  (driver/GPU catching up on queued immediate-mode calls)
[frame-stats]   prologue 0.00 ms  (of which matrix readback 0.00)  clear 0.00
[frame-stats]   sort 0.00  overlay 0.00  hud 0.00  | lighting 50223.00/render  transforms 41015.00/render  sim-steps 3.38/frame
```

`sim` is per displayed frame, so it includes every catch-up step; `sim-steps`
says how many there were. The render phases are per render.

Two things about measuring this are easy to get wrong, and both cost real time
to rediscover:

**The compositor paces the window, and `--no-vsync` does not stop it.** Windows
composites windowed surfaces through the DWM at the refresh rate whether or not
`SDL_GL_SetSwapInterval(0)` succeeds — and here it does not succeed, it returns
an interval of 1. So a single render per present can never measure worse than
"fits in 16.6 ms", and the wait does not politely sit in `swapBuffers`: it
surfaces in whichever GL call fills the driver's queue. That is why the phase
timings appeared to move between the map, the entities and a `glGetFloatv`
while the total stayed pinned at exactly 16.6 ms. `--render-bench N` draws the
frame N times per present so the per-render cost clears that floor; the numbers
above come from `--render-bench 8`.

**`--screenshot <path>[:<frame>]` is the regression test for anything that
touches the draw path.** It runs one simulation step per rendered frame, so the
captured frame is reproducible, writes a PPM and exits. Two builds photographed
at the same frame can be compared byte for byte — which is how the vertex-array
conversion below was shown to change nothing about the picture.

### What the draw path does and why

The renderer is fixed-function, but it is not immediate mode. Immediate mode
costs four GL calls per vertex (`glTexCoord2f`, `glColor4f`, `glFogCoordf`,
`glVertex3f`) and `s01_e024` emits about 59,000 vertices a frame, so roughly
205,000 driver calls. Vertices are packed into a CPU buffer instead and handed
to GL as one `glDrawArrays` per state change — no shaders, no VBOs, nothing
past GL 1.1 except `glFogCoordPointer`, which is resolved next to `glFogCoordf`
and falls back to GL's eye-distance fog when a driver has neither.

Draw order is preserved exactly: the buffer is flushed wherever `glEnd` used to
be, including before each entity, since entities are interleaved into the map by
depth bucket and would otherwise be drawn behind geometry they occlude.

Three things this bought, in the order they were worth finding:

- The camera matrices are kept from `glCameraFor` rather than read back with
  `glGetFloatv`. A get is a sync point, and the pair measured 7 ms per frame
  purely to hand back matrices the frame had just uploaded.
- Map primitives that share a texture page and cull mode batch together. The
  map is 407 visible primitives for 778 triangles, so per-primitive batching
  meant 407 draws and 407 texture binds to move less geometry than one
  character model. It is about 102 of each now.
- The specular pass skips primitives whose corners are all at zero opacity.
  Roughly 43% of its triangles face away from the half-vector and add nothing,
  and they were being submitted at full vertex cost.

## Open structural work

Gaps that belong to no one subsystem. Per-subsystem gaps are in the docs.

- The loaded scene is still owned by the harness: `MapViewer` holds the
  `SceneResourceProvider` (`src/harness/map_viewer.h`), not a runtime-owned
  scene state.
- `FUN_00228cf0`, the pass after the ground sample that can raise the result and
  set entity `+0x0C` bit `0x100`, is not modelled
  (`src/runtime/psm2_ground_query.h`).
