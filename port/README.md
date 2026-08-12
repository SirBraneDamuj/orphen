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

Two behaviors are implemented outright. Type `0x3A`, `FUN_002d1ea8`, the
treasure chest.
It is the only handler in the game with no state table -- it switches on the
animation id directly. Its `+0x198` is an **event flag id** (the placement
record's param byte plus `0x400`), not a pointer; flag clear means closed, set
means opened. See `analyzed/actor_behaviors/type_0x3A_treasure_chest.c`.

### State of play

`s01_e012`, the game's first scene, runs its init, start and per-frame entries
with **zero unimplemented opcodes over 4000 frames** and spawns 77 entities. Its
scene script is byte-identical to `scr/scr2.out`, so the whole existing scr2 body
of work applies -- `docs/scr2_offset_tables_dialogue_voice_flow.md` most of all.

The opening cutscene chain **runs to its end.** From `--scr-report`:

```
event records dispatched: 208     event flag changes: 60+
  frame     1  -> 0x40b2 (slot)      frame   634  -> 0x2c (dialogue)
  frame   544  -> 0x461f (slot)      frame 10354  -> 0xb63 (dialogue)
  frame 10426  flag 0x515 set  at 0x6813
0x6D player lock mode=-1  (state 10)   ...   mode=1  (release)
```

Forty-two lines of dialogue, a dozen camera shots, and characters walking
between marks -- about **three minutes** of cutscene, with **zero unimplemented
opcodes** and `--frames` byte-identical run to run.

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

   `s01_e012` converts fourteen entities but only **five** are characters:
   slots 18, 19, 21, 23 and 24, all really type `0x293`, each with its own body
   (`0x39d9`, `0x3a52`, `0x3b42`, `0x3c32`, `0x3ca9`). The other nine are types
   `0x281` and `0x2ca` sharing two bodies between them — scenery that happens to
   want a script, not cast. `--actor-report` prints the real type and the body
   offset behind the role, because fourteen lines reading `type=0x38` hide both.
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

**Thirteen bugs came out of getting there**, all silent, and the first two would
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
accumulated. A real dialogue port should replace that file rather than build on
it.

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

- **ADSR.** The tones carry SPU envelopes and the mixer ignores them. These are
  one-shots whose decay is in the waveform, so it is close, but a sustained cue
  would hold forever.
- **Sequences.** Section 2 of a bank is `SEQp` data and `FUN_00206128` is the
  negative-cue path that plays it. No music.
- **Scene-streamed banks.** A cue whose record byte +7 is non-zero picks its
  bank through `FUN_00205778`; the report says `alternate bank not ported`.
- **Absolute loudness.** The chain reproduces the game's relative volumes, but
  nothing models the IOP's own master, so the overall level is a guess.

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
(`FUN_0020b600`) is not -- GL clips those primitives instead, and
`--render-report` counts them rather than staying quiet about it.

**Not everything is single-sided.** Flag `0x1` on the record is the two-sided
bit: `FUN_00211230:190` hands it to VU1 as a byte of its own, `packet + 9`, next
to the vertex count. On `s01_e024` exactly 32 of 1630 primitives carry it and
they are exactly 16 coincident perpendicular pairs -- the hanging chains, built
as crossed planes. Culling those makes each plane vanish from one side, so
`drawPrimitive` skips culling for them. `--probe x,y,z[,r]` dumps the records
around a world point with their flags, which is how that was pinned down.

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
`FUN_0020b600`), and two oracles that can be checked without looking at a
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
