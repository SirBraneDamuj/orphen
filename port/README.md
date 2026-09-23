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

## The title screen

`--scene` is now optional: `--disc-root <dir>` on its own boots into **s12_e010**,
which is what `FUN_002000C0:163-165` does at power-on --

```
DAT_003551F4 = 0xC;  DAT_003551F0 = 10;  DAT_003551EC = 0x2001;
```

section 12, entry 10, with the same request bits any cold boot uses. **There is no
title game mode.** `DAT_00354D2C` reads 0 at the prompt on hardware, so the title
screen is an ordinary field frame; everything that makes it one belongs to scene
module 12, `FUN_00271220`, which `FUN_0022A360` picks out of `PTR_LAB_003252B8`.
`FUN_0022A418:371` even skips the navmesh build for `DAT_003551F4 == 0xC`.

The module is ported in `src/ported/scene/title_screen.h` plus the mode 3 and
mode 4 arms of `PortRuntime::FUN_0032536c_scene_module`:

- **Mode 3** (`FUN_0022A418:369`, between the init and start script entries)
  builds the shot. The subject is the entity named by script work word 1 -- slot
  10 here, the kneeling actor -- and the camera is an ordinary manual camera
  (`FUN_00217D70`) at the field follow radius and pitch, 3.0 and 0.36651909, that
  `FUN_00216930` seeds. The actor's facing is set once to `wrap(cameraYaw + pi)`
  and never moved again, which is why the orbit slowly comes round to his side.
- **Mode 4** walks `PTR_FUN_003256F8`. State 0 (`FUN_00271470`) starts music slot
  1 and clears the counters; state 1 (`FUN_00271558`) is the title screen, and it
  draws the prompt, steps the orbit, and watches `uGpffffb686 & 0x840`.

The orbit itself is `FUN_00272010`: `fGpffff8f58 = 0.0001875` radians per frame
tick, so a revolution is about 1047 frames.

### The logo is an entity, not an overlay

Mode 3 spawns entity type **0x48** into the record at `0x0058C7E8` -- pool slot 5
-- and parks it at `DAT_00352EC4 = 1.6` over the pedestal, with `+0x04 |= 0x100`
so `FUN_002262C0` leaves it there. Its actor handler is `FUN_00239E78`, the
no-op: it is a billboard and nothing else.

What turns it is **`FUN_002255B8`**, three lines called from `FUN_002239C8:166`,
after the camera update. It rewrites slot 5's facing from the camera every frame,
by type: `0x49` takes the camera yaw outright (that is `FUN_0025D5B8`'s cut-in,
which the port has no other half of), and `0x48` takes `wrap(cameraYaw + pi)`.
Miss it and the logo is a flat quad seen edge-on, filling the screen in
perspective. It was found by poking a sentinel into `0x0058C844` on hardware and
stepping one frame -- the camera's angle came straight back.

### The prompt is one sprite off a map page

`FUN_00272100` draws the `FUN_00239020` entry at `0x00325738`: texture slot 5,
256x40, entry `(-128, -112)`, which is `(192, 336)` in the 640x448 screen the
subtitles land in. Slot 5 is an ordinary map texture page -- `0x0103` for this
scene -- whose top 256x40 band is the words "Press START button". The rest of
that page is the copyright block the legal screen uses.

### What is shimmed

START or Cross asks for **s01_e012** directly, with request `0x2003` (the cold
boot's `0x2001` plus the fade-out hold) and a `FUN_0025D1C0(1, 0xC, 0)` fade.
The original's state 2 tears the title down and walks into the new game / load
menu (`FUN_00271858` -> `FUN_00236780`), and none of that is ported. Also not
ported: the idle hand-off to the attract demo at `0xE101` ticks (reported once
instead), the cheat-code button sequence `FUN_00271558` watches for -- the one
that sets `DAT_003555DB` -- and the menu states behind the prompt.

Servicing a scene-change request that carries bit 1 used to hang: the port
handed `FUN_0025D238` a literal zero for `DAT_003555BC`, so the fade level never
climbed and the request waited for ever. Nothing set that bit before this.

One known difference against hardware, and it is not the module's: s12_e010's
steam is far too thick in the port. The scene arms 28 plume emitters (opcode
`0x10E`, 77 live puffs) and a 15-record haze field (`0x109`); at the same orbit
angle hardware draws thin wisps where the port draws a wall of white.

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

**An opcode is reported at one of three support levels.** An opcode whose
operands go unconsumed desyncs everything after it, so a genuinely unknown one
still halts -- one honest stop beats a cascade of invented instructions. But a
long cutscene chain reaches dozens of purely cosmetic opcodes, and halting on
each in turn means never seeing the scene run at all. So:

| level | meaning |
|---|---|
| `modelled` | operands consumed and the effect reproduced |
| `operands-only` | operands consumed exactly as the original reads them, effect deliberately not reproduced |
| `UNIMPLEMENTED` | not decoded -- the stream stops here |

`consumeOnly(opcode, expressions, inlineBytes)` is the middle case, and **every
count comes out of the matching `src/FUN_*.c`, never a guess** -- a wrong count
desyncs the stream, which is exactly what the halt exists to prevent. Note the
`analyzed/ops/` filenames are not reliable here: several encode hypotheses the
dispatch table has since disproved, and `0xA4`/`0xA5`/`0xA6` are named
"audio_submit" while actually mutating map primitive flags.

`--scr-report` prints every opcode reached with its level, hit count and first
offset, plus the spawn list and the map's placement table.

### The cutscene sequencer

`FUN_0025ce30`, in `SceneScript::FUN_0025ce30_run_event_scheduler`. **This is the
mechanism a cutscene is actually built out of**, and without it no story scene
can sequence no matter how many opcodes are ported.

Because the VM has no yield, a scene cannot express "walk here, wait, then
speak" as one routine. Instead opcode `0xA1` arms one of four channels with a
stream of 8-byte records:

```
[u16 delayUnits][u16 gate][u32 targetOffset]
```

and the scheduler pays them out one at a time. The gate has three readings:
zero fires immediately, bit 15 clear waits for that event flag, bit 15 set waits
for those bits in `uGpffffb0f4`. A blocked channel does not advance its timer at
all, so a delay is measured from when the gate opened rather than from when the
stream was armed. The timer accumulates frame ticks and fires at
`timer >> 5 >= delayUnits`, so `delayUnits` is a frame count.

A target inside the dialogue pointer table's range goes straight to the message
driver; anything else is queued into a free object-script slot. That window is
`FUN_0025b288`, which walks header word 5's table and keeps its first and last
non-zero entries -- and the top bound is genuinely exclusive, so the final entry
reads as script rather than dialogue. Reproduced rather than fixed; the scene
data was authored against it.

`s01_e012`'s opening is stream `0xca30`, armed at `0x2122`. Its first records
start two script bodies, then wait on flags those bodies set, then put dialogue
up and wait for it to close.

### Two bugs this turned up

Both were silent, and both would have blocked every story scene:

- **Opcodes `0x3D`..`0x40` are the event-flag query / set / clear / toggle**, not
  resource queries. The port answered "not loaded" and wrote nothing, so no
  script could ever latch its own progress and every gate saw a cleared flag.
  The mode is the opcode byte itself, read back off the stream as a character:
  `=` query, `>` set, `?` clear, `@` toggle. All four return the value the flag
  had *before* the write, so a script can test and latch in one instruction.
  (`FUN_002663a0` sets and `FUN_002663d8` clears -- the port had the clear one
  carrying the setter's name.)
- **`+0xA8` had two owners.** It was modelled twice: once as `FUN_00225c90`'s
  animation timeline cursor and once as a per-frame "substate frame" the player
  controller incremented. Script object register 6 is `param_1[0x54]` in
  `FUN_0025c548`, which is that same halfword -- so a cutscene that sets an
  animation and then polls register 6 for a keyframe was watching a counter
  nothing advanced. `FUN_002534d8`'s jump-startup tests read it too, as
  keyframes rather than frames; the animation pass is the only writer now.

There is also a smaller one worth stating because it recurs: **type `0x38` is a
role, not a character.** Opcode `0x66` stamps it over an actor's real type when a
scene takes it over for choreography and parks the real one at `+0x1CE`. Looking
a model up by the raw type after that finds nothing, which left a whole cast
un-animated. `OriginalEntity::effectiveTypeId()` is the test every original makes
in the same situation.

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

Several behaviors are implemented outright. Type `0x37`, `FUN_00258ab8`, the
party follower, has its own section below. Type `0x3A`, `FUN_002d1ea8`, the
treasure chest.
It is the only handler in the game with no state table -- it switches on the
animation id directly. Its `+0x198` is an **event flag id** (the placement
record's param byte plus `0x400`), not a pointer; flag clear means closed, set
means opened. See `analyzed/actor_behaviors/type_0x3A_treasure_chest.c`.

### The party follows Orphen (type `0x37`)

After the opening cutscenes hand control back, Cleo and Magnus are supposed to
walk after the player. They stood still, and the reason was one operand.

Opcode `0xAC` (`FUN_002631f0`) binds an entity into a party slot and stamps type
`0x37` over it. It evaluates **four** expressions and reads the entity back from
`sp+8` -- the *third*, not the fourth. The port took the fourth, which in
`s01_e012` is `180`; that is not a pool index, so every bind resolved to nothing
and no follower was ever created. (`180` is not random: it is the value the
handler stores into `+0x1A2` a few lines later, so the script is pushing a speed
the opcode stopped reading.) Two other details of the same handler were wrong or
missing -- the "deliberately empty" path *sets* the slot flag rather than
clearing it, and the free-lane scan that assigns `+0x1C6` had no port.

With the bind fixed, `src/ported/entity/party_follower.*` is `FUN_00258ab8` and
its state table `PTR_FUN_0031e1a0`:

| state | original | what it does |
| --- | --- | --- |
| 0 | `FUN_002596c8` | init: pick this follower's side of the lead (`+0x1BC`) |
| 1 | `FUN_002597d0` | idle: watch the lead, then decide whether to walk |
| 2 | `FUN_00259D00` | turn to an angle, then push through on it |
| 3 | `FUN_00259e50` | turn in place |
| 4, 5 | — | navmesh walk, waypoint walk. **Not ported** |
| 6 | `FUN_0025a298` | wedged: teleport back onto the lead's trail |
| 7 | `FUN_0025a450` | walk blind, after being shoved |
| 8 | `FUN_0025a500` | **the follow walk** |
| 9 | `FUN_0025aa48` | sidestep out of a crowd |
| 10 | `0x0025AB48` | hold a stagger until the floor is back |

Three things about it are worth writing down.

**The formation is a ring, not a queue.** A follower walks to
`lead + (cos, sin)` of `lead.facing + its own +0x1BC`, one unit out. The first
one bound takes +150 degrees and every later one takes the *negation* of the
first one it finds, which is why two followers end up on opposite shoulders.
It only re-aims when it is more than 1.5 units from the lead and more than 0.3
from the spot, so a stationary party settles rather than jitters.

**Walk versus run comes off the stick, not off the lead's speed.**
`FUN_0025a500` reads `DAT_003555e8`, the analog magnitude, and picks animation 4
under 100 and animation `0x0E` at or over it. Past two units it stops matching
the gait at all and just adds a flat `0.05` to its step to catch up.

**The idle look-at is a bone override, not a turn.** State 1 twists the bust and
head toward the lead through `FUN_0020d8c0` -- the bust by the full angle, the
head by 0.3 of it. Past 60 degrees it gives up on twisting and hands over to
state 3, which turns the whole body. That needed two new callbacks into the
model layer (`FUN_0020da68`'s sampler and `FUN_0020d9d8`'s filtered pose),
because a follower's rest pose has to be read before it can be twisted.

The lead's breadcrumb trail (`FUN_00224060`, `DAT_00355704`) is ported with it.
It is not map data: it is a 512-entry ring of where the lead has actually been,
appended to whenever it moves a quarter unit. State 6 walks it backwards for
somewhere off camera to reappear, which is a follower's only way out of a wedge.

#### The navigation graph

`ported/entity/follower_navmesh.*` is `DAT_00355038`: one 0x34-byte record per
map primitive, holding four neighbours, a BFS depth per follower lane, and a
depth per lane per edge. `FUN_00257fc0` seeds it, `FUN_002582d0` builds it,
`FUN_002584b0` floods it, `FUN_00258c70` steps down it.

**Adjacency is discovered by probing, not by reading a stored graph.** There is
no navmesh in the map file. `FUN_00258080` stands at the midpoint of each of a
primitive's four edges, steps 0.2 sideways -- once to each side, because the
winding is not known -- from 0.3 above the primitive's centre, and asks the
ordinary single-point ground query what is underfoot. Whatever answers, if it is
a different primitive under 50 degrees and not one of the rejected surface
classes, is the neighbour. `FUN_002582d0` runs that as a flood fill from the
lead's spawn, so the graph is exactly the floor reachable from where the scene
starts.

That costs about eight ground queries per primitive at load. It is the 65537
`FUN_00227840` calls the PS2Recomp spike sees during a map load, and it is why
it runs once rather than per frame -- 0.2 s of load time for `s01_e012`,
unmeasurable against the rest.

It is rebuilt by **opcode 0xAB** (`FUN_00263148`, teleport the lead) and nowhere
else. A scene that opens a door does not rebuild it, and does not need to: the
probe's body band is the floor's own height plus 0.3, so a door panel standing
on that floor is above the band and the query answers with the floor behind it.
Doorways link whether the door is open or shut.

`[nav] follower graph 159/3948 primitives reachable from (-0.12, -2.699, -1.5)`
is printed once per load. The second number is every collision primitive in the
map -- walls, ceilings, props -- so a small first number is normal. A first
number of 0 or 1 means the seed missed the floor, and every follower in that
scene will fall back to `FUN_00259178`'s blind turns.

#### What is still missing

`FUN_0025a0c8`, state 5, and only because **nothing can reach it**.
`FUN_00259378` gets there through `FUN_00258b80`, whose retail body is a
511-iteration loop over the breadcrumb ring that discards every distance it
computes and then returns `-1` unconditionally (`addiu $v0, $zero, -1` at
0x00258c48; Ghidra reports the same thing as an unreachable block). The `-1`
always routes to state 6 instead.

#### Leaving the party releases two bones

`FUN_002589c0` -- the thing opcodes 0xAD/0xAE reach, and what a cutscene runs to
take a follower back under script control -- ends with more than a type
restore:

```
FUN_00251db8();                                  // the follower re-sort
FUN_00267e78(entity + 0x198, 0x40);              // the whole follower block
FUN_0020d9c8(entity, FUN_0020dd78(entity, 2));   // the bust bone
FUN_0020d9c8(entity, FUN_0020dd78(entity, 1));   // the head bone
```

Those last two are the overrides `FUN_00257c78`'s look-at was holding. While
`+0x168 + bone` is non-zero `FUN_0020d9d8`'s filter takes the override over the
sampled pose, so an animation played afterwards cannot move that bone at all.
The port restored the type and left the overrides installed, which pinned
Magnus' torso upright through the flooding shot in `s01_e012` with his bust
still aimed at where Orphen had been standing. The block clear is ported with
them; `FUN_00251db8` is not.

#### Falling

`FUN_002262c0:99-113` integrates gravity into `+0x38` for every actor, and the
only thing gating it is **`+0x04` bit 3**:

```
dt     = DAT_003555bc * 0.125
+0x38 += v * dt - (g * dt) * dt * 0.5
v     -= g * dt        (nudged to -1e-05 if it lands on exact zero)
```

The port used to skip this outright, on the reasoning that the EE dump shows the
six type `0x62` enemies with `+0x44` at zero. They do -- because they read
`+0x04 = 0x000b`, bit 3 set, so gravity never touches them. Every *character* in
the same dump reads `+0x48 = 0.00075` and `+0x44 = 0`, the latter because the
landing clamp zeroes it once they are resting. `+0x48`'s default in
`OriginalEntity` was a 24.0 placeholder that nothing ever integrated; it is now
that 0.00075.

The settle at `:481-520` is not symmetric. A rise is provisional -- it is taken,
tested against the headroom query, and given back whole if the query refuses. A
fall is simply clamped at the cached `+0x4C`, which is also the only thing that
raises `+0x28` for a stationary actor.

Without the fall half, a follower that stepped onto anything never came down:
the port's clamp only ever raised. The small bumps in the floor of `s01_e012`'s
chest room left Cleo and Magnus hovering a tenth of a unit up for the rest of
the scene.

#### Walls: the retry ladder, not an axis split

The party follower is the port's first ground-*walking* non-player actor, and it
found two things `integrateNonPlayerMovement` had invented.

**A refused move is retried on rotated headings.** `FUN_002262c0`'s velocity
section is one `do { } while (true)`, and the wall case falls into a ladder at
0x00226b58 that re-runs the entity clamps and `FUN_00227390` in full on five
headings derived from the request's own -- constants from the block at
0x0035243c:

| attempt | heading | speed |
|---|---|---|
| 0 | as requested | x 0.3 |
| 1 | +20 degrees | x 0.7 |
| 2 | -20 degrees | unchanged |
| 3 | +60 degrees | x 0.5 |
| 4 | -60 degrees | unchanged |
| 5 | give up |

Each retry does `flags & 0xffff7ffd | 0x4000`, so the refusal bit comes back
down and a graze the fan-out resolves does not count as a block at all. The port
used to try X alone and then Z alone, which is a different shape and grinds
along a wall instead of going round it.

**`0x20` and `0x40` are entity bits, not terrain bits.** `FUN_00228380` /
`FUN_002285d8` (the X sweeps) write `0x20`; `FUN_00228838` / `FUN_00228a90` (Z)
write `0x40`. The axis fallback was minting them from geometry, and the follower
reads `+0x0C & 0x60` as "an actor is in my way, queue behind it" and `& 0x262`
as its stuck counter -- so a wall was being reported as a person, and every
graze counted toward giving up. The port had also collapsed all four blockers
onto `0x20`; they are now split by axis.

**`+0x0C` is rebuilt every frame.** `FUN_002262c0` seeds a workspace word to
zero at :37 and stores it over `+0x0C` at :628; nothing ORs into the entity's
copy in place. The port only ever OR-ed, so once a follower was blocked the bits
never came down again -- it counted to five standing on open floor and sat in
state 6 for the rest of the scene. That was the bug behind "the party stays in
its cutscene positions and never follows again".

**This moves `s01_e024`.** The type `0x62` enemies read the same `+0x0C & 0x202`
for their scatter branch, so their positions after 3000 frames differ and the
shared RNG stream shifts with them. `s01_e012` is byte-identical at 3000 frames.
The change is toward the original, not away from it -- `FUN_002262c0` is one
function and every actor goes through it -- but it is a visible change to a
scene that was tuned without it, so it is called out here rather than buried.

### State of play

`s01_e012`, the game's first scene, runs its init, start and per-frame entries
with **zero unimplemented opcodes over 4000 frames** and spawns 77 entities. Its
scene script is byte-identical to `scr/scr2.out`, so the whole existing scr2 body
of work applies -- `docs/scr2_offset_tables_dialogue_voice_flow.md` most of all.

The opening cutscene chain **runs to its end.** From `--scr-report`:

```
event records dispatched: 208     event flag changes: 60+
  frame     1  -> 0x40b2 (slot)      frame   634  -> 0x2c (dialogue)
  frame   544  -> 0x461f (slot)      frame 13279  -> 0xb63 (dialogue)
  frame 13317  flag 0x515 set  at 0x6813
0x6D player lock mode=-1  (state 10)   ...   mode=1  (release)
```

Forty-two lines of dialogue, a dozen camera shots, and characters walking
between marks -- about **three and a half minutes** of cutscene, with **zero
unimplemented opcodes** and `--frames` byte-identical run to run. (The last two
frame numbers used to read 10354 and 10426; the chain got longer, and correctly
so, when dialogue holds started being timed from the voice clip -- see Voice.)

`0x515` is the handoff latch: `docs/scr2_offset_tables_dialogue_voice_flow.md`
identifies it as what the opening sets when it gives the player control, and the
matching `0x6D` release is in the report beside it. So the scene reaches its
first interactive moment.

### How a cutscene is actually built

Three mechanisms, and none of them is a linear script:

1. **The event scheduler** (`FUN_0025ce30`) pays out a stream of
   `[delay][gate][target]` records, sending each either to the dialogue driver
   or into a free object-script slot.
2. **Event flags are the join.** A record gates on a flag; the body it started
   sets that flag when it finishes. Flags `0x8FE`/`0x8FF` are the dialogue
   driver's own, so "wait for the line to end" is just another gate -- and
   dialogue records set story flags themselves through text control code `0x1B`,
   which is the only one of the 31 that has to *run* rather than be skipped.
3. **The choreography opcodes** (`0xE9`..`0xF5`) advance the focus entity's
   `+0x1BC` step counter when they complete, so a body reads that counter and
   switches on it. The counter is the choreography's program counter.
4. **A script-driven NPC carries its own program.** Opcode `0x66` converts an
   entity to type `0x38` and parks a blob offset in `+0x130`; `FUN_0025bf20`,
   type `0x38`'s entire behaviour, runs the body there **every frame** with the
   entity installed as both the selection (`iGpffffb0d4`) and the choreography
   focus (`iGpffffb0d8`). `s01_e012` converts fourteen. The offset is loaded
   with `lh`, so it is signed and a body must live in the low half of the blob.

   These bodies are uniformly `switch (step) { ... }` over `0xED`, usually with
   `0xE9` in front of it — `0xE9` reads and clears `+0x1CC`, the interaction
   pulse that `FUN_0025b978` sets when the player talks to the NPC.

   `s01_e012` converts fourteen entities. Seven are type `0x293` — slots 18..24,
   of which five survive the opening — and they are **the ship's doors**, not
   cast: two rows of cabin doors at `x = 5.5 / -0.5 / -6.5`, `y = ±1.2`, plus one
   at `(-9.885, 0.099)`. Their bodies are seven copies of the same 0x78-byte
   routine laid end to end from `0x39d9` (`0x39d9`, `0x3a52`, `0x3aca`, `0x3b42`,
   `0x3bba`, `0x3c32`, `0x3caa`), each differing only in its door id — see The
   doors below. The other seven are types `0x281` and `0x2ca`; the five `0x281`
   share body `0x397c`, which is not an interaction at all but a visibility gate
   (`switch (0xED) { case 0: if (flag 0x28A) clear +0x08 bit 0; }`).
   `--actor-report` prints the real type and the body offset behind the role,
   because fourteen lines reading `type=0x38` hide both.
5. **Floor panels start the rest.** Opcode `0x61` tests the terrain word the
   player is standing on, and the six panel groups at `0x3d40`..`0x400e` are
   **exact four-bit pattern matches**, not "any of these":

   ```
   0x3d40   p10 && !p20 && !p40 && !p80      -> tile 0x10
   0x3d94  !p10 &&  p20 && !p40 && !p80      -> tile 0x20
   0x3e46  !p10 && !p20 && !p40 &&  p80      -> tile 0x80
   0x3f68   p10 && !p20 &&  p40 && !p80      -> tile 0x50
   ...
   ```

   (`0x18` is logical NOT and `0x1A` is AND in the expression evaluator.) Each
   body ends in `0xA1`, arming a scheduler stream — stepping on tile `0x10` arms
   `0xd0c0`. So the high nibble of the terrain word is a **tile id**, and the
   map carries exactly six of them: `0x10`, `0x20`, `0x50`, `0x62`, `0x84`,
   `0x9c`. `--scr-report` prints where each panel bit sits so a trigger can be
   walked onto deliberately.

   The panels only start being tested around frame 10400, when the opening hands
   over control — which is the shape the scene is authored in: one automatic
   opening chain, then player-driven cutscenes from there.

   **Most of the scene's choreography lives behind them.** The blob has twelve
   `0xEB` focus sites; the opening reaches three. The other nine sit in
   `0x705d`..`0xa812`, and stream `0xca30` never targets anything above
   `0x6526` — but `0xd0c0` targets exactly that range. Six opcodes had to be
   ported before it would run: `0x6E`, `0x72`, `0x73` (angle helpers), and
   `0x94`, `0xDE`, `0x10A`, `0x10B` (audio and graphics submitters, operands
   only). Each halt named the next one, which is what the halt discipline is
   for.

### The voice handshake

`0xBD` method `0x70` starts a voice line and `0x72` polls it, and a cutscene
**blocks on the pair**: it only installs its wait loop if the start reports
success, then waits for the poll to report idle. `VOICE.BIN` is not in the disc
root, so nothing can play -- and both obvious answers hang the scene, one
retrying the start forever and one waiting forever. The port answers start =
success, poll = idle, so an unvoiced run behaves like a voiced one that has
already finished. (`FUN_002445c8`'s own no-audio path returns `-1`, which is
neither.)

**Sixteen bugs came out of getting there**, all silent, and the first two would
have blocked every story scene in the game:

- **Opcodes `0x3D`..`0x40` are the event-flag query / set / clear / toggle**,
  not resource queries. The port answered "not loaded" and wrote nothing, so no
  script could latch its own progress and every gate saw a cleared flag. The
  mode is the opcode byte read back as a character: `=` query, `>` set, `?`
  clear, `@` toggle, all returning the value from *before* the write.
  (`FUN_002663a0` sets and `FUN_002663d8` clears -- the port had the clear one
  carrying the setter's name.)
- **A handler must read its own opcode before evaluating any operand.** An
  operand expression can contain another statement opcode -- work reads (`0x36`)
  are everywhere -- and evaluating one overwrites `currentOpcode_`. Four
  handlers read it afterwards, including `0x77`..`0x7C`, so *every* object
  register write whose selector was an expression fell through its switch and
  silently did nothing. That is why the cast never animated. Every original
  saves the opcode in its first instruction; the port now does too.
- **`+0xA8` had two owners.** It was modelled twice: once as `FUN_00225c90`'s
  animation timeline cursor and once as a per-frame counter the player
  controller incremented. Script object register 6 is `param_1[0x54]` in
  `FUN_0025c548`, the same halfword -- so a cutscene that sets an animation and
  polls register 6 for a keyframe watched a counter nothing advanced.
  `FUN_002534d8`'s jump-startup tests read it as keyframes, not frames.
- **Player state 10 was not honoured.** `PTR_FUN_0031e0e8[10]` is `jr ra; nop`
  -- a real no-op, and the whole of how a cutscene takes the controller away.
  The port fell through to the grounded field branch, handing control back to
  the pad the moment a scene asked for it.
- **Text control code `0x1B` has to execute.** The dialogue stub skipped every
  control code, but `0x1B` sets an event flag, and the scheduler gates on those.
  Flag `0x6A` is set from the tail of Volcan's line and by nothing in the script
  at all, so skipping it stopped the chain dead. Its payload is two bytes, not
  three.
- **`0x7D`'s operands interleave**: an expression, an inline byte, then another
  expression. `src/FUN_00260738.c` is hand-annotated with its statements
  reordered and `analyzed/update_entity_timed_parameter.c` reads it as three
  expressions; consuming three desyncs the stream. The disassembly at
  `0x260754`..`0x260770` is the authority. It surfaced as an overrun nine
  thousand frames in, which is what the halt discipline exists for -- and
  `--scr-report` now prints where an overrun happened, not just that one did.

Smaller, and it recurs: **type `0x38` is a role, not a character.** Opcode
`0x66` stamps it over an actor's real type when a scene takes it over for
choreography and parks the real one at `+0x1CE`. Looking a model up by the raw
type after that finds nothing, which left a whole cast un-animated.
`OriginalEntity::effectiveTypeId()` is the test every original makes here.

- **The movement request was cleared twice, and the invented clear won.**
  `FUN_00239ce0` does not touch `+0x30`/`+0x34`/`+0x38` at all -- the physics
  pass owns the whole accumulate-then-spend cycle and `FUN_002262c0` zeroes them
  once it has applied them. The port zeroed them a second time, immediately
  before dispatching each behaviour, and the scene script's tick runs *before*
  the actor loop. So every movement a script asked for was destroyed on the
  frame it was made.
- **`0xEE`..`0xF1` request movement, they do not teleport.** `FUN_002658c0`
  accumulates into `+0x30`/`+0x34` (`psGpffffb0d8[0x18]` and `[0x1a]` over a
  short pointer) and lets physics spend it. Writing `+0x20`/`+0x24` directly, as
  the port did, skips collision and the ground follow and moves the actor a
  frame early relative to everything that reads its position. Its run animation
  also keys off `FUN_002298d0`'s **character class**, not off the type id.
- **A ground query with no body answers the wrong storey.** `FUN_00227070`
  stages the entity's `+0x28` and `+0x28 + +0x58` into the scan workspace at
  `+0x0B`/`+0x0C`, and `FUN_00227798` puts its `z` argument into both;
  `FUN_00227840` then refuses to settle on anything above the head. The port
  asked `queryPsm2GroundAt` with a reference of `0.0f` and no body from the
  script and actor paths, so its "closest surface to the reference" tie-break
  picked whichever floor was nearest sea level. On `s01_e012`, a ship with a
  deck at `-1.50` and structure at `+0.25`, that put Volcan a metre and a half
  in the air. Opcode `0x60` was also throwing its `z` away, which is the operand
  that says *which* storey the script means.
- **A map-streamed prop had no descriptor, so every prop animated forever.**
  `FUN_00229980` is one function covering every type range, and its streamed
  branch does not read a table -- it *synthesises* a descriptor into the scratch
  block at `DAT_0031c1d0` from the prop's own 0x28-byte record (`FUN_00229688`
  copies the fields; the three ints at `+0x0C`/`+0x10`/`+0x14` are millimetres).
  The port returned `nullopt` for the whole streamed range, so every map prop
  spawned on struct defaults: a uniform 0.15r/0.80h instead of its real
  collision size, and -- the load-bearing one -- entity `+0x06` at zero.
  `+0x06` bit `0x10` is `FUN_00225c90`'s early return, and the source record's
  `0x4000` bit is what sets it. Nearly every prop in a scene is meant to be
  *static*; the port advanced all of them every frame, driving the pose filter
  into stretched geometry that grew without bound. In `s01_e012` that is the
  blown-out white cluster over the Dortin/Volcan shot. The same synthesis also
  supplies `+0x04` (`0xD0`/`0xD8` on the record's `0x8000` bit) and `+0x08`.
- **`FUN_0020c5a8`'s first pass has a second skip.** The draw walk makes two
  passes and the port had only the second. The first tests entity `+0x02` bit
  `0x200` -- `puVar11[1]` over a `0xEC`-halfword stride -- and marks the slot
  undrawable outright. It is the same bit that picks `FUN_00225c90`'s alternate
  animation format, so it reads as "this is not a skinned entity". Nothing in
  `s01_e012` sets it, so this one fixed no visible artefact, but the walk was
  wrong.

Two smaller ones from the same pass: `FUN_00229c40`'s last three lines start
every entity with the keyframe blend saturated and `+0x08` bit `0x10` raised, so
its first drawn pose is the sampled one rather than something eased out of the
previous occupant of the slot -- the port had been leaning on the pose filter's
own `seeded` flag for that. And object registers `0x1F`/`0x20` (`+0x154`,
`+0x158`, the two extra model rotations the renderer already applies) had no
setter, which was the last unmodelled register write in either scene.

- **Opcode `0xBD` methods `0x70`/`0x72` are waypoint path-follow, not voice.**
  `FUN_00263e80` passes `uGpffffb0d4` -- the *selected entity* -- as
  `FUN_00242a18`'s first argument, so its cases are methods on an entity.
  `0x70` (`FUN_002443f8`) takes a follower slot, reads `arg3 + DAT_00355058` as
  a u32 count followed by `count*3` VM expressions, and builds a **cubic spline**
  through them with the same `FUN_00266a78` the chest camera uses; `0x72`
  (`FUN_002445c8`) reports progress and reads 0 only once the slot is freed. An
  earlier reading here called the pair a voice handshake and stubbed it "started,
  already finished", which made every path-driven actor stand still while its
  wait subproc exited on its first frame. `FUN_002446e8` walks the spline and
  writes the **movement request**, so it has to run before the actor loop -- the
  physics in that loop is what spends it. In `s01_e012` this is what walks
  Dortin: path `0x366C`, three points, duration 400 (`total = duration << 4`,
  advanced 32 ticks a frame, so 200 frames), turning him toward Volcan as he
  goes.
- **Entity `+0x04` bit `0x100` turns physics off, and the port ignored it.**
  It is the first thing `FUN_002262c0` tests (`0x00226304`): it copies `+0x04`
  into the workspace and returns before clearing `+0x64`, before gravity, before
  the terrain sample, before the epilogue that spends `+0x30`/`+0x34`/`+0x38`.
  The entity keeps its scripted position *and* its scripted `+0x4C`, which is how
  a cutscene pins an actor to a pose the floor disagrees with. `s01_e012` opens
  on Orphen lying on a bed: the script's `0x54` at `0x4557` places him at
  `z = -1.224` while the surface under him samples `-1.300`, so without the gate
  the port re-settled him and he sank into the mattress by 7.6 cm. `eeMemory.bin`
  captured on that frame reads `+0x04 = 0x312C` and `+0x4C = -1.224`; the same
  dump taken in the field reads `0x3024`, so the bit really is toggled per
  cutscene rather than being a property of the lead. The chest cutscene raises it
  in state `0x0D` and clears it in `0x13`, one state before `0x14` polls the
  grounded flag -- the original's own ordering, and it survives the gate intact.

Three more, all of which meant something correct was computed and then thrown
away before it could reach a pixel:

- **`0x9C` is a getter, not a configurator.** `FUN_0025d590` is a one-line load
  of one byte of an interpolated colour; the dispatch-table note calls it "bind a
  track/mode combination" and the port consumed it as operands-only, returning
  zero. The three opcodes are a set: `0x9A` (`FUN_0025d408`) arms one of sixteen
  colour ramps at `DAT_00572078` with a start colour, an end colour and a
  duration; `0x9B` (`FUN_0025d480`) steps one and **returns whether it finished**
  -- Ghidra types both wrapper handlers `void`, but each ends on the call and
  never touches `v0`, so the value propagates and the script branches on it;
  `0x9C` reads a channel back. `s01_e012` runs two of them permanently, each
  re-armed in the opposite direction the frame it reports finished. One drives
  the scene's directional light through `0x97` and one drives a light slot
  through `0xC3`. That ping-pong is the slow brightening and darkening over the
  whole opening, and with `0x9C` returning zero the directional light was
  **black from frame two onward**.
- **The scene environment was published only at scene load.** `FUN_00200e38`
  rebuilds VU1's lighting VIF packets every frame from the script globals, so a
  script that rewrites them per tick is rewriting what the next frame is lit by.
  `applySceneEnvironment()` ran from the load path only, which discarded all
  **13,002** of `s01_e012`'s per-frame `0x97` writes. Fixing `0x9C` alone changed
  nothing on screen; the two together take the mean frame brightness from flat to
  a 17.9 - 26.8 swing.
- **`DAT_00571DC0` was modelled twice and the script's copy was never drawn.**
  `SceneScriptState` carried its own pair of fade banks with its own arm and step
  methods, and `PortRuntime` owned the `ScreenFade` the renderer actually reads.
  The chest cutscene drove the one that renders; opcodes `0x85`/`0x86`/`0x87`/
  `0x88` drove the one that does not. Every fade a *script* asked for stepped
  correctly and was invisible -- ten of them in `s01_e012`, including the opening
  fade-in and every shot transition. In the original these are literally the same
  two banks of BSS, so the port has to share one object too. The opening now
  starts near black and reaches full over ~128 frames, which is what rate 2 over
  `0x1FE0` predicts.

The chain runs to its end: **208 event records, 42 dialogue lines**, flag
`0x515` set at frame 10426 and the player lock released beside it.

`s01_e024` runs both load-time entries **and** its per-frame entry to a clean
block end with **zero** unimplemented opcodes, and spawns 14 entities. Slot 4,
the player's bandana, is created outside the script by `FUN_00251e40`, so the
pool holds 20 live actors rather than 19.

**Object registers are entity fields.** Opcodes `0x76`..`0x7C` look like a
register file but `FUN_0025c8f8` and `FUN_0025c548` are a switch whose cases
write straight through the selected entity. Register 13 is the facing angle at
`+0x5C`, and this scene's init writes it once per party member -- so the room's
five characters face their authored directions rather than all facing zero.
Register 0 is the type id, which the per-frame entry reads every frame.

**Floor panels are terrain, not entities.** `FUN_002262c0` copies the settled
surface's first two words into `+0x6C`/`+0x70`, and opcode `0x61` tests one of
them against a mask. This scene makes two such tests, masks `0x1` and `0x2`, and
the map has exactly two up-facing floor quads carrying those values:

| primitive | terrain | position | branch |
|---|---|---|---|
| #250 | `0x1` | (-4.75, -14.00) | park the player (`0x6D`), fade out (`0x85`/`0x86`) |
| #292 | `0x2` | (-6.00, -11.25) | boot the party for battle (`0xE1`) |

Neither is a save point. The fade branch now runs to completion and stops at
`0x8E`, an audio opcode; the battle branch completes with nothing unimplemented.

**The confirm button works.** `FUN_00252cc0` / `FUN_00252a18` / `FUN_00252828`
are ported and hooked into `FUN_00256bb8`'s grounded branch on Cross. The branch
`FUN_00252828` takes comes from the descriptor flag at `+0x02`, read out of the
executable: `0x4004` on party members sends them to scene script header word 3,
`0x0100` on the chest takes the native path.

- **Chests** run their whole cutscene. See The Chest Cutscene below.
- **Party members** run header word 3 and halt honestly. `0x70` (the angle from
  an object to the player -- how a character turns to face you) is ported; `0x33`,
  inline dialogue, is where it stops, and that is the right place: its operands
  are a variable-length text stream, so a stub would desync everything after it.
  The party swap itself is not implemented.

### The doors, and the pulse nothing was setting

Every door in `s01_e012` was inert to the player, and the reason was one missing
assignment.

`FUN_00252828`'s **first** branch — before the chest branch, before the
map-streamed branch — is the scripted one: `+0x02` bit `0x4000` set, `+0x04` bit
`0x4000` clear, type not `0x37`. Opcode `0x66` raises that `+0x02` bit on every
entity it converts, so *every script-driven object in the game* arrives here. It
hands off to `FUN_0025b978`, which is three statements:

```c
psGpffffb0d4 = psGpffffb79c;                     // select the target
if (*psGpffffb79c == 0x38) target[+0x1CC] = 1;   // raise the interaction pulse
FUN_0025bc68(header word 3);                     // run the shared hook
```

The port did the first and the third. **`+0x1CC` is the mechanism**: a type-0x38
entity's behaviour, `FUN_0025bf20`, re-enters its body from the top every frame
with itself as both selection and focus, and the body tests `+0x1CC` through
`0xE9` — read and clear. With nothing ever raising it, that test read zero
forever. `FUN_00239ce0` runs after `FUN_00251ed8` in `FUN_002239c8`, so the pulse
is raised and consumed on the same frame.

Each door body is:

```
if (work[14] == -1)                  // no other door mid-animation
  if (0xE9) 0xEC(1);                 // interacted with -> step 1
  switch (0xED) {
    case 1: work[14] = <door id>; 0x90 arm ramp 7;
            0x9D(0xA0, 0xb348);      // queue the body at 0xb348 in a free slot
            0xEC(2);
    case 2: 0xF5 ...
  }
```

`0xb348` carries the marker `0B 04 83 12 00 00`, so the debug overlay's
"SCR SUBPROC DISP" names it **subproc 4739** — and all seven door bodies queue
the same one, which is why every door reports the same id. It is the door driver,
and it is six instructions:

```
if (0x91 step ramp 7)      // non-zero the frame the ramp reaches its target
{
  work[14] = -1;           // release the door lock
  0x9E(-1);                // retire this slot
}
0x7D(work[14], channel 2, 0x92 read ramp 7);   // rotate the collision group
```

So the door *is* a map collision group, rotated about channel 2 by a ramp, and
`work[14]` is both the lock and the group id. Opening the cabin door at
`(-6.50, -1.20)` (pool slot 23) moves group **5** from 2° to exactly 90° over 46
frames and the subproc then retires, leaving the group where it stopped.
`FUN_00208450` picks the transform up and, per the `0x800` note in
`port_runtime.cpp`, drops the leaf out of the ground scan while it is open.

Reproduce it headless:

```
--scr-tick --arm-stream d300:13400 --hold-stick 0,1 --press-confirm 15235
```

which walks the lead out of the re-entry cutscene into slot 23's door and opens
it. (`--press-confirm` with a *list* of frames re-opens the same door each time
it finishes, because `work[14]` is back to `-1` — that is the script's own
behaviour, not a bug.)

### The doorway scene: 0x64, 0x56, 0x8E and 0x13F

The doorway scene at the end of the hall — Orphen talking to Dortin and Volcan,
armed as stream `0xd530` by the tile-`0x62` floor panel — started on its camera
move and then stopped. The debug overlay showed six subprocs live, of which
**3507**, **4192** and **4414** belong to the scene (4927 and 4947 are the
always-resident per-frame slots and 5112 is the flood's ambient pair).

Subproc 3507 (`0x8ebf`) is the one that stops, and it is short:

```
0x54(work[2], 0x53(work[2],0), 0x53(work[2],1), 0x53(work[2],2) + 0.08);
if (0x53(work[2], 2) >= 3.0) { 0x64(work[2]); 0x9E(-1); }
```

It reads the entity's own position back with `0x53` and writes it forward with
`0x54`, so it rises 0.08 a frame, and at 3.0 it hands the object over with
`0x64` and retires. `0x64` had no implementation, so the body halted at `0x8f19`
6490 times and the object rose forever.

**`0x64` (`FUN_0025f700`) detaches an entity from the bone it is riding**, and
the order of its four statements is the whole point:

```c
FUN_0025d6c0(sel, DAT_00355044);              // select
sVar1 = cur[+0x192];                          // parent slot
if (sVar1 >= 0) {
  if ((char)cur[+0x194] >= 0) {               // a rigid bone, not the middle case
    FUN_0020dc88(&pool[sVar1], cur[+0x194], cur + 0x20, out);
    memcpy(cur + 0x20, out, 12);              // bone-local -> world, in place
    cur[+0x4C] = FUN_00227798(cur[+0x20], cur[+0x24], cur[+0x28]);
  }
  cur[+0x192] = 0xFFFF;                       // detached
}
```

An attached entity's `+0x20..+0x28` is a **bone-local offset**, not a world
position — `FUN_0020cdc0` branches on `+0x192` for exactly that reason. Clearing
`+0x192` without resolving it first drops the object at whatever small offset it
was carrying, next to the world origin. The bake is what makes a handover look
like a handover, and it is also why the rise in subproc 3507 is authored in bone
space: the object is climbing relative to whoever is holding it right up to the
frame it is let go.

Every piece was already in the port — `parentSlot192`, `attachBone194`,
`FUN_0020dc88_bone_point` in `psc3_skeleton`, and the `terrainHeight`
(`FUN_00227798`) hook. What was missing was the palette lookup, which lives in
`PortRuntime` because the script has no view of `DAT_00357e00`.

The chain now runs to its last record with **zero unimplemented opcodes** — 237
event records, four dialogue lines, the player released at the end.

One repro note: `--arm-stream d530:13400` runs the stream's first ten records
*twice*. That is the harness, not the scene. Body `0x8f41` teleports the lead to
`(-0.653, -1.658)`, which is on the tile-`0x80` panel, and arming the stream
directly skips whatever the panel's own trigger path latches — so the panel
re-arms it once. Reached through the panel it does not happen, and the second
pass runs to the end either way.

**And `0xd530` was the wrong stream.** The scene the doorway actually reaches is
`0xd780`, armed at `0x4049` by the last floor-panel group, and it is 43 records
long. Three more opcodes stood in front of it, each one named by the halt on the
last:

- **`0x56`** (`FUN_0025efa8`) sets an entity's size, and subproc **4192**
  (`0xa03c`) hits it at `0xa10a` on its *first* frame, so the stream never
  started. Two expressions, the second over `fGpffff8c44` = 100000.0. The work is
  `FUN_00229ef0`, and the thing worth copying exactly is that it re-derives the
  collision box from the **descriptor** rather than scaling what is already
  there, so repeated calls do not compound: `+0x54 = scale * desc[+0x08]`,
  `+0x58 = scale * desc[+0x0C]`, `+0x14C = +0x150 = scale`. It also writes
  `+0x140`/`+0x144`/`+0x148`, the draw cull box `FUN_0020c810:48` reads as
  `max(+0x144, +0x140)`; the port does no entity culling, so those three are
  named rather than invented into fields nothing reads.

- **`0x8E`** (`FUN_002610a8`) is **the scene transition**, and it is the last
  statement of the whole cutscene, at `0xab41`:

  ```c
  FUN_00267da0(0x31e668, 0x58bed0, 0xc);   // the lead's position -> arrival spawn
  DAT_003551f8 = param;
  DAT_003551ec = 0x20001;
  ```

  **This is the one that read as a hang.** The body ahead of it waits on the
  fade-out, releases the player, drops the camera and sets flag `0x523` — then
  asks to leave. Halting there left the slot re-entering and halting every frame,
  4071 times in one run, with the screen already faded to nothing: the scene had
  finished and simply could not leave. See *Leaving for another scene* below for
  what it does now.

- **`0x13F`** (`FUN_002604a8`) publishes a type-`0x28` rig's two children into two
  work slots. Operands only, and deliberately: the fields it reads (`+0x198` /
  `+0x19C`) are filled by `FUN_002d2f40`, type `0x28`'s own behaviour, which
  allocates types `0x27`, `0x26` and `0x19` and hangs them off each other by bone
  role — `0x26` on the rig's role-1 bone, `0x19` on the `0x26`'s role-2 bone with
  the index **negated** (`FUN_0020cdc0`'s middle, non-rigid branch) and `0x27` on
  its role-1. That behaviour is unported, and `+0x198`/`+0x19C`/`+0x1A0` already
  carry three different per-type meanings in `OriginalEntity`. Writing the work
  slots from fields nothing populates would put stale indices in front of every
  opcode that later reads them, which is worse than not writing them. It is one
  contained job — `FUN_002d2f40` is 40 lines and every piece it needs is already
  ported — but it is a storage change and belongs with the behaviour, not here.

With all four, `0xd780` runs its 43 records with **zero unimplemented opcodes**,
ending on the last at frame 15874 with the player released in the doorway and no
object-script slot left spinning.

### Leaving for another scene

`0x8E` now performs the change rather than only reporting it, and the shape of
that is worth writing down because almost none of it is in the opcode.

**Nothing loads where the request is made.** `FUN_002610a8` writes three globals
and returns; `FUN_002239c8:22`, at the top of the *next* frame, is what spends
them:

```c
if ((uGpffffb27c != 0) && (1 < iGpffffadbc - 9U) &&
    ((uGpffffb27c & 2) == 0 || FUN_0025d238(0) != 0)) { ... FUN_0022a418(); }
```

`uGpffffb27c` is `DAT_003551ec` and `iGpffffadbc` is `DAT_00354d2c`, the game
mode — the unsigned compare excludes modes 9 and 10 and nothing else. Bit 1 of
the request would defer the load until the fade-out has bottomed out; `0x8E`
writes `0x20001`, so it does not, because the script has already done that
waiting itself with `0x86`. `PortRuntime::FUN_002239c8_service_scene_change` is
that gate, called from the same place in the frame — ahead of the pad publish, so
no frame ever runs half on one scene and half on the next.

**Bit `0x20000` is the whole of what makes this a cutscene handoff.**
`FUN_0022a418:49` turns it into `DAT_003555d3`, and that changes which of two
selectors names the destination:

| | ordinary scene | group 0xE |
|---|---|---|
| bundle | `(DAT_003551f4, DAT_003551f0)` | `(0xE, DAT_003551f8)` |
| descriptor list | searched by index | `FUN_0022a238`'s entry 14, indexed directly |

and `0x8E` **writes only `DAT_003551f8`**. `DAT_003551f4` and `DAT_003551f0` are
left naming the scene being *departed*, which matters more than it looks:
`FUN_0022a418:50` copies `DAT_003551f4` into `DAT_00355208`, the map-prop bank.
So a group-0xE scene pulls its bundle out of MCB0 section 14 while its props
still come from the bank of the stage that sent it there. `loadSceneForCurrentMap`
used to read the bank off the loaded scene's own section, which is the same thing
for every load the port could previously do and asks for the non-existent bank 14
for this one. It now adopts the loaded section only when `DAT_003555d3` is clear.
The map-cycle path clears the flag for the same reason `FUN_0022b300` writes
`0x2001` on every scene it walks to.

`s01_e012`'s doorway asks for destination 1, so the scene it leaves for is
**`s14_e001`**:

```
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s01_e012 \
    --arm-stream d780:2 --frames 3000
[scene] s01_e012 -> s14_e001 (request 0x20001, frame 2533)
```

Reached the honest way it is the same thing 13 000 frames later: the opening
cutscene has to finish before the doorway is live at all. The per-frame entry
`0x24be` gates the whole floor-panel chain on `work[0x0d] == 0`, and the opening
holds that non-zero for its entire run — which is why `--scr-report` shows the
panel sites at `0x3d43`..`0x402c` with far fewer tests than the ones in `0xb439`.

Two things this does **not** yet do, both of them in the arriving scene rather
than in the transition:

- `s14_e001`'s own script halts immediately — `0x3C` at `0x803` (init), `0x117`
  at `0x15b5` (start) and `0x46` at `0x208f` (per-frame). So the scene loads,
  places its lead and draws, but does not run.
- The screen comes back on its own. In the original the fade-out block is left
  at `0x1FE0` across the load and the arriving scene's script is what reveals;
  the port clears it in `resetLeadPlayerForLoadedMap`, which is a documented
  divergence that predates this and is what stops the arrival being black while
  the script above cannot run.

### Arriving: what a group-0xE scene needs that no field scene did

`s14_e045` — where `s01_e024`'s floor panel goes — halted on its *first*
statement and again on frame 2. Four opcodes and two missing lines of
`FUN_0025b6d0` later it runs with **zero unimplemented opcodes**, and three of
the six are things only a scene reached this way ever exercises.

**`0x3C` (`FUN_0025daf8`) is the other half of the handoff.** Its whole body is
`FUN_0025c258(&gp0xffffb298)`, and gp 0xffffb298 against the 0x00359F70 base is
**0x00355208 — the map-prop bank**. Ghidra renders that one address as
`iGpffffb298` inside `FUN_00229980` and as `DAT_00355208` inside `FUN_0022a418`,
which is why `opcode_dispatch_tables.md` names it an "entity descriptor counter"
on the strength of the gp spelling alone. It is the bank type ids
`0x272..0x371` resolve against, and since `0x8E` never writes `DAT_003551f4`, a
scene arrived at through `0x8E` inherits the *departing* stage's bank. `0x3C` is
how it names its own: `s14_e045`'s init opens with it and asks for bank 10.
Without it the scene spawned nothing at all; with it, 19 entities.

**`0xE7`/`0xE8` (`FUN_00265290`) are a handle on the scheduler's own gate.**
`DAT_00355064` is `uGpffffb0f4`: 0x00359F70 − 0x355064 = 0x4F0C, and gp
0xffffb0f4 is −0x4F0C. So this pair is not an unrelated global the way
`analyzed/ops/0xE7_0xE8_*.c` reads it — it is the word a scheduler record with
bit 15 set in its gate waits on, and `0xE8` lets a body open its own gate.
`s14_e045`'s stream is built out of that: 52 records, most gated on a bit, and
each body ends by setting the bit the next record wants.

**And `FUN_0025b6d0` was missing two of its five statements**, both invisible
until now:

- `DAT_00355064 |= 0x6000` — a scene *starts* with the text gate open. Every
  scene the port ran before opened with dialogue, which raises those bits as a
  side effect, so nothing noticed. A scene whose first gated record is not
  dialogue has nothing to open it and the channel never advances.
- `if (DAT_003555d3) *(u32 *)(DAT_00355060 + 0x68) = 0x32` — the group-0xE
  branch, which had never been reachable. That is a *byte* offset into a dword
  array, so it is work word 0x1A. It shows up as `[26]=50` in `--scr-report`'s
  work dump on the transitioned run and is absent from a cold
  `--scene s14_e045`, which is the cheapest confirmation that `DAT_003555d3` is
  being carried correctly across the handoff.

The other two are ordinary: **`0x46`** (`FUN_0025dff0`) is the camera *cut* to
0x41/0x43's move — six expressions, an eye triplet and a look-at triplet, into
the already-ported `FUN_00217d70`; and **`0x62`** (`FUN_0025f548`) finds a pool
slot by its `+0x95` tag. `analyzed/ops/0x62_*.c` reads that one as a second
entity pool at `DAT_0058d120` with indices 80..324, and it is neither:
`DAT_0058beb0 + 10 * 0x1D8` is `0x58D120` exactly, so it is slot 10 of the one
pool, and the `>> 3` on a counter that runs in eighths returns an ordinary slot
index.

#### Where it stops now, and why that is the battle system

`s14_e045` reaches record 4 of its 52 and holds. Subproc `0x7E1` at `0x379e`
selects `work[0x28]` — pool slot 11 — and spins on `objreg(current, 5) & 0x10`,
which is entity `+0x06` bit 0x10.

Slot 11 will never set it, and that is not a port bug:

- The script spawns slots 11 and 12 as types 4 and 6, then **writes their type
  ids negative** (−4 and −6) through object register 0. `FUN_00239ce0`'s
  dispatch ends `if (0 < sVar1) (*(code *)(&PTR_FUN_0031c6c0)[sVar1 - 1])(...)`,
  so a negative type is skipped outright — the original does not tick them
  either. They are cutscene props, posed by the script.
- `+0x06` bit 0x10 is a pure *input* to the animation walk: `FUN_00225c90`
  returns early on it and never sets it. (Its own end-of-timeline flags are
  1/2/4/5, and the `& 0x10` in its tail reads `+0x08`, a different field.)
- Every write of `+0x06 |= 0x10` in the executable is in `FUN_0022ff20` or the
  **0x24xxxx block** — `FUN_0024a870`, `FUN_0024ac88`, `FUN_0024b410`,
  `FUN_0024b7d0`, `FUN_0024bae0`, `FUN_0024bd30`, `FUN_0024c058`,
  `FUN_0024c538`, and `FUN_00242df0` behind object method 2. None of them has a
  caller in `src/`, so they are reached only through the battle actor vtables.

So `s14_e045`'s opening is not a field cutscene that leads to a battle; it is
already being driven by the battle module, and the first thing needed to get
past record 4 is a slice of that module rather than another opcode.

#### The floor panels are a 4-bit code

`--scr-report` used to print a centroid per panel bit. It is grouped by the whole
low byte now, because that is how the script reads them: the exit chain at
`0x3d25` is eight branches that each spell out all four of `0x10`..`0x80`, and
the one that arms `0xd780` is `!0x10 && 0x20 && 0x40 && !0x80`. A per-bit
centroid averages tiles belonging to different doorways and lands on a spot that
satisfies none of them — bit `0x40` alone reported `(1.50, 0.00)`, which is the
midpoint of two doorways at opposite ends of the room and carries no panel tile
at all. `s01_e012`'s six doorways come out as codes `0x28`, `0x3a`, `0x46`, `0x50`,
`0x62`, `0x84` and `0x9c`, and the one the transition wants is `0x62`, at
`(5.50, -2.75)`.

### The scene remembers where you came from: `0x3A` and `--from-scene`

`s01_e014`, the ship interior after the crab, halted its **start entry** at
`0x9a5` on an unimplemented `0x3A`, and everything the start entry had left to
do was lost with it — Cleo and Magnus were never spawned or bound, and the
event channel that sequences the arrival was never armed.

`0x3A` (`FUN_0025DAB8`) takes no operands and returns
`bcd(DAT_00354D80) * 0x100 + bcd(DAT_00354D84)` — the section and entry of the
scene the player **came from**, packed as decimal digits by `FUN_0025DA48`, so
section 14 reads back as `0x14` and a script literal like `0x0113` means
"section 01, entry 13". (`0x3B`, `FUN_0025DA78`, is the same read against the
scene that is loaded, `DAT_003551F4`/`DAT_003551F0`.)

**`DAT_00354D80`/`84` is not the `DAT_00354D78`/`7c` pair.** Both hold a
section and an entry and both mean "previously", which is how the port came to
have one where the original has two:

- `DAT_00354D78`/`7c` is written at the **end of every load**
  (`FUN_0022A418:409-410`). By the time the arriving script runs it already
  names the scene that just loaded, and its readers — `FUN_0022A418:120`,
  `FUN_00271220` — are comparing to decide whether the map genuinely changed.
- `DAT_00354D80`/`84` is written only where a change is **requested**
  (`FUN_0022B2C0`, `FUN_0022B298`, the debug map menu, the game-over path) and
  nothing in the load disturbs it, so it still names the departure while the
  destination's script runs. That is the only pair `0x3A` reads.

The port's `FUN_0022B2C0` was writing the first pair. Fixed, and the second
pair now exists.

`s01_e014`'s start entry is a `0x02` switch on `0x3A` with exactly one case,
`0x0113`:

- **from `s01_e013`** (the adjacent room): the short arm at `0x9b4` — 76
  entities, no party bind, no cutscene.
- **anything else**, which in practice is `s14_e001`, the crab: the arm at
  `0x9d1` — spawns and binds the two followers, then arms the event channel
  that fades up from white, drives the camera path for ~300 frames of dialogue,
  and hands control back with `0x6D 1`.

A scene loaded straight off the command line has no departure, so
`--from-scene sNN_eMMM` seeds the pair. Without it a direct load takes the
default arm.

### The rain, and the fold that was hiding in it

`s01_e013`'s deck is in a downpour on hardware and was dry in the port. The rain
is opcode `0x102`, which the port was consuming the operands of and dropping:

    0x102(1000, 150000, 50000, 6, 10, 0deg, 0deg, -1)

`FUN_00262250` divides four of those by 100000 and calls
`FUN_0021AC00(length, fall, rotateX, rotateZ, count, magnitude, height,
entity)` -- note the read order is not the call order. That is a 3000-record
pool at `uGpffffbb50`, 60000 bytes of `0x14`, walked by `FUN_0021AD98` with a
three-state machine per record in `FUN_0021AFA0`:

- **spawn** into a cylinder of radius `magnitude` and height `height`;
- **fall** at `fall * frameTicks / 32`, drawing a camera-facing sliver
  `0.012 x length` in world space -- four world corners each projected on their
  own, the way a hit spark's streak is, not a screen-space sprite like every
  other pool here;
- **splash** when a periodic `FUN_00227798` under the bottom of the streak says
  the ground has arrived: a flat ring on the ground that grows and fades over
  960 ticks and then respawns the drop.

With no entity the whole volume is anchored `magnitude` units down the camera's
line of sight at eye height, so it travels with you.

Three things worth keeping:

- **The live count only reaches half the target.** `FUN_0021AC00`'s growth loop
  is `added < target - liveCount` with both sides moving, exactly like
  `FUN_0021BD30`'s. Asking for 1000 gets 500, and the hardware reads 500 at
  `0x355AA0`.
- **Opcode `0x100`'s pool selector is 1-based.** `FUN_002620A8` computes
  `index = byte - 1` and takes 1..6, so the rain's gate is byte **1** and the
  haze field's byte 5. Byte 0 selects nothing.
- **The two `if`s that reschedule the ground probe overlap.** `height > 5`
  writes 100 and `height > 1` then writes 50 over it, so the 100 is unreachable.
  Checked in the disassembly at `0x0021B26C` before reproducing it.

#### FUN_00207DE8's colour fold, finally verifiable

The first build of this drew bright white bars instead of drizzle. A GS dump of
`s01_e013` settles why. Every one of the frame's 411 streak draws carries vertex
colour **(64, 64, 64, 120)** -- and `FUN_0021AFA0` passes `0xF0808080`. That is
`FUN_00207DE8:130-141`'s fold, `(c & 0xFEFEFEFE) >> 1`, applied because the
packet is textured: the streak reaches the GS at half grey and alpha 0.9375, not
white at a clamped 1.875.

The dump pins the rest of the state too: `ALPHA = 0x44`, i.e. `(Cs-Cd)*As+Cd`,
the ordinary source-alpha blend that `+0x0C`'s bit `0x4000` selects;
`TEST = 0x5000d`; `TEX1 = 0x60`; texture page `0x3300` at `256x256` PSMT8 with
the streak's UV box landing on `(251.4, 8.4)..(252.4, 39.4)`, a one-texel column
of sheet `0x19C`.

**This also fixes the hit sparks.** They go through the same `FUN_002190F8` and
were skipping the fold, which was recorded as unverified because no capturable
scene had a `FUN_002190F8` pool alive. The rain is one, and it says the fold is
real, so a spark is `0.9375x` white rather than `1.875x`.

Not reproduced, as everywhere else: `FUN_0020B6A0`'s `& 0xE0` clip reject, which
drops a quad whose corners have slid off the screen rect. The port draws a few
more streaks per frame than the dump does (470-ish against 411) and that is
where the difference is. `FUN_00218EE0`'s near test **is** modelled.

Splash counts line up where it can be checked: parked at the dump's own player
position, `--actor-report` prints `splashes=7` against the dump's 9.

### The backdrop is a model, and `FUN_0020C290` draws it

`src/ported/render/original_background_model.*` is `FUN_0022CE60` and
`FUN_0020C2F0`: the fog sphere `s01_e013`'s boat deck stands inside.

`FUN_0020C290` reads like a display-list kick sitting next to `FUN_0020C5A8`
and `FUN_0020F3E0` in `FUN_002239C8`'s draw block. It is not. It walks four
descriptors at `DAT_00345A18` -- stride `0x24`, counted *down* from
`DAT_00345A84` -- and hands each to `FUN_0020C2F0`, which draws a whole model.
It was the last call in that block the port did not have, and it is why the
deck sat in a black void where the retail game has weather.

It was found by patching `jr ra; nop` over one candidate function at a time in
PCSX2 and screenshotting. **EE code patches through the debugger interface do
take effect**, despite the MCP tool's warning about recompiled blocks, which
made the search cheap. The fog survived the map, the entity models, the sprite
pass, `FUN_002192C0`, the actor loop, the scene script and `FUN_00207DE8`
before `FUN_0020C2F0` turned out to be the last thing standing.

#### Where the model comes from

A **PSB4** record in the scene bundle's **category 2** -- the same category the
PSM2 map lives in. `s01_e013`'s is id `0x009E`, and it is the *first* category-2
record, which is why `loadFirstPsm2FromSceneResources` walked straight past it
looking for PSM2 magic. `s14_e001`'s pier uses the same one.

No script installs it. `FUN_0022A418:134` calls `FUN_0022CDE8(descriptor, 0)`,
which reads the **halfword at scene descriptor +0x08** and loads that id into
slot 0 with its shade byte seeded to `0x80`. Opcode `0xE5` is the other way in,
and `0xE6` sets a slot's shade and Z rotation -- see
`analyzed/ops/0xE5_load_backdrop_model.c`, which used to be filed as an audio
opcode.

The file is a 16-byte header and three sections: vertices (`s16` count, then
xyz floats at a stride of 12), primitives (22 bytes each), and a UV animation
script in the format the map's section G already uses. A primitive is a flags
halfword, four vertex indices and four three-byte groups, and `flags & 0x800`
picks how those three bytes read -- the same bit the map and PSC3 builders
test. Set, they are `(u, v, textureSlot)`; clear, a flat RGB.

Everything else is in the flags word, and every bit of it is confirmed against
a GS dump of the live frame: `0x7000` the blend mode, `0x0700` a CLUT bank
(dead for an 8-bit page), `0x00FE` **the vertex alpha, halved**, and `0x8000`
the UV animation selector. The colour is not in the file at all: `FUN_00211B80`
hands VU1 the literal `DAT_00808080`, which is why every backdrop vertex in the
dump reads `(128, 128, 128)`.

`s01_e013`'s model is a sphere: 312 vertices, 391 quads, 24 segments around and
13 rings, radius 35 at the equator closing to 10 at the poles. Its cloud band
is above the equator and its teal mist below, which is the order the dump's
quads land on screen. Every primitive names **global texture slot 8** -- the
map's ninth and last texture page, which no map primitive uses. That page being
referenced by nothing was the first hard evidence the backdrop existed.

#### Camera-locked, and never in front of anything

`FUN_0020BEC8:43-45` puts the model at **the camera eye's x and y**, with
`z = eye z + fGpffff808c` = 0.4. It rides the camera exactly, so a mesh 35 units
across reads as a cylinder wrapped around the whole scene however far you walk.

Its packets end in `MSCAL 0x228`, a different VU1 program from the map's
`0x13B`/`0x14B`, and the dump shows what that program does differently: every
backdrop vertex reaches the GS at **Z = 2** -- the far end of that frame's
2..1108 range, whatever the geometry's real depth -- with `PRIM.FGE` clear where
every map primitive has it set, and `ZBUF.ZMSK` set where the map clears it.
Fog off, depth write off, always behind.

The port draws it before the map with the depth test and the depth mask both
off, which is the same picture by the only means GL offers. **It needs its own
projection.** The world is drawn with a far plane of `drawDistance + 8` -- 28
units in `s01_e013` -- and this model reaches 50 units from the eye, so the
frustum the map uses clips every one of its primitives away. The first build
loaded, parsed, transformed and submitted all 391 quads and drew nothing at
all. The backdrop pass pushes the far plane to 4096 instead, which costs
nothing because it neither tests nor writes depth.

#### What is checked, and what is not

`--render-report` prints the model and the quads it builds; `--no-background`
turns it off for an A/B. `--dump-scene-resources <dir>` writes every decoded
record of a scene bundle, which is how the PSB4 was read in the first place.

Brightness lines up: a backdrop-only frame measures `(27, 36, 37)` mean RGB in
the port against `(23, 32, 33)` in a retail frame with everything else patched
away. `s01_e024`, `s01_e012` and `s14_e001` reports are unchanged but for the
new load line.

Not checked: a pixel-for-pixel frame against retail. The one player position
where the port's camera lands exactly on the retail camera -- `(14.112, 2.5,
5.5)`, where `s01_e013`'s forced-camera zone engages -- renders a near-white
frame in the port, and `--no-background` shows the backdrop is not what causes
it. That white-out is its own bug and is not chased here.

### Which door you came in by: `DAT_00325340`

Two doors lead from `s01_e014` into `s01_e013` and two lead back, and the port
put the player through the same one every time. The doorway a scene drops you
at is carried by the **warp**, not looked up by the destination:

- Opcode `0x8C` (`FUN_00260F78`) reads six expressions — section, entry, flags,
  then an x/y/z scaled by 100000 — and hands them to `FUN_0022B2C0`, which
  stores the coordinates in `DAT_00325340/44/48` and publishes
  `DAT_003551EC = flags | 1`.
- `FUN_0022A418:212-218` spends them, in this order:

      if (DAT_003551EC & 0x2000) DAT_00325340 = DAT_003253B4;   // the scene's own
      if (DAT_003551EC & 1)      pool slot 0 position = DAT_00325340;

  `DAT_003253B4` is the spawn `FUN_0025B600` reads out of the scene's own
  defaults block. Bit `0x2000` means "nothing sent me here", and only
  `FUN_002000C0`'s boot request (`0x2001`), `FUN_0022B300`'s map-walk and the
  debug map menu raise it. A door warp leaves it clear, so **the warp's own
  coordinates survive the load**.

The port read `FUN_0025B600`'s spawn unconditionally and never looked at
`DAT_00325340` at all — the comment on it said "written and held rather than
read", which was accurate and was the bug. Both bits are honoured now, and the
command line's `--scene` seeds `0x2001` the way the boot path does, so a cold
load is unchanged.

The four warps, for reference:

| from | trigger | to | spawn |
| --- | --- | --- | --- |
| `s01_e013` @`0x57C2` | `0x61` mask `0x2`, stand at `(8.50, 2.50, 3.00)` | `s01_e014` | `(9, 2.5, 5)` |
| `s01_e013` @`0x57EC` | `0x61` mask `0x8`, stand at `(8.50, -2.50, 3.00)` | `s01_e014` | `(9, -2.5, 5)` |
| `s01_e014` @`0x0B0C` | `work[0] == 1` (`0x61` mask `0x4` @`0x141B`) | `s01_e013` | `(7.75, -2.5, 2.75)` |
| `s01_e014` @`0x0B49` | `work[0] == 2` (`0x61` mask `0x8` @`0x1435`) | `s01_e013` | `(7.75, 2.5, 2.75)` |

`s01_e013`'s defaults block happens to hold `(7.75, -2.5, 2.75)`, which is why
one of the four doors always looked right.

**The facing is not carried with it.** `FUN_0022A418:188` saves
`DAT_0058BF0C` — slot 0's `+0x5C` — before `FUN_00229C40` rebuilds the entity,
and `:220` puts it back **only** when the request has bit `0x80000`. These
warps send flags `0`, so the player really does arrive on the entity record's
default heading, and it is the destination that turns them: `s01_e013` does it
from an object script at `0x5ADE`, one `0x77` on object register `0x0D` with
180 degrees. Ported as written, bit and all.

### What a scene already did: it was always the flag bank

The other half of "two doors" is that the room should not replay its opening
when you come back through one. That needed no new code — the game-wide flag
array at `DAT_00342B70` is not part of the scene script's memory and
`SceneScript::load` already carries it across a load — but it is worth writing
down which flags, because the gate is one `0x3D` in the start entry:

- `s01_e013` tests **flag 1319** at `0x3744` and arms the arrival cutscene's
  event stream at `0x375C` only when it reads 0. The cutscene sets it at
  `0x5F12`, at the end of the fight with the five type-`0x62` enemies it spawns
  — so the flag stands only once the fight is actually won.
- `s01_e014` tests **flag 1306** at `0x144E` and sets it at `0x2409`, and gates
  a second sequence on **flag 30** at `0x1477`.
- The two doors also leave a trail of their own: `s01_e014`'s exit sets flag
  1327 the first time either door is used and records *which* in flag 1328.

Verified with a single run that makes the whole loop:

```
orphen_port --disc-root disc --scene s01_e013 --no-audio --frames 6000   --from-scene s01_e014 --press-attack 400,420,...,980 --spell-power-scale 20   --place-slot 0,8.5,-2.5,3.0:1100-1300 --place-slot 0,7.25,2.5,5.15:1500-5800
```

which wins the fight (flag 1319 set at frame 640), leaves by the `-2.5` door
and arrives at `(9, -2.5, 5)`, comes back through the other one and arrives at
`(7.75, 2.5, 2.75)` — with `event streams armed: 0` on that second visit, where
the first armed one and dispatched thirteen records. `--place-slot` grew the
`:<first>-<last>` window for this; without it one run can only ever stand on
one door.

### Terrain triggers now print where to stand

The panel-code list under `--scr-report` groups by the terrain word's **low
byte**, which is right for `s01_e012`'s doorway codes and useless for a scene
whose zones live above it: four of `s01_e014`'s ten `0x61` tests use `0x100`,
`0x400`, `0x4000` and `0x10000` and had no entry in that list at all. Each
trigger line now carries a `--spawn` for the centroid of the triangles actually
carrying **that mask**, or `NO TRIANGLE CARRIES THIS BIT` when none do — which
is a port bug rather than a panel nobody stepped on.

Standing on each in turn is how `s01_e014`'s zones were inventoried:

| mask | what it does |
| --- | --- |
| `0x10` | the arrival cutscene; the spawn point sits on it |
| `0x4`, `0x8` | the room transition — both request `s01_e014 -> s01_e013` |
| `0x40`, `0x100` | the stairwell: fade to black, `0x6D` lock, then `0x46`/`0x47`/`0x48` driving the camera by hand every frame |
| `0x20` | `0x6D` lock, no camera opcodes |
| `0x400`, `0x10000` | `0x6D` lock and a `0x44` camera path |
| `0x80` | fires, takes no control |
| `0x4000` | not reached from a spawn; its centroid sits under a floor at `z = 5.5` |

`0x6D`'s lock does hold: `PTR_FUN_0031e0e8[10]` is `jr ra; nop` and
`OriginalPlayerController` reproduces that. The console line claiming state 10
was unported was stale and is gone.

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

### The smoke cloud, and the halt hiding behind it

The `0x400`/`0x10000` zone — the "which way do we go" argument — played its whole
dialogue and then never gave control back. The cause was one unimplemented
opcode, **`0x110` at blob offset `0x1ff8`**, hit 6619 times in an 8000-frame
run: the object-script slot is re-entered from the top every frame, so it halted
at the same instruction every frame and the `0x6D 1` that ends the beat was
never reached. Control now returns around frame 2400.

`0x110` and `0x111` share `FUN_00262CF0`, which reads three expressions into one
stack block and hands them on out of order: `FUN_00212DB0(expr3 / 100000,
expr1, expr2)` for `0x110` and `FUN_00212D60(...)` for `0x111`. So the operands
are a count, a packed `0xAARRGGBB`, and a scale. `0x110` reseeds every record;
`0x111` changes the parameters and leaves them alone — s01_e014 arms once with
`0x110` and then ramps the colour with a `0x111` every frame, 98 of them,
walking grey from `0x000000` up to `0x292929` and the alpha ceiling from `0x0A`
down to `0x04`.

**Nothing in the pool has a position.** A record is three 16-bit phase angles
plus a packed nibble triple, and the world position is rebuilt every frame
relative to the camera:

```
box[c]  = eye[c] + forward[c] * 1.5          (c = 0, 1)
box[2]  = eye.z  + (0.6 - 0.2) + forward.z * 2
u       = (phase[c] - (int)(box[c] * 65536/3)) & 0xFFFF
pos[c]  = u * 3/65536 + box[c] - 1.5
```

— a 3x3x3 box hanging in front of the eye, wrapping in all three axes, with the
alpha falling linearly to nothing within 1/16 of a turn of the seam so nothing
pops as it wraps. The packed word's top nibble is a shove counter: a particle
inside a 0.8 x 0.8 x 1.1 box around the lead's waist has it set to 15 whenever
the lead is moving, and while it is up the particle drifts an extra
`dir * counter * 2` a frame. "Moving" is `+0xA0 >= 3` — any animation but a
stand — or, when standing, an actual change in position.

`src/ported/entity/original_smoke_cloud.*` is all of that, stepped in
FUN_002192C0's first slot and reported by `--actor-report`.

**It is not drawn, deliberately.** `FUN_00212F38` builds a VU1 program's input,
not a GIF stream: 40 quadwords of template to VU address 32, then 32 positions
(V4-32 to address 6) and 32 four-byte attribute quads (V4-8 to address 47) whose
byte 0 is `(i & 3) * 10` and byte 3 the alpha. The template's GIFtag reads
NLOOP 1, EOP, PRE, NREG 9, PACKED, REGS = RGBAQ then four (UV, XYZ2) pairs, and
PRIM = triangle fan with TME, ABE and FST set — a flat, textured, blended quad
whose corners are `0x510` and `0x5F0` in 4-bit fixed, texels 81..95 on both
axes. **There is no TEX0 anywhere in the packet**, and no ALPHA register behind
that ABE, so which sheet those texels belong to and how it blends are not in
this function. Picking either would be a guess. A GS dump taken while the smoke
is up, read with `port/attic/gsparse.py`, settles both.

Nothing in this scene reads an event flag it does not also write — the arrival
branch is the only thing it inherits from the scene before it.

## Battle mode

`s14_e045` stopped because it was already being driven by the battle module.
This is the first slice of that module: the player half, against **`s14_e012`**
— a small section-14 scene with one ally and several enemies, and no opener to
speak of.

```
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s14_e012 \
    --frames 460 --hold-cross 300-360 --battle-report
```

### Battle entry is script-driven, and `docs/battle_mode_activation.md` is wrong about it

That document concludes that no SCR opcode enters battle mode and that the MCB
debug menu `FUN_00268e20` is the only code that writes `cGpffffb663`. Both
halves are wrong, and both are checkable:

- **`cGpffffb663` is `DAT_003555d3`**, which `FUN_0022a418:49` sets from bit
  `0x20000` of the scene request — the bit `FUN_002610a8` raises for every
  group-0xE destination. Group 0xE is MCB0 **section 14**, which holds 63
  scenes and is the battle section. The port already tracked that flag as
  `DAT_003555d3_groupEScene_`; it just never set it for a scene loaded straight
  from `--scene`, so a cold `--scene s14_e012` ran with battle mode off.
  `loadSceneForCurrentMap` now raises it when the loaded section is 14, and
  `--scr-report`'s work dump shows `[26]=50` on a cold load as a result —
  `FUN_0025b6d0`'s group-0xE branch, the same one the `s01_e012` handoff proved.
- **Opcode `0xBD` starts the battle.** `FUN_00263e80` evaluates four operands
  and hands them to `FUN_00242a18`, a method table on the selected entity. The
  port already implemented methods `0x70`/`0x72` as waypoint path-follow; the
  battle lives in the low numbers of the same table:

  | method | handler | |
  |---|---|---|
  | 1 | `FUN_00242de0` | stop the battle (`uGpffffb052 = 0`) |
  | 2 | `FUN_00243f80` | start it, building the party first if 3 has not run |
  | 3 | `FUN_002432d8` | build the party from the roster |
  | 0x78 | `FUN_00244cc0` | equip a spell into a loadout slot |

  `s14_e012` calls all four — method `0x78` nine times at blob `0x032f`..`0x0495`
  (all three slots of rows 3, 4 and 5, packed `0x300`..`0x502`), method 3 at
  `0x0816` and `0x0a2a`, method 2 at `0x09a5` and `0x0bb0`.

The pair `FUN_002239c8:117` tests is `DAT_003555d3` and `sGpffffb052`, and bit 0
of the second is exactly what method 2 sets.

Getting there needed one more opcode: **`0xAF`** (`FUN_002635c0`), three lines
that set `DAT_00355044` — the interpreter's `currentEntity_` — from roster entry
`idx`'s `+0x0A` pool slot. `s14_e012` runs it six times at `0x04f7`..`0x06aa`,
and it is where the port used to halt. With it the scene reaches **zero
unimplemented opcodes** and its per-frame entry runs.

### The loadout is a table, and it is the one the game ships with

`FUN_002432d8` clears five tables whose sizes pin their extents, then binds each
of the three assignable slots to a button:

```c
FUN_00267e78(0x31d3c8, 1000);   // party records, stride 100  -> 10
FUN_00267e78(0x31d7b0,  600);   // control blocks, stride 0x3C -> 10
FUN_00267e78(0x31da08, 0xFC);   // per-member spell aux
FUN_00267e78(0x31dc18, 0xF0);   // button masks, stride 0x28   -> 6
FUN_00267e78(0x31dd08, 0x3C);   // cooldowns, stride 10        -> 6 x 5
```

`DAT_0031d168` is `{0x10, 0x20, 0x40}` and `DAT_0031d174` is `0x80` — Triangle,
Circle, Cross, then Square. Post-`CONCAT11` layout, the same one
`sdl_gl_window.cpp` already builds `rawHeldPad` in, so these are pad bits
directly.

Which of five mask fields a slot ORs its button into is chosen by the **item's
kind byte**, record `+0x27` of the 0x28-byte item record `FUN_00229688` returns.
Square is not chosen by anything: `DAT_0031dc3c = DAT_0031d174` is written
outright, which is why the shield is on Square whatever is equipped.

The loadout itself is `DAT_003437a0`, seven rows of three, seeded by
`FUN_002294b8` from **`0x00318b50`**. Row 0 reads `05 07 01`, and the item
database names those:

| slot | button | item | kind | element | pair | class-1 states | effect type |
|---|---|---|---|---|---|---|---|
| 0 | Triangle `0x10` | `0x05` Hand of Pyro | 12 | fire | `0x8A` → `0x8B` | 111 → 112 | `0x013D` |
| 1 | Circle `0x20` | `0x07` Bite of Lightning | -2 | lightning | `0x8C` → `0x8D` | 113 → 114 | `0x0174` |
| 2 | Cross `0x40` | `0x01` Sword of the Fallen Devil | 0 | physical | `0x86` → `0x84`/`0x85` | 107 → 105/106 | `0x0139` |
| — | Square `0x80` | the shield | — | — | `0x90` → `0x91` | 117 → 118 | `0x01C7` |

`battle_logo_loaded.p2s`, a save state taken inside a real battle, holds exactly
`05 07 01` at `0x003437A0`. The effect type comes from a second table at
**`0x00324FC8`**, stride `0x12`, item id to entity type, terminated by a zero id.

### Hold to charge is one test, and the charge is a party-record field

`FUN_002462c8` writes control block `+0x0E`, the pending action byte, and
`FUN_0024a360` spends it: for a byte in `0x84..0x92` it computes
`state = byte + 0x3FE5` — states 105..119 — with bit `0x4000` riding along as a
restart marker the handler clears itself.

Every pair works the same way. A press ORs the button that fired into the *held*
word beside its trigger word; the release test is `held-word AND
currently-held == 0`. Between those two the action byte does not move, and the
state behind it accumulates `DAT_003555bc` per frame into **party record
`+0x3C`**, capped at `0x2D00` (`FUN_00249128`). `FUN_00249270(entity, 0x780)`
divides that, capped at `0x2580`, into the **charge level 0..4** — the number
`FUN_0024bae0` stamps into the effect entity's `+0x94`. That is the projectile
count.

The whole cycle, from `--battle-report`:

```
f=300 pad=0x0040 pending=0x86 action=0x06 state=0x0078 charge=0    slot=2
f=301 pad=0x0040 pending=0x00 action=0x86 state=0x006b charge=0    slot=2
f=321 pad=0x0040 pending=0x00 action=0x86 state=0x006b charge=32   slot=2
...
f=360 pad=0x0040 pending=0x00 action=0x86 state=0x006b charge=1280 slot=2
f=361 pad=0x0000 pending=0x85 action=0x86 state=0x006b charge=1312 slot=2
f=362 pad=0x0000 pending=0x00 action=0x85 state=0x406a charge=1312 slot=2
```

`--hold-triangle`, `--hold-circle`, `--hold-cross` and `--hold-square` each take
`<first>-<last>` in 1-based frames and hold the button across the range with the
pressed edge on the first frame only. A one-frame range is a tap, and the flags
accumulate, so `--hold-cross 300-360 --hold-cross 420-420` is a charge and then
a follow-up press. The battle module cannot be exercised any other way:
`--press-magic` and friends are one-frame pulses, which enter a charge and leave
it on the same frame.

### The sword: charge, close, and two follow-up slashes

Cross is the kind-0 arm — the only one of the four whose release is not a single
shot. It is four states rather than two, and the port had the shape of all four
but the substance of none, which read as "the animation is there and nothing
else is".

**Charging elongates the blade.** `FUN_0024b7d0` (107) writes
`FUN_00249270(entity, 6) + 700, over 1000` into the effect entity's `+0x150` —
its Z scale, which for `grp_0139` is the blade's length. A full `0x2580` charge
reaches `2.3x`. The port had that, but it accumulated the charge
unconditionally; the original gates it on `FUN_002d9b78` exactly as states 111
and 113 do, so the charge stops the moment the caster's ground ring closes.
`--battle-report` now prints `blade=` (that `+0x150`, in thousandths) and
`anim=`, which is the only way to watch either of these from a headless run.

**Release runs at the target.** `FUN_0024b410` (106) plays animation 8, sizes a
travel timer from the distance (`distance / 0.064`, which is one frame's step at
the nominal `0x20` ticks) and drives `+0x30`/`+0x34` along `+0x5C` every frame.
It ends in a swing on any of four conditions: arriving inside `0.7`, the target
leaving a `+/-pi/6` cone, `+0x0C & 0x262` (blocked by geometry), or the timer
expiring. It also hops when the target stands more than `+0x58 + 0.3` above.
The port had this whole body replaced by a `return 0`, on the reading that it
"cannot run without an enemy" — true of the *body*, but the four exits and the
hop are the state, and none of them were there.

The player can also cut the run short: `FUN_002462c8`'s action-`0x85` arm turns
a press of the latched button into a pending `0x84`, and Orphen swings from
wherever he has got to.

**With no target it is one frame.** `FUN_0024cf20` forces control block `+0x2C`
positive and, when zero, to 1. State 106 negates that to -1; the read below
negates it again to 1, which fails `< 2` and asks for the swing immediately.
That is why Orphen swings in place with nothing locked on — the original's own
arithmetic, not a stub.

**The follow-ups are presses, and they are timed.** This is the part the port
had wrong outright. `FUN_0024ac88` (105) is entered *once per press*, not once
per swing: every route in goes through `FUN_0024a360`, which sets `state60 =
0x84 + 0x3FE5` — state 105 **with bit 0x4000**. The restart bit *is* the press,
and the handler splits on `+0x06` bit `0x10`:

| `+0x06 & 0x10` | meaning | what it does |
|---|---|---|
| set | fresh entry | respawn the blade, arm a 60-frame swing timer, clear the bit |
| clear | a re-press | step the animation `0x33 -> 0x30 -> 0x31`, one slash per press |

so three presses give three slashes, and the third (`0x31`) only chains back to
the first while the entity named by the blade's `+0x19C` is still alive. The
port instead advanced the chain on the animation marker every frame, with no
press involved — which produced all three slashes from one button and no way to
stop at one.

**The return value is the input lock, not the charge.** `FUN_00249610` writes
the handler's return into `+0x62` and raises control block `+0x38` bit 0
whenever it is non-zero, and `FUN_002462c8` refuses a press while that bit is
up. `FUN_0024ac88` returns 0 in exactly one case: `+0xAA` bit `0x400` is up —
the animation's cancel window — and the animation is not already `0x31`. That
one line is what makes the follow-ups a timed input rather than a mash. The
port returned the incoming `+0x62` unchanged, so the window was always open.

#### The blade decides whether the swing is a fresh one, and it is not in `src/`

`FUN_0024ac88`'s entry gate has two doors into the fresh-swing setup, and this
is the piece that took longest to find:

```c
if (blade->anim != 2) {
    if ((entity->+0x06 & 0x10) == 0) { ...re-press: advance the chain... }
}
...setup...
```

Bit `0x10` is not it. `FUN_00248e98` and `FUN_00248ee0` both end in `+0x06 &=
0xFFEE`, which clears that bit — and state 106 sets the run animation on entry,
so **105 is always handed a character with `0x10` down**. The only door left is
`blade->anim == 2`, and nothing in the battle module puts it there.

Type `0x139`'s own behaviour does. `FUN_002da350`, in the `0x2Fxxxx` block that
is not in `src/`, opens with:

```c
if (blade->anim != 2) {
  if (caster.pendingAction == 0x0B)             FUN_00225bc8(blade, 2);
  if (caster.action != 0x84 && != 0x86)         FUN_00225bc8(blade, 2);
}
```

State 106 is action **`0x85`** — neither charging nor swinging — so the blade
resets itself on the one frame 106 runs, and 105 then takes the setup branch.

Without that handler the port put the blade on animation 1 and left it there,
105 read every entry as a re-press, and the chain-advance branch sat waiting on
`+0xAA` bit `0x200` — a *sword-swing* marker that the **run** animation state
106 had just started never raises on schedule. The visible result was Orphen
jogging in place for 39 frames before each swing, and a first slash whose setup
never ran: no blade respawn, no 60-frame timer, no `+0x19C`. Porting
`FUN_002da350` removed the run outright — animation `0x33` is up on the frame
after 106 now, where it used to stay on `0x08`.

The rest of that handler is the blade's own life: a `DAT_00343888` point light
on **its own** bone 1 offset `(0, 0, 0.8)`, radius 1.3 while live and fading by
a thousandth of a tick on put-away; the `FUN_002148a8` sweep while the caster
is in action `0x84`; a five-frame re-hit cooldown in `+0x62`; and a third-slash
damage bump — reaction `0x1B` and `bonus + ((bonus / 2) | 1)`, so the last hit
of the chain lands at about 1.5x.

#### Every attack ended in a soft lock until state 108 was ported

`FUN_0024a870` is the walk back to the mark, and it is where all four arms
finish. `FUN_002462c8` returns `0xffffff36` on `current == 0x87` **before it
reads a button**, so a state 108 with no handler is not a cosmetic gap — the
character freezes on its last animation frame and never accepts input again.
`--battle-report` had been naming it (`state 108 hits=32`) since the slice
landed.

Control block `+0x14`/`+0x16`/`+0x18` holds where the member stood when the
battle started, in tenths. Two exits: under `fGpffff8810` (0.2) it goes straight
to state 120; at or over it, a three-point spline walk home — start, midpoint,
mark — with animation `0x37` under 1.5 units and `0x0C` over, the midpoint
arced up by `min(distance, 8) * 0.125`, and the last fifth dropping `0x0C` to
`0x0D` for the slow-down. It reuses `FUN_00244318`'s kind-2 array, the same
follower a scripted path walks on, but fills the slot by hand: mode forced to 1,
duration `distance * 200` with no `<< 4`, and steer `0xB5` with flag bit 1 —
so the character keeps the facing the swing left it in instead of turning to
follow the curve.

Swinging in place moves nobody, so the no-target case takes the first exit on
its first frame. `--battle-report` now shows `f=409 action=0x87 state=0x406c`
followed by `f=410 action=0x06 state=0x4078`, and a press at `f=412` starts the
next charge.

#### The target was never re-validated

`FUN_00249610:146-164` runs before every state dispatch and writes an invalid
target back to the control block as `-1`. A candidate survives only if its
`+0x12A` is at least 1 and its `+0x95` — the party-slot byte — is at least 9;
the player is 1 and allies are 2..6, so only an enemy passes. The port skipped
the write-back entirely, which left `FUN_0024cf20`'s "if the target is zero make
it 1" parked in `+0x2C` for the rest of the scene, and state 106 measuring a
distance to whatever happened to be in pool slot 1. `--battle-report` prints
`target=-1` throughout now rather than `target=1`.

Mashing Cross every four frames after a charged release, from
`--battle-report`:

```
f=413 pending=0x85 action=0x86 state=0x006b anim=0x33   <- release
f=414 pending=0x84 action=0x85 state=0x006a anim=0x08   <- 106, one frame, no target
f=415 pending=0x00 action=0x84 state=0x4069 anim=0x33   <- 105, wind-up
f=426 pending=0x00 action=0x84 state=0x0069 anim=0x33   <- slash 1
f=432 pending=0x84 action=0x84 state=0x0069 anim=0x33   <- press taken
f=449 pending=0x00 action=0x84 state=0x0069 anim=0x30   <- slash 2
f=456 pending=0x84 action=0x84 state=0x0069 anim=0x30   <- press taken
f=485 pending=0x00 action=0x84 state=0x0069 anim=0x31   <- slash 3
f=559 pending=0x00 action=0x87 state=0x406c anim=0x31   <- 108, walk home
f=560 pending=0x00 action=0x06 state=0x4078 anim=0x31   <- idle, taking input again
```

Animation `0x33` is up on the frame *after* 106, which is the whole of "he does
not run first". The presses in between produce nothing: the window was shut, and
after the third slash there is nothing alive to chain against.

### No target is a branch, not a special case

`FUN_00249610:170` is `else if (lVar11 < 3)`, where `lVar11` is control block
`+0x2C`. Below 3 the whole face-the-target block is skipped and control falls
straight to the state dispatch — no special case, the original's own branch.
Pool slots 0..2 are the party, so "below 3" and "no enemy locked on" are the
same statement.

`FUN_0024cf20` forces that halfword positive and, when it is zero, to 1 — so the
report prints `target=1` rather than `-1`, and the `< 3` test still takes the
no-target branch. State 106 reads the same field and collapses to a single frame
for the same reason; see the sword section above.

State 106 also *negates* the halfword on the frame it commits, which is what
`target=-12` in the report means. `FUN_002493B8` reads it back through its own
negate so a caller still gets slot 12; `FUN_00249610` reads the raw halfword and
therefore stops turning for the duration of the run, which is correct — the run
does its own steering through a cone test rather than through the facing block.

This was the mode the whole battle slice ran in until the enemy table landed
with targeting. It is no longer: `s14_e012` binds five enemies, D-pad or stick
selects one, and the approach run — its four exits, its cone test and its hop,
ported from `FUN_0024b410` line by line — now actually executes rather than
taking `aimed < 2` on its first frame every time.

### The lead player has had no hit points since the port began

`FUN_00249610:84` abandons the character update when `+0x12A` minus `+0xBE`
falls below 1, so a lead with no hit points reads as a corpse and never
dispatches a state. Chasing that turned up two bugs that predate the battle work
and were invisible without it:

- `SceneScript::load` rebuilds `SceneScriptState`, and the party stat table is
  one of its members — so the `FUN_002294d0` pass that ran once at
  `initialize` was gone the moment a scene script loaded. In the original
  `DAT_00343688` is a global that outlives every scene. Re-seeded after the
  load.
- `FUN_0022a418` calls `FUN_00251dc0` at :206 and places the lead at :372-375,
  and nothing between those two lines touches the entity. The port's placement
  goes through `OriginalPlayerController::resetAt`, a port-side construct that
  begins `entity() = OriginalEntity{}` — so it threw the stats away every time.

Both together left the lead at 0/0 hit points, 0 attack and 0 defence in every
scene the port has ever loaded. Nothing noticed because `FUN_00216140` floors a
hit at one point, so the damage the player dealt still looked plausible. Both
standing scenes stay byte-identical at 3000 frames with the fix in, which is the
same thing said from the other side.

### What is deliberately not here

Named by `--battle-report` rather than left silent:

- **`FUN_0023fd30`'s second loop.** The battle master tick runs a *second*
  bytecode interpreter — 19 opcodes at `PTR_LAB_0031d118`
  (`src/PTR_LAB_0031d118.txt`) — over a master battle script and a per-member AI
  script off control block `+0x30`. The master script is stepped; the per-member
  ones are installed and then not, so the enemies stand where they spawned. The
  player never needed them: `FUN_00243f80` installs them only on members *other*
  than index 0.
- **The per-action camera framing and the reaction shots.**
  `DAT_00355C90`/`C94`/`C98` and `DAT_00354E84`/`E8C` all read zero, so
  `FUN_0023C340` falls through to the spline walk every frame.
- **Classes 3, 4, 5, 6 and 7,** and class 1's states 102 and 115.

Of the effect entities `FUN_00242df0` and the state handlers spawn — types
`0x118`, `0x139`, `0x13D`, `0x174`, `0x1C7`, and the `0x15B`/`0x15C`/`0x173`/
`0x178` the casts throw — only `0x118` is still reported `UNIMPLEMENTED` by
`--actor-report`. It sits where `FUN_00242df0` left it,
hidden by the `+0x08` bit 0 it spawns it with.

#### `uGpffffb052` and `DAT_00354fc2` are one halfword

Worth stating because keeping them apart cost a visible bug. `gp` is
`0x00359F70` and the offset spells `-0x4FAE`, so `uGpffffb052` resolves to
`0x00354FC2`; the decompilation uses both spellings for it, sometimes in
adjacent functions (`FUN_0023fd30` tests `uGpffffb052 & 8`, `FUN_0023c340` tests
`DAT_00354fc2 & 8`). The port modelled them as two variables, so
`FUN_00243f80`'s

```c
if ((uGpffffb052 & 2) == 0) { FUN_002432d8(0, 0); }
```

never saw the bit `FUN_002432d8:46` had raised. Starting a battle therefore
rebuilt the whole party a second time — and leaked a second type `0x1E3`, which
then ate the `FUN_002f1380` request every frame and left the real targeting
circle hidden. `--battle-report` now prints `sGpffffb052=0x0003` where it used
to print `0x0001`.

#### The shared hit effect hides itself, and nothing else hides it

Type `0x1E3` is the exception, and it drew as a room-height cyan slab across the
middle of the arena. `FUN_002432d8`'s last line spawns it with **no post-spawn
writes at all** — so it arrives from `FUN_00229c40` drawable (`+0x02` has no
`0x200`), at scale 1, at the world origin. Nothing in the spawn path hides it.
Its behaviour does, and that behaviour is not in `src/`; recovered from
`SLUS_200.11` at `0x002F13D0`, the whole function is:

```c
entity->depthBias133 = -0x18;
if (DAT_00355588 == 0) entity->halfword08 |= 1;      // hide
if ((entity->halfword08 & 1) == 0) { DAT_00355588 = 0; return; }
entity->animationA0 = 0;                              // rewind while hidden
if (DAT_00355588 == 0) return;
entity->halfword08 &= ~1;                             // a request: show it
```

`DAT_00355588` is a one-frame request word raised by `FUN_002f1380`, the
"put the hit effect here" setter every damage path calls with a position, a
scale and a height. So the effect is hidden by default and visible only on the
frames a hit asked for it — one shared entity teleported around the field rather
than one spawned per hit. Ported, with `DAT_00355588` owned by the runtime so
the damage paths have somewhere to write when they land. Nothing raises it yet,
which is why the effect now stays down.

The general lesson is worth keeping: an effect entity that a spawner leaves
untouched is not "parked", it is **live and full-size**, and its behaviour is
the only thing standing between it and the screen.

#### Guard, end to end

Guard is the one command that runs all the way through, and it is worth reading
as the template for the rest, because **the visible half of a command is not in
the state handler**. `FUN_0024cba0` (state 117) does almost nothing to the
shield: on entry it sets the shield entity's animation to 1, stamps its own pool
slot into the shield's `+0x94`, plays cue `0xE7`, and ORs `0x11` into `+0x04`.
It never unhides it. `FUN_002e7328`, the type `0x1C7` behaviour, is the rest —
and it is where every interesting decision lives:

- **The shield is re-placed, not attached.** No `+0x192` parent, no bone: the
  handler copies the caster's facing and XY across every frame and sits it at
  `+0x58 * 0.75` above the caster's feet. That is why `FUN_00242df0` spawns it
  with `+0x192 = -1`.
- **`+0xA0` is a four-step sequence, not a looping clip.** 1 raise (one frame —
  it unhides and immediately becomes 0), 0 hold, 2 close, 3/4 destroy. Each step
  advances on `+0x06` bit 0, the animation-finished flag.
- **The caster's control block ends the guard, not the state handler.** Every
  frame it is not already closing, the shield reads the caster's current action
  byte out of `DAT_0031d7bf`; anything but `0x90` drops it into the close.

**Where "blocking wears off" comes from.** Nothing counts frames. `FUN_002462c8`
keeps a held guard alive only while the button is down *and* the shield entity's
`+0xA0` is not 2 — and the only thing that sets it to 2 out of the hold is clip
0 running to its last timeline entry in `FUN_002e7328`. So the block's duration
**is the length of the shield's hold animation**, stated in the model rather than
in code, and read the same way by two functions that never talk to each other.
On `s14_e012` that is 500 frames: guard at 301, `0x91` armed at 801 with the pad
still held, shield closed at 802.

What is not ported is `FUN_0024cba0`'s damage half — the halved hit, the second
`0x1C7` spawned at the impact bearing, cue `0xE8`, and the `0x4066` guard break.
All of it is gated on `+0xBE`, which only an attacking enemy writes, so there is
nothing yet to test it against.

#### The ground ring: what a GS dump and a save state have ruled out

The ring (type `0x18F`, `grp_00a8`) draws with a hard-edged translucent box
around it that the real game does not have, and it lacks a shimmer the real one
has. A PCSX2 capture at max charge plus the matching `eeMemory.bin` settle most
of the candidates, so they are written down rather than re-tried:

- **The blend is right.** `ALPHA` is `0x48` — `(Cs - 0) x As + Cd`, i.e.
  `GL_SRC_ALPHA, GL_ONE` — on every ring draw, and the port picks mode 2 for all
  158 of the model's subdraws. `TEX0` is MODULATE/RGBA, `TEX1` bilinear, `TEST`
  alpha-discard-at-0. All as the port already has them.
- **The scale is exact.** The save state's live marker reads `+0x14C` 2.680 and
  `+0x150` 1.940; `FUN_002d9b78` as ported computes exactly those from a maxed
  `+0x3C`. The bone palette at `0x00357E00` confirms the transform too: the
  model's rest scale is 1.30, and every ring's matrix is
  `1.30 x +0x14C` horizontally and `1.30 x +0x150` vertically.
- **The pose is genuinely static.** `grp_00a8` animation 0 is a *single*
  timeline entry — column 4, 4 frames, looping — so the column never moves. All
  four rings in the save state sit at `+0xA0 = 0`, so the original is equally
  static. **The shimmer is not bone animation.**
- **The tall geometry is in the original too.** Bone 12 carries the bulk of the
  model at +/-0.4 in height, and the real capture's ring vertices span 130 GS
  units vertically, not the ~40 the ground disc alone would. The port is not
  drawing extra geometry.
- **The zero-scale bones collapse correctly.** Bones 2..9 and 14..17 have scale
  0 at column 4 in both, and the port's posed vertices for them are degenerate.

One thing the max-charge capture appeared to leave: **vertex colour**. Its ring
uses 8 distinct colours over 1,684 vertices, at two alphas only — 128 and 63 —
and 42% of them are `(0, 0, 0, 128)`: pure black, which under additive blending
adds nothing. *That is how the wall is hidden.* Read against that, the port's
alpha-**12** vertices looked like an invention. They are not — see the resting
capture below, which emits 192 of them — so this line of inquiry closes.
Ruled out along the way: colour-table overrun (13 entries, max index 12, none over), the
per-pass texture selector (`--entity-bound-texture` changes nothing), an
unresolved texture slot, and the `-1` pass skip (`FUN_00212058:95-100` skips and
continues exactly as the port does).

The other always-present battle effect, type `0x118` (`FUN_002d8ce0`), is **not**
the shimmer: `FUN_002d8b38` activates it as a *status-effect* aura, one of six
variants selected by its `+0x19A`, and raises the matching bit in
`DAT_0031DA6C`. It does spin continuously though —
`+0x5C += frameTicks * 0.00226893` — so it is an animated ring, just a
conditional one.

#### The ring's shimmer is the model's own UV animation script

Two `.gs` captures of a *resting* battle ring, four frames each, plus the pyro
one, settle what the shimmer is. It is not the pose and it is not the geometry:
what changes is **U**, on every corner of every pass, by whole texels, with
**V** fixed. Measured against `grp_00a8`'s authored subdraw table, per game
frame:

| pass | subdraw `texFlags` | bank | U per game frame |
|---|---|---|---|
| the alpha-12 pass | `0x850C` | 0 | none |
| wall layer | `0x8C7F`, `0x8C3F` | 1 | `+4` |
| — | `0x943F` | 2 | `+8` |
| ground layer | `0x9C7F` | 3 | `+1` |
| — | `0xA4BF` | 4 | `+11` |

The mechanism is the one the map already uses for the rain outside the windows,
which is why none of the searching for a bespoke ring animator found anything:

- **PSC3 header `+0x40` is a UV animation script**, byte for byte the map's
  section G. The header map in `psc3_model.h` stopped at `+0x38`.
  `grp_00a8`'s is four one-frame scroll tracks, `u` = 256, 512, 64 and 700
  sixty-fourths of a texel with duration −3, −3, −3 and −1 — which is exactly
  the table above, since the stepper's `hold` of 1 comes out as one step every
  two frames.
- **`FUN_00221E70`** reads it at load, raises bit 2 of the model record's
  `+0x04`, and seeds **four** running copies through `FUN_002256D0` /
  `FUN_002256F0` into record `+0x18`/`+0x1C`/`+0x20`/`+0x24`.
- **`FUN_00229C40`** sets entity `+0x08` bit 3 for those models, and
  **`FUN_00229F88`** hands the entity the copy its pool slot's low two bits
  name. That is why the pyro capture has two rings on screen at `+34` and `+62`
  of the same track: four copies, not one global counter.
- **`FUN_0020C810:209-211`** steps the entity's copy once per drawn entity, then
  points the draw context at it; **`FUN_0020EEC0:97-110`** uploads seven
  `(u, v)` pairs, scaled by 1/64, as `0x640702B0` into VU1 `0x2B0`.
- **`FUN_00212058:183`** puts the subdraw's texture bank — `texFlags` bits
  13..11 — into the draw header, and VU1 adds the pair at that index. Bank 0 is
  "does not animate"; banks 1..7 are tracks 0..6. `grp_00a8` uses banks 0..4 and
  ships exactly four tracks.

The port implements all of it: `Psc3Model::uvAnimationScript`,
`EntityModelBinding::uvAnimation` (the four copies),
`PortRuntime::attachModel` (the step and the seven-pair publish) and
`SceneObjectView::uvAnimationOffsets`, applied in `drawObjectModel`'s per-corner
emit. Verified by building the draw with the bank forced to 0 and diffing a
`--screenshot` of `s14_e012` at frame 400 against the real one, and by the
accumulators advancing 256/512/64/700 per step.

Two other things the same dumps settle:

- **The alpha-12 pass is degenerate in the original.** Its 48 draws all collapse
  to a single screen point, so the box the port draws is those passes failing to
  collapse — not extra geometry and not the wrong blend. They are single-pass
  primitives, `flags 0x901`/`0x903`, colour index 0, subdraw 1 at UV
  `(135,177)..(135,215)`.
- **The port's alpha-12 vertices are legitimate.** An earlier note here said the
  original emits no alpha-12 vertices at all; that was read off a max-charge
  capture. A resting ring emits 192 of them.

#### The ring drew untextured because one of the three model tables was missing

The ring was a white drum until its sheet was actually loaded, and none of that
was the draw path's doing.

`--model-report` reports its passes as `{bound+10:48 bound+8:224 bound+9:64}`,
which reads like three global slots but is not. Every one of grp_00a8's 200
primitives carries flag `0x800`, and `FUN_00212058:183-208` sends that form down
the *other* branch: the selector is moved into packet byte 5 and byte 6 goes to
`0x3F`, the entity's own bound slot. So all 336 passes draw from slot 37 — which
is what the capture shows, all six `texFlags` groups on one page, `tbp 13716`.
`FUN_002103d0` is where that number comes from: a slot's GS base is
`0x1680 + slot * 0x104` below 24 and `12000 + (slot - 24) * 0x84` above, and
slot 37 is `12000 + 13 * 132 = 13716`. The bound slot and the page agree.

Slot 37 was simply empty. `FUN_00221fd8` walks **three** model-record tables
looking for records whose `+0x06` is 100 and loading `+0x02` into `+0x07`;
`FUN_00221fd8_staticTextureBinds` walked two, on a note that
`PTR_DAT_003228c0`'s table "lives in BSS". It does not — the original takes the
*address* of that symbol as the table base, and the records are in the
executable's LOAD segment like the other two, 226 of them with 17 static binds.
`[146] mesh=00a8 tex=0197 slot=37` is one of them, and slot 37 is reachable from
nowhere else.

With the third table walked, the port's slot table on `s01_e012` matches
`eeMemory.bin` entry for entry across 32..37, 40 and 42..44, and the ring draws
as a translucent textured swirl with the UV animation running on it. `--frames
3000 --actor-report --scr-report` on `s01_e024` and `s01_e012` is unchanged
except for the texture cache's generation counter, which counts one more load.

#### The lead is placed *before* the script entries, not after

`FUN_0022a418` in order: `:206` `FUN_00251DC0` loads the player stats, `:214-224`
places the lead, `:265` runs the init entry, `:370` runs the start entry. The
port had its own placement *after* both, which quietly threw away every scene
that places the lead itself.

A section-14 scene does exactly that. `s14_e012`'s start entry, at `0x13C3`:

```
0x13C3  0x55  entity=(0x62 tag 1) x=-900 y=300 z=0     -> pool slot 0 to (-0.9, 0.3, 0)
0x13DE  0x77  entity=(0x62 tag 1) reg=0x0D value=575960 -> +0x5C = 5.7596 rad
```

`FUN_0025F548` (opcode `0x62`) searches from pool slot 10 for `+0x95 == tag` and
returns **0** when nothing matches — which is the lead, so "tag 1" resolves to
the player either way. Object register `0x0D` is `+0x5C` scaled by 100000 and
wrapped, and 575960 → 5.7596 → **−0.5236 rad, −30°**. Both were being overwritten
a few lines later by the port's own spawn guess.

Confirmed against two PCSX2 save states taken either side of a debug-menu battle
load. In the loaded one `DAT_003555D3` is 1, `DAT_00354FC2` is `0x0002` — the
party is built, the battle has not started, so nothing in `FUN_0023FD30` has run
— and pool slot 0 is already at `(-0.900, 0.300, 0.000)` facing `-0.5235839`.
The port now reproduces that exactly. Note the original's placement block is
*conditional*: `DAT_003551EC` bit 0 for the position and bit `0x80000` for the
facing, and both save states have that word at **zero**, so on hardware neither
block runs and the script's placement is the only one there is. That is also why
the facing cannot be carry-over from a previous scene — there was no previous
scene.

It changes what the player's turn onto the first target looks like, which is the
point: from `(-0.9, 0.3)` facing −29°, the first target sits at bearing −34°, so
the turn is a **single frame** through the ±5° snap band instead of an eight-frame
swing from due east.

One line of the standing regression moves with it: `s01_e012`'s
`[nav] follower graph ... reachable from` seed goes from `(-0.12, -2.699, -1.5)`
to `(-8.371, 0.024, -1.5)`. That is `FUN_0022a418:374`, which reads the lead's
position *after* the start entry — so seeding from the script's placement rather
than from the port's guess is the correction, not a regression. Reachability
(159/3948) and the 900-frame capture are unchanged, and `s01_e024` is untouched.

Two things the same save-state diff turned up that are **not** fixed:

- Every enemy on hardware is turned to face the player at spawn — slots 11..14
  carry `2.7909`, `2.1516`, `1.5819`, `2.5425`, and each is `atan2` of the
  offset to `(-0.9, 0.3)` to four decimals. The port leaves them all at the
  placement default `1.5708`, because the type `0x8A`/`0x80` behaviours
  (`FUN_002484D0` / `FUN_00248888`) are still `UNIMPLEMENTED`.
- Hardware slot 10 is type `0x072` with `+0x95 = 0x50`, bound to the encounter's
  party record `[4]`. The port spawns the same placement as a streamed prop
  (`0x37C`) and leaves that record unbound.

#### One frame of `FUN_0023FD30` decides how the first target appears

`FUN_0023FD30`'s two arms are exclusive, and the countdown is tested **before**
it is stepped:

```c
FUN_0023fc08();
if (sGpffffb054 == 0) { ...master script...; FUN_002462c8(); FUN_002f1680(); return; }
sGpffffb054 = FUN_00248e00(); x3
if (sGpffffb054 == 0) { ...spawn one 0x192 cursor per bound record...; }
```

So `FUN_002462C8` — the command input, and the only thing that assigns the
player a real target — cannot run until the frame **after** the cursors appear.
The port ran it every battle frame, and stepped the countdown before testing it,
which got the ordering wrong twice over: the target was assigned fifteen frames
early, and again on the very frame the cursors spawned.

**That one frame is what the grow-in looks like.** `FUN_002D86B0` spawns every
cursor on animation 13, and `FUN_002D73E8` only moves a cursor off 13 through
its *"not the player's target"* branch:

```c
if (0 < FUN_002493b8(control)) {
  if (cursor->+0x19A == target) { if (cursor->anim == 10) -> 12 }   // grow-in
  else if (cursor->anim != 10)  { -> 10 }                           // idle
}
```

With no target yet on the spawn frame, all five cursors take the second branch
and are on animation 10 immediately; the next frame the target is assigned and
its cursor goes 10 → 12, the twelve-column grow-in at one frame a column. With
the target already assigned, the first branch is taken instead, does nothing at
animation 13, and that cursor sits out all eight columns of 13 at **four frames
each** before it can even reach the grow-in. 32 frames of stall in front of a
12-frame animation, which is the quarter-speed the first bracket appeared at.

Timings on `s14_e012` after the fix: cursors spawn at frame 260, the target is
assigned at 261, every other cursor is idle from 261, the target's grows in
262–274 and settles into animation 11 at 278. The player's turn onto it runs
262–269.

#### Hand of Pyro flies along the caster's facing, and nothing else

Worth stating because it is the only thing aiming the volley. `FUN_002DAB70`
takes the target only to compute two *scalars* — `+0x1BC`, the horizontal speed,
and `+0x1B0`, the climb — as "distance over flight time". The **direction** is a
straight copy of the caster's `+0x5C` at the moment of the throw:

```c
ball->facing5c = pool[casterSlot].facing5c;   // param_7, not param_4
```

`FUN_002DAE60` then resolves that into velocity every frame, and the order here
is the whole ballgame:

```c
+0x1A8 = +0x1BC * FUN_00305130(+0x5C);   // cosf -> +0x30, the X delta
+0x1AC = +0x1BC * FUN_00305218(+0x5C);   // sinf -> +0x34, the Z delta
```

The port had those two swapped, which mirrors every fireball about the 45°
diagonal — it reads as the volley flying off sideways or backwards no matter
where Orphen is pointing, and it is invisible while the caster happens to face
along an axis. `FUN_00305130` is `cosf` and `FUN_00305218` is `sinf`; the same
pair is spelled the same way round in `FUN_002493F0` and in opcode `0x5E`/`0x5F`,
so the convention across the whole port is cos into `+0x20` and sin into `+0x24`.

With it fixed and a target held, a fireball walks `(0.40, −0.96)` →
`(0.84, −1.95)` → `(1.27, −2.94)` — a heading of −66.5°, which is both the
caster's facing and the bearing to pool slot 12 at `(1.39, −3.19)`.

The volley's fan is not computed as a fan: `+0x1C6`, the link's index in the
chain, picks one of four fixed spreads (0 rises only at charge 5, 1 turns right,
2 turns left, 3 dips), and each is ramped by elapsed life over `1728`. So a
five-link chain arcs apart because its links disagree, not because anything
divides an angle.

##### `FUN_002DB230`, type `0x173`: recovered from the ELF, ten instructions

The burst a fireball turns into on contact. It is not in `src/` and — unlike
`FUN_002E4C00`, the lightning flash it is shaped like — Ghidra has no function
defined at the address either, so it came out of `SLUS_200.11` directly:

```
002db230  lhu   v0, 8(a0)
002db234  lhu   v1, 6(a0)
002db238  ori   v0, v0, 0x4000
002db23c  andi  v1, v1, 1
002db240  beqz  v1, +
002db244  sh    v0, 8(a0)      <- delay slot, so the store always happens
002db248  j     FUN_00265ec0
```

Raise `+0x08` bit `0x4000`, then destroy on `+0x06` bit 0, the
animation-finished flag. **The burst has no timer of its own**, so with no
handler nothing ever destroyed it and the explosion stood at the point of impact
for the rest of the scene. With it, the burst lives out animation 0 — about 60
frames on `s14_e012` — and goes.

Two effect types on the spell paths were in this position; the other,
`FUN_002E4C00`, was already recovered the same way. Nothing on the Hand of Pyro
or Bite of Lightning chain is reported `UNIMPLEMENTED` now.

#### Bite of Lightning, end to end

Circle's spell, the kind `< 0` arm, and the second command whose whole chain is
ported. It is not shaped like Hand of Pyro: the fire spell throws a projectile,
this one marks a spot on the ground while it charges and then hits everything
standing on it.

```
FUN_002462c8   Circle press -> pending 0x8C (hold) / 0x8D (release)
FUN_0024a360   pending + 0x3FE5 -> state 113 / 114
FUN_0024c538   113, the hold      -> the aim marker, then the charge
FUN_0024c910   114, the release   -> effect +0x60 = 1, +0x94 = level
FUN_002deae8   type 0x174, the hand effect -> FUN_002de650 on +0x60 == 1
FUN_002de650   the launch -> one 0x15C damage disc, one 0x178 flash,
                              and one 0x15C spark per victim
```

**Three entities are easy to confuse.** The ring at the caster's *feet* is type
`0x18F` (`FUN_002d9c88`), the charge gauge. The effect in the caster's *hand* is
`0x174`. The blue circle that grows on the ground at the *landing spot* is
neither — it is the single type `0x1E3` in `DAT_0031DAD0`, moved and resized
every frame by `FUN_002f1380`, whose only callers anywhere in `src/` are the six
elemental hand effects. It is the targeting marker, not a general hit flash.

##### Where the circle goes when there is no target

`FUN_002493f0` is the whole rule and there is nothing random in it:

```c
long FUN_002493f0(entity, Vec3 *out)
{
  long target = controlBlock(entity->byte95 - 1)->target2c;
  if (target < 2) {                        // no target
    out->x = 2 * cosf(entity->facing5c) + entity->posX;
    out->y = 2 * sinf(entity->facing5c) + entity->posZ;
    out->z = entity->posY;
  } else {
    *out = partyRecord(byte95 - 1) + 0x28; // what 113 tracked onto the target
  }
  return target;
}
```

**Two world units straight ahead of the caster.** It looks like it varies by
battlefield only because it varies with which way the caster happens to be
facing. Note the threshold is `< 2`, where `FUN_00249610`'s face-the-target block
uses `< 3` — genuinely different numbers. (`FUN_00305130` is `cosf` and
`FUN_00305218` is `sinf`, not the other way round.)

With a target it is the target, exactly: `FUN_002493f0` reads the three floats
state 113 has been tracking there, so on `s14_e012` the landing comes out
`(1.389, -3.188, 0.000)` against pool slot 12's `(1.39, -3.19, 0.00)`.

##### The character turns onto the target, and the port had that as a stub

`FUN_00249610:166-310`, between the state translation and the dispatch. It was
the one place in the battle module the port left as a counter rather than code,
on the reasoning that with no enemy table the branch was unreachable — which was
true right up until targeting landed, and then it read as "Orphen ignores his
target and fires wherever he happens to be pointing". Both halves of that were
the same missing block.

Five things switch it off, all of them the original's:

| gate | what it is |
|---|---|
| `DAT_0031D7BF` == `0x8E` | the kind `-1` spell hold aims itself |
| control `+0x2C` < 3 | pool slots 0..2 are the party |
| `DAT_00354ECC` != 0 | a cinematic spell owns the screen |
| `DAT_0031DA6C[member]` bit `0x400` | confusion |
| `FUN_002494E0` != 0 | party record `+0x3C`, the charge |

The last is the interesting one: it returns 0 while the state's `0x4000` restart
bit is up and the charge timer otherwise, so **the aim pins the instant a spell
starts building**. Turn first, then charge.

The turn itself is three bands off `FUN_002166E8(facing, wanted)`, closed with
`FUN_0023A320` at `DAT_003555BC * rate`:

| band | rate | runs when |
|---|---|---|
| inside 5° | `0.0043633` | always |
| 5°..45° | `0.0026180` | idle only |
| beyond 45° | `0.0061087` | idle only |

**Only an idle character turns at all** — the two wide bands are gated on the
current action being `0x06` or `0x96`. A spell released mid-swing goes where the
swing left the facing, and that is the game, not a rounding error. A zero step
back from `FUN_0023A320` means its own half-degree dead zone swallowed the
difference, and the original then assigns the exact angle rather than leaving
the remainder — so the facing lands on the bearing, bit for bit, which is what
makes the `face=`/`bearing=` columns agree.

`DAT_00354ECC` is **not** a pre-battle lock, which is what this file and the
port's comments used to call it. `FUN_002432D8` clears it, and the only writers
that raise it are five effect-entity behaviours in the `0x2Exxxx` block —
`FUN_002E01F8`, `FUN_002E1320`, `FUN_002E23E8`, `FUN_002E34B8`, `FUN_002E65D0` —
each setting it on entry and clearing it on the way out.

The frame a wide turn starts also swaps the idle animation between 2 and 7,
class 1 one way and everything else the other, guarded by bit 0 of party record
`+0x38` so it fires once per turn rather than once per frame.

The upper body is separate, class 1 only, and never while staggered. The spine
(bone `0x20`) carries whatever yaw the legs have not caught up on, clamped to
fifty degrees; during animation `0x14` at timeline cursor `+0xA8` == 4 — the beat
the cast's arms come up — the target's elevation is split three ways, half into
bone 10's Y, minus half into bone 9's X, and the negated whole into the spine
with half of it back out through Y. All three are read back with `FUN_0020D9D8`
first and written with `FUN_0020D8C0`, so the aim layers onto the animation
rather than replacing it. Party record `+0x34`, the plain distance to the
target, is written every frame the block runs and **read by nothing in the ELF**;
it is reproduced for the hardware diff only.

Two smaller divergences fell out of reading the block's neighbourhood:

- The `0x0B` park at `:120` tests the **pending** byte (`+0x0E`), not the
  current one. The port tested `+0x0F`.
- `+0x124`, the guard arc, is cleared at `:129` — before the dispatch, so state
  117 re-arms it every frame it runs and it lapses the frame the guard drops.
  The port never cleared it, which left a guard arc up for the rest of the
  scene after one Square press.

##### Party record `+0x45` is a frame number, not a flag

It starts at `-1`. When the cast animation raises `+0xAA` bit `0x200`, state 113
copies the animation cursor `+0xA8` into it, halved; from then on the state's
last line writes it *back* into `+0xA8` every frame that `+0xAA` bit `0x400` is
up. That is what pins the character in the cast pose for as long as Circle is
held — and, because the cursor still advances between pins, what makes the hold
a two-column flicker between entries 5 and 6 of animation `0x3A` rather than a
freeze. **Nothing charges before the pose is reached**: while `+0x45` is `-1` the
state is still steering the landing spot onto the target instead.

State 111 is not this shape. It captures the target position once on entry and
gates on `+0xAA` bit `0x800`; copying its body onto 113 loses the tracking, the
pin and the charge gate all at once.

##### The release is not `FUN_0024bae0`

112 tail-calls it, 114 does not — 114 is the only release state with its own
body. It holds animation `0x3A` rather than `0x14`, clears the *character's* hit
set on entry rather than the effect's, and its level runs **0..5** rather than
0..4, because `FUN_00249270(entity, 0x780)` caps the charge at `0x2580` and five
is the summon. Its `return 1` while `+0x06` bit 0 is down is the input lock:
`FUN_00249610` writes the return into `+0x62` and raises control block `+0x38`
bit 0 for any non-zero value, which is what stops a second press mid-cast.

##### `FUN_002de650` allocates both spawns as `0x174` and retypes them

Both the damage disc and the flash are `FUN_002d6c68(0x174)` with `+0x00`
overwritten afterwards — `0x15C` and `0x178`. They get `0x174`'s descriptor and
model but the other two types' behaviour. The port resolves the *handler* from
`typeId00` every frame, so that half works for free; the *model* it did not, and
a retyped entity looked up a model for a type the scene never loaded. Hence
`OriginalEntity::modelTypeId15c`, the original's own `+0x15C` model pointer: set
at spawn, unmoved by a later write to `+0x00`, and `-1` on everything else.

`FUN_002e4c00`, type `0x178`, is not in `src/` and not defined in Ghidra.
Recovered from `SLUS_200.11`, the whole function is four instructions:

```c
void FUN_002e4c00(entity) { if (entity->flags06 & 1) FUN_00265ec0(entity); }
```

A pure one-shot: play the animation the launch set, destroy on the
animation-finished flag. Its 32-frame `+0x1B0` timer is never read.

##### What `--battle-report` shows

`ring=` is the `0x18F` marker's `+0x14C` and `hitfx=` is `DAT_0031DAD0`'s, both
in thousandths. Without them the growth is invisible in a headless run.
`face=` is entity `+0x5C` and `bearing=` the angle to the control block's
target, both in whole degrees: the face-the-target block is the only thing that
closes the gap between them, so a run where they stay apart while a target is
held is that block failing.

```
orphen_port.exe --disc-root . --scene s14_e012 --frames 700 --no-audio \
    --hold-circle 300-460 --battle-report
```

```
f=301 action=0x8c state=0x0071 charge=0    ring=1000 hitfx=1500
f=365 action=0x8c state=0x0071 charge=1152 ring=1166 hitfx=2652
f=449 action=0x8c state=0x0071 charge=3840 ring=1560 hitfx=5340
f=463 action=0x8d state=0x0072 charge=4224 ring=1612 hitfx=5724
```

`hitfx` is exactly `charge/1000 + 1.5`, and at a full `0x2D00` charge it reaches
`11.1` — the read caps at `0x2580`. Hold to full and the level is 5; with no
enemy table the target stays `-1`, so `level == 5 && target > 1` is false and the
summon branch is not taken. The box the launch lays down is `1.5 * level` in the
horizontal plane and `0.5 * level` vertically, centred on the landing spot: at
level 5, `x [-5.5, 9.5] y [-7.5, 7.5]` around `(2, 0, 0)`, 5 contacts and one
`0x15C` spark each.

The hold *must* start after the battle does. `--hold-circle` presses on its first
frame only, and `s14_e012` raises `sGpffffb052` bit 0 on frame 247; a press
before that is simply not seen.

##### Deliberately not ported: the summon

`FUN_002deef0` is the only entry, taken from `FUN_002de650` on `level == 5 &&
target > 1`. It spawns type `0x13E` at animation 5 (behaviour `FUN_002df018`, in
`src/`), arms a 32-frame timer at `+0x1AC`, and raises `uGpffffaf5c` — the
battle-interrupt flag that stops the rest of the battle while the spirit plays.
The port returns 0 from that branch with a comment rather than approximating it.

#### Targeting: where the enemies come from, and how the cursor finds them

The battle module has two 0x3C-byte record arrays with the same field layout,
and confusing them costs a lot of time:

| | where | what it holds |
|---|---|---|
| control blocks | `0x0031D7B0`, 10 records | the *party*, indexed by member |
| the actor table | inside the scene script, `DAT_00354EB4` | every battle participant the encounter data names |

`FUN_0023eba0` hands both back through one pointer and picks between them on the
id: below 10 is a control block, 10 and up is an actor record. **Ids at and
above `0x50` are the party side** -- `FUN_0023eff8` and `FUN_0023f080` both stop
counting there, and `FUN_00241a88`'s AI skips anything above `'O'`.

##### Where the actor table comes from

Not the executable. `FUN_0022a418:261` calls `FUN_0023f318(0)`, which calls
`FUN_0025ba28(0)` -- the scene script's **own section table**, at script header
word 7, entry 0. A section-14 scene with no battle leaves that entry zero.

```
blob +0x00  flags; bit 31 = already relocated
     +0x04  -> the camera/placement sub-blob (DAT_00354FA8)
     +0x10  encounter group count            (DAT_00354F9C)
     +0x14  two offsets per group: the actor array, then the master script
            (DAT_00354FA0), and the word past the table is DAT_00354FA4
```

Group 0 is taken unconditionally: its first word becomes `DAT_00354EB4` and its
second becomes `DAT_0031DBD8`, which is the master pseudo-record `DAT_0031DBA8`'s
own `+0x30` -- the battle VM's program counter. The port keeps the whole thing as
offsets into a mutable copy of the decoded script, so a value in
`--battle-report` can be looked up directly in a `--scr-dump`.

s14_e012 ships six records: `0x1E 0x1F 0x20 0x22 0x23` on the enemy side and
`0x50` on the party side.

##### The enemies are placement records, and they bind themselves

This is the part that is easy to get exactly backwards, and doing so costs a day:
**the actor table never goes looking for entities. An enemy registers itself.**

`FUN_0025eb48` -- opcode 0x51, the same walk that spawns a scene's props -- has
a tail that only fires for **group 2 in a section-14 scene**:

```c
FUN_0025bae8(group, type, r);  FUN_0023a518(entity, r);   // a body
if ((group == 2) && (cGpffffb663 != '\0')) {
  bVar2 = placement[0x0F];
  if (bVar2 - 0x1e < 0x14) entity->+0x95 = bVar2;          // the actor id
  else { entity->+0x95 = scriptVar[26]; scriptVar[26]++; } // or the next free one
}
```

So the placement record's `+0x0F` byte **is the actor id**, when it falls in
`0x1E..0x31`; otherwise the scene hands out ids from script variable 26, which
`FUN_0025b6d0`'s group-0xE write seeds to `0x32` -- one past the top of the
authored range. s14_e012's group 2 carries params 30..35 and its actor table
names `0x1E 0x1F 0x20 0x22 0x23`: the placement table and the encounter table are
two halves of one list.

The bind itself is **`FUN_0023f8b8`**, called from each enemy type's *state 0* --
`FUN_0028ae10` for type 0x8A, `FUN_0027f978` for type 0x80, thirty of them in all,
each doing the same three things: `FUN_0025bae8(0, type, r)` for the stat record,
`FUN_0023f8b8(self)` for the bind, and park the returned pointer in `+0x198`.
`FUN_0023f8b8` walks **every** encounter group -- `uGpffffb02c` groups at
`iGpffffb030`, not just the one `FUN_0023f318` selected -- takes the first record
whose id byte equals the entity's `+0x95`, wipes its per-battle fields, writes the
entity into `+0x08` and raises the entity's own `+0x96` bit 0.

Note the two different stat lookups. `FUN_0025eb48`'s is
`FUN_0025bae8(group, type)`, and for group 2 that scans the stat blob's group 2 --
which holds ids `0x62..0x79` and knows nothing about a `0x8A`, so for these
enemies it finds nothing and leaves the destination alone. The one that gives an
enemy its radius, height and **hit points** is the state-0 call, and that one asks
for **group 0** indexed by `type - 0x7C`. Without it `FUN_002476c0` will not
target anything: its predicate wants `+0x12A > 0`.

`FUN_0023fc08` runs at the top of every battle tick and *drops* a binding unless
the entity is live, has a type, and still carries the record's own id in `+0x95`.
It never creates one. `+0x0C` counts up while a *downed* enemy sits in a record --
Ghidra spells that test `psVar3[0x95] < 1` off a `short *`, which is the halfword
at `+0x12A`, the hit points, not the byte at `+0x95` beside it.

**`FUN_00240870` is the third way in, not the only one.** It is the trigger-table
walker: kinds 4 and 5 spawn `FUN_002d6c68(type)`, stamp `+0x12A = 0x78`, take the
position from the record's own `+0x14/+0x16/+0x18` (each `/10`), write the id into
the new entity's `+0x95`, and bind. The trigger table is installed by VM opcode 9.
That is how *reinforcement waves* arrive; it is not how the enemies standing in
the arena when the battle opens got there.

The port does the bind at the spawn site rather than in a per-type state 0,
because it has no enemy state machines yet. Same frame, same observable result,
and it is named as a stand-in where it happens.

##### An enemy is not the only thing in the actor table

s14_e012's sixth record is `id 0x50`, and no group-2 placement carries that id.
It belongs to a **group 4** one, and it arrives through the other placement
walk -- `FUN_0025E7C0`, opcode 0x4F -- which has a tail of its own:

```c
if (*(char *)(placement + 0xf) < '\0') {          // the tag byte, read signed
  entity->+0x95 = -placement[0x0F];               // the actor-record id
  entity->+0x08 |= 1;                             // spawned hidden
  entity->+0x04 |= 1;
  entity->+0x02 |= 0x4000;
  if (cGpffffb663 != '\0') {                      // a section-14 scene
    entity->+0x12A = 1;
    FUN_0023fb20(entity);                         // -> FUN_0023f8b8, the bind
    entity->+0x08 &= ~1;                          // and unhide
    if (FUN_002f0608(entity) != 0) continue;
  }
}
```

The tag is the *negated* byte, so s14_e012's placement #104 -- group 4, id 10,
`+0x0F` = `0xB0` -- answers to record `0x50`. In a field scene the branch stops
at the first half, which is what s01_e012's seven `group 0 id 34` markers are:
`0xFF..0xF9` gives them `+0x95` = 1..7 and they stay invisible, because they are
the party's standing marks and not props. The EE dump settles both halves --
its slots 20 and 22 read `+0x02 = 0x4080`, `+0x04 = 0x00D9`, `+0x08 = 0x0011`,
`+0x95 = 3` and `5`, against `+0x02 = 0x0080`, `+0x04 = 0x00D8`, `+0x95 = 0` on
every untagged prop beside them.

###### FUN_002F0608 retypes the entity

`FUN_0025E7C0` spawned it as `(id - 1) + 0x373` = `0x37C`, a plain streamed prop.
`FUN_002F0608` then looks *that* type up through `FUN_0025BA98` -- SCR.BIN `0xBD`
group 15, index 9 -- and reads the row's `+0x27` **kind** byte. For a kind of
1..9 it saves the old type in `+0x19E` and overwrites `+0x00` with `kind + 0x6B`:

| kind | type | name in SCR.BIN 0xBF group 2 | 0xBD row |
|---|---|---|---|
| 1 | 0x6C | Candlestick | `shokudai` |
| 2 | 0x6D | Lamp | `ramp` |
| 3 | 0x6E | Fire Element | `kaendamege` |
| 4 | 0x6F | Water Element | `zettaidamege` |
| 5 | 0x70 | Electric Element | `koudendamege` |
| 6 | 0x71 | Wind Element | `kazedamege` |
| 7 | 0x72 | **Darkness Element** | `ankokudamege` |
| 8 | 0x73 | Healing Element | `partykaifuku` |
| 9 | 0x74 | -- | the five `*kouka*` rows |

**The model does not follow the retype.** `FUN_00229C40` bound it at spawn into
`+0x15C`/`+0x160` off the placement's own type, and `FUN_002F0608` rewrites only
`+0x00` -- so the object keeps the `0x37C` prop it was placed as. The port
re-resolves a model from the live type id every frame, so it needs `+0x15C`
filled in here, the same field `FUN_002DE650`'s retype trick already uses.
Without it the element draws as `0x72`, which is a party character: a second
Orphen standing in the arena.

`0x6C..0x73` is exactly the band `FUN_002334E8` scans group 2 for, so the retype
is what makes the readout say "Darkness Element" rather than `ankokudamege`. It
is also what puts the entity in `FUN_0023C340`'s `0x6C..0x7A` fixup, so the
caption reads `HP:  1` no matter what the row's real hit points are -- 5, here.

Kind 9 is the odd one and stays hidden: those are the elemental damage *zones*,
sixteen of them in s14_e012, all tagged `0x7D` and claimed by nothing. Their
`+0x04` bit `0x10` is set, which is what stops `FUN_002F08F8` giving them the
hit reaction every other kind gets. The port drew them until this was ported.

The damage each object deals is not on the entity. `FUN_002F0608` writes it into
`DAT_0058B970 + type * 4` -- element mask, power, and the row's `+0x08` -- one
row per *type*, which `FUN_002F08F8` hands straight to the hit test. That
behaviour is not ported; the table is filled anyway.

###### The tail every other placement takes

`FUN_002F0608` returning 1 is the only way past `FUN_0025E7C0:81-119`, which
stamps the same `FUN_0025BA98` row onto the entity:

| row | entity | |
|---|---|---|
| `+0x0C` | `+0x54`, `+0x11C` | collision and hit radius |
| `+0x10` | `+0x58`, `+0x120` | body height |
| `+0x14` | `+0x133` | depth bias, over `DAT_00352B98` = 0.08 |
| `+0x06` | `+0x12A` **and** `+0x128` | hit points, live and maximum |
| `+0x07` | `+0x12C` | attack |
| `+0x08` | `+0x12E` | defence |
| `+0x09` | `+0x132`, and `+0x02 \|= 0x100` when non-zero | |
| `+0x00` bit `0x2000` | `+0x02 \|= 0x10` | |
| `+0x00` bit `0x4000` | set: `+0x06 = 0`; clear: `+0xAE = +0xA0` | |
| `+0x00` bit `0x8000` | `+0x04 &= ~8`, and `+0x4C` = a `FUN_00227798` ground sample | |
| `+0x00` bit `0x1000` | `+0x04 \|= 1` | |

The first three are a second write of numbers the entity already has:
`FUN_00229980` synthesises a streamed prop's descriptor out of `+0x0C`, `+0x10`
and `+0x14` of this same row, so the two agree by construction. The rest lives
nowhere else. `+0x02` bit `0x100` is `FUN_00252828`'s examine branch -- the
thing that makes a prop respond to the action button -- and bit `0x2000`'s
`+0x02` bit `0x10` is a class bit `FUN_00215670`'s hit test masks against, which
is what a breakable barrel needs. Bit `0x8000` is also the **only** place
`FUN_0025E7C0` samples terrain; every other prop keeps its authored z.

Neither standing scene exercises any of it: 152 rows across the twenty banks
carry stats or those flag bits, and `s01_e012` and `s01_e024` place none of them,
which is why the guard did not move. `s03_e001` does -- its `0x272` rows carry
`+0x00 = 0x6000` and `+0x09 = 1`, and come out at `+0x02 = 0x0190`.

##### The master battle script

`FUN_0023fd30` steps a second bytecode VM -- nineteen opcodes at
`PTR_LAB_0031d118` -- on the master pseudo-record, and then the same VM on every
actor record carrying `+0x38` bit `0x20`. A handler returning a negative value
continues to the next opcode in the same frame; anything `>= 0` is stored in
`+0x2E` and ends the step. Every handler moves the program counter itself,
through the global `DAT_00354EAC`.

Seven of the nineteen are bare `LAB_` blocks with no `src/FUN_*.c`; recovered
from `SLUS_200.11`:

| | | |
|---|---|---|
| 0 `LAB_00241A58` | `pc += 4`, yield 0 | 10 `LAB_00240C98` | return `yield + 1`, pc unmoved |
| 1 `LAB_00241A70` | `pc -= *(u16*)(pc+2)`, yield 0 | 11 `LAB_00240CB0` | script var `[pc+2]` vs `*(u32*)(pc+4)` |
| 8 `LAB_00240C58` | `pc += 6`, yield 0 | 14 `LAB_00241640` | return `yield`, pc unmoved |
| 9 `LAB_00240C70` | install `record->+0x34`, `pc += count*16 + 4` | | |

s14_e012's master script, at `0x1A0C`, is five instructions and it is the whole
shape of the thing:

```
1a0c  12  FUN_00242c40(500, 0)     script variable 25 = 500
1a14  18  install actor script, id 0x1E   ... then 0x1F, 0x20, 0x22, 0x23
1a3c   5  sub-op 1: wait 2 ticks
1a40   3  living enemies == 0 ? jump +0x14 (out) : fall through
1a48   3  unconditional jump -12, back to the wait
```

"Arm the target display, give every enemy its AI, then spin until they are all
dead." The port runs the master half; the per-actor scripts opcode 18 installs
are recorded but not stepped, and `--battle-report` names any opcode the master
halts on.

Variable 25 is the point of the first instruction. Opcode `0xBD` method `0x68`
(`FUN_00242cf0`) reads it every frame -- s14_e012's per-frame entry calls the
method 400 times in 400 frames -- and 500 is what makes it `FUN_0023c340`, the
target display.

##### The D-pad, and why Up feels different from Right

`FUN_002462c8:73-148`, and the bit tests are the whole of it:

```
0x3000  Up or Right   -> step forward
0xC000  Down or Left  -> step backward
0x5000  Up or Down    -> *also* half-wrap: start the search half a table away
```

so Left and Right walk the line of enemies one at a time and Up and Down jump
across it, with `DAT_00354F80` holding a 120-frame lockout so the jump cannot be
mashed. `DAT_00355600`, ORed into the same test, is **the movement stick**
mapped onto those same four bits by `FUN_0023B4E8` -- not the camera stick and
not a second pad. `FUN_0023B5D8` runs the pair it just read into `DAT_003555E8`
/ `DAT_003555E4`, the stick `FUN_00256BB8` walks on, through it and keeps the
word in `DAT_003555FE`; `DAT_00355600` is that word's newly-pressed edge. Its
gate is magnitude > 100 of 128, well clear of `FUN_0023B3F0`'s 60 deadzone, so a
nudge that moves the character does not also move the cursor.

Both are live in the port: `sdl_gl_window.cpp` puts the **arrow keys** and the
pad's D-pad into the high nibble of `rawHeldPad` where the hardware pad has them,
and runs the left stick -- or WASD standing in for one -- through
`FUN_0023b4e8_stick_direction_bits` for `rawStickDirection`.

An earlier version of this note said that nibble is also what walks the
character, "as they do on hardware". **It is not, and they do not.**
`FUN_0023B5D8:38-50` gates its digital branch on `DAT_003555E0`, which it sets to
-1 for a pad whose type nibble reads 7 -- the DualShock the game ships with --
and to 0 for anything else; the branch is `0 < DAT_003555E0` on a signed char, so
it never runs and the four direction bits never reach `DAT_003555E8`/`E4`.
Sixty frames of D-pad Right on hardware left the lead's position at `0x0058BED0`
unchanged to the byte. The D-pad is a menu control: `FUN_00224FF0` opens the
field menu on Up or Down and the map on Left, and `FUN_002462C8` cycles the
battle target with all four.

That is why WASD and the arrows are split. They shared the nibble until the field
menu was ported, at which point every step forward opened the menu.

The block is gated on the member having a target at all (`0 < FUN_002493b8`), and
the player's first one does not come from `FUN_0023fd30`'s auto-acquire loop:
that loop skips member 0 unless control block 0's `+0x38` carries bit `0x100`,
and **nothing in the executable ever sets that bit** -- the only writer is VM
opcode 4 sub-op 5, out of script data. What actually happens is that
`FUN_00243f80` parks the target at -1 and the class-1 state handler flips a
negative target positive and a zero one to 1 (`battle_character_update.cpp:361`).
Target 1 is below the `< 3` "no target" threshold everything else tests, but it
is greater than zero, so the cycler runs, `FUN_00247d80` fails to find a record
for pool slot 1's `+0x95`, and the search starts from the top of the table and
takes the first live enemy.

##### The battle camera is a pair of splines, walked back and forth

The shot a battle plays in is not a follow camera at all. `FUN_00246FC0` reads
the encounter's placement sub-blob at `+0x5C`/`+0x60` -- a count and an array of
`0xC`-byte entries -- and each entry is two point lists and a duration:

```
+0x00  the look-at point list
+0x04  the eye point list          (that order; see below)
+0x08  duration, in ticks
```

Each list is `{u32 count, then count script-encoded x/y/z triples}`, decoded in
place by `FUN_0025D618` -- the same decoder opcode `0xBD` method `0x70`'s
waypoint paths go through, so the port borrows `decodePathWaypoints` rather than
carrying a second copy. `FUN_00246FC0` relocates the two words by `iGpffffb0e8`,
the **scene script** base rather than the encounter blob's own, which is why they
are already script offsets in the port and need no fixup.

The order is worth pinning down because it is the wrong way round from the
obvious reading. `FUN_0023C340` calls
`FUN_00217E88(entry[1].points, entry[1].count, entry[0].points, entry[0].count)`
and `FUN_00217E88` hands its *first* triple to `FUN_00217D70` as the eye. So word
0 is the look-at list and word 1 the eye list.

`FUN_00217E88` then builds a cubic curve over each -- the eye and the look-at,
and no roll/zoom curve, which is what `FUN_00217F38` samples instead of
`FUN_00218158`. `s14_e012` ships three pairs, each 57600 ticks long.

The rest is the walk, at `LAB_0023D04C`:

- `DAT_00355C88` is the position along the pair and `DAT_00355C80` the direction,
  `+1` or `-1`. Each frame adds `DAT_003555BC * direction`, and running past
  either end flips the direction -- so a pair is a shot that drifts one way,
  turns round, and drifts back.
- `DAT_00355CA8` is the dwell, seeded to `(rand & 1) * 0x780 + 0x5A00`, doubled:
  `0xB400` or `0xC300` ticks, about 24 or 26 seconds. When it runs out
  `DAT_00354FB2` goes to `0xFFFF` and the next frame picks a different pair.
- The pick is `(last + 1 + rand % (count - 1)) % count` -- a walk that can never
  land on the one just played. `DAT_00355CA0` is a three-entry override that
  `FUN_0023C310` fills for a scripted shot; it reads -1 here.
- The opening position is `rand % (duration/2 + duration/3)`, so a pair never
  starts at its own far end.
- Zoom is forced back to 1.0 and roll to flat every frame. The battle camera owns
  the projection outright.

**How every other shot hands the camera back** is the mode byte at the head of
the `0x571B80` workspace. `FUN_0023DE20` leaves its mode there; the next frame
that reaches the idle path sees it non-zero and re-arms `DAT_00354FB2` instead of
continuing. So the shot after a target display is a *fresh* pair, not the one it
interrupted -- and the port needs no save-and-restore of its own.

##### The camera, for those 120 frames

`FUN_0023C340` is the whole battle camera and most of it is out of scope -- the
encounter's own spline pairs, the per-action framing at `DAT_00355C90`/`C94`/
`C98`, the reaction shots. The half the player drives is the one that runs while
`DAT_00354E96` is up, and it is two shots with the changeover at the halfway
mark:

```
DAT_00354E96 > 0x780   FUN_0023DB98 mode 6    swing onto the target
DAT_00354E96 <= 0x780  FUN_0023DE20 mode 10   hold on it and orbit
```

**The swing** (`FUN_0023DB98`) puts the eye half a unit behind the player's head
-- `player - 0.5 * (cos, sin)(atan2(player - target) + 1 degree)`, at
`playerZ + height` -- and then turns the *look point* about that pivot by one
sixteenth of the angle to the target each frame, so it converges rather than
snapping. The look point's radius is the player-to-target distance, so it lands
beyond the enemy rather than on it, and its height eases toward the target's
middle at the same one-sixteenth. On the frame the display opens `DAT_00354E94`
is still set, and the look point is first thrown eight units out along the
player's facing so the swing starts from where the character is looking. Inside
`fGpffff877c` (2.3 units) the function returns -1 without touching the camera and
`FUN_0023C340` drops `DAT_00354E96` to `0x780` -- straight to the hold.

Note what the swing does *not* interpolate: the x and y of the look point are
computed linearly at `0x0023DC84` and then overwritten by the polar result at
`0x0023DD4C`/`0x0023DD80`. Only the angle and the height actually move.

**The hold** (`FUN_0023DE20`) installs a fresh manual camera every frame,
`0x9C4 / 1000` = 2.5 units from the target at `playerZ + height * 1.5`, looking
at the target's middle, walking the bearing by `DAT_00352668` (0.000192 rad a
tick, about 21 degrees a second). The bearing opens on the target-to-player line,
flipped 178 degrees (`fGpffff8784`) if the player is already inside 2.3 units,
and which way it orbits is decided once from which side of the player's facing
the target sits on -- so the sweep always goes past the player rather than away
behind them.

Both go through the manual camera at `cGpffffb6e1` = `0x23`, which the spline
above has already installed -- `FUN_00217D40` and `FUN_00217D10` are no-ops
without it.

What is *not* ported, and is named at its site: `DAT_00355C90`/`C94`/`C98` are
the per-action framing modes (a spell cast, a guard, a stagger each get their own
shot through `FUN_0023E090` / `FUN_0023D730` / `FUN_0023D8B0`), and
`DAT_00354E84`/`E8C` the reaction shots and their screen flash. All read zero in
the port, so `FUN_0023C340` falls through to the idle path every frame.

##### The pause is one bit on everything

Every D-pad step re-arms `DAT_00354E96` to 120 frames, and **that is the pause**:
while it is non-zero `FUN_0023c340` calls `FUN_002de5b8(1)`, which sets `+0x02`
bit `0x800` on every entity except type `0x192`, type `0x6A` and anything with
`+0x02 & 0x180`. Bit `0x800` is the gate `FUN_00239ce0` and `FUN_002261e0` both
skip on, so the field freezes and only the cursors keep updating.
`FUN_002de500` lifts it. `FUN_0023c340` also returns immediately when
`DAT_00354FAC` is null -- the battle camera's spline pairs, which `FUN_00246fc0`
takes from the placement sub-blob's `+0x5C` count and `+0x60` array -- so the
port carries that pointer even though the camera swing itself is not reproduced.

There is a second targeting mode, taken when `DAT_00354EC0` is non-zero: a static
20-entry marker table at `0x003253C0`, cycled by `FUN_002481F0` and refreshed by
`FUN_00248108`, which does **not** freeze anything. Every caller of
`FUN_00247F18`, the only thing that installs it, is in the `0x0026Cxxx` block --
the scripted set pieces. A normal battle leaves it null.

##### The cursor is a type `0x192`, and it is a sprite

`FUN_002d86b0` spawns one per bound record, from `FUN_0023fd30` on the frame the
pre-battle countdown `sGpffffb054` reaches zero (`FUN_0023fb50` seeds that with
half the halfword at `DAT_00354FA4`, floored at 0x5A), and stores its pool slot in
the record's `+0x0D`. `+0x19A` is the pool slot it rides.

Its behaviour `FUN_002d73e8` places it at the target's position plus 0.75 of the
target's height, projects that through the same VU0 path the sprites use, and
**writes the screen position back into `+0x20`/`+0x24`** as pixels -- `(gsX >> 4)
+ 320` and `(gsY >> 3) + 220`, with the GS origin 0x8000 subtracted first -- the
projected depth word into `+0x28`, and `+0x08` bit `0x1000`. That bit is
`FUN_0020f510`'s screen-space branch: no perspective divide, a flat `+0x14C * 256`
scale, a GS z re-keyed off `+0x28` through `DAT_00352090/94/98`, and a flat 0x80
vertex colour. It is the only entity in either standing scene that takes it, and
the port implements it for exactly this.

The animations are the whole of the selection feedback: `10` unselected, `12` the
grow-in played on the frame this cursor becomes the target, `11` selected (and the
frame it reaches 11 it also drives the `DAT_00354EC8` light onto the target at
colour `0xB4`), `13` the shrink-in it spawns with.

Two things decide whether it draws at all, and they pull in opposite directions.
Type `0x192`'s descriptor sets `+0x02` bit `0x200`, which makes `FUN_0020c5a8`'s
*model* walk refuse it and `FUN_0020f3e0`'s *sprite* walk take it. Then `+0x08`
bit 0 stops the sprite walk too, and `FUN_002d73e8` raises it only when `+0x198`
came out of the frame non-zero -- off screen, retired, or **not the current
target while the display timer is running**. So the resting state is five
brackets, one per enemy, and the 120 frames after a D-pad step show one.

They are not squares, either. `FUN_002D73E8` raises `+0x08` bit `0x400` on every
cursor and writes the angle table at `+0x168` that `FUN_0020F510`'s
rotated-corner branch turns by: a flat `DAT_003547A4` -- pi/4 exactly -- while
the cursor is the player's target, so the selected bracket stands on a corner,
and `DAT_003555BC * DAT_003547A8` accumulated per tick, about 25 degrees a
second, for every other one. See "What is still missing" under the sprite pass
for the branch itself.

Type `0x192` doubles as a scripted marker when its `+0x19A` names its own slot;
that is the branch `FUN_002d8808` sets up and it has nothing to do with targeting.

##### The readout: a name, the hit points, and the elemental pentagon

`FUN_0023C340` calls two more things while the display is up. `FUN_002334E8`
fills one 0xE8-byte block at `0x005715B8`, once, on the frame the timer is armed;
`FUN_00233818` draws it every frame after. The block is not just a stat record --
`0x005715E0` (the name) and `0x00571660` (the caption `HP:%3d`) are the same
allocation at `+0x28` and `+0xA8`, which is why `FUN_00233818`'s only guard is
"the name is not empty". A type with no row leaves it cleared and nothing is
drawn.

`FUN_002334E8` splits four ways on the entity's type id -- its `+0x1CE` when the
type is `0x38`:

| type id | table | index |
|---|---|---|
| `< 0x7C` | `uGpffffadf8` group 2, **scanned** for a row whose `+0x02` matches | -- |
| `0x7C..0xFA` | `uGpffffadf8` group 0 | `type - 0x7C` |
| `0x272..0x371` | `uGpffffadf4` group `DAT_00355208` | `type - 0x272` |
| `0x373..0x472` | `uGpffffadf4` group 15 | `type - 0x373` |
| `0x474..0x573` | `uGpffffadf4` group 16 | `type - 0x474` |

`uGpffffadf8` is SCR.BIN `0xBF`, already loaded for the party records;
`uGpffffadf4` is SCR.BIN `0xBD` beside it, and the port now loads that too. The
scene's enemies land in the second row of the table, not the first: s14_e012's
Maneater is type `0x8A`, so group 0 index 14, whose name is "Maneater" and whose
`+0x18` block is `30 80 50 80 100 20 50 100 100 30 50 ...`.

It also counts **duplicates**. The walk is pool slots 10..255 against
`DAT_005A96B0`, collecting every live entity of the same type; if there is more
than one, the caption becomes `%s-%d` with this one's position in that list. Two
Maneaters and the second reads "Maneater-2".

Two fixups sit either side of the call and neither is cosmetic. A `0x192` -- the
cursor itself -- has no row, so it is stamped `0x86`, or `0x87` when its `+0x94`
says so, for the length of the lookup. And a party character (types `0x6C..0x7A`)
has its `+0x12A` forced to **1** while the caption is formatted, so an ally's
readout always says `HP:  1` rather than its real total.

**The pentagon** (`FUN_0022EC30`) is five arms at 72 degrees, in the order
`DAT_0031C240` gives -- element 1 lightning at 38 degrees up-left, then 4 fire,
2 wind, 5 dark, 10 ice, counter-clockwise. Each arm is three stacked bars from
`DAT_0031C260`, 30, 56 and 78 wide by 16 tall, pivoting about `DAT_0031C290`'s
6, 22 and 38 units out, so the three read as concentric pentagon rings. How many
are lit is the effectiveness byte banded at `0x22` and `0x4B`: 1, 2 or 3 pips,
and 0 lights nothing. A dark pip is the same quad at alpha `0x40`.

That `0x40` is **not** half brightness, and reading it as half is what made the
port's rings run together. `FUN_00207DE8:130-141` folds every packet's RGBAQ on
the way to the GS -- an untextured packet keeps its rgb and halves the alpha, a
textured one halves all four channels:

```c
uVar8 = (uVar8 & 0xfefefefe) >> 1;   // textured
```

A pip is textured, so `FUN_0022EB00`'s `0xFF'FFFFFF` and `0x40'FFFFFF` reach the
GS as `0x7F7F7F7F` and `0x207F7F7F`. With `0x80` as 1.0 that is a lit pip at 0.99
and a dark one at **0.25**. The sprite pass and the weapon trail already applied
this fold; the HUD quads did not, and drew every dark pip at twice its alpha.

**The colour is not in the vertex.** Every pip samples the same 87x16 texel patch
of slot `0x2A` and `DAT_0031C250` picks a CLUT bank -- 14 lightning, 13 fire,
12 wind, 11 dark, 15 ice. That is worth spelling out, because the port had no
concept of it before and the sheet looks like noise without it:

> **A texture slot at or above 0x18 is a 4-bit page on the GS.**
> `FUN_002103D0:66-80` walks the BMPA index array two bytes at a time and writes
> `(a & 0xF) | ((b & 0xF) << 4)`, so the texel is the low nibble of the index and
> the high nibble is thrown away. The 16-entry CLUT then comes from CSA, and one
> 256-entry palette holds sixteen independent ramps. `FUN_00207DE8` carries the
> bank in the high byte of the packet's texture halfword -- `(bank << 8) | slot`
> -- and writes `bank + 1` into the packet, zero meaning "8-bit page". The
> s01_e012 GS dump shows the consumer plainly: 783 ordinary PSMT8 draws at CSA 0,
> and then TEX2_1 writes with PSM PSMT4 at CSA 9, 10, 12 and 13. CSA 9 is the
> character shadow -- `FUN_0020DDC8` writes `0x092A` -- and slot `0x2A`'s bank 9
> is a grey ramp with rising alpha, which is exactly what a shadow blob needs.
>
> Read the sheet 8-bit and none of this exists: bank 9's palette entries decode
> to a green blob and banks 11..15 look empty, because the arithmetic that finds
> them is the 4-bit read. The port keeps the index array and the palette on
> `BmpaTexture` now and builds one GL texture per (slot, bank) the frame asks
> for.

`--battle-report` prints the readout with the arms broken out, so the banding can
be checked against the table without a screenshot:

```
target readout: DAT_00354e90=12 timer=3520  "Maneater-2" "HP: 75"
  arm 0 element 1 lightning effectiveness=80 pips=3 clut bank 14
  arm 1 element 4 fire effectiveness=100 pips=3 clut bank 13
  arm 2 element 2 wind effectiveness=50 pips=2 clut bank 12
  arm 3 element 5 dark effectiveness=20 pips=1 clut bank 11
  arm 4 element 10 ice effectiveness=50 pips=2 clut bank 15
  15 pip quads, 16 caption glyphs
```

The chime that goes with a target step is not in either of those functions. It
belongs to the cursor: `FUN_002D73E8:248` keys cue `0xC9` on the frame a cursor
goes from animation 10 to 12, which is the frame it becomes the selected target.
It calls `FUN_002057C8` directly rather than `FUN_00267D38`, so it is never
panned by where the enemy stands -- a flat cue at `0x80` on both channels, one
step above the `0x7F` `FUN_00267D38` would have used. Because the trigger is the
*transition*, holding a direction on the same target is silent, and the first
chime of a fight is the one the initial selection fires when the cursors appear.

What is **not** here: the player's own panel. `FUN_00230E50` draws a second
pentagon with `FUN_0022EC30`'s other banding -- `param_4` 0, effectiveness over
ten capped at three -- from `DAT_00570DB0` at `(0x220, 0xD0)`, plus a bar through
`FUN_00230DB0` and `FUN_0022EF10`. It hangs off `FUN_0022E910`, the battle UI
state machine at `DAT_00354DA0`, which is a layer the port has none of.

#### An enemy turns to face you without any AI at all

The enemies in `s14_e012` all stood facing the same way -- 1.571 radians, which
is the placement byte's own `0 * 45 + 90`. The placement angle was never the
bug; `FUN_0025E7C0` reads it and the port applies it. What was missing is that
an enemy immediately turns off it.

Every enemy type is one wrapper plus a state table, and the two this scene
fields are `FUN_0027F288` (type 0x80, `PTR_FUN_00325970`, nine states) and
`FUN_0028A958` (type 0x8A, `PTR_FUN_00325B40`, twenty). The wrapper is the same
four steps in both: clear a couple of `+0x08` bits, run the *action check*, run
the freeze gate and the `+0xBE` hit reaction, then dispatch `+0x60`.

The action check -- `FUN_0027F4B0` / `FUN_0028AB28` -- is where the facing comes
from:

```c
if (record[2] != 0) { record[0x2c] |= 1; dispatch(record[2]); record[2] = 0; }
else if ((record[0x2c] & 1) == 0) { FUN_0027f5c0(entity); }
```

`record` here is entity `+0x198`, and **that is not the actor record: it is the
record plus 0x0C**, because that is what `FUN_0023F8B8` returns. So `+2` is the
record's `+0x0E` pending action, `+3` its `+0x0F`, `+0x20` its `+0x2C` target
and `+0x2C` its `+0x38` flag word.

With no pending action and the busy bit clear, `FUN_0027F5C0` runs. It sets
`+0x19C` to the bearing to the target, rolls a 100..199-tick hold into `+0x62`,
drops into state 1 and stamps the record's current action 6. State 1
(`FUN_0027FA88`) then walks `+0x5C` toward `+0x19C` at ten degrees a frame
through `FUN_0023A320`.

The target is the thing to notice. `FUN_0023A958` reads the record's `+0x2C`
and, **when it is negative, returns `&DAT_0058BEB0` -- pool slot 0**. Type
0x8A's variant, `FUN_0023A480`, does not even ask: it reads `DAT_0058BED0` and
`DAT_0058BED4` outright. So "no target" is not "no angle", it is "the player",
and an enemy standing in an arena with no script running still turns to face
Orphen. All five in `s14_e012` now land on the bearing to the lead to three
decimal places.

State 0 (`FUN_0027F978` / `FUN_0028AE10`) is the other half and is identical
between the two types: scale to 1.0, `FUN_0025BAE8(**0**, type)` -- group 0 of
SCR.BIN 0xBF at `type - 0x7C`, the enemy table -- inlined through
`FUN_0023A518`, `+0x19C` seeded with the placement facing, `+0x96` bit 0, an
animation, then `FUN_0023F8B8`. Its three `FUN_00216078` calls are deliberately
absent from the port: they fill globals at `0x005739B0` / `0x0058B140` that
nothing reads back, and their one lasting effect -- `DAT_00354C64` -- the bind
on the next line re-establishes for itself.

##### Type 0x8A re-aims every frame, and that is the original

`FUN_0027FA88` **sets** the busy bit and only clears it when the turn has
finished and the hold has run out, so a type 0x80 aims once and then holds.
`FUN_0028AF28` **clears** it unconditionally (`and v0,v0,0xfffffffe` at
`0x0028AF48`, checked against the disassembly because it reads like a typo), so
a type 0x8A runs the idle default again on the very next frame -- re-aiming, and
re-rolling its idle animation through `FUN_00225BF0`, which resets the timeline
cursor. Its pose therefore sits on frame 0 and its animation index flickers
between 0, 2 and 3.

That is not a port bug and it is what hardware does too, but it is only visible
while the record is idle -- and once the AI is running it rarely is.

#### The enemy AI is a second bytecode VM, and it only needs five opcodes

`FUN_0023FD30` runs a VM that has nothing to do with the scene script's 271
opcodes. It has nineteen, at `PTR_LAB_0031D118`, and three loops that step them:
the master battle script on the pseudo-record at `DAT_0031DBA8`, every *actor*
record's own `+0x30` script, and every party control block's. The handler
signature is `handler(block, yield) -> u16`, a negative return means "not
finished, run the next opcode now", and the program counter is a single global
at `DAT_00354EAC` that each handler moves itself -- the loops only save and
restore it around the call.

Seven of the nineteen have no `src/FUN_*.c` at all; they are bare `LAB_` blocks
Ghidra never turned into functions, and they had to come out of `SLUS_200.11`
with a disassembler. They are also the cheapest ones: opcode 0 is `pc += 4`,
opcode 1 is a backward jump, opcode 8 is `pc += 6`, opcode 10 is
"return yield + 1", opcode 14 parks forever, opcode 9 installs a trigger table
and opcode 11 waits on a script variable. Recovering them is what made
`-0x50C4($gp)` -- `0x00354EAC`, the PC -- and `-0x4F10($gp)` -- `DAT_00355060`,
the script work memory -- fall out.

The surprise is how little of it an enemy uses. `s14_e012`'s five AI scripts are
five overlapping tails of one twenty-instruction body and between them they
touch **five** opcodes:

```
1a94  16  delay   sub 0: a random 0..0x3B ticks
1a98  13  order   sub 0x80: request action 6 -- go idle, face the target
1a9c  17  gate    sub 1: hold while ANY record is mid-attack
1aa0  13  order   sub 0x80: request action 2 -- close and strike
1aa4  17  gate    sub 0: hold until this record's +0x0F is back to 6
1aa8  16  delay   sub 0: another random beat
1aac  16  delay   sub 2: 0x1E ticks per enemy still in the fight
...   the same again with action 4, then opcode 3 jumps back to 1a94
```

That is the whole of it. Wait a random beat, take a turn only when nobody else
is mid-attack, strike, wait for your own strike to finish, wait a beat scaled by
how crowded the fight is, repeat. **Opcode 17 sub-op 1 is why enemies take turns
instead of swarming** -- it scans the entire actor table and holds while any
record's current *or* pending action is in 2..5.

##### FUN_00244248 is the handshake, and it is biased by 0x0C twice

An order does not simply overwrite the pending byte. `FUN_00244248` picks its
block by the entity's `+0x95`: below 11 it is a party slot and the block is
`DAT_0031D780 + slot * 0x3C`, from 11 up it is an actor id and the block is
`DAT_00354EB4 + FUN_00247d80(id) * 0x3C + 0x0C`. Both are the record **plus
0x0C** -- the same bias `FUN_0023F8B8` hands an enemy in its `+0x198` -- which
is why the decompilation spells the two action bytes `[2]` and `[3]`. Read them
unbiased and the whole handshake looks like it is writing into the wrong fields.

The rule itself is four lines: while `+0x38` bit 0 is set and the pending byte
is neither 0x0A nor 6, an order is refused with -1 unless the *current* action
is already 6. Opcode 13's sub-op 0x80 parks the script on itself when it gets
that -1 and tries again next frame -- unless the actor has died, which is the
one thing that stops it spinning forever.

##### An idle enemy aims at slot 0; an enemy under AI is *assigned* a target

The second loop does one more thing per record, and it runs whether or not the
record has a script: it validates `+0x2C` and, when it has gone negative, walks
the three-byte preference ring at `+0x1D`. Each rotation moves `+0x1E` into
`+0x1D`, `+0x1F` into `+0x1E`, and the *negated* byte that fell off the front
into `+0x1F`, so a member that has just been tried goes to the back marked used.
Three tries, then a random member. The bytes it reads are `psVar3[0x95]` off a
**short** pointer, which is `+0x12A` -- hit points -- not `+0x95`.

With one party member the ring never matches and the random fallback picks
member 0 every time, so the port's enemies end up aimed at pool slot 0 anyway.
The difference is that they are now aimed there *by the retargeter* rather than
by `FUN_0023A958`'s no-target fallback, and a second party member would split
them.

##### What the actions actually do

`FUN_0027F5C8` and `FUN_0028AC40` are the two action tables, and they are not
symmetrical, because the two enemies are not the same shape. Type 0x80 flies:
action 2 builds a quadratic Bezier -- start where it stands, end two units
*past* the target along its own facing, apex 0.3 above the target -- and
`FUN_0023A990` walks it over exactly the travel time `FUN_0023A6D0` costed when
the order landed. Action 4 is the same machinery aimed two units *short* of the
target with a flat 1.5 apex, so it slams down in front rather than through. Both
end in state 6, which teleports the enemy back to its record's spawn position
(`+0x14`/`+0x16`/`+0x18`, world times ten) two units up and lets it sink.

Type 0x8A is a Maneater and is rooted to the floor. It never moves: action 2
turns, bites where it stands and spawns the hit volume on timeline cursor 10;
action 4 winds up for 100 ticks and spits. Action 4 publishes current action
**2**, not 4 -- so the gate that waits for a strike to finish cannot tell the
two attacks apart, which is what lets one script drive both.

Each type releases the busy bit in its own place, and that is what closes the
loop: 0x80 when state 6 has landed it back home, 0x8A when the attack animation
comes round with nothing left in flight. Only then does the idle default stamp
the current action back to 6 and let the script's gate through.

**The damage an enemy deals is not ported.** `FUN_002EBDE0`, `FUN_002EBAD8`,
`FUN_002EC920`, `FUN_002ECC68` and `FUN_00280698` are the five calls that spawn
the hit volume. Every attack plays through to its damage frame and counts itself
-- `--actor-report` prints "enemy attacks that reached their damage frame" --
and lands on nobody.

##### Damage the other way is what the reel and the death states are for

An enemy hit by a spell takes the damage as `+0xBE`, and each wrapper drains it
against `+0x12A` before it dispatches: survived goes to a stagger state, killed
to a death state. The two types number them differently -- 0x80 uses 8 and 7,
0x8A uses 6 and 5 -- and 0x80 makes far more of it, because it has a spawn point
to be knocked back to. `FUN_00280628` reels, `FUN_00280728` publishes current
action **8** and lays a Bezier home, `FUN_00280288` flies it and releases the
busy bit as it lands, and only then does the idle default stamp the action back
to 6. The Maneater's `FUN_0028B698` just holds the bit for the length of the
clip and drops back to state 1.

The two deaths both end at `+0x04` bit 0, the fade-and-free bit, on a `0x801`
write whose 0x800 half nothing in the executable reads back. `FUN_0028B568`
notably does *not* release the busy bit for a plain Maneater -- only one grown
by `FUN_0028B740`'s spit takes that branch -- but the record unbinds when the
entity is freed, so the fight moves on regardless: a 5-enemy `s14_e012` run
whittled down to `live enemies: 1 of 1 bound` with the survivor still spitting.

Without the stagger states an enemy hit mid-attack sat in an unimplemented state
forever, holding both the busy bit and its own script's gate.

#### The health bar is an entity, and there are exactly two of them

Hitting something raises a five-segment gauge, and it is not a HUD widget drawn
by the UI pass -- it is a **type 0x68 entity in pool slot 3**, with a twin in
slot 2. `FUN_0022A418:378-383` builds both on every scene load, right after the
particle reset, in place at `0x0058C260` and `0x0058C438`. Those two addresses
are `DAT_0058BEB0 + 2 * 0x1D8` and `+ 3 * 0x1D8`, so they are pool slots, not
loose structs -- which is also the cleanest confirmation of the 0x1D8 entity
stride.

`FUN_00216140` raises one for any victim whose descriptor `+0x02` carries `0x4B`
and whose `+0x96` does not carry `0x20`, and the bank comes from the same word:

| `+0x02 & 0x48` | slot | screen row | animation base |
|---|---|---|---|
| clear | 2 | slides **up** from 456 to 416 | 0 |
| set | 3 | slides **down** from -8 to 32 | 0x14 |

Every enemy descriptor in the game is `0x0008`, so an enemy always takes the
upper bar. The party types 3..7 are `0x4004`, which matches neither the `0x4B`
gate nor the `0x48` bank test -- a party member's hit raises nothing, because
the player's readout is `FUN_00230E50`'s panel instead. The lower bar belongs to
the `0x01`/`0x02` descriptor band and to the two class 1 states, `FUN_0024BD30`
and `FUN_0024CBA0`, that arm it by hand; neither is ported, so nothing raises
the lower bar yet.

**The five pips are a band, taken twice.** `FUN_002D5630` is handed the victim's
hit points *before* the wrapper drains them, its maximum, and the damage still
sitting in `+0xBE`, and rounds both the before and the after up into fifths:

```c
+0x1A4 = (clamp(hp, 0, max) * 5 + max - 1) / max                 // where it starts
+0x1A8 = (clamp(hp - min(dmg, max), 0, max) * 5 + max - 1) / max // where it stops
```

`+0x198` then walks the whole animation: fifteen frames of sliding on, one frame
that drops the first pip, one more pip per animation loop until `+0x1A4` reaches
`+0x1A8`, forty-eight frames of holding, fifteen of sliding back off, and
`+0x08` bit 0 to stop drawing. The pip count is spelled as the *animation*, not
as geometry -- three banks of five, `base - segments + 21` while arming,
`+ 6` draining and `+ 11` settled -- and all three are written straight to
`+0xA0` rather than through `FUN_00225BC8`, so a bar mid-drain does not restart
its clip.

##### It also found three things wrong in the sprite pass

The bar is the first entity either scene fields that is screen-space, asks to be
drawn in front, and lives on a 4-bit sheet, and it exposed all three.

`FUN_0020F510:0x0020F55C` reads `+0x08` **once** and latches bit `0x1000` and
bit `0x40` into its workspace in the same three instructions, before it branches
on the first of them. The port tested `0x40` only on the world-space side, so
the bar -- which carries both -- never reached the "draw over everything"
bucket.

And its `+0x28` is `DAT_00354704`, `65534.0`. That is not an arbitrary large
number: `19706.0859 / 0.3 - 152.9538` is `65534` exactly, so it is the depth
word the sprite key produces at `DAT_0035209C`, the front of the sprite range.
The original hands the GS that z and never clips; the port re-projects, and its
geometry near plane is `0.4` -- so the whole gap between the sprite floor and
the geometry near plane is thrown away by GL. The bar sat in it with five
well-formed quads in the display list and no pixels. A screen-space quad is now
parked just inside the near plane instead, which costs nothing: `perX` and
`perY` divide by that depth and the projection multiplies it straight back, so
any positive value reproduces the same corners. Note *just* inside -- a vertex
exactly on the near plane rounds to the wrong side of it in float and is
dropped.

The third is the colour. **A sprite record's `+0x09` is a CLUT bank**, and the
sprite pass was ignoring it. `FUN_0020F510:0x0020FB98-0x0020FBC0` tests the
texture slot against `0x18` and, at or above it, writes `(record[+0x09] >> 4) + 1`
into the packet's bank field -- the same `bank + 1` encoding `FUN_0022EB00` uses
for the pentagon, zero meaning "8-bit page". Slot 0x2B holds the gauge sprite in
sixteen palettes, and the bank is the whole difference between the player's bar
and the enemy's; reading the sheet 8-bit gave every enemy a blue bar. Two
records in one strip can name different banks, which is how one sheet serves
both.

That was not the bar's problem alone. The port had the bank on the HUD path
since the pentagon but never on the sprite path, so *every* sprite on a slot at
or above 0x18 was drawn from the wrong palette. The visible second case is Hand
of Pyro's impact: its four-pointed star reads as a blue-white flare rather than
the washed-out smear the 8-bit page gave. Neither field scene has such a sprite
on screen at frame 900, so both guard captures are unchanged.

#### Four of the six enemy attacks were firing an empty gun

The killer bee has two attacks and the Maneater has two, and only one of the
four did anything: the bee's dive, because its damage is its own body sweeping
through `FUN_00215AC8`, and that was already ported. Every other attack ends in
a call that leaves the enemy — a projectile, a seed, a cloud of spores, a direct
charge to the victim — and none of those five calls existed in the port. The
animations played, the cues fired, and nothing came out.

| where | call | what it should make |
|---|---|---|
| 0x80 state 2, the dive | `FUN_00280698` every frame | the body box, through `FUN_00215AC8` |
| 0x80 state 2, hit frame | `FUN_002EBDE0(e, 6)` | six type `0x10F` dust puffs in an arc |
| 0x80 state 3, hit frame | `FUN_002EBAD8` | **the shot**, a type `0x10E` off bone 11 |
| 0x8A state 2, cursor 10 | `FUN_002EC920` | **the seed**, a type `0x112` lobbed at the target |
| 0x8A state 4, throw frame | `FUN_002ECC68` | eight type `0x113` spore orbs |
| 0x8A state 4, clip end | `FUN_00216128` | the spit's damage, charged directly |

The state 3 one is worth naming properly: **the flyer's second attack is not a
second ram.** Its arc deliberately stops two units *short* of the target, and
the last frame of animation 7 launches a projectile from bone 11 aimed down at
the target's midriff — speed 60 units per 32000 ticks, and a drop rate computed
so it arrives rather than a gravity constant. Read as a ram it looks like a
lunge that misses on purpose; it is a lunge that shoots.

##### Type 0x112 has no handler, and that is deliberate

The seed's slot in `PTR_LAB_0031CAB0` is `FUN_00239E78`, the no-op. It does not
tick itself — the Maneater that spat it calls `FUN_002EC750` on it from its own
wrapper every frame and reads the return: `1` means it has landed and the clip
has come round, which is the cue to grow a clone, and `-1` means it has finished
sinking and freed itself. That is also why the Maneater has to tear the link
down by hand when the battle ends, and why `FUN_0028B568`'s tidy-up has two
shapes.

`FUN_0028B740` is the growing. It allocates a second entity of the parent's own
type at the seed's position, gives it one hit point, links the two together and
drops it into **state 3** — a state neither the AI script nor the action table
can ask for. `FUN_0028B0E8` is that state and it is the clone's whole life: walk
in turning toward the target, lunge, and on the lunge's last frame decide. Within
one unit, within 0.3 in height, the target on the ground and not already held by
something, and it grabs — raising bit 2 of the *victim's* `+0x96`, which is the
only place in either enemy that writes another entity's flags. Then 0x0C80 ticks
of chewing, one direct charge of attack record 0, and it dies. Miss, and it dies
immediately.

##### `+0x1AC` was the wrong way round

`FUN_0028AE10`, type 0x8A's state 0, ends with `+0x1AC = 1`. The port had read
`FUN_0028B568`'s branch as "a placed Maneater has 0 there", which made a corpse
take the clone's tidy-up path instead of its own. It matters now that clones
exist: 1 means "I own the seed at `+0x1A8`, and my record is not released until
both it and the clone are gone"; 2, which `FUN_0028B740` stamps, means "I am a
clone, and all I have to do is clear my parent's `+0x1A4`".

##### The player froze in state 102 the moment any of it landed

With the attacks firing, `FUN_00216140` started charging the player's `+0xBE`
and `FUN_0024A360` did what it always did — action 0x83, state 102 — and state
102's handler was a null entry in the class-1 table. Nothing else drains a party
member's `+0xBE`, and `FUN_00216140` refuses a victim that still has damage
pending, so the first hit put Orphen in a stagger he never came out of and made
him immune to everything after it. Exactly the shape of the killer-bee freeze,
on the other side of the fight.

`FUN_0024A540` is that state, and its restart frame is where a party member's
damage actually happens: drain `+0xBE` out of `+0x12A`; pick the flinch (0x1C)
or the knock-down (0x20) from the reaction byte and whether the character is on
the ground; raise `+0x38` bit 0x800 on the control block, and 0x400 as well when
the hit crossed a fifth of the maximum; at zero hit points lie down and teleport
to the position the control block last recorded; otherwise roll for a status.
The frames after it are the getting up — 0x20 down, 0x22 the push-up after
fifteen frames on the floor, 0x23 the stand, then state 108 with action 0x87 to
walk back to the recorded spot.

##### The two enemies inflict statuses, and something has to take them off again

The attack records, read out of SCR.BIN 0xBE at run time, are not all plain
physical hits:

```
type 0x80  record 0 flags=0001 +100%    the dive
           record 2 flags=1000 +10%     the shot
type 0x8A  record 0 flags=0001 +70%     the clone's bite
           record 2 flags=0200 +10%     the spit
```

`FUN_0024A190` reads that halfword back off `+0xC2` and turns the seven status
bits in `0x1632` into a status id — `0x0200` is 9, the poison, and `0x1000` is
12, the confusion the command input already reads to scramble which spell slot a
press picks. A member already carrying any status is immune to a second, and the
reaction byte is the odds in tenths that it is shrugged off.

`FUN_002D8B38` raises it: every party member owns a type `0x118` aura built with
its other effects and parked hidden, and this un-hides it over the character's
head, tells it which icon to draw, and sets the bit in `DAT_0031DA6C`.
**`FUN_002D8CE0` is the only thing that clears that bit again**, so porting the
arming without the aura's behaviour would have left the skull sitting over
Orphen for the rest of the fight. Its six status bodies are one body with a
different light colour: track the head, run `+0x62` down at two ticks a frame,
then shrink a tenth a frame until the icon is under four tenths of its size and
hide. Statuses 4 and 9 add a hit point per animation loop off the victim and arm
the **lower** health bar with it — the first live caller the port has for the
player's own gauge — and they stop at five points, so a status never kills.

##### Verified

`s14_e012`, 8000 frames, no input. All four effect types spawn (`0x10E` once
per lunge, `0x10F` in rings of six, `0x112` per bite, `0x113` in volleys of
eight), type 0x8A reaches states 3 and 5 for the first time, and the report has
no unimplemented state handlers on either side. Orphen goes from 50 hit points
to 7, staggers five times and recovers from each into state 108 rather than
sitting in 102 — before this he entered 102 at frame 760 and was still in it
7241 frames later. The status cycle arms once and clears once. `s01_e024` and
`s01_e012` are byte-identical over 3000 frames with `--actor-report` and
`--scr-report`, and both frame-900 captures are unchanged.

#### s14_e001 is one actor away from a battle

`s14_e001` is the giant crab: the encounter `s01_e012` hands off to. It opens
with a long animatic -- the crab wades in, throws Dortin and Volcan into the
water, takes two swipes at Orphen while he dodges, and the camera spins around
the pair before the fight starts. The port loaded the scene and then sat there.

Six things were in the way, and five of them were the script.

##### The structural opcode table was read one slot early

`analyzed/structural_ops/structural_ops_dispatch_table.md` lists 0x04 as
"(inline handler)" with no address and then reads the rest of
`PTR_LAB_0031e1f8` one row early. It is a flat eleven-entry table indexed by the
raw opcode (`FUN_0025bc68:31`); 0x04 has a real entry that is simply never
reached, because block end is handled before the lookup. So the true shape is

```
0x00 0x04 0x05  no-op
0x01            conditional jump
0x02            switch
0x03 0x07 0x09  relative jump
0x06 0x08       skip the rel32
0x0A            FUN_002681c0("Debug Code:%d", iGpffffb0cc), then ++it
```

and the port had 0x06/0x08 and 0x07/0x09 exactly the wrong way round. A 0x06
read as a no-op leaves its four-byte operand standing in the statement stream,
and the next byte decodes as nonsense -- which is how the scene died on
"unimplemented opcode 0xf" at 0x15c8, four bytes into the else arm of the
if/else that ends its start entry. The doc is corrected too.

##### Five opcodes

| opcode | original | what it is |
|---|---|---|
| `0x117` | `FUN_002649a8` -> `FUN_002257c0` | writes the +0x07 running flag of one of the **map's** UV animation tracks (`uGpffffb788` = `DAT_003556F8`). The crab scene stops the water with it |
| `0x47` | `FUN_0025e0e8` -> `FUN_00217d40` | move the script camera's eye, one xyz triplet over 100000.0 |
| `0x48` | `FUN_0025e170` -> `FUN_00217d10` | the same for its look-at. The whole animatic is driven by these two |
| `0x7F` / `0x80` | `FUN_00260880` | the **read** side of 0x7D/0x7E: a collision group's rotation (+0x3C) or translation (+0x48) channel, scaled back up by 100000.0 |
| `0x8F` | `LAB_002610f8` | two instructions, `jr ra; lw v0, -0x49b4(gp)` -- `DAT_003555BC`, the frame tick count |

##### And method 0x6F, which is how a cutscene drives a boss

`0xBD`'s method table has three battle-entry methods the port already had (1, 2,
3) and `0x6F`, which is `FUN_00244248` -- **the same action request the battle
VM's `aiact` opcode makes**, aimed at the entity the selector picked instead of
at the party. That is how the animatic tells the crab what to do. It needs the
selected entity, so `ScriptEnvironment::FUN_00242a18_battle_method` now carries
one and the port shares a single `battleVmEnvironment()` between the per-frame
battle step and the script.

##### What was left was the crab itself

(Ported in the next commit; see the section after this one.) With those in, the
scene script runs clean -- zero unimplemented opcodes over
12000 frames, on every entry -- and the animatic gets as far as its own logic
allows. It is a duet, and only one voice is singing:

```
slot script at 0x1fce   switch on work[0], nineteen beats ten apart
  beat   0   0x6D, take the player's controls
  beat  20   0xBD(work[2], 0x6F, 11)   crab: action 11
  beat  80   0x47 x100                 the camera walk
  beat 100   0xBD(work[2], 0x6F, 12)   crab: action 12
  beat 110   wait for work[1]          <-- parked here
  beat 140   0xBD(work[2], 0x6F, 13)   crab: action 13
  beat 150   wait for work[1] again, then kill UV tracks 4 and 7
  beat 170   wait for placements 3..8 to be gone
  beat 180   work[0] = 1000, and the per-frame entry's ladder takes over:
             0xBD method 3 at 0x16f1, method 2 at 0x1701 -- build, then start
```

`work[1]` is never written by the script. It is written by
**`FUN_0027cef8`**, which is the crab's, and reached from `FUN_0027c3e8`
(action 13's handler: swipe three times, then animation 1 and `work[1] = 1`) and
from state 13. `work[0]` itself is written by `FUN_0027b380` (state 11) as well,
and `FUN_00279298` gates its own body on `work[0] < 3000`. The animatic cannot
advance without the actor.

Type `0x7F` is `FUN_00279298` with a sixteen-entry state table at
`PTR_FUN_00325930` -- the table runs on past its end into type 0x80's states, so
only 0..15 are the crab's. The action check is `FUN_00279600`, and
`FUN_002796c8` maps the orders: 12 -> state 13, 13 -> `FUN_0027c3e8`, 14 ->
`FUN_00277d30` + `FUN_0027c7b8`, and 11 just publishes itself as the current
action. Reachable from those roots are 50 functions in the `0x27xxxx` band,
about 7900 lines of decompiled C; everything else they call is engine code the
port already has.

##### Verified

Forcing the two crab-dependent waits -- `work[1]` non-zero and the placement
scan empty -- and changing nothing else, `s14_e001` builds the party and starts
the battle on frame 661, binds Orphen's shipped loadout (Hand of Pyro,
Bite of Lightning, Sword of the Fallen Devil, shield) and puts him in state
0x78. So the battle half is already there; the animatic in front of it is what
is missing. `s14_e012` still builds on frame 2, starts on 247 and binds 5 of 5
enemies. `s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report` and `--scr-report`.

#### The crab, and the two things that were stopping it

With the script opcodes in, `s14_e001` still parked at its own beat 110 waiting
on script work word 1, which only the crab writes. Porting the crab found two
bugs that had nothing to do with the crab.

##### The actor count was established too late

`FUN_0023fb50` ends on `FUN_0023fc08`, and `FUN_0023f318` calls `FUN_0023fb50`
as it loads the scene's encounter data. So `DAT_00354EBA`, the actor count, is
set the moment the scene loads -- long before any battle starts. The port only
recounted inside the running battle, so through the whole animatic the count sat
at zero, `FUN_00247D80` answered -1 for every id, and `FUN_00244248` dropped
every order the cutscene tried to give the crab.

The pool-reading half of `FUN_0023fc08` is inert at load -- every record has just
been given "no entity" -- so the fix is the counting half, called from
`FUN_0023fb50` where the original calls it.

##### Orphen's dodge is what lets the crab swing twice

`FUN_0027c458`, the swipe, latches `DAT_0035526B` the moment the claw connects,
and its own "ask for another swipe" arm is gated on that byte being clear.
Nothing in the crab clears it. `FUN_0027d230` does -- and `FUN_0027d230` is not
the crab at all, it is **Orphen**: the wrapper runs it every frame the crab's
mode byte is non-zero, and it is the tumble the player does when a claw lands.
The claw sets 1, the tumble carries the player along a Bezier to one of three
scripted landing spots over 0x640 ticks, and on arrival it sets 9, which hands
the control block back and clears the byte to 0. Only then can the crab swing
again.

So the "Orphen jumps around dodging" half of the animatic is not an animation
the script plays. It is a state machine on one byte, shared between the boss and
the player, and the boss cannot finish its own three swipes without it.

##### What is ported

Type 0x7F is `FUN_00279298` and a sixteen-entry table at `PTR_FUN_00325930`.
The table has no terminator and the next type's starts right after it, so
reading past 15 gets type 0x80's states -- the port stops at 16.

```
 0  init: stats, the three attack records into DAT_00573788, the actor bind,
    and the pair it throws (FUN_00248f18 on tags 12 and 49)
 1  idle                          2  idle hold, then pick the next move
 3  charge and slam               5  close and stamp three times
 8  walk back to (0, -5)         12  the hit reaction and the phase change
13  **the throw**                14  **the swipe**
```

plus the action check `FUN_00279600`, the action map `FUN_002796c8`, the move
rotation `FUN_0027c7b8` and its three tables, the leg thresholds
`FUN_0027ccd0`, the body sweep `FUN_0027c8a0`, the splash `FUN_0027ce48`, the
script cue `FUN_0027cef8`, and `FUN_0027d230`.

States 4, 6, 7, 9, 10, 11 and 15 are the late-fight moves and the death; the
crab only reaches them once it has taken enough damage to shed a leg, and
`--actor-report` names any it does reach. `FUN_00277d30`, the boss camera
director, is ported -- see *The boss camera, and the shot that takes the close-up
rig away* below.

##### Verified

`s14_e001`, 12000 frames, no input. The crab inits, is bound 1 of 1, takes the
animatic's actions 11, 12, 13 and 14 in order, runs the throw for 872 ticks and
the swipe for 290, and **the party is built and the battle starts on frame
1863**. After that it cycles its opening rotation -- 2, 3, 8, 5 -- with no
unimplemented state handlers on either side. `s14_e012` still builds on frame 2,
starts on 247 and binds 5 of 5. `s01_e024` and `s01_e012` are byte-identical
over 3000 frames with `--actor-report` and `--scr-report`.

#### The boss camera, and the shot that takes the close-up rig away

`FUN_00277d30` is the boss camera director, and leaving it out cost more than
the view. It is one function with fifteen numbered shots, a sub-shot number, a
priority and a subject entity; a call whose priority is below the shot already
running is dropped, and a negative shot number releases the whole thing. Ten of
the shots are poses computed off the player, the boss, the boss's partner or a
bone; three are splines out of `0x0034E4C0`; two do nothing but claim the
priority slot.

The first time it is called after a release it takes the camera for itself --
`FUN_00217e18(1)` drops whatever was installed and `FUN_00217D70(0,0,0, 0,0,0)`
puts a fresh manual camera at the origin, which the shot then moves the same
frame. Releasing does **not** put the field camera back; it only clears the
latch, so the pose stands until something else asks.

##### The spline stride is twelve, and the copies prove it

`FUN_00217FE8(eye, rollZoomPairs, count, lookAt, lookCount)` walks its point
arrays at a stride the decompilation does not state. The three callers here
settle it: each copies a run of `.data` onto its stack in eight- and four-byte
pieces and then hands the copies over, and only a stride of **twelve** leaves
every point fully written. At a stride of sixteen, shot 9's second eye point and
shot 10's second look-at point would both read uninitialised stack.

So the three tables are plain runs of `Vec3`:

```
shot  9  0x0034E4C0  1 look-at, 2 eye, 2 (roll, zoom) pairs, over 0x1680 ticks
shot 10  0x0034E4F8  2 look-at, 3 eye, 3 pairs, over 0x12C0 ticks
shot 14  0x0034E550  2 look-at -- the first sampled live off DAT_0058BE90 --
                     3 eye, 3 pairs, over 0x12C0 ticks
```

##### Shot 3 is what rolls `DAT_0035528C` over

The crab reads `DAT_0035528C` to decide which side each of its shots comes from,
and nothing in the crab writes it except its own init. Shot 3 -- the slow orbit
around the player, a twentieth of a degree a tick -- is the writer: when the
angle passes the sub-shot's limit it writes the *next* sub-shot number into that
byte, so the side flips between moves. Miss the director and the side is
whatever the last `FUN_0027C7B8` roll left it, which is only ever 1 or 2.

Two more details worth keeping. Shot 3's look-at angle is a literal zero, not a
bearing -- the register handed to cos and sin is the one the function cleared on
entry -- so the shot always looks three units along +X of the player. And shot
6's sub-shot 2 swings off the *player's* facing where sub-shot 1 swings off the
boss's; they read `+0x5C` from different entities.

##### `uGpffffb6f1` and `DAT_00343880` are the screen smear

Both are already modelled: `uGpffffb6f1` is `DAT_00355661`, the smear's target
alpha, and `DAT_00343880` is the rotation the `0xC9` transform carries. The
director raises them for shots 6, 8 and 11, the crab's slam raises the alpha for
the impact, and the crab's wrapper clears the whole `0xC9` block plus the camera
roll every fight frame before `FUN_00279180` gets a say -- so a shot that wants
the smear has to re-raise it every frame, which is exactly what it does.

##### The close-up rig is a cinematic prop, and only the swipe puts it away

`s14_e001` places a type `0x28` close-up mount at load, hides it, and tags it
`0x2E`; opcode `0x13F` builds its rig and the script tags the published bust and
hair slots `0x2F` and `0x30`. Those three tags are exactly what the crab's throw
looks up:

```
FUN_0027BBE0  DAT_00325900 = tag 0x2E   the mount -- stood at (1.2, -3.2, 0),
                                        unhidden, and framed by shots 12 and 13
              DAT_00325904 = tag 0x2F   +0x08 |= 0x80
              DAT_00325908 = tag 0x30   +0x08 |= 0x80, animation 1
              pool slot 0   +0x08 |= 1, +0x04 |= 1   -- the player, hidden
```

and `FUN_0027C458`, the swipe, is the only thing that gives them back:
`FUN_00265EC0` on all three, both player bits cleared, all three pointers
zeroed. The port had the tag lookups but not the pointers, so the release had
nothing to release: the mount stood in the middle of the arena for the rest of
the scene and the player never came back out of hiding. That is the "Orphen
floats in the middle of the battle" the animatic ended on.

##### Verified

`s14_e001`, 2500 frames, no input. The camera now moves through the animatic --
shot 9's fixed look-at `(1.810, -7.426, 0.770)` at frame 700, shot 10 mid-spline
at zoom 3.08 at frame 900, the close-up on the mount at `(1.200, -3.188, 0.437)`
at frame 1100 -- and the rig, its three children included, stops ticking at frame
1190 instead of running to the end. The party is still built and the battle still
starts on frame 1863, `s14_e012` still builds on frame 2 and binds 5 of 5, and
`s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report` and `--scr-report`.

##### The hurl's heading is three turns and a nudge, and the nudge is not ninety degrees

Shot 9 -- the one that watches the pair go into the sea -- has a **fixed**
look-at, `(1.810, -7.426, 0.770)`, and the crab is teleported to a fixed
`(1.335, -6.400, -0.800)` to throw from. Neither of those aims anything. The
only thing that decides where the pair actually flies is `+0x5C`, the facing
`FUN_0027BBE0` has accumulated by the time the claw lets go, plus the fifteen
degrees `FUN_0027CFE0` adds on top:

```
+0x5C = bearing(crab -> pair)   at state entry, turned to gradually
      - fGpffff9214             animation 2 ends: instant
      - fGpffff9224             animation 0 ends: gradual, and the turn blocks
                                the state body, so it always completes
      - fGpffff9228             animation 0x18 ends: instant
```

Three of those four words are `1.5708`. **`fGpffff9214` is `0.174533`** -- ten
degrees, the nudge the crab makes as it settles over the pair -- and the port had
it as a fourth quarter turn. Eighty degrees of error, applied to a heading, with
a camera that does not track: the pair left frame within twenty frames of the
hurl and landed somewhere off to the left, so the shot showed nothing but the
crab standing in the water for its whole hundred and eighty frames.

With the word read right the throw runs from `(1.600, -6.385, 1.055)` to
`(3.854, -10.848)`, which is `-63.2` degrees off the hurl point against shot 9's
own `-61.2`: the pair flies straight down the camera axis, away from the lens,
and the splash lands in frame at 750.

The lesson is the cheap one. Every constant in this state is a separate
gp-relative word and three neighbours being the same value is not evidence about
the fourth; all thirteen of `0x00353180..0x003531B0` are now checked against the
ELF, and the other twelve were right.

##### The `[embed]` lines in this scene are the diagnostic working

`s14_e001` prints 42 of them, all for the crab, all in the first fifty frames.
They are not a defect. The scene drives a collision group with opcode `0x7D`
**every frame** -- 261 times over 2500 frames -- so `DAT_003555D0` is genuinely
live on almost every frame here, where a field scene raises it for the two frames
`FUN_00208450`'s dirty-byte handshake needs and then drops it. Meanwhile the
crab's scripted wade-in re-places it with opcode `0x55` every frame, and the
authored curve runs one to ten hundredths of a unit under a seabed that is
rising faster than it is. The message says the ejection is armed on the same
frame the placement needs it, which is the case it was written to confirm.

#### The thrown pair, the water, and where the crab's noise comes from

Three things the animatic was missing, all of them hanging off the crab's
wrapper rather than off any state.

##### `FUN_0027CFE0` is the only thing that takes the thrown pair away

The wrapper runs it every frame its mode byte is 12, which is the whole of the
throw. The subject is the entity at `+0x1C0`, and it does nothing until the hurl
lets go -- the gate is `+0x192` below 1 and the victim's own state non-zero, and
state 13 clears that state on entry. Then four steps counted in the *victim's*
`+0x94`:

```
0  build the arc: from where it is to five units along the crab's facing plus
   fifteen degrees. The control point is the far end on both horizontal axes,
   so it leaves fast and coasts in; only the height has a real apex, two units
   up, landing at zero. Raise +0x04 bit 3.
1  turn the victim to the crab's facing plus fifteen degrees.
2  walk the arc over 0xC80 ticks. On the last tick drop +0x04 bit 3, which
   hands it back to the physics.
3  wait for +0x0C to answer "landed", then two rings of splashes, cue 0x7C,
   and FUN_00265EC0 -- the victim is destroyed and +0x1C0 cleared.
```

Step 3 is the whole of it. Without it the thrown character floats in the water
for the rest of the scene and the water never makes a sound.

##### Type 0x10D is the water, and it is thirty lines

`PTR_LAB_0031CAB0`'s entry for 0x10D is `FUN_002EB180`: shrink both scales
toward the end of the timer, die on zero, and drift outward along the heading
while `+0x60` is non-zero. `FUN_002EB278` spawns one, and it writes
`DAT_00354A3C` -- **-0.7, the water surface** -- into the height itself, taking
only x and y from the caller. That is why neither spawner has to know how deep
the thing making the splash is.

Two spawners, and between them they are most of the water in the scene:

```
FUN_002EB398(radius, scaleA, scaleB, at, count, life)   a ring, mode 0, static
FUN_002EB500(at, count)                                 a 90-degree fan behind
                                                        the facing, mode 1,
                                                        drifting outward
```

`FUN_0027CE48` calls the second every 0x140 ticks while the crab is in the water
and playing one of its six moving clips -- the spray it kicks up wading in --
and `FUN_0027CFE0` calls the first twice when the pair lands. Sixty of them now
spawn across a 2500-frame run of `s14_e001`, where before there were none.

##### `FUN_0027EF40` is the crab's entire sound layer

The wrapper runs it outside the mode gate and outside the beat ceiling, so it is
the one piece of the crab that is audible from the frame the scene loads. Every
cue is a clip keyframe -- the animation says which move, the timeline cursor
says which frame of it, and a contact bit says the frame is the one the claw or
the foot actually lands on. Bit 8 is the claw, bit 4 the legs:

```
animation 8    the slam      cursor 0 / 6 / 8   0x114 / 0x115 / 0x116
animation 9    the stamp     cursor 0           0x117
animation 2    the walk      cursor 0           0x118 above -2, else 0x119
animation 3    the charge    cursor 4           0x118 above -4, else 0x119
animation 0x19 backing up    cursor 4           0x118 above -4, else 0x119
```

A 2500-frame run went from **8 sound events to 41**: seven 0x114, six each of
0x115, 0x116 and 0x117, nine 0x119, and the splash. 0x118 never fires in this
arena because the crab is never above the shallow line.

##### Still out

`FUN_0027F1A8`, which animations 0, 1 and 3 also reach, puts two puffs on the
crab's bones 2 and 3 through `FUN_0021F6E8` -- the same spawner the status aura
wants, and the port still has no equivalent. `FUN_0027ED58` (the three debris
pools, and cue 0xC0 when the crab drops what it is holding), `FUN_0027E118` (the
falling debris, type 0x10B) and `FUN_0027DC38` are the wrapper's other three
mode-14 helpers and are not ported. `FUN_0027CF20`'s impact dust needs
`FUN_0021A4F8`'s pool, which is a third particle system with its own step and
draw. `FUN_002EAC48`, state 5's bubble rings, is unported. So is the pad rumble,
`FUN_0023BBD8`.

#### The crab's effects: a bubble, a fourth particle system, and three debris pools

##### Type 0x10C is the stamp's bubbles

`PTR_LAB_0031CAB0`'s entry for 0x10C is `FUN_002EA7F0`, a two-stage rise. Stage
one runs the spawn timer down while the bubble travels along `+0x5C` and climbs
at `+0x1A0`; stage two adds five to the climb and scales the *horizontal* speed
by however much of its own timer is left, so it stalls as it surfaces. Either
stage ends on a `+0x0C` mask: `0x4006` -- a wall, a ceiling or the map --
destroys it outright, `0x60` -- another entity -- pops it.

Only the popping matters, and only once. `DAT_0035529C` latches the moment a
bubble lands `FUN_002EF510`, so a column of twenty cannot land twenty hits. And
only **state 0** can hit at all: state 1 is the same machine with the attack
dropped, which is what a mode-1 bubble is for.

`FUN_002EAC48` spawns them, and the crab's stamp (`FUN_0027A440`, animation 9)
asks for ten, fifteen or twenty depending on how many legs it has left -- and at
two legs gone it doubles the spread as well -- off bone 6, then three more of
mode 4 in a half-unit ring at 0, 120 and 240 degrees. Nineteen of them now spawn
across a 2500-frame run.

##### The fourth particle system

`DAT_00355A9C` is a thousand entries of `0x28` with no behaviour pointer at all:
`FUN_0021A820` is the only thing that ever steps one. It shares nothing with
`DAT_00355620` (1536 entries, one installed behaviour) or `DAT_00355B74` (a
thousand sparks in ten groups, drawn as world-space streaks).

A puff does two things: it rises by 0.003 a tick, and if its heading is
**exactly** non-zero it slides along it by 0.01 a tick. That float test is not a
flag, so the first puff of every ring genuinely does not slide. The rest is the
fade -- `(remaining << 7) / total`, 0x80 down to 0 -- and `+0x21` picks between
a rectangle that hangs below the anchor and one centred on it.

One detail worth keeping: a puff is skipped while its projected `q` is **above**
`fGpffff839C` (0.7). That is a *near* cutoff, not a far one -- the opposite way
round from every other sprite in the frame.

Three spawners sit on top of it. `FUN_0021A4F8` is the ring and does no rolling
at all -- it only stores the two jitter ranges. `FUN_00219AF0` is what the
impact helpers call: two nested rings, rolling both jitters per outer step and
folding the first into the position, so `(5, 20)` puts a hundred ragged puffs
down. `FUN_0021A170` hands its own first roll in as both the x jitter *and* the
radius, which is how a single puff scatters instead of landing on the point.

`FUN_0027CF20` -- the claw landing -- fires two of those hundreds at once, and
that is now on screen for every swipe in the animatic: 134 puffs alive at frame
1260, 169 at 1350.

The puff's own packet is where the first version of this went wrong, and both
mistakes were in fields the arithmetic never touches. `FUN_0021A820` writes
`0x10004580` into the packet's `+0x0C`, and `FUN_00207de8`'s `& 0x1C000` ladder
tests bit `0x4000` first: **mode 1, an alpha blend.** The two neighbouring pools
write `0x10008080` and `0x10008580`, whose `0x8000` picks mode 2, and taking the
dust for additive as well turned a claw landing into a white screen. Then
`FUN_00207de8`:130-141 folds the vertex colour on the way into the GS, and for a
**textured** packet -- which this is, halfword `0x0221` -- it halves *all four*
channels, not just the alpha: `0xF0F0F0` arrives as `0x787878` and a fresh
puff's `0x80` alpha as `0x40`. Both are visible in a capture; neither shows up
in a count.

Worth flagging for the two pools that were ported before it: neither
`FUN_002D3058`'s particles nor `FUN_002190F8`'s hit sparks apply that same fold
in this port, and both submit textured packets (`0x0A2B` and `0x0022`), so both
reach the GS at twice the alpha the hardware would use. No scene the harness can
capture has either pool alive, so the correction is unverified and has not been
made.

##### The three debris pools

`FUN_00279940` clears thirty entries at `DAT_005737E8`, fifty at `DAT_00573860`
and thirty at `DAT_00573928`, and the crab's wrapper walks them through
`FUN_0027ED58`. **Everything in all three is retyped to 400**, whose handler is
`FUN_00239E78`, the no-op -- that is the point: the crab steps them, so the
actor loop must not.

```
DAT_005737E8  FUN_0027E118 drops one piece of wreckage every 16000 ticks at
              x = 7, five to nine units up the arena; FUN_0027E5D8 slides it,
              and a hit turns it into cue 0xC0, a burst of chunks and two
              splash rings
DAT_00573860  the arena's own destructibles, bound at crab init by four
              FUN_002797D0 calls -- tags 20..23 the lamps (which take a light
              slot), 30..34 the crates, 35..39 the barrels, 41..42 the two that
              keep their own type -- and stepped by FUN_0027E770
DAT_00573928  the chunks a broken prop throws, from FUN_0027E370; FUN_0027EBE0
              slides each one and trails a dust puff a frame behind it
```

Sixteen type-400 entities now live in `s14_e001`: fourteen props bound at init
and two pieces of wreckage, all of them reported `implemented` where the same
slots used to read `UNIMPLEMENTED` map-streamed props.

`FUN_0027E118`'s one quirk is worth writing down rather than reproducing:
`FUN_0025BA98(0x10B, ...)` indexes the object stat groups at `type - 0x272`,
which is **negative** for 0x10B, so the original copies a byte from before the
group into `+0x12C`. There is no faithful value to port; the field keeps its
default and the read is noted where it would have been.

##### Still out, and why

`FUN_0021F6E8` is a **fifth** particle system and it is two pools, not one: a
hundred emitters of `0x3C` that spawn into a thousand particles of `0x24`, and
only the second of those draws. `FUN_0021FB68` has no packet in it at all. So it
is the whole subsystem -- seven functions, two pools, two draw paths -- or
nothing, and it is the only thing blocking `FUN_0027F1A8` (the crab's bone dust
on animations 0, 1 and 3) and `FUN_002EF688`'s kinds 2 and 3 (the crate and
barrel bursts). Both call sites are in place with the gap named.

`FUN_0027DC38` is the fight's own bookkeeping -- the live-enemy count, the
target list, the music step-downs, and the write of 3000 into script work word 0
that ends the battle. It belongs with the battle module rather than the crab.

`FUN_0023BBD8` is the pad rumble. The port has no motor anywhere, and three
other places already say so.

**Type 0x7E**, the swarm the corpse lets out, is ported -- see below.

#### The crab could only ever fight with a third of itself

Two separate faults, and between them they were the whole fight after the first
hit landed.

##### The rotation's other two tables had no handlers

`FUN_0027C7B8` is the crab's move chooser and it walks one of three tables,
picked by how far through its damage thresholds it is:

```
DAT_00325910   2, 3, 8, 5, 2, 8, 3    opening
DAT_00325920   7, 4, 7, 4, 7          one leg gone
DAT_003552A0   9, 10, 11              out of hit points
```

Every entry of the opening table was ported. **None of the other eight were.**
A state with no handler never calls `FUN_0027C7B8` again, so the first time the
player took a threshold off the crab it moved onto a state that did nothing and
stayed there for the rest of the scene, playing the neutral clip `FUN_00225BF0`
had just set. `--actor-report` said so all along -- it lists the handlers a run
reaches -- but a headless run never damages the crab, so the middle rotation
was never entered under test.

The seven that are now here:

```
 4  FUN_00279F50  the boulder. Drop a type 0x10B into the water two units to
                  one side, walk to it, swing (ten degrees either way with all
                  legs, always left with one gone), take it on claw bone 12 or
                  18, turn, and throw. Six sub-phases on +0x1B9.
 6  FUN_0027A7E8  back off to (0, -4) without turning round
 7  FUN_0027A958  back away to one of three corners in turn -- (0, -9),
                  (3.5, -8), (-3.5, -8) -- on +0x1CE, which the hit reaction
                  clears
 9  FUN_0027ACC8  walk to (0, -6), the mark the finale starts from
10  FUN_0027AE98  the run at the shore: (0, 7) at thirty rather than fifty,
                  six camera cuts on the way in as z crosses -3, -0.8, 0.8,
                  1.2 and 4.5, DAT_0035526B driven to 3 at the start and 5 at
                  a unit's range, and the tagged-19 gate broken past 1.2
11  FUN_0027B380  the death. Four beats on +0x1A4 once the clip has run: the
                  corpse settles, the camera zoom pulls in over 0xC80 ticks,
                  the swarm is let out, and the fade to state 15 is timed on
                  +0x1CC. It is the only thing in the game that writes script
                  work word 0 itself -- 2000, from `FUN_0027B380`
15  FUN_0027BA90  the corpse: release the party's records, clear the wreck
                  pool, take a light slot and stir the swarm on a timer
```

`FUN_002EA238`, type 0x10B, came with state 4 -- five states of its own (fall,
slide, settle, sink, and a quadratic Bezier flight to half a unit above the
player) and it is the same entity `FUN_0027E118` already drops into the water as
rubble.

`FUN_002EA238`'s sibling under this is **type 0x7E**, the swarm `FUN_0027C950`
lets out of the corpse -- a second enemy with its own wrapper and its own state
table. It has its own section below.

##### The 0.26 step cap was not a step cap

The other half, and it predates all of the above: the crab could not climb out
of its own arena.

`s14_e001` is a pool at -1.0 with a hard half-unit wall at z = -4.3, and two of
the crab's moves station it in that pool -- state 8 walks to (0, -5) and state 9
to (0, -6). The port's movement step refused any destination more than
`DAT_00352434` (0.26) above the actor, so once the crab was down there nothing
could bring it back up. Its charge, its stamp and its run at the shore all
played their clips against the wall without moving. **That is the crab walking
on the spot** -- and it is why the slam never arrived, because state 3 aims one
unit past the player and then never covers the distance.

That test is not what `FUN_002262C0` does with the constant:

```
lVar7 = FUN_00227390(destination);
if (+0x4C - w[6] <= +0x7C) {
  if (+0x7C < w[5] - w[6])   refuse;
  if (lVar7 != 0)            accept;      <-- no height test at all
  if ((+0x0C & 0x10000) == 0) { ...the DAT_00352434 branch... }
}
```

The 0.26 is the **fallback** taken only when the corner scan found nothing --
"is there a ledge here the scan missed" -- and both real height gates are
against `+0x7C`, the entity's own step allowance, measured against a second
workspace height the port's `TerrainSurface` does not carry. A scan that
answers is accepted however tall the step. Capping it gave every actor in the
game a quarter-unit ceiling it does not have.

The slope gate beside it (`puVar11[2] <= +0x80`) is real and stays; that is the
one that stops a follower ratcheting up the shop counter, and `s01_e012`'s
followers still sit exactly on their floors over twelve thousand frames. Both
regression scenes are byte-identical without the cap.

##### Verified

`s14_e001`, sixteen thousand frames with the sword swinging on a repeating
range: `--actor-report` reaches states 0, 1, 2, 3, **4**, 5, **7**, 8, 12, 13
and 14 with none unimplemented, the hit reaction fires 225 ticks, and the crab
retreats to (3.46, -7.93) -- the middle rotation, walking. Forcing the two
damage phases directly reaches **9, 10, 11 and 15** as well, and the fight plays
through to the death and the corpse. A capture at frame 6400 has the crab out of
the pool and up against Orphen mid-attack.

`s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report --scr-report`.

#### One countdown in the fight ends on zero, and it is the one that matters

`FUN_00279298`'s invulnerability timer is spelled differently from every other
timer in the crab:

```c
iVar9 = *(ushort *)(entity + 0x1CA) - DAT_003555BC;
*(short *)(entity + 0x1CA) = (short)iVar9;
if (iVar9 * 0x10000 < 1) {                 // <-- < 1, not < 0
  *(undefined2 *)(entity + 0x1CA) = 0;
  *(ushort *)(entity + 4) &= 0xFFEF;       // drop the "cannot be hit" bit
}
```

`* 0x10000 < 1` is `(short)remaining <= 0`. Everything else in the file -- and
the shared `countdown` helper the port wrote for them -- is `< 0`. The port used
the shared one here.

That is not an academic difference. The reload is `0x1900` and `frameTicks` is
32 in this scene, so 6400 / 32 = **200 exactly**: the timer steps 6368, 6336, …,
32, 0 and lands on zero every single time, never below it. Tested strictly it
never fires, `+0x04` bit 0x10 stays up for good, and `FUN_00216140`'s candidate
filter drops the crab from every hit test from then on. **One hit and the boss is
untouchable for the rest of the fight.**

It only showed up with a spell. `+0x04` bit 0x10 is raised by

```c
if (1 < (byte)(entity[0xBC] - 0x19)) { entity[4] |= 0x10; }
```

and the sword leaves `+0xBC` inside that window, so a Cross-only run keeps
connecting and looks fine -- 120 hit points down to 84 over seven thousand
frames. Hand of Pyro does not: the trace goes `f=2720 af=0080` (hit), `f=2721
af=0090 flinch=6368`, `f=2920 flinch=0 af=0090` -- the timer ran out and the bit
never came off -- and nothing lands again. With the inclusive test the same run
reads `f=2920 flinch=0 af=0080` and carries on 120, 108, 96, 84, 72, 60, 48 down
to zero.

##### `FUN_00215E48` is the already-hit set, not a contact flag

Found next to it. `FUN_00215E48` zeroes the nine words from `+0xCC` to `+0xEC`
and drops `+0x06` bit 0x40; those words are the set `FUN_002148A8` and
`FUN_00215AC8` mark a victim in, so the call means "forget everything this swing
has already touched". `original_hit_test.cpp` has it under that name.

`original_crab_boss.cpp` carried a second copy that only dropped the flag, and
state 3 -- the charge and slam -- is the one caller. So the crab's slam
remembered its victim between swings and could land on Orphen exactly once in
the whole fight. Type 0x7E's leap makes the same call and now makes the same one.

##### Verified

`s14_e001` with Hand of Pyro on a repeating range and no forced phases: the crab
goes from 120 hit points to 0 by frame 6055, and `--actor-report` reaches **all
sixteen** of its states plus the swarm's five, none unimplemented. `s01_e024` and
`s01_e012` byte-identical.

#### Type 0x7E is the swarm, and it is a whole enemy

Nothing spawns a `0x7E` from a placement. `FUN_0027C950` -- the crab's state 11,
its death -- allocates a hundred of them around the corpse and numbers them off
in `+0x95`, and `FUN_0027BA20` (state 15) wakes one to five at a time on a
300..599 beat timer. So the swarm only exists after the boss is dead, which is
why it went unnoticed for as long as the late-fight states did.

It is built like the other two battle enemies -- wrapper `FUN_00276C30`, action
check `FUN_00276D50`, seven states at `PTR_FUN_00325868` -- and its state 0 is
`FUN_0027F978` line for line, down to the three `FUN_00216078` calls that fill
its own attack bank at `DAT_00573778`. The one thing only it does is roll a size
between 2.00 and 2.99 onto both scale axes.

```
0  FUN_00276DE0  init
1  FUN_00276F50  walk to the mark at +0x3C/+0x40 at 10..29
2  FUN_00277110  roll the next mark -- x in -11..-6, z 4.0 give or take 0.8,
                 the far end of the beach -- and walk to that
3  FUN_00277410  mill: a whole-degree heading held 50..149 beats, one chance in
                 a hundred a frame of a small hop and one in ten of those of a
                 bubble. Every time the hold runs out the *mark* creeps 0.05
                 toward negative x if the player is more than a unit away, so
                 the swarm drifts as a body rather than each one wandering off
4  FUN_00277860  the flinch, on FUN_0023A678's floored countdown
5  LAB_00277828  the death
6  FUN_002778B0  the leap
```

##### State 5 is thirteen instructions and has no `src/` file

`PTR_FUN_00325868[5]` is `0x00277828`, which sits between the end of
`FUN_00277410` and the start of `FUN_00277860`. Ghidra has no function there --
`decompile_function_by_address` answers "no function found" -- so it was read out
of the executable:

```
80820094  lb   v0, 0x94(a0)
14400009  bne  v0, zero, 0x277854      -> jr ra
94830006  lhu  v1, 0x06(a0)
30620001  andi v0, v1, 1
10400005  beq  v0, zero, 0x277854      -> jr ra
34630010  ori  v1, v1, 0x10
94820004  lhu  v0, 0x04(a0)
a4830006  sh   v1, 0x06(a0)
34420800  ori  v0, v0, 0x800
a4820004  sh   v0, 0x04(a0)
```

Wait for the clip, then raise `+0x06` bit 0x10 and `+0x04` bit **0x800** -- which
is the flag `FUN_00239CE0` reads *instead of* the type handler, so from the next
frame on the corpse belongs to `FUN_0023A568`'s fade path and this state never
runs again. That is the whole death: no burst, no cue, no record release.

##### The leap is a Bezier with the pitch swept through it

State 6 is three phases on `+0x94`. Phase 0 closes to a unit and a half short of
the player at thirty -- and the stopping point is measured from the *player* back
toward the crab, so it stops the same distance out however it came in. Phase 1
keys cue `0x11C`, drops the `0x00FA98AE` glow it has been wearing since the state
began, raises `+0x04` bit 3 and pitches nose-down a quarter turn, then builds
three control points: where it stands, a point 2.5 above the victim, and 0.3
*past* him. Phase 2 walks that arc over `0xA00` ticks against a 2560 divisor,
sweeping `+0x154` a quarter turn back the other way and sweeping its own body box
(`FUN_00277CA0` -> `FUN_00215AC8`, attack record 0) every frame.

A wall mid-flight -- `+0x0C` bit 1 -- resets the arc timer to zero rather than
ending the leap, so it simply tries again from wherever it stopped.

##### The action check only understands two orders

`FUN_00276D50` clears `+0x154`, writes 1 into the record's `+0x2C` and drops
`+0x04` bit 3 for any pending byte at all, then: **10** means stop, and it is the
only one that returns "handled" *and* leaves the pending byte standing so it
fires again next frame; **11** means hold, without suppressing the state;
everything else is swallowed and cleared. The battle AI VM is what would send
those, and it is still out -- but the swarm never needed it, because nothing in
the crab's death path issues an order.

##### Verified

`s14_e001`, sixteen thousand frames with both damage phases forced: a hundred
`0x7E` allocate and every one survives the run, `--actor-report` reaches their
states 0, 1, 2, 3 and 6 with none unimplemented, and the hit-test counter goes
from four sweeps to 37893 -- the leaps landing on Orphen, four contacts and 48
damage. All hundred sit on their own floors at the end. States 4 and 5 need
something to damage *them*, which nothing headless does.

#### 0xAF and 0xAE are entity types, not sound cues

`FUN_0027CCD0` is the crab's damage staging: at six tenths and again at three
tenths of maximum hit points it sheds a claw, and the stage it raises is also
what moves the fight onto its next move rotation. Four lines each time:

```c
FUN_0020D8C0(crab, 0x0B, zeroed, 0);        // two bones of the claw
FUN_0020D8C0(crab, 0x0C, zeroed, 0);
FUN_0020DC88(crab, 0, zero, &at);           // bone 0 in world space
FUN_002EB7F0(crab, 0xAF, &at);              // and the claw itself
```

The port had `0xAF` and `0xAE` down as `FUN_00267D38` sound cues and left the
bone half with a note saying the override table was not modelled here. Both
halves were wrong. `FUN_002EB7F0`'s second argument goes straight to
`FUN_00265E28`: **they are entity type ids**, and the two claws are real
entities that fly off, land and fade -- `FUN_002EB680` is their behaviour, two
beats kept apart by `+0xA0`. And the pose handed to `FUN_0020D8C0` is zeroed,
which is not an identity: field 3 of the seven is the scale, so the override
collapses the bone and everything under it to a point. That is how the claw
stops being drawn, and the port has had `FUN_0020d8c0_set_bone_override` since
the close-up rig went in.

So the crab kept both claws for the whole fight and made a noise it should not
have made twice.

##### Verified

`s14_e001` with Hand of Pyro on a repeating range: the first claw goes at 60 hit
points of 120 and the second at 24, one type 0xAF and one type 0xAE spawn and
each runs 325 frames before freeing its slot, and the crab's silhouette at frame
5200 has neither pincer where at 3100 it has both. `s01_e024` and `s01_e012`
byte-identical.

#### The wall came down in silence: three dust opcodes the script layer threw away

s14_e001 opens with the crab coming through the pier wall, and in the port that
happened with no dust at all. The crab's own impacts were fine -- `FUN_0027CF20`
is ported and its two hundred-puff rings show up on every swipe -- because the
burst at the wall is not the crab's. It is the **scene script's**, and the
handler for it read its operands and dropped them:

```cpp
case 0x10B:
  note(OpcodeSupport::OperandsOnly);
  return consumeOnly(opcode, 10);      // "a graphics submitter"
```

`FUN_00262780` is opcode 0x10B and it calls `FUN_002198A0`, which is the same
nested-ring spawner as `FUN_00219AF0` -- the one the crab uses -- with the colour
and the shape forced to zero and `fGpffff8384` for the turn instead of
`fGpffff8388`. Its two siblings are the same story: 0x10A (`FUN_00262690` ->
`FUN_00219FC8`) is `FUN_0021A170` with the same two zeroed, and 0x10C
(`FUN_00262898` -> `FUN_00219D60`) is 0x10B's burst with the eleventh operand
handed through as the puff colour. All three were `OperandsOnly`. s14_e001 fires
six 0x10Bs, the first at blob `0x2253` on frame 65.

The trap in these three is the operand order. Ghidra spells the calls with the
arguments already shuffled, and the stack slots make it look like seven scaled
coordinates followed by the raw parameters. They are read in declaration order,
and the **fifth expression is the life spread** -- a raw halfword -- sitting
between the size and the two jitters:

| # | 0x10A | 0x10B | 0x10C |
|---|---|---|---|
| 1-4 | x, y, z, size | x, y, z, size | x, y, z, size |
| 5 | life spread (raw) | life spread (raw) | life spread (raw) |
| 6-7 | jitter x, jitter y | jitter x, jitter y | jitter x, jitter y |
| 8 | count (raw) | radius | radius |
| 9 | -- | outer count (raw) | outer count (raw) |
| 10 | -- | inner count (raw) | inner count (raw) |
| 11 | -- | -- | colour (raw) |

`DAT_00352C70`, `DAT_00352C74` and `fGpffff8D08` are all 100000, the same scale
as every other position operand, and it applies to the first four and to the
jitters and radius only.

One thing fell out of reading the pool again: all four full-turn words the ring
steppers use -- `fGpffff8384`, `fGpffff8388`, `fGpffff838c` and `DAT_00352300` --
hold `0x40C90FD8`, which is 6.283184 and not `float(2*pi)` (`0x40C90FDB`). The
port had the mathematical constant in all four, which walks the outer ring by a
very slightly different step. Fixed with the opcodes.

#### The crab fights in the water, and two things were keeping it on the pier

A PCSX2 save state taken as the battle opens has the crab at **(0.00, -3.18,
-1.00)** with `+0x4C` = -1.00, all four corner heights -1.000 and `+0x0A` =
0x4289 -- primitive 649, the pool floor. The port had it at -0.50, up on the
planks beside the player, stepping on and off them for the rest of the fight.
Two separate causes, one of them a correction this port had made to itself.

##### `FUN_0022DC68` is not `FUN_0022DBC8`, and the crab calls it

The arena is a pool floor at -1.0 (primitives 649 and 680, `x` -5..5, `y` -5..0)
with three planked sections laid over it at -0.5, tagged 1, 2 and 4 in record78
`+0x04`. The crab smashes one per swipe, and the swipe's own tail says so:

```c
FUN_0022DC68(1 << (DAT_0035526A - 1 & 0x1f), 0, 0x800);
FUN_00225BC8(crab, 0);
```

`FUN_0022DC68(selector, enable, bits)` walks the primitive array and sets or
clears `bits` in **record78 +0x00** for every primitive whose `+0x04` intersects
the selector. 0x800 is the bit both loops of `FUN_00227840` require before a
primitive is offered to the overlap test at all, so clearing it takes the
surface out of the ground scan. The save state shows the result plainly: those
three primitives read `+0x00` = 0x220 while their draw-side copy at record80
`+0x70` still holds the file's 0xA20.

The port had the whole mechanism -- opcode 0xA6 has used it since the doors went
in -- but the crab's call site was a comment saying it would be routed "once the
actor environment carries it", and the actor environment never did. `ActorEnvironment`
now carries `FUN_0022dc68_enable_map_terrain`, and it is a `PortRuntime` member
rather than a second lambda body, because the two callers must not drift.

It is worth being explicit about the pair, because they select on the same word
and differ only in what they write:

| | selects on | writes | effect |
|---|---|---|---|
| `FUN_0022DBC8` | record78 +0x04 | record80 +0x70 bit 0x20 | hide from the draw |
| `FUN_0022DC68` | record78 +0x04 | record78 +0x00 bit 0x800 | stop being ground |

##### A non-player actor may not step up more than 0.26 either

With the planks gone the crab still climbed them, because the port's actor
physics had no step limit at all. The predicate was `slopeAngle <= +0x80` and
nothing else, on a note arguing that `FUN_002262C0` accepts any step the corner
scan answers. That reading is wrong, and the mistake is in one variable:

```c
lVar7 = FUN_00227390(dest);
if (+0x4C - w[6] <= +0x7C) {
  if (+0x7C < w[5] - w[6]) refuse;
  if (lVar7 != 0) accept;                 // <-- not "the scan answered"
  if ((+0x0C & 0x10000) == 0) { ...the DAT_00352434 branch... }
}
```

`lVar7` is `FUN_00227390`'s return, and that function ends

```c
uVar1 = 0;
if (fVar6 <= *(float *)(entity + 0x28)) { uVar1 = 1; ...required mask... }
```

-- **1 only when the surface it found is at or below the feet**. Anything higher
never reaches the accept path; the 0.26 `DAT_00352434` branch is the only way up,
and it additionally wants `+0x28 == +0x50` (settled on the ground) and the slope
gate. That is the same rule the lead has carried as `canStepToHeight` all along.
The two `+0x7C` gates either side of it are inert here: **every entity in the
s14_e001 dump reads +0x7C = 100.0**, crab, lead and props alike, so neither can
fire, and the workspace's `w[6]` -- the lowest of the four corners -- is not
carried yet. They are noted in the code rather than guessed at.

##### Verified

`s14_e001`: the crab's walk-in now ends at (0.51, -3.53, **-1.00**) facing
1.571, against the save state's (0.00, -3.18, -1.00) facing 1.5708, and it stays
at -1.00 for the whole fight instead of bobbing between -1.00 and -0.50. The
player's own ground primitive moves from 370 (`lead` 0xA20, a plank the crab has
already broken) to 459 (0xA04), the same class of primitive as the save state's
483. `s01_e024` and `s01_e012` byte-identical over 3000 frames.

##### The endpoint is emergent, and the save state is a retry

The walk-in ends half a unit short and half a unit right of the save state's
crab. Chasing it settled what the number is made of, and the first guess -- "a
different scene-entry position" -- was wrong.

`FUN_0027C458` runs **three** times, once per swipe of `FUN_0027C3E8`'s ladder,
and each pass aims 1.5 units along the bearing to wherever the player is
standing *that frame*. The player is not wandering: `DAT_00325888` is a
four-entry table of the spots the crab's swipe throws him to --

```
(0.000, 0.000)  (-1.552, -3.087)  (0.108, -2.068)  (-0.082, -0.254)
```

-- and the port's three aims read exactly (1.200, -3.200), (-1.552, -3.087) and
(0.108, -2.068), ending with the player parked on entry 3. So the endpoint is
the accumulation of three aimed walks, not an authored spot: the decompressed
script contains no -3.18 in any width, and the only placement of the crab after
the opening is the `0x55` at (0, -3.5, 0) that both builds run.

The walks themselves check out. `FUN_0023A6D0`'s second argument is pool slot 0,
not the crab, so the hold is the *player's* distance to the aim point rather
than the crab's own step -- and the port's first walk travels 1.703 units where
that distance is 1.703, which is the odd semantics reproducing exactly.

**The save state is a retry, which is why its crab cannot be matched frame for
frame.** Four things say so at once, none of which can hold in a first run:

- the three pier planks are already non-solid, at a beat *before* the crab has
  swiped;
- `DAT_0035526A`, the swipe counter, is 0 -- only the crab's state 0
  (`FUN_00279940`) clears it and only `FUN_0027C3E8` raises it;
- control block 0 is fully populated, target 65, with a stale pending action
  0x8C, while `sGpffffb052` is 0 and `DAT_00354EBC` is 0 -- a party built and
  then torn down;
- the crab sits at x = **exactly** 0.0 having moved north from the (0, -3.5)
  placement, which only happens if it never turned off the heading that
  placement left it on.

Reproducing that state in the port -- planks cleared from frame 0 -- puts the
crab at (0.00, -3.50, -1.00) facing 1.5708 at the same beat, which is the same
spot and the same facing to two decimals.

##### Retracted: the trig is not the difference

An earlier pass here claimed the save state proved the original's trig differs
from libm. It does not. The crab holds `+0x5C` = `+0x19C` = **0x3FC90FA6**, and
that value is exactly `float(1.57079)` -- and exactly `157079 / 100000`, an
authored five-decimal angle at the script's own coordinate scale. `float(pi/2)`
is `0x3FC90FDB`, 53 ULP away, which is a hundred times further than any two
implementations of `atan2f` can be from each other. The port produces
`0x3FC90FA6` too, and carries it through the whole opening walk.

`FUN_00305130`, `FUN_00305218` and `FUN_00305408` are, on inspection, plain
fdlibm: `|x| < 0x3f490fd9` into `__kernel_cosf` / `__kernel_sinf`, otherwise
`__ieee754_rem_pio2f` and a quadrant switch, and `FUN_00305408` is only the
`matherr` wrapper around `__ieee754_atan2f` -- the string "atan2f" is right
there in it. That is the same algorithm family the C runtime already runs, so
`std::cos` / `std::sin` / `std::atan2` agree with them to about a ULP and
lifting them out of `src/` would buy nothing.

The one systematic float difference on this hardware is the **EE FPU**: no
denormals, no infinities or NaNs, flush-to-zero, and non-IEEE rounding on the
multiply-add. It touches every float operation in the game rather than three
functions, and porting fdlibm would not move it.

What is still worth a look is smaller and local: the crab enters the animation-8
spin holding the authored 1.57079 and comes out of it on `std::` pi/2, because
the spin writes quarter-turns built from the mathematical constant. The same
class of thing is real elsewhere -- every full-turn word the dust rings step by
(`fGpffff8384`/`8388`/`838c`, `DAT_00352300`) holds `0x40C90FD8`, 6.283184, and
the port had `float(2*pi)` = `0x40C90FDB` in all four.

#### A boss fight targets from a different table, and the port had never built it

`FUN_002462C8` has two targeting modes and picks between them on one word:

```c
if (DAT_00354EC0 == 0) {          // the field encounter
  ...
  DAT_00354E96 = 0xF00;           // arm the display: FUN_0023C340 freezes the
  ...                             // field for 120 frames while you pick
} else {                          // a boss
  DAT_00354E96 = 0;               // never arms; nothing freezes
  FUN_00248108();
  target = FUN_002481F0(target, direction, halfWrap);
  FUN_00249388(control, 0x4000, target);
}
```

The second arm is what a boss encounter feels like: **the target changes on the
frame the direction is read**, with no pause and no pentagon, because every
targetable thing is already wearing its own type 0x192 cursor. Ghidra reports
one write to `DAT_00354EC0` in the whole executable and it is the zero in
`FUN_0023F288`, which is why this looked like dead code -- but a raw scan of
`SLUS_200.11` for gp-relative stores finds a second at `0x00247F18`, a
two-instruction setter Ghidra had not attached to any caller list:

```
00247f18  af84af50   sw a0, -0x50b0(gp)      // DAT_00354EC0 = table
00247f1c  a785af54   sh a1, -0x50ac(gp)      // DAT_00354EC4 = count
```

Ten of its callers sit in the `0x26Cxxx` block, and they are **scene modules**.
`FUN_0022A360` reads the loaded scene's descriptor -- SCR.BIN resource 1, header
word 7, sixteen per-section lists of eight-halfword records -- and takes `+0x02`
as an index into `PTR_LAB_003252B8`, twenty-eight per-scene hooks called with a
mode number at ten points in the frame. Ten of those hooks open with

```c
FUN_00267E78(0x3253C0, 400);      // twenty rows of 0x14, cleared
FUN_00247F18(0x3253C0, 0x14);
```

s14_e001 is module **10**, `FUN_0026C0F0`. The port had no module dispatch at
all, so the crab fight ran the field encounter's paused display over an actor
table that holds exactly one record -- the crab -- and the moment the crab died
there was nothing left in the game the player could aim at.

##### The marker table is a registry of live cursors

`DAT_003253C0`, twenty rows of `{s16 kind, s16 slot, s16 cursor, f32 x, f32 y,
f32 z}`. `FUN_00247F28` stamps a row, raises the entity's `+0x96` bit 0 and --
for kind 2 -- spawns its 0x192 cursor from `FUN_002D86B0` with the row's own
offset, leaving the row as kind 3. `FUN_00248040` is the reverse and is called
from three places that all matter here: the swarm crab's own wrapper when one
walks off the map, `FUN_0027D860` when the fight ends, and `FUN_0027DA48` before
it re-aims.

##### `FUN_0027DC38` is the fight's bookkeeping, and it is what fills the table

Called once a frame from `FUN_00279298`, between the camera and the wreckage
drop. It was the one helper of the crab's six that was still out; without it the
marker table would have been installed and left empty. The crab's `+0x08` bit 0
splits it:

- **clear** -- the crab is alive. Mark the crab itself, and any wreckage still
  carrying `+0x0C` bit 0x1000.
- **set** -- the corpse. The swarm owns the fight: prune the hundred-row list at
  `iGpffffbe04`, rebuild it out of the pool when anything has gone, and every
  sixteen thousand ticks -- or the moment the list changes -- run `FUN_0027DA48`,
  which measures each crab against the player, sorts the list by that distance
  and gives **the nearest one** the only cursor the swarm gets. So a hundred
  crabs are one rolling target. The arena props tagged `+0x1D4` bit 0x10 keep
  theirs alongside it, and when the last crab dies the music stops, script work
  word 0 takes 3000 and every row is released.

##### Nine state handlers were returning the charge instead of spending it

Found on the way, and the reason **no spell could be cast for the rest of the
scene** once the crab was dead. `FUN_00249610` writes each handler's return
value straight back into entity `+0x62` and then

```c
if (*(short *)(entity + 0x62) != 0) { control->flags38 |= 1; }
```

and bit 0 is what `FUN_002462C8` tests before it will accept a button. Every
class-1 handler returns an explicit 0 or 1 -- states 101, 103, 104, 116, 118 and
119 are two instructions, `jr ra; move v0, zero` -- and the port was handing the
incoming value back from nine of them, 120 included.

That is harmless until something else writes `+0x62`. The crab's dodge does:
`DAT_0058BF12` in `FUN_0027D230` **is** the player's `+0x62`, used as the spline
timer for the two runs the dodge makes, and it is left sitting at 0x1900 when
the run ends. State 120 then handed 6400 back every frame, bit 0 never came down
and the pad was dead from the dodge onward. The three returns in `FUN_0024CF20`
are 0, 0 and 1.

##### `+0x0C` bit 0x1000 is "I was drawn this frame", and the model path never set it

The marker table went in and still no cursor appeared. `FUN_0027DC38`'s prune
drops the row of anything that has stopped being drawn:

```c
else if (((&DAT_0058bebc)[iVar4 * 0x76] & 0x1000) == 0) goto LAB_0027dcd4;  // FUN_00248040
```

The port cleared that bit and never set it back, so the crab was pruned and
re-marked on every single frame -- its 0x192 destroyed and respawned before the
spawn-in clip could finish, which is exactly "no cursor at all". The clear was
ported (`LAB_0020C73C`, `+0x0C &= 0xFFFFCFFF`); the set was not:

```
0020cb18   *(uint *)(entity + 0xc) = *(uint *)(entity + 0xc) | 0x3000;
```

It sits inside `FUN_0020C810`, past every cull -- the near-plane and
distance-fade exits all leave through `LAB_0020C9AC`, which writes `+0xB0 = 0`
and `+0x08 |= 0x10` and returns. The sprite pass has its own copy at
`FUN_0020F510:177` and that one **was** ported, which is why the type 0x68
health bar behaved and nothing modelled did.

Three things read the bit, and all three were reading a permanent zero:

- `FUN_0025A298` -- a party follower is teleported up the lead's trail when it
  is off camera **and** was not drawn. The port teleported on the second test
  alone.
- `FUN_0020C810:86` -- an attached child takes its parent's transform only if
  the parent was drawn.
- `FUN_0027DC38` -- the above.

Not modelled: the culls `FUN_0020C810` takes before line 219, so an entity the
original would have dropped still latches in the port.

##### Verified

`s14_e001`: the crab wears a cursor from the opening of the fight and it is the
selected one -- animation 11, the pi/4 bracket -- with the standing wreckage
carrying unselected ones beside it; `FUN_002057C8(0xC9)` fires on the frame the
selection lands (cue 201, frame 1906 of a cold run) and again on every change.
From the corpse on, the target moves between swarm crabs as `FUN_0027DA48`
re-picks the nearest; Triangle charges and releases after the dodge where it
used to do nothing. `--battle-report` names the scene module and whether its
marker table went in. `s01_e024` and `s01_e012` byte-identical over 3000 frames
with the draw latch in.

Still out: the per-frame hooks of the other twenty-seven modules -- eighteen of
them belong to field scenes, and `s01_e024` is module 13 -- and the two music
step-downs `FUN_0027DC38` makes as the swarm thins, which are channel fades this
port's audio path handles for itself.

#### The handback after the fight is three writes, not one

`FUN_0027D230`'s `DAT_0035526B == 9` branch -- the frame the crab's finale gives
the player back -- is four statements, and the port had transcribed one of them:

```c
(&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C] = 6;                    // pending action
FUN_00249388((&DAT_0031D7B8)[(DAT_00354EBE - 1) * 0xF], 0x4000, DAT_003253C2);
FUN_00245978((&DAT_0031D7B8)[(DAT_00354EBE - 1) * 0xF],
             &DAT_0031D774 + DAT_00354EBE * 0x3C);
```

`FUN_00249388` with bit 0x4000 writes the control block's `+0x2C`, the target
index; `DAT_003253C2` is party record 0's pool slot, and `FUN_00249610`'s target
block takes its no-target branch on anything below 3. Harmless either way.

**`FUN_00245978` is not harmless.** It re-records where the member is standing
into control `+0x14/+0x16/+0x18` and `+0x26/+0x28/+0x2A` -- the same call
`FUN_00243F80` makes when the battle starts. State 120, the battle idle, arms a
timer and when it runs out measures the character against `+0x14/+0x16`; more
than three tenths of drift and it goes to state 108, the walk home. The two runs
this dodge makes put Orphen fifteen units from where the battle recorded him, at
the far end of the beach behind the arena. So he arrived, state 120 armed its
timer, state 108 took over, could not walk him back through the map, handed back
to 120, and the pair bounced for ever:

```
f=7457 st=120 anim=2    f=7463 st=108 anim=12   f=7512 st=108 anim=13
f=7562 st=120 anim=16   f=7571 st=120 anim=2    f=7623 st=108 anim=12   ...
```

on the spot, at (-15.00, 4.00), for the rest of the scene. That is the "jumping
in place". With the re-record he arrives and stands: the trace ends at f=7457
and nothing moves again.

##### And the swarm was spawned where its marks belong

`FUN_0027C950`'s inner loop places each crab with `FUN_002662E0` at the corpse's
bone 0 -- all hundred stacked on the body -- and writes the scattered point into
`+0x3C/+0x40`, which is the **mark** its state 1 then walks out to:

```c
FUN_002662E0(bone0.x, bone0.z, bone0.y, spawned);       // position
...
*(float *)(spawned + 0x3C) = crab.x + reach * cos(heading);   // mark
*(float *)(spawned + 0x40) = crab.z + reach * sin(heading);
```

The port had those two the other way round: the crabs appeared already spread and
every one of them carried a mark of (0, 0) -- the pool clear's -- so all hundred
converged on the arena centre in a column before starting their wander. They now
boil out of the body and fan off it.

What happens after that is the design and not a fault: state 2 sends each of them
to x in -11..-6 and z about 4, and state 3's mark creeps 0.05 further along -x
every time its hold expires while the player is more than a unit away. The swarm
walks down the beach after Orphen and leaps at him from a unit and a half out.

##### Verified

`s14_e001` with Hand of Pyro on a repeating range: the crab dies at frame 6055,
the hundred spawn on the corpse at (0.00, 5.30), and the lead ends the run at
(-15.00, 4.00) in **state 120, animation 2** -- standing -- for the remaining
6500 frames. `--actor-report` reaches every crab state and swarm states 0, 1, 2,
3 and 6, none unimplemented. `s01_e024` and `s01_e012` byte-identical.

#### The spell voice is a multi-clip VOICE.BIN bank

Casting speaks two lines, and neither is a sound cue. They are VOICE.BIN clips
played through the same reserved streaming voice a line of dialogue uses, driven
by a per-slot state machine in `DAT_0031DA60` that states 111 and 113 walk:

| step | what it does |
|---|---|
| 1 | ask for the bank (`FUN_00206ae0`), on acceptance → 2 |
| 2 | poll the load (`FUN_00206c28`), when it settles → 3 |
| 3 | at the charge marker, if nothing else is speaking, play clip 0 → 100 |
| 100 | on release, play clip 1 → 101; → 102 if something is speaking |

The bank id is `DAT_0031DA54`, which `FUN_002432d8:125` took from the spell
table's family column at `0x00325230`. Hand of Pyro is bank 7.

**A VOICE.BIN entry is not always one clip.** `FUN_00206aa0` copies the first
`0x40` bytes of a loaded entry into `DAT_00314BD0 + channel * 0x40` and
`FUN_00206f08` reads them as a directory: word 0 is the clip count, then one
word per clip packing `(offset in 16-byte units << 16) | size in 16-byte units`.
`FUN_00206d98` validates it before trusting it — `count - 1 < 0xF` **and** the
halfword at `+6` equal to `align16(count * 4 + 0x13)`, the directory's own
length — and an entry that fails is played whole, which is what an ordinary line
of dialogue is. Read out of the real file:

| bank | spell | clips | clip 0 | clip 1 |
|---|---|---|---|---|
| 4 | Sword of the Fallen Devil | 2 | 31,408 B | 4,064 B |
| 7 | Hand of Pyro | 2 | 31,408 B | 4,064 B |
| 9 | Bite of Lightning | 5 | 31,408 B | 4,064 B |

Clip 0 is a ~2.5 s incantation, clip 1 a ~0.3 s shout. **A short charge never
reaches the shout**: the release checks `FUN_00206a90` and drops the line rather
than queueing it, so with the incantation still running it writes 102 and says
nothing. That is the original's own branch, not a port limitation — hold
Triangle past ~2.5 s and both play.

`DAT_00356788` is modelled as a **tick countdown taken from the clip's own
length**, not as a mixer query. The original's flag is simulation state cleared
when the stream ends; reading the mixer instead would make `--frames` output
depend on whether audio was enabled. Verified: an audible run and `--no-audio`
produce byte-identical reports.

Of the 24 class-1 state handlers at `0x0031DD60`, this slice ports 101..122
except 102 and 115. `--battle-report` names any state a run
actually reached whose handler is missing.

`--hold-square 300-380` works under `--screenshot` as well as `--frames`; it did
not at first, and a capture "during a guard" was quietly of a character standing
still. Both loops now apply the same synthetic hold.

### The close-up rig, and the draw walk's deferral queue

The doorway scene swaps Orphen for a close-up model with an animating head, and
in the port the head never appeared and his bandana stayed behind in the corridor.
Three separate things, and the first one is the whole scene:

**Type `0x28` is not a character, it is a mount.** Pool slot 64 becomes one
during the scene, and `FUN_002d2f40` — its entire behaviour — builds a
three-entity rig the first time it ticks:

```c
b = FUN_00265e28(0x26);  b->+0x192 = this;  b->+0x194 =  role1(this)
c = FUN_00265e28(0x19);  c->+0x192 = b;     c->+0x194 = -role2(b)
a = FUN_00265e28(0x27);  a->+0x192 = b;     a->+0x194 =  role1(b)
this->+0x198 = a;  this->+0x19C = b;  this->+0x1A0 = c;  this->+0x94 = 1;
```

So the mount `0x28` (`grp_0021`, 39 bones) is the **torso and arms**, `0x26`
(`grp_001f`, 30 bones) is the **head**, `0x27` (`grp_0020`) is the **hair**, and
`0x19` is a bandana of its own — the same type the field player wears. All three
settled by capture rather than by model size: hiding the `0x27` slot removes the
hair and leaves a headbanded bald head, and hiding `0x26` *and* `0x27` leaves a
headless torso with a bare neck stump, so the mount carries no face of its own
and nothing needs hiding on it the way opcode `0x140` hides the field body's
head bones.

The negated bone index on the cloth is deliberate: it selects `FUN_0020cdc0`'s
middle, position-only branch instead of the rigid one, which is what lets the
rope hang rather than being welded to the bone's orientation. On the live rig the
three resolve to bone 30 of the mount, bone 1 of the body and bone -2 of the
body.

Opcode **`0x13F`** (`FUN_002604a8`) is the script's handle on it: it clears
`+0x94` to force a rebuild, calls `FUN_002d2f40`, and publishes the two pool
indices into work slots — `work[e1]` from `+0x19C` (the bust) and `work[e2]` from
`+0x198` (the hair), which is not the order it reads its operands in. It is now
modelled rather than operands-only, and it runs exactly **once**: the 173 hits it
showed while halting were the body being re-entered, not the opcode firing.

**And `FUN_0020c5a8` walks a deferral queue, not the pool in slot order.** Its
first pass queues every live slot whose `+0x02` bit `0x200` is clear and marks it
0; everything else is `0xFF`. The second pass walks that queue *while it grows*:

| parent | action |
|---|---|
| `< 0` | pose and draw, mark 1 |
| `status[parent] == 0` | push this slot on the back and move on |
| `status[parent] == 1` | pose and draw, mark 1 |
| `status[parent] == 0xFF` | neither — the slot is dropped this frame |

The port had a plain ascending walk, and the comment on the rigid-attach branch
said as much: slot order happens to be right for the head-on-neck attachments in
the opening, so it held up. `FUN_002d2f40` breaks it, because it allocates the
hair **before** the bust — so the hair takes the lower slot and was posed against
a palette its parent had not built yet. `FUN_0020dc88`'s no-palette fallback is
the parent's own `+0x20`, and for a bone-local attachment that is `(0, 0, 0)`:
the hair was at the world origin.

With the queue, the chain resolves in dependency order and the rig lands where it
belongs — mount at `(5.50, -1.20, -1.50)`, head on its bone at
`(5.518, -1.177, -0.692)`, hair on the head's at `(5.518, -1.195, -0.677)`, cloth
at `(5.515, -1.182, -0.593)`. The shot itself is Orphen leaning through the
cargo hatch at the end of the corridor with Dortin and Volcan beyond it, and the
mount stands 0.91 m to the field model's ~1.0 m: "close-up" here means a
higher-detail model with a real face, not a smaller one.

### The field model is hidden with `+0x08` bit 0, and its bandana has to go too

The bandana that stayed behind in the doorway was not the rig's. It was slot 4,
the *field* player's, and the port was drawing it for the whole cutscene after
the field model itself had gone.

The script hides Orphen at the same instant it builds the rig. Tracing the two
`0x79` register ORs that sit between opcode `0x6D` and the `0x52` that spawns the
mount:

```
[scr] f=14795 @0xa3f5 op=0x79    reg 0x3 slot 0: 0x3024 -> 0x3025
[scr] f=14795 @0xa405 op=0x79    reg 0x4 slot 0: 0x100  -> 0x101
```

Register `0x4` is entity `+0x08`, and bit 0 is `FUN_0020c5a8`'s hidden flag — not
`+0x02` bit `0x200`, which the whole scene writes exactly once and never to slot
0. (`--scr-trace-range` now prints the slot, register and before/after value under
every `0x76`..`0x7C`, which is what made this a one-line answer instead of a
guess.)

**`FUN_0020c5a8` gives slot 0 no special treatment.** It is queue entry 0 like
anything else, and its status byte is what decides whether its children draw. The
port posed the lead outside the queue — slot 4 reads slot 0's palette to find the
neck — and then pinned `drawStatus[0]` to "posed" unconditionally, so the
bandana kept drawing against a body that was no longer there. Worse, the mount
walks forward at frame 15149 and the field model does not, so the bandana ends up
hanging alone in the doorway.

Slot 0 now writes its real status, and the three branches are not the same:

| lead state | status | why |
|---|---|---|
| `+0x02 & 0x200` | `0xFF` | children dropped with it |
| `+0x08 & 1` | stays **queued** | the original's `& 1` branch never writes the byte, so children defer against it until the queue's byte counter wraps — not drawn, by a different route |
| drawn | posed | children draw |

Verified by instrumenting the publish: slot 4 reaches the view list on frames
0..14794 and never again, which is exactly the frame the OR lands on. A pre-fix
and post-fix capture of frame 15030 differ only in a red bandana strip beside
Orphen's head.

### `0x6D` hides the bandana too, and a save state proved the head was fine

A PCSX2 save state taken during the close-up settled both halves of this, and
one of them the wrong way round: **the head was never deformed.**

`--arm-stream d780:13400` reaches the same shot at frame 15810 — same camera
(`cPOS 6161,-1226,-996` against the state's `-1228`), same line, same pose. The
head model's palette is not merely close, it is exact. All 30 bones of `grp_001f`
at animation 23 / column 2, port against `0x00357E00 + 64 * 0xA80`:

```
bone  0  port( 5.500 -0.477 -0.637)  real( 5.500 -0.477 -0.637)
bone  9  port( 5.495 -0.550 -0.576)  real( 5.495 -0.550 -0.576)
bone 28  port( 5.522 -0.539 -0.549)  real( 5.522 -0.539 -0.549)
...   worst bone 9, 0.0005 m — the print's own precision
```

and the head's bone 0 equals the mount's bone 30 in *both*, which is
`FUN_0020cdc0`'s rigid branch confirmed against hardware rather than inferred.
The keyframe tables match too: animation 23 is ten entries cycling columns
2,1,3,1,4,1,5,1,6,1, and 36 is a single column-26 key, exactly as the port reads
them.

What I had been looking at was the mount's collar and hood surrounding a head
that is small and tucked into it at that angle. Masking the head's own pixels
(`--hide-slots 71,72,73` differenced against `--hide-slots 71,73`) shows a clean,
compact face. Two lessons: **a low-poly head read through an occluder is not
evidence**, and once the palette is measurably right the search should move to
what is drawn in front of it, not further into the pose pipeline.

**The real bug was the bandana, and `0x6D` is what hides it.** `DAT_0058C610`,
`DAT_0058C614` and `DAT_0058C618` are not globals of their own — `0x0058BEB0 +
4 * 0x1D8` is `0x0058C610`, so they are pool slot 4's `+0x00`, `+0x04` and
`+0x08`. With an operand below -2 `FUN_0025fd10` sets `+0x04 |= 0x4000` and
`+0x08 |= 1` on the bandana whenever the lead is the player and slot 4 is
occupied, and clears both on the `== 1` release (gated on the lead actually
sitting in state 10). The port drove only the lead's state and skipped all of it.

The save state reads slot 4 `+0x04 = 0x4019`, `+0x08 = 0x0031`; the port now
reads `0x4019` / `0x31` at the same point in the scene.

Why it looked like a face bug: the hidden lead's position decides where the
orphaned bandana hangs, and that differs between a forced `--arm-stream` trigger
and real play. The save state has the lead at `(5.140, -0.838, -1.500)` — half a
metre from the close-up head, well inside a shot whose camera sits about 1.2 m
away. Forcing the stream leaves him at `(5.500, -1.199, -1.500)`, behind the
camera, so the same build renders the same frame clean. **A cutscene reproduced
by arming its stream does not put the player where play does**, and anything
parented to the player will lie about it.

### `+0x80` is a slope limit, and the wall test was invented

Floor panel tile `0x80` arms stream `0xd5e0`, which walks a companion in on a
spline and Orphen in with `0xF0` — **subproc 3670** at `0x93e4`:

```
0xEB(0);                                    // focus pool slot 0, the lead
if (0xF0 -0.500, 2.599) { 0x77(0,8,1); 0x3E(0x190); 0x9E(-1); }
```

He stopped dead at `(-0.50, 1.73)` and flag `0x190` never set, so record
`0xd600` blocked forever. (Subproc **5112** at `0xb9b6` is running alongside and
is unrelated — a two-line `0x53`/`0xE2` pair the flood arms, re-entered every
frame by design.)

Three things were wrong and they compound:

**1. `FUN_002262c0` has no map-wall query.** Its only geometry call is
`FUN_00227390`; the four "blocker" helpers it also calls (`FUN_00228380`,
`FUN_002285d8`, `FUN_00228838`, `FUN_00228a90`) walk `DAT_0058beb0` — the entity
pool — not the map. A move into a wall is refused because the destination's
ground scan fails, and by nothing else.

The port ran an invented swept-capsule test, `queryPsm2ActiveBlockerAlong`, over
every triangle steeper than `|nz| > 0.5` whose vertical span reached more than
5 cm above the feet. `s01_e012` has a **10 cm door sill** at `y = 1.9` —
primitive 3497, a 1.0 x 0.1 strip across the doorway, part of a collision group —
and that rejected the move permanently. A player would sidestep; a cutscene's
`0xF0` cannot. It is now deleted rather than tuned: it had no `FUN_*` behind it,
and neither did its two thresholds.

**2. Entity `+0x80` is the walkable-slope limit, not a step height.** The
original reads it in exactly one place:

```c
if (fVar18 - fVar19 < DAT_00352434) {                    // step gate
  if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80)) {  // slope gate
```

`puVar11[2]` is scan-workspace `+0x08`, which `FUN_00227390` fills from `+0x54`
on the same branch that adopts a corner's terrain flags — so it is the *winning*
corner's value — and `FUN_00227840:59` fills `+0x54` from the record's stored
angle at `+0x70 + subTriangle * 4`, defaulting to `uGpffff8504` = pi/2 when
nothing is found. That is `DRecord78::slopeAngle`, which the port already
computed and never read. Type 1's descriptor `+0x10` is **0.872665 = 50 degrees**,
the same constant `FUN_0022d258` tests the same field against.

The port called the field `maxStepHeight80`, used it as a step height, and put an
invented `0.75` in it for the lead. That is what the wall test was really
covering for: with a three-quarter-unit step allowance and no slope test, the
lead could ratchet up the ship's hull plating (which samples about 60 degrees)
and walk out of the hold — reproducibly, to `z = +0.95` at `y = 4.51`. The field
is now `slopeLimit80`, seeded from the descriptor, and the gate is ported.

**3. The step height is `DAT_00352434` = 0.26**, a global, tested *strictly*
less than. Not `+0x80`, and not 0.75.

With the slope gate in and the invented test out, every stick angle keeps the
lead on the `z = -1.50` deck instead of climbing the hull, and subproc 3670
reaches `(-0.50, 2.60)` — its authored target — at frame 13627, with the rest of
the chain (`0x193`, `0x192`) following. `--frames 4000 --actor-report
--scr-report` stays byte-identical on both scenes.

**Still not ported**, and worth naming rather than inventing around: `FUN_00228cf0`
(the dynamic-support pass `FUN_00227390` runs at `LAB_002276d8`, which can raise
the answer and set `+0x0C` bit `0x100`), and the four entity-pool blocker helpers
on the *player's* path — the actor loop has its clamp, the lead does not.

### The flooding of the hold

The story chest — pool slot **78**, at `(-12.731, -1.373, -1.50)` — opens a
cutscene that floods the map and ends on the save prompt. It reaches the player
through a path worth spelling out, because it is not the chest path:

The script gives that one chest `+0x02 = 0x4100` and `+0x95 = 100` (object
registers 1 and 17, one write each in the whole scene). `0x4100` is *both*
interaction bits, and `FUN_00252828` tests the scripted one **first** — so it
runs header word 3 rather than the native chest cutscene. Header word 3 is:

```
if (current->+0x95 == 100)              // is this the story chest?
  if (!flag(0x51C) && work[13] == 0) {
    work[15]->+0x04 |= 0x4000;          // veto the scripted branch from now on
    work[13] = 28;                      // story progress
    setFlag(0x404);
    0xA1 channel 0 <- stream 0xd430;
  }
```

`+0x04` bit `0x4000` is the same bit `FUN_00252828` checks to *suppress* the
scripted branch, so the chest reverts to an ordinary chest the moment the
cutscene starts. It fires exactly once.

Stream `0xd430` is 29 records: a camera move, subproc **3205**, three flag joins,
the map swap, five dialogue lines and the save prompt. Reproduce it with

```
--scr-tick --arm-stream d300:13400 --hold-stick -1.9,1 --press-confirm <frames>
```

which walks the lead out of the re-entry cutscene to that chest.

**Subproc 3205** (`0x86fc`) is the camera hold at the front of it:

```
if (0x42(60)) { 0x45(0); 0x43(<three curve streams>); setFlag(0x12C); 0x9E(-1); }
```

Three opcodes had to be ported to get from there to the end, and each one named
the next:

- **`0x42`** is `FUN_0025dd60`, *the same handler as `0x44`*, which picks between
  two interpolators on its own opcode — `sVar1 = sGpffffbd68`, captured in its
  first instruction, before the duration is evaluated. `0x44` calls
  `FUN_00218158`; `0x42` calls `FUN_00217f38`, which is the same function minus
  the `FUN_00266988` roll/zoom sample and the two globals it publishes. So a
  `0x42` move drives the eye and the look-at and deliberately leaves the
  projection alone. The port had `0x44` and halted on `0x42`.
- **`0x89`** (`FUN_00260ce0`) drives the full-screen overlay directly. Two
  expressions, packed by the original as `expr0 | (expr1 << 24)` and handed to
  `FUN_0025d0e0` with `(char)expr1` alongside — so expr0 is the colour, expr1 is
  the alpha, and expr1 also picks the GS blend word. `FUN_0025d0e0` is the sink
  both fade ramps already feed, so this shares `ScreenFade`'s overlay rather than
  owning a second one. The flood's white-out is a body running
  `0x89 RGB(255,255,255), 255` every frame until flag `0x132` opens — a *held*
  white, not a ramp — and body `0x89d8` then arms a normal fade-in over it, which
  is what takes the overlay back down. Measured: alpha 0 → 227 → 27 → 0 across
  frames 16000..16600, and 0 for the rest of the run.
- **`0xA7`** (`FUN_00261fd8`) **is the flooding.** It walks all `iGpffffb718`
  primitive records at `iGpffffb740` — the port's `DAT_003556b0_dRecords78`, the
  same array `FUN_00227840` scans — and for every one whose `+0x04` matches a
  mask, overwrites the **top nibble** of that word:
  `rec = (rec & 0x0FFFFFFF) | (value << 28)`. `+0x04` is `terrainFlags`, which is
  both the reject mask the ground query tests and the surface *class*
  `FUN_00253080` reads out of the top nibble (`0xD` being the drift surface). So
  one opcode retags the whole map's surfaces. In `s01_e012` the lead's
  `terrainWord` goes from `0x40120006` to `0x1012000e` across the transition.

With those three the chain runs with **zero unimplemented opcodes**: 284 event
records, all seven join flags (`0x12C`, `0x12D`, `0x12E`, `0x130`..`0x133`), five
dialogue lines, and it ends on `0xE1` at `0x8c20` — the save/menu mode. There is
no save menu, so `DAT_00354d2c` is raised to `0x10`, the following `0x6D` hands
control back and play continues on the flooded map. That last part is the known
gap, not a halt.

### The lead is walked by the script too, and its request was being wiped

`s01_e012`'s re-entry cutscene -- floor panel `0x3d94`'s second branch, arming
scheduler stream `0xd300` -- softlocked forever. Three characters walk back into
the room and the stream joins on three event flags:

| body | subproc | mechanism | flag |
|---|---|---|---|
| `0x80e0` -> `0x811c` | | `0xBD` `0x70`/`0x72`, path `0x378C` | `0xFC` |
| `0x815b` -> `0x8197` | 2991 | `0xBD` `0x70`/`0x72`, path `0x3740` | `0xFD` |
| `0x8066` | 2947 | `0xEB` focus 0, then `0xF0` to (-6.600, 2.599) | `0xFB` |

`0xFC` and `0xFD` landed. `0xFB` never did, and record `0xd340` waits on it, so
everything after -- the whole rest of the scene -- never ran.

The difference between the two mechanisms is *which entity*. The path-follow
pair drives companions; subproc 2947 focuses pool slot **0**, the lead player,
and walks him in with `0xF0`. `OriginalPlayerController::update` opened by
zeroing `+0x30`/`+0x34`/`+0x38`, and `FUN_002239c8` runs `FUN_0025b778` (the
script tick) immediately *before* `FUN_00251ed8`. So the walk request was made
one statement before the player controller destroyed it. Orphen never moved,
`0xF0` never saw `distance < step`, `+0x1BC` never advanced and the flag was
never set.

**`FUN_00251ed8` does not clear those fields anywhere.** Its only write to them
is the *additive* leader-follow at its tail (`psVar8[0x18] += ...` over a short
pointer, i.e. byte `+0x30`). `FUN_002262c0`'s epilogue is the sole owner of the
clear and it clears only after spending them. `FUN_00253080`, the drift pass
`FUN_00251ed8` really does call, *assigns* `+0x30`/`+0x34` -- but only on a
`0xD`-class surface, or while airborne with residual drift; on ordinary ground
`bVar3` stays false and it leaves them alone.

This is the same bug the actor loop had (see the `FUN_00239ce0` note above); the
lead's copy of it survived that fix because nothing had yet asked the script to
move slot 0. Removing it leaves `--frames 4000 --actor-report --scr-report`
byte-identical on both `s01_e012` and `s01_e024`.

Reproducing it needs the opening out of the way first: `--arm-stream d300:13400`
lands after the handover, and flag `0xFB` now sets 109 frames later at `0x80a9`.
Armed any earlier it fails for unrelated reasons -- before the opening's `0x78`
at script `0x5f8e` clears `+0x04` bit `0x108`, physics is off on the lead
entirely, and armed at frame 1 he walks into a map blocker no opening has moved.

**Where physics actually lives, which the port does not reproduce.** No state
handler calls `FUN_002262c0`, and neither does `FUN_00239ce0`. The only caller
is `FUN_002261e0`, a single late pass that walks all 256 pool slots -- *from slot
0* -- running `FUN_00225c90` then `FUN_002262c0` on each live one whose `+0x02`
bit `0x800` is clear and whose `+0x192` is negative. `FUN_002239c8` orders it
`FUN_0025b778`, `FUN_00251ed8`, `FUN_00239ce0`, `FUN_00208450`, **`FUN_002261e0`**,
`FUN_0025b918`, `FUN_00216aa0`. The port instead runs physics inside the player
controller and again inside the actor loop, which puts both before
`FUN_00208450` rather than after it. Nothing in either scene depends on the
difference yet, but a moving door would.

### The chest cutscene

`src/ported/player/original_chest_cutscene.*` is player states `0x0C`..`0x15`,
the sequence `FUN_00252828`'s chest branch starts. `analyzed/player_states/chest_cutscene_0x0C_0x15.c`
is the reading. Six of the ten states have no `src/FUN_*.c` — nothing calls
them directly, so Ghidra never made functions at `0x002550F0`, `0x00255148`,
`0x00255260`, `0x002552B0` or `0x00255448`; they came out of the disassembly.

What runs, with the frame each transition lands on in `s01_e024`:

| state | | frame |
|---|---|---|
| `0x0C` | arm the fade to black | 61 |
| `0x0D` | on black: stand the player 0.372 in front of the chest, install the cutscene camera, fade in, enter game mode 6 | 89 |
| `0x0E` | on visible: play animation `0x57` | 111 |
| `0x0F` | on the animation's marked keyframe, **set the chest's event flag**; on its last, build the item and hand on | 365 |
| `0x10` | reveal the item entity | 366 |
| `0x11` | cross-fade the chest and player out, the item in, raise the caption | 399 |
| `0x12` | on the caption being dismissed with Cross, arm the fade to white | — |
| `0x13` | on white: release the camera, push the player clear of the chest, leave mode 6 | 394 |
| `0x14` | once grounded, arm the fade in from white | 400 |
| `0x15` | on visible, back to idle | 422 |

Those 254 frames in state `0x0F` are exactly the sum of animation `0x57`'s
twelve keyframe durations, read out of `grp_0001`. Its keyframe 1 carries
`0x100` in the trailing word, which lands in entity `+0xAA`; `FUN_00225c90`
already raised `+0x06` bit 8 on the frame a new entry is taken, so the pair is
"the lid just came up" and that is where the flag is set.

**Game mode 6 is what freezes the camera.** `DAT_00354D2C` selects the frame
loop through `PTR_FUN_00318a88`, and entry 6 (`FUN_002245d8`) runs the player,
the actors and the draw — but not the scene script and **not `FUN_00216aa0`**.
`PortRuntime::update` reproduces that by gating the script tick and the camera
update on the mode.

That alone was not enough, and the missing piece is worth recording:
`FUN_00216aa0:79` gives the whole frame away to the manual camera whenever
`cGpffffb6e1` is non-zero. Without it the follow camera runs once more on the
frame state `0x0D` installs the cutscene camera — the mode is read at the *top*
of `FUN_002239c8`, so that frame still finishes as a field frame — and
overwrites the look-at with the player's. The port now takes the same early
exit.

**And mode 6 is what turns the weather off.** Put `FUN_002245d8` next to
`FUN_00224218`, the mode-0 tail, and they are the same list of calls with one
name missing: `FUN_002192c0`. That one call is every effect pool in the game --
the smoke cloud, the dust, the rain, the haze field, the spray, the fountain,
the gather streaks, the hit sparks and six more the port has not reached -- and
each of them steps and emits its packets in a single walk. Skip the call and
the whole group stops existing for the length of the cutscene. `FUN_002d3218`'s
particles go with it, since they sit below the mode test in `FUN_002239c8` too.

That is why the retail chest sits in a black room with nothing falling past it,
and why the port's chest sat in the rain until this was ported. It is a
one-line difference in the original and a two-part one here, because the port
splits each pool's single walk into a step that fills a draw list and a publish
that spends it: the step is gated, and the six pools that publish from a draw
list have that list emptied, since "emitted nothing this frame" has to be said
out loud when the list persists between frames. The three that publish straight
from their record arrays are gated in `publishSpriteQuads` instead.

Nothing is freed. Pausing the retail game inside the cutscene and reading the
rain pool's header at `0x00355AA0` gives the same `live = 500`, gate up, that it
gives outside — the records are frozen where they were, so the rain picks up
mid-fall on the frame the mode goes home. `DAT_00354D2C` reads `6` there, which
is the other half of the confirmation.

`src/ported/render/original_screen_fade.*` is `FUN_0025d1c0` / `FUN_0025d238` /
`FUN_0025d2f8`: two blocks, a 0..0x1FE0 ramp whose top five bits are the
overlay alpha, and a 0xA0-tick hold on the out block once it is fully covered.
The first transition fades through black and the second through white.

### The camera swing

The camera does not stand still through the opening — unless the chest is
empty. The move belongs to the *chest*, not to the player's cutscene:
`FUN_002d1ea8` runs while the lid opens, and on animation 5's third keyframe
(the one carrying `0x100`) it takes the camera over, but only when
`entity[+0x130] >= 0` (there is something inside) and `cGpffffb6e1 == 0x23` (a
script camera is already installed, which is the player's cutscene having put
one there). An empty chest fails the first test, and its camera stays where
`FUN_00254db0` parked it. That is the difference between the two chest
cutscenes.

It is a three-second natural cubic spline. Three eye control points — the
current eye, then 90 and 135 degrees round the chest, closing half a unit and
rising 0.2 — one look-at at the chest's origin plus 0.3, and a parallel
(roll, zoom) curve of `1.5 -> 2.0 -> 3.0`. The zoom values go through
`FUN_00218230`, which is `log2(2x)`, and `FUN_0020bec8` raises 2 to the result
against a 3840 base: the shipped `1.0` is 7680 and the curve ends at 23040, a
3x telephoto. That is what puts the item preview up close.

`ported/camera/original_camera_path.*` is the spline pair: `FUN_00266460`'s
tridiagonal solve and `FUN_00266668`'s evaluation, which agree with the
textbook `spline`/`splint` once you read the stored coefficients as the second
derivative over six. `FUN_00266a78` and `FUN_00266738` build the curves,
`FUN_00217fe8` installs them and `FUN_00218158` samples them each frame from
the chest's own `+0x19C` timer against `0x1680` (180 frames).

`FUN_00217b88`, the interpolator `FUN_00216aa0` runs for submode `0x23`, is
dead code in the retail build: a scan of the whole text segment finds no store
of a non-zero duration to `iGpffffbb0c` or `iGpffffbb14`. The only thing that
moves a `0x23` camera is `FUN_00218158`.

`FUN_002241d8`'s `DAT_00355658 = 1.0` puts the projection back at state
`0x13`.

**Slot 0 obeys the hidden bit too.** `FUN_0020c5a8` walks all 256 slots and
skips any whose `+0x08` bit 0 is set; `0x11` raises it on the player once the
cross-fade finishes, which is what leaves the item alone on screen.
`publishSceneObjectViews` was pushing slot 0 unconditionally, so Orphen stayed
visible behind the caption. It now takes the same skip — after `attachModel`,
because the bandana reads slot 0's palette and the hidden test is a *draw*
skip, not a pose one.

### Why the room goes black

Three separate things in `FUN_002342c0`, and none of them is the fade:

1. **Every entity from pool slot 2 up is hidden.** Its tail loop raises `+0x08`
   bit 0 and `+0x04` bit 0x4000 on slots 2..255, and `FUN_0020c5a8:69` skips a
   slot whose `+0x08` bit 0 is set, raising bit 0x10 on it so the pose sampler
   knows there is no previous frame to blend out of. Slot 0 is below the loop's
   start and state `0x0D` clears both bits on the chest — so the other six
   chests, the party, the enemies and the player's own bandana (slot 4) all
   stop drawing.
2. **`DAT_00355700` is left at 3.** It spins `FUN_002340e0` until that
   function's done byte flips, and the first pass takes the darken branch.
   `FUN_00209140:91` hands `DAT_00355700` to VU1 as the cap on every map
   primitive's fade byte, against the same `0x80` = x1.0 scale the occlusion
   fade uses, so the room draws at about 2%. The port had that cap plumbed
   through `MapVisibilityInput::globalFadeCap` already and simply never had
   anything to set it.
3. **The lighting and fog are replaced**: ambient `0x404040`, light 0
   `0x808080`, fog colour `0x000000`, light direction straight down. The fog
   colour matters as much as the cap — a capped primitive is nearly
   *transparent*, not nearly black, so what the room reads as is whatever is
   behind it, and that is the fog-colour clear. Setting only the cap leaves a
   flat grey screen.

`FUN_00234400` undoes all three: its own loop restores `+0x04` and `+0x08` per
slot, and `FUN_00233eb8` puts the camera, the lights, the fog and
`DAT_00355700` back — the last from snapshot byte `+0x1DA65`.

### The item display

States `0x10` and `0x11` are the item reveal, and they run. `FUN_00254f60`'s
item branch builds a second entity in **pool slot 2** with type `id + 0x1F1`,
animation 4, positioned on the chest's role-1 bone, and `+0x04` bit `0x4000`
set — that bit is load-bearing: without it `FUN_00239ce0` dispatches the
item's *usable* handler, which for a lantern is `FUN_002d4cd8` and would light
the player and then delete itself. `0x10` reveals the item, `0x11` ramps the
chest and the player out on `+0x134` while the item ramps in, then raises the
caption and hands to `0x12`.

`+0x134` is an alpha on the GS's `0x80` = x1.0 scale where **zero means fully
opaque** — `FUN_0020c810:142` substitutes `0x80` for it. It now reaches the
renderer through `SceneObjectView::fadeLevel`.

**The item's mesh comes from `ITM.BIN`.** The `0x1F1` band's model records
(`DAT_0031A95C + typeId * 0x2C`) name a mesh that is in no scene bundle:
`FUN_00221fd8` loads those through `FUN_00221b78`, which is archive index 4,
`ITM.BIN`. `EntityModelStore` searches it after the scene and boot bundles. A
disc root without the file is not fatal — the preview reports
`has no model -- ITM.BIN missing from the disc root?` and the cutscene
continues — but with it the lantern draws. The spin is the model's own
animation 4, not a facing the code drives: nothing writes the item's `+0x5C`
after the initial copy from the chest.

`ported/resource/item_database.*` is `FUN_00228e28`'s SCR.BIN resource 1 plus
`FUN_00229688`'s lookup: dword 8 points at per-group triples, and group 0's
`[1]` and `[2]` are `u32 -> u16 -> string` chains for names and descriptions.
The strings are plain ASCII — item 64 is `Blue Lantern`, 65 is
`Purple Lantern`, which is what `s01_e024`'s chest slot 17 holds.

### The two messages

Both branches of the cutscene put a window up, and both come out of the same
place: `FUN_0025b9e8(index)` reads dword 5 of the item-database blob — SCR.BIN
resource 1 — as a table of message-stream offsets. `FUN_00254f60` picks index
0 when the chest had something in it and index 1 when it did not.

```
index 0   1B 09 05 | 14 <id> | 01
index 1   1B 09 05 | 07 | "The chest is empty." | 01
```

Four control codes between them, out of the 31-entry handler table at
`0x0031C640`:

| code | handler | what it does |
|------|---------|--------------|
| `0x1B` | `FUN_00239aa0` | set the event flag in the next **two** bytes — `0x0509`, which is what `FUN_002391d0` tests before it will draw anything |
| `0x07` | `FUN_00239368` | `FUN_00238f98`, a new line |
| `0x14` | `FUN_002397f0` | splice in an item name; **one** operand byte, which state `0x0F` patches with the chest's id through `FUN_00237ca0` |
| `0x01` | `FUN_002391d0` | raise the prompt, wait for Cross, close |

`0x1B`'s two operand bytes are the thing that makes this readable at all: read
as three separate codes, `1B 09 05` looks like a prompt-and-wait before any
text has been drawn.

The empty line lands one row lower than the item line, because it leads with
`0x07` and the item stream does not. `FUN_00238f18` clears the row counter when
the window opens, so `0x07` steps it from 0 to 1.

`player +0x19C` is the *stream pointer* in the original, not an item id — state
`0x0F` stages `FUN_0025b9e8(0)` there with the id already patched in, and state
`0x11` opens it. The port stages the message by number and reads the id back off
the chest, so `+0x19C` is only the "there is a stream" marker the completion
test reads it as. (Storing the id there, as an earlier pass did, would have sent
a chest holding item 0 down the empty path.)

**This is still not the dialogue system.** `ported/player/original_item_window.*`
reads the real streams and expands the four codes above, but the other 27 stop
the reader and get reported; there is no wrapping against `FUN_00237b38`'s
600-unit width, and the glyph list is rebuilt each frame rather than
accumulated. The real one now exists — `ported/text/original_dialogue_window.*`,
below — and this file should be folded into it. It has not been yet, because the
chest caption is the one place that needs `0x14`'s item-name splice and a real
Cross press, and the cutscene walk does neither.

The *font*, on the other hand, is the real one — see below.

### The dialogue font and the book prompt

`ported/text/original_dialogue_text.*` is the sheet, the metrics and the
prompt; `MapViewer::drawDialogueSprites` blits what it produces.

`FUN_00221fd8` binds texture `0x173` into slot `0x2E` and `0x172` into `0x2F`.
Eleven columns of 22x22 cells indexed by `character - 0x20`, continuing in the
second slot once the index runs off the bottom of the first at character
`0x99` (`FUN_00238a08:52`, whose `+ 0xE` then modulo-256 is "subtract a sheet,
keep the 14-unit remainder of the last row").

**The font is proportional and the width table is not in the executable.**
`FUN_00238c90` computes it at boot: for each cell it scans for the rightmost
column holding a texel whose palette alpha clears 100, and stores `column + 2`
(or 6 for a blank) at `0x0031C518 + character`. The port runs the same scan
over the decoded slot. It agrees with the live table in the EE dump on all 121
cells of `0x2E`. Text advances at 90% of the measured width —
`(width * 0x5A) / 100` in `FUN_00238a08`, which is `FUN_00238608`'s
`(cellWidth * 100) / 22` with the shipped 20-wide cell.

The screen units are the debug overlay's: `FUN_00207938` writes x at `<< 4` and
y at `<< 3` about the 2048-pixel GS centre, so the same 640x448 virtual screen,
and `FUN_00239020` negates y on the way in — a larger entry y is further *up*.
`FUN_00237b38` opens a window at entry `(-0x130, -0x78)`, which is screen
`(16, 344)`; glyphs add 8 to the x and the prompt adds `0x10`.

The prompt is the flipping book, not a character. `FUN_002391d0` builds it as a
sprite in slot `0x2A` (texture `0x178`), 15x15 texels drawn at 20x22, and
`FUN_00237fc0:77-95` animates it from the four-entry table at `0x0031C630` —
cells at `(96,32)`, `(96,48)`, `(112,48)`, `(112,32)`, so the frames run *round*
the 2x2 block rather than across it. The timer advances by the frame tick,
holds each frame for `0x80` and wraps past `0x200`: four frames each, sixteen
frames a cycle.

`FUN_00238608`'s other branch — characters at or above `0xFC`, which draw a
32x32 face-button icon from slot `0x2C` through the UV table at `0x0031C220` —
is documented in the header but not reachable from a caption.

### Cutscene subtitles, and why the US release has none

`ported/text/original_dialogue_window.*` is the real text engine: the 300-entry
glyph slot array, the stream walk, word wrap, the typewriter and the scroll,
ported from `FUN_00237de8`, `FUN_00238a08`, `FUN_00238f98`, `FUN_00238f18` and
`FUN_00239760`. `DialogueStream` owns it and steps it; `buildDialogueSprites`
hands what it produces to the same blit the chest caption uses.

**The subtitles are in the US data and the US code, and one four-line test
hides them.** `FUN_00238a08` opens with

```
lVar1 = FUN_00266368(0x509);
if ((lVar1 == 0) && (lVar1 = FUN_00266368(0x50a), lVar1 == 0)) return;
```

and `FUN_002391d0` gates the prompt the same way. Nothing in `SLUS_200.11` ever
sets `0x50A`. `FUN_00237b38` *clears* `0x509` on every start, and the only code
that sets it is `FUN_002452f0`, an unrelated full-screen caption. So a stream is
visible only if it turns the flag on itself, with control code `1B 09 05` —
which is exactly how the chest windows in `SCR.BIN` resource 1 begin, and it is
why those captions were already on screen.

`scr2.out`'s 84 cutscene records do not. Their six `0x1B` codes set `0x6A`,
`0x79` and `0x6E`, all scheduler gates; not one sets `0x509`. The Japanese
build's glyph enqueue, `src-jp/FUN_0023ade8.c`, is the same function with those
four lines absent — the text was cut for the US release by adding a test, not by
editing the scripts.

**The port drops the gate.** That is the only deliberate divergence in the file.

The codes `s01_e012` actually uses are `0x00 0x01 0x02 0x07 0x0C 0x13 0x16 0x17
0x18 0x1A 0x1B` — a small set, all handled. Anything else is stepped over by its
operand width and named in the report.

Layout comes straight out of `FUN_00237b38`: a window at entry `(-0x130, -0x78)`
= screen `(16, 344)`, 600 units wide, `uGpffffbce0 = 2` rows deep. `0x13` clears
the array, draws the name in `0x80606000` — dark cyan, R=0 — on row 0, then
steps to row 1, and from there every glyph is indented ten units. Past row 2
`FUN_00238f98` scrolls: each slot's row index drops by one, the one leaving row 1
is retired, the rest move 22 units up. Row 0 is never touched, which is what
holds the name still under a scrolling line.

Two divergences the port's structure forces, both in the header:

- The walk **skips** `0x16`/`0x17`/`0x18`/`0x19` by width instead of running
  them, because `DialogueStream` already read the clip out of `VOICE.BIN` when
  the record opened. `0x1A` does block, on that clip's remaining length rather
  than on `DAT_00356788`.
- `0x01` raises the book prompt and, in the original, holds for Cross — 28 of
  the 84 records use it. The port spawns the sprite and lets the record close on
  its clip, taking the Cross press for granted (which is `FUN_00237fc0:108-115`,
  flags and all — see below). A cutscene that stopped for input every fourth
  line would not be the same scene, and the port has no input model for it.

**Making `0x1A` block is what fixed the record tails.** A record's hold is not
its clip; it is where its *bytes* end, and a record can put codes after the
`0x1A`. Dortin's is `... 1A 0C 3C 02` — a full second of held text after the
audio stops. Closing on the clip alone dropped that second. With the walk
driving the close instead, every record picks up at least the two steps the walk
needs to consume its `0x1A` and then its terminator, and Dortin's picks up 65.
Across the whole scene that is 264 frames of held text, and flag `0x515` — the
handoff to player control — moves from 13122 to **13317**:

```
dialogue lines: 42  (38 timed by their voice clip, 0 estimated, 4 empty)
  38 held open past the clip until the walk reached the terminator, 264 frames in total
```

Checkable at any frame:

```
orphen_port --disc-root . --scene s01_e012 --screenshot shot.png:700
```

### A record does not close the window, and that is where the speaker lives

Five of `s01_e012`'s records carry no `0x13` at all -- "Hey... Volcan... You
notice anything peculiar?", both of Volcan's `[vomits]`, and two of Sephy's --
and the game keeps the previous speaker's name on screen through every one of
them. The port drew them with no name.

The name survives because **the window does not come down between records.** A
record ends on control code `0x00`, which is `FUN_00239178`; at the outermost
nesting level it raises flag `0x8FE` and gate bit `0x2000` and returns, leaving
`pcGpffffaec0` pointing at the terminator. So when the next record opens,
`FUN_00237b38`'s `bVar1` -- the test on that pointer, taken before the
assignment -- is false, and the whole reset block is skipped: no
`FUN_00238f18`, no origin, no colour, no pen or line. The new text carries on
into a window that still holds the old one, `FUN_00238f98`'s scroll ages the
body out from under it, and row 0 is the row the scroll never touches.

There are exactly three ways out, and they are not interchangeable:

| | window | slots |
|---|---|---|
| `0x00` — `FUN_00239178` | stays up | kept |
| `0x01` + Cross — `FUN_00237fc0:108-115` | down | **kept** |
| `0x02` — `LAB_00239328` | down | cleared |

`LAB_00239328` is the only one that wipes the slot array, and only because it
nulls `pcGpffffaec0` *before* calling `FUN_00237b38(0)`, which flips that same
`bVar1` test the other way. An explicit `FUN_00237b38(0)` from the script does
not clear either -- the next open does. This scene closes the window between
each group of same-speaker lines with a record whose entire body is a bare
`0x02`; there are four of them, and the port already logged them as empty lines.

The flags differ too, and the difference is load-bearing: `0x00` raises only
`0x8FE`, while both closes also raise `0x8FF` and gate bit `0x4000`. The port
used to raise all of them at every record end. Record 207 of the scheduler
stream gates on `0x8FF` and is the handoff that sets flag `0x515`, so raising
it early is harmless but raising it *never* stalls the chain -- which is what
happened when `0x00` was first split out and the `0x01` prompt path had not
been. `0x01` is that path: the original's Cross press nulls the pointer and
raises `0x8FF`/`0x8FE`/`0x6000` without touching the slots, and the port stands
in for the press immediately, exactly as it always has.

None of this moves a single frame. The full run is byte-identical: 42 lines on
the same frames, 208 event records, flag `0x515` still at 13317.

For the camera half of `FUN_00234400`, `FUN_00217d70`'s own save/restore pair
covers it.

Checkable without a window, and `--press-confirm` now works under
`--screenshot` too:

```
orphen_port --disc-root . --scene s01_e024 --frames 620 \
    --spawn -4.5,-10.5,0 --press-confirm 60,440 --actor-report
```

That spawns beside chest slot 17, facing it, and the report ends with that
chest on animation 6 and every other one still on 4.

**Both presses matter.** The first opens the chest; the second dismisses the
item caption, which state `0x12` waits on. With only the first, the cutscene
parks at `0x12` forever and the last four state changes never happen — that
looks exactly like a regression and is not one. The ten transitions land on
frames 61, 89, 111, 365, 366, 399, 441, 469, 475 and 497.

### The cinematic bars, and where they put the subtitles

`ported/render/original_letterbox.*` is `FUN_0025cfb8`, which `FUN_0025b778`
runs at the end of every script tick. Two flat black sprites, full width, one
against the top edge and one against the bottom -- so the bars are **part of the
game's picture**, not a border drawn around it.

Two globals, and opcode `0x6D` (`FUN_0025fd10`) is the only writer of either:
`DAT_00355054` is the mode and `DAT_00355CFC` is a `0..0x780` ramp. An operand
of `-1` raises the mode and starts the ramp at zero, so the bars slide in; `-2`
starts it full, so they are already there; `1` runs the ramp back down and
clears the mode at the bottom. The ramp steps by `DAT_003555bc * 8`, which at
the nominal `0x20` ticks is 7.5 frames end to end. The drawn height is
`ramp >> 5`, so 60 units of the 640x448 virtual screen -- 30 of the field's 224
scanlines -- leaving a 328-unit picture between them. `s01_e012` arms them with
`-1` on frame 1.

**The subtitles move for them.** `FUN_00238a08:36-45` reads the same mode as it
enqueues each glyph and nudges the whole window clear of whichever bar it would
run into: the bottom window (origin y `-0x78`) rises `0x1E`, which puts its
third line's bottom edge at 380 against the bar's 388, and the top window
(origin y `0xD0`) drops `0x2D` for the same reason at the other end. The port
had the arithmetic already and never had anything to drive it, so a cutscene
line was drawn flush against where the bar belongs. It is a *live* read, not
something latched when the record opened, which is why `setMovieMode` is
published every frame immediately before `FUN_00237fc0`'s walk.

**Draw order comes out of the GS sort buckets.** `FUN_00207938` head-inserts
into one of 0x1010 buckets that `FUN_00200c48` chains in ascending order, so a
higher bucket draws later and, within one bucket, a *later* submission draws
*earlier*. The bars are bucket `0x1007` -- the same one the full-screen fade
uses (`FUN_0025d0e0` calls `FUN_00207de8(0x1007)`) -- and both `FUN_002239c8`
and `FUN_00224320` submit the fade first, so the bars go down first and the fade
tints them. Every text overlay is bucket `0x1009`: the dialogue glyphs
(`FUN_00237b38` seeds each slot's word 1 with `-0x1009`) and the debug text
(`FUN_00268410` passes the same), so both draw over the bars. `render()` is in
that order.

**One screen for all three.** `MapViewer::originalScreenFit` maps the 640x448
virtual screen onto the game's 4:3 box, and the bars, the subtitles and the
ported debug overlay all go through it. The two text overlays used to fit that
screen *uniformly* into the whole window instead, which is a different rectangle
-- wider than the picture on a 16:9 window, and vertically offset from it on a
4:3 one -- and once the bars exist the disagreement is the difference between
text that clears them and text that does not. The scale is deliberately not
uniform: the field was 640x224 and the display stretched it back to 4:3, which
is where the half-height y unit came from in the first place.

### The bandana

Orphen's bandana is not part of his mesh. It is a **separate entity** -- type
`0x19`, model `grp_001E` -- in reserved pool slot 4, attached to the player's
neck bone and simulated as two nine-link ropes. Three original functions, all
ported: `FUN_00251e40` creates it (and only when the lead player is type 1),
`FUN_00213720` is the simulation, and `FUN_00213640` releases its bones.
`analyzed/actor_behaviors/type_0x19_player_bandana.c` is the full reading.

Nothing calls `FUN_00213720` by name. It is `PTR_FUN_0031c6c0[0x19 - 1]`, which
is what identifies it as a behavior rather than a helper -- every neighbouring
entry in that table is the generic no-op or the party-member shell.

Three things the port did not have before:

- **`FUN_0020dd78`, the bone role lookup.** The high byte of a PSC3 submesh's
  `+0x0A` carries a semantic role in its low nibble, and native code finds bones
  by scanning for one. Role 7 is the neck; role 4 is the hand a weapon goes in.
  The docs previously said the game never reads `+0x0A` at all.
- **`FUN_0020cdc0`'s attached branch.** An entity with `+0x192 >= 0` and a
  *negative* `+0x194` rides that bone's position and keeps its own facing, and
  its `+0x20..+0x28` is a bone-local offset rather than a world position. That
  last part is why `SceneObjectView` now carries `worldOrigin` separately: the
  depth sort and the debug box both need a world point, and the entity's own
  position fields are not one.
- **Only `-1` skips a draw pass, and an untextured pass uses a different colour
  scale.** The port was dropping every negative subdraw index; `FUN_00212058:106`
  tests for exactly -1, and `FUN_002129b8` masks bit 15 off anything else and
  draws the pass untextured with the remainder as a colour index. All 20 of
  `grp_001E`'s primitives are that case, which is why the bandana was invisible
  even once it was being simulated -- and 26 passes on `grp_0001` and 43 on
  `grp_0009` were being dropped too. Such a pass never sets TME, so its colour
  goes straight to the framebuffer over 0..255 rather than through the GS's
  `(Ct * Cv) >> 7` where `0x80` means x1.0. `grp_001E`'s one colour entry is
  `(191, 0, 0)`: nonsense as a modulator (x1.49, saturating to pure red) and
  exactly right as a colour, `0xBF`. Divided by 128 the bandana rendered about
  twice as bright as the emulator and lost its shading entirely.

**`FUN_00305218` is `sinf` and `FUN_00305130` is `cosf`**, not the reverse --
0x00305218's small-|x| path calls `__kernel_sin(x, 0, 0)` and its `n&3` switch is
fdlibm's sine. Two older files under `analyzed/` label them backwards. It matters:
`FUN_00213720` stores sine at scratch `+0x15` and cosine at `+0x16`, and swapping
them rotates the tail frame a quarter turn, which puts `DAT_003151a0`'s body
clamp -- up to 0.067, six times the 0.011 tail spread -- on the sideways axis. The
tails then drift into the neck instead of trailing.

The resting state is checkable against `s01_e24.bin`, which has slot 4 live:
the anchor sits at (-3.31224, -12.75, 0.93703) with the player at
(-3.25, -12.75, 0), each of the nine links is exactly 0.025 long, and the two
chains hold gravity 0.018 and 0.010 -- both from the `{0.006, 0.010, 0.014,
0.018}` set the sim re-rolls into every 64th frame. Solving the dump's bone
matrices back for each override translation gives `(±0.011, DAT_003151a0[i],
-0.025 * i)` -- the spread in the first slot, the body clamp in the second, which
is the pair the sine/cosine identity gets wrong. `--actor-report` prints the
port's own anchor, tip, span and tip-bone translation beside slot 4.

**Actor behavior dispatches twice.** `FUN_0025ab68` and `FUN_002cd0a0` are shells
that index a per-type state table with `+0x60`; those tables are read out of the
executable too. `--actor-report` now shows the second dispatch, because a type can
be dispatched and still do nothing when the *state* it is in has no port:

```
type=0x3  state=0 -> 0x25abb8 ticks=120  implemented   (jr ra; nop -- a real no-op)
type=0x62 state=0 -> 0x2cd210 ticks=1    implemented   (init; hands to state 3)
type=0x62 state=3 -> 0x2cd3a0 ticks=119  UNIMPLEMENTED (hover and chase)
```

So one unimplemented behavior type is left (`0x272`, the streamed prop) and one
unimplemented state. **Non-player physics is still absent**, which is why the
enemy's chase state is not ported yet: it writes movement into `+0x30`/`+0x34`
and a hover height into `+0x38`, and none of that is integrated for slots
1..255, so porting it would look like nothing happened.

`integrateNonPlayerMovement` stands in for the vertical part of it, and it will
only *raise* an actor that asked to move this frame. `FUN_002262c0` raises
`+0x28` in exactly one branch, gated on the cached primitive at `+0x0A` being
valid and carrying the same material as the one just sampled — an actor walks up
a ramp it was already standing on, it is not teleported onto whatever is
overhead. The port models neither `+0x0A` nor the material table, and an actor
whose behavior moved it is the nearest test it has. A script-placed cutscene
actor never qualifies, so its authored height survives, which is the point:
`s01_e012` writes its cast onto the deck and an ungated snap lifted them off it.

## The field menu, and the two game modes it runs in

Up or Down on the D-pad in a field scene puts up a seven-item panel and freezes
everything behind it. `FUN_00224FF0:96` is the whole of the trigger:

```
if ((uGpffffb686 & 0x5000) != 0) { iGpffffadbc = 4; FUN_00231A98(); return 0; }
```

`0x5000` is Up or Down newly pressed, `iGpffffadbc` is `DAT_00354D2C` -- the
frame mode -- and `FUN_00231A98` lays the panel out. `FUN_002239C8:126` tests
that word at the top of every frame and hands the whole frame to
`PTR_FUN_00318A88[mode]` whenever it is non-zero, so the switch takes effect on
the frame *after* the press.

**Neither mode-4 nor mode-5 has a Ghidra function.** Both are `LAB_` blocks, so
the two handlers were read out of `SLUS_200.11`:

| mode | handler | what it runs |
|---|---|---|
| 4 | `0x00224570` | wait for release, then `FUN_00231C50` + the draw tail |
| 5 | `0x00224518` | `FUN_00231958` (navigate, then draw) + the same tail |

Mode 4 is `lhu v0,-0x497c(gp); andi v0,v0,0xf060; bne v0,zero,+3; li v0,5;
sw v0,-0x5244(gp)` -- while any of the four directions, Circle or Cross is still
*held*, stay in 4. It is the swallow of the press that opened the panel; without
it the Up that opened it would immediately walk the selection.

The tail both share is `FUN_00225C20, FUN_00208450, FUN_00208EE8, FUN_00208F28,
FUN_0020C5A8, FUN_0020F3E0, FUN_002192C0, FUN_0020C290` -- the draw half of the
field frame plus the effect-pool step, and nothing else. No script tick, no
player controller, no actor loop, no `FUN_002261E0` physics, no `FUN_00216AA0`
camera, no `FUN_00237FC0`. **The game is frozen, not slowed**, down to the idle
animation: ninety stepped frames on hardware with the panel up produced a
byte-identical screenshot, and the port now does the same. `FUN_002192C0` is the
one exception -- rain outside a window goes on falling.

### The panel

Seven labels, SCR.BIN resource 1 messages `0x3F`..`0x45`, which are plain
NUL-terminated ASCII rather than a dialogue stream:

```
0  Button Configuration          4  Return to Title Screen
1  Screen Ratio                  5  Item
2  Analog Controller Vibration   6  Equip
```

Each is centred at entry y `0x70 - index * 0x1E` with a bar two units below it,
the widest label plus `0x20` wide for all seven. `FUN_002318C0` walks the alpha
byte of each label's colour word towards `0x80` for the selected item and `0x20`
for the rest, by `frameTicks / 8` a frame, so the highlight fades in over 24
frames rather than snapping.

The bar is one `FUN_00239020` entry, at `0x0031C388`: a 128x20 texel block of
the button-icon sheet, slot `0x2C` read through CLUT bank 6. **Its `+0x2C` is
0** where every glyph entry `FUN_00238608` builds carries 1, and `FUN_00207938`
turns that field into `PRIM.ABE` -- which is why the bars are opaque and the
text over them is not. `DialogueSprite` carries a `blendMode` for it now.

Draw order is the display list's. `FUN_00207938` pushes each entry onto the head
of its bucket, so the last submitted draws first; `FUN_00231C50` submits text
then bar per item, top item first, and the panel therefore paints caption, bar 6,
text 6, bar 5, ... -- each bar behind its own label.

Navigation is `FUN_00231958`: Triangle closes through `FUN_002241D8` (which also
wipes the 64-entry action ring, so the Triangle cannot be spent on the way out),
Cross runs `PTR_FUN_0031C3C0[selected]` if the item's availability bit is set,
and any of the four directions through `FUN_0023B9F8(0xF000, 1)` moves the
selection and plays the move cue -- so **Left and Right click without moving**.
The repeat ladder is that helper's: the first press fires, then nothing for
eleven frames, then every fourth.

**None of the seven submenus is ported.** Cross on an available item logs the
selection and leaves the panel up.

### On the keyboard

The D-pad is the **arrow keys**, not WASD. See the battle-target-cycling note
for why: the D-pad does not walk the character on hardware, and sharing the
nibble with WASD made every step forward open the menu. `[` and `]` cycle the
map, which is what Left and Right arrow used to do.

## The area map, and why it is the level

**Left** on the D-pad puts up a free-orbiting 3D view of the scene you are
standing in. It is not a separate map asset and not a separate scene: it is the
same PSM2 geometry, the same entity pool and the same projection matrix, handed
a different view matrix and a different set of lighting globals.

`FUN_00224FF0:103` is the gate, three lines below the Up/Down one that opens the
field menu and behind all the same guards. `uGpffffb686 & 0x8000` tests event
flag `0x512`; with it set, `FUN_00213EF0` runs and `iGpffffadbc` goes to **12**.
Without it the press does nothing at all -- there is not even a refusal sound.

**Flag `0x512` is per-scene, not story progress.** `FUN_0022A418:107` rewrites it
on every scene load from the scene descriptor's halfword `+0x0C`: bit `0x8000`
clear sets it, set clears it. The port had never ported those six lines, so the
flag sat at 0 and Left did nothing in every scene. (The battle scenes and the
title stage carry the bit.)

### Mode 12 is five calls

`PTR_FUN_00318A88[0xC]` is `FUN_00224418`, and its whole body is

```
FUN_00225C20, FUN_00208450, FUN_00208EE8, FUN_00208F28, FUN_0020C5A8
if (DAT_003555F6 & 0x8040) { FUN_002241D8(); FUN_002141D8(); }
FUN_00238608(0x138 - width, -0x60, FUN_0025B9E8(0x30), 0x80808080, 0x14, 0x16)
```

That is less than the field menu's modes 4 and 5 run. Those end
`... FUN_0020F3E0, FUN_002192C0, FUN_0020C290`; mode 12 has none of the three,
so the effect pools stop and the fog backdrop is gone. `FUN_002000C0` drops four
more passes on `DAT_00354D2C == 0xC` -- the frame feedback blur, the fog,
`FUN_00210CA0` and `FUN_00202FF8` -- and skips the `FUN_002020A8` overlays.

The one thing still stepping is `FUN_00225C90(0x58BEB0)`, the marker's own
animation, called from inside `FUN_00214300` rather than from an actor loop.

`FUN_00208EE8` is where the fork lives: `iGpffffadbc == 0xC` sends it to
`FUN_00214300` instead of `FUN_0020BEC8`. Everything downstream -- the
visibility walk, the depth sort keys, the entity draw -- takes the matrix
without knowing which built it.

### FUN_00213EF0 is a save/restore stack

`FUN_00213E68` pushes a memory region onto a scratch stack at `0x01949A10` and
`FUN_00213EA8` pops it, and `FUN_002141D8` pops exactly what `FUN_00213EF0`
pushed. What it swaps out, and what for:

| region | replaced with |
| --- | --- |
| `0x00343888` +0x140 | every one of the sixteen point-light radii zeroed |
| `0x0035566C` / `70` | ambient `0x404040`, key light `0xFFFFFF` |
| `0x003439C8` +0x0C | the turning key light, below |
| `0x00343A08` +0x0C | `DAT_00343A10` = -1000.0, the literal `FUN_002000C0:216` reads as "skip the fog pass" |
| `0x00355674` / `78` | both 0 -- this is what makes the void around the level black |
| `0x0035567C` / `80` | 500.0 and 600.0 |
| `0x00355700` | 0, no global fade cap |
| `0x00355628` / `2C` | 500.0 -- **this is what lets the whole level into the frustum at once** |
| `0x005A96B0` +0x100 | the slot mask, below |
| `0x0058BEB0` +0x1D8 | slot 0, respawned as the marker |
| `0x003FFE00` +0x540 | slot 0's pose filter state, bones 0..20 -- without it the lead smooths back out of the marker's pose after closing, sunk into the floor |

The slot mask is `FUN_00213EF0:56-72`, over slots 1..255: a slot survives only
if `+0x08` has bit `0x2000` clear *and* its type is at or above `0x272`, with a
type `0x38` reading its real type out of `+0x1CE` first. On the entry-state
scene that leaves two slots live -- 0, the marker, and 10, a type `0x272`
treasure chest. The compass roses on screen are map mesh, not entities.

Slot 0 is then `FUN_00229C40(0x58BEB0, 0x256)`, respawned in place as the marker
actor, with `0x4119` ORed into its `+0x04`. Four fields are read out before the
respawn and written back after: `+0x20`, `+0x24`, `+0x28` and the ground height
at `+0x4C`. **The facing is not among them** -- Ghidra's decompile shows only
two of the four, the disassembly at `0x00214154` has all of them.
`FUN_00214300` then rescales the marker to `2.0 / zoom` every frame, which is
why the arrow holds its size on screen however far the view pulls back.

The gate runs before the actor loop in the same frame, so the frame that opens
the map already walks the masked pool -- the chest ticks once more than every
other actor across an open/close cycle, and that asymmetry is the original's.

### The camera

`FUN_00214300` is an orbit rig. It composes

```
translate(-focus) * rotZ(yaw + pi/2) * rotX(-pi/2 - pitch)
  * scale(-zoom, zoom, -zoom) * translate(0, 0, 5)
```

against `FUN_0020BEC8`'s `translate(-eye) * rotZ(yaw + pi/2) *
rotX(-pi/2 - pitch) * rotZ(roll) * scale(-1, 1, -1)`. So the zoom is not a
projection change: the world is scaled about the focus and then pushed five
units down the view axis. The projection itself is the field camera's, constant
for constant -- both calls to `FUN_0020BD58` pass 7680, 0.45, 32768, 65534, 1.0,
0.3 and a stack far plane of 128.0, just out of different constant blocks.

The focus starts at the player's position, the pitch at -1.22173 (-70 degrees)
and the zoom at 0.3, both from `0x003520F8`. The yaw starts at the *field*
camera's, so the map opens facing the way you were looking.

| input | effect |
| --- | --- |
| left stick | pan the focus, heading `stickAngle + yaw - pi/2`, speed divided by the zoom |
| right stick horizontal | zoom, x1.04 or /1.04 a frame, clamped to 0.04..2.0 |
| right stick vertical | tilt, clamped to -90..-5 degrees |
| L1 / R1 | turn, 0.0015625 rad a tick -- 0.05 a frame at the nominal 32 |
| Cross or Left | exit |

The right stick's four jobs are angular sectors, not axes: within 35 degrees of
+X zooms in, beyond 145 degrees zooms out, and 65 to 125 degrees either side
tilts. Neither zoom step is scaled by the frame tick. The tilt and the pan both
are.

The key light is the odd one out. `DAT_00354C60` advances one degree every
frame, unscaled by the tick, and the light direction is
`normalise(cos, sin, -0.8)` of it -- so the level is lit by a sun that circles
it once every six seconds. It is what makes an otherwise flat overhead view
readable, and it is the only thing in mode 12 besides the marker's animation
that changes on its own.

Verified against hardware: three frames of full-up on the camera stick moved
`DAT_00355A54` from -1.22173023 to -1.4137254, and the port's
`128 * sin(1.578148) * 0.0005 * 32 * 0.03125` per frame predicts -1.4137250 --
seven significant figures. Thirty-seven frames of full-right took the zoom from
0.3 to 1.2803, which is `0.3 * 1.04^37`.

### The marker's colour is a CLUT bank

The marker model is `grp_0066` on `tex_0171`, bound to slot 40, and every one
of its fifteen passes is `bound+7`: primitive flag `0x800` set, subdraw
selector 7. `FUN_00212058:197` turns that into packet byte 5 = `selector + 1`,
the same bank-plus-one convention as `FUN_00207DE8`, so the pass reads slot 40
**4-bit with CSA 7** -- a GS dump of the map screen shows exactly that, `PSM
0x14 CSA 7` on `tbp 14112`. Bank 7 of that sheet is a red ramp.

The port used to draw every model pass from the 8-bit page, which is right for
slots below `0x18` and never right above it: `FUN_002103D0` uploads those slots
PSMT4, so there is no 8-bit page on the GS to read. Read 8-bit, the arrow came
out cream. The model path now reads a slot at or above `0x18` through its bank:
the selector with `0x800` set, CSA 0 otherwise.

The arrow's brightness swings from dark red to pink as the key light circles --
on hardware too, where three captures 90 frames apart measured (77,27,17),
(251,158,141) and (78,27,17). Compare at matching light bearings, not at
matching frame numbers.

### On the keyboard

WASD pans, `J` and `L` are L1/R1 and turn, and the camera stick is the four keys
framing them: `U` and `O` zoom, `I` and `K` tilt. Return is Cross and closes it,
as does Left arrow.

## The field HP gauge, and the hit reaction under it

The orb in the top left corner of a field scene is pool slot 8, entity type
`0x58`, and it is an entity like any other. `FUN_0022A418:388` builds it on
every scene load that is not a battle section, not the title stage and not the
arena mode, and parks it at screen `(48, 32)` with `DAT_003524E0` — 65534.0 —
in `+0x28`. Its descriptor at `0x003196E4` carries `+0x04 = 0x0600` and
`+0x16 = 0x5040`:

| bit | what it does |
| --- | --- |
| `+0x02` `0x200` | `FUN_0020C5A8` refuses it, so `FUN_0020F3E0`'s billboard pass takes it instead |
| `+0x08` `0x1000` | that pass's screen-space branch: `+0x20`/`+0x24` are already pixels and `+0x28` is a GS depth word |

So nothing new draws it. It is a sprite strip — model record `0x57`, `grp_398`,
`tex_374` statically bound to texture slot `0x2C` — and the existing sprite pass
renders it exactly as it renders the two type `0x68` battle bars.

`FUN_002D0EA8` is the behaviour, and its tail is the whole readout:

```
+0xA0 = 30 - min(30, (lead +0x12A * 30) / lead +0x128)
```

31 animations, each an eight-frame loop of eight columns. Animation 0 is a full
orb and 30 an empty one, so the bar is spelled as an animation id rather than as
geometry — the same trick the battle bar plays with its five pips.

**It is on screen essentially all the time.** The gate at the top hides it
(`+0x08` bit 0) for a game mode other than 0, for letterbox bars, while a scene
change is pending, for a lead that is itself hidden, for a lead type at or above
`0x3A`, and for event flag `0x50B`. Past the gate the function never raises the
bit again: the lead's state-0 branch clears it once `+0x98` has counted up to
`0xF00`, and both item-pickup callers slam `+0x98` to `0xF00` and clear it
outright. The one thing that does raise it is `FUN_00234468`, the menu, and that
is a game-mode change anyway. So in a cutscene the gauge goes down at the first
gate failure and stays down, because a cutscene parks the lead in state 10 and
the "hold down" branch has no way back up — which is why `s01_e013` runs its
whole animatic without one.

### The alpha test the sprite pass never had

The gauge draws four records: a green liquid sphere, a black drain cap that
grows down from the top as the level falls, an ornate ring, and a glass
highlight. Stacked back to front that is a liquid level you can read at a
glance.

It came out as a plain brown box. The ring is record 1, 62x47, **blend mode 0**,
with an alpha-0 hole in the middle for the orb to show through — and
`MapViewer::drawSpriteQuads` had no `GL_ALPHA_TEST`. A blended record hides its
transparent texels through the blend, which is why no effect had ever noticed;
a mode-0 record does not, so the ring painted its own hole opaque over the two
records beneath it. The GS runs ATST GREATER for the whole frame (see
`docs/map_cutout_alpha_test.md`), and this pass was the last one not modelling
it. One `glEnable(GL_ALPHA_TEST)` and the gauge has a fill level again.

### Damage reaches the lead through `FUN_00251ED8`, not through an enemy

`FUN_00251ED8:97-227` is the lead's own `+0xBE` drain — the counterpart of the
block every enemy type wrapper opens with, and the only place the field player's
hit points move. It runs ahead of the state dispatch and can replace the state
outright, so a hit taken mid-swing ends the swing. Ported with it:

| state | handler | what it is |
| --- | --- | --- |
| `0x16` / anim `0x1F` | `FUN_002554D8` | the stagger: drift backwards until the animation ends |
| `0x17` / anim `0x22` | `FUN_002555A8` | the flatten: `+0x150` and `+0x58` to zero for `+0xC0` frames, parked in `+0x1A4`/`+0x1A8` |
| `0x18` / anim `0x20` | `FUN_002555D8` | the knockback: flying, down (`0x21`), getting up (`0x23`) |
| `0x19` / anim `0x0D` | `FUN_002557A0` | **not** death: the terrain hazard, sink the body and respawn it |
| `0x1A` | `FUN_00255820` | the game over, staged once |
| `0x1B` | `FUN_002559E8` | the game over, held |

`+0xC0` is the reaction's length. `FUN_00251ED8` reads it as a frame count,
copies it into `+0x62` and then multiplies it by 32 on the way out, so from the
next frame it is a tick count like every other countdown in the engine.

The red flash on a hit is real and is now reproduced: `FUN_00266008` takes a
`DAT_00343888` slot from 3 upward, writes `(255, 0, 0)` with radius 1.0 and
carries it on the body for fifteen frames. Slots 3 and above are the flat-tint
band, so the whole character goes red for a quarter of a second.

Left out, and named rather than silent: `FUN_00257B00`'s pad rumble (the port
has no rumble path), `FUN_00205938(7, 0x2F, 0)`'s death sting (no SND table
entry for it), `FUN_00255E40`'s respawn (it walks `DAT_00355704`, the lead
trail, for a primitive carrying neither `0x0800000` nor `0x1000000`), and
`FUN_00251ED8:104-110`, which detaches the object a state-9 carry is holding —
the port has no state 9.

### Dying is a knockback, and then the room goes out

The port used to run the death through state `0x19` and stop: the body faded
out, `FUN_002557A0` reached its respawn call with nothing installed, and the
frame never moved again. That state is the wrong one.

`FUN_00251ED8`'s death branch ends on `FUN_00225bf0(entity, 0x18, 0x20)` — the
**knockback**, with `uGpffff88c0` of horizontal speed and `uGpffff88c8` of
pop-up. Dying looks like being hit very hard: the body tumbles backwards through
the air, lands, and does not get up. `FUN_002555D8` is what notices, when
animation `0x21`'s `+0x62` frames run out with `+0x12A` at zero, and it writes
state `0x1A`.

State `0x19` is the **terrain hazard**: `FUN_00251ED8:60-71` writes it when the
surface under the player carries `0x1000000` — lava, or a hole — and
`FUN_002557A0` sinks the body and hands it to `FUN_00255E40`, the respawn that
puts the player back on the lead trail with one hit point. The entry test is not
ported, so nothing reaches that handler yet; it is kept because it is correct
for what it actually is.

Two more things the death branch had wrong, both from the same misreading:
`psVar8` is a `short *`, so `psVar8[2] |= 0x11` is **+0x04**, not +0x02 — and
`+0x134` goes to **zero**, not `0x7C`, because `FUN_002555D8` ramps it back up
two a frame while the body is in the air.

### The game over: `FUN_00255820` and `FUN_002559E8`

`FUN_00255820` (state `0x1A`) runs once and leaves the entity in `0x1B`. It
stages everything: a white light on the body out of the high half of
`DAT_00343888`, pool slots 4 and 7 released, `FUN_002D36F8`'s spark pool
installed on `DAT_00355620`, every weather pool stopped, the scene's lighting
replaced with ambient `0x202020` and light 0 `0x808080` pointing straight down,
the manual camera dropped and the follow distance pushed to 3.2, and all seven
sound channels ramped down.

`FUN_002559E8` (state `0x1B`) then runs three ramps at once over about a second:

| field | from → to | what it drives |
| --- | --- | --- |
| `+0x62` | 0 → `0x2000` at 4x ticks | `/32` is the body light's brightness **and** `FUN_00255CE8`'s black quad alpha |
| `DAT_00355674` | × `(255 - level)/256` a frame | the fog colour, and with it the backdrop |
| `+0x1B6` | `0xFE0` → `0x60` at 2x ticks | `/32` goes to `DAT_00355700` and to every entity's `+0x134` from slot 10 up |

**The room does not fade out — it stops being drawn.** `FUN_00209140:127` guards
the entire map primitive walk with `cap == 0 || cap > 3`, and `FUN_002559E8`
parks `DAT_00355700` at exactly 3. That is one branch, before the loop, and it
was the missing piece: the port modelled the cap as an alpha and drew the room
at 3/127 instead of not at all. Confirmed on hardware by writing `0x7F` back
into `DAT_00355700` mid-sequence, which brings the whole room straight back
while the quad, the fog and the lights stay exactly where they are.

That also settles where the quad goes. `FUN_00255CE8` submits into GS sort
bucket **2**, not the fade's `0x1007`: with the alpha saturated at `0xFF` and
the cap forced back up, the room is still visible, so the quad is under the map
and under every entity. All it can cover is the backdrop, and that is where the
port draws it.

`FUN_002D3320`, the spark column, is the other half of the picture. It is the
second behaviour the `DAT_00355620` pool can carry and it has exactly one
caller, so it exists for this and nothing else: a thousand of the 1536 entries
re-seed themselves off bone 0 of the body, four in five in the blue of
`DAT_00806428` and the rest near-white, rising by `height/300000` a tick and
curling sideways on a heading taken from their own countdown.

Once both ramps are done the state holds. `+0x1A4` counts 19200 ticks — ten
seconds — down by the frame tick, and either that or a press of Circle or Cross
hands off to `FUN_00237A08`. That function arms a fade, sets game mode `0xC` and
calls `FUN_002241D8`: the return to the title screen. The port has no mode `0xC`,
so it keeps the fade, leaves the lead in state 10 the way the original does
first, and says on stdout what it dropped.

Also left out and named: `DAT_00343A10 = -1000.0`, which gates `FUN_002025E0`;
`FUN_00212DB0(0, 0, 0)`, which sets the star field's count to zero rather than
spawning one; `cGpffffb664`, a sound-suppression latch nothing in the port
reads; and `cGpffffb6d0`, the death latch `FUN_00251ED8` raises to close a block
in `FUN_00224ff0` — the pause menu, which the port does not have.

### The music is loaded by the death, not by the scene

`FUN_002063C8(7, 0xF, 1000)` at the end of `FUN_00255820` is a volume ramp, and
a ramp on its own cannot say what plays. The piece is named thirty lines
earlier, in `FUN_00251ED8`'s death branch: **`FUN_00205938(7, 0x2F, 0)`**, which
reloads music slot 7 from category 2 record 47 and leaves it stopped. The ramp
then starts it, because `FUN_002063C8:12` plays a slot it finds idle.

So slot 7 is the game over's, scene-wide, and whatever a scene parked there is
overwritten the moment the player dies. Reading the live category-2 table at
`(&DAT_00314BA0)[2]`, record 47 is **SND.BIN resource 133** at volume 0x46 —
2080 bytes of sequence.

Leaving the load out does not fail loudly, which is what made it worth writing
down. s01_e024's own slot 7 request is `0xB2`, category 2 record 178, resource
264: a 283 KB sample bank whose section 2 is a *seventeen-byte* sequence — one
program change and an end-of-track. The ramp starts it, it ends on its first
event, and the game over plays in silence. `--sound-dump` is how that reads: RMS
per second across `--damage 20:5` went `2370, 1068, 381, 0, 0, 0` before the load
was added and `2370, 1459, 1990, 2325, 1928, 2470` after.

### The jump: a landing state, and the moon jump

`FUN_002534D8` is the whole airborne state, and the port had two things wrong
in it.

**The landing runs to the end of its animation.** On touchdown the state writes
animation `0x10` and stays in state 2; the exit is `+0x06` bit 0 — the timeline
reporting complete — and not the ground test. The port exited on `grounded`,
which is true the instant the animation is chosen, so the recovery lasted zero
frames and the character snapped from falling to standing. With the right test
it now holds animation `0x10` for about eight frames, which is what a landing
looks like.

The fall arm also owes two sounds the port never played: `FUN_00255D88(entity,
3)` — the same surface table the footsteps and the takeoff read, column 3 — and,
when `+0x0C` bit `0x400` says the actor came down on a liquid, character cue
`0x0D` instead, latched through `+0x1BB` bit `0x10` so the two cannot both fire.
Nothing in the port raises `0x400` yet, because that branch of `FUN_002262C0` is
unported, but it is the reason the thud is conditional.

**The moon jump is the original's, not a harness affordance.** It is four lines
at the top of the same function:

```
if (uGpffffbd54 != 0 && (uGpffffb09c & 0x80) != 0) {
    +0xA0 = 0xC;  +0x44 = DAT_0035287C;
}
```

`uGpffffbd54` is set in `FUN_00251ED8:21-23` from the attack button held, cleared
unless `cGpffffb66a` — the debug byte — is up; `uGpffffb09c` is that frame's
newly-pressed mapped word and `0x80` is jump. The remap table is the identity, so
in practice: **hold Circle, tap Square.** Each tap re-seeds the vertical velocity
to the full jump speed and puts the rise animation back. That is all of it — no
state change, no return, and no four-frame startup, so the taps compound into a
climb.

The port had this as a debug affordance that zeroed `+0x44` and armed the
startup instead, so every tap cancelled the fall and then waited four frames
before pushing: the character hovered. Measured in `s01_e024`, tapping every six
frames now climbs 0 → 1.33 → 2.69 → 3.04 and then rides the ceiling, against an
ordinary jump's 1.86 apex.

Two details that are easy to get wrong:

- **The animation is sampled before the moon jump rewrites it** (`:12`), and
  every branch tests the sample. A moon jump out of a fall still takes the
  `0x0D` arm on the frame it fires.
- **`uGpffffb09c` is that frame's pressed word, not `FUN_0023B890(8)`.** The
  eight-frame OR that `FUN_00256BB8` reads would hold the boost on for eight
  frames per tap. `FUN_00251ED8` is handed the single-frame pair
  (`uGpffffb688` / `uGpffffb09c`) and the port now passes
  `FUN_0023B890_recent(1)` alongside the eight.

`--press-jump <frames>` and `--hold-attack <first>-<last>` were added to reach
any of this headlessly; nothing in the jump, the landing or the moon jump was
testable before.

### The plume is the landing, not the game over

The pale cloud that comes up around the body is **`FUN_002262C0`'s landing
dust**, and it has nothing to do with dying. The death launches the body with
`uGpffff88c8` of pop-up, so it falls and lands like any jump, and the touchdown
throws the same ring a jump does.

It sits at `FUN_002262C0:576-601`, right before `+0x0C` is written, so it is
shared by every actor the generic physics moves:

```
FUN_00219AF0(x, y, z - 0.15, 0.4, r, 0, r, 0x1E, 1, 5, 0, kind != 0)
```

with `r` the actor's own `+0x54` — one outer step of five puffs, each living
5..35 frames. The top nibble of `+0x6C` is the surface kind and only 0 and 3
raise dust; the rest are water, which gets `FUN_002D4108`'s ripple instead. The
kind doubles as the colour, 3 being lit white and 0 the pool's own default.

The speed gate is `FUN_0030BD20(v * 128.0) < -4`, and `FUN_0030BD20` **truncates
toward zero** — it shifts the mantissa down before applying the sign — so the
real threshold is `v <= -5/128`, not `-4/128`. That boundary does real work: the
death's fall reaches -0.045 and throws dust, a plain knockback's reaches -0.034
(which is -4.35, truncating to -4) and does not. Rounding instead of truncating
would put dust under the knockback as well.

The port's touchdown test lives in the lead's own physics copy, because
`FUN_002262C0` is still unported for slots 1..255 — so nothing else raises dust
on landing yet.

### A faded model still writes depth

`+0x134` rides in the draw header — `FUN_0020C810:140` copies it to `+0x1FC`,
substituting `0x80` for zero, and `FUN_0020DFB0:79` hands VU1 `+0x1FC >> 2`. The
port turns that into a vertex alpha and, because an opaque pass would otherwise
throw it away, promotes the pass to blending. That promotion used to select
register block 1.

**It has to stay on block 0.** The fade never touches the block index; only a
pass's own texFlags mode rewrites it. Block 0 is the one block with ZMSK clear,
so a faded model goes on writing depth, and block 1 does not. Promoting to 1
turned the depth mask off and let every triangle of the body draw over every
other in submission order. At the game over's `0x7C` that is invisible in the
colour — 124/128 is within 3% of opaque — and ruinous in the structure: the far
arm showed through the torso and the body read as a ghost.

Confirmed on hardware, mid-game-over, by poking the lead's `+0x134` down from
`0x7C`: at `0x08` the body blends with the floor tiles behind it, so ABE really
does come on, and its own limbs stay correctly sorted the whole way down. The
fix is to pass `mode` through unchanged and only raise `blend`.

### Checking it

Nothing in the port lands a blow on the lead: no ported enemy runs an attack
against it. So the harness writes `FUN_00216140`'s four output fields directly —
`+0xBE` the damage, `+0xBC` the reaction, `+0xC0` its length, `+0xC4` the
direction — and lets `FUN_00251ED8` spend them.

Keys `1`..`5` in the window, and `--damage <frame>[:<kind>][,...]` for a
headless or captured run; `0` heals. The kinds are

| kind | damage | `+0xBC` | reaction |
| --- | --- | --- | --- |
| 1 | 3 | — | stagger |
| 2 | 12 | — | stagger |
| 3 | 12 | `0x12` | knockback |
| 4 | 12 | `0x13` | flatten |
| 5 | 999 | — | lethal: the knockback, then the game over |

```
orphen_port --disc-root disc --scene s01_e024 --no-audio \
  --damage 30:1,50:1,70:1,90:1 --screenshot out.ppm:160
```

The whole death takes about twelve seconds, so a lethal capture wants a late
frame: `--damage 20:5 --screenshot out.ppm:260` is the black room with the spark
column, and running to frame 800 with `--actor-report` reaches the hand-off and
leaves the lead in state 10.

## The sword attack, and the input buffer behind it

Circle, grounded, swings a glowing sword. `FUN_00256bb8`'s attack branch
dispatches on `FUN_002298d0(*entity)` -- the entity's **type id**, not an
equipped item -- and type 1, the lead player, answers weapon class 0, which is
`FUN_00225bf0(entity, 0x1C, 0x33)` and nothing else. Everything after that is
driven by the animation.

`FUN_00251ed8` sends states at or above `0x1C` through a *second* dispatch
table, `PTR_FUN_0031e160`, whose first entry is `FUN_00256130`. The two tables
are adjacent in memory -- `0x0031E160` is `0x0031E0E8 + 0x78` -- but they are
genuinely two tables, offset by two entries, and indices `0x1C`/`0x1D` of the
first are null. Ported states have to be routed by hand; falling through to the
grounded field branch, which is what the controller used to do for anything that
was not state 2 or 10, re-reads the pad and overwrites the swing animation with
`stand` on the next frame.

### The state is four points in a timeline

`FUN_00256130` reads no input at all. It watches entity `+0xA8`, the timeline
cursor `FUN_00225c90` steps by two per keyframe, and `+0x06`, the three latches
that stepper writes:

| when | what |
|---|---|
| cursor reaches 2 (`+0x06` bit `0x08`) | spawn the blade, type `0x42` |
| that keyframe's duration expires (bit `0x04`) | play cue `0xA4`, the swing |
| a keyframe carrying `+0xAA` bit `0x200` | put the blade on its dissipate animation |
| the animation completes (bit `0x01`) | `FUN_002560e8` returns to idle |

`+0xAA` is the third halfword of the current keyframe record, and it is where
the animation data carries its own events. Animation `0x33` on `grp_0001` is
nine entries totalling 46 frames, and entry 7 is the one holding `0x200`:

```
[ 0] col= 75 dur=10  trailing=0x0000
[ 1] col= 76 dur= 6  trailing=0x0000    <- cursor 2, the blade spawns here
[ 2] col= 77 dur= 4  trailing=0x0d00
[ 3] col= 78 dur= 2  trailing=0x0400
[ 4] col= 79 dur= 2  trailing=0x0400
[ 5] col= 80 dur= 4  trailing=0x0400
[ 6] col= 81 dur= 4  trailing=0x0000
[ 7] col= 82 dur= 2  trailing=0x0200    <- the blade is told to dissipate here
[ 8] col= 83 dur=12  last entry
```

The swing is committed the moment it starts: no movement, no facing change and
no pad reads for the whole 46 frames. That is why it cannot be steered, and why
the character slides to a stop rather than turning into the blow.

### The blade is an entity, and it carries a light

Type `0x42` is `grp_0179` out of the boot bundle -- four bones, three
animations -- attached to the swinging entity's **role-5** bone. Not role 4:
role 4 is the right hand, where a held weapon goes, and role 5 on `grp_0001` is
bone 17, a finger. The blade grows out of the fist rather than being held. Its
`+0x158` is set to `DAT_00352998`, which is pi -- a roll on the root matrix,
not a facing; the facing is copied separately into `+0x5C`.

`FUN_002d21b8` runs it. Every frame it drives a `DAT_00343888` light slot from
its own bone 0, ramping the colour from 128 grey to 255 white as `+0x62` climbs
`0x1000 -> 0x1FE0` at eight per tick, and steps `+0x134` by a flat 4 until it
passes `0x78` and then drops it to 0 -- so the blade fades in from `4/128` alpha
over about thirty frames and then draws solid. Its own animations are the rest
of its lifetime: 1 is the swing, 0 the idle it falls into when that completes,
2 the dissipate, and completing 2 deletes it.

It also re-reads `DAT_0058BF10` -- pool slot 0's `+0x60` -- every frame and
deletes itself the moment the lead is not in state `0x1C`. The blade cannot
outlive the swing even if its own animation has not finished.

**`FUN_00265ec0` is not `EntityPool::releaseSlot`.** The pool's own release is
the map-load clear; the original's does three things first, and one of them is
`FUN_00266098`, which hands the entity's light slot back by writing its radius
to 0. That is the *only* thing that frees a light slot, so without it every
swing would leave a two-unit white light burning where it ended and sixteen
swings would fill the table. `FUN_00265ec0_destroy_entity` in
`actor_frame_update.cpp` is the full version; `--scr-report` after four swings
shows slot 0 allocated, driven and released each time.

### `FUN_0023b890(8)` is an input buffer, and it was missing

`FUN_0023b5d8` pushes one word per frame into a 64-entry ring at
`DAT_00342a70`, packed `held << 16 | pressed`, where both halves are the raw pad
run through the remap table at `DAT_00571A50`. That table is the **identity** in
both EE dumps, so a mapped action bit is the raw pad bit: `0x80` Square is jump,
`0x20` Circle is attack, `0x10` Triangle is use.

`FUN_00256bb8` does not read the current frame's buttons. It calls
`FUN_0023b890(8)`, which ORs the last eight entries together -- so a press
counts if it happened at any point in the last eight frames. That window is the
game's input buffer: an attack pressed during the tail of a jump still fires on
the frame the player lands. The port had been feeding the controller a single
instantaneous `jumpRequested` bool; `MappedActionHistory` in
`ported/input/mapped_action_history.h` is the ring, and jump now goes through it
as well.

One bug fell out of writing it. `InputSnapshot::rawPressedPad` was not in
`main.cpp`'s `consumeEdgeTriggered` list, so on a frame that ran several
catch-up simulation steps every edge-triggered pad read -- the interaction
probe, the camera's recentre -- saw the same press once per step. The original
computes it as `held & ~previous` once per tick, which can only be true on one
of them.

### Not ported

`FUN_002148a8`, the swept hit test the blade runs on its animations 0 and 1, and
`FUN_002d59c0`, the reaction it triggers. It is several hundred lines of capsule
sweeps against the entity pool, and nothing in the port takes damage yet.
`FUN_00216078(ownerType, 0, effect + 0x198)`, which fills the blade's `+0x198`
from a per-type parameter table, is skipped with it -- that field is the hit
test's only reader. Entity `+0x12C`, the attack power `FUN_00215670` subtracts
the defender's `+0x12E` from, *is* modelled and copied onto the blade, but
nothing fills the player's copy: `FUN_00251dc0` reads it out of the party stat
table at `DAT_0034368F`, which the port does not load.

Weapon classes 1..5 -- the other things the lead can be holding, two of which
throw a type `0x4E`/`0x50` projectile from this same branch -- are not
reproduced, because there is no inventory to reach them through.

### Checking it

`--press-attack <frames>` fires Circle on a comma-separated list of 1-based
frames, the same shape as `--press-confirm`, so the swing is reachable from a
headless or captured run:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 \
    --no-audio --frames 300 --press-attack 60,120,180,240 --actor-report --scr-report
```

reports `type=0x42 entities=1 ticks=124 firstSlot=29` -- four swings of 31 ticks
each, all recycling the same pool slot -- and one light slot allocated and
released. `s01_e024` is the scene to use: the lead is controllable within a
frame or two of load, where `s01_e012` opens on cutscenes.

`--press-jump <frames>` is the same for Square, and `--hold-attack
<first>-<last>` holds Circle across a range **without** re-pressing it, so it
arms the moon jump without starting a swing on every frame. Together they are
the only way into `FUN_002534D8` from a headless run:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 \
    --no-audio --frames 60 --press-jump 15 --actor-report
```

lands at frame 59 and holds animation `0x10` -- the landing -- until frame 67.
Adding `--hold-attack 18-140` and a tap every six frames climbs instead:
`0 -> 1.33 -> 2.69 -> 3.04`, against the plain jump's 1.86 apex.

## The magic cast, and the sprite-strip model kind

Triangle, grounded, throws a homing magic projectile. It is the *use* branch of
`FUN_00256bb8`, not a second attack branch -- the same `FUN_002298d0` type-id
lookup, the same weapon class 0, and for the lead player that is state `0x1D`
with animation `0x14`. `PTR_FUN_0031e160[1]` is `FUN_002562b0`.

The cast cue `0xA5` plays on entry, from `FUN_00257b50`, rather than partway
through the animation the way the sword's `0xA4` does.

### The projectile exists for the gap between two keyframes

`FUN_002562b0` has the same shape as the sword's state -- `FUN_002560e8` at the
top, then points in the timeline -- but the projectile is alive between them:

| when | what |
|---|---|
| cursor reaches 6 | `FUN_002d2e00` spawns it, charging, at the caster's role-4 bone |
| every frame while its `+0x60` is 0 | its `+0x20` is rewritten to that bone |
| cursor reaches 10 | launch: `+0x60 = 1`, `+0x04` bit `0x100` cleared, cue `0xA6` |
| the animation completes | back to idle |

**It is not attached through `+0x192`.** The caster writes its position every
frame instead, which is a different thing: an attachment rides the bone matrix,
this is a copy. That is why interrupting the cast leaves the charge where it was
rather than dragging it, and why `FUN_002d2470`'s state 0 has to delete itself by
watching `DAT_0058BF10` for the caster leaving state `0x1D`.

`FUN_002d2e00` refuses to spawn at all if `FUN_00227798` says the floor is above
the hand, which is the guard against casting while clipped into geometry. The
original treats that refusal and a full pool identically: drop the cast.

### The homing is two angles and a ramp

`FUN_002d2ca8` picks the target **once**, at spawn, and nothing ever revisits it
-- so the projectile locks on and can be dodged by moving afterwards. It scans
pool slots 10 upward, so it can never choose the caster, and takes the nearest
candidate within ten units whose `+0x02` has bit `0x08` and whose `+0x04` does
not have bit `0x10`. The elevation gate reads backwards from what you would
expect: a candidate more than two units away *vertically* is accepted on
distance alone, and only one within two units also has to sit inside a
60-degree cone (`fGpffffa738`).

In flight, the yaw at `+0x5C` and the elevation at `+0x1A0` each step toward the
target through `FUN_0023a320`, capped by `+0x1A4` -- which starts at **zero** and
ramps by 0.005 a frame to 0.349 (20 degrees). The projectile leaves the hand
travelling dead straight and only tightens later. That ramp is most of why it
reads as a guided missile rather than a tracking beam, and it is the reason a
cast at something close often misses.

Three things detonate it: the swept hit test, `+0x0C` picking up any of `0x266`,
or outliving `0x2580` ticks. Then `+0x60 = 4`, and state 4 fades its light down
by two a frame until it is out.

Every other frame it drops a ghost of itself -- same type, same position, same
scale, `+0x60 = 2`, physics off -- which shrinks by 0.05 a frame and dies when
*its* animation ends. That is the trail, and it is what made the sprite-strip
work below unavoidable.

### `+0x02` bit `0x200`: the other model kind

`FUN_00225c90`'s very first line branches on entity `+0x02` bit `0x200`, and the
port only ever had the `== 0` half. Type `0x44` is the first thing to carry the
bit, and its model, `grp_017e`, is 324 bytes with **no PSC3 magic**:

```
+0x00 u16  sprite record count   (12)
+0x02 u16  animation count       (2)
+0x08 u32  sprite record table, 16 bytes each
+0x0C u32  animation table, one u32 offset per animation
```

The animation table sits at the same header offset a PSC3's does, which is what
lets `FUN_00225c90` read `+0x9C` the same way for both. Its entries are **four**
bytes rather than six -- a column and a duration, and no trailing event halfword,
so a sprite strip cannot carry the keyframe events the sword swing is driven by.
Duration bit `0x8000` is still "last entry"; bit `0x4000` is new, a conditional
entry skipped unless `+0x06` bit `0x20` is set. The blend at `+0x13C` is left at
1.0 throughout: sprite frames cut, they do not interpolate.

`grp_017e` animation 1, the one the trail ghosts play, is ten entries of two
frames on columns 2..11.

This was not optional. The ghost's lifetime is owned by `flags06` bit 0, which
only the animation stepper sets; without the branch every ghost the projectile
dropped lived forever and the pool filled in a few seconds.

### What is drawn

The projectile is drawn by the **second** entity pass, `FUN_0020f3e0`, not the
skeletal one -- `FUN_0020c5a8` refuses a `0x200` entity outright, so `NODRAW` in
the actor report is the correct answer for that pass and always was. That second
pass is ported now; see The billboard pass, below. `grp_017e` is a strip of
billboard sprite records rather than a mesh, and the bolt is a two-column looping
glow with a trail of shrinking copies behind it.

It also carries a light of its own: `FUN_002d2e00` allocates a `DAT_00343888`
slot through `FUN_00266050`, state 0 brightens it four a frame while the charge
grows, `FUN_002660d0` carries it along the flight, and state 4 fades it out. That
light is what the homing was originally verified against, before there was
anything to see.

`FUN_00215ac8` -- the box hit test -- and `FUN_00216078(casterType, 1, ...)`,
which fills the `+0x1AC` parameters it is the only reader of, are both ported
now; see Contact, damage and death, below. So is the hundred-particle impact
burst, and so is state 4. The lifetime is complete, and a bolt that reaches an
enemy detonates on it rather than flying on to its timeout.

One caveat that is not the projectile's fault. `integrateNonPlayerMovement` is
not `FUN_002262c0` -- see the note on it -- so where a flying actor is stopped is
approximate, and the detonation point with it.

### Checking it

`--press-magic <frames>`, alongside `--press-attack` and `--press-confirm`.
Homing needs a target inside ten units, and `s01_e024`'s spawn is about eleven
from the nearest enemy, so the plain cast flies straight -- which is correct, and
also not a test of anything. Move first:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 \
    --no-audio --frames 900 --spawn -2.5,-8,0 --press-magic 60,200,340,480 \
    --actor-report --scr-report
```

`type=0x44 entities=24 ticks=5137` over four casts, `live actors: 20` -- the same
as the same run with no casts at all, which is the check that the trail is being
collected. The light report ends with the slot released at the enemy cluster
rather than at the caster, which is the homing having worked.

## The billboard pass, and why there are two entity draws

`FUN_0020c5a8` -- the skeletal entity pass -- refuses any entity whose `+0x02`
carries bit `0x200`. `FUN_0020f3e0` then walks the same 256 slots for exactly
those. The two passes **partition** the pool rather than filtering it: an effect
entity is never a candidate for the skinned path, and a character is never a
candidate for this one. The port had only ever had the first, which is why every
sprite effect was invisible.

`FUN_0020f3e0`'s guards are the mirror image: a live type id, `+0x02` bit
`0x200` set, `+0x08` bit 0 clear. It also clears `+0x0C` bit `0x1000` on the way
in, and `FUN_0020f510` sets it back on anything it submits -- a "was drawn this
frame" latch nothing else reads.

### The model is a strip of sprite records

Parsed by `loadSpriteStripModel`. No PSC3 magic, and the header's `+0x00` is a
**column** count, not a record count:

```
+0x00 u16  column count
+0x02 u16  animation count
+0x04 u32  column table:  {u16 firstRecord, u16 recordCount} per column
+0x08 u32  sprite record table, 16 bytes each
+0x0C u32  animation table, one u32 timeline offset per animation
```

The animation picks a column (entity `+0xAC`) and the column names a run of
records, drawn **back to front** -- `FUN_0020f510` seeds its cursor at the last
record of the run and walks down. `grp_017e`, the magic projectile's, is twelve
columns of one record each.

Each record:

| off | what |
|---|---|
| `+0x00` | bits 0-1 blend mode, `0x10` flip X, `0x20` flip Y |
| `+0x01`/`+0x02` | texel origin |
| `+0x03`/`+0x04` | extent, **one more** than the real width and height |
| `+0x05` | twice the alpha byte |
| `+0x06`/`+0x07` | offset in whole sprite units, signed |
| `+0x08` | depth bias, over 100, added to the view depth |
| `+0x0C` | float scale |

The blend mode is the same 0..3 the map and PSC3 paths already use, so it goes
straight into `setMapBlendMode`. That is not a guess: `FUN_0020f510` packs the
GS PRIM word as `0x0D` or `0x1D` (untextured / textured triangle strip) and ORs
bit `0x40` -- **ABE** -- for any non-zero mode. Mode 0 does not blend at all,
which is also why its alpha byte is forced to `0x80` and never read.

### The quad is built on the projected origin

`FUN_0020b600` projects the entity origin to an integer GS screen position, and
**every corner is an offset from it**, in GS units:

```
projScaleX = trunc(entity+0x14C * 280.0 * G / viewZ)      G = 2 * DAT_00355658
gsX = originX + (spriteX * projScaleX >> 8)               spriteX in 1/16 units
gsY = originY + (spriteY * projScaleY >> 9)               note the extra shift
```

The quad is axis-aligned in screen space, which is why a sprite never rotates
with the camera. Both `projScale`s are truncated to integers before being used
as fixed point, so a distant sprite's size quantises in visible steps rather
than shrinking smoothly; the record's own scale at `+0x0C` truncates the same
way. The extra shift on Y is not a bug to correct: the GS output is 640x224
shown at 4:3, so its pixels are 2:1 and a square sprite has to be twice as wide
in pixels. `X` uses `+0x14C` and `Y` uses `+0x150`, which is why a non-uniform
entity scale stretches the sprite.

The port runs that arithmetic in GS integers and then un-projects the four
finished corners back to view space, because it draws through the same
projection the world does instead of writing GIF packets. Re-projecting them
gives back the same GS numbers -- it is the inverse of the transform above, at
the depth the GS z was keyed off. That is the only adaptation in the function,
and `drawSpriteQuads` accordingly runs with the modelview at identity, the
scene's own projection, and depth writes off. PRIM's `FGE` bit is clear in both
of `FUN_0020f510`'s words, so a sprite is never fogged however far away it is.

> The first cut of this pass emitted the offsets *without* the origin, on the
> reasoning that the perspective divide made the quad a fixed size in view space
> and the origin would come out in the wash. It does not: the offsets are
> relative to a point that has to be added back. Every sprite landed dead centre
> of the screen at its own depth, so the bolt sat on Orphen and vanished behind
> him as it flew away. Reformulating the maths is how that got missed; the port
> now follows `FUN_0020f510`'s own shape and adapts only at the last step.

### Depth is keyed off a different number than the position

```
depth = viewZ + (char)entity+0x133 * DAT_003520a0        (0.08)
              + (char)record+0x08 / 100.0
gsZ   = trunc(DAT_003555a4 / depth + DAT_003555a0)       floored at 0
```

Both biases move the sprite in depth only -- the position and size came out of
`projScale`, which used the unbiased `viewZ`. Entity `+0x133` is copied from
descriptor `+0x02` by `FUN_00229c40`, and most effect descriptors carry `-12`, so
the usual effect sits about a unit nearer the camera than it stands. Entity
`+0x08` bit `0x40` throws all of it away for a flat `0xFFFF`, nearer than
anything the world writes.

Submission is a 4096-bucket display list on `gsZ >> 4` clamped to `1..0xFFF`,
plus the `0x1005` bucket above it for the `bit 0x40` sprites. `gsZ` rises as a
sprite comes nearer, so ascending bucket is back to front, and the port sorts
the same way. The one thing it does not reproduce is that the original links
each packet in at the *head* of its bucket, so co-bucketed sprites come out
reversed among themselves -- invisible for the additive blending everything
drawn so far uses.

### The colour

`FUN_0020f3e0` stages `max(DAT_0035566c, DAT_00355670)` per channel -- the scene
ambient against light 0's colour -- and `FUN_0020f510` averages that with the VU0
point-light contribution at the sprite's own position, saturating. `+0x08` bit
`0x4000` skips the whole thing for a flat `0x80`. GS bytes throughout, `0x80` for
1.0, so a sprite can be brighter than white before the blend.

The near clip here is `DAT_0035209c`, **0.3** -- not the `0.4` the geometry path
uses. The far one is the scene's draw distance.

### What is still missing

Nothing, now. Both of the branches this section used to list as missing landed
with the battle target cursor, which is the one thing in the executable that
takes them together:

- **The rotation branch**, `+0x08` bit `0x400`, rebuilds the quad as four
  rotated corners off the angle table at entity `+0x168`, for record indices
  under nine. Angle 0 swings the quad's *centre* about the sprite origin; angle
  N -- N being the record's own descending index -- spins the quad about its own
  centre. Everything is done with Y doubled, because the GS output is 640x224 at
  4:3 and rotating in raw GS units would shear the sprite; the sine term is
  halved on the way back out.

  `FUN_002D73E8` writes those angles: `DAT_003547A4`, which is pi/4 to the bit,
  while the cursor is the player's target, and an accumulation of `DAT_003555BC
  * DAT_003547A8` -- about 25 degrees a second -- when it is not. That is the
  difference between the selected bracket sitting corner-up and the rest slowly
  drifting.
- **The HUD branch**, `+0x08` bit `0x1000`, takes a position that is already in
  screen space rather than projecting one: a flat `+0x14C * 256` scale with no
  perspective divide, and a GS z read back out of `+0x28` as an integer.

Note the aliasing on `+0x168`. For a skinned entity those 42 bytes are the
per-bone override modes (see "`+0x168` is inside the slot"); for a sprite entity
-- which carries `+0x02` bit `0x200` and can never reach `FUN_0020C5A8` -- they
are seven floats of quad angles. The two never coexist. The port splits them:
bone modes live in `EntityBoneOverrides`, angles in `OriginalEntity`.

### Checking it

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 \
    --no-audio --spawn -2.5,-8,0 --press-magic 60 --screenshot shot.png:112
```

catches the bolt as a glowing orb in Orphen's hands; frames 124 onward have it
clear of him and travelling with its trail. Four casts over 900 frames still
report `live actors: 20`, and `--render-bench 6` reports the same primitive and
batch counts as it did before the pass existed -- which is the check worth
making, because absolute frame times on this machine move by 30% run to run for
reasons that have nothing to do with the port. Compare the counts, not the
milliseconds.

## The spark shower, and the pool behind it

When the magic projectile ends it throws a hundred sparks. They come out of a
**global particle pool**, `DAT_00355620` — 0x18000 bytes at 0x01C69A00, so 1536
entries of 0x40 — with a single behaviour pointer, `DAT_00355e0c`, that every
entry is stepped through. `FUN_002d3218` walks all 1536 once a frame, from the
slot `FUN_002239c8` gives it immediately after the actor loop. A null behaviour
skips the pool entirely, however much of it is marked alive.

`FUN_0022a418` resets the pool at scene init. **That reset draws from the RNG
once per entry** — 1536 draws — and writes each into `+0x22` as a stagger. Only
the unported ambient-dust behaviour reads that stagger; the spark path
overwrites `+0x22` on spawn. So for sparks the reset's only observable effect is
the advance of the shared generator, and porting it faithfully moved this
port's `--actor-report` baseline. Runs remain reproducible; the numbers are just
1536 draws further along than they were.

### The entry

```
+0x00 f32  x, y, z            world, the axes an entity's +0x20 uses
+0x10 f32  vertical velocity
+0x14 f32  horizontal speed
+0x18 u32  GS RGBA, alpha in the high byte
+0x1C s16  alive -- the spawn loop tests this and nothing else
+0x1E s16  quad width  in units of 40 GS 12.4 units at unit depth
+0x20 s16  quad height in units of 20 -- the 2:1 GS pixel again
+0x22 s16  ticks before the fade starts; allowed to go negative
+0x24 f32  gravity
+0x28 f32  heading, radians
+0x2C s16  the spawning entity's pool slot; written, never read
```

Every field was read out of a PCSX2 save state taken with the sparks live:
exactly 100 alive, all tagged with the projectile's slot, headings stepping by
0.0349066 rad, colours with red in `{0, 0xFF}`, green and blue each in
`[0xC0, 0xFF]`, alpha `0xE0`.

### The burst

`FUN_002d2470` fills up to a hundred entries, fanning out from the projectile's
own facing plus `DAT_00354690` (π/2, so the first spark leaves square to the
flight) and stepping two degrees per particle. The fan advances once per
particle *seeded*, not once per slot examined, so a burst landing in a
fragmented pool still gets an even spread — and simply gets fewer than a hundred
rather than waiting.

Red is all-or-nothing on a coin flip while green and blue are each a random
`0xC0..0xFF`, which is why the shower reads as half white and half cyan and
never dim. Width and height take the *same* draw, 1 or 3, so a spark is either
one pixel or three times that with nothing in between.

`FUN_002d2348` then flies each one outward along its heading, bleeding
`DAT_00354678` off the speed a frame until it reaches that value, on a properly
integrated ballistic arc — the position takes the old velocity plus half the
acceleration term and the velocity updates from the same old value. Two
different tick scalings are in play and they are not the same quantity: the
vertical integration works in ticks/8 and the horizontal step in ticks/2. Once
`+0x22` runs out the alpha falls by two a frame until the particle frees itself,
so a spark lives about 30-59 frames plus 112 of fade.

### The quad

`FUN_0020b600`'s `vftoi0` is masked to `.xyz`, so lane W survives as a float
holding `Q = 1 / max(viewZ, eps)`. `FUN_002d3058` multiplies its two literals by
that directly, which means **a particle's size is a plain 1/z with no projection
term in it** — one to three pixels at any normal depth. The projected point is
the quad's top-left corner, not its centre.

The texture is the boot sheet in slot `0x2A`, which the port already binds: the
packet carries `0x2B` and that field is slot + 1, the same encoding
`FUN_0020f510` uses for entity `+0x136`. The UV rectangle is fixed at texels
(65,177)-(79,191), and `DAT_10008080`'s bit `0x8000` selects blend mode 2,
additive. `FUN_00207de8(0x1000)` puts every particle in one display-list bucket,
above all of `FUN_0020f510`'s depth-sorted `1..0xFFF` and below its `0x1005`, so
the port emits them into the same list with that bucket.

Unlike the sprite pass, `FUN_002d3058` has **no near, far or window cull** — its
only guard is the clip flags. The port rejects a particle behind the eye, which
`FUN_0020b600`'s `w` clamp cannot save and which would otherwise smear across
the screen, and nothing else.

### What is not ported

`FUN_002d3320`, the ambient dust `FUN_002d36f8` installs into the same pool. It
reseeds its own particles off the player's bones through `FUN_0020dc88` and
reads a per-character colour table at `DAT_00326708`; it is a separate effect
that happens to share the storage.

### Checking it

`--actor-report` ends with a `particles:` line giving the live count and the
installed behaviour. On `s01_e024` with `--spawn -2.5,-8,0 --press-magic 60` the
projectile detonates around frame 165, the pool reads `alive=100` from there
through frame 250, and it is empty again by 340. Screenshots are a poor check
here: the bolt homes on the enemy cluster and leaves frame before it ends.

## Contact, damage and death

A hit is three things on three different frames, and keeping them apart is most
of what makes this readable:

1. an **effect** entity — the sword blade, the magic bolt — sweeps a volume
   against the pool and calls `FUN_00216140` for whatever it touches;
2. `FUN_00216140` works out what that contact costs and **adds it to the
   victim's `+0xBE`**. It never reads hit points and never kills anything;
3. the victim's own behaviour, next time it is ticked, drains `+0xBE` against
   its `+0x12A` and decides whether that was fatal.

`+0xBE` is a mailbox, not a subtraction. That is why a hit landed after a victim
has already run this frame still counts, and why two attackers in one frame both
land.

The attacker in step 1 is the *effect*, not the swinger — `+0x12C`, `+0x02` and
`+0x96` are all read off the blade. `FUN_00256130` copying the player's `+0x12C`
onto it at spawn is the only place the two are connected.

### The volume comes out of the model

PSC3 header `+0x30` and `+0x34` are two sections nothing else reads, and only
weapon-effect models carry them: `grp_0179` (the blade) has both, `grp_0001`
(the player) has neither. That is the early return which makes `FUN_002148a8` a
no-op for everything that is not a weapon, and it is why the parser had never
needed them.

`+0x30` is one byte per pose column — a record index, or `0xFF` for "no volume
here". The blade's is sixteen bytes and only columns 1, 3, 4, 11 and 12 are
armed, so the swing is dangerous for part of its arc and inert for the rest.
`+0x34` holds `{steps, radius, A[3], B[3]}` records: a segment in model space,
how many boxes to lay along it, and their half-extent in fortieths of a unit.

**The sweep looks forward, not back.** The interpolation factor is `+0xA4 /
+0xA6` — the countdown *still to run* over the entry's duration — so it is 1 at
the start of a timeline entry and 0 at its end, and the volume runs from the
column being entered back toward the one being left. The pair actually measured
is `[now, next frame]`: the near end is evaluated at `remaining - frameTicks`
and cached into `+0xF0`/`+0x100`, and the far end is last frame's cache.
`+0x06` bit `0x40` says the cache is valid, which is why forgetting the
already-hit set and forgetting the sweep history are the same call.

Between the two the test fills the gap: any box that moved more than four
half-extents in a frame gets extra boxes appended, interpolated corner-wise. The
cap is 31 and it **abandons the whole fill mid-segment** rather than clamping,
so a very fast swing gets fewer boxes at its far end rather than coarser ones
along its length.

### The return value is not what the decompiler says

`cStack_161` reads as a local nothing ever assigns, which is why the port used
to call `FUN_002148a8` dead and treat the bolt's hit as unreachable. It is
`sp+0x83F`, and the scratch buffer the test hands `FUN_00216140` starts at
`sp+0` — `0x002164D0` is `sb v0, 2111(s2)`, `FUN_00216140` storing to exactly
that byte. Ghidra never connects the two.

**Both hit tests return the number of contacts.** That is what plays the sword's
hit cue and what makes the magic bolt detonate on something alive instead of
flying on to its five-second timeout.

### Two seams worth knowing about

**The already-hit set does not line up with its own clear.** Both tests start a
`uint *` at `+0xD0` and step it *before* the first slot, so slot n's bit is in
word `(n >> 5) + 1` — `+0xD4` for slots 0..31, up to `+0xF0` for 224..255.
`FUN_00215e48` clears eight words from `+0xD0` down: it clears `+0xD0`, which no
slot uses, and misses `+0xF0`, which slots 224..255 do. A slot that high stays
flagged for the rest of the scene once hit. And `+0xF0` is also where the test
caches last frame's blade endpoints, so on the real machine those slots test the
float bits of a coordinate. Reproduced rather than tidied; `s01_e024` never
fills the pool past slot 29, so nothing can observe it here.

**The enemy resistance table is indexed by `type - 0x7C`, unchecked.** The type
`0x62` flyer sits below the enemy range, so its index is **-26** and the read
runs backwards out of the group into the blob's string pool. It is perfectly
deterministic — same file, same offset — and it comes out as 46 for element 0.
So the sword's `1.3x` lands on `trunc(0.46 * 1.3 * 1) = 0`, and the hit costs
anything at all only because `FUN_00216140` floors the net at one point.

That floor is confirmed from outside: `FUN_002206a8` spawns `damage * 10` hit
sparks, and the save state holds two struck flyers with ten sparks each.

### The flyers have no hit points

`+0x128` and `+0x12A` are both zero on every type `0x62` in the dump, so
`0 - damage < 1` on the first hit and **any** contact kills. `FUN_002cd0a0`'s
kill branch then puts them in state 6 on animation 4, clears `+0x0C` bit 0 so
the death starts airborne, and sets `+0x134` to `0x7C` and `+0x138` to `0xC0`.
`eeMemory.bin` catches two of them mid-fall in exactly that state, one frame
apart, both with `+0xCC` pointing at the blade.

`FUN_002cda60`, state 6, splits on the grounded flag: airborne it pitches toward
π/2 and spins about its own facing at exactly twice the pitch rate, so it rolls
through half a turn while it tips over; grounded it swaps to animation 5 and,
when that completes, assigns `+0x06 = 0x10` — an assignment, not an or — and
raises `+0x04` bit `0x800` to hand the slot to `FUN_0023a568` to fade out and
free itself.

### The lead's own numbers

`FUN_00251dc0`, which `FUN_0022a418` calls at scene init with `DAT_0058beb0`
outright, copies four fields off the party record: max hit points, hit points,
attack power and defence. Orphen's record gives 50 / 50 / 1 / 0, which is what
the dump holds. Attack power is the number `FUN_00216140` scales, so without it
every hit fell through the damage floor by accident.

### Checking it

`--actor-report` grew a `hit tests:` line — sweeps built, boxes laid down after
subdivision, contacts, and the points those contacts charged — plus the world
bounds of the last sweep, which is how you tell "the swing is too short" from
"the swing is at the wrong height".

The flyers in `s01_e024` hover about eight units above the floor the player can
actually stand on, so they cannot be walked up to in the harness. `--place-slot
<slot>,<x>,<y>,<z>` parks one pool entity at a fixed point every frame, which is
scaffolding rather than anything the original does:

```bat
orphen_port.exe --disc-root . --scene s01_e024 --frames 400 ^
  --spawn -2.5,-8,0 --place-slot 23,-2.15,-8.0,0.3 --press-attack 60 --actor-report
```

reports `sweeps=14 boxes=38 contacts=4 damage=4`, `live actors` down from 20 to
16, and `type=0x62 state=6 -> 0x2cda60` running for 311 ticks. Swapping
`--press-attack` for `--press-magic` and parking the flyer in the bolt's path
gives one contact, one point, one death and the impact burst.

### The hit sparks

`FUN_002206a8` is a **third** particle system, sharing nothing with the other
two: a thousand entries of `0x38` bytes at `DAT_00355b74` in ten fixed groups of
a hundred, no behaviour pointer, and a quad oriented in world space. Ten hits
can be showing at once and an eleventh silently shows nothing, because a burst
takes the first group whose count is not positive and gives up if there is none.

A burst is `min(100, damage * 10)` sparks at three quarters of the victim's
height, each with a random yaw over the full circle and a place in a fan that
starts at thirty degrees and steps `(360 / count)` degrees — an **integer**
division, so a burst of seven leaves a gap rather than closing the circle.

A spark is a **streak**, not a billboard: four corners at `(±1, ±0.01)` in the
local x/z plane with **only the long axis scaled**, so it stays 0.6 units long
and 0.02 wide however far away it is. Two matrices place it, and they have to be
separate because the streak points along its own travel while the burst as a
whole is oriented off the camera: `Z(-fanAngle)` for the streak, and
`Y(yaw) → Z(-cameraYaw - pi/2) → T(spawn)` for the burst. That second Z is the
exact inverse of the view matrix's own yaw, so the fan opens across the screen.

Two things had to be read out of the disassembly rather than the decompiler.

**The matrix sequence.** `FUN_00220910` loads `vf20..23` with the identity once
and never writes them again, so every `sqc2 vf20` in the middle of the run is
*restoring the identity* to the scratchpad before the next rotation builder
writes its four entries over it, and `vcallms 0xC` accumulates into `vf28..31`
oldest first. Read as C it looks like four builders each overwriting part of a
product matrix, which would be meaningless.

**The texture slot.** `FUN_002190f8`'s last argument carries `0x22` in its low
byte, and that is the slot **itself** — texture `0x19A`, which at exactly the two
rectangles in the descriptors at `0x003159B8` holds two lens-shaped streaks,
blue for a party-side victim and gold for everything else, each the exact size
of its rectangle. Slot `0x21` has a smoke puff across both and nothing
streak-shaped anywhere on it, and the save state's own screenshot shows gold.

`FUN_0020f510` writes `slot + 1` into the same packet field for the sprite pass,
so the two producers disagree by one and only the sheets say which the consumer
honours. The same reading fixed `DAT_00355620`'s particles, which were drawing
slot `0x2A`'s flat grey noise instead of slot `0x2B`'s round spark. The port
never reads that field for sprites — they carry the slot the cache handed them
— so the disagreement only decides these two effect paths.

The port steps the pool in the draw phase, where `FUN_002192c0` runs it, rather
than beside the actor loop where `DAT_00355620` steps; the original really does
run the two in different halves of the frame. It hands the four world-space
corners to the scene's own projection instead of projecting each to integer GS
coordinates, which is the faithful analogue here: unlike the sprite pass, whose
corners are *built* in GS integer units around a truncated origin, these are
built in world space and only meet the projection at the end.

`--actor-report` grew a `hit sparks:` line — how many are alive and how many
groups are busy, which is also how many hits are currently showing.

### The sword trail

`FUN_0020e840`, the green ribbon the blade leaves as it swings. Ported into
`ported/render/original_weapon_trail.{h,cpp}`, with the model side in
`ported/model/psc3_model.{h,cpp}`. Written up in
`analyzed/weapon_motion_trail.c`.

Finding it is most of the work, because it is **not** one of the standalone
effect systems `FUN_002192c0` runs. Every one of those has a gate global, and
in the `sword_trail` save state every one of them reads zero while the trail is
plainly on screen. `FUN_0020c810` calls this last, per entity, after the bone
palette is composed and after `FUN_0020eec0` has worked out the entity's depth
bucket — a motion trail is part of a model's own draw, and its state lives in
the entity and in a static pool at `0x004FBC7C`.

The low eight bits of entity `+0xAA` — the halfword the animation stepper
stages out of the current keyframe — are an **enable mask**, one bit per
descriptor in the model's header `+0x38` table. `+0xB0` holds last frame's copy
so an edge either way is seen, and `+0xB1..+0xB8` hold one-based handles into a
pool of 32 slots. A descriptor is a colour, two vertex indices and a sample
count. `grp_0179`, the sword blade, carries two of them — a bright pale-green
ribbon over three samples and a darker one over four, both on the same pair of
blade vertices — and its **animation 0** turns both on. Animation 0 is the long
phase after the six-frame spawn flourish, the one that also runs the swept hit
test, so the trail is alive for exactly as long as the blade can hit something.

Both named vertices are skinned by their own bones, through the same vertex
stream and the same matrix buffer the mesh draw uses, and the pair goes into a
16-deep history with the newest at index 0. Each frame the newest `sampleCount`
pairs — clamped to `[3, 16]` and then to what has been recorded — become the
control points of a **natural cubic spline**, one per edge, resampled at twelve
points and stitched into eleven quads whose alpha ramps from the descriptor's
own down to a twelfth of it.

Two things are worth knowing about the solve, `FUN_00266460`. Its scratch array
at `[17]` means two different things at two different times — the forward sweep
overwrites each divided difference with the eliminated diagonal, and the back
substitution divides by *that* — and it reduces the last row twice, which is
harmless only because the term it subtracts is the coefficient the natural
boundary condition already set to zero. `FUN_00266668`'s coefficient is a third
of the textbook one and its powers are arranged to match, so the two are only
right together; both are ported as written rather than normalised.

**This is the only untextured primitive in the executable.** `FUN_0020e840`
writes `0xFFFF` into the packet's texture halfword; `FUN_00207de8` increments
that field before use, so it arrives as zero and takes the branch that emits
PRIM `0x0D` — a gouraud triangle fan with TME clear — instead of `0x1D`. That
same untextured branch halves only the packet's *alpha* on the way to the GS
and leaves the rgb alone, which is why the descriptor's rgb is free to run over
`0x80`: `grp_0179`'s first trail is 1.95x on green. `SpriteQuad` grew an
`untextured` form with a colour per corner to carry it, since the ramp is the
whole of the fade.

`--actor-report` grew a `motion trails:` line — how many pool slots are held
and the allocation mask itself, which is `0x3` mid-swing in both the save state
and the port.

### What is not ported

Called out at their sites: `FUN_00215670` (a third hit-test
form, used by enemy attacks), `FUN_002d5630` (the HP bar, gated on
`DAT_003555D3`, which is zero in both dumps), `FUN_002d59c0` (the hit cue, on
the sound engine's priority channel that the port does not reach), and the
`FUN_0025ba98` branch of the resistance lookup, which no attacker in this scene
can reach because a streamed prop's `+0x02` is not in the `0x2048` candidate
mask.

## The screen smear

`FUN_00201a38` draws the *previous* frame back over the current one,
alpha-blended. It is what makes the camera look distorted and blurry on
impacts — the lightning strikes in the ship scenes, Volcan's sword swing, a
falling crate. Ported into
`ported/render/original_frame_feedback.{h,cpp}`.

The two opcodes that drive it were documented as something else entirely.
`analyzed/opcode_dispatch_tables.md` called `0xC8` `set_text_color_index` and
`0xC9` `set_text_color_index_and_palette`, and the port consumed both as
`operands-only` on that basis. Neither touches text:

```
0xC8 FUN_00264448  DAT_00355661 = expr                       blend alpha
0xC9 FUN_00264470  same, plus five shorts at DAT_00343878    dx, dy, sx, sy, rot
```

Nothing in the dialogue or menu renderer reads `DAT_00355661`. Its only readers
are `FUN_002000c0:214`, which gates the whole effect on it —

```c
if ((DAT_00355661 != '\0') || (DAT_00354b88 != 0)) FUN_00201a38();
```

— and `FUN_00201a38` itself, which uses it as the blend alpha. The global wears
four different Ghidra names across the decompilation (`DAT_00355661`,
`uGpffffb6f1`, `bGpffffb6f1`, `cGpffffb6f1`), which is part of why the
connection was easy to miss.

### The source is a framebuffer, not a copy

`FUN_002f9620` builds `TEX0` with `TBP0 = DAT_00354C2C * 0x8C0`, `TBW` 10,
`PSM` 1 (`PSMCT24`). `DAT_00354C2C` is the parity of the buffer *not* being
drawn into, so `TBP0` names the frame the player is currently looking at. `TBW`
10 is 640 pixels and the two bases (0 and `0x8C000`) are `640*224*4` apart, so
the frame is one 224-line field. `FUN_00201a38` then clears `TCC` out of the
word it was handed, so the sampled alpha is discarded and `RGBAQ`'s is used.

The primitive is one alpha-blended textured tri-fan over the whole screen:

| register | value | meaning |
|---|---|---|
| `PRIM` | `0x155` | `TRI_FAN \| TME \| ABE \| FST` |
| `ALPHA_1` | `0x44` | `(Cs - Cd) * As + Cd` |
| `RGBAQ` | `alpha << 24 \| 0x808080` | `0x80` is unity under `MODULATE`, so only alpha does work |

Alpha is a GS blend factor over **128**, not 255. `0x80` replaces the frame
outright; the `0x7E` the ship scenes use is 98%. And because each frame
re-samples a frame that already contains the previous blend, it compounds — the
trail is exponential, not a single ghost. That compounding is the effect, and
reproducing it is the one ordering constraint on the port: the capture has to
happen *after* the frame's own quad.

The packet head-inserts at `DAT_7000000C + 0x10064`. `FUN_00207de8`'s tail shows
the bucket stride is `0x10` with the head pointer at `+4`, so that is sort
bucket **`0x1006`** — under the letterbox bars and the fullscreen fade (both
`0x1007`) and under every text overlay (`0x1009`). The smear covers the world,
and the fade tints the smear.

### The ramp that almost never runs

`DAT_00355661` is the target and `DAT_00354B88` the current level, and the head
of `FUN_00201a38` steps one toward the other by 1 a frame, drawing nothing below
2. Except that the very first test is `if (current == 0)`, and that branch takes
the target verbatim and never ramps. Only opcode `0xBE`'s table entry 12
(`sh a0,-0x53e8(gp)`) ever seeds a non-zero current level, so **every script use
of `0xC8` gets exactly the alpha it wrote**. `s01_e012` snaps it to `0x7E`, then
re-issues `0xC8` every frame with the current value of an `0x90`/`0x91`/`0x92`
parameter ramp decaying to zero, and writes 0 to clear. The script's own ramp is
the fade-out; `FUN_00201a38`'s is dead code for it.

### Only the destination moves

The base quad is ±5104 × ±1776 in GS 12.4 fixed point (±319 × ±111 pixels) and
carries UVs for the 1..639 × 1..223 rectangle of the source. Untransformed that
is a 1:1 copy, which pins the two origin constants the function adds last:
`0x7FF8` and `0x7FFE` *are* the screen centre, and the `0x7FF6` variant is the
same point half a line up for the other interlace field. The port never needs
the GS `XYOFFSET` — centre plus `X/16` is the same answer with one fewer
constant to be wrong about.

`0xC9`'s five shorts then transform that quad, and **not** the source rectangle:

| global | step | effect |
|---|---|---|
| `DAT_0034387C/E` | `v * (s + 1024) >> 10` | zoom blur |
| `DAT_00343880` | rotate by `v/10` degrees | swirl |
| `DAT_00343878/A` | translate | directional smear |

All zero gives a plain ghost. `fGpffff8010` is `0.017453289` — π/180 — so the
rotation is in tenths of a degree. The `y` term is doubled going into the
rotation and halved coming out: the quad is 224 lines over 640 pixels, so a line
is worth two pixels and a 90° spin would otherwise come out squashed to the
field's aspect. `FUN_0023c340`, the battle/spell path, writes these globals
directly with scales of `0x32`, `0x78` and `-70` — that is the impact punch.

### What the port does instead

There is no framebuffer to name in `TEX0`, so the game's picture is copied out
of the back buffer into a texture once per presented frame, at the end of the
composite — after the world, the smear, the bars, the fade and the subtitles,
and before the harness's own overlays. On the console the buffer would also hold
`FUN_00268270`'s debug text, but the port's overlays are the harness's and would
smear a HUD the game never drew.

Three things that are easy to get wrong here, all of which cost a build:

- **The capture is unconditional.** Gating it on the effect being up leaves the
  frame a script *arms* the smear on with nothing to sample — the console always
  has the other framebuffer sitting there, and there is no lookahead that would
  let the port know one frame early.
- **Match the framebuffer format.** `glTexImage2D` with `GL_RGB` makes
  `glCopyTexSubImage2D` convert every pixel. Measured on `s01_e024` at
  1280×960, `--render-bench 8`: 4.0 ms a render with no capture at all, 4.4 ms
  with `GL_RGBA8`, 5.15 ms with `GL_RGB`. Same pixels out of all three.
- **`v` flips.** GS `v` counts down from the top row; a texture copied out of
  the back buffer has its origin at the bottom.

The texture is power-of-two and larger than the window with the used fraction
tracked separately, because a fixed-function context is not promised
`GL_ARB_texture_non_power_of_two`.

`--no-screen-smear` skips both the quad and the capture, so the same frame can
be captured with and without it — which is how this was confirmed to reach
pixels at all rather than merely to be "implemented". On `s01_e012` frame 2060,
during Volcan's sword swing, 483,516 of 691,200 pixels differ between the two;
on frame 1900, outside both bursts, the two captures are byte-identical. The
difference image is a fan of ghosted blades trailing the sword and nothing at
all on the static geometry, which is the signature a 1:1 feedback blend should
have.

`--scr-report` prints a `screen smear` section: how many frames the alpha was
written on, how many were non-zero, the peak, and whether any caller reached for
`0xC9`'s transform.

## The dynamic point lights

The sixteen-slot table at `DAT_00343888` and the falloff that consumes it are
both ported. Opcodes `0xBF`/`0xC0` allocate, `0xC2` alpha, `0xC3` colour, `0xC4`
radius, `0xC5`/`0xC6` position, `0xC7` release. The *radius* field is
simultaneously the light's extent and the allocator's free-list marker, which is
why a memory dump shows plausible positions and colours in slots nothing is
using.

`FUN_0020b430` compacts the live slots into a VU0 list — count at quadword 2,
then `{position, (r, r², 1/r²), colour/255}` per light — and two different
consumers read it:

- **Map draws** run the whole list per vertex (`_vcallms(0xe0)` falling into the
  loop at VU0 `0x52`). The result reaches VU1 only through the second additive
  term at `0x1da..0x1e0`, `extra * colour / 128`.
- **Entity draws** resolve table slots **0..2** — not the three nearest — into
  VU1's directional lights 1..3 (`FUN_0020eec0`, VU0 program `0x33`), and sum
  everything from slot 3 up into a flat per-entity tint (`_vcallms(0x220)`).

That split is what the two allocators are for, and it makes the dispatch table's
names backwards: `0xC0` allocates from slot 0 and is the one that can become a
real directional light on a character; `0xBF` allocates from slot 3 and can only
ever tint one. Both still light the map per vertex.

The loop itself, off VU0 `0x52..0x79`:

```
reject unless |light - point| < r on every axis   (two SUBx, FMAND 0xE0)
reject unless |d|² < r²                           (SUBy.w, FMAND 0x10)
accumulate colour * clamp(1 - |d|²/r², 0, 1)
min the sum to 2.0, multiply by 127.5, truncate
```

Both rejections are strict — a sign flag is set only for a negative result.
Program `0x33`, the per-entity one, has *no* rejection tests at all; the clamp is
what turns a light the entity stands outside of into black.

**Confirmed end to end against a save state**, not fitted. `vu0Memory.bin` taken
during the Dortin scene holds count 1 and
`(5.498, -2.684, -0.468) / (2, 4, 0.25) / (0.502, 0.502, 0.502)`, and the
script's own table slot 3 reads position `(5.498, -2.684, -0.468)`, colour
`(128,128,128)`, radius `2.0`. The port's `--scr-report` prints the same slot
independently. That also resolved the packing question `docs/vu1_microprogram.md`
had left open: the three parallel scratchpad runs are staging, and the `VSQI`
loop at the tail of `FUN_0020b430` interleaves them into the stride-3 form.

In `s01_e012` slot 3 is live from frame 1409 to 5440 — it is the lantern over the
shop counter, and the counter, shelving and back wall visibly pick it up. Slot 0
carries the second colour ramp in four ~60-frame bursts plus a window from 9477
to 10426.

Cost, measured with `--render-bench 6 --screenshot :2200` so both runs step the
scene identically block for block:

| | map | entities |
|---|---|---|
| no light live | 0.309 → 0.312 ms | 2.562 → 2.575 ms |
| one light live | 0.687 → **0.762** ms | 3.111 → **3.202** ms |

So ~0.17 ms/frame while a light is live, out of ~5 ms, and nothing measurable
otherwise — the per-vertex path early-outs on the light count. `s01_e024` and
`s01_e012` before frame 1409 render **byte-identically** with and without it.
`--lighting-no-points` turns it off for A/B. It defaults **on**, because the VU0
list was read back out of a save state and matched the script's table exactly
rather than being derived from the microprogram alone. So does the light floor
below; `--lighting-unlit` is the one lighting behaviour still off by default.

### The lantern's light pool: the unlit flag, and the GS's missing octave

A GS dump plus a save state of s01_e012's shop settled two things at once and
**corrected an earlier wrong call**, so the retraction comes first.

**Retracted: the per-material light floor.** VU1 `0x01d1`'s `MAXz` is real and
the port still implements it, but it is back **off**, behind `--lighting-floor`.
It was briefly turned on because a hardware wall patch appeared to confirm it --
and that patch sat *inside the lantern's light pool*. A global lift fitted to a
local effect. With the pool drawn properly the floor only overshoots: at the
matched frame the wall away from the lantern is (30, 22, 16) on hardware,
(28, 22, 17) without the floor and (32, 24, 18) with it.

**Pinning the frame first.** The save state's camera is
`DAT_0058c0a8 = (3.5337, -3.3193, -1.4797)`, the port's f3000 camera to four
decimals -- but `uGpffffb700` is `0x002a170c`, light 0 = (42, 23, 12), while the
port at f3000 sits at the ramp's peak, (118, 78, 59). The scene light ping-pongs
on a ~120-frame cycle, so *any* unmatched frame is a brightness argument about
nothing. Port **f2949** emits (42, 23, 12) and f2950 emits (43, 24, 13), which is
what VU1 `0x2a3` holds -- the EE word and the VU upload are one frame apart, and
the port's ramp is frame-exact. Everything below is f2949.

**The pool is twelve unlit decals.** In the dump the pool is a fan of gouraud
quads whose bright corner carries the vertex colour **(254, 155, 122)** at alpha
31. The lighting model cannot produce that: with this frame's ambient
(10, 30, 50) and light 0 (42, 23, 12), the largest vertex colour it can emit is
`255/256 * (52, 53, 62)`. Anything above 128 in a channel is an unlit draw, and
the frame has exactly 26 of those out of 3556 -- the authored glow decals and
nothing else. `--lighting-unlit` is therefore **on by default** now, with
`--lighting-no-unlit` to A/B it. The map primitives are `#1121`-`#1128`, `#2233`
and `#2234`, all `flags=0x22044` (bit `0x2000` set) with `slot0 a=0x1f f=0x20`.

**And GL was throwing away half of it.** The GS's texture function is
`(Ct * Cv) >> 7`: a vertex colour of `0x80` is x1.0 and `0xFF` is **x1.99**, so
the GS can brighten a texel. `GL_MODULATE` clamps both operands to 1.0 and can
only darken, so (254, 155, 122) came out at x1.0 -- half the pool. The draw paths
now scale vertex colours by 1/256 instead of 1/128 and put the factor of two back
with `GL_COMBINE` + `GL_RGB_SCALE 2` (`applyGsTextureEnv`). That also moves the
clamp to where the GS has it: the old code clamped `c * modulator` at 128, and
the GS clamps at 255 and *then* shifts by 7.

The two together, across a horizontal band of the wall:

| x | 160 | 200 | 240 | 280 | 320 | 360 | 400 | 440 | 480 | 520 |
|---|---|---|---|---|---|---|---|---|---|---|
| hardware | 12.7 | 19.4 | 24.2 | 35.5 | **48.2** | 42.8 | 35.2 | 43.6 | 34.8 | 28.2 |
| before | 11.8 | 15.4 | 9.9 | 13.5 | 18.1 | 15.1 | 23.5 | 22.0 | 22.3 | 23.6 |
| after | 12.9 | 19.6 | 18.4 | 29.7 | 42.5 | **47.9** | 54.7 | 44.9 | 33.1 | 26.6 |

The pool's position, width and warmth now track. Its peak is about one sample
right of hardware's and ~15% high, which is not chased further here.

**Two things the dump settled in passing.**

`TEST_1` is `0x5000d` on 3554 of this frame's 3556 draws. **Corrected later:**
this file first read that as `ATST` GEQUAL against `AREF` 0 -- a test that never
rejects -- and built two open questions on top of it. `ATST` is bits 1..3, so
`0xd` is `110` = **GREATER**, not `101` = GEQUAL. `ATE` on, GREATER, `AREF` 0,
`AFAIL` KEEP is exactly `glAlphaFunc(GL_GREATER, 0)`: the GS discards alpha-zero
texels and nothing else. See the chains section.

The four VU1 register blocks at `608 + mode*3`, read straight out of
`vu1Memory.bin`, are exactly what this file already claimed: mode 0 `ALPHA 0x44`
ZMSK 0, mode 1 `ALPHA 0x44` ZMSK 1, mode 2 `ALPHA 0x48` ZMSK 1, mode 3
`ALPHA 0xa1` ZMSK 1. The decals draw as mode 2, matching `f=0x20`, so
`mapBlendMode` is correct. A first pass at the dump appeared to show mode 1, and
that was a parser bug worth recording: VU1 emits the *next* draw's A+D block
ahead of its GIF tag, so reading the registers at batch-flush time shifts every
blend mode by one draw.

**Cost.** The 20000-frame `--actor-report`/`--scr-report` is byte-identical.
s01_e012 f6000 moves 6% of pixels, mean 26.51 -> 27.26. s01_e024 moves more than
the pixel count suggests -- 50% of f600, but the mean only goes 55.51 -> 55.96 and
the visible change is the chest lid plus a slightly bluer floor. That is the room
where the old clamp point was actually biting: e024's modulator exceeds 1.0 in
blue, e012's never does.

### The lantern hung a quarter unit too high

Same save state, a different bug, and the cheapest kind to confirm: the entity
pool is at `DAT_0058beb0` with a `0x1D8` stride, type at `+0x00` and position at
`+0x20`, so hardware's answer can just be read out of `eeMemory.bin` and diffed
against `--actor-report`.

Across the whole scene, **two** entities disagreed. One was the hanging lantern:

```
slot 40  port 0x0299 (4.0, -1.4, 0.25)   hw 0x299 (4.0, -1.4, 0.0)
slot 35  port 0x02b7 (1.74, -2.21, -0.96) hw 0x2b7 (1.737, -2.205, -1.0)
```

s01_e012 has 25 of these lanterns and they all sit at z = 0. Only the one at
(4, -1.4) was lifted, because only that one hangs over a shelf: primitive
`#2223`, an upward face at z = 0.25 whose `+0x00` carries the `0x800`
terrain-sample bit, so the ground scan accepts it. Nothing wrong with the scan.

The fault was in the placement spawn. `FUN_0025e7c0` writes the record's z into
`+0x28` and `+0x4C` and stops -- **it does not sample terrain** -- and the port's
comment said exactly that, immediately above a line that wrote the sample into
`+0x4C` anyway. The sample is only meant to tell `--scr-report` whether the
placement landed on anything. Hardware confirms it from the other side: every
lantern has `+0x4C = 0.000`, and `+0x4C` equals the spawn z for every other
placed prop in the scene, including slot 35's -1.000.

With that line reduced to `spawnRecord.grounded = height.has_value()`, **all 67
reported entities match the save state's pool.** s01_e024 is byte-identical --
the 20000-frame `--actor-report`/`--scr-report` and the f600 frame both -- and
s01_e012 f6000 moves 1.2% of pixels, which is the lantern coming down.

Worth keeping in mind for the rest of the placed props: a prop is only visibly
wrong when it happens to sit over geometry, so this class of bug hides until the
camera finds the one instance that does.


### Cues

`FUN_00228e28` loads 711 eight-byte records out of **SCR.BIN resource 199**:
bank, program, note, volume, cap. `FUN_00205118` loads three banks from
**SND.BIN resources 1, 2 and 3**; bank 0 is where the common sound effects
live. Both files are in the disc root already.

`FUN_00267a80` measures against the **camera**, not the player: silent past 14
units, panned by the angle between the sound and the camera's yaw, with a floor
inside three units that keeps a close sound in both ears while the camera
swings. `FUN_002057c8` then scales by the record's volume and the master.

### What plays, and through what

Nothing here is a per-sound hook. Each of these is the *mechanism* the original
uses, ported once, with the specific sounds falling out of it:

| sound | mechanism | ported in |
|-------|-----------|-----------|
| footsteps | `FUN_00256ff8`: an animation keyframe carrying `0x100`, then the surface table | `ported/entity/original_entity_sound.*` |
| jump | the same surface table, column 2, from `FUN_00256bb8`'s jump branch | the player controller |
| the flying enemies' buzz | `FUN_002cd0a0` retriggering `FUN_002cde50` on a per-entity period | `FUN_002cd0a0_enemy62` |
| the chest lid | `FUN_002d59e0` on animation 5's event keyframe | `FUN_002d1ea8_treasure_chest` |
| the item fanfare | `FUN_00257b10` at `0x00255240` | the chest cutscene's state `0x11` |

**Footsteps are authored into the animation.** `FUN_00256ff8` fires only when
the entity stepped onto a keyframe whose trailing word has `0x100`; in
`grp_0001` the walk animation carries it on keyframes 1 and 3 of four, and the
run on 4 and 9 of ten. `0x200` alongside it distinguishes the two feet, and
picks the dust effect rather than the sound.

The cue then comes from the **surface**: `FUN_00255d88` takes the top nibble of
the collision record's word 1 -- the `terrainFlags` the ground query already
returns -- as a material, indexes `DAT_0031E028[material][kind]`, and
`FUN_00251c80` offsets that by the character's class. Orphen is class 0, base
`0x3F`, so on material 0 he walks with cue 63 and runs with 67 -- 0.14 s and
0.13 s of waveform. Walking around `s01_e024` reaches materials 0 and 3.

`+0x04` bit `0x1000` gates it, and `FUN_0022a418:204` sets that bit on pool
slot 0 and nowhere else -- so **only the lead player has footsteps**, which is
also why `FUN_00256ff8` is only ever called from player states. The port was
not setting `+0x04` on the lead player at all, which is what
`FUN_0022a418_stamp_lead_player_flags` now fixes; the party members' `0x00A4`
correctly stays silent.

The buzz is one cue retriggered: type `0x62` rolls a period of 24..31 frames
once into `+0x1C6` and fires cue `0x196` whenever the *global* frame counter
divides by it. The waveform is 1.3 s long, so the repeats overlap into a drone,
and because the test is against the frame counter rather than a per-entity
timer, a group of them beats against itself. **Porting that roll shifts the
RNG stream**, so the enemies and the bandana land in different places than the
pre-sound baseline -- the original consumes `FUN_00216868` there and the port
previously did not.

### Waveforms

Bank 0 is a standard Sony VAB, version 7, 14 programs. Its programs are **key
split** -- every tone has `min == max`, so the note picks the waveform rather
than transposing one. Program 11 note 60 is waveform 13 and program 0 note 67
is waveform 54, and their centre notes put them at 11027 Hz and 22055 Hz
against SPU2's 48 kHz pitch base. A VAB records a sample rate as a centre note.

**The tone's `shift` is added, not subtracted:**

```
rate = 48000 * 2^(((note - centre) * 128 + shift) / (12 * 128))
```

The field reads like a fine tune *on* the centre note, which would subtract it
-- and `shift` is 69 almost everywhere, so getting that backwards puts
everything 1.08 semitones flat. Two checks say added: 86 of the 280 resolvable
cues then land within 1% of a standard authoring rate against 9 the other way,
and five of the SPU2 voice pitch registers in a PCSX2 savestate are reproduced
*exactly* against none. Cue 8 lands on 22055 Hz and cue 159 on 11027, which are
22050 and 11025 to within a cent.

The decode was checked against a PCSX2 savestate: **SND.BIN resource 1's body
section is byte-identical to the 241504 bytes at SPU2 RAM `0x19000`**, and its
header matches the copy at IOP RAM `0x93900` apart from the `ProgAtr` block,
which libsnd rewrites when it opens a VAB. `0x93900` is also what `DAT_00355A1C`
holds in the EE dump, so what the EE calls a bank's "SPU address" is an IOP one.

### Running it

Audio opens on any windowed run, capture runs included -- the mixer is on its
own thread and cannot reach anything a capture compares, and two captures of
the same frame still hash identically with it open. `--no-audio` turns it off.

Headless runs never open a device. To hear one anyway:

```
orphen_port --disc-root . --scene s01_e024 --frames 620     --spawn -4.5,-10.5,0 --press-confirm 60,440     --sound-dump out/sfx/chest_cutscene.wav --sound-report
```

`--hold-stick <angle>,<magnitude>` drives the analog stick for every headless
or capture frame, which is how the footsteps get exercised without a pad --
magnitude is the original's 0..128 and above 100 is a run:

```
orphen_port --disc-root . --scene s01_e024 --frames 400 --spawn -4.5,-10.5,0 --hold-stick 1.0,60 --sound-dump out/sfx/walk_and_buzz.wav --sound-report
```

`--sound-report` prints every cue with what it resolved to, which is how the
C++ path was checked against an independent Python decode -- both land on
waveform 13 at 18704 samples and waveform 54 at 50652 samples, same rates.
`--sound-dump` renders one frame of mixer output per simulation step; the two
bursts in that WAV start at frames 166 and 391, matching the cue log.

### What is not ported

- **Absolute loudness.** The chain reproduces the game's relative volumes, but
  nothing models the IOP's own master, so the overall level is a guess.

## Music, and where every note of it was hiding

The port played sound effects and no music, and the reason was one section of
one file.

A SND.BIN bank resource has three sections (`FUN_00205548`). The port read
section 0 (the waveforms) and section 1 (the VAB header). **Section 2 is either
the literal bytes `NSEQ` -- the marker for "no sequence here" -- or a real Sony
`SEQp` chunk.** The three banks `FUN_00205118` loads at boot are exactly the
three that are `NSEQ`, so a sound-effect-only implementation reads every bank
that has no music in it and none of the banks that do.

### A scene asks for its own music

`FUN_0025b2f0` copies 16 bytes -- eight `u16` requests -- out of **scene script
header word 10**, and `FUN_00206840` acts on them. Slot *i* draws from music
category `min(i, 2)`, the low 15 bits index that category's table, and **bit 15
means "start it now"**. Everything else is loaded ready for a later opcode.

The tables themselves come from SCR.BIN resource 199 -- the same resource the
sound-effect cue table comes from, built in the same pass by `FUN_00228e28`.
Header word 7 points at three offsets, one per category; each is a run of
eight-byte records ending at the first with a zero resource id. Retail has 51,
33 and 201 records, which is exactly what `DAT_00354c00`'s bounds check expects.

For `s01_e012`:

```
slot 2  0x801a  cat 2 index  26  -> SND resource 112  vol 70   PLAY
slot 6  0x007f  cat 2 index 127  -> SND resource 213  vol 80
```

Slot 2 is the only one with bit 15 set, and it is the wind that runs under the
whole scene. Slot 6 is started later by the script: opcode **0x129** at blob
offset `0x5e56`, `(slot 6, fader 1000)` -- the piece under Sephy's scene.

### The wind is four notes long

SND resource 112's whole sequence, decoded by hand:

```
02  c0 00       program change ch0 -> program 0
02  b0 07 7f    CC7  volume 127
02     0a 40    CC10 pan 64
02     0b 7f    CC11 expression 127
02  90 3c 64    note on ch0, note 60, velocity 100
81 3e (Δ190)   b0 63 14   CC99 = 20    loop start
01              06 7f     CC6  = 127   forever
86 01 (Δ769)    63 1e     CC99 = 30    loop end
00  ff 2f 00    end of track
```

One held note. Looping is Sony's controller convention (CC99 = 20/30 around the
loop, CC6 for the count) rather than anything in the header.

The *sustain* is not in the sequence at all -- it is in the PS-ADPCM flag byte,
which the decoder had been discarding. Bit 2 marks the block a repeat returns to
and bit 1 says the end block repeats. **The trap:** a one-shot's terminator is
`0x07`, which sets bit 1 *and* points the loop at its own final block. That is
the hardware spelling "stop", so a loop that starts inside the last block is not
a loop, and treating it as one makes every sound effect stutter forever.

### ADSR stopped being optional

One-shots never needed an envelope: they run a waveform to its end block and
stop. A held sequencer note has no end block to reach, so the envelope is the
only thing that ever ends it. `AdsrEnvelope` runs the SPU's own algorithm --
`1 << max(0, (rate >> 2) - 11)` samples between steps of
`step << max(0, 11 - (rate >> 2))`, exponential rise slowing 4x above `0x6000`,
exponential fall scaling by the current level.

### The fader is 0..1000, not 0..127

`FUN_00206048` keeps two numbers per slot: the record's volume byte (0..127) and
a fader running 0..1000. What reaches the sequencer is `fader * base / 1000`, so
a fader of 1000 means "this slot's authored volume" -- which is the 1000 that
both `FUN_00206840` and opcode 0x129 pass. `FUN_002063c8` (0x12A, up) and
`FUN_00206260` (0x12B, down) ramp it, over a frame count worked out from the
0..127 delta rather than the fader delta.

### The reverb, and the table that was never in the EE image

A music record is eight bytes. `+0` is the SND resource and `+2` the volume;
`+4` is a **reverb type** and `+6` a **reverb depth**. `FUN_00205938:90-113` is
their only reader:

- `+4 >= 0` sends the type to the IOP as command `0x7314` with `type | 0x100`,
  then command `6` with 1 to turn the effect on (0 when the type is 0).
- `+6 & 0xFFFE` goes out as `FUN_00204ca8(10, depth << 8, depth << 8)` -- one
  effect volume per channel.

Both are cached in `sGpffffbab4` and `sGpffffbab6`, so a slot asking for what is
already set resends nothing, and a `+4` of `-1` skips the block outright and
leaves whatever the last scene chose. **78 of the 285 music records select a
preset** -- 26 of 51 in category 0, 28 of 33 in category 1, 24 of 201 in
category 2. (An earlier count here said 71, which was the type-4 records alone.)

#### The coefficients are on the disc, not in the ELF

The EE never holds a single reverb coefficient. It forwards a type number, and
the table that turns that number into hardware registers lives in the game's own
IOP sound driver, `cdrom0:\RSPU2DRV.IRX` -- which is on the ISO but was never in
the flattened disc root here. `scripts/extract_spu2_reverb_presets.py` pulls it
out of either, and generates `port/src/ported/sound/spu2_reverb_presets.h`.

Two tables in the module's `.data`:

- **0x16390** -- ten `u32` buffer sizes in eight-byte units, `0x4D8` through
  `0x780`. These are the stock Sony sizes: `0x4D8 * 8` is room's `0x26C0`.
- **0x163C0** -- ten `0x44`-byte entries, `{ u32 fieldMask; u16 params[32] }`.
  The mask is zero in every shipped entry, which the driver reads as "write
  every field".

So the modes are the familiar ten: 0 off, 1 room, 2-4 studio small/medium/large,
5 hall, 6 space echo, 7 echo, 8 delay, 9 pipe. `s14_e031`'s type 4 is **studio
large**. Across the whole game only 4 (x71), 3 (x4), 5 (x2), 2 (x1) and 0 (x2)
are ever asked for.

The driver's writer at `0xF6F0` walks the entry front to back against ascending
register indices, which is what pins the field order down with nothing left to
guess: `+4`/`+6` are `APF1_SIZE`/`APF2_SIZE`, `+8` through `+0x16` are the ten
volume coefficients at `0x774`-`0x782`, `+0x18` through `+0x3E` are the twenty
delay-line addresses at `0x2EC`-`0x338` in order, and `+0x40`/`+0x42` are
`IN_COEF_L`/`IN_COEF_R`. Addresses go out as `value << 2` -- eight-byte units to
16-bit words -- and `ESA` as `(memtop - (size * 8 - 2)) >> 1`, so the buffer is
exactly the PS1's byte geometry and a preset unit is four samples.

#### Routing is per tone, and the flag is the VAB's

A VagAtr's `mode` byte at `+1` is **4** for a tone that feeds the effect bus and
**0** for one that does not. Those are the only two values anywhere in the game.
The three boot banks are 400 tones of solid 0, which is why sound effects have
never wanted this, and of the 283 banks the music tables reference only **61**
carry a wet tone at all -- a scene can select a preset and still route nothing to
it, which is exactly what `s01_e012` does.

This is a PS1-shaped VAB, where the per-voice reverb bit is purely an extra
send: a wet tone is still heard dry. `mode` 4 means "and also wet", not "wet
instead".

#### s14_e031

Its track is category 2 index 84 -> SND resource 170, `vol 70, reverb type 4,
depth 60`: a send of `0x3C00` out of `0x7FFF` on both channels, and **11 of its
12 tones at mode 4**. (An earlier note here said every tone in the bank; one is
dry.) The SEQp is seven channels at 48 ticks per quarter and 480000 us per
quarter, so a tick is 10 ms -- and **channel 6 is a note-for-note copy of
channel 3 delayed by 32 ticks**. 512 note-ons against 507, no pitch mismatches,
the offset exactly 32 on every one, the same instrument on both (programs 3 and
6 are a single tone each on VAG 4), and only the channel volume different: CC7
120 against 73. It is an authored slap delay. Wet, it is the tail of a room.
Dry, it was the melody played twice 320 ms apart.

#### What the port runs

`Spu2Reverb` is the documented SPU reverb topology: two cross-coupled IIR comb
lines feeding a four-tap early-echo comb, then two all-pass sections, all of them
reading and writing one delay buffer that scrolls a sample per tick. The
different-side lines cross -- the left one feeds back off the *right* source tap
-- and straightening that out collapses the stereo image, which is how you notice
it has been got wrong. Field names in the code are the driver's, so `sameLDst`
is the address the hardware's `SAME_L_DST` holds.

Two deliberate departures, both audible only under a null test: it runs in float
where the hardware is 16-bit fixed point with a saturating multiply, and the wet
return is linearly interpolated across the sample pair a tick covers rather than
resampled through the hardware's FIR. It does tick at half the output rate, as
the hardware does.

Measured on `s14_e031`, 15 s of `--music-solo` at frames 900, wet against
`--no-reverb`:

| | dry | wet |
|---|---|---|
| RMS | 5885 | 6758 |
| peak | 24119 | 30526 |
| L/R correlation | 0.858 | 0.632 |

The difference signal is 3278 RMS, a little over half the dry level, which is
where a `0x3C00`-of-`0x7FFF` send should land. Nothing clips and the IIR does not
run away. Correlating that difference against the dry mix gives no dominant
single lag -- a diffuse `+-0.1` out to 640 ms, with one `+0.18` bump at 320 ms,
which is the authored slap delay showing through the tail rather than the reverb
producing an echo of its own.

The simulation is untouched: `--frames 600 --actor-report --scr-report` on
`s14_e031` is byte-identical to the same report from the build before this
change, and the `--music-solo` dumps for `s01_e024` and `s01_e012` hash the same
before and after -- the second of those despite selecting studio large at depth
30, because not one of its tones asks for the bus.

One edge the shipped data never reaches: a preset selected at depth zero would
freeze the delay buffer here where the hardware would keep circulating it. No
record in the game does that.

### Scene-streamed sound effects live in the music banks

A cue record's byte +7 is **not** a bank id. `FUN_002057c8:56` hands it to
`FUN_00205778`, which searches the *music slot requests* (`DAT_00356a18`,
**indices 2..7**) for the one whose low 15 bits match, and returns `slot + 3` —
the bank index. So a "scene-streamed" effect is not a separate system: it plays
out of a music slot's VAB.

In `s01_e012` cues 677..680 carry `+7 = 127`, slot 6 requests category-2 index
127, and that is SND resource 213 — the same bank whose *sequence* is the cue
under Sephy's scene. Its **program 4** holds Volcan's sword: draw, sheathe and
flourish on notes 60/61/62, plus a hard-panned stereo pair on 63. They come out
at 22057 Hz, which is 22050 to within a cent — a good check on the pitch maths.

### Pool slot 1 is the camera, and that is how a sound plays non-positionally

`FUN_00228e28:203-210` builds slot 1 at boot: type `0xFFFF`, position zeroed,
`+0x4C`/`+0x50` pinned to `-60` so the floor never touches it. Those writes are
spelled as `DAT_0058c088`, `DAT_0058c0a8..b0` and `DAT_0058c0d4/d8`, which are
that slot's `+0x00`, `+0x20..+0x28` and `+0x4C`/`+0x50`.

So **`DAT_0058c0a8` — the listener `FUN_00267a80` measures against — is slot 1's
position.** A script cue aimed at selector 1 plays *at the listener*: distance
zero, full volume, dead centre. That is the engine's idiom for "this sound is
not positional", and `s01_e012` uses it for the storm's thunder (cues 586/587)
and for Volcan's sword (677/678/679).

Leave that slot empty and every one of those cues is measured from the world
origin instead. The sword still happens to play, because the Dortin/Volcan
camera sits ~5 units from the origin — but panned wrongly (`62/13` instead of
`60/60`). The thunder does not, because the establishing shot puts the camera at
`(0, 60, 0.5)`, 60 units out, past `FUN_00267a80`'s 14-unit cutoff.

**The pool stride is `0x1D8`, not `0xEC`.** `FUN_0025d6c0` reads
`&DAT_0058beb0 + selector * 0xEC` where the pointer is `undefined2 *` — 0xEC
*halfwords*. `analyzed/entity_pool_and_descriptors.c` has always said so; this
section exists because an ad-hoc dump script here used 236 instead of 472 and
made slot 1 look empty in a save state where it plainly is not. Read that file
before indexing the pool by hand.

`--sound-report` prints the source and listener positions for exactly this
reason: `src(0,0,0)` on a cue that should be centred is the signature.

### A VAB program layers, and getting that wrong sounds like a stuck note

A program is a *set* of tones, and keying a note sounds **every** tone whose
range covers it. Returning the first match is silently wrong, and this data
layers constantly — SND resource 2 has 631 overlapping tone pairs.

The one that gives itself away is the wind. SND resource 112's program 0 holds
**two** tones over the same 0..120 range, at **pan 0 and pan 127**: a stereo
recording split into two mono waveforms. Play only the first and the ambient bed
is mono and hard left — right-channel RMS measures exactly `0.0`. It stops
sounding like wind and starts sounding like a held note.

That is worth knowing as a symptom: *"a note hangs until the scene ends"* was
this, not a sequencer fault. The loop machinery was working the whole time (the
wind takes 27 loops in a 13,000-frame run). Confirm with:

```
orphen_port --disc-root . --scene s01_e012 --frames 600 --music-solo \
    --sound-dump out/audio/wind.wav
```

`--no-reverb` holds the effect bus off whatever the scene's music record asks
for, so a wet track can be dumped twice and differenced. It is a divergence by
construction -- diagnostic only.

`--music-solo` mutes the effect pool and the voice line so a dump holds only the
sequence slots — dialogue is centred and full-scale and buries the music in any
measurement of the whole mix.

### The meta encoding is not standard MIDI's

A tempo change is **`FF 51 <u24>` with no length byte**, where a .mid writes
`FF 51 03 <u24>`. Read the first tempo byte as a length and everything after it
desynchronises.

Across all 283 sequences in the game only two meta types occur: `FF 2F` in 282
of them, and `FF 51` **four times, all four in SND resource 117**. So anything
else is a parse failure, not a meta to skip — the port now stops the slot and
flags `DESYNCED` in `--sound-report` rather than carrying the damage forward.

Resource 117 is the cue under Sephy's scene: 34.1 s, with a ritardando ending
(50, 49, 46, 44, 42 BPM). Its first tempo change is at tick 875 ≈ 21.9 s, and
the scene plays it for 26.9 s — so the wrong reading plays four fifths of the
piece correctly and then falls apart. That is exactly how it presented: "stops a
few seconds early, then holds a note", because the garbage after the desync keys
notes on that never receive a note-off.

A second bug kept that note ringing. `FUN_00206260` returns early when the slot
state is below 2, because on the real machine the IOP owns the voices and has
already silenced them. The port's voices are its own, and a track that ran to its
end left them in *release* — with a slow release rate they ring on, and the
ramp-down that should have stopped them returned early too. A script asking for
silence now gets silence.

### Sephy's cue: slot 7, not slot 6

Two different pieces play near each other and it is easy to chase the wrong one:

- **slot 7 / SND 117** — frames 6805..8420, stopped by **subproc 1495** at blob
  offset `0x534d`. This is the piece under Sephy's scene.
- **slot 6 / SND 213** — frames 11564..11873, a five-second sting later on.

### Slot 6's five seconds are by design

Worth recording so it is not re-investigated. Opcode `0x129` starts slot 6 at
frame 11564 and `0x12B` fades it at 11873 — 309 frames. The gap is script, not
drift: a `0x90` at `0x5e1d` arms ramp 1 with `current 126, target 0, step 1`, the
`0x91`/`0x01` pair at `0x5e7e` polls it for exactly 126 frames to frame 11690,
then dialogue runs and the fade follows 183 frames later. Every operand checks
out against all five `0x12B` sites (`0x671d` gives slot 2 → fader 500, matching
the frame-13122 scene handover).

So the track never reaches its own loop end at tick 2309 (24 s) — it is stopped
at 5 s. `--sound-report` says so directly: `loops taken 0, end of track not
reached`. The tail after that loop end exists for the case where the loop count
expires: it holds the note-off for the ch0 drone that was keyed at tick 5 and
swelled in over the intro.

### SCR SUBPROC DISP

`FUN_0025b778:22-24` prints one line per occupied object-script slot, before
that slot runs, gated on `DAT_003555dd` bit 7 — the debug menu's
"SCR SUBPROC DISP" entry (`bGpffffb66d & 0x80`; gp `0xffffb66d` resolves to
`0x003555dd` against the `0x00359F70` base):

```
Subproc:%3d [%5d]        0x0034CA60 — slot, then the dword at (body - 4)
```

That dword is the authored subproc id, stored as the `0B 04 <id16> 00 00`
marker in front of every body — the same pattern
`docs/scr_script_assembly.md` scans for. s01_e012 has 292 of them.

The port holds the bit set by default, the same way it holds
`DAT_00355098_positionDisplay_` set: the menu that writes the byte has no way in
here, and these lines belong to the same readout as the position display. `O`
toggles it, `--no-scr-subproc-disp` starts it off.

### `+0x168` is inside the slot, so clearing the slot clears it

Orphen's jaw came apart in the s01_e012 close-up when the scene was **played**,
and never under `--arm-stream d780:13400`. The armed run put the close-up head in
pool slot 72; play puts it in slot **64**, which is where the save state has it
too -- so the allocation was right and the difference was what had been in the
slot before.

A `G` snapshot from play, differenced against the head's palette in the save
state (`0x00357E00 + 64 * 0xA80`), named the bones:

```
bone   port(rel to bone0)          real(rel to bone0)         delta
   1  (  0.000, -0.010,  0.021)   (  0.000, -0.010,  0.021)     0.3 mm
   2  ( -0.000,  0.049,  0.081)   ( -0.000,  0.049,  0.081)     0.1 mm
   3  (  0.000, -0.010,  0.021)   (  0.000, -0.019,  0.061)    41.1 mm
   4  (  0.000, -0.010,  0.021)   ( -0.006, -0.069,  0.053)    67.4 mm
   5  (  0.000, -0.010,  0.021)   (  0.006, -0.069,  0.053)    67.4 mm
   6  (  0.000, -0.010,  0.021)   (  0.000, -0.072,  0.052)    69.4 mm
   8  (  0.000, -0.074,  0.062)   (  0.000, -0.074,  0.061)     0.8 mm
```

Bones 3, 4, 5 and 6 sit at exactly bone 1's position -- bone 3's parent -- and 7
follows because it is 3's child. Everything else is right to under 2 mm.

**`FUN_002cdb28` drives bones `{3,4,5,6}`.** `DAT_00326650` is that list and
`DAT_00326640` the roll angles; it is the wing flap on every type `0x62`, it sets
translation 0 with duration 0, and a bone overridden to zero translation lands on
its parent. One of them had lived in slot 64 earlier in the scene and left its
overrides behind.

In the original that cannot happen, because **entity `+0x168`..`+0x191` -- the 42
per-bone override modes -- are part of the 0x1D8-byte slot**, and
`FUN_00229c40:20` opens with `FUN_00267e78(param_1, 0x1d8)`: allocating a slot
zeroes the modes along with everything else. The port keeps them in a side table
(`EntityBoneOverrides`, so the mode byte and the pose it selects stay together,
which is still the right call) and `entity = OriginalEntity{}` does not reach it.

`EntityPool` now owns a pointer to that table and clears the entry in `reset`,
`releaseSlot` and `FUN_00229c40_initialize` -- the three places it clears the
struct. Reproduced deterministically by planting `{3,4,5,6}` overrides on slot 72
at frame 14000 before the rig builds: without the clear the armed run collapses
to the same `(0.000, -0.010, 0.021)` on all four bones, with it the jaw is
correct to hardware. The armed-stream capture at 15810 is unchanged.

**The lesson is about storage, not animation.** Any per-slot state the port keeps
outside `OriginalEntity` has to be cleared by whatever clears the slot, because
in the original it was never outside it. The pose filter bank at `0x003FFE00` is
the other one, and it is handled separately -- see the `seeded` flag.

### `G`: snapshot the frame you are looking at

The failures worth chasing in this port are increasingly the ones that only
appear when the scene is **played**. The script reaches them carrying state an
armed stream never builds -- s01_e012's close-up is reached with the flood's
subproc 5112 live if you walk in, and not at all under
`--arm-stream d780:13400` -- so there is no frame number to point `--screenshot`
at, and a `--frames` capture photographs a different run.

`G` during play dumps the frame you are on: the text goes to stdout **and** to
`orphen_snapshot_<frame>.txt`, and the framebuffer is written beside it as
`orphen_snapshot_<frame>.ppm`, both in the working directory. One press is one
snapshot even when a slow frame drives several simulation steps.

The report carries, per entity in the draw list, the fields that decide a pose:
`+0xA0` animation, `+0xAC`/`+0xAE` pose column and previous, `+0x13C` blend,
`+0x04`/`+0x06`/`+0x08`, `+0x192`/`+0x194` parent and bone. Then two derived
numbers that are the point of it:

- **`span` / `bind` / `ratio`** -- the posed mesh's bounding box against the same
  model's unposed one. Skinning moves a mesh; it does not treble the size of its
  box. Rows are sorted worst ratio first, so a broken entity is line one rather
  than somewhere in the middle of sixty static props, and anything past 3.0 with
  a real bind box is marked `<<< DEFORMED`. The bind box has to be real for the
  ratio to mean anything: a rope (`grp_001e`, the bandana) is authored with every
  bone stacked at the origin and takes its shape from the simulation, so its bind
  box is 0.044 and it reads 6.06 while posed correctly to 4 mm against hardware.
- **the bone table**, for every attached entity and everything something is
  attached to. Origins are printed relative to bone 0 so they diff straight
  against `0x00357E00 + slot * 0xA80` in an EE dump without subtracting a world
  position first.

A `live but not drawn` line closes it, with `+0x04`/`+0x08`/parent for each, so a
child that `FUN_0020c5a8`'s queue dropped can be told from one that was hidden.

`FUN_0025b778:38-58` has a second loop behind the same printf — the
"SCEN WORK DISP" submenu. Four words of mask at `DAT_0031e770`, one bit per work
word, each set bit printing ` %02d:%d(%X)` (`0x0034CA78`) for
`DAT_00355060[index]`. Only `FUN_0026a508`, the submenu itself, ever writes that
mask, so it stays zero and the loop prints nothing until a slot is turned on.
Ported and inert, which is what the original does.

### A hidden parent must stay "queued", not "posed"

`FUN_0020c5a8`'s second pass has four outcomes per slot, and the status byte it
writes -- or does not write -- is what the *children* read:

```
+0x08 bit 0 set        clear +0x0C bits 0x3000, raise +0x08 bit 0x10,
                       **leave the status byte at 0**
parent < 0             pose and draw, status = 1
status[parent] == 0    push this slot on the back of the queue
status[parent] == 1    pose and draw, status = 1
status[parent] == 0xFF neither; dropped this frame
```

The port had the hidden test inside its publish helper, which returned early --
and then the walk wrote `status = 1` over the top anyway. A hidden slot
therefore read as *posed*, and every child bound to it was drawn.

In s01_e012 that child is **slot 61**, a type `0x2C4` parented to slot 83
(Dortin, `+0x08 = 0x0133`, hidden from the first frame). With no parent palette
to resolve against it fell back to its own `+0x20`, which for a bone-local
attachment is near enough the world origin -- putting the sack Dortin is
supposed to be carrying on the floor of the middle doorway for the whole scene,
and then again in his hand once he was drawn. The EE dump agrees with the port
on every field of slots 61-64, so this was never a spawn or placement problem;
only the draw walk's bookkeeping.

The hidden branch's third write, `+0xB0 = 0`, is the pose blend's "no previous
column" (`FUN_0020e840:60` reads it next to `+0xAA`). The port's pose filter
keeps that in its own state, so there is no field to write.

### Three names in the dispatch tables that are wrong

`analyzed/opcode_dispatch_tables.md` calls opcodes **0xDC** and **0xDD**
"audio_dispatch". They reach `FUN_0023baf8`, which is an **empty stub in the
retail build** -- they do nothing at all. **0xDE** is not audio either: it is a
four-channel timer over `DAT_00571b50`, parallel to the event scheduler.

**0x94 is the camera shake**, not "set_audio_position_normalized". It picked up
the audio name from the same stub: `FUN_002612e0` divides its first expression
by `DAT_00352c34` (100000, the ordinary world-space scale), truncates its
second to a halfword, and hands both to `FUN_0022dcf0` -- which stores them in
`fGpffffb6f4` / `uGpffffb6f8` and then tail-calls `FUN_0023baf8`. On a devkit
that last call drove the pad actuators; in retail it is `jr $ra; nop`, so the
two globals are the whole of what the opcode does.

`FUN_0020bec8` spends them, and that is the only place they are read. At
0x0020bf78 it has just written the eye height (`DAT_0058C0B0 + 0.4`) into the
translation it is about to build; at 0x0020bfc4 it adds

```
min(remaining * 0.0003125, magnitude) * sinf(remaining / 40.0)
```

to it and subtracts the frame tick from `remaining`. So the shake is **one
axis, vertical, applied to the eye only** -- the camera rises and falls in
place and nothing else about the shot moves. The magnitude is a cap on an
envelope rather than an amplitude: at 200 ticks the ramp term is 0.0625 and the
scripted 0.3 never bites, which is why every shake in the game fades out on its
own without the script asking it to.

Two details worth keeping:

- **The request is refused while a shake is running.** 0x0022dd08 compares the
  incoming *magnitude* against the remaining *tick count* as a float
  (`c.olt.s $f12, $f0`). That is not a Ghidra artefact -- the disassembly reads
  the same -- and since magnitudes are fractions and tick counts are hundreds,
  it means first one wins until the current shake expires.
- **The spend is 16-bit.** `remaining - frameTicks` is truncated to a halfword
  and the exhaustion test is that halfword's sign, so an overshoot clamps to 0
  instead of wrapping to 65000-odd ticks.

`FUN_0022a418:287` clears `uGpffffb6f8` on scene load and leaves the magnitude
standing; the port does the same.

s01_e012 is the scene that shows it. Subproc **4174** (body 0x9FDF) is four
opcodes -- cue 702, an `0xDE`, `0x94 0.3 200`, retire -- and the scheduler
stream at **0xD860** pays it out four times, at 60/60/60/30 frame delays,
interleaved with Dortin's "Ahhhhh!" and Volcan's "Wha-what's that!?" at the end
of the ship. Six frames of a decaying vertical jolt, about +/-6 cm at the peak.
`--arm-stream d860:30` reaches it from a standing start.

### A cue's bank byte can name a music slot

Chasing the rumble that goes with that shake turned up a second gap. Cue 702
was resolving and then reporting **"bank not loaded"**, because the engine held
three banks and the cue asked for bank 4.

There are eleven. `FUN_002057c8:44` indexes one table for every cue --
`(&DAT_003567d4)[bank * 0xb]` -- and `FUN_00205938` loads **music slot N's bank
into entry N + 3** (`FUN_00205310(desc, param_1 + 3, ...)`). `FUN_00205118`
fills 0..2 from SND.BIN resources 1/2/3 at boot; slots 0..7 fill 3..10. The
`slot + 3` that `FUN_00205778` returns is an index into that same table, not a
slot number, which is the tell.

The port modelled only the indirect route: a cue's **+0x07** alternate id
through `FUN_00205778` to a music slot. But a cue's **+0x00** bank byte reaches
the same table directly, and cue 702 uses it -- bank 4 is music slot 1, SND.BIN
resource 66, which s01_e012 loads at scene start. `bankFor` now maps any bank
at or above the three boot banks to slot `bank - 3`.

After the fix all four rumbles play (waveform 2, 18536 samples at 14.7 kHz,
about 1.3 s each), and no cue in either regression scene reports a missing
bank. Cue 702 is aimed at selector 1, the camera entity, so it plays
unattenuated at the listener -- the same "not positional" idiom the storm's
thunder uses.

### Checking it

```
orphen_port --disc-root . --scene s01_e012 --frames 20000 --sound-report
orphen_port --disc-root . --scene s01_e012 --frames 600 --sound-dump out/audio/wind.wav
```

The report's music section names every slot, what it resolved to, and every
play/ramp with its frame. For `s01_e012` that is slot 2 at frame 0, slot 7 up at
6805 and down at 8420, slot 6 (Sephy) at 11564, and the wind ramping down at
13122 as the scene hands over.

The sequencer runs on the mixer's clock, like the voice line, so none of it can
reach the simulation: `--frames 20000` is byte-identical across runs with and
without it.

## Voice, and why cutscenes now keep time

A line of dialogue holds for exactly as long as its voice clip. That is not an
inference from the pacing — the record says so, in control codes whose handlers
tail-call the audio system:

```
13 <name> 00      speaker
17                wait out the load the previous record started
16 ch w id32      cache the clip for the NEXT record, on the other channel
18 ch             start the clip cached for THIS record   -> j FUN_00206d98
<text>
1a                block until DAT_00356788 clears           -> FUN_00206a90
```

The stream **double-buffers**: a record's `0x16` arms the clip the *next* record
will speak, alternating between channels 0 and 1, so what a record plays was
armed a record earlier. `eeMemory.bin` was captured during `s01_e012`'s opening
and `DAT_00356480` reads `{50, 79, 0}` there — channel 1 holds what the record
on screen is speaking, channel 0 what the next one will. Walking all 83 records
of `scr2.out` with the real control widths, every one has exactly one `0x18` and
exactly one `0x1A`, and the channel it starts is always already armed.

`0x18` was documented in `analyzed/text_ops/` as a "conditional control byte
set" that consumed a byte and returned. It does consume the byte, and then:

```
00239a64: j     0x00206d98        ; FUN_00206d98(channel)
00239a68: daddu a0, a2, zero      ; delay slot: a0 = the operand byte
```

Two instructions were missing from the earlier reading, and they were the ones
that mattered. The file is now `text_op_18_start_voice_line.c`.

### The table

`VOICE.BIN`'s own first sectors:

```
word 0   entry count (3310)
word i   (sector << 15) | (sizeBytes >> 4)     -- data at sector * 2048
```

**This is not the flat-archive packing.** `FlatBinArchive` splits its entries 15
bits of sector over 17 bits of size-in-words; this is the other way round.
Reading one with the other's shifts gives plausible-looking offsets, so
`VoiceIndex` checks rather than assumes: the entries must tile the file without
overlapping. They do, and they end at 149,237,200 bytes against a 149,237,760
byte file.

`FUN_00221b90`'s bootstrap looks circular and is worth spelling out. It writes a
*fake* entry 0 of `1` into the empty buffer, which decodes to "sector 0, 16
bytes", and reads that — landing the file's first word, the entry count, in the
buffer. It rewrites that as `(count + 4) >> 2`, the whole table's length in
16-byte units, and reads again. Two reads, no table needed to find the table.

The table also sits in both EE dumps, since the game loads it at boot and parks
the pointer in `piGpffffbc30` (`0x00355BA0`). `VoiceIndex::loadFromEeDump` reads
it from there, so a disc root missing the 142 MiB archive still gets exact
timing and only loses the audio. `--voice-index <path>` overrides the search.

### Playback

Clips are raw SPU ADPCM, 16-byte blocks, no VAG header, played at the rate
`FUN_00207010`'s pitch register asks for: `0x760 / 0x1000 * 48000` = **22125 Hz**.
`decodePsAdpcm` was already there for sound effects.

`FUN_00207010` programs a *reserved* SPU2 voice rather than going through
`FUN_002057c8`'s 22-voice effect pool, so a long line cannot be stolen by a
footstep. `SoundEngine` keeps that separation with one dedicated slot that owns
its own samples.

**Nothing here reaches the simulation.** The hold comes from the table, not from
how much of the buffer the mixer has consumed, so a headless run keeps identical
timing — and headless runs do not decode at all, since a clip is a megabyte of
work per line otherwise thrown away.

`tools/voice_extract.py <id> <out.wav>` dumps a clip, and `--list` prints the
first forty with their lengths.

### What it changed

`s01_e012`'s 42 dialogue dispatches: **38 now timed by their clip, 0 estimated,
4 empty**. The four empties are one `0x33` site whose inline text is a bare
`0x02` terminate — the scene closing the window, not speaking — which used to
hold for a second each.

Line by line the old `60 + 2 * characters` estimate was nowhere near: "Monsters?"
held 90 frames against a real 34, and Volcan's opening rant held 310 against a
real 639. Flag `0x515`, the handoff to player control, moves from frame 10426 to
**13122** — and to 13317 once the subtitle walk drives the record's close rather
than the clip alone, which is the section on cutscene subtitles below.

Remaining gap: a *texted* record with no clip behind it still falls back to the
estimate, because there the original waits for the player's Cross press rather
than for audio. `s01_e012` has none; a scene that does will say so in the report.

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

## Rendering

`src/ported/render/` is a port of the map draw pipeline. `docs/rendering_pipeline_analysis.md`
is the reading it is built from; the short version is that the original builds
each primitive's GS packet once at load (`FUN_00211230`) and then, per frame,
decides which of them to call and in what order (`FUN_00209140`).

- `original_view_projection.*` ports `FUN_0020bec8` and `FUN_0020bd58` with the
  matrix helpers. The view is `translate(-eye) * rotZ(yaw + pi/2) *
  rotX(-pi/2 - pitch) * rotZ(roll) * scale(-1, 1, -1)`, in row-vector order.
  The projection's x scale is `powf(2, fGpffffb6e8) * 3840`, so `fGpffffb6e8` is
  a **log2 zoom** and its default of 1.0 gives the shipped 7680.
- Combined with the GS geometry measured from the repo's GS dump
  (`SCISSOR_1 = 640x224`, `XYOFFSET_1` centre 320 x 112), that is **67.4 degrees
  horizontal and 54.8 vertical** -- not the 60 vertical the harness used to
  assume. The port keeps the vertical exactly and widens horizontally with the
  window ("Hor+"), so a 4:3 window reproduces the shipped framing and a wider
  one reveals more to the sides. That deliberately differs from the game's own
  widescreen path (`cGpffffb66e`, a fixed 0.77 x squeeze).
- The 3D viewport is a **4:3 box centred in the window**, with the leftover as
  bars. Anything that belongs to the game's picture is confined to that box:
  the fog-colour clear is scissored to it, and so is the screen fade
  (`FUN_0025d0e0`'s sprite is a GS primitive inside the 640x224 frame, so there
  is nothing outside the frame for it to cover -- drawn full-window, the
  cutscene's fade to white washed the bars out with it). `--window <w>x<h>` is
  the way to see that: the default 960x720 is already 4:3 and has no bars.
- `original_map_visibility.*` ports the per-frame loop: the sphere-vs-frustum
  reject, the per-primitive occlusion fade, and the 4096-bucket back-to-front
  depth sort. It runs on the fixed simulation step, not in `render()`, because
  the fade byte is per-frame state.

### Standing on things: the map ships the answer

An earlier pass here invented a "step-up bit" — `record78 +0x04` bit `0x100` —
to decide which of two candidate surfaces an actor stands on. **It does not
exist.** It was a rule fitted to two data points because the port had no way to
break the tie. The engine does not break ties at all, and the reason is that the
map file tells it the order outright. `analyzed/terrain_ground_query.c` is the
full reading; the short version:

**`FUN_00227840` is an ordered walk, not a search.** PSM2 header word 6 holds a
64×64 grid of `int16` cell heads and a shared run list, and `FUN_0022b5a8`
copies both out of the file computing nothing. The scan walks one cell's run in
the authored order, keeps the first front-facing hit, and stops at the first hit
at or below the head. That order *is* the tie-break. The port now loads the
section (`loadCollisionGrid`) and walks it; every scoring heuristic it used
before — nearest height, highest eligible, the step-up bit — is gone.

Verified byte for byte rather than by eye: `out/mapbin/0001.psm2` reproduces the
dump's grid 4096/4096 and its cell list 1022/1022. All 165 PSM2 maps carry the
section, so the old unordered scan survives only as a fallback for a map that
fails to parse.

Three flags on `leadingWord` drive it, all read rather than guessed: `0x800`
"participates in collision", `0x200` "flat, height is the constant at `+0x2C`"
(which is the primitive's *maximum* corner z), and `0x100` "ceiling". A ceiling
does **not** reject — on a hit it clears the have-recorded latch without writing
a height, so a later front face below it replaces whatever was latched above.
The reject mask is entity `+0x74`, per entity, not a constant.

**And the lift is a landing snap, not a material gate.** `FUN_002262c0:41-85`'s
resample block needs the cached primitive's `+0x13` to be non-zero, and that is
`0` for every primitive in s01_e012 — it never fires there. What raises `+0x28`
is `:502-520`: `pos <= +0x4C → pos = +0x4C`.

The other half of that: **a stationary non-player actor never resamples the
ground.** `FUN_00227390` is reached only from the velocity loop or from a branch
gated on `DAT_003555d0 != 0`, and that global is `0` in the dump. Such an actor
keeps the `+0x4C` its placement opcode gave it for the whole scene — `0x55`
calls the query and gets a real height, `0x54` just mirrors the authored z. So
Magnus's `0x55` writes `-1.200` (hidden flat quad #9 over the bed) while leaving
him at the authored `-1.500`, and the snap lifts him. Slots 82 and 84 have
`+0x0A = -1` and `+0x6C = 0` — never queried — and sampling them every frame is
exactly what put them at the wrong height.

**Checked against the dump, entity by entity: 80 of 82 positions match.** The
two that do not are Volcan, whom the port moves when the original does not (his
`+0x04` reads `0x21` against the dump's `0x23`), and one NPC mid-walk. Both
predate this. `s01_e024` still runs all ten chest states with zero unimplemented
opcodes, and s01_e012 stays deterministic over 20,000 frames with `0x515` at
13122.

**The collision groups, and four-corner sampling.** Two things this pass first
listed as gaps and then closed, because a gap between the port and the
decompilation is a bug, not a design choice.

`FUN_00227840` has a **second loop** over the `0x74`-stride collision groups at
`DAT_003556e0`. Those groups own a block of primitives at the top of the
record78 array that the cell grid never indexes — in s01_e012 the grid stops
near 2500 and the groups run 3131..3948 — so without the loop roughly 800
primitives did not collide at all. It needs two more map sections: header word 1
(descriptors, **file stride 24**, which is what makes `4 + count * 24` land
exactly on the next section) and header word 7 (groups). The group's XY box is
not in the file — `FUN_00208450` recomputes it per frame — but for a group that
never moves it is exactly the union of its primitives' bounds, verified **20/20**
against the dump. A group the port ever animates will need the live recompute.

`FUN_00227070` samples **four corners**, at ±`entity+0x54`, and takes the
maximum, unless `entity+0x04 & 2` selects a single point. Party members read
`0xAF` (single); props, NPCs and the player (`0x312C`) are all four-corner, so
the port's single point was wrong for nearly everything — that is why an actor
would not stand on the edge of anything. The bookkeeping is not just a max:
`+0x6C` takes the winning sample's flags and ORs in any that tie, `+0x70` takes
the AND across all four, `+0x0A` only updates when the new sample found
something, and `+0x84..+0x90` publish the four heights.

Still missing, and named: `FUN_00228cf0` (riding another entity),
`FUN_002281a0` (the dynamic `0x10000` plane resolve), and `FUN_00208450` itself.

#### The frame's order: behaviours, then groups, then physics

`FUN_002239c8:116-135` is explicit about this and the port had it wrong:

```
FUN_0025b778   script tick -- 0x55 placements, 0x7E collision-group moves
FUN_00251ed8   lead player
FUN_00239ce0   actor behaviours
FUN_00208450   collision groups          -> sets DAT_003555d0
FUN_002261e0   physics, FUN_002262c0     -> reads DAT_003555d0
```

Two separate walks of the pool, and `FUN_00208450` sits **between** them. The
port fused physics into the behaviour loop (`FUN_00239ce0_update_actors` called
`integrateNonPlayerMovement` at the bottom of each iteration) and ran the groups
afterwards. That is wrong twice over: entity A's physics ran before entity B's
behaviour, and `DAT_003555d0` was always a **frame stale**.

The stale frame is what kept Magnus on the crates. A cutscene `0x55` that drops
an actor inside scenery gets exactly one physics pass to push it out before the
vertical settle stands it on top -- so the flag has to be readable on the same
frame the script raised it, which is precisely why the original puts
`FUN_00208450` immediately in front of `FUN_002261e0`. The port now has
`FUN_002261e0_update_physics` as its own pass in that slot.

Confirmed on hardware by forcing `DAT_003555d0` to 0 through the door-close
animation: the game then leaves Magnus at `(5.546, 0.128, -0.500)` on primitive
1114 -- the port's exact output, to the last decimal.

**This moves both regression scenes, and two props get worse.** `s01_e024`'s type
`0x62` enemies shift, because all behaviours now run before any physics. In
`s01_e012`, slots 35 and 40 now settle onto a floor 0.04 and 0.25 above where
hardware has them. That is not the reorder's fault -- their `+0x4C` was already
wrong, and the reorder merely gives the landing snap a chance to act on it. The
cause is the placement-grounding rule named below: `FUN_0025e7c0:115-120` grounds
a placed record **only** when its `FUN_0025ba98` record carries `0x8000`, and the
port grounds every one. Fixing that needs the `uGpffffadf4` archive, which the
port does not load yet -- `FUN_0025ba98` fills the same 0x28-byte record as
`FUN_0025bae8` but out of a different file. Against `eeMemory.bin` at frame 1091
this is 2 position divergences in 84 slots, where before it was 0.

**A diagnostic worth keeping.** Opcode `0x55` now prints when a placement lands
inside geometry -- the query answered above the authored z -- along with
`DAT_003555d0` as the script saw it:

```
[embed] frame 90 slot 81 placed at (5.546, 0.128, -1.5) but the floor there is -0.5 -- DAT_003555d0=0
```

That one line is what turned "the push never fires" into "the flag is one frame
late", after three rounds of static reading had failed to.

#### `DAT_003555d0` is not dead, it is transient

Recorded here because it cost most of a session and the wrong conclusion was
written down twice. `FUN_002262c0:112` has a branch that lets a **stationary**
actor resample the floor, gated on `DAT_003555d0 != 0 && (entity +0x08 & 0x20)`.
Three EE dumps all read `0` there, and that was taken as proof the branch never
runs. It is a **per-frame flag**: `FUN_00208450` clears it at the top of every
frame and raises it for any collision group with a live dirty byte -- so it is up
only while a door is swinging or the sea is rolling, and a save state almost
never lands on one of those frames.

What the branch does is eject an actor from scenery it is standing inside.
Sample the four footprint corners where the actor already is; set bit `n` when
corner `n`'s ground is **above** `+0x28`; index `DAT_00318ad0`, sixteen `(x, z)`
pairs of `0` and `±0.18`:

```
+0x30 = +0x30 * 0.5 + table[mask].x
+0x34 = +0x34 * 0.5 + table[mask].z
```

It is a movement *request*, spent the same frame by the velocity section, so the
ejection still obeys walls and step height. Mask `0xF` -- every corner buried, no
"away" to push toward -- probes 0.5 back along the facing instead, rotating by
`pi/2` up to four times. The lead additionally gets the same push every 64 frames
(`DAT_003555b4 & 0x3F`) with no group needed.

**It has exactly one frame to work.** A cutscene `0x55` leaves the actor's feet
at the authored z with `+0x4C` up on the obstacle, and the vertical settle lifts
him onto it that same frame. After that the corners are level with the feet, the
mask reads 0, and he is standing on the crate for good.

That is "Magnus stands on the crates". `0x55` puts him at `(5.546, 0.128)` with a
`0.15` radius, burying corners 1 and 2 in crate primitive 1114 at `-0.5`. Mask 6,
table entry `(-0.18, 0.00)`, and hardware ends at `5.366` -- which is where his
corner clears the crate's edge by 1e-4 and the mask goes to zero. The same 1e-4
is why the overlap test below must carry no tolerance: with slack the mask never
clears and the push never stops.

Found by putting a PCSX2 write breakpoint on entity `+0x20`
(`0x0058beb0 + slot*0x1D8 + 0x20`) and reading the storing PC. Static reading had
run out three times over; the breakpoint took one hit. Reach for it earlier.

#### The overlap test has no tolerance, and inventing one moves people

`FUN_00227d28` decides whether a query point is on a primitive, and the port had
been answering that question with a winding-agnostic barycentric test carrying
`-0.0005` of slack. Both halves of that were invented.

The original is an ordered set of `FUN_00228058` edge functions with plain `<`
and `>` against zero:

- A **triangle** (corner 2 == corner 3) wants `(0,1)`, `(1,2)` and `(2,0)` all
  `>= 0`.
- A **quad** is split on the `1--3` diagonal, and `cross(v1, v3)` *selects* the
  half rather than both being tried. On or left of it, test half 0 = `(3,0,1)`;
  right of it, half 1 = `(1,2,3)`, which is also the only path that writes
  `+0xD0 = 1`.
- Every comparison **reverses** when `+0x22` is set, which `FUN_00227840` does
  for `kCeilingBit` primitives. A primitive whose authored winding disagrees
  with its ceiling bit therefore does not collide at all — that is the hardware
  behaviour, not an omission to paper over.

The slack is what put Magnus on the crates in the middle of `s01_e012`. He
stands at `(5.366, 0.128)` with a `0.15` radius, and four-corner sampling asks
at `(5.516, -0.022)` among others. Crate top 1114 is a quad whose `v1--v2` edge
runs through `x = 5.5161` at that `y`: the corner is **outside it by 1e-4 of
world space**, or `-0.00048` in barycentric units. That cleared `-0.0005` by a
hair, so the corner "hit" the crate at `-0.500`, the four-corner max took it
over the floor at `-1.500`, and `FUN_002262c0`'s landing snap stood him on top.

With the edge functions the query returns `-1.500` on primitive `1734` —
`eeMemory.bin`'s `+0x4C` and `+0x0A` for that slot, exactly. Twelve of the
sixteen entities in the `magnus_floor` save state that carry a queried `+0x0A`
now reproduce both fields; three of the other four are bandana rope nodes whose
`+0x4C` is not a ground answer (their primitive index matches), and the fourth
is the corridor chest sitting on group primitive `3292`, which carries the
`0x10000` dynamic bit and needs `FUN_002281a0`.

Both regression scenes are byte-identical over 3000 frames, so nothing in the
opening of either was leaning on the tolerance.

### Diffing the entity pool against hardware, field by field

`G` (or `--snapshot-at <frame>` headless) now ends its report with the entity
pool laid out in fixed-width columns at the original's own offsets, and
`scripts/dump_ee_entities.py` prints the identical table out of an EE dump:

```
python scripts/dump_ee_entities.py <dump-or-savestate-dir> --compare orphen_snapshot_<frame>.txt
```

`--compare` aligns the two by slot and prints only the fields that differ, which
a whole-block `diff` cannot do -- one field wrong on eighty slots is one bug,
twenty fields wrong on one slot is a different one. The snapshot also carries the
scheduler cursors and both fade levels, which is what dates it against a dump
instead of eyeballing the camera.

Run against `eeMemory.bin` at frame 1091 it immediately found two fields wrong on
**all 84 occupied slots**, both from an incompletely ported `FUN_00229c40`:

- **`+0x74`, the terrain reject mask, was zero everywhere.** `0x04000000` is
  seeded at :79 for every entity; `FUN_00266240` only ORs a caller's extra bits
  on top, the player adds `0x08000000` (`FUN_002cb9a8`) and a party member
  `0x0D000000`. A zero mask rejects *nothing*, so every surface class the
  original refuses to stand on was walkable floor in the port.
- **`+0x64` cleared to -1 instead of 0.** `FUN_002262c0:40` writes
  `*(undefined4 *)(iVar12 + 100) = 0` -- decimal 100 is `+0x64`. Zero is a real
  blocker (slot 0 is the lead), which is why the original's readers gate on
  `+0x0C & 0x60` rather than on a sentinel.

Both regression scenes stayed byte-identical at 3000 frames across both fixes.

#### Still wrong, and named

The same comparison leaves three gaps standing. None is guesswork -- each is a
specific line of the decompilation the port does not run:

- **The ground query's writeback.** Hardware carries `+0x0A` (the packed
  `primitive | (half << 14)`) and `+0x6C` / `+0x70` (the winning and ANDed
  terrain flags) on every entity that has been queried; the port leaves all
  three at their spawn values. `FUN_002262c0`'s resample block reads `+0x0A`,
  and `0` for every s01_e012 primitive's `+0x13` is the only reason that has not
  bitten yet.
- **`+0x0C` on a static prop.** Hardware reads `0x0005` / `0x3015`; the port
  reads `0`, because it does not tick `FUN_002262c0` for entities that never
  move. The original does, every frame, and rebuilds the word from scratch.
- **Placement does not always ground.** `FUN_0025e7c0:115-120` samples the floor
  **only** when the type's record carries `0x8000`, and then through
  `FUN_00227798` -- the single-point, body-less probe -- clearing `+0x04` bit 8
  on the same branch. Everything else keeps the authored z that `FUN_002662e0`
  mirrored into `+0x4C`. The port grounds every placed record unconditionally,
  which is why a dozen props read `-1.500` against hardware's `-1.250` and the
  shop clutter reads `0.250` against `0.259`. Since `+0x4C` is exactly what
  `FUN_002262c0`'s landing snap raises an actor to, this is the first place to
  look when something stands on scenery it should be beside.

### `eeMemory.bin` is frame ~1091, and Volcan was never a bug

The single most expensive mistake in this area, recorded so it is not repeated:
**the dump had been dated by eyeballing the camera, which put it at frame ~9600.
It is actually frame ~1091.** Every "remaining mismatch" chased against frame
9600 — Volcan's height, a walking NPC's position, an entity `+0x04` bit, and a
whole theory that the cutscene scheduler ran ahead — was an artifact of comparing
two different moments. At frame ~1091 the port matches the dump **83 of 83 on
position**, and on animation counters, `+0x04` and `+0x4C` too.

Two pieces of state date a dump exactly. Use them before comparing anything:

1. **Event scheduler channel 0**, `DAT_00571e40` = `{cursor, timer, count}`,
   12-byte stride. The cursor is a pointer — subtract the script base at
   `iGpffffb0e8` (`0x355058`). Here `0x01c564b0 - 0x01c49a00 = 0xCAB0`, record
   [16] of stream `0xca30`, `count = 16`. That record is gated on event flag
   `0x04`, so the game is *blocked* there.
2. **The fullscreen fade**, `DAT_00571dd0` = `0x0d00` of `0x1fe0` at speed 2. It
   advances `speed * DAT_003555bc` = 64 a frame, so it is 52 frames into a fade
   the port starts at 1039 → ~1091.

Animation counters are the confirmation: they tick every frame, and slots 84, 85,
81 and 82 read 48, 38, 138 and 96 in both.

Worth keeping from the investigation itself: `0x86` is `FUN_00260ca0`, which
Ghidra types `void` but which tail-calls `FUN_0025d238` so the completion flag
arrives in `$v0`. That fade has **two** phases — ramp to `0x1fe0`, then hold
`DAT_00571dda` (160) ticks — and the port models both. And while tracing, note
that `currentOpcode_` is clobbered by the nested opcodes operand evaluation
dispatches, so a print placed after the operand reads names the wrong opcode;
`FUN_0025eeb0` captures it at entry for exactly that reason.

One real residual remains: Magnus (slot 81) sits at `-1.19x` where the dump has
exactly `-1.200`, stable across frames. Primitive 9 is flat, so the scan should
return its `+0x2C` of `-1.200` outright — the port is landing somewhere else.
Small, but it is a genuine divergence and belongs on the fix list.

`--probe x,y,z[,r]` prints `lead=` (record78 `+0x00`) beside `terrain=`
(`+0x04`); the scan gates on the first and the reject mask on the second, and
both have a meaningful bit `0x100`, so telling them apart matters.
`--actor-report` prints `af=` (entity `+0x04`) so a run can be diffed straight
against a dump's entity block.

### Why walls go see-through

Two things together, and the port has both now. **Backface culling** makes a
wall single-sided -- the GS has no culling hardware, the original does it in the
VU1 program at `0xE0`, but the winding is fully determined by `FUN_0022c6e8`'s
corner order, so GL reproduces it. And the **occlusion fade**: a byte at
0x80-record `+0x2E` walks from `0x80` down to `0x5C` and back up to `0x7E` at
one step per frame, and any primitive standing in front of the player, above
`playerZ + 0.38`, that covers the player's projected rectangle, fades.

The overlap test (`FUN_002099d8` / `FUN_00209928`) assumes a consistent
screen-space winding, so a back-facing primitive passes every edge trivially and
always reads as covering the player. That is why a room's near wall goes
translucent when the camera is outside it.

The near-plane path has its *own*, looser condition set (`FUN_0020a2c0`): blend
flag, overlap and height only. That path is ported; the polygon clip it wraps
is not -- GL clips those primitives instead, at the plane the next section is
about, and `--render-report` counts them rather than staying quiet about it.

**Not everything is single-sided.** Flag `0x1` on the record is the two-sided
bit: `FUN_00211230:190` hands it to VU1 as a byte of its own, `packet + 9`, next
to the vertex count. On `s01_e024` exactly 32 of 1630 primitives carry it and
they are exactly 16 coincident perpendicular pairs -- the hanging chains, built
as crossed planes. Culling those makes each plane vanish from one side, so
`drawPrimitive` skips culling for them. `--probe x,y,z[,r]` dumps the records
around a world point with their flags, which is how that was pinned down.

### The near plane is 0.4, and it is not the projection's 0.3

`s01_e012`'s "Big ones! They're all over the place!" cut rests its camera at
`(-2.793, 3.424, -0.792)`, **0.31 m behind the cabin's near wall** -- primitive
2672, the quad standing at `x = -3.100`. On hardware the shot is clear. The port
filled the entire frame with that wall.

The camera was not the problem. At frame 13260 the port reaches
`cPOS>(-2793, 3424, -792)` against the save state's `(-2793, 3424, -791)`, and
the eye's z is the same float `0xBF4AC083` in both -- the last unit is a print,
not a position, and the millimetre note below is the whole of it. What differed
was where the wall got cut off.

Two 0.3/0.4 pairs live in this engine and only one of them clips anything:

- `uGpffff80b4` (0x00352024) **= 0.3** is `FUN_0020bd58`'s near argument. It
  fixes the depth mapping `z_screen = m22 + m32/z` and nothing else. No
  geometry is removed by it.
- `DAT_00351FE8` / `DAT_00351FEC` / `DAT_00351FF0` **= 0.4** is
  `FUN_0020a2c0`'s Sutherland-Hodgman pass: every edge crossing 0.4 is split at
  it and every vertex behind it dropped. Nothing else clips the map.
  `FUN_00209140` routes any primitive whose bounding sphere comes within
  `DAT_00351FCC + DAT_00351FD0` of the eye through `FUN_00209ca0` into that
  clipper, and everything it keeps instead is wholly beyond 0.6 by
  construction, so no unclipped primitive can reach a near plane at all.

`glCameraFor` was being handed the first one. The 0.1 m between them decides the
whole shot, because the wall is nearly edge-on and the cut line sweeps across it
fast:

    prim 2672, view depths at its four corners:  0.039  0.279  0.409  0.559
      clipped at 0.30  ->  covers 100% of the 640x224 frame
      clipped at 0.40  ->  the surviving sliver projects to x in
                           [-2792, -158] px, entirely past the left edge: 0%

`constants::kGeometryNearClip` is that 0.4 and both `glCameraFor` callers use
it. `kNearPlane` stays 0.3 and stays where it belongs, in
`FUN_0020bec8_build`'s projection. Ordinary play does not notice: the follow
camera sits 3 m back, and frame 13400 of the same run -- the shot after this cut
hands the camera back -- is byte-identical across the change.

**The millimetre.** The EE core's FPU has no rounding-mode control; every result
is truncated toward zero. `-0.792f * 1000.0f` therefore stays at `-791.9999957`
on hardware and becomes exactly `-792.0` on a round-to-nearest host, which is a
whole unit apart once `FUN_0030bd20` truncates. `original_position_display.cpp`
now does that multiply in double and truncates the exact product, which lands on
the same integer the EE does -- truncating to float and then to int only ever
moves toward zero twice and cannot cross an integer the one-step version keeps.

### The cutscene fade cap, and the room that would not go dark

`DAT_00355700` is a global cap on that same `+0x2E` fade. `FUN_002340e0:32`
leaves it at **3** for the chest cutscene, and `FUN_00209140:344` overwrites the
emitted fade byte with it — a fade of 3 against a `0x80` = x1.0 scale is the
black room the item reveal happens in. `FUN_002342c0` does the rest: it hides
every entity from pool slot 2 up, zeroes the fog colour and drops both light
colours, so what shows through the near-transparent map is the fog-colour clear.

All of that was ported and none of it reached the screen. The cap was computed,
emitted, folded into the vertex colour — **and then discarded, because a map
primitive only blends when its material slot says so, and an opaque primitive
never reads its vertex alpha.** So the chest cutscene played out over a fully lit
room, the item reveal showed the floor behind it, and the scene read as suddenly
"warmer" because `FUN_002342c0`'s neutral 0x808080 light had replaced the room's
own without the room going away.

The entity path had already hit this and solved it — "a fading entity has to
blend whatever its passes say", `drawObjectModel`'s `mode == 0 && fade < 1`. The
map path now does the same, scoped to the capped case: `MapDrawItem` carries
`globalFadeCapped`, set only on the `FUN_00209140:344` branch, and
`drawPrimitiveSlot` promotes blend mode 0 to 1 for those. The ordinary occlusion
fade is untouched, so nothing outside a cutscene moves — `s01_e024` at frame 300
and `s01_e012` at frame 1500 are both byte-identical across the change.

The full cycle checks out: black room from state `0x0D`, item reveal on black,
and the room, the other six chests and the scene's own lighting all back after
`0x15`. Reproduce it with

```
orphen_port --disc-root . --scene s01_e024 --spawn -4.5,-10.5,0 \
            --press-confirm 60,500 --screenshot out/c.ppm:680
```

Two frames matter: the second confirm is what dismisses the caption, and without
it the run stops at state `0x12` and the last four states never run.

### Draw distance and fog

`DAT_00355628` defaults to 32.0 (`FUN_0022a360`) and is overridden per scene by
script (`FUN_00263cb8`), which is not wired up yet -- `--draw-distance` is there
to experiment with in the meantime. Fog starts at a quarter of the draw distance
and ends at it, and the PRIM word only carries the fog bit when that start is
below 5.0, so a stock 32.0 map renders unfogged.

### PSM2 record fixes this needed

Reading `FUN_0022b5a8:184-245` properly turned up three things the loader had
wrong: the flag word is w4 alone (w5 is a colour index, and the high half of the
flags is runtime-only), w6..w9 are four material-slot selectors rather than one
section E index, and w15 is the section B index whose entry is the primitive's
**face normal**. `psm2_material_expansion.*` ports `FUN_0022c3d8`, which expands
those selectors and turns the colour index into real per-vertex colours out of
the map's palette (PSM2 header word `0x10`), replacing the placeholder shading.

### Map blending, and the black slabs around the lanterns

`FUN_00211230` builds each map primitive's GS packet once at load, and part of
that is deciding whether the PRIM word gets its ABE bit. Lines 143-158, reading
the *base* material slot's byte +0x0B for flags and +0x0A for alpha:

```
flags & 0x70 == 0            -> mode 0, opaque, ABE never enabled
flags & 0x40                 -> mode 1, alpha blend -- but alpha 0x80 is fully
                                opaque, so it folds straight back to mode 0
flags & 0x40 == 0, & 0x10    -> mode 3
flags & 0x40 == 0, & 0x10==0 -> mode 2, additive
```

then `plVar8[1] |= 0x40` (PRIM bit 6, ABE) and `pfVar27[0x1c] |= 0x40` on the
primitive. That last flag is the one `psm2_material_expansion` was already
computing for `FUN_00209140`'s "already blended, never fade" gate.

**The draw path never read it.** `drawPrimitive`'s batch key was texture and
cull mode only, so all 841 of `s01_e012`'s flagged primitives -- 21% of the map
-- drew opaque. The visible result was a black stair-stepped slab behind every
hanging lantern: the glow quads are `flags=0x20, alpha=0x1F`, so mode 2 at 24%,
and drawing an additive glow opaquely paints its black surround over the wall.
The stair-stepping was the alpha test doing its job on a low-resolution texture
while the surviving texels came out solid.

The mode numbering is the same 0..3 the PSC3 path uses, and both feed VU1
programs that select GS state by it, so `setMapBlendMode` mirrors
`drawObjectModel`'s `setBlendMode` case for case. Ordering needs nothing new:
map primitives and entities already merge into one shared far-to-near bucket
table, which is how the original sorts them too.

Slot 0 is the slot asked, because slot 0 is the one this renderer draws -- it
supplies the texture page and the UVs as well. The original emits a pass per
slot and would blend on *any* slot's flags, so 158 of the 841 still come out
opaque here; all of them have an untextured slot 0.

`--map-no-blend` restores the old look for an A/B. At `s01_e012` f3000 the two
differ in 25.2% of pixels, at f6000 12.0%; `s01_e024` is **byte-identical**, so
the room the port was developed against is untouched. Cost is 23 extra batches
and 0.80 -> 0.92 ms of map time, entities unchanged.

### The cutout discard the map path was missing

Making the map's blend state explicit above had a second-order cost that only
showed up later, in `s01_e024`'s hanging chains. Each chain is four crossed
quads -- primitives 1618-1621 and 1626-1629, at `(-+0.45, -4.30, 1..3)` --
sampling a 16x64 strip at `u 0..15, v 144..207` of map page 0. That strip's
alpha is strictly binary: the link ring is 255, the surround is 0. Its single
material slot reads `type=0x00 a=0x00 f=0x00`, so `mapBlendMode` correctly
returns **0, opaque**, and an opaque draw of a cutout texture paints the
surround. Every chain link came back as a black stair-stepped slab.

`drawObjectModel` has discarded fully transparent texels since the grp_0003
hair-through-jeans fix -- `GL_ALPHA_TEST` at `GL_GREATER 0`, the fixed-function
stand-in for the GS alpha test. **The map path never had one.** Both paths feed
the same VU1 program and select GS state out of the same register blocks at
`608 + mode*3`, so whatever removes an alpha-0 texel for a model removes it for
a map primitive too; the port simply applied it on one side only.

It went unnoticed because the map draw used to *inherit* its blend state.
`drawObjectModel` called `setBlendMode` per subdraw and never reset it on exit,
and until `setMapBlendMode` existed nothing in `drawPrimitiveSlot` touched
`GL_BLEND` at all -- so map primitives drew under whatever the last entity
subdraw happened to leave enabled. An inherited alpha blend hides a cutout's
surround by accident. Building `cd24333^` and walking to the chains shows it
exactly: no black slab, but the whole chain is translucent enough to read the
floor and the player through it. Correct blend state removed the accident and
exposed the real gap.

So `drawPrimitiveSlot` now enables the alpha test with the rest of its batch
state and `flushMapBatch` clears it, which also re-arms it after each
interleaved entity (`drawObjectModel` disables it on the way out). Fragment
alpha here is the slot alpha times the occlusion fade, and `alphaForFade`
returns 1.0 for "no fade", so nothing opaque can reach exactly zero and vanish;
the cases that can are already invisible under their own blend.

The 20000-frame `--actor-report`/`--scr-report` is byte-identical. `s01_e012`
f6000 is unchanged, f3000 differs in 11 pixels (max delta 6, in a near-black
corner), and `s01_e024` at f600 differs in 0.35% -- a second cutout slab, in the
floor grating at the bottom right, going away for the same reason.

The earlier claim in the section above that the lanterns' stair-stepped
silhouette was "the alpha test doing its job" was wrong about the mechanism: the
map path had no alpha test to do it. The stair-stepping came from the same
inherited blend.

**Settled by a GS dump taken at the chains** (`s01_e024`, player standing in the
doorway, four captured frames, 1212 draws each). Both chains are `tbp 5760`
trifans, two crossed quads deep at any pixel through a link:

| | |
|---|---|
| `PRIM` | `0x03d` -- trifan, IIP, TME, FGE, **`ABE` 0** |
| `ALPHA_1` | `0x44` (register block 0) |
| `ZBUF.ZMSK` | 0 -- depth written |
| `TEST_1` | `0x5000d` |
| `TEX1_1` | `0x60` -- bilinear, as everywhere else |

So the chains do **not** blend. `mapBlendMode` returning 0 for `slot0 f=0x00` was
right, and the alternative floated below -- that the port misreads the material
byte and the primitives really blend -- is dead.

What removes the surround is the alpha test, and the earlier doubt about it was a
decode slip on my part. `ATST` occupies bits 1..3 of `TEST`, so `0x5000d`'s `0xd`
is `110` = **GREATER**, not GEQUAL; with `AREF` 0 and `AFAIL` KEEP that discards
exactly the alpha-zero texels and passes everything else. It is `0x5000d` on
4836 of the dump's 4848 draws and `0x3000d` on the other 12 (`ZTST` ALWAYS, an
overlay), and every one of the 4848 has `ATE` 1 / GREATER / `AREF` 0. The page's
CLUT at `CBP 6016` carries **exactly one** alpha-zero index among 256 -- the
chain surround -- and 234 at alpha 128.

`glAlphaFunc(GL_GREATER, 0.0f)` on both paths is therefore not a stand-in for
something else; it is what the GS is configured to do, and the port already had
it right on both sides. Nothing to change.

Two smaller things the dump confirmed in passing. `ZTST` is GEQUAL against a
reversed Z on every draw, which is why the specular pass -- coplanar with the
base pass it follows -- needs `GL_LEQUAL` and gets it. And the entity's per-face
additive second pass is there in the capture exactly as `drawObjectModel` builds
it: 365 untextured `ALPHA 0x48` `ZMSK 1` quads interleaved one-for-one with the
player's textured faces, in a constant `(32, 80, 160)` with the intensity in the
vertex alpha.

### `ABE` is not the register-block index

`s01_e012`, Sephy's close-up: her bangs drew as a set of solid pink strips laid
over her face where hardware feathers them into it.

The port had one `mode` variable standing for two independent things.
`FUN_00212058:137-141`:

```c
if ((texFlags & 0xc000) != 0) {
    plVar5[0x2c] = texFlags & 0x7f;          // pass alpha
    plVar5[6]    = texFlags >> 0xe;          // GS register-block index
    plVar5[1]   |= 0x40;                     // PRIM.ABE -- set here, and kept
    if ((texFlags & 0x7f) == 0x7f && (plVar5[0x2c] = 0x80, texFlags >> 0xe == 1))
        plVar5[6] = 0;                       // only the *index* folds
}
```

`plVar5[6]` picks the GS state at `608 + mode*3`; `plVar5[1] |= 0x40` is PRIM
bit 6. The full-alpha fold rewrites the index and leaves ABE standing, so block
0 is **not "the opaque block"** -- it is `ALPHA_1 0x44` with `ZBUF.ZMSK` clear,
differing from block 1 only in that it writes depth. The port read the fold as
"switch blending off".

Read out of the save state, the four blocks and how each is actually reached:

| block | `ALPHA_1` | `ZMSK` | reached by | draws in the dump |
|---|---|---|---|---|
| 0 | `0x44` | 0 | mode 0 (ABE off), or mode 1 folded at full alpha (ABE on) | 540 off, 84 on |
| 1 | `0x44` | 1 | mode 1 below full alpha | 28 |
| 2 | `0x48` | 1 | mode 2, additive | 9 |
| 3 | `0xa1` | 1 | mode 3, reverse subtract | 0 |

The 84 are her hair: `PRIM.ABE 1`, `ALPHA_1 0x44`, `ZMSK 0`, vertex alpha `0x80`
on all 332 vertices. With `TEX0.TCC = 1` and `TFX = 0` (MODULATE) the GS's `As`
is `(Av * At) >> 7`, so at `Av = 0x80` the blend factor **is the texel's alpha**
-- and her sheet's CLUT at `CBP 0x24b4` carries a full `0..128` ramp. That is
the whole effect, and the fold was throwing it away.

It hid for so long because it is a no-op on an opaque sheet: `As = 128` gives
`(Cs-Cd)*128>>7 + Cd = Cs` exactly. grp_0172's 60 chest passes are the same
fold case and look identical either way, which is why folding to opaque seemed
to work when it was introduced -- the chests that went translucent back then did
so because the port sent them to block *1*, losing the depth write, not because
they blended.

`setBlendMode(mode)` is now `setBlendState(mode, blend)`, carrying the two
separately, and block 0 sets `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` with
`glDepthMask(GL_TRUE)`.

**The map path had the identical shape, and now carries the two separately
too.** `FUN_00211230:152-161` folds `plVar8[6]` to 0 when byte `+0x0A` is `0x80`
and then sets `plVar8[1] |= 0x40` *outside* the branch, exactly as the PSC3 path
does. `mapBlendMode` still returns the block index; `mapBlendEnabled` is the new
`ABE`, `(flags & 0x70) != 0` on a textured slot, and `setMapBlendState(mode,
blend)` mirrors `setBlendState`. The slot alpha is now folded into the vertex
colour whenever the slot names a mode rather than only when the mode survives --
`plVar8[4]` is written before the fold, and the folded case is `0x80`, so the two
agree.

**It changes nothing in either scene, and that is a measured claim, not an
assumption.** Counting the fold case (`flags & 0x70` set *and* alpha `0x80`) over
the first 20,000 map slot draws: `s01_e024` `fold=0 blend=0 plain=20000` -- no
map primitive blends there at all -- and `s01_e012` `fold=0 blend=4367
plain=15633`. Zero either way, so the path exists but no shipped material reaches
it. `s01_e024` and `s01_e012` at f600 and f3000 are byte-identical before and
after, and the 20000-frame `--actor-report`/`--scr-report` diff is empty. Worth
having anyway: it was a real divergence, and it is the same one line of reasoning
as the entity path rather than a second, different rule.

### Texture filtering is bilinear, and the port was sampling nearest

`TEX1_1 = 0x0000000000000060` -- `MMAG 1`, `MMIN 1`, both LINEAR, with `MXL 0`
and `LCM 0` so there are no mipmaps and minification is plain bilinear. It is
written 160 times across the capture and never takes another value. `CLAMP_1` is
`0`, `WMS`/`WMT` both REPEAT, which the port already had.

Every texture the port uploads was `GL_NEAREST`. `SCISSOR_1` is `640x224` -- one
interlaced field -- so every 256x256 sheet is magnified several times over on
its way to the screen, and nearest sampling turns a graded alpha ramp into hard
strand edges. The GS filters the RGBA the CLUT lookup produced, so filtering an
already-expanded RGBA8 page is the same operation; `GL_LINEAR` with no mipmap
levels is the exact equivalent.

`s01_e024` f600 moves 90% of its pixels and its mean goes 58.75 -> 57.63. The
20000-frame `--actor-report`/`--scr-report` is byte-identical, as it must be:
neither this nor the ABE change is visible to the simulation.

### What is left on the bangs, and what it is not

They are still slightly heavier than hardware. Two things it is **not**:

- **The `GL_RGB_SCALE 2.0` combiner.** GS MODULATE is `C = (Ct * Cv) >> 7`; the
  port computes `tex * (Cv/256) * 2`, which is the same expression. Nothing to
  change.
- **Fog.** All 332 hair vertices carry `F = 255` in `XYZF2`, and `FOGCOL` is 0.
  The hair is unfogged on hardware.

What remains is the lit vertex colour feeding the card: hardware's hair vertices
are median RGB `(70, 51, 25)`, alpha 128 throughout. So the residual is a
scene-lighting brightness question -- the same thread as the lantern work's
"the port is overall too bright" -- and not a property of the hair at all.

~~**Worth carrying forward: the GS discards nothing here.**~~ **Withdrawn.**
`TEST_1` is `0x5000d` on every draw in the capture, and `ATST` is bits 1..3, so
that is GREATER against `AREF` 0 -- a cutout discard -- not GEQUAL. The port's
`glAlphaFunc(GL_GREATER, 0)` on both paths is the exact equivalent. Draw order
matters too, but it is not doing this job alone.

### The shop's additive props: a bundle-lookup order bug

s01_e012 drew white/pink sheets and a rope swirl above the shop counter that the
original does not show. **Fixed** — `EntityModelStore::loadModel` was asking the
wrong bundle.

Bisecting pool slots against an all-entities-hidden render at f3000 — which by
itself matches the emulator frame, so this is entirely entity draws:

| slot | type | model | share of upper frame |
|---|---|---|---|
| **14** | 0x27b | grp_00a8 | **34.6%** |
| 28-32 | 0x27c-0x280 | grp_00a9..00ad | 10.7% |
| 11 | 0x27d | grp_00aa | 0.2% |

`--pose-report <slot>` prints one entity's bone palette beside an unfiltered
rebuild of it, plus the subdraw blend histogram. It says grp_00a8 is **158
subdraws, every one blend mode 2 (additive), mean alpha 105/128**. These are
effect models, not props.

Six leads are ruled out, and are recorded so they are not re-derived:

- **Not the model parser.** This entry used to describe these as "stretched
  shards from a model-parser bug". The parse is fine.
- **Not the pose filter.** live palette == unfiltered rebuild on every bone,
  gap 0.000; +0x13C is 1.0, so the filter is a no-op here regardless.
- **Not the pose column.** The renderer uses entity +0xAC = 0 and `eeMemory.bin`
  reads +0xAC = 0 too. An earlier "live=2.44 vs posed=0.83" was a diagnostic
  comparing different columns: `--model-report`'s bbox uses
  `firstPoseColumnForAnimation`, the renderer uses +0xAC.
- **Not entity visibility.** `DAT_005a96b0[14] = 1` and +0x04 = 0x00D8, so both
  of FUN_0020c5a8's first-pass tests pass in the original.
- **Not the fade byte.** +0x134 is 0.0 on every entity in the dump, player
  included; 0 means "not fading" and the port already maps it to 1.0.
- **The zero bone matrices are an artefact.** 9 of 19 matrices at
  `0x357E00 + 14*0xA80` read zero, but the dump is the bed shot and slot 14 has
  never been drawn there, so that bank is untouched BSS.

**Root cause: the port binds the wrong mesh.** `FUN_00229c40:28` stores the
bound model's base pointer at entity **+0x15C**, and a loaded PSC3 keeps its
magic, so an EE dump names the mesh the original actually drew and its bone
count at +0x04:

| type | port binds | port bones | real bones | |
|---|---|---|---|---|
| 0x275 | grp_00a2 | 31 | 31 | ok |
| 0x279 | grp_00a6 | *parse fails* | 30 | **wrong** |
| 0x27b | grp_00a8 | 19 | 2 | **wrong** |
| 0x27c | grp_00a9 | 18 | 2 | **wrong** |
| 0x27d | grp_00aa | 10 | 10 | ok |
| 0x27e | grp_00ab | 2 | 2 | ok |
| 0x27f | grp_00ac | 41 | 2 | **wrong** |
| 0x280 | grp_00ad | 11 | 2 | **wrong** |
| 0x281 | grp_00ae | *parse fails* | 17 | **wrong** |
| 0x299 | grp_00c6 | 4 | 4 | ok |
| 0x2c0 | grp_01cb | 31 | 31 | ok |

The wrong ones are exactly the slots a hide-slot bisect had found. The **type ->
mesh id mapping is fine** (`id = type - 0x1D3` across this band, confirmed by the
five that match), and so is the descriptor table (contiguous at 0x00F467xx,
stride 0x2C, ordered by type).

The fault was **lookup order**. `loadModel` asked for category 0 across *both*
bundles and only then tried category 2:

```cpp
bytes = decodeResource(kGrpCategory, meshId);   // scene cat 0, then BOOT cat 0
if (bytes.empty())
  bytes = decodeResource(kMapCategory, meshId); // scene cat 2
```

That rested on ids being scene-private, which the old comment admitted was only
"in practice". s01_e012 breaks it: the six models it wants live in its own
category 2, and the boot bundle answers for those same ids out of category 0
first. Two of those answers are a different model — which parsed and drew as a
plausible but wrong mesh — and two are not models at all, which is why the
"missing PSC3 magic" failures always sat right next to the "shards". One bug,
two symptoms.

The fix exhausts the **bundle** before the category: for each provider, scene
then boot, try category 0 then category 2. After it, all eleven types in the
band match the dump's bone counts, and `s01_e012`'s model failures go from 16 to
**0** — grp_00a6 (30 bones) and grp_00ae (17) load for the first time and put
the shop's crates and side counter on screen.

The real props are 2-bone meshes. The port had been drawing 19- and 41-bone ones
whose subdraws are 100% additive, which is why they read as translucent sheets
rather than as obviously broken geometry.

`s01_e024` renders **byte-identical**, `s01_e012` stays deterministic, flag
`0x515` still lands at 13122, and the chest cutscene still runs its ten states.

### Texture slots are global, and a pass picks its own

The window curtains in `s01_e012` drew as the shop's gold medallion instead of
white sheer fabric. Neither the model nor the texture binding was wrong —
`eeMemory.bin` agrees with the port on both, entity by entity. What was wrong is
that **the port drew every textured pass with the entity's one bound texture**.

Two facts, and the second follows from the first.

**`DAT_003429a8` is one global array of 64 texture slots.** Entity models take
slots 10..23 and 24..39 through `FUN_00266118`'s two banks, the boot binds take
32..48 — and the *map's own texture pages* take slots 0..9, loaded by
`FUN_0022a178` from a ten-entry table at `DAT_00325394`. The dump confirms both:
`DAT_00325394` = `{0286, 0002, 02c8, 0253, 000d, 02c9, 000f}` and
`DAT_003429a8[0..6]` holds the same ids in the same order, which is exactly the
port's map page list. So a PSM2 material slot's `type` byte was never a page
index — it is a slot index that happens to start at 0. `FUN_0022a178_bind_map_textures`
now loads them, which is what makes the rest of this resolvable.

**Every PSC3 subdraw names its own texture.** `FUN_00212058:180-208` reads
`texFlags` bits 10..7 and writes packet byte 6 from it:

| selector | primitive flag 0x800 | byte 6 | means |
|---|---|---|---|
| 0 | either | 0x3F | the entity's bound slot |
| 0xF | either | 0x3E, byte 5 = 0x11 | the special mode the map path reaches through its own type 9 |
| 1..0xE | clear | the selector | **global slot `selector - 1`** |
| 1..0xE | set | 0x3F, selector rides in byte 5 | the bound slot still wins |

Byte 6 is `globalSlot + 1` on the map path too (`FUN_00211230:186`), which is what
pins the arithmetic down.

`--model-report` prints the histogram per entity as `passes={…}`. For `s01_e012`:

| model | passes | drew with | should draw with |
|---|---|---|---|
| grp_01d5 window curtain | `gslot3:48` | tex_0133, the gold medallion | tex_0253, the white sheer curtain |
| grp_00ae window frame | `gslot3:96` | tex_0130 | tex_0253 |
| grp_01d6 window surround | `gslot0:30` | tex_0133 | tex_0286, the sea |
| grp_00c6 lantern | `bound:49 gslot1:4` | all tex_012f | four passes on tex_0002, the flame sheet |
| grp_0172 chest | `bound+4/6/8` | tex_0179 | tex_0179 — the 0x800 form, already right |

The frames looked plausible before because tex_0130 and tex_0253 share their top
half; only the bottom differs, and the curtain lives there. The lanterns were
unlit for the same reason — their flame pass was being drawn with the lantern's
own sheet.

`s01_e024` renders **byte-identical** (nothing in it uses a selector),
`s01_e012` stays deterministic, flag `0x515` still lands at 13122, and the chest
cutscene still runs its ten states.

### The map draws a pass per material slot

`FUN_00211230:104-360` loops material slots 0..3 and emits a GS packet for every
slot whose `type` is `>= -1`, each with its own texture page, UVs, flat colour,
alpha and blend mode. The port drew slot 0 only. `s01_e012` has 202 primitives
with two slots and 68 with three; drawing them all takes the shot at frame 200
from 437 to 518 map triangles for the same 226 primitives, at no measurable cost.

Honest caveat: this is faithful to the decompilation and the extra passes are
demonstrably emitted, but **no shot checked so far looks different with them on**
(`--map-base-slot` renders identically at f1500 and f3600, and `s01_e024` is
byte-identical). The layers are there in the data; what they are for has not been
seen yet.

### Notes on GS dumps

`tools/gs_dump_parse.py` drops VSync boundaries, so re-walk the packets if you
need per-frame draw lists. The field is **640x224** with XYOFFSET
(2048-320, 2048-112) — the measured vertex y span of 226.6 settles it. ABE is
not a useful discriminator in this game: most of the scene is blended, and
nearly every texture shows opaque ~= abe because each primitive gets a second
packet.

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

## Running The PSM2 Slice

From the repository root after building:

```sh
port/build/msvc-Release/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2
```

Or load from extracted disc files in a directory containing `MCB0.BIN` and `MCB1.BIN`:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012
```

**A scene does carry its own spawn point**, and it is in the SCR. `FUN_0025b600`,
called from `FUN_0022a418` with the per-scene defaults struct at `0x325368`,
reads a block sitting immediately after header word 6's texture page list: skip
one halfword, copy sixteen halfwords, align to 4, then four ints scaled by
**1000.0** -- not the 100000.0 the coordinate opcodes use.

```
s01_e024 blob 0x614:  -3250, -12750, 0, 32000
              /1000 = -3.25, -12.75, 0.0, 32.0
```

The first three are the spawn, landing at struct `+0x4C` (`0x3253B4`); the fourth
is `DAT_0032538c`, the scene's **draw distance**, which `FUN_0022a360` seeds to
32.0 and this overrides per scene. Both are wired up now.

`FUN_0022a418` copies that backup into `DAT_00325340` when `DAT_003551ec` has bit
`0x2000`, and applies it to pool slot 0 when it has bit `1`. `FUN_002000c0` sets
`0x2001` at boot, so arriving *without* an explicit warp target -- which is what
loading a map from the debug menu does -- lands on the script's own spawn. A warp
from another map overrides it through `FUN_0022b2c0` (opcodes `0x8B`/`0x8C`).

Confirmed against an EE memory dump of `s01_e024`: `DAT_00325340` and its backup
both read (-3.25, -12.75) and pool slot 0 is there.

So the port picks, in order: `--spawn x,y,z`; a script teleport (`0xAB`); **the
scene script's own spawn**; the first group 2 placement record; otherwise the
walkable triangle nearest the map's horizontal centre. The console says which was
used. The group 2 fallback was previously the default and gave (-5.5, -12) on
`s01_e024` -- 2.3 units from the real spawn -- so it is a guess, kept only for
scenes with no defaults block.

To validate the loader without opening a window:

```sh
port/build/msvc-Release/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2 --load-only
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012 --load-only
```

To inspect the resources loaded by a disc scene:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012 --scene-tree --load-only
```

The scene tree currently groups MCB bundle records by category and prints record ids, bundle offsets, packed/decoded sizes, and known decoded signatures such as PSM2, BMPA, SCR, and PSC3. `s01_e024` is a useful early exploratory scene because it is much smaller than `s01_e012` and appears to be a debug scene:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --frames 60
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only --scr-report
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only --actor-report
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --frames 120 --scr-tick --scr-report --actor-report
```

`--scr-tick` runs the per-frame script entry and the object-script slots.
`--actor-report` lists every live entity, the behavior address its type resolves
to, and whether that behavior is ported; with `--frames` it also reports tick
counts per type. Both reports resolve straight from the pool, so `--load-only`
gives a useful answer before a single frame has run.

`--probe x,y,z[,radius]` dumps every primitive whose bounds come within radius
of a world point -- flags, terrain flags, centre, radius, plane normal, corner
count and material slots. It is a hypothesis-testing tool, not part of the port.

`--render-report` prints what the map visibility pass culled, faded and drew,
how many primitives straddle the near plane (which GL clips rather than
`FUN_0020a2c0`), and two oracles that can be checked without looking at a
picture: whether the plane normals agree with the map's own `0x100` ceiling
flag, and how much of the drawn set faces the camera.

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

Game inputs -- these reach the ported code as raw pad bits and nothing else:

- `W/A/S/D` moves the runtime lead player relative to the current camera yaw.
- `Space`, or gamepad X / face west (Square), jumps when the lead player is grounded.
- `C`, or gamepad B (Circle), swings the sword when the lead player is grounded. See The sword attack, above.
- `V`, or gamepad Y / face north (Triangle), casts the homing magic projectile, also only when grounded. See The magic cast, above.
- Holding `C` re-arms the jump in mid-air. This one is a harness debug affordance, not something the original's airborne state does; it restarts state 2 / animation `0x0C` through the same startup and `+0x44` seed as a grounded jump, which is how you get up to a ceiling to test against. It shares Circle with the attack because the original's own gate on it is `input_flags & 0x20` (`FUN_00251ed8`, `uGpffffbd54`).
- `Return`, or gamepad A (Cross), confirms -- the interaction probe, and dialogue advance.
- `J/L` orbits the player camera, mapped onto the original's L1/R1 raw pad bits (0x04/0x08). With no player active they rotate the free viewer camera instead.
- Holding `P`, or the gamepad right trigger (R2), fast forwards. See below.

Harness controls, which the simulation cannot see:

- Left/right arrows cycle maps when running from `--disc-root`.
- `R` resets the viewer camera.
- `F` toggles wireframe.
- `H` toggles the game's own debug readout, the `FUN_00268270` overlay.
- `B` toggles the in-world debug overlay: collision boxes, entity labels, origin axes.
- `O` toggles SCR SUBPROC DISP. It was `P` until fast forward took that key.
- `G` dumps a pose/draw-list snapshot of the current frame and photographs it.

`C` was `B` until the sword attack landed on it, at which point one key was
firing two gameplay actions and a harness toggle. The free viewer's own pitch
(`I/K`) and zoom (`Q/E`) are gone with the same tidy-up: they were map-viewer
holdovers with no pad button behind them, and the free viewer keeps yaw and pan
off the game's own axes. `R` still restores its default framing.

### Fast forward

Holding `P`, or the pad's right trigger, runs the simulation as fast as the
machine will carry it. It is the original's own debug affordance: `FUN_002000c0`
enters its vsync-wait block only while `DAT_003555db == 0 || (DAT_003555f4 & 2)
== 0`, so with the cheat flag set (see `analyzed/`) holding R2 drops the wait and
the present together and the game free-runs.

The port does the same thing in the same place -- `main()` keeps calling
`PortRuntime::update` with `render` and `swapBuffers` skipped. That is sound
because the headless `--frames` path already runs `update` with no renderer at
all; nothing in a step reads back from a draw. On this machine a step costs
about 0.34 ms against a presented frame's ~16.6 ms, so the ceiling is roughly
40x.

Three things differ from the original, all of them harness concerns:

- **Steps run against a wall-clock budget**, 20 ms, rather than truly uncapped.
  One press cannot then starve event polling, so the key can still be let go of.
- **A frame is still presented about every 33 ms**, at whatever point the
  simulation has reached. A window that goes black until release is worse to
  work with than one that updates at ~20 Hz while the scene sprints, and it is
  the only way to see where to stop.
- **Audio is muted while held.** The mixer runs at 1x on its own thread, so
  every cue the sprinting simulation fires would key on over the top of the last
  one. The device stays open and `mix` still runs -- that is what drains the
  pending key-on queue -- and the harness zeroes the buffer after it.

Releasing prints what the burst achieved:

```
[ff] 2612 steps in 1.04 s (2511.5 steps/s, 41.9x)
```

`--screenshot` ignores the key outright. That path promises one simulation step
per rendered frame so two builds photograph the same instant, and a held key
must not be able to move it.

## POSITION_DISP, the original's own overlay

`src/ported/debug/` is the game's debug text path rather than the harness's:

- `original_debug_text.*` is `FUN_002681c0` (the printf that appends into
  `DAT_00572c38` behind the `DAT_003555dc` / `DAT_003555da` gates) and
  `FUN_00268270` (the once-a-frame pass that walks that buffer placing one
  glyph per printable character and then clears `DAT_003551dc`).
- `original_position_display.*` is the `cGpffffb128` block of `FUN_002239c8`,
  which is what the debug menu's `ON :POSITION DISP` row turns on.

It draws **always** for now. `cGpffffb128` (`DAT_00355098`) is held on and both
debug gates with it, because nothing reaches `FUN_00268d30`'s menu yet.

There are two readouts and the original picks between them on the gates:
`cGpffffb66a == 0 || cGpffffb66c != 0` takes the detailed one, otherwise the
compact one. Both are ported; the port shows the detailed one, which is five
lines:

```
(-3250, -12750, 0)
MF:00003015 AF:3024 SF:0026 NF:0000
tPOS>(-3250, -12750, 800)
cPOS>(-6101, -12750, 875)
MAP>(MP0124)
```

That block is the `s01_e024` EE dump read through the ported formatter, and it
is the check the layout was built against: the unlabelled first line is the lead
player's `+0x20` triple, `tPOS` is the camera's look-at (`DAT_0058be90`), `cPOS`
is the camera entity's own position (`0x0058C088 + 0x20`), and `MAP` is
`DAT_003551f4` / `DAT_003551f0`, which are the MCB section and entry. Positions
are scaled by 1000 and truncated. **`AF`/`SF` do not match the dump yet** --
`FUN_0022a418`'s `DAT_0058beb4 = ... | 0x3000` and `DAT_0058beb8 |= 4` are not
in the port's scene bootstrap, so those two read 0.

### Coordinates

`FUN_00268270` works in the units `FUN_00268410` hands the sprite builder, and
`FUN_00207938` writes x at `<<4` but y at `<<3`. With the shipped GS geometry
(`SCISSOR_1` 640x224, `XYOFFSET_1` centred on 320 x 112) that makes one x unit
one framebuffer pixel and one y unit half of one -- the framebuffer is a field,
displayed at 448 lines. So the overlay is authored on a **640x448** screen:
`screenX = 320 + x`, `screenY = 224 - y`, a 16-pixel left margin, a first line
at y = 8, a 10x20 glyph cell, a 12-pixel advance and a 20-pixel line pitch.
Lines wrap once x passes 304 and `~` jumps to a bottom line at y = 414,
right-aligned on x = 640 by the length of *everything left in the buffer* --
not the token that follows, which is what `FUN_002685e8` is actually measuring.

The port fits that 640x448 box into the window uniformly and centred. The world
is drawn Hor+, but the overlay's two anchors only line up with each other inside
the shipped 4:3 frame.

### The glyph atlas

`FUN_00268410` textures each 10x20 quad from a 7x15 texel window at
`(((c - 0x20) & 0x1F) * 8 + 1, ((c - 0x20) >> 5) * 16 + 1)` of texture slot
`0x30` -- an 8x16 cell grid, 32 columns, three rows covering 0x20..0x7F, so a
256x48 band. `FUN_00221fd8` binds slot `0x30` to texture `0x179`, which the EE
dump confirms (`DAT_003429a8[0x30] == 0x179`), and the port already had it:
it is one of `EntityModelStore::FUN_00221fd8_bind_boot_textures`' seven fixed
binds, resolved out of the `s00_e000` boot bundle and uploaded per slot by
`ensureSlotTexturesUploaded`. So the overlay draws the game's own glyphs.

`0x179` is a 256x256 sheet shared with the chest and title art, and **the font
band is at the bottom in storage order** -- v = 0 is the last stored row. That
is exactly the flip `decodeBmpaTexture` already applies, so the window
coordinates index the decoded image directly. Reading the raw record without
the flip shows the particle sprites that sit at the sheet's other end, which is
what made this look for a while like the wrong texture.

The harness stroke font is still the fallback for a run with no boot bundle;
it is sized to sit inside the original's cell so the layout does not change.

## Debug HUD

`H` toggles the game's own debug readout -- `FUN_00268270`'s overlay, with the
POSITION_DISP block `FUN_002239c8` feeds it. It is off by default: the port
holds `DAT_003555DA` and `DAT_003555DC` on (neither byte has a way in yet),
which is what selects the detailed readout, so it would otherwise sit over the
picture on every frame where retail only shows it in a debug build. The toggle
is display-only: the glyphs are laid out and drained on the simulation step
either way, so `--frames` determinism does not move with it.

The harness used to carry a second overlay of its own on that key -- position,
facing, state and animation ids, gait, camera mode, ground triangle -- stacked
underneath the ported one. It is gone. Everything it showed is in the game's own
readout or in `--actor-report` / `--scr-report`, and it covered the picture
while doing it.

`src/harness/debug_text.*` is a small stroke font, PC-only diagnostics with no
relationship to the game's own. `drawOriginalOverlay` stamps the glyphs
`FUN_00268270` already placed -- from slot `0x30`'s atlas when it is resident,
falling back to the stroke font when it is not. The world-space entity labels
use the same stroke table.

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

## A moving collision group's primitives need their plane rebuilt per query

`FUN_00228090:11` checks the dynamic bit `0x10000` **before** the flat bit
`0x200`, so a primitive owned by an animated group never answers with a
constant. It goes through `FUN_002281a0`, which rebuilds the normal from the
live vertices -- `(P1-P0) x (P2-P0)` over the same triple the static plane uses,
triangle `(0,1,2)` anchored at 1, quad half 0 `(3,0,1)` anchored at 0, half 1
`(1,2,3)` anchored at 2 -- and evaluates the plane normally. The stored plane is
stale the moment the group moves.

`heightOnPrimitive` used to return `bounds.max.z` for these, under a comment
saying the port had no moving collision. That was accurate when written and
stopped being accurate when `FUN_00208450` was ported; nothing rechecked it.
Every door primitive was handing the ground scan its bounding-box lid as if it
were floor.

s01_e012, Volcan closing the door at (5.353, -2.019), r 0.15, feet -1.5:

| corner | point | hardware | port (before) |
|---|---|---|---|
| 2 | (5.503, -1.869) | `128` (no ground) | `-1.5` |
| 3 | (5.203, -1.869) | `-1.5` | `0` |

mask 4 -> `(-0.18, -0.18)` -> 5.173 on hardware; mask 8 -> `(+0.18, -0.18)` ->
5.533 in the port. Note that `128` is the no-ground sentinel and it *sets* the
mask bit -- the test is `feet < corner`, so the push-out ejects an actor away
from a hole as readily as out of geometry.

s01_e024 is byte-identical across the fix; s01_e012 moves one line, the player's
height on primitive 3248 (`leading=0x10804`, dynamic) going from the bounding
box's -1.18997 to the plane's -1.194.

### Two instruments worth reusing

`--push-probe` logs every embedded-corner push: slot, position, feet, the four
corner heights, the per-corner primitive, the mask and the table entry. Those
are the same fields a PCSX2 **execute breakpoint at `0x002265E4`** shows, where
`$v1` is the mask, `$s1` the entity (`0x0058BEB0 + slot*0x1D8`) and `$s0` the
workspace.

Better still: a PCSX2 save state taken at that breakpoint carries the live
workspace in `Scratchpad.bin`. `DAT_70000000` (scratchpad +0) minus `0x170` is
`FUN_002261e0`'s frame; `ws[0x4a]` names the entity, `ws+0x34..0x40` are the
corner heights and `ws+0x162` is the mask. A save state at a breakpoint answers
a register question without needing the registers.

## Known open: placement records sit one ULP below the floor

`DAT_003556e8`'s records author z as `0xBFC00001` where the floor plane is
exactly `0xBFC00000` -- in the map file and in every dump. Hardware entities all
read the exact `-1.5`; the port keeps the authored value, and `feet < corner`
then reads all four corners as embedded (mask 15 on five s01_e012 NPCs at frame
1). Not yet established as a bug: `FUN_002262c0:112`'s push test runs before the
landing snap at `:502`, so hardware would see the same value if `DAT_003555d0`
were raised that early. Needs a breakpoint hit at scene load to settle.

## A ceiling-only collision group adopts the no-ground sentinel

`FUN_00227840:176`'s merge is gated on the group's hit **count** alone
(`+0x6E`), and that count is incremented for ceilings as well as floors. A group
whose only overlapping primitive is a ceiling at or below the head therefore
reaches the merge having recorded nothing, with its height still seeded to
`0x43000000` = 128 -- and adopts it, overwriting whatever loop one found and
resetting `+0x5C` to `0xFFFF`.

That is not a curiosity. It is how a doorway reads as a **hole** while the door
is swinging through it, and the embedded-corner push-out depends on it: the mask
test is `feet < corner`, so a corner that finds no ground sets its bit and the
actor is ejected out of the opening.

The port had an extra `!groupHit.has_value()` guard on that merge, which skipped
it whenever nothing had been recorded -- exactly the ceiling-only case. Removing
it is the fix.

Confirmed against `magnus_floor` and `volcan_bug`: the two door entities, slots
18 and 19 at (5.50, +/-1.20), read `+0x0A = -1` and `+0x6C = +0x70 = 0` on
hardware -- no primitive ever found. The port used to report a floor at -1.50
there and now reports none. s01_e024 is byte-identical across the change.

### The door itself was never wrong

Worth recording because two rounds were spent on it. The port's door leaf sits
at the correct pivot with radii identical to hardware's to five decimals; the
only difference was phase. `0x7D` writes an **absolute** angle, so there is no
ramp to get wrong, and the logged sequence (`--push-probe` now prints every
group rotation write) opens at 2 deg/frame to 90, jumps to 45, then closes at
0.5 deg/frame. Hardware's push fires on the 45 deg frame; the port's fired eight
frames later at 41 deg, because at 45 deg its mask was still 0 -- the missing
ceiling merge, not the animation.

## The push-out must not go through the "found" gate

`FUN_00227070` returns the **max** of the four corners, so a single corner over
a hole (128) makes the whole sample read as 128, and the runtime's
`terrainSurface` lambda turns that into `nullopt`. The push-out's mask builder
then saw no surface and produced mask 0 -- discarding four perfectly good corner
heights because one of them was the sentinel.

The original never asks that question. `FUN_002262c0:118-152` calls
**`FUN_00227390`**, whose corner array at workspace `+0x34..+0x40` is always
filled (128 for a miss), and tests each corner independently:
`entity +0x28 < corner`. `-1.5 < 128` is true, so **a no-ground corner sets its
bit** -- that is the entire point, and it is how an actor gets ejected from a
doorway.

Fixed by giving the push-out its own `FUN_00227390_corner_sample` callback: the
same scan, without the `found` gate. `terrainSurface` keeps its old contract for
every other consumer.

This was the last of three separate faults stacked on one symptom in s01_e012's
door close, all of which had to go before the next became visible:

1. dynamic primitives answered with their bounding box instead of a rebuilt
   plane (`FUN_002281a0`);
2. a ceiling-only collision group never adopted the 128 sentinel, because of an
   extra `!groupHit.has_value()` guard on `FUN_00227840`'s merge;
3. the push-out read that sentinel through the `found` gate and lost it.

### `--push-probe`

Kept, because all three faults above were invisible without it. It turns on:

- `[push]` -- **every** gated push-out evaluation, including `mask 0`. Logging
  the zero case is what found the third fault: the interesting frame was the one
  where nothing happened. It prints position, feet and `+0x4C` with raw float
  bits, the four corner heights, each corner's answering primitive index, the
  mask, and the table entry.
- `[group]` -- the collision-group table once at load (first vertex, vertex
  count, first primitive, primitive count, so it can be diffed against a dump's
  descriptors), then every rotation write as it happens.

The fields match a PCSX2 execute breakpoint at `0x002265E4`, where `$v1` is the
mask, `$s1` the entity (`0x0058BEB0 + slot*0x1D8`) and `$s0` the workspace, so
the two logs diff directly. Correlating the door's logged angle against the push
mask is what localised this; the port's door leaf matched hardware's to four
decimals at 45 degrees, so the animation was never the problem.

Both regression scenes stay byte-identical with the probe compiled in -- it is
inert unless the flag is passed.

## s14_e031, the spell-reward cutscene

One scene plays the "you learned a spell" cutscene for all eleven learnable
spells. Event flags **850..860** (BFLG 50..60) pick which; the ladder is in the
*init* entry, so `--set-event-flag <id>:0` is the only form that works -- a flag
raised on frame 1 is already too late.

```
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s14_e031 \
  --no-audio --frames 4000 --set-event-flag 856:0 --battle-report --actor-report
```

| flag | spell | id | hand effect | hands off to |
|---|---|---|---|---|
| 850 | Feathers of the Hurricane | 3 | `0x13B` | s14_e026 |
| 851 | Smoke of Pain | 4 | `0x13C` | s14_e045 |
| 852 | Coldness of Destruction | 6 | `0x194` | s14_e042 |
| 853 | Bolt of Thunder | 2 | `0x13A` | s14_e002 |
| 854 | Falcon of Death | 8 | `0x175` | s14_e028 |
| 855 | Hammer of Evil | 9 | `0x177` | s14_e029 |
| 856 | Pinnacle of the Sun | 10 | `0x179` | s14_e024 |
| 857 | Hail of Heavens | 11 | `0x17C` | s14_e013 |
| 858 | Shield of Immunity | 12 | `0x143` | s14_e001 |
| 859 | Shield of Inferno | 13 | `0x127` | s14_e027 |
| 860 | Armor of Purity | 14 | `0x144` | s14_e025 |

The spell ids are the master record table at `0x00324FC8`, stride `0x12`, and
the demo entity the scene spawns into pool slot 10 is `0x1F1 + spellId` -- which
is why `0x1F6` and `0x1F8` are skipped. Those two are Hand of Pyro and Bite of
Lightning, the spells Orphen already has.

### The three things that made it run

**The scene has a target, and it is a type `0x8B` training dummy.** It stands at
`(3.5, 0, 0)`; the script gives it `+0x95 = 50` through object register `0x11`
and its own state 0 fills `+0x12A = 62` and binds an actor record. Both are
needed before `FUN_00249610` will keep a target, and all three save states show
`target = 15` for the whole cast. Without it the control block sat on the 1 that
state 120 parks it on, the spell landed two units in front of the caster instead
of on the dummy, and `level == 5 && target > 1` -- the summon -- could not fire.

**The beat gate is scene module 18, mode 4.** `FUN_0026C980` is two lines:

```c
if (FUN_00266368(0x35D) && sGpffffaf5c == 0) FUN_002663A0(0x35E);
```

The script raises flag 861 when the cast goes off and then parks scheduler
channel 1 on the record at blob `0x1B78`, gated on flag **862** -- which nothing
in the script ever sets. `sGpffffaf5c` is `DAT_00354ECC`, held at 1 by a level-5
summon for its whole run, so the beat waits exactly as long as the summon is on
screen. Before this hook the four kind -2 arms hung there forever.

**The barrier's animation order is 1 -> 0 -> 2.** `LAB_002DE0B8` (types `0x127`,
`0x143`, `0x144`; `0x2DE0A8` and `0x2DE0B0` are two-instruction thunks into it)
is a Ghidra LAB with no `src/` file, recovered from `SLUS_200.11` at
`0x002DE0B8..0x002DE36C`. 1 is the rise, 0 the hold, 2 the drop, and state 115
ends the cast the moment it reads 2 -- so spawning the barrier at animation 0
released it on the first frame, which is what "the shield demos play with nothing
on screen" actually was. State 115 also has to stamp `+0x94` (the caster's pool
slot, which the barrier rides every frame), `+0x12C` and `+0x198`; none of the
four were being written.

### GRP.BIN, and why no spell effect draws

`--model-report` reports `NO MODEL: missing PSC3 magic` for every hand effect,
projectile and barrier in this scene, and it is not a behaviour problem --
Bite of Lightning is just as affected here.

An entity model does not come from a scene bundle in the original.
`FUN_00222498` loads it straight out of a flat archive by mesh id, and the model
record's `+0x04` **bit 6** picks which: set means `MAP.BIN`, clear means
`GRP.BIN`. Every spell effect record carries `flags04 = 0x2D`, bit 6 clear.

`GRP.BIN` is not in the disc root. `EntityModelStore::loadModel` now takes the
record's flags and falls back bundle -> (`GRP.BIN` | `MAP.BIN`) -> `ITM.BIN`, and
the texture loader falls back to `TEX.BIN` -- which *is* present, and which on its
own cleared every "no pixels" slot. Drop `GRP.BIN` in beside the other archives
and the effects become visible with no further code change.

Do not shortcut this with a blanket `MAP.BIN` fallback: `MAP.BIN` id `0xBD` is a
perfectly valid but completely unrelated PSC3, so the guess returns a plausible
wrong mesh rather than a miss.

### The four kind-12 spells

Bolt of Thunder, Feathers of the Hurricane, Smoke of Pain and Coldness of
Destruction are Hand of Pyro's shape four more times, and the only family where
the **projectile** is genuinely per-element. The hands come in two shapes --
Bolt and Feathers take the bone index negative and ride the caster's facing,
Smoke and Cold park a fixed pose and tilt it by class -- and the four launches
are one function. The steering is what makes each spell look like itself:

- **Bolt of Thunder** fans by a *time-scaled* yaw, so the bolts splay wider the
  longer they fly, and each homes on a `+0x1B4` that tracks the target
  separately from the yaw it is drawn at. It is also the one that moves its
  chain and charge to `+0x1C8`/`+0x1C9` so it can keep `+0x1C6` as the aimed
  shot's costed flight time.
- **Feathers** takes one fixed yaw step, flies blind until it has turned, then
  homes -- and its volley **fans across up to five different enemies** rather
  than stacking on one. The picker is pool slot 2 first, then every live entity
  whose `+0x96` bit 0 is up.
- **Smoke of Pain** holds its yaw as a *bias* for the whole flight rather than
  folding it into the facing, so the shots corkscrew around the aim line; each
  throws exactly one successor and then halves its own speed.
- **Coldness** is Feathers' fan plus a hop as the spread ends, and it throws the
  fan when it *lands* rather than on a timer.

Three of the four compute a vertical trim as `spread * tau / 360 / N * elapsed`
with the spread multiplied by a literal zero -- the compiler kept the multiply
-- so only Bolt actually climbs or dips.

The bursts cost nothing: types `0x170`, `0x171`, `0x172` and `0x196` are
`j 0x2DB230`, the same two instructions Hand of Pyro's `0x173` runs, so they are
four extra labels on a case that was already there.

Not ported, and called out where they would go: `FUN_0023C220`, the per-victim
offer that swings the battle camera onto whatever was hit, and `FUN_0023BBD8`'s
rumble. `FUN_002D6BD0`, the volley's own hit cue, is here.

### The five level-5 summons

Released at full charge with a live target, an elemental spell is not thrown at
all -- it becomes a creature, and the creature takes the scene over for its run:

| spell | type | behaviour | spawner |
|---|---|---|---|
| 7 Bite of Lightning | `0x13E` | `FUN_002DF018` | `FUN_002DEEF0` |
| 8 Falcon of Death | `0x13F` | `FUN_002E34B8` | `FUN_002E2F50` |
| 9 Hammer of Evil | `0x140` | `FUN_002E23E8` | `FUN_002E1F28` |
| 10 Pinnacle of the Sun | `0x141` | `FUN_002E01F8` | `FUN_002E00D8` |
| 11 Hail of Heavens | `0x142` | `FUN_002E1320` | `FUN_002E0E60` |

The stage all five set is in `ported/entity/original_summon_stage.cpp`; the
creatures themselves are in `actor_frame_update.cpp` beside their launches.
Three things make the stage:

- **The freeze.** `FUN_002DE4A8` raises `+0x02` bit `0x800` on every live slot
  but the target cursors -- the bit `FUN_00239CE0` and `FUN_002261E0` already
  skip on, so one write stops every behaviour and every physics step in the
  scene. The creature then hands the bit back to itself, the player, the
  caster's ground ring and shield effect, and the shared hit effect.
- **The dim.** `DAT_0058BB00` is a 256-bit mask of the slots that fade with the
  stage: every live slot from 2 up whose `+0x96` has neither low bit and whose
  `+0x134` is already zero, minus the handful `FUN_002D6F38` takes back out.
  `FUN_002D6FA0` then stamps `DAT_00355700 / 100` across the whole set every
  frame, so the map's global fade cap and everything standing on it darken
  together and the creature and the caster do not.
- **The camera.** Two natural cubic splines, built in the creature's own frame:
  `FUN_00266CE8`'s sample is pulled apart into a length and an angle, the
  creature's facing is added to the angle, and the result is offset by the
  creature's position. Four to nine points apiece, read straight out of
  `SLUS_200.11` at `DAT_0034FAE8`..`DAT_0034FC7C`. Bite of Lightning is the
  exception: its eye and look-at are simply the two bones carrying roles 1 and
  2 on its own model.

The creature does not move. Every frame it copies the caster's position and
facing, and the caster is turned one capped step -- an eighth of a degree --
toward the angle latched on the frame the camera started. The beats are
animation markers: `(+0xAA & 0xF00)` naming 7, 6 and 3 with `+0x06` bit 2 up
are the three shouts (voice clips 2, 3 and 4 on `DAT_0031DA65`'s channel), and
3 is also where the damage goes off. The blast is the spell's *own* launch
called back at **level 6**, which clamps to 5 everywhere it is read and is not
5 for the `level == 5` test, so a creature can never summon another one; it is
thrown from the target's position, not the caster's.

`DAT_00354ECC` is raised for the whole run. That is what makes the four kind -2
arms of `s14_e031` wait the retail length instead of advancing the frame after
flag 861: with the summons in, the hand-offs move from 2120/2127/2126/2127 to
**2245/2247/2357/2271**.

Not ported, and named at the call sites:

- `FUN_002D7038`, the flat veil the dimmed field recedes into -- one quad over
  the whole virtual screen in bucket 2, under the world. The port has no path
  that submits a raw packet that early, and the two halves that carry the
  effect, the map fade cap and the per-entity `+0x134`, are both in. Its alpha
  is still tracked so the frame-smear handoff stays faithful.
- `DAT_0031DA1E`, the first-time spirit-name banner. Four of the five arm it;
  nothing in the executable draws it, and its only reader counts its timer down.

### The barrier was attached to the caster's hand

For a while the shield barrier drew as a flat green slab standing beside the
player instead of a cylinder around him, and it read as a poser bug because the
port's animation state matched hardware exactly -- animation 0,
`poseColumn(+0xAC) = 1`, `prev(+0xAE) = 18`. It was not. `grp_00bd` is eight
coincident curved sheets on eight children of one bone, with no rotation key in
any of them at the hold column, so the shape is entirely the root matrix's
doing; `shield_of_immunity` has slot 10's `+0x192` at **-1**, and the port had
it at **0 / bone 0x12** -- the caster's hand.

**`FUN_0024BD30` writes neither `+0x192` nor `+0x194`.** `FUN_0024C058:41-47`
(state 111) and the state-113 body both do, and the port shared one
`respawnSlotEffect` helper across all three, so the barrier inherited the hand
attachment and took the caster's hand bone as its root -- a tilted matrix a
metre off the floor. The helper now takes an `attachToCaster` flag and state 115
passes false, which is the original's own split.

The whole live palette matches hardware after it, bone for bone: bone 0 at
`(0.200, -0.000, 0.000)` with a clean 90-degree yaw at scale 1.5, and children
at `(0.222, -0.037, 0.753)`, `(0.219, -0.007, 0.806)`, `(0.220, -0.039, 0.702)`
and on through bone 9's sentinel, every one of them the `shield_of_immunity`
value to three decimals.

### Orphen stood in front of his own shield: entity `+0x133`

With the barrier rooted correctly it was still drawn *behind* the caster -- the
cylinder's near wall never covered him, where hardware shows him dimly through
it. Nothing about the pose was wrong. Blended primitives draw with `ZMSK` set,
so they test depth without writing it, which makes submission order the only
thing that decides who wins; the barrier's origin is the caster's own position
plus half a unit, so it sorted into the caster's bucket and lost.

`FUN_0020C810:216` reads entity `+0x133`, scales it by `fGpffff80c4`
(`0x00352034` = 0.08) into the draw context's `+0x140`, and `FUN_0020EEC0:181`
keys the sort on `ctx+0x68 + ctx+0x140` -- the view depth **plus that bias** --
rejecting the result below `fGpffff811c` (0.1) to the far end of the table. The
port applied the byte on the sprite path only, and the model path's own comment
recorded it as "a field the port does not model".

It is how every effect that wraps a character gets in front of it:

| writer | `+0x133` | effect |
|---|---|---|
| `LAB_002DE0B8` (type `0x143` only) | `-10` | Shield of Immunity's cylinder |
| `FUN_002E34B8` | `-48` on the veil, `+48` on slot 0 | the summon veil, caster pushed the other way |
| ground rings, markers, discs | `-12` | a unit nearer than they stand |
| 44 of the 124 primary descriptors | `-2` | every character, uniformly |

Only `0x143` takes the bias: `0x002DE1F0` tests the type against `323` and
branches past the `addiu v0,zero,-10` / `sb v0,0x133(s0)` pair for `0x127` and
`0x144`, which get their cue and nothing else. Shield of Inferno's cage really
does sort at the caster's own depth on hardware.

`s01_e024` and `s14_e012` are visually unchanged by it, and the `--frames 1200
--actor-report --scr-report` guard is byte-identical on both plus `s14_e001`:
this is a render-side read of a field the simulation was already writing.
### Two HUD pieces the spell demo showed and hardware does not

`s14_e031` drew a health bar for every spell that lands on the target dummy, and
left the target cursor parked in the corner of the screen through the narration
close-up. Both are the same shape of bug: a gate the original has and the port
did not read.

**The bar.** `FUN_00216140:102` arms `FUN_002D5630` only when the victim's
`+0x02` carries one of `0x4B` **and its `+0x96` bit `0x20` is clear**. The dummy
(type `0x8B`) has descriptor flags `0x0008`, so it passes the first test and
takes the upper bank -- and the scene's init writes `32` into its `+0x96`
through object register `0x40` for exactly that reason. That register was the
one `FUN_0025C8F8` case the port had a name for but no implementation, so the
write was counted as unmodelled and dropped, and the gauge slid on. Bit `0x20`
has one reader in the whole executable and this is it.

**The cursor.** `FUN_002D73E8` never returns early. Its VU0 divide is clamped,
so `FUN_0020B600` always writes a screen position back, and the two depth
rejects either side of it -- `w` over `DAT_0035479C`, the pre-divide `z` at or
under `DAT_003547A0` (0.3) -- only raise `+0x198` bit 0 and fall through to the
tail, where a non-zero `+0x198` is what raises `+0x08` bit 0 and stops the draw.
The port bailed out at the rejection instead, so the marker kept the last
position it had projected to; the narration camera looks away from the dummy, so
that position was the top-left corner and it stayed there.

Two more gates went in with it, both of which decide the same thing and neither
of which fires in this scene: `DAT_00354FC2 & 5` must read exactly 1 (running,
not suspended by opcode `0xBD` method `0x76`), and `DAT_0031DA6C` bit `0x20` on
the driven member hides them too. The third term, `DAT_003555C6`, is the
attract-mode demo flag `FUN_00271558` raises on a title-screen timeout; the port
has no attract mode, so it is zero by construction.

All eleven arms still hand off to the same destination scenes, and the
`--frames 1200` guard is byte-identical on `s01_e024`, `s14_e001` and
`s14_e012`.

### The second bandana, and why destroying an entity is not releasing a slot

Orphen wore two bandanas through `s14_e031`'s camera swoop -- one trailing off
the close-up bust correctly, a second forking away from it. The extra one was a
real entity, pool slot 14, type `0x19`, and it had been alive since the scene's
init entry.

`s14_e031` builds the close-up rig **twice**. The init entry runs opcode `0x13F`,
which clears `+0x94` and calls `FUN_002D2F40`; that lands a mount at slot 11 with
hair at 12, bust at 13 and cloth at 14. The scene then tears that first rig down
again with three opcode `0x5C` calls -- slot 13 (the `0x26` bust), slot 12 (the
`0x27` hair), slot 11 (the `0x28` mount) -- and builds a fresh one later at
10/11/12/13.

**It never names the cloth.** It does not have to: `FUN_0025F238`, opcode `0x5C`'s
whole body, is a call to `FUN_00265EC0`, and `FUN_00265EC0` calls `FUN_00265F70`,
which destroys every entity whose `+0x192` names this one **through `FUN_00265EC0`
again**. The cascade is the whole subtree. Destroying the bust takes the cloth
hanging off it, because the cloth is the bust's child.

The port had two separate divergences that added up to the same entity surviving:

- Opcode `0x5C` called `EntityPool::releaseSlot`, which is the map-load blank --
  no light slot given back, no cascade at all.
- `FUN_00265ec0_destroy_entity` did cascade, but only one level: it released each
  direct child with a bare `releaseSlot` rather than recursing. A grandchild
  always survived.

So the first rig's cloth outlived its bust, its `+0x192` kept naming slot 13, and
slot 13 was recycled into the *second* rig's cloth -- which is why the stray
bandana tracked the real one instead of hanging somewhere random.

`FUN_00265EC0` is now one function taking a pool and the light table, with the
`ActorEnvironment` form as a thin wrapper, and `0x5C` and `0x142`
(`FUN_002606D0`, which calls `FUN_00265F70` outright) both go through it. Two
details of it that are easy to miss:

- The status byte is cleared **before** the rescan -- the original zeroes
  `DAT_005A96B0[slot]` at the top of `FUN_00265EC0`, ahead of everything else.
  That is what stops a parent cycle looping, and the port's release-then-scan
  order stands in for it.
- A slot whose type reads `< 1` takes the short branch: `+0x96`, the type and
  `+0x95` are cleared and nothing else happens -- no light freed, no cascade.

Verified: the rig at frame 1400 of arm 853 is four entities (mount 10, hair 11,
bust 12, cloth 13) plus the field bandana in slot 4, where it was five; the swoop
draws one tail; all eleven arms still reach the same destination scenes; and the
`--frames 1200` guard is byte-identical on `s01_e024`, `s14_e001` and `s14_e012`.

## s14_e001: the swarm's skitter is a music slot, not a cue

The swarm's continuous chitter is **not** in the cue table at all, which is why
looking for it there found nothing. It is a looping SEQ on a music slot, started
by `FUN_0027B380:76` -- the crab's death -- three lines after the hundred crabs
are released:

```c
FUN_0027c950(param_1);   // release the swarm
DAT_00355270 = 0;
FUN_00205d90(4,1000);    // and start slot 4, looping, at a full fader
```

`FUN_0027DC38:85-97` then steps it down as the swarm thins: at 70 left it ramps
slot 4 out and starts slot 3, at `fGpffff9290` = 30 it ramps 3 out and starts
slot 2, and when the last one dies `FUN_00205F40(2)` stops it. So the bed thins
with the swarm instead of cutting.

The port had none of it. `FUN_0027B380`'s call was a comment ("keys the death
sting on its own channel") and `FUN_0027DC38`'s three were written off as
"channel fades, which this port's audio path handles for itself". It does not --
nothing else starts these slots.

**And the slots would not have played anyway.** s14_e001 asks for SND.BIN
resources 98, 99 and 100 in slots 2, 3 and 4. Only 98 carries samples; 99 and
100 open section 1 with `NVB5` instead of a `pBAV` header, and `SoundBank::load`
rejected them, so both slots reported `no sequence`. `FUN_00205548:24-28` is
what that marker means:

```c
if (sVar1 == 0x564e) {                       // section 1 starts "NV"
  iVar4 = -(*(byte *)(sec1 + 3) - 0x30);     // the ASCII digit, negated
  param_1[3] = iVar4;                        // as both section sizes
  param_1[1] = iVar4;
}
```

and `FUN_00205310:36-45` reads the negative size back as *borrow*: copy channel
`digit`'s VAB id rather than uploading one. Channel n is music slot n - 3, so
`NVB5` is channel 5 is slot 2 -- resource 98's bank, shared by all three
sequences. `SequencePlayer` now carries a `borrowedSlot_`, and `bank()` answers
with the lender's.

Each of the three is one channel of program 0 between `CC99=20` and `CC6=127`,
loop-forever, playing overlapping short notes at scattered pitches -- 100 the
densest and lowest, 99 higher, 98 sparsest. That is the skitter.

Verified: slot 4 starts at frame 3083, the frame the swarm is released, and at
frame 6000 the report reads `loops taken 2, ... playing`. Mixed output rises
from ~4800 RMS to ~6650 across the start. The 70- and 30-crab step-downs and the
final stop are transcribed but **not** exercised here: the headless player
cannot kill swarm members, so nothing has yet driven the count down.

Separately, cue `0x11C` is the only entry type 0x7E has in the cue table -- one
`jal 0x00267d38` at `0x00277A34`, on the state-6 leap. It fires 37 times over a
9464-frame swarm phase, about one per 250 frames, ~0.43 s at volume ~38 of 127.
It is a per-leap lunge, not the bed, and it starts ~330 frames into the phase
because a fresh member must walk to its mark, cross to `x -11..-6` and settle
into state 3 before `FUN_0027B918` will take it.

Reading the cue log is the check for a *cue*, not listening: `FUN_00267D38_play_at`
logs **every** request, including `out of range` past 14 units and `no such cue`.
A cue absent from `--sound-report` was never requested -- but a missing sound
that is a music slot will never appear there at all. Check the report's slot
table for `no sequence` and its `music slot ... play` lines too.

### `--enemy-hp <slot>=<hp>[:<frame>]`

Writes one pool slot's live hit points (`+0x12A`), the same field the stat
record fills at spawn. A harness probe for reaching a boss's later phases.

It is not sufficient alone -- the crab only dies through the damage path, so it
still needs a hit to finish:

```
--enemy-hp 65=1:2000 --hold-cross 1900-1910 --hold-cross 1930-1940 ...
```

kills it around frame 2100 and puts the swarm on screen by 3536.

Without it there is no headless route. The player never walks, the crab drifts
out of sword range, and its HP plateaus at exactly **84 of 120** however long the
run -- 9,500 frames and 26,500 frames both end there. `--spell-power-scale` does
not help: it scales power, and the problem is hits not landing.

## s14_e002: the boss *is* the scene

`s01_e013` hands off to `s14_e002`, the fight in the ship's rigging. The port
dropped Orphen into empty air and stopped dead.

Neither half of that was collision or spawn. The scene's object script is a
`work[0]` state machine in beats of ten, and beat 0 places the lead at
`(-5.2, -2.3, 2.25)` -- a spot with **no floor under it**, on hardware too: the
ground query there answers `-45`, the no-ground sentinel. Hardware does not fall
because two frames later the boss picks the player up.

The handshake is three beats:

| beat | what it does |
|---|---|
| 10 | wait until the boss's `+0x60` is non-zero -- state 0 has run |
| 20 | opcode `0xBD` method `0x6F`: request action 12 on the entity in `work[2]` |
| 30 | wait on script work word 1, which only `FUN_0029C510` writes |

Type `0x95`'s `FUN_00299390` was unported, so its `+0x60` never left 0, beat 10
never passed, and the lead fell until the sentinel caught it. Everything else --
the map, the 16 spawns, the placement table, the script -- already matched the
save state exactly.

### The intro flies the player, not the boss

`FUN_0029C198` is state 13, and it is a **camera path applied to pool slot 0**.
It takes two natural cubic splines out of `DAT_0034EB60` -- six blocks of three
points, taken in pairs, with the player's own position substituted for the first
control point -- and for 2 x 4800 ticks writes slot 0's `+0x20/+0x24/+0x28`
straight off the curve, its `+0x5C` from the tangent one frame ahead, and swaps
its animation from 12 to 13 at the halfway mark. That is the leap up the
shrouds. `FUN_0029D658` is the same move again for the fight proper, selected by
the work block's mode byte instead of by the state.

Checked against a save state of the real transition, aligned on the first frame
the curve moves the player:

|  | hardware (frame 15201) | port (frame 31) |
|---|---|---|
| slot 0 | (-5.5451, -2.5466, 6.9092) | (-5.55, -2.55, 6.93) |
| camera eye | (-5.9802, -3.9707, 8.257) | (-5.98, -3.97, 8.27) |
| camera look-at | (-5.533, -2.5389, 7.757) | (-5.53, -2.54, 7.77) |

and the curve's last control point, `(-6.175, -0.894, 12.599)`, is where slot 0
is standing when the carry ends -- grounded onto the crow's nest at 12.4, which
is what hardware reads too.

### Bit 3 of `+0x04` turns gravity off, and the lead's copy did not have it

`FUN_002262C0:99` reads the entity's `+0x04` into its workspace and gates the
whole velocity integration on bit 3:

```c
if ((*(ushort *)(puVar11 + 0x58) & 8) == 0) {   /* +0x160 is a copy of +0x04 */
  ... +0x38 += v*dt - (g*dt)*dt*0.5;  v -= g*dt;
}
```

The non-player path in `actor_frame_update.cpp` has had that gate since the
crates; the **lead player's copy never did**. Both carries raise the bit for
their whole run, so in the port the lead accrued about a third of a unit of fall
per frame under a spline that teleported it back. The position came out right
because the curve overwrote it, but the camera reads the player *before* the
curve write, so the look-at trailed 0.34 low the entire leap.

Fixing it changed nothing in `s01_e024`, `s01_e012`, `s01_e014`, `s14_e012` or
`s14_e031` over 1800 frames. In `s14_e001` it removed twelve `[player]
primitive=` lines and nothing else: the crab's own Bezier carry raises the same
bit, and the lead's ground query had been flickering between primitives
underneath it.

### State 4 had no `src/` file, and it is state 3 mirrored

Ghidra produced no function at `0x00299C98`, which is the fourth entry of
`PTR_FUN_00325E50` -- and the *first* move the rotation picks, so nothing could
be checked without it. Disassembled out of `SLUS_200.11` (capstone in MIPS64
mode; MIPS32 chokes on the EE's `daddu` and stops after one instruction), it
turns out to be `FUN_002999B0` with three differences: `+0x1C1` is 1 rather than
0, the orbit turns the other way, and the close shot comes in on camera sub-shot
1 rather than 2. Its five tuning constants at `0x003538B4` hold the same values
as state 3's at `0x0035389C`.

Three more entries are bare `jr ra` -- `0x00299868` (1), `0x0029A4E0` (7) and
`0x0029C190` (11), two instructions each. Real no-ops, not gaps.

### The move rotation, and the geometry it flies

`DAT_00325E28` is eighteen entries cycling: 4, 5, 6, 5, 3, 5, 6, 8, 6, 4, 5, 8,
5, 8, 3, 5, 6, 9. `FUN_0029C468` walks it, and it is also where "the player is
down" (state 2) and "I am dead" (state 12) override the pick.

States 3, 4 and 6 are one orbit: twenty units out, five below the water, twelve
of turn per 32000 ticks, for `0x1900` ticks -- and the radius pulls in by up to
six as the pass comes abeam, `r = 20 - 6·|cos(angle)|`. Checked both ways: the
save state at PS2 frame 15896 has the boss in state 4 at `(10.8966, 11.5569,
-5.0)`, which is radius 15.884 against the formula's 15.888, and the port's own
state-4 samples sit on the same curve to three decimals with the height at
exactly -5.00.

State 5, the dive, is seven of the eighteen: a three-point curve down one of
three lanes in `DAT_00325D38`, the player dropped on the spot that lane and
direction call for, a splash each time the body crosses the water line, and a
wake that sheds three parts when its clip ends.

**The body is nine bones, and they all get the same pair of angles.**
`FUN_0029C7A8` measures the lag between where the head is actually pointing and
where the entity says it is facing, and writes yaw × 8 and pitch × 10 into every
one of `DAT_0034EB40`'s nine. The rotation compounds down the chain, which is
what makes the body arc rather than kink. That is a different nine from the
segments': those ride `DAT_0034EB30`.

### The rest of the fight: states 8, 9, 10 and 12

All fourteen state handlers are ported now, along with the four helpers the
wrapper runs once the mode byte reaches 14. Over 30000 frames the rotation
cycles all eighteen entries of `DAT_00325E28` and wraps, and `--actor-report`
finds nothing left:

```
type=0x95 state=0 -> 0x2995e0 ticks=1      implemented
type=0x95 state=1 -> 0x299868 ticks=360    implemented
type=0x95 state=3 -> 0x2999b0 ticks=2010   implemented
type=0x95 state=4 -> 0x299c98 ticks=2010   implemented
type=0x95 state=5 -> 0x299f80 ticks=15030  implemented
type=0x95 state=6 -> 0x29a2c0 ticks=3855   implemented
type=0x95 state=8 -> 0x29a4e8 ticks=3840   implemented
type=0x95 state=9 -> 0x29a838 ticks=2592   implemented
type=0x95 state=13 -> 0x29c198 ticks=302   implemented
unimplemented state handlers: 0
```

#### `FUN_0029DED8` is what makes the fight a fight

The boss was not targetable at all before this pass. `FUN_0029DED8` has two
arms and neither registers the boss: while `+0x1B1` is clear it **empties**
`DAT_003253C0` outright, and while it is set it registers exactly one entity --
the *first body segment*, work `+0x4B0`. The other eight segments are never in
the table. `+0x1B1` is raised by the states that hold still long enough to be
hit (8 and 10) and dropped by the ones that do not, which is the whole of the
fight's "you can aim at it now". A type `0x192` cursor now appears in the pool
for 17340 of 30000 ticks.

#### One write in `FUN_0029D658` decides whether the fight finishes

State 9's phase 11 waits on the work block's carry byte going back to zero, and
the carry is `FUN_0029D658` mode 1: two 4800-tick curve legs counted on the
*player's* `+0x62`. The port had `(&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C] =
0x0B` written down as a comment rather than a call, on the grounds that nothing
outside the battle module writes that byte and the mode was unreachable anyway.

It is not cosmetic. That byte parks the player's own state machine, and the
first thing the machine does when it runs is reset `+0x62`. With it left out the
carry's timer sat at 32 or 33 for ever, the curve never advanced, and the boss
waited at phase 11 for the remaining 12000 frames of the run. The trace was

```
[mast9] phase=11 frame=7339
carryMode=1 carryPhase=1 route=2 ramp=32 frame=7339
carryMode=1 carryPhase=1 route=2 ramp=33 frame=7346   <- 7 frames, 1 tick
```

With the write in place the same run reads `carryMode=0 carryPhase=0 route=3`
one frame before phase 12.

#### Nothing picks state 10

`DAT_00325E28` tops out at 9, `FUN_0029C468` only ever overrides with 2 or 12,
and the action map handles 12, 13 and 14. The only writer of `+0x60 = 10` in
`src/` is `FUN_0029B628` itself. It is ported because it is in the table and
because `+0x1BE`, the mast-section counter it owns, is read nowhere else -- but
a fight that runs off the rotation alone never breaks a plank, and the water
line never rises.

#### Two blocks in the retail build are dead

Ported as written, marked where they sit:

- **State 12 phase 1 dereferences null.** `0x0029BF50` branches to the
  sub-timer when work `+0x48C` is non-zero; the other arm loads `+0x06` *off
  that null pointer* and gates cue `0x13D` on bit 0 of whatever the EE has at
  address 6. Phase 0 always fills `+0x48C` before handing over, so the arm is
  unreachable.
- **State 9 phase 12's `+0x1BF > 2` arm**, which stands the player at the origin
  and puts 999 in his `+0xBE`. The guard at the top of state 9 gives the turn
  back from `+0x1BF == 2`, so the counter never reaches 3 with the block in
  reach.

#### `FUN_0025D0E0` is per-frame in the original and sticky here

Both of state 9's white flashes paint the screen through `FUN_0025D0E0`. In the
original that pushes one screen-sized sprite into *this frame's* draw list, so a
caller that stops calling it stops covering the screen. The port's `ScreenFade`
holds the last value it was handed and `PortRuntime` pushes that to the renderer
every frame, so the first build of state 9 whited the screen out at phase 5 and
never took it back -- a `--screenshot` of the frames after phase 5 came out
solid 0xFFFFFF.

Anything in the port that paints the overlay directly has to release it. Both of
the boss's users now do -- phase 5 on its way to phase 6, and `FUN_0023ABD0` on
the frame it reports done.

#### The rest of the director: shots 3, 4, 6, 7, 8, 10 and 12

`FUN_00298160` has twelve numbered shots and all twelve are in now. Six of them
are more poses off the boss or the player and read like the ones already there.
Three are not:

**Shots 6 and 8 are camera paths, not poses.** Each drops the manual camera,
installs a spline through `FUN_00217E88` -- an eye curve and a one-point look-at
curve, and *no* roll/zoom curve, which is the whole difference between
`FUN_00217E88` and `FUN_00217FE8` -- and walks it on its own `DAT_0035532E`
counter. Shot 6, the transformation, authors four points in the boss's **bone 8
space**, turns them by the boss's facing less 97.93 degrees and drops them on
wherever bone 8 was when the shot started, while aiming at bone 8 live every
frame; shot 8, the beam, authors three points as a plain offset from a row of
`DAT_00325DD8` picked by the boss's `+0x1BF`, and does not rotate at all. Both
hold the curve's last point once the counter runs out, and both of those "last
points" are `DAT_00325DCC` and `DAT_00325E18` -- which are not separate
constants, they are the final entry of each curve.

`FUN_0020BAE0`, which builds shot 6's rotation, writes **four** of the sixteen
floats in `DAT_00342828` and leaves the rest alone; every other caller in the
engine hands it a matrix `FUN_0020BC38` has just made identity, so identity is
what the standing contents are. The port builds one fresh rather than relying on
that.

**Shots 4, 7 and 10 roll their own framing.** The orbit angle is
`FUN_0029CC28(0, boss)` -- which memsets its own scratch to zero and never fills
it, so it is the bearing from the **world origin**, not from anything nearby --
plus fifteen to forty-four degrees off `FUN_00216868`. Two runs of the same move
are never framed identically.

Two details are reproduced rather than corrected, and marked where they happen:
shot 5's limit tests read `DAT_00355324`, which shot 5 never writes, and shot
8's look-at adds `DAT_003556FC` twice -- once inside the anchor and once again
on the way out, which is why it sits over the creature rather than level with
it.

`DAT_0058B190` also stopped being a synonym for the eye. It is the **anchor**:
eight of the twelve shots stamp a point there on the frame their sub-shot
changes and build every later frame off it, which is what makes a shot a fixed
frame the creature flies through rather than a follow cam. Shots 1 and 2 were
writing their eye into it, which would have moved shot 4 sub 2's and shot 11's
framing under them.

Over a 30000-frame run the director reaches shots 1, 2, 5, 6, 7, 8, 9 and 11.
Shots 3, 4, 10 and 12 belong to states 10 and 12, which the move rotation never
picks -- see "Nothing picks state 10" above.

#### The strafing run fired nothing, and left a slick on the water

Reported as: the creature swims out, charges, is supposed to shoot something,
nothing happens, and it leaves an effect in the water that never goes away.
Both halves are one missing function.

State 8's animation 0xC calls `FUN_0029D168` on the release frame, which stands
up a type `0x1AE` on the creature's bone 9 and hands it attack record 0 out of
the work block. The port had that much. What it did not have is type `0x1AE`'s
own behaviour, **`FUN_002EDC40`** -- the dispatch word at `0x0031CDD4`, which is
`PTR_LAB_0031CAB0[0x1AE - 0xFC]`. There is no `src/` file for it; it was
recovered from the ELF disassembly and cross-read against Ghidra's decompile.

That function is the whole attack. It lays down a three-point Bezier per axis,
once, latched on `+0x94`:

```
p0 = where the wash stands
p2 = the player, one unit further along the bearing, at waist height
     (player +0x28 + player +0x58 * 0.5)
p1 = half way there, three units up
```

and then walks it, spending the curve through the movement request at
`+0x30/+0x34/+0x38` rather than writing the position, so the wash still
collides on the way. Every frame it hit-tests through `FUN_002EF510` with the
record it is carrying, switches to animation 2 on contact, and expires three
ways: `+0x62` past `+0x1C0`, `+0x0C & 6`, or the burst animation finishing.

Without it the entity just sat where it was stood up, for ever. Over a
12000-frame fight, before and after:

| | live `0x1AE` at the end | slots used | dispatch frames |
|---|---|---|---|
| before | **6** | 6 | 27195 |
| after | **0** | 1, reused | 606 |

Six shots go out, at frames 3105, 4264, 5021, 9782, 10941 and 11698 (cue
`0x134` marks each release). Sampled across one flight the wash leaves the
creature at `(-3.04, 20.24, 6.75)`, arcs up over the deck and comes down on
`(-6.11, -0.85, 13.02)` -- the player was standing at `(-6.10, -0.80, 12.40)`.

Two details the decompile alone would have got wrong. `FUN_0021ED50`'s ninth
float, `baseZ`, arrives **on the stack** (`float in_stack_00000000` in its own
body), so Ghidra renders the call with thirteen arguments and no Z;
`swc1 f2,0x0(sp)` at `0x002EDF80` is the wash's `+0x28`. And the two spray
constants are gp-relative: `uGpffffab54` at `0x00354AC4` is 0.001 and
`uGpffffab58` at `0x00354AC8` is 0.05, both read off hardware.

**It does not damage the player yet, and that is a different bug.** See the
next section.

#### Nothing can hit the lead, because its +0x02 is zero

Found while checking whether the wash connects. Instrumented at the closest
frame of a pass, every geometric test in `FUN_00215AC8` passes:

```
wash box   x[-6.86,-5.36]  z[-1.60,-0.10]  y[13.02,14.52]
player     centre(-6.10,-0.80) r=0.15  foot 12.40  head 13.20
           12.40 < 14.52  and  13.02 < 13.20        -- overlaps
```

and the contact is thrown out before the shape is ever measured, by
`candidateAccepted`:

```
if ((victim.descriptorFlags02 & candidateMask) == 0) return false;
```

The port's lead has `+0x02 == 0x0000`, so it fails every mask. **Hardware has
`0x0001`**, in both EE dumps of this scene. `+0x04` is wrong too: `0x3024` in
the port against `0x30A4` on hardware.

The cause is that `OriginalPlayerController::resetAt` hand-builds the lead out
of `OriginalEntity{}` defaults and sets only `typeId00 = 1`. It never seeds
`+0x02` or `+0x04` from type 1's descriptor the way
`FUN_00265E28`/`entity_pool.cpp:99` does for everything else. The radius, height
and slope limit next to it are hardcoded copies of that descriptor's values
rather than reads of it, which is the same shortcut one field further along.

The consequence is not limited to the wash: outside a battle that retypes the
lead to `0x5C` (which does `+0x02 |= 1` at `battle_party.cpp:667`), **no attack
in the game can land on the player at all.** `s14_e002` is such a scene --
hardware keeps the lead at type 1 through the whole fight.

#### The disintegration sparkle ended up on Orphen

After the fight, the victory close-up had nine white starbursts stuck to
Orphen's hands and torso -- the effect the creature throws off as it comes
apart.

The creature's death spawns nine type `0x1C5` limbs, one per segment bone, and
binds each to itself:

```
*(short *)(limb + 0x192) = (entity - 0x58beb0) / 0x1d8;   // its own pool slot
*(char  *)(limb + 0x194) = bone;
```

(The magic-number spelling Ghidra prints, `* -0x5f75270d >> 3`, is exact
division by `0x1D8`: `0xA08AD8F3` is the inverse of 59 mod 2^32, applied after
`>> 3`.)

Type `0x1C5` has **no behaviour of its own** -- `PTR_LAB_0031CAB0[0x1C5 - 0xFC]`
at `0x0031CDD4` reads `0x00239E78`, the no-op, which the same table's `0x1E3`
entry (`0x002F13D0`, the known hit-effect handler) confirms is indexed right. So
nothing retires a limb on its own timer. What retires them is the parent going
away: `FUN_00265EC0` calls `FUN_00265F70`, which walks all `0x100` slots and
re-enters `FUN_00265EC0` for every live one whose `+0x192` names the slot being
freed.

The port's death did `pool.releaseSlot(slot)` -- the bare one-slot free --
instead of `FUN_00265ec0_destroy_entity`, which has the cascade. The nine limbs
stayed behind holding `+0x192 = 25`, and the victory close-up rig (type `0x28`)
is allocated into the slot the creature just vacated. `FUN_0020C810`'s attached
branch only asks whether the parent was **drawn this frame** -- the `+0x0C` bit
`0x1000` latch -- never whether it is still the same entity, so the orphans
re-parented onto Orphen and drew off his bones.

The nine type `0xBF` body segments survive the cascade, and that is correct:
`FUN_002995E0` never writes `+0x192` on them. They are positioned outright by
`FUN_0029CCB8` every frame rather than bone-attached, which is also why they
never appeared on Orphen.

Verified by killing the creature headlessly (`--enemy-hp 25=1:2450` with
`--press-attack` on a timer): `--actor-report` at frame 4000 goes from nine live
`type=0x1c5` at `pos=(0,0,0)` to none, live actors 40 -> 31, the nine `0xBF`
segments unchanged, and the victory pose captures clean. Seven-scene guard
byte-identical.

The other nine releases in `original_mast_boss.cpp` were the same divergence --
every one of them is `FUN_00265EC0` in the original -- and are now the cascading
form too. They change nothing observable, because those entities have no
children: the death run is byte-identical with and without them. That is the
point. The port matching the original's shape is not conditional on the
difference being visible yet.

#### Hitting the creature did nothing visible or audible

Reported as "it's supposed to flash red and make a noise -- I don't think we
have either in place". Both were real, and they were two unrelated bugs that
happened to land on the same event. `FUN_00299390`, the boss wrapper, does
three things when `+0xBE` -- the damage mailbox -- comes back non-zero:

```
FUN_00295A60(0x131, entity);       // the noise
*(entity + 0x1BC) = 0xC80;         // the flash timer
FUN_002D5630(...); *(entity + 0x12A) -= *(entity + 0xBE);
```

**The noise went through the wrong wrapper.** The port called
`FUN_00267D38` here -- volume 100, real distance -- where the original calls
`FUN_00295A60`, the boss's own `-1` wrapper described in the section below.
This is not the same bug as that one: that one was about the cues being faint,
and `0x131` was not faint, it was *gone*. The creature orbits twenty-odd units
out, `FUN_00267A80`'s cutoff is fourteen, and the hit cue is emitted from the
creature. Twelve hits landed over a 6400-frame run with the sword swinging on
a timer; `--sound-report` on the old build shows all twelve as

```
frame 2608 cue 305 ... dist 21.311993 ... vol 0/0 -> waveform 0, 0 samples  out of range
```

and on the new one all twelve as `dist 0.300000 ... vol 58/58 ... played`.

**The flash was written and never read.** `+0x1BC` is only a timer;
`FUN_0029CCB8` counts it down and, while it runs, holds entity **`+0x138` at
`0x14C8`**. `+0x138` is the general additive tint, and `FUN_0020EEC0:53-61` is
the only thing that consumes it:

```
if (*(int *)(entity + 0x138) != 0)
  for (i = 0; i < 3; i++)
    ctx[0x1BC + i] = min(255, ctx[0x1BC + i] + entity[0x138 + i]);
```

`ctx+0x1BC` is the byte triple VU0 has just written from the dynamic point
lights, so the tint is added in the same place and on the same scale, then VU1
divides the lot by 128. The port had the whole simulation side right --
`fadeColor138` is written in six places, both bosses, the swarm crab, the fade
ramp and object register 0x23 -- and **the draw path never looked at the field
once**, so nothing in the game has ever flashed when hit.

`0x14C8` is R+200, G+20, B+0. The fix adds the three bytes into
`DynamicContribution::additive` before the modulator runs, and has to be able
to raise the contribution pointer on its own, because the original writes
`ctx+0x1BC` from VU0 unconditionally and then tests `+0x138` separately --
gating the tint on the scene having point lights would have dropped it.

Hardware settled the two things that could have made this wrong. Paused on a
damage frame, boss in slot 25: `+0x1B2` is 1 (the gate), `+0x1BC` is `0x0A00`
(decayed from `0xC80`), `+0x128`/`+0x12A` are 66/63, and `+0x138` is exactly
`0x000014C8`. The player in slot 0 and the body segments in slots 26+ are all
`+0x138 = 0`, which is what makes an additive read safe: zero is the resting
value, and the segments do not flash -- only the main body does.

The one value that looked alarming is `FUN_0023A568`, which ramps `+0x138` up
to `0xFFFFFF` and leaves it there. Added, that is pure white. It is *meant* to
be: the function only runs for entities carrying `+0x04` bit `0x800`, it
brightens them to white over about eight frames, then fades `+0x134` down and
releases the slot. It is a dissolve, and the port was not drawing that either.

Verified: the seven-scene guard is byte-identical, and so are fourteen
screenshots across those scenes on both builds -- nothing else in the game
currently carries a non-zero `+0x138` on screen. At `s14_e002` frame 2612, four
frames after a hit, the two builds differ by `R+181, G+74, B+0` over the
creature: red up, green up a little, blue untouched to the bit, which is the
`0x14C8` signature.

#### The creature was too quiet, and that is a real bug

`FUN_00295A60`, the wrapper every one of the boss's own cues goes through, is
`FUN_00267D88(cue, entity, -1)`. The port routed it through
`FUN_00267D38(cue, entity)` instead, which is the same call with the volume
nailed to **100**.

That is not a small difference. `FUN_00267A80` reads a negative volume as *do
not attenuate*: it puts the scale back to 100 **and** replaces the measured
distance with `fGpffff8D9C`, which is 0.3. So the cue keys on at 125 of 128
wherever the source is, and the fourteen-unit cutoff at the top of the function
-- which makes a `FUN_00267D38` cue **silent**, not quiet -- cannot fire. The
fight orbits twenty units out, so on the port half the creature's roars were
being dropped on the floor and the rest were faint.

The sound engine has had that branch since it was written; nothing in the actor
layer could reach it, because `ActorEnvironment` only carried the fixed-100
wrapper. It now carries `FUN_00267D88` as well, and three places use it: the
mast boss's cue wrapper, and the crab's `FUN_0027BBE0` hurl and `FUN_0027CFE0`
splash, both of which had the divergence written down in a comment.

The same hook fixed script opcode **0x126**. `FUN_00261330` reads an inline cue
id, an expression for the entity, and -- for 0x126 only -- a second expression
that is the **volume**, which it hands to `FUN_00267D88`. The port evaluated
that expression and threw it away, which made 0x126 identical to 0x125.

#### Orphen hopped on the spot, and it was not the boss

Reported from play: idle in the fight he lands, stands for a second or so, hops
again, and cannot cast while hopping. It is a 120 -> 108 -> 120 loop.

Battle state **120** (the idle) arms a timer and on expiry measures the
character against control block `+0x14/+0x16`, the mark `FUN_00243F80` recorded
when the battle started. Drift past `fGpffff8844` and it hands off to **108**,
the walk home, which walks back and returns to 120. Every pass this boss makes
calls `FUN_0029C538`, which teleports the player onto one of the six lane spots
-- about three tenths from where the battle recorded him -- so 120 fires, 108
starts a return walk, and the player **did not move**, so 108 "arrived" three
frames later at the same spot and 120 sent him straight back. `FUN_002462C8`
returns on current action `0x87` before it reads a button, which is why the pad
is dead for those three frames.

The reason he did not move is one line in the frame:

```
FUN_002239C8:118   FUN_00251ED8(...)    the field controller      -- or --
FUN_002239C8:121   FUN_00249610(...)    the battle module
FUN_002239C8:129   FUN_0023FD30()       -> FUN_002446E8, the path followers
FUN_002239C8:136   FUN_002261E0()       physics for ALL 0x100 slots, slot 0 too
```

**Battle replaces the controller and nothing else.** `FUN_002261E0` loops the
whole pool from `0x58BEB0`, so the lead keeps its physics, spends `+0x30/+0x34`
and follows the ground whatever is driving it. The port had slot 0's physics
only inside `OriginalPlayerController::update`, which the battle branch skips,
so in battle nothing spent the request `FUN_002446E8` had just written.
`PortRuntime::update` now runs `leadPlayer_.FUN_002261e0_step_physics` after the
path followers whenever the battle is active.

With that in, state 108 walks him from where the pass dropped him to exactly the
recorded mark and stops -- ten short corrective steps over 6000 frames, one per
pass, instead of seventy-six.

Two smaller things came out of the same read:

- `fGpffff8844` at `0x003527B4` is **0.2**, not the 0.3 the port carried.
  `FUN_0024CF20:0x24D0E4` loads it into a `c.olt.s threshold, distance`.
- `FUN_0029D658` mode 9's two missing calls went in: `FUN_00249388` points the
  player's record back at target-marker row 0, and `FUN_00245978` re-records his
  home spot at wherever the carry left him. Neither is what was causing the hop
  -- mode 9's `+0x94 == 0x0E` arm is not reached by the intro handback -- but
  both are part of the handback and both were missing.

`s14_e012` is the one regression-guard scene whose report moves, and only in the
lead's ground bookkeeping: `triangle=690 -> 691` and `terrainWord=0x0 ->
0x20000001`. The position is identical. That is the physics doing its job in a
battle where previously it did nothing at all.

#### What is still out

- `FUN_0023BBD8`, the pad rumble, everywhere. No rumble path in the port.

(Type `0x1AE`, the strafing run's wash, used to be listed here. It is ported
now -- see *The strafing run fired nothing, and left a slick on the water*
above.)

The fight's own numbers were taken out of `SLUS_200.11` rather than out of a
note: `DAT_00325CF0` (the three smash runs), `DAT_00325D80` (the three beam-head
spots), `DAT_0034EB50` (the four collision groups each mast section is made of),
`DAT_00355340`/`DAT_00355348`/`DAT_00355338` (the plank tags and group bits) and
the `gp`-relative float run at `0x003538D8..0x003539FC`. The states themselves
are verified structurally against the disassembly and by running the rotation to
a full wrap; they are **not** frame-checked against hardware, because reaching
state 9 on the PS2 means playing the fight for several minutes rather than
loading a save state.

#### The chapter ends here, and it ends three different ways

The boss dying is not the end of `s14_e002`. The scene hands off to `s14_e031`
for the Bolt of Thunder reward, `s14_e031` hands back, and then `s14_e002`'s own
script runs the chapter's exit: **two FMVs and one of three destination maps**.

The port used to stop dead at the first statement of that exit, because it is
opcode **`0x13A`** and the extended dispatch had no case for it. `FUN_00265378`
is one line -- `DAT_003555D2 = expr` -- and the byte is spent later, by
`FUN_0022A418`, which calls `FUN_002F1808` on it once the fade is down and
before the next map loads. Both halves are in now: the opcode, and the block in
`FUN_002239c8_service_scene_change` that spends it.

**The movies are stubbed, and the chain is not.** `\MV3\M01.MV3;1` through
`M19.MV3` are on the disc and the port has no MV3 decoder, so
`PortRuntime::FUN_002f1808_play_movie` logs a line per leg. It keeps
`FUN_002F1808`'s do/while, though, because that loop is what decides how many
films a request is worth:

```
movie 2                       -> then movie 13
movie 17, flag 0x55A clear    -> then movie 1, or movie 3 if flag 0x55B is set
```

`s14_e002` asks for 17, and neither `0x55A` nor `0x55B` is set here, so the
chapter ends on **M17 then M01** -- two films, from one opcode.

**The three-way route is the script's, not the engine's.** At blob `0x0B1E`,
just after the boss goes down, `s14_e002` converts "who is with me" into a route
flag, three symmetric arms:

| condition | sets | companion | destination | spawn |
| --- | --- | --- | --- | --- |
| flag 1377 and not 1881 | 801 | Magnus | `s03_e001` | (0, -22.000, 2.000) |
| flag 1378 and not 1891 | 802 | Cleo | `s05_e051` | (4.655, 3.539, 0) |
| flag 1379 and not 1901 | 803 | Mar | -- | -- |

and then at `0x0F51`, after the `0x13A`, an if/else-if/else on the route flag:
801 goes to `s03_e001`, 802 goes to `s05_e051`, and **anything else falls
through to `s07_e011`**. Flag 803 is written and never read -- the third
companion is the default arm, not a tested one.

`s01_e013` is where the three companion flags are set, at `0x670F`, `0x6CAA` and
`0x7530`, each next to its own `0x524`/`0x525`/`0x526` join flag and its own
dialogue stream. The `188x/189x/190x` flags the exit ANDs against are read all
over `s01_e013` as well, so they are the "this one did not make it" override.

Verified against a PCSX2 save state parked at the end of the reward cutscene.
Hardware has flag 1378 set and 1891 clear, sets 802, requests movie 17 and lands
on section 5 entry 51 with `DAT_003551EC = 1` -- which is the Cleo route. All
three arms run in the port:

```
--scene s14_e002 --enemy-hp 25=0:3000 --frames 7000 --set-event-flag <flag>:0
  1377 -> [movie] M17, M01 -> [scene] s14_e002 -> s03_e001   (frame 6546)  Magnus
  1378 -> [movie] M17, M01 -> [scene] s14_e002 -> s05_e051   (frame 6546)  Cleo
  1379 -> [movie] M17, M01 -> [scene] s14_e002 -> s07_e011   (frame 6546)  Mar
```

With no flag at all it is `s07_e011`, Mar's arm, which is what a port run that
never played the ship chapter should get.

**Landing there is not the same as the scene working.** All three destinations
load their map and run their init, and all three then stop on opcodes this port
has no case for -- `s03_e001` on `0xD8` at `0x151D`, `s07_e011` on `0x124` at
`0x170E` and then `0x8A` per frame at `0x33AD`. `s05_e051` gets further and is
the one to start with. Porting chapter 2's scenes is its own job; what this
section claims is only that the hand-off picks the right one.

**All three arms are named.** Cleo came off the save state -- it has flag 1378
set, and 1378 is the `s05_e051` arm. Magnus is `s03_e001`, confirmed by the
author of this port from play; Mar is then the fall-through by elimination,
which matches `s07_e011` being the arm whose route flag (803) is written and
never tested.

One piece of `FUN_0022A418`'s movie block is deliberately not here. Lines 58-63
arm movie `0x12` when the game arrives at the **title screen** -- section 12,
entry 10 -- and clear flag `0x511`; the port has no title screen and no
`DAT_003555D8`, so the condition can never be true and there would be nothing to
test the code with. The sound teardown around the playback
(`FUN_00206680`, `FUN_00203AA0(4)`, `FUN_0022A1F8`, and the eight halfwords at
`DAT_0031E686`) is left out for the same reason -- there is no MPEG decoder to
hand the display to. The unconditional `DAT_003555D2 = 0` after the block is
*not* left out.
