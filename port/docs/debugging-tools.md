# Debugging tools

## SCR SUBPROC DISP

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

## `G`: snapshot the frame you are looking at

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

## Diffing the entity pool against hardware, field by field

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

### Still different (re-run 2026-10-23)

The three gaps this section used to list are closed: the ground query writes
`+0x0A` / `+0x6C` / `+0x70` back (`FUN_00227070:128-138`, in
`actor_frame_update.cpp`), a static prop's `+0x0C` is rebuilt every frame, and
placement samples the floor only for a record carrying `0x8000`, as
`FUN_0025e7c0:115-120` does.

The frame-1091 dump is no longer on disk. The re-run used the `sea_low` save
state instead, which is the same stream two records later: channel 0 at `0xCAC0`,
timer 3424, 18 consumed. The port reaches exactly that state at **frame 1284**
(`--snapshot-at 1284`). Every position but one matches, Magnus included. What
still differs, none of it traced yet:

- **`+0x08` bits.** The ~50 static props read `0x10` / `0x18` on hardware and `0`
  in the port; slots 63-70 lack `0x08`; the lead and slots 84/85 lack `0x04`
  (`0x137` / `0x13B` on hardware, `0x133` in the port).
- **Slots 75-79** (`+0x08` bit `0x20`, the push-out gate) carry a ground query's
  `+0x0A` / `+0x6C` / `+0x70` on hardware; the port leaves them at `-1` / `0`.
  They never move, so the only query that could have written them is the
  push-out branch's corner sample, which in the port does not write back.
- **`+0x0C` on static props**: `0x0005` on hardware, `0x3005` in the port. The low
  bits agree. `0x3000` is the "I was drawn" latch `FUN_0020C810` raises. The port
  raises it for everything it submits, because the distance-fade and near-plane
  culls ahead of `FUN_0020C810:219` are not modelled. That is the likely cause,
  though where in the frame the dump was taken could also explain it.
- **`+0x64` on slots 0 and 1** is `-1` in the port and `0` on hardware: those two
  slots are not built through `FUN_00229c40`'s clear.
- **Slot 1** differs in radius (`+0x54`, 0.30 on hardware against 0.15), height
  (`+0x58`, 0.60 against 0.80), `+0x62` and `+0x192`. **Slots 5 and 6** are
  occupied on hardware and empty in the port. **Slot 8** is parked at a different
  off-screen y.

## `eeMemory.bin` is frame ~1091, and Volcan was never a bug

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

A residual recorded here, Magnus (slot 81) at `-1.19x` against the dump's
`-1.200`, is gone: the re-run above matches him exactly.

`--probe x,y,z[,r]` prints `lead=` (record78 `+0x00`) beside `terrain=`
(`+0x04`); the scan gates on the first and the reject mask on the second, and
both have a meaningful bit `0x100`, so telling them apart matters.
`--actor-report` prints `af=` (entity `+0x04`) so a run can be diffed straight
against a dump's entity block.

## POSITION_DISP, the original's own overlay

`src/ported/debug/` is the game's debug text path rather than the harness's:

- `original_debug_text.*` is `FUN_002681c0` (the printf that appends into
  `DAT_00572c38` behind the `DAT_003555dc` / `DAT_003555da` gates) and
  `FUN_00268270` (the once-a-frame pass that walks that buffer placing one
  glyph per printable character and then clears `DAT_003551dc`).
- `original_position_display.*` is the `cGpffffb128` block of `FUN_002239c8`,
  which is what the debug menu's `ON :POSITION DISP` row turns on.

`cGpffffb128` (`DAT_00355098`) is held on, and both debug gates with it,
because nothing reaches `FUN_00268d30`'s menu yet. Whether it reaches the screen
is the `H` toggle; see Debug HUD below.

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
are scaled by 1000 and truncated. `AF` matches now:
`FUN_0022a418_stamp_lead_player_flags` applies `DAT_0058beb4 = ... | 0x3000`.
`SF` does not yet. It is still missing bit `0x04`, which is the
`DAT_0058beb8 |= 4` from the same function; the diff above shows it.

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

