# Physics and collision

### `+0x80` is a slope limit, and the wall test was invented

Floor panel tile `0x80` arms stream `0xd5e0`, which walks a companion in on a
spline and Orphen in with `0xF0` — **subproc 3670** at `0x93e4`:

```
0xEB(0);                                    // focus pool slot 0, the lead
if (0xF0 -0.500, 2.599) { 0x77(0,8,1); 0x3E(0x190); 0x9E(-1); }
```

He stopped dead at `(-0.50, 1.73)` and flag `0x190` never set, so record
`0xd600` blocked forever. (Subproc **5112** at `0xb9b6` is running alongside and
is unrelated — a two-line `0x53`/`0xE2` pair the flood arms, re-entered every
frame by design.)

Three things were wrong and they compound:

**1. `FUN_002262c0` has no map-wall query.** Its only geometry call is
`FUN_00227390`; the four "blocker" helpers it also calls (`FUN_00228380`,
`FUN_002285d8`, `FUN_00228838`, `FUN_00228a90`) walk `DAT_0058beb0` — the entity
pool — not the map. A move into a wall is refused because the destination's
ground scan fails, and by nothing else.

The port ran an invented swept-capsule test, `queryPsm2ActiveBlockerAlong`, over
every triangle steeper than `|nz| > 0.5` whose vertical span reached more than
5 cm above the feet. `s01_e012` has a **10 cm door sill** at `y = 1.9` —
primitive 3497, a 1.0 x 0.1 strip across the doorway, part of a collision group —
and that rejected the move permanently. A player would sidestep; a cutscene's
`0xF0` cannot. It is now deleted rather than tuned: it had no `FUN_*` behind it,
and neither did its two thresholds.

**2. Entity `+0x80` is the walkable-slope limit, not a step height.** The
original reads it in exactly one place:

```c
if (fVar18 - fVar19 < DAT_00352434) {                    // step gate
  if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80)) {  // slope gate
```

`puVar11[2]` is scan-workspace `+0x08`, which `FUN_00227390` fills from `+0x54`
on the same branch that adopts a corner's terrain flags — so it is the *winning*
corner's value — and `FUN_00227840:59` fills `+0x54` from the record's stored
angle at `+0x70 + subTriangle * 4`, defaulting to `uGpffff8504` = pi/2 when
nothing is found. That is `DRecord78::slopeAngle`, which the port already
computed and never read. Type 1's descriptor `+0x10` is **0.872665 = 50 degrees**,
the same constant `FUN_0022d258` tests the same field against.

The port called the field `maxStepHeight80`, used it as a step height, and put an
invented `0.75` in it for the lead. That is what the wall test was really
covering for: with a three-quarter-unit step allowance and no slope test, the
lead could ratchet up the ship's hull plating (which samples about 60 degrees)
and walk out of the hold — reproducibly, to `z = +0.95` at `y = 4.51`. The field
is now `slopeLimit80`, seeded from the descriptor, and the gate is ported.

**3. The step height is `DAT_00352434` = 0.26**, a global, tested *strictly*
less than. Not `+0x80`, and not 0.75.

With the slope gate in and the invented test out, every stick angle keeps the
lead on the `z = -1.50` deck instead of climbing the hull, and subproc 3670
reaches `(-0.50, 2.60)` — its authored target — at frame 13627, with the rest of
the chain (`0x193`, `0x192`) following. `--frames 4000 --actor-report
--scr-report` stays byte-identical on both scenes.

**Still not ported**, and worth naming rather than inventing around: `FUN_00228cf0`
(the dynamic-support pass `FUN_00227390` runs at `LAB_002276d8`, which can raise
the answer and set `+0x0C` bit `0x100`), and the four entity-pool blocker helpers
on the *player's* path — the actor loop has its clamp, the lead does not.

### The jump: a landing state, and the moon jump

`FUN_002534D8` is the whole airborne state, and the port had two things wrong
in it.

**The landing runs to the end of its animation.** On touchdown the state writes
animation `0x10` and stays in state 2; the exit is `+0x06` bit 0 — the timeline
reporting complete — and not the ground test. The port exited on `grounded`,
which is true the instant the animation is chosen, so the recovery lasted zero
frames and the character snapped from falling to standing. With the right test
it now holds animation `0x10` for about eight frames, which is what a landing
looks like.

The fall arm also owes two sounds the port never played: `FUN_00255D88(entity,
3)` — the same surface table the footsteps and the takeoff read, column 3 — and,
when `+0x0C` bit `0x400` says the actor came down on a liquid, character cue
`0x0D` instead, latched through `+0x1BB` bit `0x10` so the two cannot both fire.
Nothing in the port raises `0x400` yet, because that branch of `FUN_002262C0` is
unported, but it is the reason the thud is conditional.

**The moon jump is the original's, not a harness affordance.** It is four lines
at the top of the same function:

```
if (uGpffffbd54 != 0 && (uGpffffb09c & 0x80) != 0) {
    +0xA0 = 0xC;  +0x44 = DAT_0035287C;
}
```

`uGpffffbd54` is set in `FUN_00251ED8:21-23` from the attack button held, cleared
unless `cGpffffb66a` — the debug byte — is up; `uGpffffb09c` is that frame's
newly-pressed mapped word and `0x80` is jump. The remap table is the identity, so
in practice: **hold Circle, tap Square.** Each tap re-seeds the vertical velocity
to the full jump speed and puts the rise animation back. That is all of it — no
state change, no return, and no four-frame startup, so the taps compound into a
climb.

The port had this as a debug affordance that zeroed `+0x44` and armed the
startup instead, so every tap cancelled the fall and then waited four frames
before pushing: the character hovered. Measured in `s01_e024`, tapping every six
frames now climbs 0 → 1.33 → 2.69 → 3.04 and then rides the ceiling, against an
ordinary jump's 1.86 apex.

Two details that are easy to get wrong:

- **The animation is sampled before the moon jump rewrites it** (`:12`), and
  every branch tests the sample. A moon jump out of a fall still takes the
  `0x0D` arm on the frame it fires.
- **`uGpffffb09c` is that frame's pressed word, not `FUN_0023B890(8)`.** The
  eight-frame OR that `FUN_00256BB8` reads would hold the boost on for eight
  frames per tap. `FUN_00251ED8` is handed the single-frame pair
  (`uGpffffb688` / `uGpffffb09c`) and the port now passes
  `FUN_0023B890_recent(1)` alongside the eight.

`--press-jump <frames>` and `--hold-attack <first>-<last>` were added to reach
any of this headlessly; nothing in the jump, the landing or the moon jump was
testable before.

### Standing on things: the map ships the answer

An earlier pass here invented a "step-up bit" — `record78 +0x04` bit `0x100` —
to decide which of two candidate surfaces an actor stands on. **It does not
exist.** It was a rule fitted to two data points because the port had no way to
break the tie. The engine does not break ties at all, and the reason is that the
map file tells it the order outright. `analyzed/terrain_ground_query.c` is the
full reading; the short version:

**`FUN_00227840` is an ordered walk, not a search.** PSM2 header word 6 holds a
64×64 grid of `int16` cell heads and a shared run list, and `FUN_0022b5a8`
copies both out of the file computing nothing. The scan walks one cell's run in
the authored order, keeps the first front-facing hit, and stops at the first hit
at or below the head. That order *is* the tie-break. The port now loads the
section (`loadCollisionGrid`) and walks it; every scoring heuristic it used
before — nearest height, highest eligible, the step-up bit — is gone.

Verified byte for byte rather than by eye: `out/mapbin/0001.psm2` reproduces the
dump's grid 4096/4096 and its cell list 1022/1022. All 165 PSM2 maps carry the
section, so the old unordered scan survives only as a fallback for a map that
fails to parse.

Three flags on `leadingWord` drive it, all read rather than guessed: `0x800`
"participates in collision", `0x200` "flat, height is the constant at `+0x2C`"
(which is the primitive's *maximum* corner z), and `0x100` "ceiling". A ceiling
does **not** reject — on a hit it clears the have-recorded latch without writing
a height, so a later front face below it replaces whatever was latched above.
The reject mask is entity `+0x74`, per entity, not a constant.

**And the lift is a landing snap, not a material gate.** `FUN_002262c0:41-85`'s
resample block needs the cached primitive's `+0x13` to be non-zero, and that is
`0` for every primitive in s01_e012 — it never fires there. What raises `+0x28`
is `:502-520`: `pos <= +0x4C → pos = +0x4C`.

The other half of that: **a stationary non-player actor never resamples the
ground.** `FUN_00227390` is reached only from the velocity loop or from a branch
gated on `DAT_003555d0 != 0`, and that global is `0` in the dump. Such an actor
keeps the `+0x4C` its placement opcode gave it for the whole scene — `0x55`
calls the query and gets a real height, `0x54` just mirrors the authored z. So
Magnus's `0x55` writes `-1.200` (hidden flat quad #9 over the bed) while leaving
him at the authored `-1.500`, and the snap lifts him. Slots 82 and 84 have
`+0x0A = -1` and `+0x6C = 0` — never queried — and sampling them every frame is
exactly what put them at the wrong height.

**Checked against the dump, entity by entity: 80 of 82 positions match.** The
two that do not are Volcan, whom the port moves when the original does not (his
`+0x04` reads `0x21` against the dump's `0x23`), and one NPC mid-walk. Both
predate this. `s01_e024` still runs all ten chest states with zero unimplemented
opcodes, and s01_e012 stays deterministic over 20,000 frames with `0x515` at
13122.

**The collision groups, and four-corner sampling.** Two things this pass first
listed as gaps and then closed, because a gap between the port and the
decompilation is a bug, not a design choice.

`FUN_00227840` has a **second loop** over the `0x74`-stride collision groups at
`DAT_003556e0`. Those groups own a block of primitives at the top of the
record78 array that the cell grid never indexes — in s01_e012 the grid stops
near 2500 and the groups run 3131..3948 — so without the loop roughly 800
primitives did not collide at all. It needs two more map sections: header word 1
(descriptors, **file stride 24**, which is what makes `4 + count * 24` land
exactly on the next section) and header word 7 (groups). The group's XY box is
not in the file — `FUN_00208450` recomputes it per frame — but for a group that
never moves it is exactly the union of its primitives' bounds, verified **20/20**
against the dump. A group the port ever animates will need the live recompute.

`FUN_00227070` samples **four corners**, at ±`entity+0x54`, and takes the
maximum, unless `entity+0x04 & 2` selects a single point. Party members read
`0xAF` (single); props, NPCs and the player (`0x312C`) are all four-corner, so
the port's single point was wrong for nearly everything — that is why an actor
would not stand on the edge of anything. The bookkeeping is not just a max:
`+0x6C` takes the winning sample's flags and ORs in any that tie, `+0x70` takes
the AND across all four, `+0x0A` only updates when the new sample found
something, and `+0x84..+0x90` publish the four heights.

Still missing, and named: `FUN_00228cf0` (riding another entity),
`FUN_002281a0` (the dynamic `0x10000` plane resolve), and `FUN_00208450` itself.

#### The frame's order: behaviours, then groups, then physics

`FUN_002239c8:116-135` is explicit about this and the port had it wrong:

```
FUN_0025b778   script tick -- 0x55 placements, 0x7E collision-group moves
FUN_00251ed8   lead player
FUN_00239ce0   actor behaviours
FUN_00208450   collision groups          -> sets DAT_003555d0
FUN_002261e0   physics, FUN_002262c0     -> reads DAT_003555d0
```

Two separate walks of the pool, and `FUN_00208450` sits **between** them. The
port fused physics into the behaviour loop (`FUN_00239ce0_update_actors` called
`integrateNonPlayerMovement` at the bottom of each iteration) and ran the groups
afterwards. That is wrong twice over: entity A's physics ran before entity B's
behaviour, and `DAT_003555d0` was always a **frame stale**.

The stale frame is what kept Magnus on the crates. A cutscene `0x55` that drops
an actor inside scenery gets exactly one physics pass to push it out before the
vertical settle stands it on top -- so the flag has to be readable on the same
frame the script raised it, which is precisely why the original puts
`FUN_00208450` immediately in front of `FUN_002261e0`. The port now has
`FUN_002261e0_update_physics` as its own pass in that slot.

Confirmed on hardware by forcing `DAT_003555d0` to 0 through the door-close
animation: the game then leaves Magnus at `(5.546, 0.128, -0.500)` on primitive
1114 -- the port's exact output, to the last decimal.

**This moves both regression scenes, and two props get worse.** `s01_e024`'s type
`0x62` enemies shift, because all behaviours now run before any physics. In
`s01_e012`, slots 35 and 40 now settle onto a floor 0.04 and 0.25 above where
hardware has them. That is not the reorder's fault -- their `+0x4C` was already
wrong, and the reorder merely gives the landing snap a chance to act on it. The
cause is the placement-grounding rule named below: `FUN_0025e7c0:115-120` grounds
a placed record **only** when its `FUN_0025ba98` record carries `0x8000`, and the
port grounds every one. Fixing that needs the `uGpffffadf4` archive, which the
port does not load yet -- `FUN_0025ba98` fills the same 0x28-byte record as
`FUN_0025bae8` but out of a different file. Against `eeMemory.bin` at frame 1091
this is 2 position divergences in 84 slots, where before it was 0.

**A diagnostic worth keeping.** Opcode `0x55` now prints when a placement lands
inside geometry -- the query answered above the authored z -- along with
`DAT_003555d0` as the script saw it:

```
[embed] frame 90 slot 81 placed at (5.546, 0.128, -1.5) but the floor there is -0.5 -- DAT_003555d0=0
```

That one line is what turned "the push never fires" into "the flag is one frame
late", after three rounds of static reading had failed to.

#### `DAT_003555d0` is not dead, it is transient

Recorded here because it cost most of a session and the wrong conclusion was
written down twice. `FUN_002262c0:112` has a branch that lets a **stationary**
actor resample the floor, gated on `DAT_003555d0 != 0 && (entity +0x08 & 0x20)`.
Three EE dumps all read `0` there, and that was taken as proof the branch never
runs. It is a **per-frame flag**: `FUN_00208450` clears it at the top of every
frame and raises it for any collision group with a live dirty byte -- so it is up
only while a door is swinging or the sea is rolling, and a save state almost
never lands on one of those frames.

What the branch does is eject an actor from scenery it is standing inside.
Sample the four footprint corners where the actor already is; set bit `n` when
corner `n`'s ground is **above** `+0x28`; index `DAT_00318ad0`, sixteen `(x, z)`
pairs of `0` and `±0.18`:

```
+0x30 = +0x30 * 0.5 + table[mask].x
+0x34 = +0x34 * 0.5 + table[mask].z
```

It is a movement *request*, spent the same frame by the velocity section, so the
ejection still obeys walls and step height. Mask `0xF` -- every corner buried, no
"away" to push toward -- probes 0.5 back along the facing instead, rotating by
`pi/2` up to four times. The lead additionally gets the same push every 64 frames
(`DAT_003555b4 & 0x3F`) with no group needed.

**It has exactly one frame to work.** A cutscene `0x55` leaves the actor's feet
at the authored z with `+0x4C` up on the obstacle, and the vertical settle lifts
him onto it that same frame. After that the corners are level with the feet, the
mask reads 0, and he is standing on the crate for good.

That is "Magnus stands on the crates". `0x55` puts him at `(5.546, 0.128)` with a
`0.15` radius, burying corners 1 and 2 in crate primitive 1114 at `-0.5`. Mask 6,
table entry `(-0.18, 0.00)`, and hardware ends at `5.366` -- which is where his
corner clears the crate's edge by 1e-4 and the mask goes to zero. The same 1e-4
is why the overlap test below must carry no tolerance: with slack the mask never
clears and the push never stops.

Found by putting a PCSX2 write breakpoint on entity `+0x20`
(`0x0058beb0 + slot*0x1D8 + 0x20`) and reading the storing PC. Static reading had
run out three times over; the breakpoint took one hit. Reach for it earlier.

#### The overlap test has no tolerance, and inventing one moves people

`FUN_00227d28` decides whether a query point is on a primitive, and the port had
been answering that question with a winding-agnostic barycentric test carrying
`-0.0005` of slack. Both halves of that were invented.

The original is an ordered set of `FUN_00228058` edge functions with plain `<`
and `>` against zero:

- A **triangle** (corner 2 == corner 3) wants `(0,1)`, `(1,2)` and `(2,0)` all
  `>= 0`.
- A **quad** is split on the `1--3` diagonal, and `cross(v1, v3)` *selects* the
  half rather than both being tried. On or left of it, test half 0 = `(3,0,1)`;
  right of it, half 1 = `(1,2,3)`, which is also the only path that writes
  `+0xD0 = 1`.
- Every comparison **reverses** when `+0x22` is set, which `FUN_00227840` does
  for `kCeilingBit` primitives. A primitive whose authored winding disagrees
  with its ceiling bit therefore does not collide at all — that is the hardware
  behaviour, not an omission to paper over.

The slack is what put Magnus on the crates in the middle of `s01_e012`. He
stands at `(5.366, 0.128)` with a `0.15` radius, and four-corner sampling asks
at `(5.516, -0.022)` among others. Crate top 1114 is a quad whose `v1--v2` edge
runs through `x = 5.5161` at that `y`: the corner is **outside it by 1e-4 of
world space**, or `-0.00048` in barycentric units. That cleared `-0.0005` by a
hair, so the corner "hit" the crate at `-0.500`, the four-corner max took it
over the floor at `-1.500`, and `FUN_002262c0`'s landing snap stood him on top.

With the edge functions the query returns `-1.500` on primitive `1734` —
`eeMemory.bin`'s `+0x4C` and `+0x0A` for that slot, exactly. Twelve of the
sixteen entities in the `magnus_floor` save state that carry a queried `+0x0A`
now reproduce both fields; three of the other four are bandana rope nodes whose
`+0x4C` is not a ground answer (their primitive index matches), and the fourth
is the corridor chest sitting on group primitive `3292`, which carries the
`0x10000` dynamic bit and needs `FUN_002281a0`.

Both regression scenes are byte-identical over 3000 frames, so nothing in the
opening of either was leaning on the tolerance.

## A moving collision group's primitives need their plane rebuilt per query

`FUN_00228090:11` checks the dynamic bit `0x10000` **before** the flat bit
`0x200`, so a primitive owned by an animated group never answers with a
constant. It goes through `FUN_002281a0`, which rebuilds the normal from the
live vertices -- `(P1-P0) x (P2-P0)` over the same triple the static plane uses,
triangle `(0,1,2)` anchored at 1, quad half 0 `(3,0,1)` anchored at 0, half 1
`(1,2,3)` anchored at 2 -- and evaluates the plane normally. The stored plane is
stale the moment the group moves.

`heightOnPrimitive` used to return `bounds.max.z` for these, under a comment
saying the port had no moving collision. That was accurate when written and
stopped being accurate when `FUN_00208450` was ported; nothing rechecked it.
Every door primitive was handing the ground scan its bounding-box lid as if it
were floor.

s01_e012, Volcan closing the door at (5.353, -2.019), r 0.15, feet -1.5:

| corner | point | hardware | port (before) |
|---|---|---|---|
| 2 | (5.503, -1.869) | `128` (no ground) | `-1.5` |
| 3 | (5.203, -1.869) | `-1.5` | `0` |

mask 4 -> `(-0.18, -0.18)` -> 5.173 on hardware; mask 8 -> `(+0.18, -0.18)` ->
5.533 in the port. Note that `128` is the no-ground sentinel and it *sets* the
mask bit -- the test is `feet < corner`, so the push-out ejects an actor away
from a hole as readily as out of geometry.

s01_e024 is byte-identical across the fix; s01_e012 moves one line, the player's
height on primitive 3248 (`leading=0x10804`, dynamic) going from the bounding
box's -1.18997 to the plane's -1.194.

### Two instruments worth reusing

`--push-probe` logs every embedded-corner push: slot, position, feet, the four
corner heights, the per-corner primitive, the mask and the table entry. Those
are the same fields a PCSX2 **execute breakpoint at `0x002265E4`** shows, where
`$v1` is the mask, `$s1` the entity (`0x0058BEB0 + slot*0x1D8`) and `$s0` the
workspace.

Better still: a PCSX2 save state taken at that breakpoint carries the live
workspace in `Scratchpad.bin`. `DAT_70000000` (scratchpad +0) minus `0x170` is
`FUN_002261e0`'s frame; `ws[0x4a]` names the entity, `ws+0x34..0x40` are the
corner heights and `ws+0x162` is the mask. A save state at a breakpoint answers
a register question without needing the registers.

## Known open: placement records sit one ULP below the floor

`DAT_003556e8`'s records author z as `0xBFC00001` where the floor plane is
exactly `0xBFC00000` -- in the map file and in every dump. Hardware entities all
read the exact `-1.5`; the port keeps the authored value, and `feet < corner`
then reads all four corners as embedded (mask 15 on five s01_e012 NPCs at frame
1). Not yet established as a bug: `FUN_002262c0:112`'s push test runs before the
landing snap at `:502`, so hardware would see the same value if `DAT_003555d0`
were raised that early. Needs a breakpoint hit at scene load to settle.

## A ceiling-only collision group adopts the no-ground sentinel

`FUN_00227840:176`'s merge is gated on the group's hit **count** alone
(`+0x6E`), and that count is incremented for ceilings as well as floors. A group
whose only overlapping primitive is a ceiling at or below the head therefore
reaches the merge having recorded nothing, with its height still seeded to
`0x43000000` = 128 -- and adopts it, overwriting whatever loop one found and
resetting `+0x5C` to `0xFFFF`.

That is not a curiosity. It is how a doorway reads as a **hole** while the door
is swinging through it, and the embedded-corner push-out depends on it: the mask
test is `feet < corner`, so a corner that finds no ground sets its bit and the
actor is ejected out of the opening.

The port had an extra `!groupHit.has_value()` guard on that merge, which skipped
it whenever nothing had been recorded -- exactly the ceiling-only case. Removing
it is the fix.

Confirmed against `magnus_floor` and `volcan_bug`: the two door entities, slots
18 and 19 at (5.50, +/-1.20), read `+0x0A = -1` and `+0x6C = +0x70 = 0` on
hardware -- no primitive ever found. The port used to report a floor at -1.50
there and now reports none. s01_e024 is byte-identical across the change.

### The door itself was never wrong

Worth recording because two rounds were spent on it. The port's door leaf sits
at the correct pivot with radii identical to hardware's to five decimals; the
only difference was phase. `0x7D` writes an **absolute** angle, so there is no
ramp to get wrong, and the logged sequence (`--push-probe` now prints every
group rotation write) opens at 2 deg/frame to 90, jumps to 45, then closes at
0.5 deg/frame. Hardware's push fires on the 45 deg frame; the port's fired eight
frames later at 41 deg, because at 45 deg its mask was still 0 -- the missing
ceiling merge, not the animation.

## The push-out must not go through the "found" gate

`FUN_00227070` returns the **max** of the four corners, so a single corner over
a hole (128) makes the whole sample read as 128, and the runtime's
`terrainSurface` lambda turns that into `nullopt`. The push-out's mask builder
then saw no surface and produced mask 0 -- discarding four perfectly good corner
heights because one of them was the sentinel.

The original never asks that question. `FUN_002262c0:118-152` calls
**`FUN_00227390`**, whose corner array at workspace `+0x34..+0x40` is always
filled (128 for a miss), and tests each corner independently:
`entity +0x28 < corner`. `-1.5 < 128` is true, so **a no-ground corner sets its
bit** -- that is the entire point, and it is how an actor gets ejected from a
doorway.

Fixed by giving the push-out its own `FUN_00227390_corner_sample` callback: the
same scan, without the `found` gate. `terrainSurface` keeps its old contract for
every other consumer.

This was the last of three separate faults stacked on one symptom in s01_e012's
door close, all of which had to go before the next became visible:

1. dynamic primitives answered with their bounding box instead of a rebuilt
   plane (`FUN_002281a0`);
2. a ceiling-only collision group never adopted the 128 sentinel, because of an
   extra `!groupHit.has_value()` guard on `FUN_00227840`'s merge;
3. the push-out read that sentinel through the `found` gate and lost it.

### `--push-probe`

Kept, because all three faults above were invisible without it. It turns on:

- `[push]` -- **every** gated push-out evaluation, including `mask 0`. Logging
  the zero case is what found the third fault: the interesting frame was the one
  where nothing happened. It prints position, feet and `+0x4C` with raw float
  bits, the four corner heights, each corner's answering primitive index, the
  mask, and the table entry.
- `[group]` -- the collision-group table once at load (first vertex, vertex
  count, first primitive, primitive count, so it can be diffed against a dump's
  descriptors), then every rotation write as it happens.

The fields match a PCSX2 execute breakpoint at `0x002265E4`, where `$v1` is the
mask, `$s1` the entity (`0x0058BEB0 + slot*0x1D8`) and `$s0` the workspace, so
the two logs diff directly. Correlating the door's logged angle against the push
mask is what localised this; the port's door leaf matched hardware's to four
decimals at 45 degrees, so the animation was never the problem.

Both regression scenes stay byte-identical with the probe compiled in -- it is
inert unless the flag is passed.

