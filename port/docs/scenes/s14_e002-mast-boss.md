# s14_e002: the mast boss

## s14_e002: the boss *is* the scene

`s01_e013` hands off to `s14_e002`, the fight in the ship's rigging. The port
dropped Orphen into empty air and stopped dead.

Neither half of that was collision or spawn. The scene's object script is a
`work[0]` state machine in beats of ten, and beat 0 places the lead at
`(-5.2, -2.3, 2.25)` -- a spot with **no floor under it**, on hardware too: the
ground query there answers `-45`, the no-ground sentinel. Hardware does not fall
because two frames later the boss picks the player up.

The handshake is three beats:

| beat | what it does |
|---|---|
| 10 | wait until the boss's `+0x60` is non-zero -- state 0 has run |
| 20 | opcode `0xBD` method `0x6F`: request action 12 on the entity in `work[2]` |
| 30 | wait on script work word 1, which only `FUN_0029C510` writes |

Type `0x95`'s `FUN_00299390` was unported, so its `+0x60` never left 0, beat 10
never passed, and the lead fell until the sentinel caught it. Everything else --
the map, the 16 spawns, the placement table, the script -- already matched the
save state exactly.

### The intro flies the player, not the boss

`FUN_0029C198` is state 13, and it is a **camera path applied to pool slot 0**.
It takes two natural cubic splines out of `DAT_0034EB60` -- six blocks of three
points, taken in pairs, with the player's own position substituted for the first
control point -- and for 2 x 4800 ticks writes slot 0's `+0x20/+0x24/+0x28`
straight off the curve, its `+0x5C` from the tangent one frame ahead, and swaps
its animation from 12 to 13 at the halfway mark. That is the leap up the
shrouds. `FUN_0029D658` is the same move again for the fight proper, selected by
the work block's mode byte instead of by the state.

Checked against a save state of the real transition, aligned on the first frame
the curve moves the player:

|  | hardware (frame 15201) | port (frame 31) |
|---|---|---|
| slot 0 | (-5.5451, -2.5466, 6.9092) | (-5.55, -2.55, 6.93) |
| camera eye | (-5.9802, -3.9707, 8.257) | (-5.98, -3.97, 8.27) |
| camera look-at | (-5.533, -2.5389, 7.757) | (-5.53, -2.54, 7.77) |

and the curve's last control point, `(-6.175, -0.894, 12.599)`, is where slot 0
is standing when the carry ends -- grounded onto the crow's nest at 12.4, which
is what hardware reads too.

### Bit 3 of `+0x04` turns gravity off, and the lead's copy did not have it

`FUN_002262C0:99` reads the entity's `+0x04` into its workspace and gates the
whole velocity integration on bit 3:

```c
if ((*(ushort *)(puVar11 + 0x58) & 8) == 0) {   /* +0x160 is a copy of +0x04 */
  ... +0x38 += v*dt - (g*dt)*dt*0.5;  v -= g*dt;
}
```

The non-player path in `actor_frame_update.cpp` has had that gate since the
crates; the **lead player's copy never did**. Both carries raise the bit for
their whole run, so in the port the lead accrued about a third of a unit of fall
per frame under a spline that teleported it back. The position came out right
because the curve overwrote it, but the camera reads the player *before* the
curve write, so the look-at trailed 0.34 low the entire leap.

Fixing it changed nothing in `s01_e024`, `s01_e012`, `s01_e014`, `s14_e012` or
`s14_e031` over 1800 frames. In `s14_e001` it removed twelve `[player]
primitive=` lines and nothing else: the crab's own Bezier carry raises the same
bit, and the lead's ground query had been flickering between primitives
underneath it.

### State 4 had no `src/` file, and it is state 3 mirrored

Ghidra produced no function at `0x00299C98`, which is the fourth entry of
`PTR_FUN_00325E50` -- and the *first* move the rotation picks, so nothing could
be checked without it. Disassembled out of `SLUS_200.11` (capstone in MIPS64
mode; MIPS32 chokes on the EE's `daddu` and stops after one instruction), it
turns out to be `FUN_002999B0` with three differences: `+0x1C1` is 1 rather than
0, the orbit turns the other way, and the close shot comes in on camera sub-shot
1 rather than 2. Its five tuning constants at `0x003538B4` hold the same values
as state 3's at `0x0035389C`.

Three more entries are bare `jr ra` -- `0x00299868` (1), `0x0029A4E0` (7) and
`0x0029C190` (11), two instructions each. Real no-ops, not gaps.

### The move rotation, and the geometry it flies

`DAT_00325E28` is eighteen entries cycling: 4, 5, 6, 5, 3, 5, 6, 8, 6, 4, 5, 8,
5, 8, 3, 5, 6, 9. `FUN_0029C468` walks it, and it is also where "the player is
down" (state 2) and "I am dead" (state 12) override the pick.

States 3, 4 and 6 are one orbit: twenty units out, five below the water, twelve
of turn per 32000 ticks, for `0x1900` ticks -- and the radius pulls in by up to
six as the pass comes abeam, `r = 20 - 6·|cos(angle)|`. Checked both ways: the
save state at PS2 frame 15896 has the boss in state 4 at `(10.8966, 11.5569,
-5.0)`, which is radius 15.884 against the formula's 15.888, and the port's own
state-4 samples sit on the same curve to three decimals with the height at
exactly -5.00.

State 5, the dive, is seven of the eighteen: a three-point curve down one of
three lanes in `DAT_00325D38`, the player dropped on the spot that lane and
direction call for, a splash each time the body crosses the water line, and a
wake that sheds three parts when its clip ends.

**The body is nine bones, and they all get the same pair of angles.**
`FUN_0029C7A8` measures the lag between where the head is actually pointing and
where the entity says it is facing, and writes yaw × 8 and pitch × 10 into every
one of `DAT_0034EB40`'s nine. The rotation compounds down the chain, which is
what makes the body arc rather than kink. That is a different nine from the
segments': those ride `DAT_0034EB30`.

### The rest of the fight: states 8, 9, 10 and 12

All fourteen state handlers are ported now, along with the four helpers the
wrapper runs once the mode byte reaches 14. Over 30000 frames the rotation
cycles all eighteen entries of `DAT_00325E28` and wraps, and `--actor-report`
finds nothing left:

```
type=0x95 state=0 -> 0x2995e0 ticks=1      implemented
type=0x95 state=1 -> 0x299868 ticks=360    implemented
type=0x95 state=3 -> 0x2999b0 ticks=2010   implemented
type=0x95 state=4 -> 0x299c98 ticks=2010   implemented
type=0x95 state=5 -> 0x299f80 ticks=15030  implemented
type=0x95 state=6 -> 0x29a2c0 ticks=3855   implemented
type=0x95 state=8 -> 0x29a4e8 ticks=3840   implemented
type=0x95 state=9 -> 0x29a838 ticks=2592   implemented
type=0x95 state=13 -> 0x29c198 ticks=302   implemented
unimplemented state handlers: 0
```

#### `FUN_0029DED8` is what makes the fight a fight

The boss was not targetable at all before this pass. `FUN_0029DED8` has two
arms and neither registers the boss: while `+0x1B1` is clear it **empties**
`DAT_003253C0` outright, and while it is set it registers exactly one entity --
the *first body segment*, work `+0x4B0`. The other eight segments are never in
the table. `+0x1B1` is raised by the states that hold still long enough to be
hit (8 and 10) and dropped by the ones that do not, which is the whole of the
fight's "you can aim at it now". A type `0x192` cursor now appears in the pool
for 17340 of 30000 ticks.

#### One write in `FUN_0029D658` decides whether the fight finishes

State 9's phase 11 waits on the work block's carry byte going back to zero, and
the carry is `FUN_0029D658` mode 1: two 4800-tick curve legs counted on the
*player's* `+0x62`. The port had `(&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C] =
0x0B` written down as a comment rather than a call, on the grounds that nothing
outside the battle module writes that byte and the mode was unreachable anyway.

It is not cosmetic. That byte parks the player's own state machine, and the
first thing the machine does when it runs is reset `+0x62`. With it left out the
carry's timer sat at 32 or 33 for ever, the curve never advanced, and the boss
waited at phase 11 for the remaining 12000 frames of the run. The trace was

```
[mast9] phase=11 frame=7339
carryMode=1 carryPhase=1 route=2 ramp=32 frame=7339
carryMode=1 carryPhase=1 route=2 ramp=33 frame=7346   <- 7 frames, 1 tick
```

With the write in place the same run reads `carryMode=0 carryPhase=0 route=3`
one frame before phase 12.

#### Nothing picks state 10

`DAT_00325E28` tops out at 9, `FUN_0029C468` only ever overrides with 2 or 12,
and the action map handles 12, 13 and 14. The only writer of `+0x60 = 10` in
`src/` is `FUN_0029B628` itself. It is ported because it is in the table and
because `+0x1BE`, the mast-section counter it owns, is read nowhere else -- but
a fight that runs off the rotation alone never breaks a plank, and the water
line never rises.

#### Two blocks in the retail build are dead

Ported as written, marked where they sit:

- **State 12 phase 1 dereferences null.** `0x0029BF50` branches to the
  sub-timer when work `+0x48C` is non-zero; the other arm loads `+0x06` *off
  that null pointer* and gates cue `0x13D` on bit 0 of whatever the EE has at
  address 6. Phase 0 always fills `+0x48C` before handing over, so the arm is
  unreachable.
- **State 9 phase 12's `+0x1BF > 2` arm**, which stands the player at the origin
  and puts 999 in his `+0xBE`. The guard at the top of state 9 gives the turn
  back from `+0x1BF == 2`, so the counter never reaches 3 with the block in
  reach.

#### `FUN_0025D0E0` is per-frame in the original and sticky here

Both of state 9's white flashes paint the screen through `FUN_0025D0E0`. In the
original that pushes one screen-sized sprite into *this frame's* draw list, so a
caller that stops calling it stops covering the screen. The port's `ScreenFade`
holds the last value it was handed and `PortRuntime` pushes that to the renderer
every frame, so the first build of state 9 whited the screen out at phase 5 and
never took it back -- a `--screenshot` of the frames after phase 5 came out
solid 0xFFFFFF.

Anything in the port that paints the overlay directly has to release it. Both of
the boss's users now do -- phase 5 on its way to phase 6, and `FUN_0023ABD0` on
the frame it reports done.

#### The rest of the director: shots 3, 4, 6, 7, 8, 10 and 12

`FUN_00298160` has twelve numbered shots and all twelve are in now. Six of them
are more poses off the boss or the player and read like the ones already there.
Three are not:

**Shots 6 and 8 are camera paths, not poses.** Each drops the manual camera,
installs a spline through `FUN_00217E88` -- an eye curve and a one-point look-at
curve, and *no* roll/zoom curve, which is the whole difference between
`FUN_00217E88` and `FUN_00217FE8` -- and walks it on its own `DAT_0035532E`
counter. Shot 6, the transformation, authors four points in the boss's **bone 8
space**, turns them by the boss's facing less 97.93 degrees and drops them on
wherever bone 8 was when the shot started, while aiming at bone 8 live every
frame; shot 8, the beam, authors three points as a plain offset from a row of
`DAT_00325DD8` picked by the boss's `+0x1BF`, and does not rotate at all. Both
hold the curve's last point once the counter runs out, and both of those "last
points" are `DAT_00325DCC` and `DAT_00325E18` -- which are not separate
constants, they are the final entry of each curve.

`FUN_0020BAE0`, which builds shot 6's rotation, writes **four** of the sixteen
floats in `DAT_00342828` and leaves the rest alone; every other caller in the
engine hands it a matrix `FUN_0020BC38` has just made identity, so identity is
what the standing contents are. The port builds one fresh rather than relying on
that.

**Shots 4, 7 and 10 roll their own framing.** The orbit angle is
`FUN_0029CC28(0, boss)` -- which memsets its own scratch to zero and never fills
it, so it is the bearing from the **world origin**, not from anything nearby --
plus fifteen to forty-four degrees off `FUN_00216868`. Two runs of the same move
are never framed identically.

Two details are reproduced rather than corrected, and marked where they happen:
shot 5's limit tests read `DAT_00355324`, which shot 5 never writes, and shot
8's look-at adds `DAT_003556FC` twice -- once inside the anchor and once again
on the way out, which is why it sits over the creature rather than level with
it.

`DAT_0058B190` also stopped being a synonym for the eye. It is the **anchor**:
eight of the twelve shots stamp a point there on the frame their sub-shot
changes and build every later frame off it, which is what makes a shot a fixed
frame the creature flies through rather than a follow cam. Shots 1 and 2 were
writing their eye into it, which would have moved shot 4 sub 2's and shot 11's
framing under them.

Over a 30000-frame run the director reaches shots 1, 2, 5, 6, 7, 8, 9 and 11.
Shots 3, 4, 10 and 12 belong to states 10 and 12, which the move rotation never
picks -- see "Nothing picks state 10" above.

#### The strafing run fired nothing, and left a slick on the water

Reported as: the creature swims out, charges, is supposed to shoot something,
nothing happens, and it leaves an effect in the water that never goes away.
Both halves are one missing function.

State 8's animation 0xC calls `FUN_0029D168` on the release frame, which stands
up a type `0x1AE` on the creature's bone 9 and hands it attack record 0 out of
the work block. The port had that much. What it did not have is type `0x1AE`'s
own behaviour, **`FUN_002EDC40`** -- the dispatch word at `0x0031CDD4`, which is
`PTR_LAB_0031CAB0[0x1AE - 0xFC]`. There is no `src/` file for it; it was
recovered from the ELF disassembly and cross-read against Ghidra's decompile.

That function is the whole attack. It lays down a three-point Bezier per axis,
once, latched on `+0x94`:

```
p0 = where the wash stands
p2 = the player, one unit further along the bearing, at waist height
     (player +0x28 + player +0x58 * 0.5)
p1 = half way there, three units up
```

and then walks it, spending the curve through the movement request at
`+0x30/+0x34/+0x38` rather than writing the position, so the wash still
collides on the way. Every frame it hit-tests through `FUN_002EF510` with the
record it is carrying, switches to animation 2 on contact, and expires three
ways: `+0x62` past `+0x1C0`, `+0x0C & 6`, or the burst animation finishing.

Without it the entity just sat where it was stood up, for ever. Over a
12000-frame fight, before and after:

| | live `0x1AE` at the end | slots used | dispatch frames |
|---|---|---|---|
| before | **6** | 6 | 27195 |
| after | **0** | 1, reused | 606 |

Six shots go out, at frames 3105, 4264, 5021, 9782, 10941 and 11698 (cue
`0x134` marks each release). Sampled across one flight the wash leaves the
creature at `(-3.04, 20.24, 6.75)`, arcs up over the deck and comes down on
`(-6.11, -0.85, 13.02)` -- the player was standing at `(-6.10, -0.80, 12.40)`.

Two details the decompile alone would have got wrong. `FUN_0021ED50`'s ninth
float, `baseZ`, arrives **on the stack** (`float in_stack_00000000` in its own
body), so Ghidra renders the call with thirteen arguments and no Z;
`swc1 f2,0x0(sp)` at `0x002EDF80` is the wash's `+0x28`. And the two spray
constants are gp-relative: `uGpffffab54` at `0x00354AC4` is 0.001 and
`uGpffffab58` at `0x00354AC8` is 0.05, both read off hardware.

**It does not damage the player yet, and that is a different bug.** See the
next section.

#### Nothing can hit the lead, because its +0x02 is zero

Found while checking whether the wash connects. Instrumented at the closest
frame of a pass, every geometric test in `FUN_00215AC8` passes:

```
wash box   x[-6.86,-5.36]  z[-1.60,-0.10]  y[13.02,14.52]
player     centre(-6.10,-0.80) r=0.15  foot 12.40  head 13.20
           12.40 < 14.52  and  13.02 < 13.20        -- overlaps
```

and the contact is thrown out before the shape is ever measured, by
`candidateAccepted`:

```
if ((victim.descriptorFlags02 & candidateMask) == 0) return false;
```

The port's lead has `+0x02 == 0x0000`, so it fails every mask. **Hardware has
`0x0001`**, in both EE dumps of this scene. `+0x04` is wrong too: `0x3024` in
the port against `0x30A4` on hardware.

The cause is that `OriginalPlayerController::resetAt` hand-builds the lead out
of `OriginalEntity{}` defaults and sets only `typeId00 = 1`. It never seeds
`+0x02` or `+0x04` from type 1's descriptor the way
`FUN_00265E28`/`entity_pool.cpp:99` does for everything else. The radius, height
and slope limit next to it are hardcoded copies of that descriptor's values
rather than reads of it, which is the same shortcut one field further along.

The consequence is not limited to the wash: outside a battle that retypes the
lead to `0x5C` (which does `+0x02 |= 1` at `battle_party.cpp:667`), **no attack
in the game can land on the player at all.** `s14_e002` is such a scene --
hardware keeps the lead at type 1 through the whole fight.

#### The disintegration sparkle ended up on Orphen

After the fight, the victory close-up had nine white starbursts stuck to
Orphen's hands and torso -- the effect the creature throws off as it comes
apart.

The creature's death spawns nine type `0x1C5` limbs, one per segment bone, and
binds each to itself:

```
*(short *)(limb + 0x192) = (entity - 0x58beb0) / 0x1d8;   // its own pool slot
*(char  *)(limb + 0x194) = bone;
```

(The magic-number spelling Ghidra prints, `* -0x5f75270d >> 3`, is exact
division by `0x1D8`: `0xA08AD8F3` is the inverse of 59 mod 2^32, applied after
`>> 3`.)

Type `0x1C5` has **no behaviour of its own** -- `PTR_LAB_0031CAB0[0x1C5 - 0xFC]`
at `0x0031CDD4` reads `0x00239E78`, the no-op, which the same table's `0x1E3`
entry (`0x002F13D0`, the known hit-effect handler) confirms is indexed right. So
nothing retires a limb on its own timer. What retires them is the parent going
away: `FUN_00265EC0` calls `FUN_00265F70`, which walks all `0x100` slots and
re-enters `FUN_00265EC0` for every live one whose `+0x192` names the slot being
freed.

The port's death did `pool.releaseSlot(slot)` -- the bare one-slot free --
instead of `FUN_00265ec0_destroy_entity`, which has the cascade. The nine limbs
stayed behind holding `+0x192 = 25`, and the victory close-up rig (type `0x28`)
is allocated into the slot the creature just vacated. `FUN_0020C810`'s attached
branch only asks whether the parent was **drawn this frame** -- the `+0x0C` bit
`0x1000` latch -- never whether it is still the same entity, so the orphans
re-parented onto Orphen and drew off his bones.

The nine type `0xBF` body segments survive the cascade, and that is correct:
`FUN_002995E0` never writes `+0x192` on them. They are positioned outright by
`FUN_0029CCB8` every frame rather than bone-attached, which is also why they
never appeared on Orphen.

Verified by killing the creature headlessly (`--enemy-hp 25=1:2450` with
`--press-attack` on a timer): `--actor-report` at frame 4000 goes from nine live
`type=0x1c5` at `pos=(0,0,0)` to none, live actors 40 -> 31, the nine `0xBF`
segments unchanged, and the victory pose captures clean. Seven-scene guard
byte-identical.

The other nine releases in `original_mast_boss.cpp` were the same divergence --
every one of them is `FUN_00265EC0` in the original -- and are now the cascading
form too. They change nothing observable, because those entities have no
children: the death run is byte-identical with and without them. That is the
point. The port matching the original's shape is not conditional on the
difference being visible yet.

#### Hitting the creature did nothing visible or audible

Reported as "it's supposed to flash red and make a noise -- I don't think we
have either in place". Both were real, and they were two unrelated bugs that
happened to land on the same event. `FUN_00299390`, the boss wrapper, does
three things when `+0xBE` -- the damage mailbox -- comes back non-zero:

```
FUN_00295A60(0x131, entity);       // the noise
*(entity + 0x1BC) = 0xC80;         // the flash timer
FUN_002D5630(...); *(entity + 0x12A) -= *(entity + 0xBE);
```

**The noise went through the wrong wrapper.** The port called
`FUN_00267D38` here -- volume 100, real distance -- where the original calls
`FUN_00295A60`, the boss's own `-1` wrapper described in the section below.
This is not the same bug as that one: that one was about the cues being faint,
and `0x131` was not faint, it was *gone*. The creature orbits twenty-odd units
out, `FUN_00267A80`'s cutoff is fourteen, and the hit cue is emitted from the
creature. Twelve hits landed over a 6400-frame run with the sword swinging on
a timer; `--sound-report` on the old build shows all twelve as

```
frame 2608 cue 305 ... dist 21.311993 ... vol 0/0 -> waveform 0, 0 samples  out of range
```

and on the new one all twelve as `dist 0.300000 ... vol 58/58 ... played`.

**The flash was written and never read.** `+0x1BC` is only a timer;
`FUN_0029CCB8` counts it down and, while it runs, holds entity **`+0x138` at
`0x14C8`**. `+0x138` is the general additive tint, and `FUN_0020EEC0:53-61` is
the only thing that consumes it:

```
if (*(int *)(entity + 0x138) != 0)
  for (i = 0; i < 3; i++)
    ctx[0x1BC + i] = min(255, ctx[0x1BC + i] + entity[0x138 + i]);
```

`ctx+0x1BC` is the byte triple VU0 has just written from the dynamic point
lights, so the tint is added in the same place and on the same scale, then VU1
divides the lot by 128. The port had the whole simulation side right --
`fadeColor138` is written in six places, both bosses, the swarm crab, the fade
ramp and object register 0x23 -- and **the draw path never looked at the field
once**, so nothing in the game has ever flashed when hit.

`0x14C8` is R+200, G+20, B+0. The fix adds the three bytes into
`DynamicContribution::additive` before the modulator runs, and has to be able
to raise the contribution pointer on its own, because the original writes
`ctx+0x1BC` from VU0 unconditionally and then tests `+0x138` separately --
gating the tint on the scene having point lights would have dropped it.

Hardware settled the two things that could have made this wrong. Paused on a
damage frame, boss in slot 25: `+0x1B2` is 1 (the gate), `+0x1BC` is `0x0A00`
(decayed from `0xC80`), `+0x128`/`+0x12A` are 66/63, and `+0x138` is exactly
`0x000014C8`. The player in slot 0 and the body segments in slots 26+ are all
`+0x138 = 0`, which is what makes an additive read safe: zero is the resting
value, and the segments do not flash -- only the main body does.

The one value that looked alarming is `FUN_0023A568`, which ramps `+0x138` up
to `0xFFFFFF` and leaves it there. Added, that is pure white. It is *meant* to
be: the function only runs for entities carrying `+0x04` bit `0x800`, it
brightens them to white over about eight frames, then fades `+0x134` down and
releases the slot. It is a dissolve, and the port was not drawing that either.

Verified: the seven-scene guard is byte-identical, and so are fourteen
screenshots across those scenes on both builds -- nothing else in the game
currently carries a non-zero `+0x138` on screen. At `s14_e002` frame 2612, four
frames after a hit, the two builds differ by `R+181, G+74, B+0` over the
creature: red up, green up a little, blue untouched to the bit, which is the
`0x14C8` signature.

#### The creature was too quiet, and that is a real bug

`FUN_00295A60`, the wrapper every one of the boss's own cues goes through, is
`FUN_00267D88(cue, entity, -1)`. The port routed it through
`FUN_00267D38(cue, entity)` instead, which is the same call with the volume
nailed to **100**.

That is not a small difference. `FUN_00267A80` reads a negative volume as *do
not attenuate*: it puts the scale back to 100 **and** replaces the measured
distance with `fGpffff8D9C`, which is 0.3. So the cue keys on at 125 of 128
wherever the source is, and the fourteen-unit cutoff at the top of the function
-- which makes a `FUN_00267D38` cue **silent**, not quiet -- cannot fire. The
fight orbits twenty units out, so on the port half the creature's roars were
being dropped on the floor and the rest were faint.

The sound engine has had that branch since it was written; nothing in the actor
layer could reach it, because `ActorEnvironment` only carried the fixed-100
wrapper. It now carries `FUN_00267D88` as well, and three places use it: the
mast boss's cue wrapper, and the crab's `FUN_0027BBE0` hurl and `FUN_0027CFE0`
splash, both of which had the divergence written down in a comment.

The same hook fixed script opcode **0x126**. `FUN_00261330` reads an inline cue
id, an expression for the entity, and -- for 0x126 only -- a second expression
that is the **volume**, which it hands to `FUN_00267D88`. The port evaluated
that expression and threw it away, which made 0x126 identical to 0x125.

#### Orphen hopped on the spot, and it was not the boss

Reported from play: idle in the fight he lands, stands for a second or so, hops
again, and cannot cast while hopping. It is a 120 -> 108 -> 120 loop.

Battle state **120** (the idle) arms a timer and on expiry measures the
character against control block `+0x14/+0x16`, the mark `FUN_00243F80` recorded
when the battle started. Drift past `fGpffff8844` and it hands off to **108**,
the walk home, which walks back and returns to 120. Every pass this boss makes
calls `FUN_0029C538`, which teleports the player onto one of the six lane spots
-- about three tenths from where the battle recorded him -- so 120 fires, 108
starts a return walk, and the player **did not move**, so 108 "arrived" three
frames later at the same spot and 120 sent him straight back. `FUN_002462C8`
returns on current action `0x87` before it reads a button, which is why the pad
is dead for those three frames.

The reason he did not move is one line in the frame:

```
FUN_002239C8:118   FUN_00251ED8(...)    the field controller      -- or --
FUN_002239C8:121   FUN_00249610(...)    the battle module
FUN_002239C8:129   FUN_0023FD30()       -> FUN_002446E8, the path followers
FUN_002239C8:136   FUN_002261E0()       physics for ALL 0x100 slots, slot 0 too
```

**Battle replaces the controller and nothing else.** `FUN_002261E0` loops the
whole pool from `0x58BEB0`, so the lead keeps its physics, spends `+0x30/+0x34`
and follows the ground whatever is driving it. The port had slot 0's physics
only inside `OriginalPlayerController::update`, which the battle branch skips,
so in battle nothing spent the request `FUN_002446E8` had just written.
`PortRuntime::update` now runs `leadPlayer_.FUN_002261e0_step_physics` after the
path followers whenever the battle is active.

With that in, state 108 walks him from where the pass dropped him to exactly the
recorded mark and stops -- ten short corrective steps over 6000 frames, one per
pass, instead of seventy-six.

Two smaller things came out of the same read:

- `fGpffff8844` at `0x003527B4` is **0.2**, not the 0.3 the port carried.
  `FUN_0024CF20:0x24D0E4` loads it into a `c.olt.s threshold, distance`.
- `FUN_0029D658` mode 9's two missing calls went in: `FUN_00249388` points the
  player's record back at target-marker row 0, and `FUN_00245978` re-records his
  home spot at wherever the carry left him. Neither is what was causing the hop
  -- mode 9's `+0x94 == 0x0E` arm is not reached by the intro handback -- but
  both are part of the handback and both were missing.

`s14_e012` is the one regression-guard scene whose report moves, and only in the
lead's ground bookkeeping: `triangle=690 -> 691` and `terrainWord=0x0 ->
0x20000001`. The position is identical. That is the physics doing its job in a
battle where previously it did nothing at all.

#### What is still out

- `FUN_0023BBD8`, the pad rumble, everywhere. No rumble path in the port.

(Type `0x1AE`, the strafing run's wash, used to be listed here. It is ported
now -- see *The strafing run fired nothing, and left a slick on the water*
above.)

The fight's own numbers were taken out of `SLUS_200.11` rather than out of a
note: `DAT_00325CF0` (the three smash runs), `DAT_00325D80` (the three beam-head
spots), `DAT_0034EB50` (the four collision groups each mast section is made of),
`DAT_00355340`/`DAT_00355348`/`DAT_00355338` (the plank tags and group bits) and
the `gp`-relative float run at `0x003538D8..0x003539FC`. The states themselves
are verified structurally against the disassembly and by running the rotation to
a full wrap; they are **not** frame-checked against hardware, because reaching
state 9 on the PS2 means playing the fight for several minutes rather than
loading a save state.

#### The chapter ends here, and it ends three different ways

The boss dying is not the end of `s14_e002`. The scene hands off to `s14_e031`
for the Bolt of Thunder reward, `s14_e031` hands back, and then `s14_e002`'s own
script runs the chapter's exit: **two FMVs and one of three destination maps**.

The port used to stop dead at the first statement of that exit, because it is
opcode **`0x13A`** and the extended dispatch had no case for it. `FUN_00265378`
is one line -- `DAT_003555D2 = expr` -- and the byte is spent later, by
`FUN_0022A418`, which calls `FUN_002F1808` on it once the fade is down and
before the next map loads. Both halves are in now: the opcode, and the block in
`FUN_002239c8_service_scene_change` that spends it.

**The movies play, and so does the chain.** `PortRuntime::FUN_002f1808_play_movie`
hands each leg to the movie player (see "The FMVs" below), or logs it in a
headless run. It keeps `FUN_002F1808`'s do/while, because that loop is what
decides how many films a request is worth:

```
movie 2                       -> then movie 13
movie 17, flag 0x55A clear    -> then movie 1, or movie 3 if flag 0x55B is set
```

`s14_e002` asks for 17, and neither `0x55A` nor `0x55B` is set here, so the
chapter ends on **M17 then M01** -- two films, from one opcode.

**The three-way route is the script's, not the engine's.** At blob `0x0B1E`,
just after the boss goes down, `s14_e002` converts "who is with me" into a route
flag, three symmetric arms:

| condition | sets | companion | destination | spawn |
| --- | --- | --- | --- | --- |
| flag 1377 and not 1881 | 801 | Magnus | `s03_e001` | (0, -22.000, 2.000) |
| flag 1378 and not 1891 | 802 | Cleo | `s05_e051` | (4.655, 3.539, 0) |
| flag 1379 and not 1901 | 803 | Mar | -- | -- |

and then at `0x0F51`, after the `0x13A`, an if/else-if/else on the route flag:
801 goes to `s03_e001`, 802 goes to `s05_e051`, and **anything else falls
through to `s07_e011`**. Flag 803 is written and never read -- the third
companion is the default arm, not a tested one.

`s01_e013` is where the three companion flags are set, at `0x670F`, `0x6CAA` and
`0x7530`, each next to its own `0x524`/`0x525`/`0x526` join flag and its own
dialogue stream. The `188x/189x/190x` flags the exit ANDs against are read all
over `s01_e013` as well, so they are the "this one did not make it" override.

Verified against a PCSX2 save state parked at the end of the reward cutscene.
Hardware has flag 1378 set and 1891 clear, sets 802, requests movie 17 and lands
on section 5 entry 51 with `DAT_003551EC = 1` -- which is the Cleo route. All
three arms run in the port:

```
--scene s14_e002 --enemy-hp 25=0:3000 --frames 7000 --set-event-flag <flag>:0
  1377 -> [movie] M17, M01 -> [scene] s14_e002 -> s03_e001   (frame 6546)  Magnus
  1378 -> [movie] M17, M01 -> [scene] s14_e002 -> s05_e051   (frame 6546)  Cleo
  1379 -> [movie] M17, M01 -> [scene] s14_e002 -> s07_e011   (frame 6546)  Mar
```

With no flag at all it is `s07_e011`, Mar's arm, which is what a port run that
never played the ship chapter should get.

**Landing there is not the same as the scene working.** All three destinations
load their map and run their init, and all three then stop on opcodes this port
has no case for -- `s03_e001` on `0xD8` at `0x151D`, `s07_e011` on `0x124` at
`0x170E` and then `0x8A` per frame at `0x33AD`. `s05_e051` gets further and is
the one to start with. Porting chapter 2's scenes is its own job; what this
section claims is only that the hand-off picks the right one.

**All three arms are named.** Cleo came off the save state -- it has flag 1378
set, and 1378 is the `s05_e051` arm. Magnus is `s03_e001`, confirmed by the
author of this port from play; Mar is then the fall-through by elimination,
which matches `s07_e011` being the arm whose route flag (803) is written and
never tested.

`FUN_0022A418:58-63` arm movie `0x12` when the game arrives at the **title
screen** -- section 12, entry 10 -- with no movie already queued, and clear flag
`0x511`. That is ported now that something loads the title (see "Return to
Title Screen"); `DAT_003555D8` is taken as retail's `0x22`, which always passes.
The music teardown around the playback is ported with the player (below);
`FUN_00203AA0(4)` and `FUN_0022A1F8` are not. The unconditional
`DAT_003555D2 = 0` after the block is kept.

#### The FMVs

`M01.MV3`..`M19.MV3` play in a windowed run: `--movie <id>` queues one the way
the debug menu's `MOVIE No` entry does (`FUN_00269A98`: the movie, then
`s12_e099` with request `0x2001`), and `--no-movies` goes back to logging them.
A headless or `--screenshot` run never opens the player and leaves exactly the
same game state behind, so `--frames` reports do not depend on it.

| piece | original | port |
| --- | --- | --- |
| container | `FUN_002F1A70`, `FUN_002F1C98` | `ported/movie/mv3_stream.*` |
| tables, skip rule | `FUN_002F1808`, `FUN_002F2198` | `ported/movie/original_movie_session.h` |
| MPEG-2 decode + CSC | IPU + libmpeg | `harness/mpeg2_video_decoder.*` (FFmpeg) |
| session loop | `FUN_002F2198`, `FUN_002F2820` | `harness/movie_player.*` |
| stream audio | SPU2 core 0 AutoDMA at BVOL | `AudioDevice::queueMoviePcm` |

**The decoder is a dependency.** The streams are interlaced MPEG-2 Main Profile
with B-frames, and nothing of the IPU is worth porting, so CMake fetches BtbN's
prebuilt LGPL FFmpeg 8.1 on Windows (`ORPHEN_PORT_FETCH_FFMPEG`, or point
`ORPHEN_PORT_FFMPEG_ROOT` at your own tree) and copies `avcodec`, `avutil` and
`swresample` beside the exe. Without it the port still builds and logs movies.
The colour conversion is the IPU's, not FFmpeg's: BT.601 studio range with
chroma repeated over each 2x2 block, coefficients from PCSX2's reference model
-- not checked against a hardware capture.

**What is on screen** is picture rows 16..463: `FUN_002F2198`'s display is
640x448 at DBY 16 of a 480-line buffer, so hardware loses sixteen lines top and
bottom. Pacing is two vblanks a picture, and a late picture resets the clock
instead of being dropped (`FUN_002F2820`).

**Each movie sets a flag** from `DAT_00326FF8` before it plays -- 1980..1996,
the SFLG bank -- and it does so headless too. The stub did not.

**The music fades first.** `FUN_0022A418` zeroes the scene's eight requests and
calls `FUN_00206680`, which ramps every slot down at speed 12; `FUN_00206840`,
inside the session, then waits 60 vblanks past that (`DAT_003555AC` is counted
by the vblank handler `FUN_00203A08`) before replacing the slots. So a film
starts about a second after the request, on a black screen, while the music
fades. Movie volume is `DAT_00326FD0` written straight to SPU2 `BVOL`
(RSPU2DRV.IRX's command `0x1023` handler), so `0x5000` is 0.625.

**Start skips** -- Tab on the keyboard, Start on a pad (`DAT_003555F6 &
0x800`). It is not a cut: eight pictures fade to black at `alpha = counter / 2`
while the volume drops by `1 + volume / 8` a picture, and the ninth ends the
film. `FUN_002F1808` locks it for movies 10 and 15 and for the opening (0x12)
the first time it plays after power-on -- but only while the debug-active byte
`DAT_003555DA` is clear, and the port holds that byte set, so in the port every
film can be skipped. The lock is decided once per request, so the second film
of a chain inherits it.

Verified: M13 decodes to 600 pictures (ffprobe agrees) and 21.504 s of PCM;
picture 150 matches FFmpeg's own RGB decode to a mean of under one level; the
skip sequence and the locks were driven through the ported state machine. The
skip has **not** been pressed in a live window by anything but a person.

