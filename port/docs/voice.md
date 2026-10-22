# Voice

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

