# Battle: enemies

#### An enemy turns to face you without any AI at all

The enemies in `s14_e012` all stood facing the same way -- 1.571 radians, which
is the placement byte's own `0 * 45 + 90`. The placement angle was never the
bug; `FUN_0025E7C0` reads it and the port applies it. What was missing is that
an enemy immediately turns off it.

Every enemy type is one wrapper plus a state table, and the two this scene
fields are `FUN_0027F288` (type 0x80, `PTR_FUN_00325970`, nine states) and
`FUN_0028A958` (type 0x8A, `PTR_FUN_00325B40`, twenty). The wrapper is the same
four steps in both: clear a couple of `+0x08` bits, run the *action check*, run
the freeze gate and the `+0xBE` hit reaction, then dispatch `+0x60`.

The action check -- `FUN_0027F4B0` / `FUN_0028AB28` -- is where the facing comes
from:

```c
if (record[2] != 0) { record[0x2c] |= 1; dispatch(record[2]); record[2] = 0; }
else if ((record[0x2c] & 1) == 0) { FUN_0027f5c0(entity); }
```

`record` here is entity `+0x198`, and **that is not the actor record: it is the
record plus 0x0C**, because that is what `FUN_0023F8B8` returns. So `+2` is the
record's `+0x0E` pending action, `+3` its `+0x0F`, `+0x20` its `+0x2C` target
and `+0x2C` its `+0x38` flag word.

With no pending action and the busy bit clear, `FUN_0027F5C0` runs. It sets
`+0x19C` to the bearing to the target, rolls a 100..199-tick hold into `+0x62`,
drops into state 1 and stamps the record's current action 6. State 1
(`FUN_0027FA88`) then walks `+0x5C` toward `+0x19C` at ten degrees a frame
through `FUN_0023A320`.

The target is the thing to notice. `FUN_0023A958` reads the record's `+0x2C`
and, **when it is negative, returns `&DAT_0058BEB0` -- pool slot 0**. Type
0x8A's variant, `FUN_0023A480`, does not even ask: it reads `DAT_0058BED0` and
`DAT_0058BED4` outright. So "no target" is not "no angle", it is "the player",
and an enemy standing in an arena with no script running still turns to face
Orphen. All five in `s14_e012` now land on the bearing to the lead to three
decimal places.

State 0 (`FUN_0027F978` / `FUN_0028AE10`) is the other half and is identical
between the two types: scale to 1.0, `FUN_0025BAE8(**0**, type)` -- group 0 of
SCR.BIN 0xBF at `type - 0x7C`, the enemy table -- inlined through
`FUN_0023A518`, `+0x19C` seeded with the placement facing, `+0x96` bit 0, an
animation, then `FUN_0023F8B8`. Its three `FUN_00216078` calls are deliberately
absent from the port: they fill globals at `0x005739B0` / `0x0058B140` that
nothing reads back, and their one lasting effect -- `DAT_00354C64` -- the bind
on the next line re-establishes for itself.

##### Type 0x8A re-aims every frame, and that is the original

`FUN_0027FA88` **sets** the busy bit and only clears it when the turn has
finished and the hold has run out, so a type 0x80 aims once and then holds.
`FUN_0028AF28` **clears** it unconditionally (`and v0,v0,0xfffffffe` at
`0x0028AF48`, checked against the disassembly because it reads like a typo), so
a type 0x8A runs the idle default again on the very next frame -- re-aiming, and
re-rolling its idle animation through `FUN_00225BF0`, which resets the timeline
cursor. Its pose therefore sits on frame 0 and its animation index flickers
between 0, 2 and 3.

That is not a port bug and it is what hardware does too, but it is only visible
while the record is idle -- and once the AI is running it rarely is.

#### The enemy AI is a second bytecode VM, and it only needs five opcodes

`FUN_0023FD30` runs a VM that has nothing to do with the scene script's 271
opcodes. It has nineteen, at `PTR_LAB_0031D118`, and three loops that step them:
the master battle script on the pseudo-record at `DAT_0031DBA8`, every *actor*
record's own `+0x30` script, and every party control block's. The handler
signature is `handler(block, yield) -> u16`, a negative return means "not
finished, run the next opcode now", and the program counter is a single global
at `DAT_00354EAC` that each handler moves itself -- the loops only save and
restore it around the call.

Seven of the nineteen have no `src/FUN_*.c` at all; they are bare `LAB_` blocks
Ghidra never turned into functions, and they had to come out of `SLUS_200.11`
with a disassembler. They are also the cheapest ones: opcode 0 is `pc += 4`,
opcode 1 is a backward jump, opcode 8 is `pc += 6`, opcode 10 is
"return yield + 1", opcode 14 parks forever, opcode 9 installs a trigger table
and opcode 11 waits on a script variable. Recovering them is what made
`-0x50C4($gp)` -- `0x00354EAC`, the PC -- and `-0x4F10($gp)` -- `DAT_00355060`,
the script work memory -- fall out.

The surprise is how little of it an enemy uses. `s14_e012`'s five AI scripts are
five overlapping tails of one twenty-instruction body and between them they
touch **five** opcodes:

```
1a94  16  delay   sub 0: a random 0..0x3B ticks
1a98  13  order   sub 0x80: request action 6 -- go idle, face the target
1a9c  17  gate    sub 1: hold while ANY record is mid-attack
1aa0  13  order   sub 0x80: request action 2 -- close and strike
1aa4  17  gate    sub 0: hold until this record's +0x0F is back to 6
1aa8  16  delay   sub 0: another random beat
1aac  16  delay   sub 2: 0x1E ticks per enemy still in the fight
...   the same again with action 4, then opcode 3 jumps back to 1a94
```

That is the whole of it. Wait a random beat, take a turn only when nobody else
is mid-attack, strike, wait for your own strike to finish, wait a beat scaled by
how crowded the fight is, repeat. **Opcode 17 sub-op 1 is why enemies take turns
instead of swarming** -- it scans the entire actor table and holds while any
record's current *or* pending action is in 2..5.

##### FUN_00244248 is the handshake, and it is biased by 0x0C twice

An order does not simply overwrite the pending byte. `FUN_00244248` picks its
block by the entity's `+0x95`: below 11 it is a party slot and the block is
`DAT_0031D780 + slot * 0x3C`, from 11 up it is an actor id and the block is
`DAT_00354EB4 + FUN_00247d80(id) * 0x3C + 0x0C`. Both are the record **plus
0x0C** -- the same bias `FUN_0023F8B8` hands an enemy in its `+0x198` -- which
is why the decompilation spells the two action bytes `[2]` and `[3]`. Read them
unbiased and the whole handshake looks like it is writing into the wrong fields.

The rule itself is four lines: while `+0x38` bit 0 is set and the pending byte
is neither 0x0A nor 6, an order is refused with -1 unless the *current* action
is already 6. Opcode 13's sub-op 0x80 parks the script on itself when it gets
that -1 and tries again next frame -- unless the actor has died, which is the
one thing that stops it spinning forever.

##### An idle enemy aims at slot 0; an enemy under AI is *assigned* a target

The second loop does one more thing per record, and it runs whether or not the
record has a script: it validates `+0x2C` and, when it has gone negative, walks
the three-byte preference ring at `+0x1D`. Each rotation moves `+0x1E` into
`+0x1D`, `+0x1F` into `+0x1E`, and the *negated* byte that fell off the front
into `+0x1F`, so a member that has just been tried goes to the back marked used.
Three tries, then a random member. The bytes it reads are `psVar3[0x95]` off a
**short** pointer, which is `+0x12A` -- hit points -- not `+0x95`.

With one party member the ring never matches and the random fallback picks
member 0 every time, so the port's enemies end up aimed at pool slot 0 anyway.
The difference is that they are now aimed there *by the retargeter* rather than
by `FUN_0023A958`'s no-target fallback, and a second party member would split
them.

##### What the actions actually do

`FUN_0027F5C8` and `FUN_0028AC40` are the two action tables, and they are not
symmetrical, because the two enemies are not the same shape. Type 0x80 flies:
action 2 builds a quadratic Bezier -- start where it stands, end two units
*past* the target along its own facing, apex 0.3 above the target -- and
`FUN_0023A990` walks it over exactly the travel time `FUN_0023A6D0` costed when
the order landed. Action 4 is the same machinery aimed two units *short* of the
target with a flat 1.5 apex, so it slams down in front rather than through. Both
end in state 6, which teleports the enemy back to its record's spawn position
(`+0x14`/`+0x16`/`+0x18`, world times ten) two units up and lets it sink.

Type 0x8A is a Maneater and is rooted to the floor. It never moves: action 2
turns, bites where it stands and spawns the hit volume on timeline cursor 10;
action 4 winds up for 100 ticks and spits. Action 4 publishes current action
**2**, not 4 -- so the gate that waits for a strike to finish cannot tell the
two attacks apart, which is what lets one script drive both.

Each type releases the busy bit in its own place, and that is what closes the
loop: 0x80 when state 6 has landed it back home, 0x8A when the attack animation
comes round with nothing left in flight. Only then does the idle default stamp
the current action back to 6 and let the script's gate through.

**The damage an enemy deals is not ported.** `FUN_002EBDE0`, `FUN_002EBAD8`,
`FUN_002EC920`, `FUN_002ECC68` and `FUN_00280698` are the five calls that spawn
the hit volume. Every attack plays through to its damage frame and counts itself
-- `--actor-report` prints "enemy attacks that reached their damage frame" --
and lands on nobody.

##### Damage the other way is what the reel and the death states are for

An enemy hit by a spell takes the damage as `+0xBE`, and each wrapper drains it
against `+0x12A` before it dispatches: survived goes to a stagger state, killed
to a death state. The two types number them differently -- 0x80 uses 8 and 7,
0x8A uses 6 and 5 -- and 0x80 makes far more of it, because it has a spawn point
to be knocked back to. `FUN_00280628` reels, `FUN_00280728` publishes current
action **8** and lays a Bezier home, `FUN_00280288` flies it and releases the
busy bit as it lands, and only then does the idle default stamp the action back
to 6. The Maneater's `FUN_0028B698` just holds the bit for the length of the
clip and drops back to state 1.

The two deaths both end at `+0x04` bit 0, the fade-and-free bit, on a `0x801`
write whose 0x800 half nothing in the executable reads back. `FUN_0028B568`
notably does *not* release the busy bit for a plain Maneater -- only one grown
by `FUN_0028B740`'s spit takes that branch -- but the record unbinds when the
entity is freed, so the fight moves on regardless: a 5-enemy `s14_e012` run
whittled down to `live enemies: 1 of 1 bound` with the survivor still spitting.

Without the stagger states an enemy hit mid-attack sat in an unimplemented state
forever, holding both the busy bit and its own script's gate.

#### The health bar is an entity, and there are exactly two of them

Hitting something raises a five-segment gauge, and it is not a HUD widget drawn
by the UI pass -- it is a **type 0x68 entity in pool slot 3**, with a twin in
slot 2. `FUN_0022A418:378-383` builds both on every scene load, right after the
particle reset, in place at `0x0058C260` and `0x0058C438`. Those two addresses
are `DAT_0058BEB0 + 2 * 0x1D8` and `+ 3 * 0x1D8`, so they are pool slots, not
loose structs -- which is also the cleanest confirmation of the 0x1D8 entity
stride.

`FUN_00216140` raises one for any victim whose descriptor `+0x02` carries `0x4B`
and whose `+0x96` does not carry `0x20`, and the bank comes from the same word:

| `+0x02 & 0x48` | slot | screen row | animation base |
|---|---|---|---|
| clear | 2 | slides **up** from 456 to 416 | 0 |
| set | 3 | slides **down** from -8 to 32 | 0x14 |

Every enemy descriptor in the game is `0x0008`, so an enemy always takes the
upper bar. The party types 3..7 are `0x4004`, which matches neither the `0x4B`
gate nor the `0x48` bank test -- a party member's hit raises nothing, because
the player's readout is `FUN_00230E50`'s panel instead. The lower bar belongs to
the `0x01`/`0x02` descriptor band and to the two class 1 states, `FUN_0024BD30`
and `FUN_0024CBA0`, that arm it by hand; neither is ported, so nothing raises
the lower bar yet.

**The five pips are a band, taken twice.** `FUN_002D5630` is handed the victim's
hit points *before* the wrapper drains them, its maximum, and the damage still
sitting in `+0xBE`, and rounds both the before and the after up into fifths:

```c
+0x1A4 = (clamp(hp, 0, max) * 5 + max - 1) / max                 // where it starts
+0x1A8 = (clamp(hp - min(dmg, max), 0, max) * 5 + max - 1) / max // where it stops
```

`+0x198` then walks the whole animation: fifteen frames of sliding on, one frame
that drops the first pip, one more pip per animation loop until `+0x1A4` reaches
`+0x1A8`, forty-eight frames of holding, fifteen of sliding back off, and
`+0x08` bit 0 to stop drawing. The pip count is spelled as the *animation*, not
as geometry -- three banks of five, `base - segments + 21` while arming,
`+ 6` draining and `+ 11` settled -- and all three are written straight to
`+0xA0` rather than through `FUN_00225BC8`, so a bar mid-drain does not restart
its clip.

##### It also found three things wrong in the sprite pass

The bar is the first entity either scene fields that is screen-space, asks to be
drawn in front, and lives on a 4-bit sheet, and it exposed all three.

`FUN_0020F510:0x0020F55C` reads `+0x08` **once** and latches bit `0x1000` and
bit `0x40` into its workspace in the same three instructions, before it branches
on the first of them. The port tested `0x40` only on the world-space side, so
the bar -- which carries both -- never reached the "draw over everything"
bucket.

And its `+0x28` is `DAT_00354704`, `65534.0`. That is not an arbitrary large
number: `19706.0859 / 0.3 - 152.9538` is `65534` exactly, so it is the depth
word the sprite key produces at `DAT_0035209C`, the front of the sprite range.
The original hands the GS that z and never clips; the port re-projects, and its
geometry near plane is `0.4` -- so the whole gap between the sprite floor and
the geometry near plane is thrown away by GL. The bar sat in it with five
well-formed quads in the display list and no pixels. A screen-space quad is now
parked just inside the near plane instead, which costs nothing: `perX` and
`perY` divide by that depth and the projection multiplies it straight back, so
any positive value reproduces the same corners. Note *just* inside -- a vertex
exactly on the near plane rounds to the wrong side of it in float and is
dropped.

The third is the colour. **A sprite record's `+0x09` is a CLUT bank**, and the
sprite pass was ignoring it. `FUN_0020F510:0x0020FB98-0x0020FBC0` tests the
texture slot against `0x18` and, at or above it, writes `(record[+0x09] >> 4) + 1`
into the packet's bank field -- the same `bank + 1` encoding `FUN_0022EB00` uses
for the pentagon, zero meaning "8-bit page". Slot 0x2B holds the gauge sprite in
sixteen palettes, and the bank is the whole difference between the player's bar
and the enemy's; reading the sheet 8-bit gave every enemy a blue bar. Two
records in one strip can name different banks, which is how one sheet serves
both.

That was not the bar's problem alone. The port had the bank on the HUD path
since the pentagon but never on the sprite path, so *every* sprite on a slot at
or above 0x18 was drawn from the wrong palette. The visible second case is Hand
of Pyro's impact: its four-pointed star reads as a blue-white flare rather than
the washed-out smear the 8-bit page gave. Neither field scene has such a sprite
on screen at frame 900, so both guard captures are unchanged.

#### Four of the six enemy attacks were firing an empty gun

The killer bee has two attacks and the Maneater has two, and only one of the
four did anything: the bee's dive, because its damage is its own body sweeping
through `FUN_00215AC8`, and that was already ported. Every other attack ends in
a call that leaves the enemy — a projectile, a seed, a cloud of spores, a direct
charge to the victim — and none of those five calls existed in the port. The
animations played, the cues fired, and nothing came out.

| where | call | what it should make |
|---|---|---|
| 0x80 state 2, the dive | `FUN_00280698` every frame | the body box, through `FUN_00215AC8` |
| 0x80 state 2, hit frame | `FUN_002EBDE0(e, 6)` | six type `0x10F` dust puffs in an arc |
| 0x80 state 3, hit frame | `FUN_002EBAD8` | **the shot**, a type `0x10E` off bone 11 |
| 0x8A state 2, cursor 10 | `FUN_002EC920` | **the seed**, a type `0x112` lobbed at the target |
| 0x8A state 4, throw frame | `FUN_002ECC68` | eight type `0x113` spore orbs |
| 0x8A state 4, clip end | `FUN_00216128` | the spit's damage, charged directly |

The state 3 one is worth naming properly: **the flyer's second attack is not a
second ram.** Its arc deliberately stops two units *short* of the target, and
the last frame of animation 7 launches a projectile from bone 11 aimed down at
the target's midriff — speed 60 units per 32000 ticks, and a drop rate computed
so it arrives rather than a gravity constant. Read as a ram it looks like a
lunge that misses on purpose; it is a lunge that shoots.

##### Type 0x112 has no handler, and that is deliberate

The seed's slot in `PTR_LAB_0031CAB0` is `FUN_00239E78`, the no-op. It does not
tick itself — the Maneater that spat it calls `FUN_002EC750` on it from its own
wrapper every frame and reads the return: `1` means it has landed and the clip
has come round, which is the cue to grow a clone, and `-1` means it has finished
sinking and freed itself. That is also why the Maneater has to tear the link
down by hand when the battle ends, and why `FUN_0028B568`'s tidy-up has two
shapes.

`FUN_0028B740` is the growing. It allocates a second entity of the parent's own
type at the seed's position, gives it one hit point, links the two together and
drops it into **state 3** — a state neither the AI script nor the action table
can ask for. `FUN_0028B0E8` is that state and it is the clone's whole life: walk
in turning toward the target, lunge, and on the lunge's last frame decide. Within
one unit, within 0.3 in height, the target on the ground and not already held by
something, and it grabs — raising bit 2 of the *victim's* `+0x96`, which is the
only place in either enemy that writes another entity's flags. Then 0x0C80 ticks
of chewing, one direct charge of attack record 0, and it dies. Miss, and it dies
immediately.

##### `+0x1AC` was the wrong way round

`FUN_0028AE10`, type 0x8A's state 0, ends with `+0x1AC = 1`. The port had read
`FUN_0028B568`'s branch as "a placed Maneater has 0 there", which made a corpse
take the clone's tidy-up path instead of its own. It matters now that clones
exist: 1 means "I own the seed at `+0x1A8`, and my record is not released until
both it and the clone are gone"; 2, which `FUN_0028B740` stamps, means "I am a
clone, and all I have to do is clear my parent's `+0x1A4`".

##### The player froze in state 102 the moment any of it landed

With the attacks firing, `FUN_00216140` started charging the player's `+0xBE`
and `FUN_0024A360` did what it always did — action 0x83, state 102 — and state
102's handler was a null entry in the class-1 table. Nothing else drains a party
member's `+0xBE`, and `FUN_00216140` refuses a victim that still has damage
pending, so the first hit put Orphen in a stagger he never came out of and made
him immune to everything after it. Exactly the shape of the killer-bee freeze,
on the other side of the fight.

`FUN_0024A540` is that state, and its restart frame is where a party member's
damage actually happens: drain `+0xBE` out of `+0x12A`; pick the flinch (0x1C)
or the knock-down (0x20) from the reaction byte and whether the character is on
the ground; raise `+0x38` bit 0x800 on the control block, and 0x400 as well when
the hit crossed a fifth of the maximum; at zero hit points lie down and teleport
to the position the control block last recorded; otherwise roll for a status.
The frames after it are the getting up — 0x20 down, 0x22 the push-up after
fifteen frames on the floor, 0x23 the stand, then state 108 with action 0x87 to
walk back to the recorded spot.

##### The two enemies inflict statuses, and something has to take them off again

The attack records, read out of SCR.BIN 0xBE at run time, are not all plain
physical hits:

```
type 0x80  record 0 flags=0001 +100%    the dive
           record 2 flags=1000 +10%     the shot
type 0x8A  record 0 flags=0001 +70%     the clone's bite
           record 2 flags=0200 +10%     the spit
```

`FUN_0024A190` reads that halfword back off `+0xC2` and turns the seven status
bits in `0x1632` into a status id — `0x0200` is 9, the poison, and `0x1000` is
12, the confusion the command input already reads to scramble which spell slot a
press picks. A member already carrying any status is immune to a second, and the
reaction byte is the odds in tenths that it is shrugged off.

`FUN_002D8B38` raises it: every party member owns a type `0x118` aura built with
its other effects and parked hidden, and this un-hides it over the character's
head, tells it which icon to draw, and sets the bit in `DAT_0031DA6C`.
**`FUN_002D8CE0` is the only thing that clears that bit again**, so porting the
arming without the aura's behaviour would have left the skull sitting over
Orphen for the rest of the fight. Its six status bodies are one body with a
different light colour: track the head, run `+0x62` down at two ticks a frame,
then shrink a tenth a frame until the icon is under four tenths of its size and
hide. Statuses 4 and 9 add a hit point per animation loop off the victim and arm
the **lower** health bar with it — the first live caller the port has for the
player's own gauge — and they stop at five points, so a status never kills.

##### Verified

`s14_e012`, 8000 frames, no input. All four effect types spawn (`0x10E` once
per lunge, `0x10F` in rings of six, `0x112` per bite, `0x113` in volleys of
eight), type 0x8A reaches states 3 and 5 for the first time, and the report has
no unimplemented state handlers on either side. Orphen goes from 50 hit points
to 7, staggers five times and recovers from each into state 108 rather than
sitting in 102 — before this he entered 102 at frame 760 and was still in it
7241 frames later. The status cycle arms once and clears once. `s01_e024` and
`s01_e012` are byte-identical over 3000 frames with `--actor-report` and
`--scr-report`, and both frame-900 captures are unchanged.

