# Entities and the party

### Entity pool

`src/ported/entity/` is the pool at `DAT_0058beb0`: 256 slots of **0x1D8** bytes
(the decompiled `slot * 0xec` is over an `undefined2 *`, so it is halfwords), with
the status array `DAT_005a96b0` landing exactly at the end of it. Scripts
allocate from `[10, 256)`; **slot 0 is the lead player**, which is why
`DAT_0058bed0` is the camera's read of the player's world X. The player
controller writes through a pointer bound to that slot.

Collision radius and height come from the type descriptor, which lives in static
tables inside `SLUS_200.11` rather than in any disc resource.
`src/ported/resource/elf_data_reader.*` maps PS2 virtual addresses to file
offsets so those tables can be read directly. It is optional: `--elf` overrides,
the disc root is searched for `SLUS_200.11`, and without it objects fall back to
a default box size and say so. Ids from `0x272` up ship with the map and cannot
be resolved this way at all.

### Actor behavior is not script

Once an entity exists, what it does each frame comes from native code, not from
the SCR. `FUN_00239ce0` walks pool slots 2..255 and calls a function selected by
the entity's **type id** through four function-pointer tables in `SLUS_200.11`;
each of those handlers then dispatches again on the entity's **state** (`+0x60`)
through a per-type-family table. The lead player is slot 0 and is updated by
`FUN_00251ed8` on its own path, so this loop never sees it.

`src/ported/entity/actor_dispatch_table.*` reads the four tables out of the
executable rather than transcribing 700-odd pointers, reproducing
`FUN_00239ce0`'s unsigned range tests literally -- including the seam that sends
type `0xFB` to the primary table's index `0xFA`. `actor_frame_update.*` is the
loop, the freeze gate `FUN_0023a068`, and the fade path `FUN_0023a568`.

**Behaviors with no port do nothing and are counted**, the same discipline the
opcode VM uses. `--actor-report` lists every live entity with the handler address
it resolves to and whether that handler is ported. That report, not guesswork,
picks the next behavior to write.

Several behaviors are implemented outright. Type `0x37`, `FUN_00258ab8`, the
party follower, has its own section below. Type `0x3A`, `FUN_002d1ea8`, the
treasure chest.
It is the only handler in the game with no state table -- it switches on the
animation id directly. Its `+0x198` is an **event flag id** (the placement
record's param byte plus `0x400`), not a pointer; flag clear means closed, set
means opened. See `analyzed/actor_behaviors/type_0x3A_treasure_chest.c`.

### The party follows Orphen (type `0x37`)

After the opening cutscenes hand control back, Cleo and Magnus are supposed to
walk after the player. They stood still, and the reason was one operand.

Opcode `0xAC` (`FUN_002631f0`) binds an entity into a party slot and stamps type
`0x37` over it. It evaluates **four** expressions and reads the entity back from
`sp+8` -- the *third*, not the fourth. The port took the fourth, which in
`s01_e012` is `180`; that is not a pool index, so every bind resolved to nothing
and no follower was ever created. (`180` is not random: it is the value the
handler stores into `+0x1A2` a few lines later, so the script is pushing a speed
the opcode stopped reading.) Two other details of the same handler were wrong or
missing -- the "deliberately empty" path *sets* the slot flag rather than
clearing it, and the free-lane scan that assigns `+0x1C6` had no port.

With the bind fixed, `src/ported/entity/party_follower.*` is `FUN_00258ab8` and
its state table `PTR_FUN_0031e1a0`:

| state | original | what it does |
| --- | --- | --- |
| 0 | `FUN_002596c8` | init: pick this follower's side of the lead (`+0x1BC`) |
| 1 | `FUN_002597d0` | idle: watch the lead, then decide whether to walk |
| 2 | `FUN_00259D00` | turn to an angle, then push through on it |
| 3 | `FUN_00259e50` | turn in place |
| 4, 5 | — | navmesh walk, waypoint walk. **Not ported** |
| 6 | `FUN_0025a298` | wedged: teleport back onto the lead's trail |
| 7 | `FUN_0025a450` | walk blind, after being shoved |
| 8 | `FUN_0025a500` | **the follow walk** |
| 9 | `FUN_0025aa48` | sidestep out of a crowd |
| 10 | `0x0025AB48` | hold a stagger until the floor is back |

Three things about it are worth writing down.

**The formation is a ring, not a queue.** A follower walks to
`lead + (cos, sin)` of `lead.facing + its own +0x1BC`, one unit out. The first
one bound takes +150 degrees and every later one takes the *negation* of the
first one it finds, which is why two followers end up on opposite shoulders.
It only re-aims when it is more than 1.5 units from the lead and more than 0.3
from the spot, so a stationary party settles rather than jitters.

**Walk versus run comes off the stick, not off the lead's speed.**
`FUN_0025a500` reads `DAT_003555e8`, the analog magnitude, and picks animation 4
under 100 and animation `0x0E` at or over it. Past two units it stops matching
the gait at all and just adds a flat `0.05` to its step to catch up.

**The idle look-at is a bone override, not a turn.** State 1 twists the bust and
head toward the lead through `FUN_0020d8c0` -- the bust by the full angle, the
head by 0.3 of it. Past 60 degrees it gives up on twisting and hands over to
state 3, which turns the whole body. That needed two new callbacks into the
model layer (`FUN_0020da68`'s sampler and `FUN_0020d9d8`'s filtered pose),
because a follower's rest pose has to be read before it can be twisted.

The lead's breadcrumb trail (`FUN_00224060`, `DAT_00355704`) is ported with it.
It is not map data: it is a 512-entry ring of where the lead has actually been,
appended to whenever it moves a quarter unit. State 6 walks it backwards for
somewhere off camera to reappear, which is a follower's only way out of a wedge.

#### The navigation graph

`ported/entity/follower_navmesh.*` is `DAT_00355038`: one 0x34-byte record per
map primitive, holding four neighbours, a BFS depth per follower lane, and a
depth per lane per edge. `FUN_00257fc0` seeds it, `FUN_002582d0` builds it,
`FUN_002584b0` floods it, `FUN_00258c70` steps down it.

**Adjacency is discovered by probing, not by reading a stored graph.** There is
no navmesh in the map file. `FUN_00258080` stands at the midpoint of each of a
primitive's four edges, steps 0.2 sideways -- once to each side, because the
winding is not known -- from 0.3 above the primitive's centre, and asks the
ordinary single-point ground query what is underfoot. Whatever answers, if it is
a different primitive under 50 degrees and not one of the rejected surface
classes, is the neighbour. `FUN_002582d0` runs that as a flood fill from the
lead's spawn, so the graph is exactly the floor reachable from where the scene
starts.

That costs about eight ground queries per primitive at load. It is the 65537
`FUN_00227840` calls the PS2Recomp spike sees during a map load, and it is why
it runs once rather than per frame -- 0.2 s of load time for `s01_e012`,
unmeasurable against the rest.

It is rebuilt by **opcode 0xAB** (`FUN_00263148`, teleport the lead) and nowhere
else. A scene that opens a door does not rebuild it, and does not need to: the
probe's body band is the floor's own height plus 0.3, so a door panel standing
on that floor is above the band and the query answers with the floor behind it.
Doorways link whether the door is open or shut.

`[nav] follower graph 159/3948 primitives reachable from (-0.12, -2.699, -1.5)`
is printed once per load. The second number is every collision primitive in the
map -- walls, ceilings, props -- so a small first number is normal. A first
number of 0 or 1 means the seed missed the floor, and every follower in that
scene will fall back to `FUN_00259178`'s blind turns.

#### What is still missing

`FUN_0025a0c8`, state 5, and only because **nothing can reach it**.
`FUN_00259378` gets there through `FUN_00258b80`, whose retail body is a
511-iteration loop over the breadcrumb ring that discards every distance it
computes and then returns `-1` unconditionally (`addiu $v0, $zero, -1` at
0x00258c48; Ghidra reports the same thing as an unreachable block). The `-1`
always routes to state 6 instead.

#### Leaving the party releases two bones

`FUN_002589c0` -- the thing opcodes 0xAD/0xAE reach, and what a cutscene runs to
take a follower back under script control -- ends with more than a type
restore:

```
FUN_00251db8();                                  // the follower re-sort
FUN_00267e78(entity + 0x198, 0x40);              // the whole follower block
FUN_0020d9c8(entity, FUN_0020dd78(entity, 2));   // the bust bone
FUN_0020d9c8(entity, FUN_0020dd78(entity, 1));   // the head bone
```

Those last two are the overrides `FUN_00257c78`'s look-at was holding. While
`+0x168 + bone` is non-zero `FUN_0020d9d8`'s filter takes the override over the
sampled pose, so an animation played afterwards cannot move that bone at all.
The port restored the type and left the overrides installed, which pinned
Magnus' torso upright through the flooding shot in `s01_e012` with his bust
still aimed at where Orphen had been standing. The block clear is ported with
them; `FUN_00251db8` is not.

#### Falling

`FUN_002262c0:99-113` integrates gravity into `+0x38` for every actor, and the
only thing gating it is **`+0x04` bit 3**:

```
dt     = DAT_003555bc * 0.125
+0x38 += v * dt - (g * dt) * dt * 0.5
v     -= g * dt        (nudged to -1e-05 if it lands on exact zero)
```

The port used to skip this outright, on the reasoning that the EE dump shows the
six type `0x62` enemies with `+0x44` at zero. They do -- because they read
`+0x04 = 0x000b`, bit 3 set, so gravity never touches them. Every *character* in
the same dump reads `+0x48 = 0.00075` and `+0x44 = 0`, the latter because the
landing clamp zeroes it once they are resting. `+0x48`'s default in
`OriginalEntity` was a 24.0 placeholder that nothing ever integrated; it is now
that 0.00075.

The settle at `:481-520` is not symmetric. A rise is provisional -- it is taken,
tested against the headroom query, and given back whole if the query refuses. A
fall is simply clamped at the cached `+0x4C`, which is also the only thing that
raises `+0x28` for a stationary actor.

Without the fall half, a follower that stepped onto anything never came down:
the port's clamp only ever raised. The small bumps in the floor of `s01_e012`'s
chest room left Cleo and Magnus hovering a tenth of a unit up for the rest of
the scene.

#### Walls: the retry ladder, not an axis split

The party follower is the port's first ground-*walking* non-player actor, and it
found two things `integrateNonPlayerMovement` had invented.

**A refused move is retried on rotated headings.** `FUN_002262c0`'s velocity
section is one `do { } while (true)`, and the wall case falls into a ladder at
0x00226b58 that re-runs the entity clamps and `FUN_00227390` in full on five
headings derived from the request's own -- constants from the block at
0x0035243c:

| attempt | heading | speed |
|---|---|---|
| 0 | as requested | x 0.3 |
| 1 | +20 degrees | x 0.7 |
| 2 | -20 degrees | unchanged |
| 3 | +60 degrees | x 0.5 |
| 4 | -60 degrees | unchanged |
| 5 | give up |

Each retry does `flags & 0xffff7ffd | 0x4000`, so the refusal bit comes back
down and a graze the fan-out resolves does not count as a block at all. The port
used to try X alone and then Z alone, which is a different shape and grinds
along a wall instead of going round it.

**`0x20` and `0x40` are entity bits, not terrain bits.** `FUN_00228380` /
`FUN_002285d8` (the X sweeps) write `0x20`; `FUN_00228838` / `FUN_00228a90` (Z)
write `0x40`. The axis fallback was minting them from geometry, and the follower
reads `+0x0C & 0x60` as "an actor is in my way, queue behind it" and `& 0x262`
as its stuck counter -- so a wall was being reported as a person, and every
graze counted toward giving up. The port had also collapsed all four blockers
onto `0x20`; they are now split by axis.

**`+0x0C` is rebuilt every frame.** `FUN_002262c0` seeds a workspace word to
zero at :37 and stores it over `+0x0C` at :628; nothing ORs into the entity's
copy in place. The port only ever OR-ed, so once a follower was blocked the bits
never came down again -- it counted to five standing on open floor and sat in
state 6 for the rest of the scene. That was the bug behind "the party stays in
its cutscene positions and never follows again".

**This moves `s01_e024`.** The type `0x62` enemies read the same `+0x0C & 0x202`
for their scatter branch, so their positions after 3000 frames differ and the
shared RNG stream shifts with them. `s01_e012` is byte-identical at 3000 frames.
The change is toward the original, not away from it -- `FUN_002262c0` is one
function and every actor goes through it -- but it is a visible change to a
scene that was tuned without it, so it is called out here rather than buried.

### `+0x168` is inside the slot, so clearing the slot clears it

Orphen's jaw came apart in the s01_e012 close-up when the scene was **played**,
and never under `--arm-stream d780:13400`. The armed run put the close-up head in
pool slot 72; play puts it in slot **64**, which is where the save state has it
too -- so the allocation was right and the difference was what had been in the
slot before.

A `G` snapshot from play, differenced against the head's palette in the save
state (`0x00357E00 + 64 * 0xA80`), named the bones:

```
bone   port(rel to bone0)          real(rel to bone0)         delta
   1  (  0.000, -0.010,  0.021)   (  0.000, -0.010,  0.021)     0.3 mm
   2  ( -0.000,  0.049,  0.081)   ( -0.000,  0.049,  0.081)     0.1 mm
   3  (  0.000, -0.010,  0.021)   (  0.000, -0.019,  0.061)    41.1 mm
   4  (  0.000, -0.010,  0.021)   ( -0.006, -0.069,  0.053)    67.4 mm
   5  (  0.000, -0.010,  0.021)   (  0.006, -0.069,  0.053)    67.4 mm
   6  (  0.000, -0.010,  0.021)   (  0.000, -0.072,  0.052)    69.4 mm
   8  (  0.000, -0.074,  0.062)   (  0.000, -0.074,  0.061)     0.8 mm
```

Bones 3, 4, 5 and 6 sit at exactly bone 1's position -- bone 3's parent -- and 7
follows because it is 3's child. Everything else is right to under 2 mm.

**`FUN_002cdb28` drives bones `{3,4,5,6}`.** `DAT_00326650` is that list and
`DAT_00326640` the roll angles; it is the wing flap on every type `0x62`, it sets
translation 0 with duration 0, and a bone overridden to zero translation lands on
its parent. One of them had lived in slot 64 earlier in the scene and left its
overrides behind.

In the original that cannot happen, because **entity `+0x168`..`+0x191` -- the 42
per-bone override modes -- are part of the 0x1D8-byte slot**, and
`FUN_00229c40:20` opens with `FUN_00267e78(param_1, 0x1d8)`: allocating a slot
zeroes the modes along with everything else. The port keeps them in a side table
(`EntityBoneOverrides`, so the mode byte and the pose it selects stay together,
which is still the right call) and `entity = OriginalEntity{}` does not reach it.

`EntityPool` now owns a pointer to that table and clears the entry in `reset`,
`releaseSlot` and `FUN_00229c40_initialize` -- the three places it clears the
struct. Reproduced deterministically by planting `{3,4,5,6}` overrides on slot 72
at frame 14000 before the rig builds: without the clear the armed run collapses
to the same `(0.000, -0.010, 0.021)` on all four bones, with it the jaw is
correct to hardware. The armed-stream capture at 15810 is unchanged.

**The lesson is about storage, not animation.** Any per-slot state the port keeps
outside `OriginalEntity` has to be cleared by whatever clears the slot, because
in the original it was never outside it. The pose filter bank at `0x003FFE00` is
the other one, and it is handled separately -- see the `seeded` flag.

