# Menus and game modes

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

(Item 3's label, message `0x42`, was never written down here.)

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

**Return to Title Screen** (4), **Item** (5) and **Equip** (6) are ported; see
below and "The Equip screen". Cross on any other available item logs the
selection and leaves the panel up.

### Return to Title Screen

`PTR_FUN_0031C3C0` is seven words in `SLUS_200.11`; slot 4 is `FUN_00232FA8`, a
Yes/No confirm. `FUN_00231958` keeps the handler's return in `iGpffffbcbc` and
calls the handler with it every frame while it is non-zero, and the handler
returns its own state -- so that word *is* the cursor: 1 is Yes, 2 is No, and it
opens on No. Left or Right (`FUN_0023B9F8(0xA000, 1)`) flips it.

While it runs the panel is not drawn (`FUN_00231958:43` skips `FUN_00231C50` when
`iGpffffbcbc` is set and the selection is 4). It draws message `0x1E` ("Cancel
game and return to the Title Screen?"), the `0x29` caption at y `-0x40`, Yes and
No (`0x4C`/`0x4D`) centred as one run under the question, and
`FUN_0025D0E0(0x60000000, 1)` -- a black screen quad at alpha `0x60`. Measured
against hardware, the dim multiplies the scene by 0.62 on both.

**Every exit closes the whole menu**, not just the confirm: Cross on either
answer and Triangle all end in `FUN_002241D8`. Only Cross on Yes goes on to
`FUN_00237A08`, which is five writes and three calls:

```
FUN_0025D1C0(1, 0xC, 0);                 fade out
uGpffffb280 = 10; uGpffffb284 = 0xC;     DAT_003551F0/F4: s12_e010
uGpffffb27c = 2;                         DAT_003551EC: load behind the fade
uGpffffb662 = 0xFF;                      DAT_003555D2: movie request -1
FUN_0023BB00(); FUN_002663D8(0x511); FUN_002241D8();
```

The `0xC` is the scene section, not a game mode -- an earlier note read it as
`DAT_00354D2C = 12` and the game over's hand-off stopped at the fade because of
it. The `-1` movie request is what gets past `FUN_0022A418:58`, which would
otherwise play the opening movie on the way into the title. `FUN_0023BB00` stops
the rumble motors; the port has none. The game over's hand-off is the same
function and now goes through the same code.

Confirmed on PCSX2 from `entry_stable.p2s`: the confirm opens on No, the dim is
0.62x, and Yes lands on the title with the logo up and no movie, with
`DAT_003551EC` spent.

The dim is a one-frame quad, as `FUN_0025D0E0`'s is in the original: the runtime
releases the overlay at the top of every frame and the confirm puts it back
while it is still up.

**Not ported: module 12's mode 0** (still true 2026-10-23). `FUN_00271220(0)` runs on every title load,
and when the previous scene was not section 12 it calls `FUN_002294D0` -- the
new-game wipe: every event flag cleared except `0x50C..0x50F`, party records
reloaded, inventory and item counters zeroed, the default loadout copied back.
The port does none of it, so a returned-to title keeps the run's flags and
items until START. START then runs the same reset through flag `0x500` (see
"START is New Game"), so the game it starts is clean either way.

### On the keyboard

The D-pad is the **arrow keys**, not WASD. See target cycling in
[Battle: targeting](battle/targeting.md) for why: the D-pad does not walk the character on hardware, and sharing the
nibble with WASD made every step forward open the menu. `[` and `]` cycle the
map, which is what Left and Right arrow used to do.

## The pause, game mode 2

Start in the field (Tab on the keyboard, `--hold-start` from the command line)
goes through the same gate as the menu, `FUN_00224FF0`, and raises mode 2
(`src/ported/scene/field_pause.h`):

- **Guards.** Only the first half of the gate's guards apply to Start. Those
  are event flag `0x508`, the cinematic bars, a scene change, anything in pool
  slot 5, sections 0/`0xC`/`0xD`, and the death latch `cGpffffb6d0`, which the
  port now raises in the death branch and clears at scene load. The menu's own
  guards are tested after Start, so **Start pauses mid-action and with a
  dialogue up**, where Up does nothing. Start or Cross takes the pause branch,
  so Up and Cross on one frame open nothing.
- **Music.** Slots 0 and 1, if playing and not already ramping, save their
  fader and ramp to 500 (half) at speed `0x19`. Start again ramps them back
  to the saved value.
- **The paused frame**, `FUN_00224320`: the `0x50` black dim, message `0x25`
  ("Paused") centred at y `0xB`, the bars and the draw tail. There is no
  `FUN_002192C0`, so unlike the field menu the effect pools stop too, and no
  `FUN_00237FC0`, so the dialogue window is not drawn.
- **Resume.** The handler calls `FUN_00224FF0` first. Start with the guards
  passing restores the music, zeroes `DAT_00354E96`, and runs `FUN_002241D8`
  (mode 0 and a cleared action ring) and `FUN_00224218`, so that frame has
  `FUN_002261E0` and the effect pools in it.

### The battle pause, modes 1 then 2

In a section-14 scene Start raises **mode 1** instead (`0x00224268`, not in
`src/`; `src/ported/scene/battle_pause.*`). That frame:

- cuts a speaking spell line (`FUN_00206CE0`)
- sets up a two-line panel at `0x005609C0`
- sets mode 2
- runs `FUN_00224218`'s tail

From then on mode 2 draws `FUN_00225340` in place of the "Paused" caption:

- **The lines** are messages `0x59`/`0x5A`, "Return to Battle" and "Change
  Equipment and Return to Battle". Battle entries `0x39`..`0x3F` use
  `0x5B`/`0x5C`, the Battle Training pair. Each line is centred, at y 0 and
  `-0x16`, with message `0x29`'s "Press ✕ to Select △ to Cancel" at `-0xA0`.
- **The cursor.** Up or Down flips it with cue 1. The selected line's alpha
  ramps to `0x80` and the other's to `0x20` (`FUN_002318C0`).
- **Start, Triangle or Cross** play cue 2, and that frame draws nothing.
  - Line 0, or Triangle, resumes. Start is forced into the pressed word and
    `FUN_00224FF0` runs, so the music comes back the same way. Unlike Start's
    own resume, this one does not run `FUN_00224218`.
  - Line 1 in an ordinary battle is `FUN_0022EAD0`: the Equip screen at
    state 1, row 6, with lead `+0x04` bit `0x100` set. The music stays
    ducked. The Equip screen already leaves a battle by reloading it, so
    Triangle out of it comes back to a fresh `s14_e012`.
  - Line 1 in Battle Training loads `s12_e010` with request `0x2003`.

Start itself still resumes from the panel, because `FUN_00224320` tests it
before `FUN_00225340` runs. Battle entry `0x1F` (`s14_e031`, the spell-earn
scene) cannot be paused at all.

Checked in captures of `s14_e012`: the panel; resume by Triangle and by
Start; Down then Cross into the Equip screen; and Triangle back out into the
reloaded battle. The Battle Training branch is untested.

Not ported:

- **`FUN_00225C20`**, which zeroes `+0x13C` on live slots. The menu modes
  leave it out too.
- **The play clock** (`uGpffffb6c8`), which mode 2 does not advance. The port
  has no play clock.

The dim alpha goes through the same `FUN_0025D0E0` path as the save prompt's
`0x50`, which draws it as `0x50/0xFF`. Whether the GS reads that byte against
`0x80` instead has not been checked on hardware. (The Return to Title dim was
measured at 0.62x on both, which fits `0x60/0xFF`, not `0x60/0x80`.)

## The Equip screen, and how spells are learned

### Spells are items

There is no unlock bitfield. `DAT_003437B8` is the inventory -- one byte per
item id -- and spells are items `1..0xE`, the rows of the spell table at
`DAT_00324FC8`. **Script opcode `0xBC` (`FUN_00263E30`) is "give item"**: one
more of `id`, capped at 99. `analyzed/` called it an event counter, and the port
kept a *second* copy of the table under that name that nothing but the opcode
read, so a learned spell never reached the loadout code. There is one copy now,
`SceneScriptState::DAT_003437b8_itemCounts`.

- **s01_e024 gives you everything, on hardware too.** Its init runs `0xBC` 43
  times -- items 1..0xE and 29 others -- before anything else. It is a test room.
- **s14_e031's reward arms each run `0xBC` with their spell.** That is the whole
  of learning one; the arm then equips it into slot 0 (`0xBD` method `0x78`).
- **A chest adds its item directly**, not through `0xBC`: `FUN_00254F60:26`
  does `DAT_003437B8[chest +0x130]++` the moment the lid's animation event
  fires -- a plain byte add, with no cap at 99. The port had the chest cutscene
  and the item window but not this line, so no chest ever raised a count.
  Items without record flag `0x80` (the lanterns, for one) are counted but
  never appear in the Item screen's ring.
- **An equipped item is not counted.** Swapping one in takes one off its count
  and puts the old one back. A new game (`FUN_002294D0`) zeroes counts
  `0..0x7F` and equips `05 07 01`: Hand of Pyro, Bite of Lightning, Sword.

`--battle-report` prints the held spell counts. The table is game-wide like the
event flags, so both scene-load paths carry it across the state reset; the port
used to wipe it on every load, which would have undone every reward.

### Mode 3

Field-menu rows 5 (Item) and 6 (Equip) are both `iGpffffadbc = 3; return 0`
(`0x00233240` / `0x00233250`, no `src/` file). Mode 3's handler at `0x002244C8`
is `FUN_0022E910`, then **`FUN_002261E0`**, then the draw tail. That second
call is the difference from the menu modes: `FUN_002261E0` is where
`FUN_00225C90`, the animation step, runs -- so on the Equip screen Orphen
breathes and the highlighted icon animates, where the menu freezes both.
(An older note here put the animation step in `FUN_00239CE0`; it is not.)

`FUN_0022E910` handles Triangle, then dispatches `PTR_LAB_0031C2F0[DAT_00354DA0]`:

| state | handler | ported |
|---|---|---|
| 0 | `0x0022F010`, -> 1 | yes |
| 1 | `FUN_0022F020`, build the screen once the lead is grounded | yes |
| 2 | `FUN_0022F2D8`, the fade (`FUN_002340E0`, twice a frame) | yes |
| 3 | `0x0022F3E8`, highlight slot 0 | yes |
| 4 | `FUN_0022F408`, Up/Down picks a slot, Cross opens the ring | yes |
| 5 | `FUN_0022F588`, the ring closing | yes |
| 6 | `FUN_0022F620`, the ring open: turn, move slot, Cross swaps | yes |
| 7 | `FUN_0022FA18`, the two icons trade places | yes |
| 8 | `FUN_0022FBD0`, "No items available" until Cross | yes |
| 9 | `0x0022FCA8`, the Item screen's ring (slot 3) | yes |
| 10 | `FUN_0022FD38`, browse items; Cross picks and leaves | yes; the use is a log line |
| 11 | `FUN_0022FDE8`, "No items available"; only Triangle leaves | yes |
| 12 | `FUN_0022FEA8`, leave by reloading (battle scenes) | yes |
| 13 | `FUN_0022FF20`, leave by restoring the field | yes |

### The ring, and what "equip" writes

The ring model (type `0x4C`, slot 6) has forty rim bones. `FUN_00230450` turns
them into an eighty-point loop -- each bone, then the midpoint to the next --
every time the ring opens, from last frame's pose. `FUN_00230910` spawns one
icon per item that is held *and* whose record's first halfword carries both the
character's roster bit and `0x100`; all start at point 0 and slide to point
`i * 80 / count` along a chord-length spline (`FUN_00230608` into
`FUN_00266A78` with a positive fourth argument -- the camera paths only ever
used the uniform knots). Left/Right re-aim every icon one position round;
Cross picks whichever icon is at position 0.

The swap (`FUN_0022F620`) is the whole of equipping: one off the new item's
count, one onto the old one's, and the loadout byte at
`DAT_003437A0[roster * 3 + slot]` overwritten. Then the two icons fly along
random bowed curves for 1920 ticks, the slot gets a fresh icon, and the ring is
rebuilt with the spell just removed at the front.

Checked against hardware from `savestates/field_menu_on_equip.p2s`: icon
spacing on open, the turn, the swap's flight and landing, the loadout bytes and
every count after the swap, and the "no items" box (counts zeroed in RAM).

The ring's turn direction is read from the held word *after* `FUN_0023B9F8`
has ORed the stick's direction bits into it. The port first read the word from
before that call, so the stick (WASD) turned the ring the same way both sides.

Things the original does that look like bugs and are not:

- **The ring stays faintly visible after it closes.** `FUN_002333E8` fades it
  to about `0x80` and stops, leaving `+0x134` at 3 or 4; only opening it again
  fades it back to opaque (`+0x134 = 0`).
- **Stick-up on the slot list steps down** (read from the code, not yet seen
  on hardware). `FUN_0022F408` takes the direction from the *pressed* word,
  which the stick never reaches, so only the D-pad goes up.
- **A swapped-in slot icon is lit, not flat.** `FUN_0022FA18` sets only `+0x08`
  bit `0x40`, not `FUN_002302F0`'s `0x4040`, and leaves its physics on.

### The Item screen is the same ring

Field-menu row 5 sets the same game mode 3; `uGpffffae34` is all that tells the
two screens apart. No slot icons are spawned, and state 9 builds the ring with
`FUN_00230910(3, -1)`: slot 3 asks each item record for bit `0x80` instead of
`0x100`, so the ring holds consumables rather than spells. State 10 prints the
front item as `"%s*%d"` (name and count; `*` is the font's multiply sign) and
Cross leaves straight to state 13 with the item's type in ring `+0x1C8`.

**Using the item happens on the way out**, in `FUN_0022FF20:28-50`: the item
is spawned over the lead's head, the lead plays animation `0x45` in state 10,
cue `FUN_00237AA8` plays and the count goes down one. None of that is ported:
the port logs `[item] use item ...` and leaves the count alone. Checked against
hardware: the ring's contents and order, and Left then Cross using the Herb
(`0x55`, count 1 -> 0 in RAM) -- the item the port logs.

### The pentagon is the battle readout's

`FUN_00230E50` draws the element pentagon with `FUN_0022EC30(0x220, 0xD0,
0x570DB0, 0)` -- the same function the battle target readout (`FUN_00233818`)
calls with `param_4 = 1`, so the port calls the same
`battle::FUN_0022ec30_pentagon`. The `0` picks the other banding: pips lit =
element value / 10, capped at 3, where the enemy readout bands at `0x22`/`0x4B`.
`DAT_00570DB0` is the item's `FUN_00229820` record, refilled on a slot pick, on
the ring closing and at the end of each ring slide -- not on the swap -- and
never cleared, so it keeps the last spell shown between visits. Checked against
hardware for Armor of Purity and Hand of Pyro; the pips match.

The pips are sort key `0x1006` (`FUN_0022EB00` into `FUN_00207DE8`): the
smear's bucket, under the letterbox bars and the fade (`0x1007`). The port used
to draw every HUD quad after the fade; they now go straight after the smear,
for the battle readout too.

### The field is copied, not hidden

`FUN_00233B28` snapshots the whole pool, its status bytes, the lights, the
camera, the scene colours and the fog; then every live slot from 10 up is
*released*. `FUN_0022FF20` copies slots 2..255 back wholesale. Slots 2, 3 and 6
are released and the ring built in slot 6 *before* the copy, so the ring comes
back with the rest and is released again. The room goes the game over's way:
`DAT_00355700` steps down to 3, where `FUN_00209140` stops drawing the map, over
`FUN_00255CE8`'s black quad. The restore reads `DAT_00355700` from a snapshot
byte nothing writes; hardware has 0 there after the screen closes.

The field HP orb disappears for a couple of seconds after the screen closes.
That is the gauge's own count-up: it hides itself on the frame the menu opens,
the snapshot keeps it hidden, and it takes `+0x98` to `0xF00` to come back.

The screen smear is left at 7 after the fade, not 0 -- the ramp stops at 96
before its `< 6` branch can zero it. Hardware reads 7 there too, so a slot
change leaves a one-frame 5% ghost of the old description.

### Two renderer fixes it needed

- **Entity `+0x08` bit `0x40` puts a model in sort bucket `0x1005`**
  (`FUN_0020EEC0:181`), past the depth table and among the 2D overlays. The slot
  icons carry it, which is why they sit *over* the bars (`0x1004`) and under
  the button glyphs. The port had the constant as `kBlendedBucket` with nothing
  assigning it. Sprites now carry their bucket too, and `MapViewer::render`
  draws buckets up to `0x1005` before the smear and fade.
- **An untextured PSC3 pass is not always opaque.** With primitive flag `0x200`,
  `FUN_002129B8` reads the colour entry after the pass's own as an alpha byte
  and a register block (`FUN_00212058:153-167`). The ring is all such passes:
  a GS dump shows ABE on, `ALPHA 0x48`, vertex alpha 63.

The dialogue sprite pass also alpha-tests now (`TEST_1` is `ATST GREATER`,
`AREF 0` everywhere), which is what removes the dark box behind each button
glyph.

**Open:** the ring still draws at twice hardware's brightness. The GS dump has
only 55 of its 120 primitives, every one wound the same way -- VU1 back-face
culls them -- while the port draws models double-sided, so both faces add.
Culling is a whole-renderer question (party models ship reversed primitives
that must stay visible), not an Equip-screen one.

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
| `0x00343A08` +0x0C | opcode 0x99's offset from the lead; `DAT_00343A10` = -1000.0, the literal `FUN_002000C0:216` reads as "skip `FUN_002025E0`" |
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

