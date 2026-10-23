# Field combat

## The sword attack, and the input buffer behind it

Circle, grounded, swings a glowing sword. `FUN_00256bb8`'s attack branch
dispatches on `FUN_002298d0(*entity)` -- the entity's **type id**, not an
equipped item -- and type 1, the lead player, answers weapon class 0, which is
`FUN_00225bf0(entity, 0x1C, 0x33)` and nothing else. Everything after that is
driven by the animation.

`FUN_00251ed8` sends states at or above `0x1C` through a *second* dispatch
table, `PTR_FUN_0031e160`, whose first entry is `FUN_00256130`. The two tables
are adjacent in memory -- `0x0031E160` is `0x0031E0E8 + 0x78` -- but they are
genuinely two tables, offset by two entries, and indices `0x1C`/`0x1D` of the
first are null. Ported states have to be routed by hand; falling through to the
grounded field branch, which is what the controller used to do for anything that
was not state 2 or 10, re-reads the pad and overwrites the swing animation with
`stand` on the next frame.

### The state is four points in a timeline

`FUN_00256130` reads no input at all. It watches entity `+0xA8`, the timeline
cursor `FUN_00225c90` steps by two per keyframe, and `+0x06`, the three latches
that stepper writes:

| when | what |
|---|---|
| cursor reaches 2 (`+0x06` bit `0x08`) | spawn the blade, type `0x42` |
| that keyframe's duration expires (bit `0x04`) | play cue `0xA4`, the swing |
| a keyframe carrying `+0xAA` bit `0x200` | put the blade on its dissipate animation |
| the animation completes (bit `0x01`) | `FUN_002560e8` returns to idle |

`+0xAA` is the third halfword of the current keyframe record, and it is where
the animation data carries its own events. Animation `0x33` on `grp_0001` is
nine entries totalling 46 frames, and entry 7 is the one holding `0x200`:

```
[ 0] col= 75 dur=10  trailing=0x0000
[ 1] col= 76 dur= 6  trailing=0x0000    <- cursor 2, the blade spawns here
[ 2] col= 77 dur= 4  trailing=0x0d00
[ 3] col= 78 dur= 2  trailing=0x0400
[ 4] col= 79 dur= 2  trailing=0x0400
[ 5] col= 80 dur= 4  trailing=0x0400
[ 6] col= 81 dur= 4  trailing=0x0000
[ 7] col= 82 dur= 2  trailing=0x0200    <- the blade is told to dissipate here
[ 8] col= 83 dur=12  last entry
```

The swing is committed the moment it starts: no movement, no facing change and
no pad reads for the whole 46 frames. That is why it cannot be steered, and why
the character slides to a stop rather than turning into the blow.

### The blade is an entity, and it carries a light

Type `0x42` is `grp_0179` out of the boot bundle -- four bones, three
animations -- attached to the swinging entity's **role-5** bone. Not role 4:
role 4 is the right hand, where a held weapon goes, and role 5 on `grp_0001` is
bone 17, a finger. The blade grows out of the fist rather than being held. Its
`+0x158` is set to `DAT_00352998`, which is pi -- a roll on the root matrix,
not a facing; the facing is copied separately into `+0x5C`.

`FUN_002d21b8` runs it. Every frame it drives a `DAT_00343888` light slot from
its own bone 0, ramping the colour from 128 grey to 255 white as `+0x62` climbs
`0x1000 -> 0x1FE0` at eight per tick, and steps `+0x134` by a flat 4 until it
passes `0x78` and then drops it to 0 -- so the blade fades in from `4/128` alpha
over about thirty frames and then draws solid. Its own animations are the rest
of its lifetime: 1 is the swing, 0 the idle it falls into when that completes,
2 the dissipate, and completing 2 deletes it.

It also re-reads `DAT_0058BF10` -- pool slot 0's `+0x60` -- every frame and
deletes itself the moment the lead is not in state `0x1C`. The blade cannot
outlive the swing even if its own animation has not finished.

**`FUN_00265ec0` is not `EntityPool::releaseSlot`.** The pool's own release is
the map-load clear; the original's does three things first, and one of them is
`FUN_00266098`, which hands the entity's light slot back by writing its radius
to 0. That is the *only* thing that frees a light slot, so without it every
swing would leave a two-unit white light burning where it ended and sixteen
swings would fill the table. `FUN_00265ec0_destroy_entity` in
`actor_frame_update.cpp` is the full version; `--scr-report` after four swings
shows slot 0 allocated, driven and released each time.

### `FUN_0023b890(8)` is an input buffer, and it was missing

`FUN_0023b5d8` pushes one word per frame into a 64-entry ring at
`DAT_00342a70`, packed `held << 16 | pressed`, where both halves are the raw pad
run through the remap table at `DAT_00571A50`. That table is the **identity** in
both EE dumps, so a mapped action bit is the raw pad bit: `0x80` Square is jump,
`0x20` Circle is attack, `0x10` Triangle is use.

`FUN_00256bb8` does not read the current frame's buttons. It calls
`FUN_0023b890(8)`, which ORs the last eight entries together -- so a press
counts if it happened at any point in the last eight frames. That window is the
game's input buffer: an attack pressed during the tail of a jump still fires on
the frame the player lands. The port had been feeding the controller a single
instantaneous `jumpRequested` bool; `MappedActionHistory` in
`ported/input/mapped_action_history.h` is the ring, and jump now goes through it
as well.

One bug fell out of writing it. `InputSnapshot::rawPressedPad` was not in
`main.cpp`'s `consumeEdgeTriggered` list, so on a frame that ran several
catch-up simulation steps every edge-triggered pad read -- the interaction
probe, the camera's recentre -- saw the same press once per step. The original
computes it as `held & ~previous` once per tick, which can only be true on one
of them.

### Not ported

`FUN_002148a8`, the swept hit test the blade runs on its animations 0 and 1, and
`FUN_002d59c0`, the reaction it triggers. It is several hundred lines of capsule
sweeps against the entity pool, and nothing in the port takes damage yet.
`FUN_00216078(ownerType, 0, effect + 0x198)`, which fills the blade's `+0x198`
from a per-type parameter table, is skipped with it -- that field is the hit
test's only reader. Entity `+0x12C`, the attack power `FUN_00215670` subtracts
the defender's `+0x12E` from, *is* modelled and copied onto the blade, but
nothing fills the player's copy: `FUN_00251dc0` reads it out of the party stat
table at `DAT_0034368F`, which the port does not load.

Weapon classes 1..5 -- the other things the lead can be holding, two of which
throw a type `0x4E`/`0x50` projectile from this same branch -- are not
reproduced, because there is no inventory to reach them through.

### Checking it

`--press-attack <frames>` fires Circle on a comma-separated list of 1-based
frames, the same shape as `--press-confirm`, so the swing is reachable from a
headless or captured run:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root disc --scene s01_e024 \
    --no-audio --frames 300 --press-attack 60,120,180,240 --actor-report --scr-report
```

reports `type=0x42 entities=1 ticks=124 firstSlot=29` -- four swings of 31 ticks
each, all recycling the same pool slot -- and one light slot allocated and
released. `s01_e024` is the scene to use: the lead is controllable within a
frame or two of load, where `s01_e012` opens on cutscenes.

`--press-jump <frames>` is the same for Square, and `--hold-attack
<first>-<last>` holds Circle across a range **without** re-pressing it, so it
arms the moon jump without starting a swing on every frame. Together they are
the only way into `FUN_002534D8` from a headless run:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root disc --scene s01_e024 \
    --no-audio --frames 60 --press-jump 15 --actor-report
```

lands at frame 59 and holds animation `0x10` -- the landing -- until frame 67.
Adding `--hold-attack 18-140` and a tap every six frames climbs instead:
`0 -> 1.33 -> 2.69 -> 3.04`, against the plain jump's 1.86 apex.

## The magic cast, and the sprite-strip model kind

Triangle, grounded, throws a homing magic projectile. It is the *use* branch of
`FUN_00256bb8`, not a second attack branch -- the same `FUN_002298d0` type-id
lookup, the same weapon class 0, and for the lead player that is state `0x1D`
with animation `0x14`. `PTR_FUN_0031e160[1]` is `FUN_002562b0`.

The cast cue `0xA5` plays on entry, from `FUN_00257b50`, rather than partway
through the animation the way the sword's `0xA4` does.

### The projectile exists for the gap between two keyframes

`FUN_002562b0` has the same shape as the sword's state -- `FUN_002560e8` at the
top, then points in the timeline -- but the projectile is alive between them:

| when | what |
|---|---|
| cursor reaches 6 | `FUN_002d2e00` spawns it, charging, at the caster's role-4 bone |
| every frame while its `+0x60` is 0 | its `+0x20` is rewritten to that bone |
| cursor reaches 10 | launch: `+0x60 = 1`, `+0x04` bit `0x100` cleared, cue `0xA6` |
| the animation completes | back to idle |

**It is not attached through `+0x192`.** The caster writes its position every
frame instead, which is a different thing: an attachment rides the bone matrix,
this is a copy. That is why interrupting the cast leaves the charge where it was
rather than dragging it, and why `FUN_002d2470`'s state 0 has to delete itself by
watching `DAT_0058BF10` for the caster leaving state `0x1D`.

`FUN_002d2e00` refuses to spawn at all if `FUN_00227798` says the floor is above
the hand, which is the guard against casting while clipped into geometry. The
original treats that refusal and a full pool identically: drop the cast.

### The homing is two angles and a ramp

`FUN_002d2ca8` picks the target **once**, at spawn, and nothing ever revisits it
-- so the projectile locks on and can be dodged by moving afterwards. It scans
pool slots 10 upward, so it can never choose the caster, and takes the nearest
candidate within ten units whose `+0x02` has bit `0x08` and whose `+0x04` does
not have bit `0x10`. The elevation gate reads backwards from what you would
expect: a candidate more than two units away *vertically* is accepted on
distance alone, and only one within two units also has to sit inside a
60-degree cone (`fGpffffa738`).

In flight, the yaw at `+0x5C` and the elevation at `+0x1A0` each step toward the
target through `FUN_0023a320`, capped by `+0x1A4` -- which starts at **zero** and
ramps by 0.005 a frame to 0.349 (20 degrees). The projectile leaves the hand
travelling dead straight and only tightens later. That ramp is most of why it
reads as a guided missile rather than a tracking beam, and it is the reason a
cast at something close often misses.

Three things detonate it: the swept hit test, `+0x0C` picking up any of `0x266`,
or outliving `0x2580` ticks. Then `+0x60 = 4`, and state 4 fades its light down
by two a frame until it is out.

Every other frame it drops a ghost of itself -- same type, same position, same
scale, `+0x60 = 2`, physics off -- which shrinks by 0.05 a frame and dies when
*its* animation ends. That is the trail, and it is what made the sprite-strip
work below unavoidable.

### `+0x02` bit `0x200`: the other model kind

`FUN_00225c90`'s very first line branches on entity `+0x02` bit `0x200`, and the
port only ever had the `== 0` half. Type `0x44` is the first thing to carry the
bit, and its model, `grp_017e`, is 324 bytes with **no PSC3 magic**:

```
+0x00 u16  sprite record count   (12)
+0x02 u16  animation count       (2)
+0x08 u32  sprite record table, 16 bytes each
+0x0C u32  animation table, one u32 offset per animation
```

The animation table sits at the same header offset a PSC3's does, which is what
lets `FUN_00225c90` read `+0x9C` the same way for both. Its entries are **four**
bytes rather than six -- a column and a duration, and no trailing event halfword,
so a sprite strip cannot carry the keyframe events the sword swing is driven by.
Duration bit `0x8000` is still "last entry"; bit `0x4000` is new, a conditional
entry skipped unless `+0x06` bit `0x20` is set. The blend at `+0x13C` is left at
1.0 throughout: sprite frames cut, they do not interpolate.

`grp_017e` animation 1, the one the trail ghosts play, is ten entries of two
frames on columns 2..11.

This was not optional. The ghost's lifetime is owned by `flags06` bit 0, which
only the animation stepper sets; without the branch every ghost the projectile
dropped lived forever and the pool filled in a few seconds.

### What is drawn

The projectile is drawn by the **second** entity pass, `FUN_0020f3e0`, not the
skeletal one -- `FUN_0020c5a8` refuses a `0x200` entity outright, so `NODRAW` in
the actor report is the correct answer for that pass and always was. That second
pass is ported now; see The billboard pass, below. `grp_017e` is a strip of
billboard sprite records rather than a mesh, and the bolt is a two-column looping
glow with a trail of shrinking copies behind it.

It also carries a light of its own: `FUN_002d2e00` allocates a `DAT_00343888`
slot through `FUN_00266050`, state 0 brightens it four a frame while the charge
grows, `FUN_002660d0` carries it along the flight, and state 4 fades it out. That
light is what the homing was originally verified against, before there was
anything to see.

`FUN_00215ac8` -- the box hit test -- and `FUN_00216078(casterType, 1, ...)`,
which fills the `+0x1AC` parameters it is the only reader of, are both ported
now; see Contact, damage and death, below. So is the hundred-particle impact
burst, and so is state 4. The lifetime is complete, and a bolt that reaches an
enemy detonates on it rather than flying on to its timeout.

One caveat that is not the projectile's fault. `integrateNonPlayerMovement` is
not `FUN_002262c0` -- see the note on it -- so where a flying actor is stopped is
approximate, and the detonation point with it.

### Checking it

`--press-magic <frames>`, alongside `--press-attack` and `--press-confirm`.
Homing needs a target inside ten units, and `s01_e024`'s spawn is about eleven
from the nearest enemy, so the plain cast flies straight -- which is correct, and
also not a test of anything. Move first:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root disc --scene s01_e024 \
    --no-audio --frames 900 --spawn -2.5,-8,0 --press-magic 60,200,340,480 \
    --actor-report --scr-report
```

`type=0x44 entities=24 ticks=5137` over four casts, `live actors: 20` -- the same
as the same run with no casts at all, which is the check that the trail is being
collected. The light report ends with the slot released at the enemy cluster
rather than at the caster, which is the homing having worked.

## Contact, damage and death

A hit is three things on three different frames, and keeping them apart is most
of what makes this readable:

1. an **effect** entity — the sword blade, the magic bolt — sweeps a volume
   against the pool and calls `FUN_00216140` for whatever it touches;
2. `FUN_00216140` works out what that contact costs and **adds it to the
   victim's `+0xBE`**. It never reads hit points and never kills anything;
3. the victim's own behaviour, next time it is ticked, drains `+0xBE` against
   its `+0x12A` and decides whether that was fatal.

`+0xBE` is a mailbox, not a subtraction. That is why a hit landed after a victim
has already run this frame still counts, and why two attackers in one frame both
land.

The attacker in step 1 is the *effect*, not the swinger — `+0x12C`, `+0x02` and
`+0x96` are all read off the blade. `FUN_00256130` copying the player's `+0x12C`
onto it at spawn is the only place the two are connected.

### The volume comes out of the model

PSC3 header `+0x30` and `+0x34` are two sections nothing else reads, and only
weapon-effect models carry them: `grp_0179` (the blade) has both, `grp_0001`
(the player) has neither. That is the early return which makes `FUN_002148a8` a
no-op for everything that is not a weapon, and it is why the parser had never
needed them.

`+0x30` is one byte per pose column — a record index, or `0xFF` for "no volume
here". The blade's is sixteen bytes and only columns 1, 3, 4, 11 and 12 are
armed, so the swing is dangerous for part of its arc and inert for the rest.
`+0x34` holds `{steps, radius, A[3], B[3]}` records: a segment in model space,
how many boxes to lay along it, and their half-extent in fortieths of a unit.

**The sweep looks forward, not back.** The interpolation factor is `+0xA4 /
+0xA6` — the countdown *still to run* over the entry's duration — so it is 1 at
the start of a timeline entry and 0 at its end, and the volume runs from the
column being entered back toward the one being left. The pair actually measured
is `[now, next frame]`: the near end is evaluated at `remaining - frameTicks`
and cached into `+0xF0`/`+0x100`, and the far end is last frame's cache.
`+0x06` bit `0x40` says the cache is valid, which is why forgetting the
already-hit set and forgetting the sweep history are the same call.

Between the two the test fills the gap: any box that moved more than four
half-extents in a frame gets extra boxes appended, interpolated corner-wise. The
cap is 31 and it **abandons the whole fill mid-segment** rather than clamping,
so a very fast swing gets fewer boxes at its far end rather than coarser ones
along its length.

### The return value is not what the decompiler says

`cStack_161` reads as a local nothing ever assigns, which is why the port used
to call `FUN_002148a8` dead and treat the bolt's hit as unreachable. It is
`sp+0x83F`, and the scratch buffer the test hands `FUN_00216140` starts at
`sp+0` — `0x002164D0` is `sb v0, 2111(s2)`, `FUN_00216140` storing to exactly
that byte. Ghidra never connects the two.

**Both hit tests return the number of contacts.** That is what plays the sword's
hit cue and what makes the magic bolt detonate on something alive instead of
flying on to its five-second timeout.

### Two seams worth knowing about

**The already-hit set does not line up with its own clear.** Both tests start a
`uint *` at `+0xD0` and step it *before* the first slot, so slot n's bit is in
word `(n >> 5) + 1` — `+0xD4` for slots 0..31, up to `+0xF0` for 224..255.
`FUN_00215e48` clears eight words from `+0xD0` down: it clears `+0xD0`, which no
slot uses, and misses `+0xF0`, which slots 224..255 do. A slot that high stays
flagged for the rest of the scene once hit. And `+0xF0` is also where the test
caches last frame's blade endpoints, so on the real machine those slots test the
float bits of a coordinate. Reproduced rather than tidied; `s01_e024` never
fills the pool past slot 29, so nothing can observe it here.

**The enemy resistance table is indexed by `type - 0x7C`, unchecked.** The type
`0x62` flyer sits below the enemy range, so its index is **-26** and the read
runs backwards out of the group into the blob's string pool. It is perfectly
deterministic — same file, same offset — and it comes out as 46 for element 0.
So the sword's `1.3x` lands on `trunc(0.46 * 1.3 * 1) = 0`, and the hit costs
anything at all only because `FUN_00216140` floors the net at one point.

That floor is confirmed from outside: `FUN_002206a8` spawns `damage * 10` hit
sparks, and the save state holds two struck flyers with ten sparks each.

### The flyers have no hit points

`+0x128` and `+0x12A` are both zero on every type `0x62` in the dump, so
`0 - damage < 1` on the first hit and **any** contact kills. `FUN_002cd0a0`'s
kill branch then puts them in state 6 on animation 4, clears `+0x0C` bit 0 so
the death starts airborne, and sets `+0x134` to `0x7C` and `+0x138` to `0xC0`.
`eeMemory.bin` catches two of them mid-fall in exactly that state, one frame
apart, both with `+0xCC` pointing at the blade.

`FUN_002cda60`, state 6, splits on the grounded flag: airborne it pitches toward
π/2 and spins about its own facing at exactly twice the pitch rate, so it rolls
through half a turn while it tips over; grounded it swaps to animation 5 and,
when that completes, assigns `+0x06 = 0x10` — an assignment, not an or — and
raises `+0x04` bit `0x800` to hand the slot to `FUN_0023a568` to fade out and
free itself.

### The lead's own numbers

`FUN_00251dc0`, which `FUN_0022a418` calls at scene init with `DAT_0058beb0`
outright, copies four fields off the party record: max hit points, hit points,
attack power and defence. Orphen's record gives 50 / 50 / 1 / 0, which is what
the dump holds. Attack power is the number `FUN_00216140` scales, so without it
every hit fell through the damage floor by accident.

### Checking it

`--actor-report` grew a `hit tests:` line — sweeps built, boxes laid down after
subdivision, contacts, and the points those contacts charged — plus the world
bounds of the last sweep, which is how you tell "the swing is too short" from
"the swing is at the wrong height".

The flyers in `s01_e024` hover about eight units above the floor the player can
actually stand on, so they cannot be walked up to in the harness. `--place-slot
<slot>,<x>,<y>,<z>` parks one pool entity at a fixed point every frame, which is
scaffolding rather than anything the original does:

```bat
orphen_port.exe --disc-root disc --scene s01_e024 --frames 400 ^
  --spawn -2.5,-8,0 --place-slot 23,-2.15,-8.0,0.3 --press-attack 60 --actor-report
```

reports `sweeps=14 boxes=38 contacts=4 damage=4`, `live actors` down from 20 to
16, and `type=0x62 state=6 -> 0x2cda60` running for 311 ticks. Swapping
`--press-attack` for `--press-magic` and parking the flyer in the bolt's path
gives one contact, one point, one death and the impact burst.

### The hit sparks

`FUN_002206a8` is a **third** particle system, sharing nothing with the other
two: a thousand entries of `0x38` bytes at `DAT_00355b74` in ten fixed groups of
a hundred, no behaviour pointer, and a quad oriented in world space. Ten hits
can be showing at once and an eleventh silently shows nothing, because a burst
takes the first group whose count is not positive and gives up if there is none.

A burst is `min(100, damage * 10)` sparks at three quarters of the victim's
height, each with a random yaw over the full circle and a place in a fan that
starts at thirty degrees and steps `(360 / count)` degrees — an **integer**
division, so a burst of seven leaves a gap rather than closing the circle.

A spark is a **streak**, not a billboard: four corners at `(±1, ±0.01)` in the
local x/z plane with **only the long axis scaled**, so it stays 0.6 units long
and 0.02 wide however far away it is. Two matrices place it, and they have to be
separate because the streak points along its own travel while the burst as a
whole is oriented off the camera: `Z(-fanAngle)` for the streak, and
`Y(yaw) → Z(-cameraYaw - pi/2) → T(spawn)` for the burst. That second Z is the
exact inverse of the view matrix's own yaw, so the fan opens across the screen.

Two things had to be read out of the disassembly rather than the decompiler.

**The matrix sequence.** `FUN_00220910` loads `vf20..23` with the identity once
and never writes them again, so every `sqc2 vf20` in the middle of the run is
*restoring the identity* to the scratchpad before the next rotation builder
writes its four entries over it, and `vcallms 0xC` accumulates into `vf28..31`
oldest first. Read as C it looks like four builders each overwriting part of a
product matrix, which would be meaningless.

**The texture slot.** `FUN_002190f8`'s last argument carries `0x22` in its low
byte, and that is the slot **itself** — texture `0x19A`, which at exactly the two
rectangles in the descriptors at `0x003159B8` holds two lens-shaped streaks,
blue for a party-side victim and gold for everything else, each the exact size
of its rectangle. Slot `0x21` has a smoke puff across both and nothing
streak-shaped anywhere on it, and the save state's own screenshot shows gold.

`FUN_0020f510` writes `slot + 1` into the same packet field for the sprite pass,
so the two producers disagree by one and only the sheets say which the consumer
honours. The same reading fixed `DAT_00355620`'s particles, which were drawing
slot `0x2A`'s flat grey noise instead of slot `0x2B`'s round spark. The port
never reads that field for sprites — they carry the slot the cache handed them
— so the disagreement only decides these two effect paths.

The port steps the pool in the draw phase, where `FUN_002192c0` runs it, rather
than beside the actor loop where `DAT_00355620` steps; the original really does
run the two in different halves of the frame. It hands the four world-space
corners to the scene's own projection instead of projecting each to integer GS
coordinates, which is the faithful analogue here: unlike the sprite pass, whose
corners are *built* in GS integer units around a truncated origin, these are
built in world space and only meet the projection at the end.

`--actor-report` grew a `hit sparks:` line — how many are alive and how many
groups are busy, which is also how many hits are currently showing.

### The sword trail

`FUN_0020e840`, the green ribbon the blade leaves as it swings. Ported into
`ported/render/original_weapon_trail.{h,cpp}`, with the model side in
`ported/model/psc3_model.{h,cpp}`. Written up in
`analyzed/weapon_motion_trail.c`.

Finding it is most of the work, because it is **not** one of the standalone
effect systems `FUN_002192c0` runs. Every one of those has a gate global, and
in the `sword_trail` save state every one of them reads zero while the trail is
plainly on screen. `FUN_0020c810` calls this last, per entity, after the bone
palette is composed and after `FUN_0020eec0` has worked out the entity's depth
bucket — a motion trail is part of a model's own draw, and its state lives in
the entity and in a static pool at `0x004FBC7C`.

The low eight bits of entity `+0xAA` — the halfword the animation stepper
stages out of the current keyframe — are an **enable mask**, one bit per
descriptor in the model's header `+0x38` table. `+0xB0` holds last frame's copy
so an edge either way is seen, and `+0xB1..+0xB8` hold one-based handles into a
pool of 32 slots. A descriptor is a colour, two vertex indices and a sample
count. `grp_0179`, the sword blade, carries two of them — a bright pale-green
ribbon over three samples and a darker one over four, both on the same pair of
blade vertices — and its **animation 0** turns both on. Animation 0 is the long
phase after the six-frame spawn flourish, the one that also runs the swept hit
test, so the trail is alive for exactly as long as the blade can hit something.

Both named vertices are skinned by their own bones, through the same vertex
stream and the same matrix buffer the mesh draw uses, and the pair goes into a
16-deep history with the newest at index 0. Each frame the newest `sampleCount`
pairs — clamped to `[3, 16]` and then to what has been recorded — become the
control points of a **natural cubic spline**, one per edge, resampled at twelve
points and stitched into eleven quads whose alpha ramps from the descriptor's
own down to a twelfth of it.

Two things are worth knowing about the solve, `FUN_00266460`. Its scratch array
at `[17]` means two different things at two different times — the forward sweep
overwrites each divided difference with the eliminated diagonal, and the back
substitution divides by *that* — and it reduces the last row twice, which is
harmless only because the term it subtracts is the coefficient the natural
boundary condition already set to zero. `FUN_00266668`'s coefficient is a third
of the textbook one and its powers are arranged to match, so the two are only
right together; both are ported as written rather than normalised.

**This is the only untextured primitive in the executable.** `FUN_0020e840`
writes `0xFFFF` into the packet's texture halfword; `FUN_00207de8` increments
that field before use, so it arrives as zero and takes the branch that emits
PRIM `0x0D` — a gouraud triangle fan with TME clear — instead of `0x1D`. That
same untextured branch halves only the packet's *alpha* on the way to the GS
and leaves the rgb alone, which is why the descriptor's rgb is free to run over
`0x80`: `grp_0179`'s first trail is 1.95x on green. `SpriteQuad` grew an
`untextured` form with a colour per corner to carry it, since the ramp is the
whole of the fade.

`--actor-report` grew a `motion trails:` line — how many pool slots are held
and the allocation mask itself, which is `0x3` mid-swing in both the save state
and the port.

### What is not ported

Called out at their sites: `FUN_00215670` (a third hit-test
form, used by enemy attacks), `FUN_002d5630` (the HP bar, gated on
`DAT_003555D3`, which is zero in both dumps), `FUN_002d59c0` (the hit cue, on
the sound engine's priority channel that the port does not reach), and the
`FUN_0025ba98` branch of the resistance lookup, which no attacker in this scene
can reach because a streamed prop's `+0x02` is not in the `0x2048` candidate
mask.

