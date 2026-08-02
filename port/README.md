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

Runtime update owns an original-shaped lead player entity that runs a ported slice of the native field movement/jump/collision path (`FUN_00225bf0`, `FUN_00252d88`, `FUN_00256bb8`, `FUN_002534d8`, `FUN_00253468`, `FUN_00253488`, `FUN_00256ab0`, `FUN_00227390`, `FUN_002262c0`). Collision samples the PSM2 `0x78` terrain records using the original `0x800` sample bit, terrain reject masks, required footprint flag overlap, four-corner radius sampling, step-height acceptance, and simple axis fallback for sliding.

The sample is posed with the actor's body, which `FUN_00227390` stages at its workspace `+0x2C`/`+0x30` (entity `+0x28`, and that plus `+0x58`). Two things fall out of that and neither used to be modelled:

- **Terrain above the head is not ground.** `FUN_00227840` will not settle on a surface above `+0x30`. Without the limit, a footprint corner over a hole in the floor picks the *storey overhead* as its ground and the move is refused as an impossible step. `s01_e024` has a 0.05-wide seam at `y = -4.475` where the room floor stops and the corridor floor starts, and walking north at `x = -1.13` stopped dead there while a jump crossed it, because a jump is ungrounded and skips the step test.
- **`0x100` marks the ceilings.** `FUN_00227840` sets the workspace's `+0x22` winding selector for those records and `FUN_00227d28` then reverses every edge test, so they are the downward-facing surfaces -- in `s01_e024`, 427 of the 435 point straight down and no up-facing primitive carries the bit. The original never records one as ground; it only lets a ceiling at or below the head stop the scan, which returns "no ground". The port had been treating all of them as walkable floor.

Together those give the head bump: `FUN_002262c0` at `0x00226cb4` treats an upward step as provisional, writing the raised height into entity `+0x28` in the delay slot of a `FUN_00227390` call so the query is posed from where the actor is trying to get to. A ceiling now inside the body means no ground, the rise is given back whole from workspace `+0x0C`, and `+0x44` is zeroed. It is the only place upward motion is cancelled. Player movement is now camera-relative using the `FUN_00256ab0` camera/input angle relationship, with grounded movement using the original normal run scalar (`fGpffff8a4c = 0.045` per nominal frame) and jump startup applying the original vertical seed (`DAT_00355000 = 0.053`) from airborne substate `0x0C`; the lead entity gravity field uses the debug `JUMP TEST` `G_FORCE 00075` value (`0.00075` at the menu's x100000 scale). The camera is now a port of the original driver rather than a harness approximation -- see Camera below.

Scenes now bootstrap from their SCR script -- see Scene Script below. The previous PSC3 wireframe gallery is still gone; PSC3 records are visible in the resource tree but are not rendered, so script-spawned objects draw as labelled boxes rather than models.

## Scene Script

`src/ported/script/` is a narrow, faithful port of the SCR bytecode VM:
`FUN_0025bc68`'s three dispatch tables with the 16-deep call stack, plus
`FUN_0025c258` and `FUN_0025bf70` as methods of the same object, because
`pbGpffffbd60` and `DAT_00355cd0` are one address and there is only ever one
stream pointer.

`src/ported/script/scene_script.*` is `FUN_0025b390`'s header plus all five
entrypoints as named functions. Words 0 and 1 run at load, which is what
`FUN_0022a418` does -- it invokes `FUN_0025b6d0` and `FUN_0025b728` from
different points in the bootstrap, with a lot of state setup between them.

The per-frame entry (`FUN_0025b778`, word 2) runs behind `--scr-tick`, off by
default so the determinism baseline is unchanged. It runs header word 2, then
every occupied slot of the 65-entry object-script table, then the lead-bound
slot `0x40` with the entity selection on pool slot 0.

**The object-script slots are not coroutines.** `FUN_0025bc68` always runs to a
block end; nothing in the executable ever nulls `pbGpffffbd60` mid-stream, so
there is no yield. A slot holds a *fixed* entry offset and is re-entered from the
top every frame. Persistent state lives in the work array and the flag banks.
That makes the per-frame path far cheaper than a resumable VM, and it is why
`0x33` is not a frame sync -- it is inline dialogue, see
`analyzed/scene_script_frame_entry.c`.

Header words 3 and 4 are *not* per-frame and are not driven by the tick. Word 3
is the player's interaction probe (`FUN_00252828`) and word 4 is entity teardown
(`FUN_00265ec0`); both are reachable through `runEntry` when those paths land.

**Unimplemented opcodes halt rather than fall through.** An opcode whose operands
go unconsumed desyncs everything after it, so one honest stop beats a cascade of
invented instructions. `--scr-report` prints where it stopped, every opcode
reached with hit counts and first offsets, the spawn list, and the map's
placement table.

### Where scene objects actually come from

Not the script. PSM2 header word 13 holds a count followed by 16-byte placement
records -- position, angle byte, group byte, id byte, param -- which
`FUN_0022b5a8` parses into `DAT_003556e8`. Opcodes `0x4F` and `0x51` walk that
table and instantiate entries by group:

- `0x4F` takes groups 0, 4 and 5, mapping the record id into the map-streamed
  descriptor bands (`id - 1 + 0x272`, `+0x373`, `+0x474`).
- `0x51 <group>` takes one group, looking the record id up in the 16-entry table
  `0x4E` fills. Group 3 spawns type `0x3A` unconditionally.

Type `0x55` is a "marker, not an actor" sentinel: `0x52` refuses it and `0x51`
skips lookup entries carrying it.

The script-to-world coordinate scale is `fGpffff8c40` = 100000.0, not the 4096
fixed point used elsewhere in the engine. With the `0x0F` literal's built-in
`* 100`, a script value of 1000 is one world unit.

### Entity pool

`src/ported/entity/` is the pool at `DAT_0058beb0`: 256 slots of **0x1D8** bytes
(the decompiled `slot * 0xec` is over an `undefined2 *`, so it is halfwords), with
the status array `DAT_005a96b0` landing exactly at the end of it. Scripts
allocate from `[10, 256)`; **slot 0 is the lead player**, which is why
`DAT_0058bed0` is the camera's read of the player's world X. The player
controller writes through a pointer bound to that slot.

Collision radius and height come from the type descriptor, which lives in static
tables inside `SLUS_200.11` rather than in any disc resource.
`src/ported/resource/elf_data_reader.*` maps PS2 virtual addresses to file
offsets so those tables can be read directly. It is optional: `--elf` overrides,
the disc root is searched for `SLUS_200.11`, and without it objects fall back to
a default box size and say so. Ids from `0x272` up ship with the map and cannot
be resolved this way at all.

### Actor behavior is not script

Once an entity exists, what it does each frame comes from native code, not from
the SCR. `FUN_00239ce0` walks pool slots 2..255 and calls a function selected by
the entity's **type id** through four function-pointer tables in `SLUS_200.11`;
each of those handlers then dispatches again on the entity's **state** (`+0x60`)
through a per-type-family table. The lead player is slot 0 and is updated by
`FUN_00251ed8` on its own path, so this loop never sees it.

`src/ported/entity/actor_dispatch_table.*` reads the four tables out of the
executable rather than transcribing 700-odd pointers, reproducing
`FUN_00239ce0`'s unsigned range tests literally -- including the seam that sends
type `0xFB` to the primary table's index `0xFA`. `actor_frame_update.*` is the
loop, the freeze gate `FUN_0023a068`, and the fade path `FUN_0023a568`.

**Behaviors with no port do nothing and are counted**, the same discipline the
opcode VM uses. `--actor-report` lists every live entity with the handler address
it resolves to and whether that handler is ported. That report, not guesswork,
picks the next behavior to write.

One behavior is implemented: type `0x3A`, `FUN_002d1ea8`, the treasure chest.
It is the only handler in the game with no state table -- it switches on the
animation id directly. Its `+0x198` is an **event flag id** (the placement
record's param byte plus `0x400`), not a pointer; flag clear means closed, set
means opened. See `analyzed/actor_behaviors/type_0x3A_treasure_chest.c`.

### State of play

`s01_e024` runs both load-time entries **and** its per-frame entry to a clean
block end with **zero** unimplemented opcodes, and spawns 14 entities. The tick
reaches `0x36`, `0x3D`, `0x61` and `0x76` that the load-time entries never touch;
this scene installs no object-script slots. Story maps get further than they used
to but still stop; the report names the opcode and offset each time.

Of the 14 entities, 7 are chests with a ported behavior. The other 7 resolve to
`FUN_0025ab68` (party members, a 12-state machine at `PTR_LAB_0031e1d0`),
`FUN_002cd0a0` (an enemy, 20 states at `PTR_FUN_00326660`) and `FUN_002cfe08`
(map-streamed props). None of those is ported, so nothing in the scene moves yet.
Non-player physics -- gravity, ground snapping, collision for slots 1..255 -- is
also still absent, so a behavior that requests movement would not be integrated.

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

A scene has no spawn point of its own: `FUN_0022a418` copies the position staged
in `DAT_00325340` into entity pool slot 0, and that was written by the *previous*
map's warp (`FUN_0022b2c0`, reached from script opcodes `0x8B`/`0x8C`). Booting
cold into a scene has nothing to read. See `analyzed/map_bootstrap_sequence.c`.

So the port picks, in order: `--spawn x,y,z`; a script teleport (`0xAB`); the
first group 2 placement record; otherwise the walkable triangle nearest the map's
horizontal centre. The console says which was used.

Group 2 records standing in for the player start is an **inference**, not
something read out of the original. The evidence is that `s01_e024`'s init
registers `(id 1, type 0x55)` and then runs `0x51` with group 2, and
`FUN_0025eb48` explicitly declines to spawn type `0x55`, whose descriptor is 0.1
by 0.1 -- so those records are authored positions that deliberately produce no
entity, sitting in the middle of the scene's content.

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
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e024 --load-only --scr-report
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e024 --load-only --actor-report
port/build/msvc-Debug/orphen_port.exe --disc-root . --scene s01_e024 --frames 120 --scr-tick --scr-report --actor-report
```

`--scr-tick` runs the per-frame script entry and the object-script slots.
`--actor-report` lists every live entity, the behavior address its type resolves
to, and whether that behavior is ported; with `--frames` it also reports tick
counts per type. Both reports resolve straight from the pool, so `--load-only`
gives a useful answer before a single frame has run.

`--frames` runs the runtime update loop without opening a window. The old `--script-frames` spelling is still accepted as a compatibility alias, but it no longer executes script frames.

A gamepad is used when one is present: left stick moves, shoulder buttons orbit
the camera, and the face buttons map by position onto the PS2 layout, so on an
Xbox pad Y is Triangle, B is Circle, A is Cross, and X (SDL "face west") is
Square. Square is the jump binding, matching the PS2 game. The keyboard remains
available as a digital fallback.

The left stick goes through the ported `FUN_0023b3f0`, so it inherits the
original's response curve: a deadzone of 60 out of 128 raw units means nothing
happens below roughly 47 percent deflection, and the remaining 68 units rescale
to 0..128 so the walk/run threshold of 100 lands near 88 percent. That deadzone
is why there is a perceptible delay between pushing the stick and the character
moving -- the stick has to travel almost halfway first. The keyboard follows
FUN_0023b5d8's digital branch instead, which writes 128.0 outright, so a held
key always runs.

Controls:

- `W/A/S/D` moves the runtime lead player relative to the current camera yaw.
- `Space`, or gamepad X / face west (Square), jumps when the lead player is grounded.
- Holding `B`, or gamepad B (Circle), re-arms that jump in mid-air. This is a harness debug affordance, not something the original's airborne state does; it restarts state 2 / animation `0x0C` through the same startup and `+0x44` seed as a grounded jump, which is how you get up to a ceiling to test against.
- `J/L` orbits the player camera, mapped onto the original's L1/R1 raw pad bits (0x04/0x08). With no player active they rotate the free viewer camera instead.
- `I/K` adjusts pitch in free viewer mode.
- Left/right arrows cycle maps when running from `--disc-root`.
- `Q/E` zoom out/in in free viewer mode. The player follow camera currently uses the original normal field camera distance.
- `R` resets the viewer camera.
- `F` toggles wireframe.
- `H` toggles the debug HUD.

## Debug HUD

`H` toggles an on-screen overlay showing position and facing, the entity `+0x60`
state and `+0xA0` animation id with its substate frame, grounded flag and
vertical velocity, stick magnitude with the resulting walk/run gait, camera mode
/ yaw / pitch / distance, and the current ground triangle. There is no automated
PCSX2 trace comparison, so this overlay plus `--frames` determinism is how
behavior gets judged.

`src/harness/debug_text.*` is a small stroke font for that overlay only. It is
PC-only diagnostics and is unrelated to the original's text renderer
(`FUN_002681c0` and its glyph tables), which draws through the GS.

The origin axis indicator uses red for game +X, blue for game +Y, and green for game +Z. The viewer currently maps game `(x, y, z)` to viewer `(x, z, -y)`.

Script-spawned objects are drawn as pink wireframe boxes at their descriptor's
collision size, labelled `#slot Ttype Mmodel` on a camera-facing billboard.
Objects whose descriptor could not be resolved are duller, drawn at a default
size, and labelled `?`.

The current lead player is drawn in magenta, and its current ground triangle is highlighted in yellow. The console prints the primitive index, triangle index, height, leading word, and terrain flags when it enters a new ground triangle.

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
