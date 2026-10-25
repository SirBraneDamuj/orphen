# Battle mode

## The first slice: s14_e012

`s14_e045` stopped because it was already being driven by the battle module.
This is the first slice of that module: the player half, against **`s14_e012`**
— a small section-14 scene with one ally and several enemies, and no opener to
speak of.

```
port\build\msvc-Release\orphen_port.exe --disc-root disc --scene s14_e012 \
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

### No target is a branch, not a special case

`FUN_00249610:170` is `else if (lVar11 < 3)`, where `lVar11` is control block
`+0x2C`. Below 3 the whole face-the-target block is skipped and control falls
straight to the state dispatch — no special case, the original's own branch.
Pool slots 0..2 are the party, so "below 3" and "no enemy locked on" are the
same statement.

`FUN_0024cf20` forces that halfword positive and, when it is zero, to 1 — so the
report prints `target=1` rather than `-1`, and the `< 3` test still takes the
no-target branch. State 106 reads the same field and collapses to a single frame
for the same reason; see [Sword and guard](sword-and-guard.md).

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

- **The allies' AI scripts.** The battle master tick runs a *second*
  bytecode interpreter — 19 opcodes at `PTR_LAB_0031d118`
  (`src/PTR_LAB_0031d118.txt`) — over a master battle script and a per-member AI
  script off control block `+0x30`. The master script and the enemies' scripts
  run now (`--battle-report`'s "per-actor VM" line). A party member other than
  the lead gets an executable-resident script (`PTR_DAT_0031d1a8`, or
  `PTR_DAT_0031d1f0` for class 3) that the port does not install, so allies
  stand still (`battle_party.cpp`).
- **The per-action camera framing and the reaction shots.**
  `DAT_00355C90`/`C94`/`C98` and `DAT_00354E84`/`E8C` all read zero, so
  `FUN_0023C340` falls through to the spline walk every frame.
- **Classes 3, 4, 6 and 7.** Class 1 (Orphen) and class 5 (Sephy, below) are
  ported.

The effect entities `FUN_00242df0` and the state handlers spawn — types
`0x118` (the status aura, `FUN_002d8ce0`), `0x139`, `0x13D`, `0x174`, `0x1C7`,
and the `0x15B`/`0x15C`/`0x173`/`0x178` the casts throw — are all ported.

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

The same save-state diff turned up two more differences, both fixed since:

- Every enemy on hardware is turned to face the player at spawn — slots 11..14
  carry `2.7909`, `2.1516`, `1.5819`, `2.5425`, and each is `atan2` of the
  offset to `(-0.9, 0.3)` to four decimals. The port left them at the placement
  default `1.5708` until the type `0x8A`/`0x80` behaviours were ported; the turn
  is the enemy's own idle default (see [Enemies](enemies.md)).
- Hardware slot 10 is type `0x072` with `+0x95 = 0x50`, bound to the encounter's
  party record `[4]`. The port binds it now. Its behaviour (`0x2f1200`) is
  still `UNIMPLEMENTED`, as is the type `0x74` that fills slots 15..30.

### Sephy fights as battle class 5

`s14_e003` is Sephy's own battle, the one Magnus's route reaches from
`s03_e001`: three type `0x8B` dummies, with Sephy made the lead by the scene's
own `0xB0`, so `--scene s14_e003` is a faithful way in. On hardware it is the
debug menu's MAP SELECT, `MP14` > `BG03` (Right steps the section, Cross picks).
Its save states are `savestates/s14_e003_*.p2s`.

`FUN_00249610` dispatches through a 24-entry table picked by party record
`+0x00`, and the port had only class 1's. Sephy is class 5
(`0x0031DDC0`), so she sat in state 101 for good; state 120 never ran, so
control `+0x2C` was never turned from -1 into the 1 that starts the target
cycler, which is why nothing could be targeted. Fifteen of her 24 entries are
class 1's addresses. Her nine are in `battle_character_update.cpp` under
"class 5". Her effects hang off `FUN_0020DD78(caster, 4)`, a bone role, where
Orphen's use fixed bones.

Her loadout is row 3, and all three spells are ported:

| Button | Item | Kind | States | Effect entities |
|---|---|---|---|---|
| Triangle | `18` Ball of Wind | 12 | 111 -> 112/109 | `0x14A` hand, `0x165` ball, `0x178` burst |
| Circle | `1C` Dance of Ice | -2 | 113 -> 114 | `0x14B` hand, `0x17B` burst, `0x1A9` bystander, `0x1A8` summon |
| Cross | `14` Blazing Baton | 0 | 107 -> 106 -> 105 | `0x176` baton |

Checked against hardware in the same arena, one cast at a time, with the
damage read off dummy `+0x12A`: the baton's cut takes it 62 to 59, the wind
ball 59 to 48 (ball in slot 39, burst in slot 41 on both), Dance of Ice 48 to
41 at the hardware's own charge of `0x240`. The state sequence and the dash's
frame count match too.

Dance of Ice at full charge on a live target summons instead: `FUN_002E5F48`
puts creature `0x1A8` (`FUN_002E65D0`) on the hand. It sets the same stage as
Orphen's five summons -- freeze, dim set, `DAT_00354ECC` -- with its own copies
of the globals (`DAT_00355568` fade, `DAT_0035556C` veil, `DAT_00355570`
facing) and one four-point eye curve at `DAT_0034FC80`, looking at the
creature's bone 1. Its blast at timeline frame `0x22` is `FUN_002E5998` again
at level 6. On hardware (`savestates/s14_e003_ice_full_charge.p2s`) the blast
lands 170-173 frames after the release and takes 7 off all three dummies; the
port lands it at 171 for the same 7 each, and the creature is gone inside the
same 40-frame window. The swirling smear around it reads stronger in the port
than on hardware; not chased. Its veil quad and banner are not ported, as for
the other summons.

A wind ball that traps its target raises the `FUN_00248D58`
flag the type `0x6A` HUD sprite reads, and that sprite is not ported. Sephy as
the lead never traps; see `FUN_002e45a0_wind_ball`.

## Class-1 state handlers and the harness hold

Of the 24 class-1 state handlers at `0x0031DD60`, all of 101..122 are ported
now; 100 and 123 are the original's own null entries
(`battle_character_update.cpp`). `--battle-report` names any state a run
reached whose handler is missing.

`--hold-square 300-380` works under `--screenshot` as well as `--frames`; it did
not at first, and a capture "during a guard" was quietly of a character standing
still. Both loops now apply the same synthetic hold.

## The battle logo, and `--battle-logo`

The JP release puts up a "Sorcerous Stabber ORPHEN" cut-in at the start of a
battle; the US release does not. Opcode `0xDF` (`FUN_00264FA0` ->
`FUN_0025D5B8`) spawns it as entity type `0x49` in pool slot 5 at scale 12, sets
`+0x08` bit `0x40`, and selects it. Its actor handler, `LAB_00239FB8`, sits where
Ghidra has no function and was read off the ELF: idle until the script sets
animation 1, then fade in from 3 by 8 a frame, hold until the timeline cursor
`+0xA8` reaches `0x12`, fade out from `0x7F`, and release itself.
`FUN_002255B8` drags it to the camera eye plus six units of `DAT_0058BEA0`
every frame and turns it to the camera's yaw, pitch and roll. All of it is in
`ported/entity/original_battle_logo.*`.

The US scripts never reach `0xDF`. Diffing `scr_jp/` against the US disc with
`tools/scrdis` (2026-10-25): JP calls it 120 times in 52 battle scenes, the US
scripts keep 13 of those -- s14_e001..e004 and e024..e030 -- each inside
`if (0 == 1)`, and deleted the other 85, along with the setup lines and the
90-frame wait around them. `--battle-logo` puts all 98 back at script load,
after checking each site byte for byte. It is a port option, off by default.
`python -m tools.scrdis.battle_logo` finds both kinds of site from the US disc
and prints the port's two tables.

- **The 13 wrapped calls**: the `==` of each `if (0 == 1)` becomes `!=`.
- **84 deleted calls** are pairs in the battle intro's `switch (work[N])`, the
  same in all 42 regular encounters. JP's case 10 opens with `0xDF` plus
  `reg(5, 3) |= 0x4000; reg(5, 4) |= 1`, a hidden and suspended preload; case
  40 with `destroy(5); 0xDF; reg(current, 8) = 1; ramp_start(0, 90, 1, 0)`;
  and case 50 holds `player_lock(1)` until `ramp_step(0)`. The port appends
  those lines, copied from the JP script, to the blob, and points the case
  table's entries for 10, 40 and 50 at them; each copy ends in a `goto` to
  where the US case starts, so no US byte moves.
- **The last one** is an s14_e004 event body that JP has as
  `destroy(5); 0xDF; reg(current, 8) = 1; retire(-1)` and US as a bare
  `retire(-1)`. That instruction is five bytes, exactly a `goto`, which leads
  to an appended copy of the JP body.

Not checked against a JP console: there is no JP disc image here. The
appended lines are the JP bytes, run by the same interpreter; the logo
itself is the part checked against hardware, below.

Checked against PCSX2 running `s14_e001_loaded.p2s` with the same byte poked at
`0x1C4C412` (script base `0x1C4A4F8` in that state): at 24 and 69 frames after
the spawn the port and the hardware draw the same frames -- the giant
fly-in slabs, then the settled logo -- and the entity sits at the same
`(-0.30, -4.30, -0.49)`. Two renderer fixes were needed to get there; see
[rendering](../rendering.md#overlay-models-are-not-depth-tested).
