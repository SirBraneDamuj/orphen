# s14_e001: the crab

## s14_e001 is one actor away from a battle

`s14_e001` is the giant crab: the encounter `s01_e012` hands off to. It opens
with a long animatic -- the crab wades in, throws Dortin and Volcan into the
water, takes two swipes at Orphen while he dodges, and the camera spins around
the pair before the fight starts. The port loaded the scene and then sat there.

Six things were in the way, and five of them were the script.

### The structural opcode table was read one slot early

`analyzed/structural_ops/structural_ops_dispatch_table.md` lists 0x04 as
"(inline handler)" with no address and then reads the rest of
`PTR_LAB_0031e1f8` one row early. It is a flat eleven-entry table indexed by the
raw opcode (`FUN_0025bc68:31`); 0x04 has a real entry that is simply never
reached, because block end is handled before the lookup. So the true shape is

```
0x00 0x04 0x05  no-op
0x01            conditional jump
0x02            switch
0x03 0x07 0x09  relative jump
0x06 0x08       skip the rel32
0x0A            FUN_002681c0("Debug Code:%d", iGpffffb0cc), then ++it
```

and the port had 0x06/0x08 and 0x07/0x09 exactly the wrong way round. A 0x06
read as a no-op leaves its four-byte operand standing in the statement stream,
and the next byte decodes as nonsense -- which is how the scene died on
"unimplemented opcode 0xf" at 0x15c8, four bytes into the else arm of the
if/else that ends its start entry. The doc is corrected too.

### Five opcodes

| opcode | original | what it is |
|---|---|---|
| `0x117` | `FUN_002649a8` -> `FUN_002257c0` | writes the +0x07 running flag of one of the **map's** UV animation tracks (`uGpffffb788` = `DAT_003556F8`). The crab scene stops the water with it |
| `0x47` | `FUN_0025e0e8` -> `FUN_00217d40` | move the script camera's eye, one xyz triplet over 100000.0 |
| `0x48` | `FUN_0025e170` -> `FUN_00217d10` | the same for its look-at. The whole animatic is driven by these two |
| `0x7F` / `0x80` | `FUN_00260880` | the **read** side of 0x7D/0x7E: a collision group's rotation (+0x3C) or translation (+0x48) channel, scaled back up by 100000.0 |
| `0x8F` | `LAB_002610f8` | two instructions, `jr ra; lw v0, -0x49b4(gp)` -- `DAT_003555BC`, the frame tick count |

### And method 0x6F, which is how a cutscene drives a boss

`0xBD`'s method table has three battle-entry methods the port already had (1, 2,
3) and `0x6F`, which is `FUN_00244248` -- **the same action request the battle
VM's `aiact` opcode makes**, aimed at the entity the selector picked instead of
at the party. That is how the animatic tells the crab what to do. It needs the
selected entity, so `ScriptEnvironment::FUN_00242a18_battle_method` now carries
one and the port shares a single `battleVmEnvironment()` between the per-frame
battle step and the script.

### What was left was the crab itself

(Ported since; see the next section.) With those in, the
scene script runs clean -- zero unimplemented opcodes over
12000 frames, on every entry -- and the animatic gets as far as its own logic
allows. It is a duet, and only one voice is singing:

```
slot script at 0x1fce   switch on work[0], nineteen beats ten apart
  beat   0   0x6D, take the player's controls
  beat  20   0xBD(work[2], 0x6F, 11)   crab: action 11
  beat  80   0x47 x100                 the camera walk
  beat 100   0xBD(work[2], 0x6F, 12)   crab: action 12
  beat 110   wait for work[1]          <-- parked here
  beat 140   0xBD(work[2], 0x6F, 13)   crab: action 13
  beat 150   wait for work[1] again, then kill UV tracks 4 and 7
  beat 170   wait for placements 3..8 to be gone
  beat 180   work[0] = 1000, and the per-frame entry's ladder takes over:
             0xBD method 3 at 0x16f1, method 2 at 0x1701 -- build, then start
```

`work[1]` is never written by the script. It is written by
**`FUN_0027cef8`**, which is the crab's, and reached from `FUN_0027c3e8`
(action 13's handler: swipe three times, then animation 1 and `work[1] = 1`) and
from state 13. `work[0]` itself is written by `FUN_0027b380` (state 11) as well,
and `FUN_00279298` gates its own body on `work[0] < 3000`. The animatic cannot
advance without the actor.

Type `0x7F` is `FUN_00279298` with a sixteen-entry state table at
`PTR_FUN_00325930` -- the table runs on past its end into type 0x80's states, so
only 0..15 are the crab's. The action check is `FUN_00279600`, and
`FUN_002796c8` maps the orders: 12 -> state 13, 13 -> `FUN_0027c3e8`, 14 ->
`FUN_00277d30` + `FUN_0027c7b8`, and 11 just publishes itself as the current
action. Reachable from those roots are 50 functions in the `0x27xxxx` band,
about 7900 lines of decompiled C; everything else they call is engine code the
port already has.

### Verified

Forcing the two crab-dependent waits -- `work[1]` non-zero and the placement
scan empty -- and changing nothing else, `s14_e001` builds the party and starts
the battle on frame 661, binds Orphen's shipped loadout (Hand of Pyro,
Bite of Lightning, Sword of the Fallen Devil, shield) and puts him in state
0x78. So the battle half is already there; the animatic in front of it is what
is missing. `s14_e012` still builds on frame 2, starts on 247 and binds 5 of 5
enemies. `s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report` and `--scr-report`.

## The crab, and the two things that were stopping it

With the script opcodes in, `s14_e001` still parked at its own beat 110 waiting
on script work word 1, which only the crab writes. Porting the crab found two
bugs that had nothing to do with the crab.

### The actor count was established too late

`FUN_0023fb50` ends on `FUN_0023fc08`, and `FUN_0023f318` calls `FUN_0023fb50`
as it loads the scene's encounter data. So `DAT_00354EBA`, the actor count, is
set the moment the scene loads -- long before any battle starts. The port only
recounted inside the running battle, so through the whole animatic the count sat
at zero, `FUN_00247D80` answered -1 for every id, and `FUN_00244248` dropped
every order the cutscene tried to give the crab.

The pool-reading half of `FUN_0023fc08` is inert at load -- every record has just
been given "no entity" -- so the fix is the counting half, called from
`FUN_0023fb50` where the original calls it.

### Orphen's dodge is what lets the crab swing twice

`FUN_0027c458`, the swipe, latches `DAT_0035526B` the moment the claw connects,
and its own "ask for another swipe" arm is gated on that byte being clear.
Nothing in the crab clears it. `FUN_0027d230` does -- and `FUN_0027d230` is not
the crab at all, it is **Orphen**: the wrapper runs it every frame the crab's
mode byte is non-zero, and it is the tumble the player does when a claw lands.
The claw sets 1, the tumble carries the player along a Bezier to one of three
scripted landing spots over 0x640 ticks, and on arrival it sets 9, which hands
the control block back and clears the byte to 0. Only then can the crab swing
again.

So the "Orphen jumps around dodging" half of the animatic is not an animation
the script plays. It is a state machine on one byte, shared between the boss and
the player, and the boss cannot finish its own three swipes without it.

### What is ported

Type 0x7F is `FUN_00279298` and a sixteen-entry table at `PTR_FUN_00325930`.
The table has no terminator and the next type's starts right after it, so
reading past 15 gets type 0x80's states -- the port stops at 16.

```
 0  init: stats, the three attack records into DAT_00573788, the actor bind,
    and the pair it throws (FUN_00248f18 on tags 12 and 49)
 1  idle                          2  idle hold, then pick the next move
 3  charge and slam               5  close and stamp three times
 8  walk back to (0, -5)         12  the hit reaction and the phase change
13  **the throw**                14  **the swipe**
```

plus the action check `FUN_00279600`, the action map `FUN_002796c8`, the move
rotation `FUN_0027c7b8` and its three tables, the leg thresholds
`FUN_0027ccd0`, the body sweep `FUN_0027c8a0`, the splash `FUN_0027ce48`, the
script cue `FUN_0027cef8`, and `FUN_0027d230`.

States 4, 6, 7, 9, 10, 11 and 15 are the late-fight moves and the death; the
crab only reaches them once it has taken enough damage to shed a leg, and
`--actor-report` names any it does reach. `FUN_00277d30`, the boss camera
director, is ported -- see *The boss camera, and the shot that takes the close-up
rig away* below.

### Verified

`s14_e001`, 12000 frames, no input. The crab inits, is bound 1 of 1, takes the
animatic's actions 11, 12, 13 and 14 in order, runs the throw for 872 ticks and
the swipe for 290, and **the party is built and the battle starts on frame
1863**. After that it cycles its opening rotation -- 2, 3, 8, 5 -- with no
unimplemented state handlers on either side. `s14_e012` still builds on frame 2,
starts on 247 and binds 5 of 5. `s01_e024` and `s01_e012` are byte-identical
over 3000 frames with `--actor-report` and `--scr-report`.

## The boss camera, and the shot that takes the close-up rig away

`FUN_00277d30` is the boss camera director, and leaving it out cost more than
the view. It is one function with fifteen numbered shots, a sub-shot number, a
priority and a subject entity; a call whose priority is below the shot already
running is dropped, and a negative shot number releases the whole thing. Ten of
the shots are poses computed off the player, the boss, the boss's partner or a
bone; three are splines out of `0x0034E4C0`; two do nothing but claim the
priority slot.

The first time it is called after a release it takes the camera for itself --
`FUN_00217e18(1)` drops whatever was installed and `FUN_00217D70(0,0,0, 0,0,0)`
puts a fresh manual camera at the origin, which the shot then moves the same
frame. Releasing does **not** put the field camera back; it only clears the
latch, so the pose stands until something else asks.

### The spline stride is twelve, and the copies prove it

`FUN_00217FE8(eye, rollZoomPairs, count, lookAt, lookCount)` walks its point
arrays at a stride the decompilation does not state. The three callers here
settle it: each copies a run of `.data` onto its stack in eight- and four-byte
pieces and then hands the copies over, and only a stride of **twelve** leaves
every point fully written. At a stride of sixteen, shot 9's second eye point and
shot 10's second look-at point would both read uninitialised stack.

So the three tables are plain runs of `Vec3`:

```
shot  9  0x0034E4C0  1 look-at, 2 eye, 2 (roll, zoom) pairs, over 0x1680 ticks
shot 10  0x0034E4F8  2 look-at, 3 eye, 3 pairs, over 0x12C0 ticks
shot 14  0x0034E550  2 look-at -- the first sampled live off DAT_0058BE90 --
                     3 eye, 3 pairs, over 0x12C0 ticks
```

### Shot 3 is what rolls `DAT_0035528C` over

The crab reads `DAT_0035528C` to decide which side each of its shots comes from,
and nothing in the crab writes it except its own init. Shot 3 -- the slow orbit
around the player, a twentieth of a degree a tick -- is the writer: when the
angle passes the sub-shot's limit it writes the *next* sub-shot number into that
byte, so the side flips between moves. Miss the director and the side is
whatever the last `FUN_0027C7B8` roll left it, which is only ever 1 or 2.

Two more details worth keeping. Shot 3's look-at angle is a literal zero, not a
bearing -- the register handed to cos and sin is the one the function cleared on
entry -- so the shot always looks three units along +X of the player. And shot
6's sub-shot 2 swings off the *player's* facing where sub-shot 1 swings off the
boss's; they read `+0x5C` from different entities.

### `uGpffffb6f1` and `DAT_00343880` are the screen smear

Both are already modelled: `uGpffffb6f1` is `DAT_00355661`, the smear's target
alpha, and `DAT_00343880` is the rotation the `0xC9` transform carries. The
director raises them for shots 6, 8 and 11, the crab's slam raises the alpha for
the impact, and the crab's wrapper clears the whole `0xC9` block plus the camera
roll every fight frame before `FUN_00279180` gets a say -- so a shot that wants
the smear has to re-raise it every frame, which is exactly what it does.

### The close-up rig is a cinematic prop, and only the swipe puts it away

`s14_e001` places a type `0x28` close-up mount at load, hides it, and tags it
`0x2E`; opcode `0x13F` builds its rig and the script tags the published bust and
hair slots `0x2F` and `0x30`. Those three tags are exactly what the crab's throw
looks up:

```
FUN_0027BBE0  DAT_00325900 = tag 0x2E   the mount -- stood at (1.2, -3.2, 0),
                                        unhidden, and framed by shots 12 and 13
              DAT_00325904 = tag 0x2F   +0x08 |= 0x80
              DAT_00325908 = tag 0x30   +0x08 |= 0x80, animation 1
              pool slot 0   +0x08 |= 1, +0x04 |= 1   -- the player, hidden
```

and `FUN_0027C458`, the swipe, is the only thing that gives them back:
`FUN_00265EC0` on all three, both player bits cleared, all three pointers
zeroed. The port had the tag lookups but not the pointers, so the release had
nothing to release: the mount stood in the middle of the arena for the rest of
the scene and the player never came back out of hiding. That is the "Orphen
floats in the middle of the battle" the animatic ended on.

### Verified

`s14_e001`, 2500 frames, no input. The camera now moves through the animatic --
shot 9's fixed look-at `(1.810, -7.426, 0.770)` at frame 700, shot 10 mid-spline
at zoom 3.08 at frame 900, the close-up on the mount at `(1.200, -3.188, 0.437)`
at frame 1100 -- and the rig, its three children included, stops ticking at frame
1190 instead of running to the end. The party is still built and the battle still
starts on frame 1863, `s14_e012` still builds on frame 2 and binds 5 of 5, and
`s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report` and `--scr-report`.

### The hurl's heading is three turns and a nudge, and the nudge is not ninety degrees

Shot 9 -- the one that watches the pair go into the sea -- has a **fixed**
look-at, `(1.810, -7.426, 0.770)`, and the crab is teleported to a fixed
`(1.335, -6.400, -0.800)` to throw from. Neither of those aims anything. The
only thing that decides where the pair actually flies is `+0x5C`, the facing
`FUN_0027BBE0` has accumulated by the time the claw lets go, plus the fifteen
degrees `FUN_0027CFE0` adds on top:

```
+0x5C = bearing(crab -> pair)   at state entry, turned to gradually
      - fGpffff9214             animation 2 ends: instant
      - fGpffff9224             animation 0 ends: gradual, and the turn blocks
                                the state body, so it always completes
      - fGpffff9228             animation 0x18 ends: instant
```

Three of those four words are `1.5708`. **`fGpffff9214` is `0.174533`** -- ten
degrees, the nudge the crab makes as it settles over the pair -- and the port had
it as a fourth quarter turn. Eighty degrees of error, applied to a heading, with
a camera that does not track: the pair left frame within twenty frames of the
hurl and landed somewhere off to the left, so the shot showed nothing but the
crab standing in the water for its whole hundred and eighty frames.

With the word read right the throw runs from `(1.600, -6.385, 1.055)` to
`(3.854, -10.848)`, which is `-63.2` degrees off the hurl point against shot 9's
own `-61.2`: the pair flies straight down the camera axis, away from the lens,
and the splash lands in frame at 750.

The lesson is the cheap one. Every constant in this state is a separate
gp-relative word and three neighbours being the same value is not evidence about
the fourth; all thirteen of `0x00353180..0x003531B0` are now checked against the
ELF, and the other twelve were right.

### The `[embed]` lines in this scene are the diagnostic working

`s14_e001` prints 42 of them, all for the crab, all in the first fifty frames.
They are not a defect. The scene drives a collision group with opcode `0x7D`
**every frame** -- 261 times over 2500 frames -- so `DAT_003555D0` is genuinely
live on almost every frame here, where a field scene raises it for the two frames
`FUN_00208450`'s dirty-byte handshake needs and then drops it. Meanwhile the
crab's scripted wade-in re-places it with opcode `0x55` every frame, and the
authored curve runs one to ten hundredths of a unit under a seabed that is
rising faster than it is. The message says the ejection is armed on the same
frame the placement needs it, which is the case it was written to confirm.

## The thrown pair, the water, and where the crab's noise comes from

Three things the animatic was missing, all of them hanging off the crab's
wrapper rather than off any state.

### `FUN_0027CFE0` is the only thing that takes the thrown pair away

The wrapper runs it every frame its mode byte is 12, which is the whole of the
throw. The subject is the entity at `+0x1C0`, and it does nothing until the hurl
lets go -- the gate is `+0x192` below 1 and the victim's own state non-zero, and
state 13 clears that state on entry. Then four steps counted in the *victim's*
`+0x94`:

```
0  build the arc: from where it is to five units along the crab's facing plus
   fifteen degrees. The control point is the far end on both horizontal axes,
   so it leaves fast and coasts in; only the height has a real apex, two units
   up, landing at zero. Raise +0x04 bit 3.
1  turn the victim to the crab's facing plus fifteen degrees.
2  walk the arc over 0xC80 ticks. On the last tick drop +0x04 bit 3, which
   hands it back to the physics.
3  wait for +0x0C to answer "landed", then two rings of splashes, cue 0x7C,
   and FUN_00265EC0 -- the victim is destroyed and +0x1C0 cleared.
```

Step 3 is the whole of it. Without it the thrown character floats in the water
for the rest of the scene and the water never makes a sound.

### Type 0x10D is the water, and it is thirty lines

`PTR_LAB_0031CAB0`'s entry for 0x10D is `FUN_002EB180`: shrink both scales
toward the end of the timer, die on zero, and drift outward along the heading
while `+0x60` is non-zero. `FUN_002EB278` spawns one, and it writes
`DAT_00354A3C` -- **-0.7, the water surface** -- into the height itself, taking
only x and y from the caller. That is why neither spawner has to know how deep
the thing making the splash is.

Two spawners, and between them they are most of the water in the scene:

```
FUN_002EB398(radius, scaleA, scaleB, at, count, life)   a ring, mode 0, static
FUN_002EB500(at, count)                                 a 90-degree fan behind
                                                        the facing, mode 1,
                                                        drifting outward
```

`FUN_0027CE48` calls the second every 0x140 ticks while the crab is in the water
and playing one of its six moving clips -- the spray it kicks up wading in --
and `FUN_0027CFE0` calls the first twice when the pair lands. Sixty of them now
spawn across a 2500-frame run of `s14_e001`, where before there were none.

### `FUN_0027EF40` is the crab's entire sound layer

The wrapper runs it outside the mode gate and outside the beat ceiling, so it is
the one piece of the crab that is audible from the frame the scene loads. Every
cue is a clip keyframe -- the animation says which move, the timeline cursor
says which frame of it, and a contact bit says the frame is the one the claw or
the foot actually lands on. Bit 8 is the claw, bit 4 the legs:

```
animation 8    the slam      cursor 0 / 6 / 8   0x114 / 0x115 / 0x116
animation 9    the stamp     cursor 0           0x117
animation 2    the walk      cursor 0           0x118 above -2, else 0x119
animation 3    the charge    cursor 4           0x118 above -4, else 0x119
animation 0x19 backing up    cursor 4           0x118 above -4, else 0x119
```

A 2500-frame run went from **8 sound events to 41**: seven 0x114, six each of
0x115, 0x116 and 0x117, nine 0x119, and the splash. 0x118 never fires in this
arena because the crab is never above the shallow line.

### Still out

Of what this section used to list, only two things are left:
`FUN_0027F1A8`, which animations 0, 1 and 3 also reach and which puts two puffs
on the crab's bones 2 and 3 through `FUN_0021F6E8`, and the pad rumble,
`FUN_0023BBD8`. The rest are ported now: `FUN_0027ED58` (the three debris pools,
and cue 0xC0 when the crab drops what it is holding), `FUN_0027E118` (the falling
debris, type 0x10B), `FUN_0027DC38`, `FUN_0027CF20`'s impact dust on
`FUN_0021A4F8`'s pool, and `FUN_002EAC48`, state 5's bubble rings.

## The crab's effects: a bubble, a fourth particle system, and three debris pools

### Type 0x10C is the stamp's bubbles

`PTR_LAB_0031CAB0`'s entry for 0x10C is `FUN_002EA7F0`, a two-stage rise. Stage
one runs the spawn timer down while the bubble travels along `+0x5C` and climbs
at `+0x1A0`; stage two adds five to the climb and scales the *horizontal* speed
by however much of its own timer is left, so it stalls as it surfaces. Either
stage ends on a `+0x0C` mask: `0x4006` -- a wall, a ceiling or the map --
destroys it outright, `0x60` -- another entity -- pops it.

Only the popping matters, and only once. `DAT_0035529C` latches the moment a
bubble lands `FUN_002EF510`, so a column of twenty cannot land twenty hits. And
only **state 0** can hit at all: state 1 is the same machine with the attack
dropped, which is what a mode-1 bubble is for.

`FUN_002EAC48` spawns them, and the crab's stamp (`FUN_0027A440`, animation 9)
asks for ten, fifteen or twenty depending on how many legs it has left -- and at
two legs gone it doubles the spread as well -- off bone 6, then three more of
mode 4 in a half-unit ring at 0, 120 and 240 degrees. Nineteen of them now spawn
across a 2500-frame run.

### The fourth particle system

`DAT_00355A9C` is a thousand entries of `0x28` with no behaviour pointer at all:
`FUN_0021A820` is the only thing that ever steps one. It shares nothing with
`DAT_00355620` (1536 entries, one installed behaviour) or `DAT_00355B74` (a
thousand sparks in ten groups, drawn as world-space streaks).

A puff does two things: it rises by 0.003 a tick, and if its heading is
**exactly** non-zero it slides along it by 0.01 a tick. That float test is not a
flag, so the first puff of every ring genuinely does not slide. The rest is the
fade -- `(remaining << 7) / total`, 0x80 down to 0 -- and `+0x21` picks between
a rectangle that hangs below the anchor and one centred on it.

One detail worth keeping: a puff is skipped while its projected `q` is **above**
`fGpffff839C` (0.7). That is a *near* cutoff, not a far one -- the opposite way
round from every other sprite in the frame.

Three spawners sit on top of it. `FUN_0021A4F8` is the ring and does no rolling
at all -- it only stores the two jitter ranges. `FUN_00219AF0` is what the
impact helpers call: two nested rings, rolling both jitters per outer step and
folding the first into the position, so `(5, 20)` puts a hundred ragged puffs
down. `FUN_0021A170` hands its own first roll in as both the x jitter *and* the
radius, which is how a single puff scatters instead of landing on the point.

`FUN_0027CF20` -- the claw landing -- fires two of those hundreds at once, and
that is now on screen for every swipe in the animatic: 134 puffs alive at frame
1260, 169 at 1350.

The puff's own packet is where the first version of this went wrong, and both
mistakes were in fields the arithmetic never touches. `FUN_0021A820` writes
`0x10004580` into the packet's `+0x0C`, and `FUN_00207de8`'s `& 0x1C000` ladder
tests bit `0x4000` first: **mode 1, an alpha blend.** The two neighbouring pools
write `0x10008080` and `0x10008580`, whose `0x8000` picks mode 2, and taking the
dust for additive as well turned a claw landing into a white screen. Then
`FUN_00207de8`:130-141 folds the vertex colour on the way into the GS, and for a
**textured** packet -- which this is, halfword `0x0221` -- it halves *all four*
channels, not just the alpha: `0xF0F0F0` arrives as `0x787878` and a fresh
puff's `0x80` alpha as `0x40`. Both are visible in a capture; neither shows up
in a count.

Worth flagging for the two pools that were ported before it: neither
`FUN_002D3058`'s particles nor `FUN_002190F8`'s hit sparks apply that same fold
in this port, and both submit textured packets (`0x0A2B` and `0x0022`), so both
reach the GS at twice the alpha the hardware would use. No scene the harness can
capture has either pool alive, so the correction is unverified and has not been
made.

### The three debris pools

`FUN_00279940` clears thirty entries at `DAT_005737E8`, fifty at `DAT_00573860`
and thirty at `DAT_00573928`, and the crab's wrapper walks them through
`FUN_0027ED58`. **Everything in all three is retyped to 400**, whose handler is
`FUN_00239E78`, the no-op -- that is the point: the crab steps them, so the
actor loop must not.

```
DAT_005737E8  FUN_0027E118 drops one piece of wreckage every 16000 ticks at
              x = 7, five to nine units up the arena; FUN_0027E5D8 slides it,
              and a hit turns it into cue 0xC0, a burst of chunks and two
              splash rings
DAT_00573860  the arena's own destructibles, bound at crab init by four
              FUN_002797D0 calls -- tags 20..23 the lamps (which take a light
              slot), 30..34 the crates, 35..39 the barrels, 41..42 the two that
              keep their own type -- and stepped by FUN_0027E770
DAT_00573928  the chunks a broken prop throws, from FUN_0027E370; FUN_0027EBE0
              slides each one and trails a dust puff a frame behind it
```

Sixteen type-400 entities now live in `s14_e001`: fourteen props bound at init
and two pieces of wreckage, all of them reported `implemented` where the same
slots used to read `UNIMPLEMENTED` map-streamed props.

`FUN_0027E118`'s one quirk is worth writing down rather than reproducing:
`FUN_0025BA98(0x10B, ...)` indexes the object stat groups at `type - 0x272`,
which is **negative** for 0x10B, so the original copies a byte from before the
group into `+0x12C`. There is no faithful value to port; the field keeps its
default and the read is noted where it would have been.

### Still out, and why

`FUN_0021F6E8` is a **fifth** particle system and it is two pools, not one: a
hundred emitters of `0x3C` that spawn into a thousand particles of `0x24`, and
only the second of those draws. `FUN_0021FB68` has no packet in it at all. It was
the whole subsystem or nothing, and it is ported now as the plume pool (see
[Effects](../effects.md)); `FUN_002EF688`'s kinds 2 and 3 (the crate and barrel
bursts) go through it. `FUN_0027F1A8`, the crab's bone dust on animations 0, 1
and 3, still does not: the pool is there, the call is not.

`FUN_0027DC38` is the fight's own bookkeeping -- the live-enemy count, the
target list, the music step-downs, and the write of 3000 into script work word 0
that ends the battle. It is ported; see the boss-targeting section below.

`FUN_0023BBD8` is the pad rumble. The port has no motor anywhere, and three
other places already say so.

**Type 0x7E**, the swarm the corpse lets out, is ported -- see below.

## The crab could only ever fight with a third of itself

Two separate faults, and between them they were the whole fight after the first
hit landed.

### The rotation's other two tables had no handlers

`FUN_0027C7B8` is the crab's move chooser and it walks one of three tables,
picked by how far through its damage thresholds it is:

```
DAT_00325910   2, 3, 8, 5, 2, 8, 3    opening
DAT_00325920   7, 4, 7, 4, 7          one leg gone
DAT_003552A0   9, 10, 11              out of hit points
```

Every entry of the opening table was ported. **None of the other eight were.**
A state with no handler never calls `FUN_0027C7B8` again, so the first time the
player took a threshold off the crab it moved onto a state that did nothing and
stayed there for the rest of the scene, playing the neutral clip `FUN_00225BF0`
had just set. `--actor-report` said so all along -- it lists the handlers a run
reaches -- but a headless run never damages the crab, so the middle rotation
was never entered under test.

The seven that are now here:

```
 4  FUN_00279F50  the boulder. Drop a type 0x10B into the water two units to
                  one side, walk to it, swing (ten degrees either way with all
                  legs, always left with one gone), take it on claw bone 12 or
                  18, turn, and throw. Six sub-phases on +0x1B9.
 6  FUN_0027A7E8  back off to (0, -4) without turning round
 7  FUN_0027A958  back away to one of three corners in turn -- (0, -9),
                  (3.5, -8), (-3.5, -8) -- on +0x1CE, which the hit reaction
                  clears
 9  FUN_0027ACC8  walk to (0, -6), the mark the finale starts from
10  FUN_0027AE98  the run at the shore: (0, 7) at thirty rather than fifty,
                  six camera cuts on the way in as z crosses -3, -0.8, 0.8,
                  1.2 and 4.5, DAT_0035526B driven to 3 at the start and 5 at
                  a unit's range, and the tagged-19 gate broken past 1.2
11  FUN_0027B380  the death. Four beats on +0x1A4 once the clip has run: the
                  corpse settles, the camera zoom pulls in over 0xC80 ticks,
                  the swarm is let out, and the fade to state 15 is timed on
                  +0x1CC. It is the only thing in the game that writes script
                  work word 0 itself -- 2000, from `FUN_0027B380`
15  FUN_0027BA90  the corpse: release the party's records, clear the wreck
                  pool, take a light slot and stir the swarm on a timer
```

`FUN_002EA238`, type 0x10B, came with state 4 -- five states of its own (fall,
slide, settle, sink, and a quadratic Bezier flight to half a unit above the
player) and it is the same entity `FUN_0027E118` already drops into the water as
rubble.

`FUN_002EA238`'s sibling under this is **type 0x7E**, the swarm `FUN_0027C950`
lets out of the corpse -- a second enemy with its own wrapper and its own state
table. It has its own section below.

### The 0.26 step cap was not a step cap

The other half, and it predates all of the above: the crab could not climb out
of its own arena.

`s14_e001` is a pool at -1.0 with a hard half-unit wall at z = -4.3, and two of
the crab's moves station it in that pool -- state 8 walks to (0, -5) and state 9
to (0, -6). The port's movement step refused any destination more than
`DAT_00352434` (0.26) above the actor, so once the crab was down there nothing
could bring it back up. Its charge, its stamp and its run at the shore all
played their clips against the wall without moving. **That is the crab walking
on the spot** -- and it is why the slam never arrived, because state 3 aims one
unit past the player and then never covers the distance.

That test is not what `FUN_002262C0` does with the constant:

```
lVar7 = FUN_00227390(destination);
if (+0x4C - w[6] <= +0x7C) {
  if (+0x7C < w[5] - w[6])   refuse;
  if (lVar7 != 0)            accept;      <-- no height test at all
  if ((+0x0C & 0x10000) == 0) { ...the DAT_00352434 branch... }
}
```

The 0.26 is the **fallback** taken only when the corner scan found nothing --
"is there a ledge here the scan missed" -- and both real height gates are
against `+0x7C`, the entity's own step allowance, measured against a second
workspace height the port's `TerrainSurface` does not carry. A scan that
answers is accepted however tall the step. Capping it gave every actor in the
game a quarter-unit ceiling it does not have.

The slope gate beside it (`puVar11[2] <= +0x80`) is real and stays; that is the
one that stops a follower ratcheting up the shop counter, and `s01_e012`'s
followers still sit exactly on their floors over twelve thousand frames. Both
regression scenes are byte-identical without the cap.

### Verified

`s14_e001`, sixteen thousand frames with the sword swinging on a repeating
range: `--actor-report` reaches states 0, 1, 2, 3, **4**, 5, **7**, 8, 12, 13
and 14 with none unimplemented, the hit reaction fires 225 ticks, and the crab
retreats to (3.46, -7.93) -- the middle rotation, walking. Forcing the two
damage phases directly reaches **9, 10, 11 and 15** as well, and the fight plays
through to the death and the corpse. A capture at frame 6400 has the crab out of
the pool and up against Orphen mid-attack.

`s01_e024` and `s01_e012` are byte-identical over 3000 frames with
`--actor-report --scr-report`.

## One countdown in the fight ends on zero, and it is the one that matters

`FUN_00279298`'s invulnerability timer is spelled differently from every other
timer in the crab:

```c
iVar9 = *(ushort *)(entity + 0x1CA) - DAT_003555BC;
*(short *)(entity + 0x1CA) = (short)iVar9;
if (iVar9 * 0x10000 < 1) {                 // <-- < 1, not < 0
  *(undefined2 *)(entity + 0x1CA) = 0;
  *(ushort *)(entity + 4) &= 0xFFEF;       // drop the "cannot be hit" bit
}
```

`* 0x10000 < 1` is `(short)remaining <= 0`. Everything else in the file -- and
the shared `countdown` helper the port wrote for them -- is `< 0`. The port used
the shared one here.

That is not an academic difference. The reload is `0x1900` and `frameTicks` is
32 in this scene, so 6400 / 32 = **200 exactly**: the timer steps 6368, 6336, …,
32, 0 and lands on zero every single time, never below it. Tested strictly it
never fires, `+0x04` bit 0x10 stays up for good, and `FUN_00216140`'s candidate
filter drops the crab from every hit test from then on. **One hit and the boss is
untouchable for the rest of the fight.**

It only showed up with a spell. `+0x04` bit 0x10 is raised by

```c
if (1 < (byte)(entity[0xBC] - 0x19)) { entity[4] |= 0x10; }
```

and the sword leaves `+0xBC` inside that window, so a Cross-only run keeps
connecting and looks fine -- 120 hit points down to 84 over seven thousand
frames. Hand of Pyro does not: the trace goes `f=2720 af=0080` (hit), `f=2721
af=0090 flinch=6368`, `f=2920 flinch=0 af=0090` -- the timer ran out and the bit
never came off -- and nothing lands again. With the inclusive test the same run
reads `f=2920 flinch=0 af=0080` and carries on 120, 108, 96, 84, 72, 60, 48 down
to zero.

### `FUN_00215E48` is the already-hit set, not a contact flag

Found next to it. `FUN_00215E48` zeroes the nine words from `+0xCC` to `+0xEC`
and drops `+0x06` bit 0x40; those words are the set `FUN_002148A8` and
`FUN_00215AC8` mark a victim in, so the call means "forget everything this swing
has already touched". `original_hit_test.cpp` has it under that name.

`original_crab_boss.cpp` carried a second copy that only dropped the flag, and
state 3 -- the charge and slam -- is the one caller. So the crab's slam
remembered its victim between swings and could land on Orphen exactly once in
the whole fight. Type 0x7E's leap makes the same call and now makes the same one.

### Verified

`s14_e001` with Hand of Pyro on a repeating range and no forced phases: the crab
goes from 120 hit points to 0 by frame 6055, and `--actor-report` reaches **all
sixteen** of its states plus the swarm's five, none unimplemented. `s01_e024` and
`s01_e012` byte-identical.

## Type 0x7E is the swarm, and it is a whole enemy

Nothing spawns a `0x7E` from a placement. `FUN_0027C950` -- the crab's state 11,
its death -- allocates a hundred of them around the corpse and numbers them off
in `+0x95`, and `FUN_0027BA20` (state 15) wakes one to five at a time on a
300..599 beat timer. So the swarm only exists after the boss is dead, which is
why it went unnoticed for as long as the late-fight states did.

It is built like the other two battle enemies -- wrapper `FUN_00276C30`, action
check `FUN_00276D50`, seven states at `PTR_FUN_00325868` -- and its state 0 is
`FUN_0027F978` line for line, down to the three `FUN_00216078` calls that fill
its own attack bank at `DAT_00573778`. The one thing only it does is roll a size
between 2.00 and 2.99 onto both scale axes.

```
0  FUN_00276DE0  init
1  FUN_00276F50  walk to the mark at +0x3C/+0x40 at 10..29
2  FUN_00277110  roll the next mark -- x in -11..-6, z 4.0 give or take 0.8,
                 the far end of the beach -- and walk to that
3  FUN_00277410  mill: a whole-degree heading held 50..149 beats, one chance in
                 a hundred a frame of a small hop and one in ten of those of a
                 bubble. Every time the hold runs out the *mark* creeps 0.05
                 toward negative x if the player is more than a unit away, so
                 the swarm drifts as a body rather than each one wandering off
4  FUN_00277860  the flinch, on FUN_0023A678's floored countdown
5  LAB_00277828  the death
6  FUN_002778B0  the leap
```

### State 5 is thirteen instructions and has no `src/` file

`PTR_FUN_00325868[5]` is `0x00277828`, which sits between the end of
`FUN_00277410` and the start of `FUN_00277860`. Ghidra has no function there --
`decompile_function_by_address` answers "no function found" -- so it was read out
of the executable:

```
80820094  lb   v0, 0x94(a0)
14400009  bne  v0, zero, 0x277854      -> jr ra
94830006  lhu  v1, 0x06(a0)
30620001  andi v0, v1, 1
10400005  beq  v0, zero, 0x277854      -> jr ra
34630010  ori  v1, v1, 0x10
94820004  lhu  v0, 0x04(a0)
a4830006  sh   v1, 0x06(a0)
34420800  ori  v0, v0, 0x800
a4820004  sh   v0, 0x04(a0)
```

Wait for the clip, then raise `+0x06` bit 0x10 and `+0x04` bit **0x800** -- which
is the flag `FUN_00239CE0` reads *instead of* the type handler, so from the next
frame on the corpse belongs to `FUN_0023A568`'s fade path and this state never
runs again. That is the whole death: no burst, no cue, no record release.

### The leap is a Bezier with the pitch swept through it

State 6 is three phases on `+0x94`. Phase 0 closes to a unit and a half short of
the player at thirty -- and the stopping point is measured from the *player* back
toward the crab, so it stops the same distance out however it came in. Phase 1
keys cue `0x11C`, drops the `0x00FA98AE` glow it has been wearing since the state
began, raises `+0x04` bit 3 and pitches nose-down a quarter turn, then builds
three control points: where it stands, a point 2.5 above the victim, and 0.3
*past* him. Phase 2 walks that arc over `0xA00` ticks against a 2560 divisor,
sweeping `+0x154` a quarter turn back the other way and sweeping its own body box
(`FUN_00277CA0` -> `FUN_00215AC8`, attack record 0) every frame.

A wall mid-flight -- `+0x0C` bit 1 -- resets the arc timer to zero rather than
ending the leap, so it simply tries again from wherever it stopped.

### The action check only understands two orders

`FUN_00276D50` clears `+0x154`, writes 1 into the record's `+0x2C` and drops
`+0x04` bit 3 for any pending byte at all, then: **10** means stop, and it is the
only one that returns "handled" *and* leaves the pending byte standing so it
fires again next frame; **11** means hold, without suppressing the state;
everything else is swallowed and cleared. The battle AI VM is what would send
those (it is ported now; see [Battle: enemies](../battle/enemies.md)), but the
swarm never needed it, because nothing in the crab's death path issues an
order.

### Verified

`s14_e001`, sixteen thousand frames with both damage phases forced: a hundred
`0x7E` allocate and every one survives the run, `--actor-report` reaches their
states 0, 1, 2, 3 and 6 with none unimplemented, and the hit-test counter goes
from four sweeps to 37893 -- the leaps landing on Orphen, four contacts and 48
damage. All hundred sit on their own floors at the end. States 4 and 5 need
something to damage *them*, which nothing headless does.

## 0xAF and 0xAE are entity types, not sound cues

`FUN_0027CCD0` is the crab's damage staging: at six tenths and again at three
tenths of maximum hit points it sheds a claw, and the stage it raises is also
what moves the fight onto its next move rotation. Four lines each time:

```c
FUN_0020D8C0(crab, 0x0B, zeroed, 0);        // two bones of the claw
FUN_0020D8C0(crab, 0x0C, zeroed, 0);
FUN_0020DC88(crab, 0, zero, &at);           // bone 0 in world space
FUN_002EB7F0(crab, 0xAF, &at);              // and the claw itself
```

The port had `0xAF` and `0xAE` down as `FUN_00267D38` sound cues and left the
bone half with a note saying the override table was not modelled here. Both
halves were wrong. `FUN_002EB7F0`'s second argument goes straight to
`FUN_00265E28`: **they are entity type ids**, and the two claws are real
entities that fly off, land and fade -- `FUN_002EB680` is their behaviour, two
beats kept apart by `+0xA0`. And the pose handed to `FUN_0020D8C0` is zeroed,
which is not an identity: field 3 of the seven is the scale, so the override
collapses the bone and everything under it to a point. That is how the claw
stops being drawn, and the port has had `FUN_0020d8c0_set_bone_override` since
the close-up rig went in.

So the crab kept both claws for the whole fight and made a noise it should not
have made twice.

### Verified

`s14_e001` with Hand of Pyro on a repeating range: the first claw goes at 60 hit
points of 120 and the second at 24, one type 0xAF and one type 0xAE spawn and
each runs 325 frames before freeing its slot, and the crab's silhouette at frame
5200 has neither pincer where at 3100 it has both. `s01_e024` and `s01_e012`
byte-identical.

## The wall came down in silence: three dust opcodes the script layer threw away

s14_e001 opens with the crab coming through the pier wall, and in the port that
happened with no dust at all. The crab's own impacts were fine -- `FUN_0027CF20`
is ported and its two hundred-puff rings show up on every swipe -- because the
burst at the wall is not the crab's. It is the **scene script's**, and the
handler for it read its operands and dropped them:

```cpp
case 0x10B:
  note(OpcodeSupport::OperandsOnly);
  return consumeOnly(opcode, 10);      // "a graphics submitter"
```

`FUN_00262780` is opcode 0x10B and it calls `FUN_002198A0`, which is the same
nested-ring spawner as `FUN_00219AF0` -- the one the crab uses -- with the colour
and the shape forced to zero and `fGpffff8384` for the turn instead of
`fGpffff8388`. Its two siblings are the same story: 0x10A (`FUN_00262690` ->
`FUN_00219FC8`) is `FUN_0021A170` with the same two zeroed, and 0x10C
(`FUN_00262898` -> `FUN_00219D60`) is 0x10B's burst with the eleventh operand
handed through as the puff colour. All three were `OperandsOnly`. s14_e001 fires
six 0x10Bs, the first at blob `0x2253` on frame 65.

The trap in these three is the operand order. Ghidra spells the calls with the
arguments already shuffled, and the stack slots make it look like seven scaled
coordinates followed by the raw parameters. They are read in declaration order,
and the **fifth expression is the life spread** -- a raw halfword -- sitting
between the size and the two jitters:

| # | 0x10A | 0x10B | 0x10C |
|---|---|---|---|
| 1-4 | x, y, z, size | x, y, z, size | x, y, z, size |
| 5 | life spread (raw) | life spread (raw) | life spread (raw) |
| 6-7 | jitter x, jitter y | jitter x, jitter y | jitter x, jitter y |
| 8 | count (raw) | radius | radius |
| 9 | -- | outer count (raw) | outer count (raw) |
| 10 | -- | inner count (raw) | inner count (raw) |
| 11 | -- | -- | colour (raw) |

`DAT_00352C70`, `DAT_00352C74` and `fGpffff8D08` are all 100000, the same scale
as every other position operand, and it applies to the first four and to the
jitters and radius only.

One thing fell out of reading the pool again: all four full-turn words the ring
steppers use -- `fGpffff8384`, `fGpffff8388`, `fGpffff838c` and `DAT_00352300` --
hold `0x40C90FD8`, which is 6.283184 and not `float(2*pi)` (`0x40C90FDB`). The
port had the mathematical constant in all four, which walks the outer ring by a
very slightly different step. Fixed with the opcodes.

## The crab fights in the water, and two things were keeping it on the pier

A PCSX2 save state taken as the battle opens has the crab at **(0.00, -3.18,
-1.00)** with `+0x4C` = -1.00, all four corner heights -1.000 and `+0x0A` =
0x4289 -- primitive 649, the pool floor. The port had it at -0.50, up on the
planks beside the player, stepping on and off them for the rest of the fight.
Two separate causes, one of them a correction this port had made to itself.

### `FUN_0022DC68` is not `FUN_0022DBC8`, and the crab calls it

The arena is a pool floor at -1.0 (primitives 649 and 680, `x` -5..5, `y` -5..0)
with three planked sections laid over it at -0.5, tagged 1, 2 and 4 in record78
`+0x04`. The crab smashes one per swipe, and the swipe's own tail says so:

```c
FUN_0022DC68(1 << (DAT_0035526A - 1 & 0x1f), 0, 0x800);
FUN_00225BC8(crab, 0);
```

`FUN_0022DC68(selector, enable, bits)` walks the primitive array and sets or
clears `bits` in **record78 +0x00** for every primitive whose `+0x04` intersects
the selector. 0x800 is the bit both loops of `FUN_00227840` require before a
primitive is offered to the overlap test at all, so clearing it takes the
surface out of the ground scan. The save state shows the result plainly: those
three primitives read `+0x00` = 0x220 while their draw-side copy at record80
`+0x70` still holds the file's 0xA20.

The port had the whole mechanism -- opcode 0xA6 has used it since the doors went
in -- but the crab's call site was a comment saying it would be routed "once the
actor environment carries it", and the actor environment never did. `ActorEnvironment`
now carries `FUN_0022dc68_enable_map_terrain`, and it is a `PortRuntime` member
rather than a second lambda body, because the two callers must not drift.

It is worth being explicit about the pair, because they select on the same word
and differ only in what they write:

| | selects on | writes | effect |
|---|---|---|---|
| `FUN_0022DBC8` | record78 +0x04 | record80 +0x70 bit 0x20 | hide from the draw |
| `FUN_0022DC68` | record78 +0x04 | record78 +0x00 bit 0x800 | stop being ground |

### A non-player actor may not step up more than 0.26 either

With the planks gone the crab still climbed them, because the port's actor
physics had no step limit at all. The predicate was `slopeAngle <= +0x80` and
nothing else, on a note arguing that `FUN_002262C0` accepts any step the corner
scan answers. That reading is wrong, and the mistake is in one variable:

```c
lVar7 = FUN_00227390(dest);
if (+0x4C - w[6] <= +0x7C) {
  if (+0x7C < w[5] - w[6]) refuse;
  if (lVar7 != 0) accept;                 // <-- not "the scan answered"
  if ((+0x0C & 0x10000) == 0) { ...the DAT_00352434 branch... }
}
```

`lVar7` is `FUN_00227390`'s return, and that function ends

```c
uVar1 = 0;
if (fVar6 <= *(float *)(entity + 0x28)) { uVar1 = 1; ...required mask... }
```

-- **1 only when the surface it found is at or below the feet**. Anything higher
never reaches the accept path; the 0.26 `DAT_00352434` branch is the only way up,
and it additionally wants `+0x28 == +0x50` (settled on the ground) and the slope
gate. That is the same rule the lead has carried as `canStepToHeight` all along.
The two `+0x7C` gates either side of it are inert here: **every entity in the
s14_e001 dump reads +0x7C = 100.0**, crab, lead and props alike, so neither can
fire, and the workspace's `w[6]` -- the lowest of the four corners -- is not
carried yet. They are noted in the code rather than guessed at.

### Verified

`s14_e001`: the crab's walk-in now ends at (0.51, -3.53, **-1.00**) facing
1.571, against the save state's (0.00, -3.18, -1.00) facing 1.5708, and it stays
at -1.00 for the whole fight instead of bobbing between -1.00 and -0.50. The
player's own ground primitive moves from 370 (`lead` 0xA20, a plank the crab has
already broken) to 459 (0xA04), the same class of primitive as the save state's
483. `s01_e024` and `s01_e012` byte-identical over 3000 frames.

### The endpoint is emergent, and the save state is a retry

The walk-in ends half a unit short and half a unit right of the save state's
crab. Chasing it settled what the number is made of, and the first guess -- "a
different scene-entry position" -- was wrong.

`FUN_0027C458` runs **three** times, once per swipe of `FUN_0027C3E8`'s ladder,
and each pass aims 1.5 units along the bearing to wherever the player is
standing *that frame*. The player is not wandering: `DAT_00325888` is a
four-entry table of the spots the crab's swipe throws him to --

```
(0.000, 0.000)  (-1.552, -3.087)  (0.108, -2.068)  (-0.082, -0.254)
```

-- and the port's three aims read exactly (1.200, -3.200), (-1.552, -3.087) and
(0.108, -2.068), ending with the player parked on entry 3. So the endpoint is
the accumulation of three aimed walks, not an authored spot: the decompressed
script contains no -3.18 in any width, and the only placement of the crab after
the opening is the `0x55` at (0, -3.5, 0) that both builds run.

The walks themselves check out. `FUN_0023A6D0`'s second argument is pool slot 0,
not the crab, so the hold is the *player's* distance to the aim point rather
than the crab's own step -- and the port's first walk travels 1.703 units where
that distance is 1.703, which is the odd semantics reproducing exactly.

**The save state is a retry, which is why its crab cannot be matched frame for
frame.** Four things say so at once, none of which can hold in a first run:

- the three pier planks are already non-solid, at a beat *before* the crab has
  swiped;
- `DAT_0035526A`, the swipe counter, is 0 -- only the crab's state 0
  (`FUN_00279940`) clears it and only `FUN_0027C3E8` raises it;
- control block 0 is fully populated, target 65, with a stale pending action
  0x8C, while `sGpffffb052` is 0 and `DAT_00354EBC` is 0 -- a party built and
  then torn down;
- the crab sits at x = **exactly** 0.0 having moved north from the (0, -3.5)
  placement, which only happens if it never turned off the heading that
  placement left it on.

Reproducing that state in the port -- planks cleared from frame 0 -- puts the
crab at (0.00, -3.50, -1.00) facing 1.5708 at the same beat, which is the same
spot and the same facing to two decimals.

### Retracted: the trig is not the difference

An earlier pass here claimed the save state proved the original's trig differs
from libm. It does not. The crab holds `+0x5C` = `+0x19C` = **0x3FC90FA6**, and
that value is exactly `float(1.57079)` -- and exactly `157079 / 100000`, an
authored five-decimal angle at the script's own coordinate scale. `float(pi/2)`
is `0x3FC90FDB`, 53 ULP away, which is a hundred times further than any two
implementations of `atan2f` can be from each other. The port produces
`0x3FC90FA6` too, and carries it through the whole opening walk.

`FUN_00305130`, `FUN_00305218` and `FUN_00305408` are, on inspection, plain
fdlibm: `|x| < 0x3f490fd9` into `__kernel_cosf` / `__kernel_sinf`, otherwise
`__ieee754_rem_pio2f` and a quadrant switch, and `FUN_00305408` is only the
`matherr` wrapper around `__ieee754_atan2f` -- the string "atan2f" is right
there in it. That is the same algorithm family the C runtime already runs, so
`std::cos` / `std::sin` / `std::atan2` agree with them to about a ULP and
lifting them out of `src/` would buy nothing.

The one systematic float difference on this hardware is the **EE FPU**: no
denormals, no infinities or NaNs, flush-to-zero, and non-IEEE rounding on the
multiply-add. It touches every float operation in the game rather than three
functions, and porting fdlibm would not move it.

What is still worth a look is smaller and local: the crab enters the animation-8
spin holding the authored 1.57079 and comes out of it on `std::` pi/2, because
the spin writes quarter-turns built from the mathematical constant. The same
class of thing is real elsewhere -- every full-turn word the dust rings step by
(`fGpffff8384`/`8388`/`838c`, `DAT_00352300`) holds `0x40C90FD8`, 6.283184, and
the port had `float(2*pi)` = `0x40C90FDB` in all four.

## A boss fight targets from a different table, and the port had never built it

`FUN_002462C8` has two targeting modes and picks between them on one word:

```c
if (DAT_00354EC0 == 0) {          // the field encounter
  ...
  DAT_00354E96 = 0xF00;           // arm the display: FUN_0023C340 freezes the
  ...                             // field for 120 frames while you pick
} else {                          // a boss
  DAT_00354E96 = 0;               // never arms; nothing freezes
  FUN_00248108();
  target = FUN_002481F0(target, direction, halfWrap);
  FUN_00249388(control, 0x4000, target);
}
```

The second arm is what a boss encounter feels like: **the target changes on the
frame the direction is read**, with no pause and no pentagon, because every
targetable thing is already wearing its own type 0x192 cursor. Ghidra reports
one write to `DAT_00354EC0` in the whole executable and it is the zero in
`FUN_0023F288`, which is why this looked like dead code -- but a raw scan of
`SLUS_200.11` for gp-relative stores finds a second at `0x00247F18`, a
two-instruction setter Ghidra had not attached to any caller list:

```
00247f18  af84af50   sw a0, -0x50b0(gp)      // DAT_00354EC0 = table
00247f1c  a785af54   sh a1, -0x50ac(gp)      // DAT_00354EC4 = count
```

Ten of its callers sit in the `0x26Cxxx` block, and they are **scene modules**.
`FUN_0022A360` reads the loaded scene's descriptor -- SCR.BIN resource 1, header
word 7, sixteen per-section lists of eight-halfword records -- and takes `+0x02`
as an index into `PTR_LAB_003252B8`, twenty-eight per-scene hooks called with a
mode number at ten points in the frame. Ten of those hooks open with

```c
FUN_00267E78(0x3253C0, 400);      // twenty rows of 0x14, cleared
FUN_00247F18(0x3253C0, 0x14);
```

s14_e001 is module **10**, `FUN_0026C0F0`. The port had no module dispatch at
all, so the crab fight ran the field encounter's paused display over an actor
table that holds exactly one record -- the crab -- and the moment the crab died
there was nothing left in the game the player could aim at.

### The marker table is a registry of live cursors

`DAT_003253C0`, twenty rows of `{s16 kind, s16 slot, s16 cursor, f32 x, f32 y,
f32 z}`. `FUN_00247F28` stamps a row, raises the entity's `+0x96` bit 0 and --
for kind 2 -- spawns its 0x192 cursor from `FUN_002D86B0` with the row's own
offset, leaving the row as kind 3. `FUN_00248040` is the reverse and is called
from three places that all matter here: the swarm crab's own wrapper when one
walks off the map, `FUN_0027D860` when the fight ends, and `FUN_0027DA48` before
it re-aims.

### `FUN_0027DC38` is the fight's bookkeeping, and it is what fills the table

Called once a frame from `FUN_00279298`, between the camera and the wreckage
drop. It was the one helper of the crab's six that was still out; without it the
marker table would have been installed and left empty. The crab's `+0x08` bit 0
splits it:

- **clear** -- the crab is alive. Mark the crab itself, and any wreckage still
  carrying `+0x0C` bit 0x1000.
- **set** -- the corpse. The swarm owns the fight: prune the hundred-row list at
  `iGpffffbe04`, rebuild it out of the pool when anything has gone, and every
  sixteen thousand ticks -- or the moment the list changes -- run `FUN_0027DA48`,
  which measures each crab against the player, sorts the list by that distance
  and gives **the nearest one** the only cursor the swarm gets. So a hundred
  crabs are one rolling target. The arena props tagged `+0x1D4` bit 0x10 keep
  theirs alongside it, and when the last crab dies the music stops, script work
  word 0 takes 3000 and every row is released.

### Nine state handlers were returning the charge instead of spending it

Found on the way, and the reason **no spell could be cast for the rest of the
scene** once the crab was dead. `FUN_00249610` writes each handler's return
value straight back into entity `+0x62` and then

```c
if (*(short *)(entity + 0x62) != 0) { control->flags38 |= 1; }
```

and bit 0 is what `FUN_002462C8` tests before it will accept a button. Every
class-1 handler returns an explicit 0 or 1 -- states 101, 103, 104, 116, 118 and
119 are two instructions, `jr ra; move v0, zero` -- and the port was handing the
incoming value back from nine of them, 120 included.

That is harmless until something else writes `+0x62`. The crab's dodge does:
`DAT_0058BF12` in `FUN_0027D230` **is** the player's `+0x62`, used as the spline
timer for the two runs the dodge makes, and it is left sitting at 0x1900 when
the run ends. State 120 then handed 6400 back every frame, bit 0 never came down
and the pad was dead from the dodge onward. The three returns in `FUN_0024CF20`
are 0, 0 and 1.

### `+0x0C` bit 0x1000 is "I was drawn this frame", and the model path never set it

The marker table went in and still no cursor appeared. `FUN_0027DC38`'s prune
drops the row of anything that has stopped being drawn:

```c
else if (((&DAT_0058bebc)[iVar4 * 0x76] & 0x1000) == 0) goto LAB_0027dcd4;  // FUN_00248040
```

The port cleared that bit and never set it back, so the crab was pruned and
re-marked on every single frame -- its 0x192 destroyed and respawned before the
spawn-in clip could finish, which is exactly "no cursor at all". The clear was
ported (`LAB_0020C73C`, `+0x0C &= 0xFFFFCFFF`); the set was not:

```
0020cb18   *(uint *)(entity + 0xc) = *(uint *)(entity + 0xc) | 0x3000;
```

It sits inside `FUN_0020C810`, past every cull -- the near-plane and
distance-fade exits all leave through `LAB_0020C9AC`, which writes `+0xB0 = 0`
and `+0x08 |= 0x10` and returns. The sprite pass has its own copy at
`FUN_0020F510:177` and that one **was** ported, which is why the type 0x68
health bar behaved and nothing modelled did.

Three things read the bit, and all three were reading a permanent zero:

- `FUN_0025A298` -- a party follower is teleported up the lead's trail when it
  is off camera **and** was not drawn. The port teleported on the second test
  alone.
- `FUN_0020C810:86` -- an attached child takes its parent's transform only if
  the parent was drawn.
- `FUN_0027DC38` -- the above.

Not modelled: the culls `FUN_0020C810` takes before line 219, so an entity the
original would have dropped still latches in the port.

### Verified

`s14_e001`: the crab wears a cursor from the opening of the fight and it is the
selected one -- animation 11, the pi/4 bracket -- with the standing wreckage
carrying unselected ones beside it; `FUN_002057C8(0xC9)` fires on the frame the
selection lands (cue 201, frame 1906 of a cold run) and again on every change.
From the corpse on, the target moves between swarm crabs as `FUN_0027DA48`
re-picks the nearest; Triangle charges and releases after the dodge where it
used to do nothing. `--battle-report` names the scene module and whether its
marker table went in. `s01_e024` and `s01_e012` byte-identical over 3000 frames
with the draw latch in.

Still out: the per-frame (mode 4) hooks of most other modules. Modules 10, 18,
31 and 32 have theirs (`PortRuntime`'s scene-module dispatch); eighteen of the
rest belong to field scenes, and `s01_e024` is module 13. The two music
step-downs `FUN_0027DC38` makes as the swarm thins, at seventy and thirty crabs
left, are ported.

## The handback after the fight is three writes, not one

`FUN_0027D230`'s `DAT_0035526B == 9` branch -- the frame the crab's finale gives
the player back -- is four statements, and the port had transcribed one of them:

```c
(&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C] = 6;                    // pending action
FUN_00249388((&DAT_0031D7B8)[(DAT_00354EBE - 1) * 0xF], 0x4000, DAT_003253C2);
FUN_00245978((&DAT_0031D7B8)[(DAT_00354EBE - 1) * 0xF],
             &DAT_0031D774 + DAT_00354EBE * 0x3C);
```

`FUN_00249388` with bit 0x4000 writes the control block's `+0x2C`, the target
index; `DAT_003253C2` is party record 0's pool slot, and `FUN_00249610`'s target
block takes its no-target branch on anything below 3. Harmless either way.

**`FUN_00245978` is not harmless.** It re-records where the member is standing
into control `+0x14/+0x16/+0x18` and `+0x26/+0x28/+0x2A` -- the same call
`FUN_00243F80` makes when the battle starts. State 120, the battle idle, arms a
timer and when it runs out measures the character against `+0x14/+0x16`; more
than three tenths of drift and it goes to state 108, the walk home. The two runs
this dodge makes put Orphen fifteen units from where the battle recorded him, at
the far end of the beach behind the arena. So he arrived, state 120 armed its
timer, state 108 took over, could not walk him back through the map, handed back
to 120, and the pair bounced for ever:

```
f=7457 st=120 anim=2    f=7463 st=108 anim=12   f=7512 st=108 anim=13
f=7562 st=120 anim=16   f=7571 st=120 anim=2    f=7623 st=108 anim=12   ...
```

on the spot, at (-15.00, 4.00), for the rest of the scene. That is the "jumping
in place". With the re-record he arrives and stands: the trace ends at f=7457
and nothing moves again.

### And the swarm was spawned where its marks belong

`FUN_0027C950`'s inner loop places each crab with `FUN_002662E0` at the corpse's
bone 0 -- all hundred stacked on the body -- and writes the scattered point into
`+0x3C/+0x40`, which is the **mark** its state 1 then walks out to:

```c
FUN_002662E0(bone0.x, bone0.z, bone0.y, spawned);       // position
...
*(float *)(spawned + 0x3C) = crab.x + reach * cos(heading);   // mark
*(float *)(spawned + 0x40) = crab.z + reach * sin(heading);
```

The port had those two the other way round: the crabs appeared already spread and
every one of them carried a mark of (0, 0) -- the pool clear's -- so all hundred
converged on the arena centre in a column before starting their wander. They now
boil out of the body and fan off it.

What happens after that is the design and not a fault: state 2 sends each of them
to x in -11..-6 and z about 4, and state 3's mark creeps 0.05 further along -x
every time its hold expires while the player is more than a unit away. The swarm
walks down the beach after Orphen and leaps at him from a unit and a half out.

### Verified

`s14_e001` with Hand of Pyro on a repeating range: the crab dies at frame 6055,
the hundred spawn on the corpse at (0.00, 5.30), and the lead ends the run at
(-15.00, 4.00) in **state 120, animation 2** -- standing -- for the remaining
6500 frames. `--actor-report` reaches every crab state and swarm states 0, 1, 2,
3 and 6, none unimplemented. `s01_e024` and `s01_e012` byte-identical.

## s14_e001: the swarm's skitter is a music slot, not a cue

The swarm's continuous chitter is **not** in the cue table at all, which is why
looking for it there found nothing. It is a looping SEQ on a music slot, started
by `FUN_0027B380:76` -- the crab's death -- three lines after the hundred crabs
are released:

```c
FUN_0027c950(param_1);   // release the swarm
DAT_00355270 = 0;
FUN_00205d90(4,1000);    // and start slot 4, looping, at a full fader
```

`FUN_0027DC38:85-97` then steps it down as the swarm thins: at 70 left it ramps
slot 4 out and starts slot 3, at `fGpffff9290` = 30 it ramps 3 out and starts
slot 2, and when the last one dies `FUN_00205F40(2)` stops it. So the bed thins
with the swarm instead of cutting.

The port had none of it. `FUN_0027B380`'s call was a comment ("keys the death
sting on its own channel") and `FUN_0027DC38`'s three were written off as
"channel fades, which this port's audio path handles for itself". It does not --
nothing else starts these slots.

**And the slots would not have played anyway.** s14_e001 asks for SND.BIN
resources 98, 99 and 100 in slots 2, 3 and 4. Only 98 carries samples; 99 and
100 open section 1 with `NVB5` instead of a `pBAV` header, and `SoundBank::load`
rejected them, so both slots reported `no sequence`. `FUN_00205548:24-28` is
what that marker means:

```c
if (sVar1 == 0x564e) {                       // section 1 starts "NV"
  iVar4 = -(*(byte *)(sec1 + 3) - 0x30);     // the ASCII digit, negated
  param_1[3] = iVar4;                        // as both section sizes
  param_1[1] = iVar4;
}
```

and `FUN_00205310:36-45` reads the negative size back as *borrow*: copy channel
`digit`'s VAB id rather than uploading one. Channel n is music slot n - 3, so
`NVB5` is channel 5 is slot 2 -- resource 98's bank, shared by all three
sequences. `SequencePlayer` now carries a `borrowedSlot_`, and `bank()` answers
with the lender's.

Each of the three is one channel of program 0 between `CC99=20` and `CC6=127`,
loop-forever, playing overlapping short notes at scattered pitches -- 100 the
densest and lowest, 99 higher, 98 sparsest. That is the skitter.

Verified: slot 4 starts at frame 3083, the frame the swarm is released, and at
frame 6000 the report reads `loops taken 2, ... playing`. Mixed output rises
from ~4800 RMS to ~6650 across the start. The 70- and 30-crab step-downs and the
final stop are transcribed but **not** exercised here: the headless player
cannot kill swarm members, so nothing has yet driven the count down.

Separately, cue `0x11C` is the only entry type 0x7E has in the cue table -- one
`jal 0x00267d38` at `0x00277A34`, on the state-6 leap. It fires 37 times over a
9464-frame swarm phase, about one per 250 frames, ~0.43 s at volume ~38 of 127.
It is a per-leap lunge, not the bed, and it starts ~330 frames into the phase
because a fresh member must walk to its mark, cross to `x -11..-6` and settle
into state 3 before `FUN_0027B918` will take it.

Reading the cue log is the check for a *cue*, not listening: `FUN_00267D38_play_at`
logs **every** request, including `out of range` past 14 units and `no such cue`.
A cue absent from `--sound-report` was never requested -- but a missing sound
that is a music slot will never appear there at all. Check the report's slot
table for `no sequence` and its `music slot ... play` lines too.

### `--enemy-hp <slot>=<hp>[:<frame>]`

Writes one pool slot's live hit points (`+0x12A`), the same field the stat
record fills at spawn. A harness probe for reaching a boss's later phases.

It is not sufficient alone -- the crab only dies through the damage path, so it
still needs a hit to finish:

```
--enemy-hp 65=1:2000 --hold-cross 1900-1910 --hold-cross 1930-1940 ...
```

kills it around frame 2100 and puts the swarm on screen by 3536.

Without it there is no headless route. The player never walks, the crab drifts
out of sword range, and its HP plateaus at exactly **84 of 120** however long the
run -- 9,500 frames and 26,500 frames both end there. `--spell-power-scale` does
not help: it scales power, and the problem is hits not landing.

