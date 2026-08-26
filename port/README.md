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

#### What is still missing

`FUN_00259378` and the cell-graph flood fill behind it (`FUN_002584b0`,
`FUN_00258c70`) are **not ported**. That is the follower's pathfinder: the thing
that routes it *around* an obstacle rather than into it. A follower that gets
wedged against geometry while the player can see it therefore has no recovery --
state 6 refuses to teleport on camera, exactly as the original does, and the
original's other exit is the pathfinder. `--actor-report` names it:

```
type=0x37 state=6 -> 0x259378 ticks=6568  UNIMPLEMENTED
```

That only shows up under `--hold-stick`, which pins the lead into a wall for
thousands of frames; normal play walks out of it.

#### The gate that had never been needed before

The party follower is the port's first ground-*walking* non-player actor, and it
immediately found the gap `integrateNonPlayerMovement` had been carrying a note
about. That step committed a move and then raised the actor onto whatever the
ground scan answered with, so a follower walking into the shop counter ratcheted
up it about a tenth of a unit per frame and finished the scene standing in the
air.

`FUN_002262c0` gates that on the destination's slope against the entity's
`+0x80`, the same test the lead's copy has carried since the hull-climbing bug.
It is now applied to non-player actors too, with the same one-axis-at-a-time
slide and the same refusal bits written into `+0x0C` -- which the follower's own
stuck detection reads, and which nothing had ever been setting for a non-player.

**This moves `s01_e024`.** The type `0x62` enemies now get blocked by the hull
where they used to pass through it, so their positions after 3000 frames differ
and the shared RNG stream shifts with them. `s01_e012` is byte-identical at 3000
frames. The change is toward the original, not away from it -- `FUN_002262c0` is
one function and every actor goes through it -- but it is a visible change to a
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

  `FUN_0022b300`, the map loader, reads `DAT_003551ec` as a selector (`0x2001` an
  ordinary scene, `0x20000` the 0xE group) and `DAT_003551f8` as the index into
  the table at `DAT_00315b04`. **This is the one that read as a hang.** The body
  ahead of it waits on the fade-out, releases the player, drops the camera and
  sets flag `0x523` — then asks to leave. Halting there left the slot re-entering
  and halting every frame, 4071 times in one run, with the screen already faded
  to nothing: the scene had finished and simply could not leave. Consumed and
  reported now, so the body reaches its `0x9E` and retires; `--scr-report` prints
  `0x8E scene change requested ... last destination=1`.

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
`--lighting-no-points` turns it off for A/B. Unlike `--lighting-floor` and
`--lighting-unlit` this defaults **on**, because the VU0 list was read back out
of a save state and matched the script's table exactly rather than being derived
from the microprogram alone.

`0xC1` stays operands-only. It is the one member of the family that is not a
plain table write — it claims the entity's own light index at `+0x195` and runs
the offset through `FUN_0020dc88`, the attachment-chain matrix walk, to place
the light on a bone. Neither scene calls it.

One assumption is left in this path: `FUN_0020eec0` reads the entity position it
resolves against from the per-draw context at `+0xA0`, and the port uses the
entity root instead. Nothing in either dump pins `+0xA0` down independently —
the scratchpad region is rebuilt per draw and the save state caught it holding
another draw's data.

`--arm-stream <hex>[:<frame>]` arms scheduler channel 0 with a stream at the
given frame, exactly as opcode `0xA1` does. Most of a scene's cutscenes are not
in the opening chain — they are armed by floor panels, and a panel is a
two-triangle square that no constant `--hold-stick` angle reliably finds. This
reaches them without solving navigation, which is the only way to exercise the
second half of a scene's choreography headlessly:

```
orphen_port --disc-root . --scene s01_e012 --frames 20000 \
    --arm-stream d0c0:11000 --scr-report
```

That takes `s01_e012` from 208 event records and 42 dialogue lines to **260 and
54**, and runs a character walk at `0x705d`/`0x7067` that nothing else reaches.

`--scr-trace-range <lo>-<hi>` prints every SCR opcode executed at a blob offset
inside the range, with the frame it ran on. The aggregate report answers "was
this opcode reached"; this answers "in what order, and did the branch go the way
I think". It is the only way to read a script body — there is no disassembler —
and the offsets to feed it are the ones the report already prints. It is how the
floor-panel guards above were read.

`--hide-slots <slot>[,<slot>...]` drops those pool slots from the published draw
list and nothing else — the simulation is untouched. A report names entities and
a screenshot names pixels; this is what connects them. Bisecting the slot range
against `--screenshot` at a fixed frame, and comparing the `.ppm` bytes rather
than eyeballing, identifies the entity behind any piece of on-screen geometry in
about seven runs. That is how the map-prop descriptor bug above was cornered.

`--press-confirm <frame>[,<frame>...]` fires Cross on each listed frame from
`--frames` or `--screenshot`, so the interaction path is checkable without a
window — the chest cutscene needs two presses, one to open it and one to
dismiss the caption. `--frames` remains exactly deterministic.

## Sound

The port makes noise now, for the two cues the chest cutscene plays. The
sources are `ported/sound/original_sound_bank.*` (the container, the VAB and
the PS-ADPCM decoder), `ported/sound/original_sound_engine.*` (the cue table,
the attenuation and the mixer) and `harness/audio_device.*` (SDL2). The
analysis is in `analyzed/sound_effect_playback.c`.

**The EE never mixed anything.** Every sound function in the executable ends at
a SIF command to the IOP, and the IOP runs a libsnd driver that owns SPU2. So
the ported half stops exactly where `FUN_00204d88` does:

```
FUN_00204d88(0x4069, (vabId << 8) | program, note << 8, volLeft, volRight)
```

and everything past that is the harness's own.

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
- **Reverb.** `FUN_00205938:90-113` sets an SPU2 reverb type and depth per music
  slot. Not ported, so sequences play dry.

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
here, and these lines belong to the same readout as the position display. `P`
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

### Two names in the dispatch tables that are wrong

`analyzed/opcode_dispatch_tables.md` calls opcodes **0xDC** and **0xDD**
"audio_dispatch". They reach `FUN_0023baf8`, which is an **empty stub in the
retail build** -- they do nothing at all. **0xDE** is not audio either: it is a
four-channel timer over `DAT_00571b50`, parallel to the event scheduler.

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

`H` toggles an on-screen overlay showing position and facing, the entity `+0x60`
state and `+0xA0` animation id with its substate frame, grounded flag and
vertical velocity, stick magnitude with the resulting walk/run gait, camera mode
/ yaw / pitch / distance, and the current ground triangle. There is no automated
PCSX2 trace comparison, so this overlay plus `--frames` determinism is how
behavior gets judged.

It stacks below the ported POSITION_DISP overlay rather than through it.

`src/harness/debug_text.*` is a small stroke font, PC-only diagnostics with no
relationship to the game's own. It serves both overlays: the harness HUD lays
out its own lines with it, and `drawOriginalOverlay` stamps the glyphs
`FUN_00268270` already placed -- from slot `0x30`'s atlas when it is resident,
falling back to the stroke font when it is not.

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
