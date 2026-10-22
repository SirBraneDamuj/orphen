# Effects

### The rain, and the fold that was hiding in it

`s01_e013`'s deck is in a downpour on hardware and was dry in the port. The rain
is opcode `0x102`, which the port was consuming the operands of and dropping:

    0x102(1000, 150000, 50000, 6, 10, 0deg, 0deg, -1)

`FUN_00262250` divides four of those by 100000 and calls
`FUN_0021AC00(length, fall, rotateX, rotateZ, count, magnitude, height,
entity)` -- note the read order is not the call order. That is a 3000-record
pool at `uGpffffbb50`, 60000 bytes of `0x14`, walked by `FUN_0021AD98` with a
three-state machine per record in `FUN_0021AFA0`:

- **spawn** into a cylinder of radius `magnitude` and height `height`;
- **fall** at `fall * frameTicks / 32`, drawing a camera-facing sliver
  `0.012 x length` in world space -- four world corners each projected on their
  own, the way a hit spark's streak is, not a screen-space sprite like every
  other pool here;
- **splash** when a periodic `FUN_00227798` under the bottom of the streak says
  the ground has arrived: a flat ring on the ground that grows and fades over
  960 ticks and then respawns the drop.

With no entity the whole volume is anchored `magnitude` units down the camera's
line of sight at eye height, so it travels with you.

Three things worth keeping:

- **The live count only reaches half the target.** `FUN_0021AC00`'s growth loop
  is `added < target - liveCount` with both sides moving, exactly like
  `FUN_0021BD30`'s. Asking for 1000 gets 500, and the hardware reads 500 at
  `0x355AA0`.
- **Opcode `0x100`'s pool selector is 1-based.** `FUN_002620A8` computes
  `index = byte - 1` and takes 1..6, so the rain's gate is byte **1** and the
  haze field's byte 5. Byte 0 selects nothing.
- **The two `if`s that reschedule the ground probe overlap.** `height > 5`
  writes 100 and `height > 1` then writes 50 over it, so the 100 is unreachable.
  Checked in the disassembly at `0x0021B26C` before reproducing it.

#### FUN_00207DE8's colour fold, finally verifiable

The first build of this drew bright white bars instead of drizzle. A GS dump of
`s01_e013` settles why. Every one of the frame's 411 streak draws carries vertex
colour **(64, 64, 64, 120)** -- and `FUN_0021AFA0` passes `0xF0808080`. That is
`FUN_00207DE8:130-141`'s fold, `(c & 0xFEFEFEFE) >> 1`, applied because the
packet is textured: the streak reaches the GS at half grey and alpha 0.9375, not
white at a clamped 1.875.

The dump pins the rest of the state too: `ALPHA = 0x44`, i.e. `(Cs-Cd)*As+Cd`,
the ordinary source-alpha blend that `+0x0C`'s bit `0x4000` selects;
`TEST = 0x5000d`; `TEX1 = 0x60`; texture page `0x3300` at `256x256` PSMT8 with
the streak's UV box landing on `(251.4, 8.4)..(252.4, 39.4)`, a one-texel column
of sheet `0x19C`.

**This also fixes the hit sparks.** They go through the same `FUN_002190F8` and
were skipping the fold, which was recorded as unverified because no capturable
scene had a `FUN_002190F8` pool alive. The rain is one, and it says the fold is
real, so a spark is `0.9375x` white rather than `1.875x`.

Not reproduced, as everywhere else: `FUN_0020B6A0`'s `& 0xE0` clip reject, which
drops a quad whose corners have slid off the screen rect. The port draws a few
more streaks per frame than the dump does (470-ish against 411) and that is
where the difference is. `FUN_00218EE0`'s near test **is** modelled.

Splash counts line up where it can be checked: parked at the dump's own player
position, `--actor-report` prints `splashes=7` against the dump's 9.

### The smoke cloud, and the halt hiding behind it

The `0x400`/`0x10000` zone — the "which way do we go" argument — played its whole
dialogue and then never gave control back. The cause was one unimplemented
opcode, **`0x110` at blob offset `0x1ff8`**, hit 6619 times in an 8000-frame
run: the object-script slot is re-entered from the top every frame, so it halted
at the same instruction every frame and the `0x6D 1` that ends the beat was
never reached. Control now returns around frame 2400.

`0x110` and `0x111` share `FUN_00262CF0`, which reads three expressions into one
stack block and hands them on out of order: `FUN_00212DB0(expr3 / 100000,
expr1, expr2)` for `0x110` and `FUN_00212D60(...)` for `0x111`. So the operands
are a count, a packed `0xAARRGGBB`, and a scale. `0x110` reseeds every record;
`0x111` changes the parameters and leaves them alone — s01_e014 arms once with
`0x110` and then ramps the colour with a `0x111` every frame, 98 of them,
walking grey from `0x000000` up to `0x292929` and the alpha ceiling from `0x0A`
down to `0x04`.

**Nothing in the pool has a position.** A record is three 16-bit phase angles
plus a packed nibble triple, and the world position is rebuilt every frame
relative to the camera:

```
box[c]  = eye[c] + forward[c] * 1.5          (c = 0, 1)
box[2]  = eye.z  + (0.6 - 0.2) + forward.z * 2
u       = (phase[c] - (int)(box[c] * 65536/3)) & 0xFFFF
pos[c]  = u * 3/65536 + box[c] - 1.5
```

— a 3x3x3 box hanging in front of the eye, wrapping in all three axes, with the
alpha falling linearly to nothing within 1/16 of a turn of the seam so nothing
pops as it wraps. The packed word's top nibble is a shove counter: a particle
inside a 0.8 x 0.8 x 1.1 box around the lead's waist has it set to 15 whenever
the lead is moving, and while it is up the particle drifts an extra
`dir * counter * 2` a frame. "Moving" is `+0xA0 >= 3` — any animation but a
stand — or, when standing, an actual change in position.

`src/ported/entity/original_smoke_cloud.*` is all of that, stepped in
FUN_002192C0's first slot and reported by `--actor-report`.

**It is not drawn, deliberately.** `FUN_00212F38` builds a VU1 program's input,
not a GIF stream: 40 quadwords of template to VU address 32, then 32 positions
(V4-32 to address 6) and 32 four-byte attribute quads (V4-8 to address 47) whose
byte 0 is `(i & 3) * 10` and byte 3 the alpha. The template's GIFtag reads
NLOOP 1, EOP, PRE, NREG 9, PACKED, REGS = RGBAQ then four (UV, XYZ2) pairs, and
PRIM = triangle fan with TME, ABE and FST set — a flat, textured, blended quad
whose corners are `0x510` and `0x5F0` in 4-bit fixed, texels 81..95 on both
axes. **There is no TEX0 anywhere in the packet**, and no ALPHA register behind
that ABE, so which sheet those texels belong to and how it blends are not in
this function. Picking either would be a guess. A GS dump taken while the smoke
is up, read with `port/attic/gsparse.py`, settles both.

Nothing in this scene reads an event flag it does not also write — the arrival
branch is the only thing it inherits from the scene before it.

## The spark shower, and the pool behind it

When the magic projectile ends it throws a hundred sparks. They come out of a
**global particle pool**, `DAT_00355620` — 0x18000 bytes at 0x01C69A00, so 1536
entries of 0x40 — with a single behaviour pointer, `DAT_00355e0c`, that every
entry is stepped through. `FUN_002d3218` walks all 1536 once a frame, from the
slot `FUN_002239c8` gives it immediately after the actor loop. A null behaviour
skips the pool entirely, however much of it is marked alive.

`FUN_0022a418` resets the pool at scene init. **That reset draws from the RNG
once per entry** — 1536 draws — and writes each into `+0x22` as a stagger. Only
the unported ambient-dust behaviour reads that stagger; the spark path
overwrites `+0x22` on spawn. So for sparks the reset's only observable effect is
the advance of the shared generator, and porting it faithfully moved this
port's `--actor-report` baseline. Runs remain reproducible; the numbers are just
1536 draws further along than they were.

### The entry

```
+0x00 f32  x, y, z            world, the axes an entity's +0x20 uses
+0x10 f32  vertical velocity
+0x14 f32  horizontal speed
+0x18 u32  GS RGBA, alpha in the high byte
+0x1C s16  alive -- the spawn loop tests this and nothing else
+0x1E s16  quad width  in units of 40 GS 12.4 units at unit depth
+0x20 s16  quad height in units of 20 -- the 2:1 GS pixel again
+0x22 s16  ticks before the fade starts; allowed to go negative
+0x24 f32  gravity
+0x28 f32  heading, radians
+0x2C s16  the spawning entity's pool slot; written, never read
```

Every field was read out of a PCSX2 save state taken with the sparks live:
exactly 100 alive, all tagged with the projectile's slot, headings stepping by
0.0349066 rad, colours with red in `{0, 0xFF}`, green and blue each in
`[0xC0, 0xFF]`, alpha `0xE0`.

### The burst

`FUN_002d2470` fills up to a hundred entries, fanning out from the projectile's
own facing plus `DAT_00354690` (π/2, so the first spark leaves square to the
flight) and stepping two degrees per particle. The fan advances once per
particle *seeded*, not once per slot examined, so a burst landing in a
fragmented pool still gets an even spread — and simply gets fewer than a hundred
rather than waiting.

Red is all-or-nothing on a coin flip while green and blue are each a random
`0xC0..0xFF`, which is why the shower reads as half white and half cyan and
never dim. Width and height take the *same* draw, 1 or 3, so a spark is either
one pixel or three times that with nothing in between.

`FUN_002d2348` then flies each one outward along its heading, bleeding
`DAT_00354678` off the speed a frame until it reaches that value, on a properly
integrated ballistic arc — the position takes the old velocity plus half the
acceleration term and the velocity updates from the same old value. Two
different tick scalings are in play and they are not the same quantity: the
vertical integration works in ticks/8 and the horizontal step in ticks/2. Once
`+0x22` runs out the alpha falls by two a frame until the particle frees itself,
so a spark lives about 30-59 frames plus 112 of fade.

### The quad

`FUN_0020b600`'s `vftoi0` is masked to `.xyz`, so lane W survives as a float
holding `Q = 1 / max(viewZ, eps)`. `FUN_002d3058` multiplies its two literals by
that directly, which means **a particle's size is a plain 1/z with no projection
term in it** — one to three pixels at any normal depth. The projected point is
the quad's top-left corner, not its centre.

The texture is the boot sheet in slot `0x2A`, which the port already binds: the
packet carries `0x2B` and that field is slot + 1, the same encoding
`FUN_0020f510` uses for entity `+0x136`. The UV rectangle is fixed at texels
(65,177)-(79,191), and `DAT_10008080`'s bit `0x8000` selects blend mode 2,
additive. `FUN_00207de8(0x1000)` puts every particle in one display-list bucket,
above all of `FUN_0020f510`'s depth-sorted `1..0xFFF` and below its `0x1005`, so
the port emits them into the same list with that bucket.

Unlike the sprite pass, `FUN_002d3058` has **no near, far or window cull** — its
only guard is the clip flags. The port rejects a particle behind the eye, which
`FUN_0020b600`'s `w` clamp cannot save and which would otherwise smear across
the screen, and nothing else.

### What is not ported

`FUN_002d3320`, the ambient dust `FUN_002d36f8` installs into the same pool. It
reseeds its own particles off the player's bones through `FUN_0020dc88` and
reads a per-character colour table at `DAT_00326708`; it is a separate effect
that happens to share the storage.

### Checking it

`--actor-report` ends with a `particles:` line giving the live count and the
installed behaviour. On `s01_e024` with `--spawn -2.5,-8,0 --press-magic 60` the
projectile detonates around frame 165, the pool reads `alive=100` from there
through frame 250, and it is empty again by 340. Screenshots are a poor check
here: the bolt homes on the enemy cluster and leaves frame before it ends.

## The screen smear

`FUN_00201a38` draws the *previous* frame back over the current one,
alpha-blended. It is what makes the camera look distorted and blurry on
impacts — the lightning strikes in the ship scenes, Volcan's sword swing, a
falling crate. Ported into
`ported/render/original_frame_feedback.{h,cpp}`.

The two opcodes that drive it were documented as something else entirely.
`analyzed/opcode_dispatch_tables.md` called `0xC8` `set_text_color_index` and
`0xC9` `set_text_color_index_and_palette`, and the port consumed both as
`operands-only` on that basis. Neither touches text:

```
0xC8 FUN_00264448  DAT_00355661 = expr                       blend alpha
0xC9 FUN_00264470  same, plus five shorts at DAT_00343878    dx, dy, sx, sy, rot
```

Nothing in the dialogue or menu renderer reads `DAT_00355661`. Its only readers
are `FUN_002000c0:214`, which gates the whole effect on it —

```c
if ((DAT_00355661 != '\0') || (DAT_00354b88 != 0)) FUN_00201a38();
```

— and `FUN_00201a38` itself, which uses it as the blend alpha. The global wears
four different Ghidra names across the decompilation (`DAT_00355661`,
`uGpffffb6f1`, `bGpffffb6f1`, `cGpffffb6f1`), which is part of why the
connection was easy to miss.

### The source is a framebuffer, not a copy

`FUN_002f9620` builds `TEX0` with `TBP0 = DAT_00354C2C * 0x8C0`, `TBW` 10,
`PSM` 1 (`PSMCT24`). `DAT_00354C2C` is the parity of the buffer *not* being
drawn into, so `TBP0` names the frame the player is currently looking at. `TBW`
10 is 640 pixels and the two bases (0 and `0x8C000`) are `640*224*4` apart, so
the frame is one 224-line field. `FUN_00201a38` then clears `TCC` out of the
word it was handed, so the sampled alpha is discarded and `RGBAQ`'s is used.

The primitive is one alpha-blended textured tri-fan over the whole screen:

| register | value | meaning |
|---|---|---|
| `PRIM` | `0x155` | `TRI_FAN \| TME \| ABE \| FST` |
| `ALPHA_1` | `0x44` | `(Cs - Cd) * As + Cd` |
| `RGBAQ` | `alpha << 24 \| 0x808080` | `0x80` is unity under `MODULATE`, so only alpha does work |

Alpha is a GS blend factor over **128**, not 255. `0x80` replaces the frame
outright; the `0x7E` the ship scenes use is 98%. And because each frame
re-samples a frame that already contains the previous blend, it compounds — the
trail is exponential, not a single ghost. That compounding is the effect, and
reproducing it is the one ordering constraint on the port: the capture has to
happen *after* the frame's own quad.

The packet head-inserts at `DAT_7000000C + 0x10064`. `FUN_00207de8`'s tail shows
the bucket stride is `0x10` with the head pointer at `+4`, so that is sort
bucket **`0x1006`** — under the letterbox bars and the fullscreen fade (both
`0x1007`) and under every text overlay (`0x1009`). The smear covers the world,
and the fade tints the smear.

### The ramp that almost never runs

`DAT_00355661` is the target and `DAT_00354B88` the current level, and the head
of `FUN_00201a38` steps one toward the other by 1 a frame, drawing nothing below
2. Except that the very first test is `if (current == 0)`, and that branch takes
the target verbatim and never ramps. Only opcode `0xBE`'s table entry 12
(`sh a0,-0x53e8(gp)`) ever seeds a non-zero current level, so **every script use
of `0xC8` gets exactly the alpha it wrote**. `s01_e012` snaps it to `0x7E`, then
re-issues `0xC8` every frame with the current value of an `0x90`/`0x91`/`0x92`
parameter ramp decaying to zero, and writes 0 to clear. The script's own ramp is
the fade-out; `FUN_00201a38`'s is dead code for it.

### Only the destination moves

The base quad is ±5104 × ±1776 in GS 12.4 fixed point (±319 × ±111 pixels) and
carries UVs for the 1..639 × 1..223 rectangle of the source. Untransformed that
is a 1:1 copy, which pins the two origin constants the function adds last:
`0x7FF8` and `0x7FFE` *are* the screen centre, and the `0x7FF6` variant is the
same point half a line up for the other interlace field. The port never needs
the GS `XYOFFSET` — centre plus `X/16` is the same answer with one fewer
constant to be wrong about.

`0xC9`'s five shorts then transform that quad, and **not** the source rectangle:

| global | step | effect |
|---|---|---|
| `DAT_0034387C/E` | `v * (s + 1024) >> 10` | zoom blur |
| `DAT_00343880` | rotate by `v/10` degrees | swirl |
| `DAT_00343878/A` | translate | directional smear |

All zero gives a plain ghost. `fGpffff8010` is `0.017453289` — π/180 — so the
rotation is in tenths of a degree. The `y` term is doubled going into the
rotation and halved coming out: the quad is 224 lines over 640 pixels, so a line
is worth two pixels and a 90° spin would otherwise come out squashed to the
field's aspect. `FUN_0023c340`, the battle/spell path, writes these globals
directly with scales of `0x32`, `0x78` and `-70` — that is the impact punch.

### What the port does instead

There is no framebuffer to name in `TEX0`, so the game's picture is copied out
of the back buffer into a texture once per presented frame, at the end of the
composite — after the world, the smear, the bars, the fade and the subtitles,
and before the harness's own overlays. On the console the buffer would also hold
`FUN_00268270`'s debug text, but the port's overlays are the harness's and would
smear a HUD the game never drew.

Three things that are easy to get wrong here, all of which cost a build:

- **The capture is unconditional.** Gating it on the effect being up leaves the
  frame a script *arms* the smear on with nothing to sample — the console always
  has the other framebuffer sitting there, and there is no lookahead that would
  let the port know one frame early.
- **Match the framebuffer format.** `glTexImage2D` with `GL_RGB` makes
  `glCopyTexSubImage2D` convert every pixel. Measured on `s01_e024` at
  1280×960, `--render-bench 8`: 4.0 ms a render with no capture at all, 4.4 ms
  with `GL_RGBA8`, 5.15 ms with `GL_RGB`. Same pixels out of all three.
- **`v` flips.** GS `v` counts down from the top row; a texture copied out of
  the back buffer has its origin at the bottom.

The texture is power-of-two and larger than the window with the used fraction
tracked separately, because a fixed-function context is not promised
`GL_ARB_texture_non_power_of_two`.

`--no-screen-smear` skips both the quad and the capture, so the same frame can
be captured with and without it — which is how this was confirmed to reach
pixels at all rather than merely to be "implemented". On `s01_e012` frame 2060,
during Volcan's sword swing, 483,516 of 691,200 pixels differ between the two;
on frame 1900, outside both bursts, the two captures are byte-identical. The
difference image is a fan of ghosted blades trailing the sword and nothing at
all on the static geometry, which is the signature a 1:1 feedback blend should
have.

`--scr-report` prints a `screen smear` section: how many frames the alpha was
written on, how many were non-zero, the peak, and whether any caller reached for
`0xC9`'s transform.

