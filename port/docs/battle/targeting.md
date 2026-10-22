# Battle: targeting

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

