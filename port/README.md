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
- `docs/` - findings and write-ups, one file per subsystem. Index below.

## Docs

Split out of this README in pass 1 of a reorganisation: the text was moved
verbatim, so cross-references like "see Camera below" may still point at
sections that now live in another file. Pass 2 audits each file against the code.

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

## Current Milestone

The current executable opens a resizable SDL window, creates an OpenGL context, and can load one map either from an already-decoded PSM2 file or directly from `MCB0.BIN`/`MCB1.BIN` in an extracted disc directory. PSM2 files still flow through the ported `loadDecodedPsm2` (`src/FUN_0022b5a8.c`) / `buildPsm2DerivedGeometry` (`src/FUN_0022c6e8.c`) path. Disc-loaded scenes flow through `SceneResourceProvider`, which owns the selected MCB scene bundle, indexes resource records by category/id, and lets the map loader decode the first PSM2 plus adjacent BMPA texture pages.

Runtime update owns an original-shaped lead player entity that runs a ported slice of the native field movement/jump/collision path (`FUN_00225bf0`, `FUN_00252d88`, `FUN_00256bb8`, `FUN_002534d8`, `FUN_00253468`, `FUN_00253488`, `FUN_00256ab0`, `FUN_00227390`, `FUN_002262c0`). Collision samples the PSM2 `0x78` terrain records using the original `0x800` sample bit, terrain reject masks, required footprint flag overlap, four-corner radius sampling, step-height acceptance, and simple axis fallback for sliding.

The sample is posed with the actor's body, which `FUN_00227390` stages at its workspace `+0x2C`/`+0x30` (entity `+0x28`, and that plus `+0x58`). Two things fall out of that and neither used to be modelled:

- **Terrain above the head is not ground.** `FUN_00227840` will not settle on a surface above `+0x30`. Without the limit, a footprint corner over a hole in the floor picks the *storey overhead* as its ground and the move is refused as an impossible step. `s01_e024` has a 0.05-wide seam at `y = -4.475` where the room floor stops and the corridor floor starts, and walking north at `x = -1.13` stopped dead there while a jump crossed it, because a jump is ungrounded and skips the step test.
- **`0x100` marks the ceilings.** `FUN_00227840` sets the workspace's `+0x22` winding selector for those records and `FUN_00227d28` then reverses every edge test, so they are the downward-facing surfaces -- in `s01_e024`, 427 of the 435 point straight down and no up-facing primitive carries the bit. The original never records one as ground; it only lets a ceiling at or below the head stop the scan, which returns "no ground". The port had been treating all of them as walkable floor.

Together those give the head bump: `FUN_002262c0` at `0x00226cb4` treats an upward step as provisional, writing the raised height into entity `+0x28` in the delay slot of a `FUN_00227390` call so the query is posed from where the actor is trying to get to. A ceiling now inside the body means no ground, the rise is given back whole from workspace `+0x0C`, and `+0x44` is zeroed. It is the only place upward motion is cancelled. Player movement is now camera-relative using the `FUN_00256ab0` camera/input angle relationship, with grounded movement using the original normal run scalar (`fGpffff8a4c = 0.045` per nominal frame) and jump startup applying the original vertical seed (`DAT_00355000 = 0.053`) from airborne substate `0x0C`; the lead entity gravity field uses the debug `JUMP TEST` `G_FORCE 00075` value (`0.00075` at the menu's x100000 scale). The camera is now a port of the original driver rather than a harness approximation -- see Camera below.

Scenes now bootstrap from their SCR script -- see Scene Script below. The previous PSC3 wireframe gallery is still gone; PSC3 records are visible in the resource tree but are not rendered, so script-spawned objects draw as labelled boxes rather than models.

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

Measured on `s01_e024`, one simulation step and one render per frame:

| | Debug | Release |
|---|---|---|
| simulation | 11.88 ms | 0.76 ms |
| render | 28.60 ms | 4.16 ms |
| **frame** | **40.5 ms (24 fps)** | **4.9 ms (203 fps)** |

Debug is worse in practice than even that suggests. The fixed-timestep
accumulator in `main()` runs up to `kMaxStepsPerFrame` simulation steps per
rendered frame, so once a frame costs more than 16.6 ms it starts running
several steps to catch up, which makes the frame cost more again. It settles
against the cap at about 4.4 steps per frame and roughly 12 fps.

```bat
port\build-msvc.bat Release
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s01_e024
```

## Performance

`--frame-stats` prints where a frame's time goes, once a second:

```
[frame-stats] 64 renders / 8 frames | work 4.91 ms/frame (203.51 fps ceiling) | sim 0.76 render 4.16 swap-wait 0.17
[frame-stats]   map      0.19 ms  407 prim  778 tri  101.62 batches  101.62 binds
[frame-stats]   entities 2.92 ms  20 models  7023 prim  12871 tri  242 batches  3838.75 gleam-tri
[frame-stats]   drain    0.98 ms  (driver/GPU catching up on queued immediate-mode calls)
```

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

## Suggested Next Slices

1. Promote the provider-backed loaded scene into a runtime-owned `SceneState` rather than letting `MapViewer` own the active scene. Still outstanding.
2. Replace keyboard-derived movement vectors with the original controller globals and analog smoothing path from `FUN_0023b5d8`/`FUN_00256ab0`.
3. Port the directional entity/body blocker helpers (`FUN_00228380`, `FUN_002285d8`, `FUN_00228838`, `FUN_00228a90`) and dynamic entity support helper `FUN_00228cf0`.
4. Rebuild camera behavior from the original camera state/update functions before adding a new follow camera.
5. Keep widening SCR opcode coverage the same way: run a scene, read where it
   halted, port that opcode from `src/`. `0xB7`, `0xBD`, `0xAC`, `0xE2`, `0xE5`
   and `0x149` are the next ones the story maps hit.
6. Render PSC3 models in place of the placeholder boxes, and resolve the
   map-streamed descriptors (ids from `0x272`) so those objects get real sizes.
7. Port `FUN_0025ab68` + `PTR_LAB_0031e1d0` (party members, 12 states) or
   `FUN_002cd0a0` + `PTR_FUN_00326660` (the type `0x62` enemy, 20 states). These
   are what `--actor-report` names on `s01_e024`, and they are what would make
   anything in the scene move.
8. Port the shared non-player physics step so slots 1..255 get gravity, ground
   snapping and collision. Nothing a behavior does to an entity's movement
   request is integrated today.
9. Drive header word 3 from a player interaction probe (`FUN_00252828`), which is
   what actually opens a chest and is the only thing that moves a type `0x3A`
   past animation 4.

