# Scene script

`src/ported/script/` is a narrow, faithful port of the SCR bytecode VM:
`FUN_0025bc68`'s three dispatch tables with the 16-deep call stack, plus
`FUN_0025c258` and `FUN_0025bf70` as methods of the same object, because
`pbGpffffbd60` and `DAT_00355cd0` are one address and there is only ever one
stream pointer.

`src/ported/script/scene_script.*` is `FUN_0025b390`'s header plus all five
entrypoints as named functions. Words 0 and 1 run at load, which is what
`FUN_0022a418` does -- it invokes `FUN_0025b6d0` and `FUN_0025b728` from
different points in the bootstrap, with a lot of state setup between them.

The per-frame entry (`FUN_0025b778`, word 2) runs every frame by default;
`--no-scr-tick` turns it off. It runs header word 2, then
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
is the player's interaction probe (`FUN_00252828`) and word 4 is entity teardown:
`FUN_00265EC0` runs it, through `FUN_0025B9A8`, for an entity whose `+0x02` has
bit `0x8000`, with that entity selected and after its children have gone but
before its own fields are cleared. `s03_e001` needs it. The block on the pillar
at (-7.0, -23.0) gets the bit and `+0x95 = 0x69` once opcode `0xD9` makes it
burnable. Its word-4 body is
`if (current.+0x95 == 0x69) lead.+0x74 &= ~0x20000`. Init set that bit to keep
the lead off the four pillar-top faces tagged `0x20000` (#1877-1880), so without
the hook the burnt block left the path shut.

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

## The cutscene sequencer

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

## Where scene objects actually come from

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

## State of play: `s01_e012`

`s01_e012`, the game's first scene, runs its init, start and per-frame entries
with **zero unimplemented opcodes over 4000 frames** and spawns 77 entities
(re-checked 2026-10-23). Its
scene script is byte-identical to `scr/scr2.out`, so the whole existing scr2 body
of work applies -- `docs/scr2_offset_tables_dialogue_voice_flow.md` most of all.

The opening cutscene chain **runs to its end.** From `--scr-report`:

```
event records dispatched: 208     event flag changes: 60+
  frame     1  -> 0x40b2 (slot)      frame   634  -> 0x2c (dialogue)
  frame   544  -> 0x461f (slot)      frame 13212  -> 0xb63 (dialogue)
  frame 13250  flag 0x515 set  at 0x6813
0x6D player lock mode=-1  (state 10)   ...   mode=1  (release)
```

Forty-two lines of dialogue, a dozen camera shots, and characters walking
between marks -- about **three and a half minutes** of cutscene, with **zero
unimplemented opcodes** and `--frames` byte-identical run to run. The frame
numbers move whenever pacing gets more faithful -- they read 10354 and 10426
before dialogue holds were timed from the voice clip (see [Voice](voice.md)) --
so treat them as a snapshot, not a constant.

`0x515` is the handoff latch: `docs/scr2_offset_tables_dialogue_voice_flow.md`
identifies it as what the opening sets when it gives the player control, and the
matching `0x6D` release is in the report beside it. So the scene reaches its
first interactive moment.

## How a cutscene is actually built

Five mechanisms, and none of them is a linear script:

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
   `0x3bba`, `0x3c32`, `0x3caa`), each differing only in its door id — see
   [Doors](doors-and-transitions.md). The other seven are types `0x281` and `0x2ca`; the five `0x281`
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

   The panels only start being tested once the opening hands over control
   (flag `0x515`, around frame 13250) — which is the shape the scene is authored in: one automatic
   opening chain, then player-driven cutscenes from there.

   **Most of the scene's choreography lives behind them.** The blob has twelve
   `0xEB` focus sites; the opening reaches three. The other nine sit in
   `0x705d`..`0xa812`, and stream `0xca30` never targets anything above
   `0x6526` — but `0xd0c0` targets exactly that range. Six opcodes had to be
   ported before it would run: `0x6E`, `0x72`, `0x73` (angle helpers), and
   `0x94`, `0xDE`, `0x10A`, `0x10B`, first as operands-only. `0x94` (camera
   shake, below) and `0x10A`/`0x10B` (the impact-dust pool, see
   [Effects](effects.md)) have since been modelled. Each halt named the next
   one, which is what the halt discipline is for.

## What getting `s01_e012`'s opening to run fixed

Sixteen bugs, all silent. The event-flag opcodes, the two owners of `+0xA8` and
type `0x38` being a role are in "Two bugs this turned up" above; the rest:

- **A handler must read its own opcode before evaluating any operand.** An
  operand expression can contain another statement opcode -- work reads (`0x36`)
  are everywhere -- and evaluating one overwrites `currentOpcode_`. Four
  handlers read it afterwards, including `0x77`..`0x7C`, so *every* object
  register write whose selector was an expression fell through its switch and
  silently did nothing. That is why the cast never animated. Every original
  saves the opcode in its first instruction; the port now does too.
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
  wait subproc exited on its first frame. (That misreading is also why this
  list first lived under a "voice handshake" heading.) `FUN_002446e8` walks the spline and
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
`0x515` set and the player lock released beside it (frame 13250 as of
2026-10-23; see State of play above).

### `s01_e024`

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

Neither is a save point. The fade branch ends on `0x8E`, which is the scene
transition (to `s14_e045`; see [Doors and transitions](doors-and-transitions.md)),
not an audio opcode as first recorded. The battle branch completes with nothing
unimplemented.

**The confirm button works.** `FUN_00252cc0` / `FUN_00252a18` / `FUN_00252828`
are ported and hooked into `FUN_00256bb8`'s grounded branch on Cross. The branch
`FUN_00252828` takes comes from the descriptor flag at `+0x02`, read out of the
executable: `0x4004` on party members sends them to scene script header word 3,
`0x0100` on the chest takes the native path.

- **Chests** run their whole cutscene. See
  [s01_e012: the chest and the flood](scenes/s01_e012-chest-and-flood.md).
- **Party members** run header word 3. `0x70` (the angle from an object to the
  player -- how a character turns to face you) and `0x33`, inline dialogue, are
  both modelled now, and the party swap is opcode `0xB0`, which is ported.

## Three names in the dispatch tables that were wrong

`analyzed/opcode_dispatch_tables.md` called opcodes **0xDC** and **0xDD**
"audio_dispatch". They reach `FUN_0023baf8`, which is an **empty stub in the
retail build** -- they do nothing at all.

**0xDE** is not audio either: it starts a **controller vibration pattern**.
`FUN_0023bbd8` scales its first operand to a strength (`(10 - x) * fGpffff86dc`,
clamped to `0x3C..0x100`), resolves the second to a pattern in script memory,
and parks it in one of four channels at `DAT_00571b50` (a full set evicts a
random one through `FUN_00216868`). `FUN_0023bd00` walks the channels each frame
and writes the result into `DAT_00571a40 + pad * 6`: on/off words at `+0` and
`+2` and a strength byte at `+4`. That block belongs to `FUN_0023af50`, the pad
driver (`scePadGetState`/`scePadRead` on the `DAT_00571a00` buffers), which is
what makes these actuator settings. `FUN_0023bd00` also limits total buzz time:
past `0x2580` ticks in a row it fades the strength out and holds it off for a
cool-down. A non-zero `cGpffffb656` refuses the request outright. The port has no
rumble, so `0xDE` stays operands-only; the one thing it skips that touches game
state is that rare `FUN_00216868` draw.

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

