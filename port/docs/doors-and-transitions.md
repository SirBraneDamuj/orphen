# Doors, triggers and scene transitions

## The doors, and the pulse nothing was setting

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
--arm-stream d300:13400 --hold-stick 0,1 --press-confirm 15235
```

which walks the lead out of the re-entry cutscene into slot 23's door and opens
it. (`--press-confirm` with a *list* of frames re-opens the same door each time
it finishes, because `work[14]` is back to `-1` — that is the script's own
behaviour, not a bug.)

## The doorway scene: 0x64, 0x56, 0x8E and 0x13F

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
  work slots. The fields it reads (`+0x198` / `+0x19C`) are filled by
  `FUN_002d2f40`, type `0x28`'s own behaviour, which allocates types `0x27`,
  `0x26` and `0x19` and hangs them off each other by bone role — `0x26` on the
  rig's role-1 bone, `0x19` on the `0x26`'s role-2 bone with the index
  **negated** (`FUN_0020cdc0`'s middle, non-rigid branch) and `0x27` on its
  role-1. It was operands-only until that behaviour was ported; both are
  modelled now. See [Player model, close-up rig and bandana](player-model-and-bandana.md).

With all four, `0xd780` runs its 43 records with **zero unimplemented opcodes**,
ending on the last at frame 15874 with the player released in the doorway and no
object-script slot left spinning.

## Leaving for another scene

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
port\build\msvc-Release\orphen_port.exe --disc-root disc --scene s01_e012 \
    --arm-stream d780:2 --frames 3000
[scene] s01_e012 -> s14_e001 (request 0x20001, frame 2533)
```

Reached the honest way it is the same thing 13 000 frames later: the opening
cutscene has to finish before the doorway is live at all. The per-frame entry
`0x24be` gates the whole floor-panel chain on `work[0x0d] == 0`, and the opening
holds that non-zero for its entire run — which is why `--scr-report` shows the
panel sites at `0x3d43`..`0x402c` with far fewer tests than the ones in `0xb439`.

`s14_e001`'s own script used to halt at once on the far side (`0x3C`, `0x117`,
`0x46`); it now runs with zero unimplemented opcodes -- see
[s14_e001](scenes/s14_e001-crab.md).

**Known divergence: the screen comes back on its own.** In the original the
fade-out block is left at `0x1FE0` across the load and the arriving scene's
script is what reveals. The port still resets `DAT_00571dc0` in
`resetLeadPlayerForLoadedMap`. That made sense while arriving scripts could not
run; now that they can, it is a fidelity bug to remove, not a feature.

## Arriving: what a group-0xE scene needs that no field scene did

`s14_e045` — where `s01_e024`'s floor panel goes — halted on its *first*
statement and again on frame 2. Four opcodes and two missing lines of
`FUN_0025b6d0` later it runs with **zero unimplemented opcodes**, and three of
the six are things only a scene reached this way ever exercises.

**`0x3C` (`FUN_0025daf8`) is the other half of the handoff.** Its whole body is
`FUN_0025c258(&gp0xffffb298)`, and gp 0xffffb298 against the 0x00359F70 base is
**0x00355208 — the map-prop bank**. Ghidra renders that one address as
`iGpffffb298` inside `FUN_00229980` and as `DAT_00355208` inside `FUN_0022a418`,
which is why `opcode_dispatch_tables.md` used to name it an "entity descriptor
counter" on the strength of the gp spelling alone. It is the bank type ids
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

### Where it stopped, and why that was the battle system

**Status (2026-10-23):** a cold `--scene s14_e045 --frames 12000` now gets
past this, dispatching 43 of the 52 records before halting on opcode `0xB6`
(unimplemented) at `0x41e3`. The section below is why it used to stop at
record 4.

`s14_e045` reached record 4 of its 52 and held. Subproc `0x7E1` at `0x379e`
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
already being driven by the battle module, and what got it past record 4 was a
slice of that module rather than another opcode. See [Battle](battle/overview.md).

### The floor panels are a 4-bit code

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

## The scene remembers where you came from: `0x3A` and `--from-scene`

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

## Which door you came in by: `DAT_00325340`

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

## What a scene already did: it was always the flag bank

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

## Terrain triggers print where to stand

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

