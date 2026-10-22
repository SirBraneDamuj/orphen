# Battle: sword and guard

### The sword: charge, close, and two follow-up slashes

Cross is the kind-0 arm — the only one of the four whose release is not a single
shot. It is four states rather than two, and the port had the shape of all four
but the substance of none, which read as "the animation is there and nothing
else is".

**Charging elongates the blade.** `FUN_0024b7d0` (107) writes
`FUN_00249270(entity, 6) + 700, over 1000` into the effect entity's `+0x150` —
its Z scale, which for `grp_0139` is the blade's length. A full `0x2580` charge
reaches `2.3x`. The port had that, but it accumulated the charge
unconditionally; the original gates it on `FUN_002d9b78` exactly as states 111
and 113 do, so the charge stops the moment the caster's ground ring closes.
`--battle-report` now prints `blade=` (that `+0x150`, in thousandths) and
`anim=`, which is the only way to watch either of these from a headless run.

**Release runs at the target.** `FUN_0024b410` (106) plays animation 8, sizes a
travel timer from the distance (`distance / 0.064`, which is one frame's step at
the nominal `0x20` ticks) and drives `+0x30`/`+0x34` along `+0x5C` every frame.
It ends in a swing on any of four conditions: arriving inside `0.7`, the target
leaving a `+/-pi/6` cone, `+0x0C & 0x262` (blocked by geometry), or the timer
expiring. It also hops when the target stands more than `+0x58 + 0.3` above.
The port had this whole body replaced by a `return 0`, on the reading that it
"cannot run without an enemy" — true of the *body*, but the four exits and the
hop are the state, and none of them were there.

The player can also cut the run short: `FUN_002462c8`'s action-`0x85` arm turns
a press of the latched button into a pending `0x84`, and Orphen swings from
wherever he has got to.

**With no target it is one frame.** `FUN_0024cf20` forces control block `+0x2C`
positive and, when zero, to 1. State 106 negates that to -1; the read below
negates it again to 1, which fails `< 2` and asks for the swing immediately.
That is why Orphen swings in place with nothing locked on — the original's own
arithmetic, not a stub.

**The follow-ups are presses, and they are timed.** This is the part the port
had wrong outright. `FUN_0024ac88` (105) is entered *once per press*, not once
per swing: every route in goes through `FUN_0024a360`, which sets `state60 =
0x84 + 0x3FE5` — state 105 **with bit 0x4000**. The restart bit *is* the press,
and the handler splits on `+0x06` bit `0x10`:

| `+0x06 & 0x10` | meaning | what it does |
|---|---|---|
| set | fresh entry | respawn the blade, arm a 60-frame swing timer, clear the bit |
| clear | a re-press | step the animation `0x33 -> 0x30 -> 0x31`, one slash per press |

so three presses give three slashes, and the third (`0x31`) only chains back to
the first while the entity named by the blade's `+0x19C` is still alive. The
port instead advanced the chain on the animation marker every frame, with no
press involved — which produced all three slashes from one button and no way to
stop at one.

**The return value is the input lock, not the charge.** `FUN_00249610` writes
the handler's return into `+0x62` and raises control block `+0x38` bit 0
whenever it is non-zero, and `FUN_002462c8` refuses a press while that bit is
up. `FUN_0024ac88` returns 0 in exactly one case: `+0xAA` bit `0x400` is up —
the animation's cancel window — and the animation is not already `0x31`. That
one line is what makes the follow-ups a timed input rather than a mash. The
port returned the incoming `+0x62` unchanged, so the window was always open.

#### The blade decides whether the swing is a fresh one, and it is not in `src/`

`FUN_0024ac88`'s entry gate has two doors into the fresh-swing setup, and this
is the piece that took longest to find:

```c
if (blade->anim != 2) {
    if ((entity->+0x06 & 0x10) == 0) { ...re-press: advance the chain... }
}
...setup...
```

Bit `0x10` is not it. `FUN_00248e98` and `FUN_00248ee0` both end in `+0x06 &=
0xFFEE`, which clears that bit — and state 106 sets the run animation on entry,
so **105 is always handed a character with `0x10` down**. The only door left is
`blade->anim == 2`, and nothing in the battle module puts it there.

Type `0x139`'s own behaviour does. `FUN_002da350`, in the `0x2Fxxxx` block that
is not in `src/`, opens with:

```c
if (blade->anim != 2) {
  if (caster.pendingAction == 0x0B)             FUN_00225bc8(blade, 2);
  if (caster.action != 0x84 && != 0x86)         FUN_00225bc8(blade, 2);
}
```

State 106 is action **`0x85`** — neither charging nor swinging — so the blade
resets itself on the one frame 106 runs, and 105 then takes the setup branch.

Without that handler the port put the blade on animation 1 and left it there,
105 read every entry as a re-press, and the chain-advance branch sat waiting on
`+0xAA` bit `0x200` — a *sword-swing* marker that the **run** animation state
106 had just started never raises on schedule. The visible result was Orphen
jogging in place for 39 frames before each swing, and a first slash whose setup
never ran: no blade respawn, no 60-frame timer, no `+0x19C`. Porting
`FUN_002da350` removed the run outright — animation `0x33` is up on the frame
after 106 now, where it used to stay on `0x08`.

The rest of that handler is the blade's own life: a `DAT_00343888` point light
on **its own** bone 1 offset `(0, 0, 0.8)`, radius 1.3 while live and fading by
a thousandth of a tick on put-away; the `FUN_002148a8` sweep while the caster
is in action `0x84`; a five-frame re-hit cooldown in `+0x62`; and a third-slash
damage bump — reaction `0x1B` and `bonus + ((bonus / 2) | 1)`, so the last hit
of the chain lands at about 1.5x.

#### Every attack ended in a soft lock until state 108 was ported

`FUN_0024a870` is the walk back to the mark, and it is where all four arms
finish. `FUN_002462c8` returns `0xffffff36` on `current == 0x87` **before it
reads a button**, so a state 108 with no handler is not a cosmetic gap — the
character freezes on its last animation frame and never accepts input again.
`--battle-report` had been naming it (`state 108 hits=32`) since the slice
landed.

Control block `+0x14`/`+0x16`/`+0x18` holds where the member stood when the
battle started, in tenths. Two exits: under `fGpffff8810` (0.2) it goes straight
to state 120; at or over it, a three-point spline walk home — start, midpoint,
mark — with animation `0x37` under 1.5 units and `0x0C` over, the midpoint
arced up by `min(distance, 8) * 0.125`, and the last fifth dropping `0x0C` to
`0x0D` for the slow-down. It reuses `FUN_00244318`'s kind-2 array, the same
follower a scripted path walks on, but fills the slot by hand: mode forced to 1,
duration `distance * 200` with no `<< 4`, and steer `0xB5` with flag bit 1 —
so the character keeps the facing the swing left it in instead of turning to
follow the curve.

Swinging in place moves nobody, so the no-target case takes the first exit on
its first frame. `--battle-report` now shows `f=409 action=0x87 state=0x406c`
followed by `f=410 action=0x06 state=0x4078`, and a press at `f=412` starts the
next charge.

#### The target was never re-validated

`FUN_00249610:146-164` runs before every state dispatch and writes an invalid
target back to the control block as `-1`. A candidate survives only if its
`+0x12A` is at least 1 and its `+0x95` — the party-slot byte — is at least 9;
the player is 1 and allies are 2..6, so only an enemy passes. The port skipped
the write-back entirely, which left `FUN_0024cf20`'s "if the target is zero make
it 1" parked in `+0x2C` for the rest of the scene, and state 106 measuring a
distance to whatever happened to be in pool slot 1. `--battle-report` prints
`target=-1` throughout now rather than `target=1`.

Mashing Cross every four frames after a charged release, from
`--battle-report`:

```
f=413 pending=0x85 action=0x86 state=0x006b anim=0x33   <- release
f=414 pending=0x84 action=0x85 state=0x006a anim=0x08   <- 106, one frame, no target
f=415 pending=0x00 action=0x84 state=0x4069 anim=0x33   <- 105, wind-up
f=426 pending=0x00 action=0x84 state=0x0069 anim=0x33   <- slash 1
f=432 pending=0x84 action=0x84 state=0x0069 anim=0x33   <- press taken
f=449 pending=0x00 action=0x84 state=0x0069 anim=0x30   <- slash 2
f=456 pending=0x84 action=0x84 state=0x0069 anim=0x30   <- press taken
f=485 pending=0x00 action=0x84 state=0x0069 anim=0x31   <- slash 3
f=559 pending=0x00 action=0x87 state=0x406c anim=0x31   <- 108, walk home
f=560 pending=0x00 action=0x06 state=0x4078 anim=0x31   <- idle, taking input again
```

Animation `0x33` is up on the frame *after* 106, which is the whole of "he does
not run first". The presses in between produce nothing: the window was shut, and
after the third slash there is nothing alive to chain against.

#### Guard, end to end

Guard is the one command that runs all the way through, and it is worth reading
as the template for the rest, because **the visible half of a command is not in
the state handler**. `FUN_0024cba0` (state 117) does almost nothing to the
shield: on entry it sets the shield entity's animation to 1, stamps its own pool
slot into the shield's `+0x94`, plays cue `0xE7`, and ORs `0x11` into `+0x04`.
It never unhides it. `FUN_002e7328`, the type `0x1C7` behaviour, is the rest —
and it is where every interesting decision lives:

- **The shield is re-placed, not attached.** No `+0x192` parent, no bone: the
  handler copies the caster's facing and XY across every frame and sits it at
  `+0x58 * 0.75` above the caster's feet. That is why `FUN_00242df0` spawns it
  with `+0x192 = -1`.
- **`+0xA0` is a four-step sequence, not a looping clip.** 1 raise (one frame —
  it unhides and immediately becomes 0), 0 hold, 2 close, 3/4 destroy. Each step
  advances on `+0x06` bit 0, the animation-finished flag.
- **The caster's control block ends the guard, not the state handler.** Every
  frame it is not already closing, the shield reads the caster's current action
  byte out of `DAT_0031d7bf`; anything but `0x90` drops it into the close.

**Where "blocking wears off" comes from.** Nothing counts frames. `FUN_002462c8`
keeps a held guard alive only while the button is down *and* the shield entity's
`+0xA0` is not 2 — and the only thing that sets it to 2 out of the hold is clip
0 running to its last timeline entry in `FUN_002e7328`. So the block's duration
**is the length of the shield's hold animation**, stated in the model rather than
in code, and read the same way by two functions that never talk to each other.
On `s14_e012` that is 500 frames: guard at 301, `0x91` armed at 801 with the pad
still held, shield closed at 802.

What is not ported is `FUN_0024cba0`'s damage half — the halved hit, the second
`0x1C7` spawned at the impact bearing, cue `0xE8`, and the `0x4066` guard break.
All of it is gated on `+0xBE`, which only an attacking enemy writes, so there is
nothing yet to test it against.

