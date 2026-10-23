# Battle: spells

#### The ground ring: what a GS dump and a save state have ruled out

The ring (type `0x18F`, `grp_00a8`) draws with a hard-edged translucent box
around it that the real game does not have, and it lacks a shimmer the real one
has. A PCSX2 capture at max charge plus the matching `eeMemory.bin` settle most
of the candidates, so they are written down rather than re-tried:

- **The blend is right.** `ALPHA` is `0x48` — `(Cs - 0) x As + Cd`, i.e.
  `GL_SRC_ALPHA, GL_ONE` — on every ring draw, and the port picks mode 2 for all
  158 of the model's subdraws. `TEX0` is MODULATE/RGBA, `TEX1` bilinear, `TEST`
  alpha-discard-at-0. All as the port already has them.
- **The scale is exact.** The save state's live marker reads `+0x14C` 2.680 and
  `+0x150` 1.940; `FUN_002d9b78` as ported computes exactly those from a maxed
  `+0x3C`. The bone palette at `0x00357E00` confirms the transform too: the
  model's rest scale is 1.30, and every ring's matrix is
  `1.30 x +0x14C` horizontally and `1.30 x +0x150` vertically.
- **The pose is genuinely static.** `grp_00a8` animation 0 is a *single*
  timeline entry — column 4, 4 frames, looping — so the column never moves. All
  four rings in the save state sit at `+0xA0 = 0`, so the original is equally
  static. **The shimmer is not bone animation.**
- **The tall geometry is in the original too.** Bone 12 carries the bulk of the
  model at +/-0.4 in height, and the real capture's ring vertices span 130 GS
  units vertically, not the ~40 the ground disc alone would. The port is not
  drawing extra geometry.
- **The zero-scale bones collapse correctly.** Bones 2..9 and 14..17 have scale
  0 at column 4 in both, and the port's posed vertices for them are degenerate.

One thing the max-charge capture appeared to leave: **vertex colour**. Its ring
uses 8 distinct colours over 1,684 vertices, at two alphas only — 128 and 63 —
and 42% of them are `(0, 0, 0, 128)`: pure black, which under additive blending
adds nothing. *That is how the wall is hidden.* Read against that, the port's
alpha-**12** vertices looked like an invention. They are not — see the resting
capture below, which emits 192 of them — so this line of inquiry closes.
Ruled out along the way: colour-table overrun (13 entries, max index 12, none over), the
per-pass texture selector (`--entity-bound-texture` changes nothing), an
unresolved texture slot, and the `-1` pass skip (`FUN_00212058:95-100` skips and
continues exactly as the port does).

The other always-present battle effect, type `0x118` (`FUN_002d8ce0`), is **not**
the shimmer: `FUN_002d8b38` activates it as a *status-effect* aura, one of six
variants selected by its `+0x19A`, and raises the matching bit in
`DAT_0031DA6C`. It does spin continuously though —
`+0x5C += frameTicks * 0.00226893` — so it is an animated ring, just a
conditional one.

#### The ring's shimmer is the model's own UV animation script

Two `.gs` captures of a *resting* battle ring, four frames each, plus the pyro
one, settle what the shimmer is. It is not the pose and it is not the geometry:
what changes is **U**, on every corner of every pass, by whole texels, with
**V** fixed. Measured against `grp_00a8`'s authored subdraw table, per game
frame:

| pass | subdraw `texFlags` | bank | U per game frame |
|---|---|---|---|
| the alpha-12 pass | `0x850C` | 0 | none |
| wall layer | `0x8C7F`, `0x8C3F` | 1 | `+4` |
| — | `0x943F` | 2 | `+8` |
| ground layer | `0x9C7F` | 3 | `+1` |
| — | `0xA4BF` | 4 | `+11` |

The mechanism is the one the map already uses for the rain outside the windows,
which is why none of the searching for a bespoke ring animator found anything:

- **PSC3 header `+0x40` is a UV animation script**, byte for byte the map's
  section G. The header map in `psc3_model.h` stopped at `+0x38`.
  `grp_00a8`'s is four one-frame scroll tracks, `u` = 256, 512, 64 and 700
  sixty-fourths of a texel with duration −3, −3, −3 and −1 — which is exactly
  the table above, since the stepper's `hold` of 1 comes out as one step every
  two frames.
- **`FUN_00221E70`** reads it at load, raises bit 2 of the model record's
  `+0x04`, and seeds **four** running copies through `FUN_002256D0` /
  `FUN_002256F0` into record `+0x18`/`+0x1C`/`+0x20`/`+0x24`.
- **`FUN_00229C40`** sets entity `+0x08` bit 3 for those models, and
  **`FUN_00229F88`** hands the entity the copy its pool slot's low two bits
  name. That is why the pyro capture has two rings on screen at `+34` and `+62`
  of the same track: four copies, not one global counter.
- **`FUN_0020C810:209-211`** steps the entity's copy once per drawn entity, then
  points the draw context at it; **`FUN_0020EEC0:97-110`** uploads seven
  `(u, v)` pairs, scaled by 1/64, as `0x640702B0` into VU1 `0x2B0`.
- **`FUN_00212058:183`** puts the subdraw's texture bank — `texFlags` bits
  13..11 — into the draw header, and VU1 adds the pair at that index. Bank 0 is
  "does not animate"; banks 1..7 are tracks 0..6. `grp_00a8` uses banks 0..4 and
  ships exactly four tracks.

The port implements all of it: `Psc3Model::uvAnimationScript`,
`EntityModelBinding::uvAnimation` (the four copies),
`PortRuntime::attachModel` (the step and the seven-pair publish) and
`SceneObjectView::uvAnimationOffsets`, applied in `drawObjectModel`'s per-corner
emit. Verified by building the draw with the bank forced to 0 and diffing a
`--screenshot` of `s14_e012` at frame 400 against the real one, and by the
accumulators advancing 256/512/64/700 per step.

Two other things the same dumps settle:

- **The alpha-12 pass is degenerate in the original.** Its 48 draws all collapse
  to a single screen point, so the box the port draws is those passes failing to
  collapse — not extra geometry and not the wrong blend. They are single-pass
  primitives, `flags 0x901`/`0x903`, colour index 0, subdraw 1 at UV
  `(135,177)..(135,215)`.
- **The port's alpha-12 vertices are legitimate.** An earlier note here said the
  original emits no alpha-12 vertices at all; that was read off a max-charge
  capture. A resting ring emits 192 of them.

#### The ring drew untextured because one of the three model tables was missing

The ring was a white drum until its sheet was actually loaded, and none of that
was the draw path's doing.

`--model-report` reports its passes as `{bound+10:48 bound+8:224 bound+9:64}`,
which reads like three global slots but is not. Every one of grp_00a8's 200
primitives carries flag `0x800`, and `FUN_00212058:183-208` sends that form down
the *other* branch: the selector is moved into packet byte 5 and byte 6 goes to
`0x3F`, the entity's own bound slot. So all 336 passes draw from slot 37 — which
is what the capture shows, all six `texFlags` groups on one page, `tbp 13716`.
`FUN_002103d0` is where that number comes from: a slot's GS base is
`0x1680 + slot * 0x104` below 24 and `12000 + (slot - 24) * 0x84` above, and
slot 37 is `12000 + 13 * 132 = 13716`. The bound slot and the page agree.

Slot 37 was simply empty. `FUN_00221fd8` walks **three** model-record tables
looking for records whose `+0x06` is 100 and loading `+0x02` into `+0x07`;
`FUN_00221fd8_staticTextureBinds` walked two, on a note that
`PTR_DAT_003228c0`'s table "lives in BSS". It does not — the original takes the
*address* of that symbol as the table base, and the records are in the
executable's LOAD segment like the other two, 226 of them with 17 static binds.
`[146] mesh=00a8 tex=0197 slot=37` is one of them, and slot 37 is reachable from
nowhere else.

With the third table walked, the port's slot table on `s01_e012` matches
`eeMemory.bin` entry for entry across 32..37, 40 and 42..44, and the ring draws
as a translucent textured swirl with the UV animation running on it. `--frames
3000 --actor-report --scr-report` on `s01_e024` and `s01_e012` is unchanged
except for the texture cache's generation counter, which counts one more load.

#### Hand of Pyro flies along the caster's facing, and nothing else

Worth stating because it is the only thing aiming the volley. `FUN_002DAB70`
takes the target only to compute two *scalars* — `+0x1BC`, the horizontal speed,
and `+0x1B0`, the climb — as "distance over flight time". The **direction** is a
straight copy of the caster's `+0x5C` at the moment of the throw:

```c
ball->facing5c = pool[casterSlot].facing5c;   // param_7, not param_4
```

`FUN_002DAE60` then resolves that into velocity every frame, and the order here
is the whole ballgame:

```c
+0x1A8 = +0x1BC * FUN_00305130(+0x5C);   // cosf -> +0x30, the X delta
+0x1AC = +0x1BC * FUN_00305218(+0x5C);   // sinf -> +0x34, the Z delta
```

The port had those two swapped, which mirrors every fireball about the 45°
diagonal — it reads as the volley flying off sideways or backwards no matter
where Orphen is pointing, and it is invisible while the caster happens to face
along an axis. `FUN_00305130` is `cosf` and `FUN_00305218` is `sinf`; the same
pair is spelled the same way round in `FUN_002493F0` and in opcode `0x5E`/`0x5F`,
so the convention across the whole port is cos into `+0x20` and sin into `+0x24`.

With it fixed and a target held, a fireball walks `(0.40, −0.96)` →
`(0.84, −1.95)` → `(1.27, −2.94)` — a heading of −66.5°, which is both the
caster's facing and the bearing to pool slot 12 at `(1.39, −3.19)`.

The volley's fan is not computed as a fan: `+0x1C6`, the link's index in the
chain, picks one of four fixed spreads (0 rises only at charge 5, 1 turns right,
2 turns left, 3 dips), and each is ramped by elapsed life over `1728`. So a
five-link chain arcs apart because its links disagree, not because anything
divides an angle.

##### `FUN_002DB230`, type `0x173`: recovered from the ELF, ten instructions

The burst a fireball turns into on contact. It is not in `src/` and — unlike
`FUN_002E4C00`, the lightning flash it is shaped like — Ghidra has no function
defined at the address either, so it came out of `SLUS_200.11` directly:

```
002db230  lhu   v0, 8(a0)
002db234  lhu   v1, 6(a0)
002db238  ori   v0, v0, 0x4000
002db23c  andi  v1, v1, 1
002db240  beqz  v1, +
002db244  sh    v0, 8(a0)      <- delay slot, so the store always happens
002db248  j     FUN_00265ec0
```

Raise `+0x08` bit `0x4000`, then destroy on `+0x06` bit 0, the
animation-finished flag. **The burst has no timer of its own**, so with no
handler nothing ever destroyed it and the explosion stood at the point of impact
for the rest of the scene. With it, the burst lives out animation 0 — about 60
frames on `s14_e012` — and goes.

Two effect types on the spell paths were in this position; the other,
`FUN_002E4C00`, was already recovered the same way. Nothing on the Hand of Pyro
or Bite of Lightning chain is reported `UNIMPLEMENTED` now.

#### Bite of Lightning, end to end

Circle's spell, the kind `< 0` arm, and the second command whose whole chain is
ported. It is not shaped like Hand of Pyro: the fire spell throws a projectile,
this one marks a spot on the ground while it charges and then hits everything
standing on it.

```
FUN_002462c8   Circle press -> pending 0x8C (hold) / 0x8D (release)
FUN_0024a360   pending + 0x3FE5 -> state 113 / 114
FUN_0024c538   113, the hold      -> the aim marker, then the charge
FUN_0024c910   114, the release   -> effect +0x60 = 1, +0x94 = level
FUN_002deae8   type 0x174, the hand effect -> FUN_002de650 on +0x60 == 1
FUN_002de650   the launch -> one 0x15C damage disc, one 0x178 flash,
                              and one 0x15C spark per victim
```

**Three entities are easy to confuse.** The ring at the caster's *feet* is type
`0x18F` (`FUN_002d9c88`), the charge gauge. The effect in the caster's *hand* is
`0x174`. The blue circle that grows on the ground at the *landing spot* is
neither — it is the single type `0x1E3` in `DAT_0031DAD0`, moved and resized
every frame by `FUN_002f1380`, whose only callers anywhere in `src/` are the six
elemental hand effects. It is the targeting marker, not a general hit flash.

##### Where the circle goes when there is no target

`FUN_002493f0` is the whole rule and there is nothing random in it:

```c
long FUN_002493f0(entity, Vec3 *out)
{
  long target = controlBlock(entity->byte95 - 1)->target2c;
  if (target < 2) {                        // no target
    out->x = 2 * cosf(entity->facing5c) + entity->posX;
    out->y = 2 * sinf(entity->facing5c) + entity->posZ;
    out->z = entity->posY;
  } else {
    *out = partyRecord(byte95 - 1) + 0x28; // what 113 tracked onto the target
  }
  return target;
}
```

**Two world units straight ahead of the caster.** It looks like it varies by
battlefield only because it varies with which way the caster happens to be
facing. Note the threshold is `< 2`, where `FUN_00249610`'s face-the-target block
uses `< 3` — genuinely different numbers. (`FUN_00305130` is `cosf` and
`FUN_00305218` is `sinf`, not the other way round.)

With a target it is the target, exactly: `FUN_002493f0` reads the three floats
state 113 has been tracking there, so on `s14_e012` the landing comes out
`(1.389, -3.188, 0.000)` against pool slot 12's `(1.39, -3.19, 0.00)`.

##### The character turns onto the target, and the port had that as a stub

`FUN_00249610:166-310`, between the state translation and the dispatch. It was
the one place in the battle module the port left as a counter rather than code,
on the reasoning that with no enemy table the branch was unreachable — which was
true right up until targeting landed, and then it read as "Orphen ignores his
target and fires wherever he happens to be pointing". Both halves of that were
the same missing block.

Five things switch it off, all of them the original's:

| gate | what it is |
|---|---|
| `DAT_0031D7BF` == `0x8E` | the kind `-1` spell hold aims itself |
| control `+0x2C` < 3 | pool slots 0..2 are the party |
| `DAT_00354ECC` != 0 | a cinematic spell owns the screen |
| `DAT_0031DA6C[member]` bit `0x400` | confusion |
| `FUN_002494E0` != 0 | party record `+0x3C`, the charge |

The last is the interesting one: it returns 0 while the state's `0x4000` restart
bit is up and the charge timer otherwise, so **the aim pins the instant a spell
starts building**. Turn first, then charge.

The turn itself is three bands off `FUN_002166E8(facing, wanted)`, closed with
`FUN_0023A320` at `DAT_003555BC * rate`:

| band | rate | runs when |
|---|---|---|
| inside 5° | `0.0043633` | always |
| 5°..45° | `0.0026180` | idle only |
| beyond 45° | `0.0061087` | idle only |

**Only an idle character turns at all** — the two wide bands are gated on the
current action being `0x06` or `0x96`. A spell released mid-swing goes where the
swing left the facing, and that is the game, not a rounding error. A zero step
back from `FUN_0023A320` means its own half-degree dead zone swallowed the
difference, and the original then assigns the exact angle rather than leaving
the remainder — so the facing lands on the bearing, bit for bit, which is what
makes the `face=`/`bearing=` columns agree.

`DAT_00354ECC` is **not** a pre-battle lock, which is what this file and the
port's comments used to call it. `FUN_002432D8` clears it, and the only writers
that raise it are five effect-entity behaviours in the `0x2Exxxx` block —
`FUN_002E01F8`, `FUN_002E1320`, `FUN_002E23E8`, `FUN_002E34B8`, `FUN_002E65D0` —
each setting it on entry and clearing it on the way out.

The frame a wide turn starts also swaps the idle animation between 2 and 7,
class 1 one way and everything else the other, guarded by bit 0 of party record
`+0x38` so it fires once per turn rather than once per frame.

The upper body is separate, class 1 only, and never while staggered. The spine
(bone `0x20`) carries whatever yaw the legs have not caught up on, clamped to
fifty degrees; during animation `0x14` at timeline cursor `+0xA8` == 4 — the beat
the cast's arms come up — the target's elevation is split three ways, half into
bone 10's Y, minus half into bone 9's X, and the negated whole into the spine
with half of it back out through Y. All three are read back with `FUN_0020D9D8`
first and written with `FUN_0020D8C0`, so the aim layers onto the animation
rather than replacing it. Party record `+0x34`, the plain distance to the
target, is written every frame the block runs and **read by nothing in the ELF**;
it is reproduced for the hardware diff only.

Two smaller divergences fell out of reading the block's neighbourhood:

- The `0x0B` park at `:120` tests the **pending** byte (`+0x0E`), not the
  current one. The port tested `+0x0F`.
- `+0x124`, the guard arc, is cleared at `:129` — before the dispatch, so state
  117 re-arms it every frame it runs and it lapses the frame the guard drops.
  The port never cleared it, which left a guard arc up for the rest of the
  scene after one Square press.

##### Party record `+0x45` is a frame number, not a flag

It starts at `-1`. When the cast animation raises `+0xAA` bit `0x200`, state 113
copies the animation cursor `+0xA8` into it, halved; from then on the state's
last line writes it *back* into `+0xA8` every frame that `+0xAA` bit `0x400` is
up. That is what pins the character in the cast pose for as long as Circle is
held — and, because the cursor still advances between pins, what makes the hold
a two-column flicker between entries 5 and 6 of animation `0x3A` rather than a
freeze. **Nothing charges before the pose is reached**: while `+0x45` is `-1` the
state is still steering the landing spot onto the target instead.

State 111 is not this shape. It captures the target position once on entry and
gates on `+0xAA` bit `0x800`; copying its body onto 113 loses the tracking, the
pin and the charge gate all at once.

##### The release is not `FUN_0024bae0`

112 tail-calls it, 114 does not — 114 is the only release state with its own
body. It holds animation `0x3A` rather than `0x14`, clears the *character's* hit
set on entry rather than the effect's, and its level runs **0..5** rather than
0..4, because `FUN_00249270(entity, 0x780)` caps the charge at `0x2580` and five
is the summon. Its `return 1` while `+0x06` bit 0 is down is the input lock:
`FUN_00249610` writes the return into `+0x62` and raises control block `+0x38`
bit 0 for any non-zero value, which is what stops a second press mid-cast.

##### `FUN_002de650` allocates both spawns as `0x174` and retypes them

Both the damage disc and the flash are `FUN_002d6c68(0x174)` with `+0x00`
overwritten afterwards — `0x15C` and `0x178`. They get `0x174`'s descriptor and
model but the other two types' behaviour. The port resolves the *handler* from
`typeId00` every frame, so that half works for free; the *model* it did not, and
a retyped entity looked up a model for a type the scene never loaded. Hence
`OriginalEntity::modelTypeId15c`, the original's own `+0x15C` model pointer: set
at spawn, unmoved by a later write to `+0x00`, and `-1` on everything else.

`FUN_002e4c00`, type `0x178`, is not in `src/` and not defined in Ghidra.
Recovered from `SLUS_200.11`, the whole function is four instructions:

```c
void FUN_002e4c00(entity) { if (entity->flags06 & 1) FUN_00265ec0(entity); }
```

A pure one-shot: play the animation the launch set, destroy on the
animation-finished flag. Its 32-frame `+0x1B0` timer is never read.

##### What `--battle-report` shows

`ring=` is the `0x18F` marker's `+0x14C` and `hitfx=` is `DAT_0031DAD0`'s, both
in thousandths. Without them the growth is invisible in a headless run.
`face=` is entity `+0x5C` and `bearing=` the angle to the control block's
target, both in whole degrees: the face-the-target block is the only thing that
closes the gap between them, so a run where they stay apart while a target is
held is that block failing.

```
orphen_port.exe --disc-root disc --scene s14_e012 --frames 700 --no-audio \
    --hold-circle 300-460 --battle-report
```

```
f=301 action=0x8c state=0x0071 charge=0    ring=1000 hitfx=1500
f=365 action=0x8c state=0x0071 charge=1152 ring=1166 hitfx=2652
f=449 action=0x8c state=0x0071 charge=3840 ring=1560 hitfx=5340
f=463 action=0x8d state=0x0072 charge=4224 ring=1612 hitfx=5724
```

`hitfx` is exactly `charge/1000 + 1.5`, and at a full `0x2D00` charge it reaches
`11.1` — the read caps at `0x2580`. Hold to full and the level is 5; with no
enemy table the target stays `-1`, so `level == 5 && target > 1` is false and the
summon branch is not taken. The box the launch lays down is `1.5 * level` in the
horizontal plane and `0.5 * level` vertically, centred on the landing spot: at
level 5, `x [-5.5, 9.5] y [-7.5, 7.5]` around `(2, 0, 0)`, 5 contacts and one
`0x15C` spark each.

The hold *must* start after the battle does. `--hold-circle` presses on its first
frame only, and `s14_e012` raises `sGpffffb052` bit 0 on frame 247; a press
before that is simply not seen.

##### Deliberately not ported: the summon

`FUN_002deef0` is the only entry, taken from `FUN_002de650` on `level == 5 &&
target > 1`. It spawns type `0x13E` at animation 5 (behaviour `FUN_002df018`, in
`src/`), arms a 32-frame timer at `+0x1AC`, and raises `uGpffffaf5c` — the
battle-interrupt flag that stops the rest of the battle while the spirit plays.
The port returns 0 from that branch with a comment rather than approximating it.

### The five level-5 summons

Released at full charge with a live target, an elemental spell is not thrown at
all -- it becomes a creature, and the creature takes the scene over for its run:

| spell | type | behaviour | spawner |
|---|---|---|---|
| 7 Bite of Lightning | `0x13E` | `FUN_002DF018` | `FUN_002DEEF0` |
| 8 Falcon of Death | `0x13F` | `FUN_002E34B8` | `FUN_002E2F50` |
| 9 Hammer of Evil | `0x140` | `FUN_002E23E8` | `FUN_002E1F28` |
| 10 Pinnacle of the Sun | `0x141` | `FUN_002E01F8` | `FUN_002E00D8` |
| 11 Hail of Heavens | `0x142` | `FUN_002E1320` | `FUN_002E0E60` |

The stage all five set is in `ported/entity/original_summon_stage.cpp`; the
creatures themselves are in `actor_frame_update.cpp` beside their launches.
Three things make the stage:

- **The freeze.** `FUN_002DE4A8` raises `+0x02` bit `0x800` on every live slot
  but the target cursors -- the bit `FUN_00239CE0` and `FUN_002261E0` already
  skip on, so one write stops every behaviour and every physics step in the
  scene. The creature then hands the bit back to itself, the player, the
  caster's ground ring and shield effect, and the shared hit effect.
- **The dim.** `DAT_0058BB00` is a 256-bit mask of the slots that fade with the
  stage: every live slot from 2 up whose `+0x96` has neither low bit and whose
  `+0x134` is already zero, minus the handful `FUN_002D6F38` takes back out.
  `FUN_002D6FA0` then stamps `DAT_00355700 / 100` across the whole set every
  frame, so the map's global fade cap and everything standing on it darken
  together and the creature and the caster do not.
- **The camera.** Two natural cubic splines, built in the creature's own frame:
  `FUN_00266CE8`'s sample is pulled apart into a length and an angle, the
  creature's facing is added to the angle, and the result is offset by the
  creature's position. Four to nine points apiece, read straight out of
  `SLUS_200.11` at `DAT_0034FAE8`..`DAT_0034FC7C`. Bite of Lightning is the
  exception: its eye and look-at are simply the two bones carrying roles 1 and
  2 on its own model.

The creature does not move. Every frame it copies the caster's position and
facing, and the caster is turned one capped step -- an eighth of a degree --
toward the angle latched on the frame the camera started. The beats are
animation markers: `(+0xAA & 0xF00)` naming 7, 6 and 3 with `+0x06` bit 2 up
are the three shouts (voice clips 2, 3 and 4 on `DAT_0031DA65`'s channel), and
3 is also where the damage goes off. The blast is the spell's *own* launch
called back at **level 6**, which clamps to 5 everywhere it is read and is not
5 for the `level == 5` test, so a creature can never summon another one; it is
thrown from the target's position, not the caster's.

`DAT_00354ECC` is raised for the whole run. That is what makes the four kind -2
arms of `s14_e031` wait the retail length instead of advancing the frame after
flag 861: with the summons in, the hand-offs move from 2120/2127/2126/2127 to
**2245/2247/2357/2271**.

Not ported, and named at the call sites:

- `FUN_002D7038`, the flat veil the dimmed field recedes into -- one quad over
  the whole virtual screen in bucket 2, under the world. The port has no path
  that submits a raw packet that early, and the two halves that carry the
  effect, the map fade cap and the per-entity `+0x134`, are both in. Its alpha
  is still tracked so the frame-smear handoff stays faithful.
- `DAT_0031DA1E`, the first-time spirit-name banner. Four of the five arm it;
  nothing in the executable draws it, and its only reader counts its timer down.

### The barrier was attached to the caster's hand

For a while the shield barrier drew as a flat green slab standing beside the
player instead of a cylinder around him, and it read as a poser bug because the
port's animation state matched hardware exactly -- animation 0,
`poseColumn(+0xAC) = 1`, `prev(+0xAE) = 18`. It was not. `grp_00bd` is eight
coincident curved sheets on eight children of one bone, with no rotation key in
any of them at the hold column, so the shape is entirely the root matrix's
doing; `shield_of_immunity` has slot 10's `+0x192` at **-1**, and the port had
it at **0 / bone 0x12** -- the caster's hand.

**`FUN_0024BD30` writes neither `+0x192` nor `+0x194`.** `FUN_0024C058:41-47`
(state 111) and the state-113 body both do, and the port shared one
`respawnSlotEffect` helper across all three, so the barrier inherited the hand
attachment and took the caster's hand bone as its root -- a tilted matrix a
metre off the floor. The helper now takes an `attachToCaster` flag and state 115
passes false, which is the original's own split.

The whole live palette matches hardware after it, bone for bone: bone 0 at
`(0.200, -0.000, 0.000)` with a clean 90-degree yaw at scale 1.5, and children
at `(0.222, -0.037, 0.753)`, `(0.219, -0.007, 0.806)`, `(0.220, -0.039, 0.702)`
and on through bone 9's sentinel, every one of them the `shield_of_immunity`
value to three decimals.

### Orphen stood in front of his own shield: entity `+0x133`

With the barrier rooted correctly it was still drawn *behind* the caster -- the
cylinder's near wall never covered him, where hardware shows him dimly through
it. Nothing about the pose was wrong. Blended primitives draw with `ZMSK` set,
so they test depth without writing it, which makes submission order the only
thing that decides who wins; the barrier's origin is the caster's own position
plus half a unit, so it sorted into the caster's bucket and lost.

`FUN_0020C810:216` reads entity `+0x133`, scales it by `fGpffff80c4`
(`0x00352034` = 0.08) into the draw context's `+0x140`, and `FUN_0020EEC0:181`
keys the sort on `ctx+0x68 + ctx+0x140` -- the view depth **plus that bias** --
rejecting the result below `fGpffff811c` (0.1) to the far end of the table. The
port applied the byte on the sprite path only, and the model path's own comment
recorded it as "a field the port does not model".

It is how every effect that wraps a character gets in front of it:

| writer | `+0x133` | effect |
|---|---|---|
| `LAB_002DE0B8` (type `0x143` only) | `-10` | Shield of Immunity's cylinder |
| `FUN_002E34B8` | `-48` on the veil, `+48` on slot 0 | the summon veil, caster pushed the other way |
| ground rings, markers, discs | `-12` | a unit nearer than they stand |
| 44 of the 124 primary descriptors | `-2` | every character, uniformly |

Only `0x143` takes the bias: `0x002DE1F0` tests the type against `323` and
branches past the `addiu v0,zero,-10` / `sb v0,0x133(s0)` pair for `0x127` and
`0x144`, which get their cue and nothing else. Shield of Inferno's cage really
does sort at the caster's own depth on hardware.

`s01_e024` and `s14_e012` are visually unchanged by it, and the `--frames 1200
--actor-report --scr-report` guard is byte-identical on both plus `s14_e001`:
this is a render-side read of a field the simulation was already writing.
### Two HUD pieces the spell demo showed and hardware does not

`s14_e031` drew a health bar for every spell that lands on the target dummy, and
left the target cursor parked in the corner of the screen through the narration
close-up. Both are the same shape of bug: a gate the original has and the port
did not read.

**The bar.** `FUN_00216140:102` arms `FUN_002D5630` only when the victim's
`+0x02` carries one of `0x4B` **and its `+0x96` bit `0x20` is clear**. The dummy
(type `0x8B`) has descriptor flags `0x0008`, so it passes the first test and
takes the upper bank -- and the scene's init writes `32` into its `+0x96`
through object register `0x40` for exactly that reason. That register was the
one `FUN_0025C8F8` case the port had a name for but no implementation, so the
write was counted as unmodelled and dropped, and the gauge slid on. Bit `0x20`
has one reader in the whole executable and this is it.

**The cursor.** `FUN_002D73E8` never returns early. Its VU0 divide is clamped,
so `FUN_0020B600` always writes a screen position back, and the two depth
rejects either side of it -- `w` over `DAT_0035479C`, the pre-divide `z` at or
under `DAT_003547A0` (0.3) -- only raise `+0x198` bit 0 and fall through to the
tail, where a non-zero `+0x198` is what raises `+0x08` bit 0 and stops the draw.
The port bailed out at the rejection instead, so the marker kept the last
position it had projected to; the narration camera looks away from the dummy, so
that position was the top-left corner and it stayed there.

Two more gates went in with it, both of which decide the same thing and neither
of which fires in this scene: `DAT_00354FC2 & 5` must read exactly 1 (running,
not suspended by opcode `0xBD` method `0x76`), and `DAT_0031DA6C` bit `0x20` on
the driven member hides them too. The third term, `DAT_003555C6`, is the
attract-mode demo flag `FUN_00271558` raises on a title-screen timeout; the port
has no attract mode, so it is zero by construction.

All eleven arms still hand off to the same destination scenes, and the
`--frames 1200` guard is byte-identical on `s01_e024`, `s14_e001` and
`s14_e012`.

