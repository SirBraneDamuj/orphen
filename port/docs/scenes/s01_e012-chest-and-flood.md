# s01_e012: the flood, and the chest cutscene

The flood is `s01_e012`'s story chest. The ordinary chest cutscene is generic --
player states `0x0C`..`0x15` -- and its frame numbers below are from
`s01_e024`'s chest slot 17, the cheapest one to reach.

## The flooding of the hold

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
--arm-stream d300:13400 --hold-stick -1.9,1 --press-confirm <frames>
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
dialogue lines, and it ends on `0xE1` at `0x8c20`, which raises the save prompt
(game mode `0x10`). The prompt and its wait mode `0x11` are ported
(`src/ported/scene/save_prompt.*`). The memory-card save screen behind Yes
(`FUN_00234468`, mode 8) is not: on Yes the port backs out to the field the way
No does, and says so on the console. That is a known divergence.

## The lead is walked by the script too, and its request was being wiped

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

This is the same bug the actor loop had (see the `FUN_00239ce0` note in
[Scene script](../scene-script.md)); the
lead's copy of it survived that fix because nothing had yet asked the script to
move slot 0. Removing it leaves `--frames 4000 --actor-report --scr-report`
byte-identical on both `s01_e012` and `s01_e024`.

Reproducing it needs the opening out of the way first: `--arm-stream d300:13400`
lands after the handover, and flag `0xFB` now sets 109 frames later at `0x80a9`.
Armed any earlier it fails for unrelated reasons -- before the opening's `0x78`
at script `0x5f8e` clears `+0x04` bit `0x108`, physics is off on the lead
entirely, and armed at frame 1 he walks into a map blocker no opening has moved.

**Where physics actually lives.** No state handler calls `FUN_002262c0`, and
neither does `FUN_00239ce0`. The only caller is `FUN_002261e0`, a single late
pass that walks all 256 pool slots -- *from slot 0* -- running `FUN_00225c90`
then `FUN_002262c0` on each live one whose `+0x02` bit `0x800` is clear and whose
`+0x192` is negative. `FUN_002239c8` orders it `FUN_0025b778`, `FUN_00251ed8`,
`FUN_00239ce0`, `FUN_00208450`, **`FUN_002261e0`**, `FUN_0025b918`,
`FUN_00216aa0`. The port used to run physics inside the player controller and
the actor loop; it now has `FUN_002261e0` as its own pass in that slot, lead
included (see [Physics](../physics.md)).

## The chest cutscene

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
| `0x12` | on the caption being dismissed with Cross, arm the fade to white | 441 |
| `0x13` | on white: release the camera, push the player clear of the chest, leave mode 6 | 469 |
| `0x14` | once grounded, arm the fade in from white | 475 |
| `0x15` | on visible, back to idle | 497 |

The frame is when the state hands on, with the caption dismissed at frame 440
(re-measured 2026-10-23 with the command at the end of this file).

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
dead code in the retail build (see [Camera](../camera.md)); the only thing that
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
`DAT_00355700` back — the last from snapshot byte `+0x1DA65`. For the camera
half, `FUN_00217d70`'s own save/restore pair covers it.

It restores those two words from `FUN_00233b28`'s copy; it does not just clear
the bits it set. The port used to clear them, which un-hid every entity that was
hidden before the chest opened: in `s01_e012` that is the seven `+0x08 = 0x0011`
boxes in the doorways (slots 18..24), which came back as flat grey slabs.

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
accumulated. The real engine is `ported/text/original_dialogue_window.*` (see
[Text](../text-and-subtitles.md)), and this file should be folded into it. It has
not been yet, because the chest caption is the one place that needs `0x14`'s
item-name splice and a real Cross press, and the cutscene walk does neither.

The *font* is the real one, from the same doc.

## Checking the chest cutscene headlessly

Checkable without a window, and `--press-confirm` now works under
`--screenshot` too:

```
orphen_port --disc-root disc --scene s01_e024 --frames 620 \
    --spawn -4.5,-10.5,0 --press-confirm 60,440 --actor-report
```

That spawns beside chest slot 17, facing it, and the report ends with that
chest on animation 6 and every other one still on 4.

**Both presses matter.** The first opens the chest; the second dismisses the
item caption, which state `0x12` waits on. With only the first, the cutscene
parks at `0x12` forever and the last four state changes never happen — that
looks exactly like a regression and is not one. The ten transitions land on
frames 61, 89, 111, 365, 366, 399, 441, 469, 475 and 497.
