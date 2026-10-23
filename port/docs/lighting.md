# Lighting

## The dynamic point lights

The sixteen-slot table at `DAT_00343888` and the falloff that consumes it are
both ported. Opcodes `0xBF`/`0xC0` allocate, `0xC2` alpha, `0xC3` colour, `0xC4`
radius, `0xC5`/`0xC6` position, `0xC7` release. The *radius* field is
simultaneously the light's extent and the allocator's free-list marker, which is
why a memory dump shows plausible positions and colours in slots nothing is
using.

`FUN_0020b430` compacts the live slots into a VU0 list — count at quadword 2,
then `{position, (r, r², 1/r²), colour/255}` per light — and two different
consumers read it:

- **Map draws** run the whole list per vertex (`_vcallms(0xe0)` falling into the
  loop at VU0 `0x52`). The result reaches VU1 only through the second additive
  term at `0x1da..0x1e0`, `extra * colour / 128`.
- **Entity draws** resolve table slots **0..2** — not the three nearest — into
  VU1's directional lights 1..3 (`FUN_0020eec0`, VU0 program `0x33`), and sum
  everything from slot 3 up into a flat per-entity tint (`_vcallms(0x220)`).

That split is what the two allocators are for, and it makes the dispatch table's
names backwards: `0xC0` allocates from slot 0 and is the one that can become a
real directional light on a character; `0xBF` allocates from slot 3 and can only
ever tint one. Both still light the map per vertex.

The loop itself, off VU0 `0x52..0x79`:

```
reject unless |light - point| < r on every axis   (two SUBx, FMAND 0xE0)
reject unless |d|² < r²                           (SUBy.w, FMAND 0x10)
accumulate colour * clamp(1 - |d|²/r², 0, 1)
min the sum to 2.0, multiply by 127.5, truncate
```

Both rejections are strict — a sign flag is set only for a negative result.
Program `0x33`, the per-entity one, has *no* rejection tests at all; the clamp is
what turns a light the entity stands outside of into black.

**Confirmed end to end against a save state**, not fitted. `vu0Memory.bin` taken
during the Dortin scene holds count 1 and
`(5.498, -2.684, -0.468) / (2, 4, 0.25) / (0.502, 0.502, 0.502)`, and the
script's own table slot 3 reads position `(5.498, -2.684, -0.468)`, colour
`(128,128,128)`, radius `2.0`. The port's `--scr-report` prints the same slot
independently. That also resolved the packing question `docs/vu1_microprogram.md`
had left open: the three parallel scratchpad runs are staging, and the `VSQI`
loop at the tail of `FUN_0020b430` interleaves them into the stride-3 form.

In `s01_e012` slot 3 is live from frame 1409 to 5440 — it is the lantern over the
shop counter, and the counter, shelving and back wall visibly pick it up. Slot 0
carries the second colour ramp in four ~60-frame bursts plus a window from 9477
to 10426.

Cost, measured with `--render-bench 6 --screenshot :2200` so both runs step the
scene identically block for block:

| | map | entities |
|---|---|---|
| no light live | 0.309 → 0.312 ms | 2.562 → 2.575 ms |
| one light live | 0.687 → **0.762** ms | 3.111 → **3.202** ms |

So ~0.17 ms/frame while a light is live, out of ~5 ms, and nothing measurable
otherwise — the per-vertex path early-outs on the light count. `s01_e024` and
`s01_e012` before frame 1409 render **byte-identically** with and without it.
`--lighting-no-points` turns it off for A/B. It defaults **on**, because the VU0
list was read back out of a save state and matched the script's table exactly
rather than being derived from the microprogram alone.

The three lighting switches and their defaults:

| behaviour | default | flag |
|---|---|---|
| dynamic point lights | on | `--lighting-no-points` turns off |
| unlit flag (decals drawn at vertex colour) | on | `--lighting-no-unlit` turns off |
| per-material light floor (VU1 `0x01d1` `MAXz`) | **off** -- retracted, below | `--lighting-floor` turns on |

`--lighting-all` turns on both of the last two; `--lighting-unlit` and
`--lighting-no-floor` are kept as aliases for the default.

## The lantern's light pool: the unlit flag, and the GS's missing octave

A GS dump plus a save state of s01_e012's shop settled two things at once and
**corrected an earlier wrong call**, so the retraction comes first.

**Retracted: the per-material light floor.** VU1 `0x01d1`'s `MAXz` is real and
the port still implements it, but it is back **off**, behind `--lighting-floor`.
It was briefly turned on because a hardware wall patch appeared to confirm it --
and that patch sat *inside the lantern's light pool*. A global lift fitted to a
local effect. With the pool drawn properly the floor only overshoots: at the
matched frame the wall away from the lantern is (30, 22, 16) on hardware,
(28, 22, 17) without the floor and (32, 24, 18) with it.

**Pinning the frame first.** The save state's camera is
`DAT_0058c0a8 = (3.5337, -3.3193, -1.4797)`, the port's f3000 camera to four
decimals -- but `uGpffffb700` is `0x002a170c`, light 0 = (42, 23, 12), while the
port at f3000 sits at the ramp's peak, (118, 78, 59). The scene light ping-pongs
on a ~120-frame cycle, so *any* unmatched frame is a brightness argument about
nothing. Port **f2949** emits (42, 23, 12) and f2950 emits (43, 24, 13), which is
what VU1 `0x2a3` holds -- the EE word and the VU upload are one frame apart, and
the port's ramp is frame-exact. Everything below is f2949.

**The pool is twelve unlit decals.** In the dump the pool is a fan of gouraud
quads whose bright corner carries the vertex colour **(254, 155, 122)** at alpha
31. The lighting model cannot produce that: with this frame's ambient
(10, 30, 50) and light 0 (42, 23, 12), the largest vertex colour it can emit is
`255/256 * (52, 53, 62)`. Anything above 128 in a channel is an unlit draw, and
the frame has exactly 26 of those out of 3556 -- the authored glow decals and
nothing else. `--lighting-unlit` is therefore **on by default** now, with
`--lighting-no-unlit` to A/B it. The map primitives are `#1121`-`#1128`, `#2233`
and `#2234`, all `flags=0x22044` (bit `0x2000` set) with `slot0 a=0x1f f=0x20`.

**And GL was throwing away half of it.** The GS's texture function is
`(Ct * Cv) >> 7`: a vertex colour of `0x80` is x1.0 and `0xFF` is **x1.99**, so
the GS can brighten a texel. `GL_MODULATE` clamps both operands to 1.0 and can
only darken, so (254, 155, 122) came out at x1.0 -- half the pool. The draw paths
now scale vertex colours by 1/256 instead of 1/128 and put the factor of two back
with `GL_COMBINE` + `GL_RGB_SCALE 2` (`applyGsTextureEnv`). That also moves the
clamp to where the GS has it: the old code clamped `c * modulator` at 128, and
the GS clamps at 255 and *then* shifts by 7.

The two together, across a horizontal band of the wall:

| x | 160 | 200 | 240 | 280 | 320 | 360 | 400 | 440 | 480 | 520 |
|---|---|---|---|---|---|---|---|---|---|---|
| hardware | 12.7 | 19.4 | 24.2 | 35.5 | **48.2** | 42.8 | 35.2 | 43.6 | 34.8 | 28.2 |
| before | 11.8 | 15.4 | 9.9 | 13.5 | 18.1 | 15.1 | 23.5 | 22.0 | 22.3 | 23.6 |
| after | 12.9 | 19.6 | 18.4 | 29.7 | 42.5 | **47.9** | 54.7 | 44.9 | 33.1 | 26.6 |

The pool's position, width and warmth now track. Its peak is about one sample
right of hardware's and ~15% high, which is not chased further here.

**Two things the dump settled in passing.**

`TEST_1` is `0x5000d` on 3554 of this frame's 3556 draws. **Corrected later:**
this file first read that as `ATST` GEQUAL against `AREF` 0 -- a test that never
rejects -- and built two open questions on top of it. `ATST` is bits 1..3, so
`0xd` is `110` = **GREATER**, not `101` = GEQUAL. `ATE` on, GREATER, `AREF` 0,
`AFAIL` KEEP is exactly `glAlphaFunc(GL_GREATER, 0)`: the GS discards alpha-zero
texels and nothing else. See "The cutout discard the map path was missing" in
[Rendering](rendering.md).

The four VU1 register blocks at `608 + mode*3`, read straight out of
`vu1Memory.bin`, are exactly what this file already claimed: mode 0 `ALPHA 0x44`
ZMSK 0, mode 1 `ALPHA 0x44` ZMSK 1, mode 2 `ALPHA 0x48` ZMSK 1, mode 3
`ALPHA 0xa1` ZMSK 1. The decals draw as mode 2, matching `f=0x20`, so
`mapBlendMode` is correct. A first pass at the dump appeared to show mode 1, and
that was a parser bug worth recording: VU1 emits the *next* draw's A+D block
ahead of its GIF tag, so reading the registers at batch-flush time shifts every
blend mode by one draw.

**Cost.** The 20000-frame `--actor-report`/`--scr-report` is byte-identical.
s01_e012 f6000 moves 6% of pixels, mean 26.51 -> 27.26. s01_e024 moves more than
the pixel count suggests -- 50% of f600, but the mean only goes 55.51 -> 55.96 and
the visible change is the chest lid plus a slightly bluer floor. That is the room
where the old clamp point was actually biting: e024's modulator exceeds 1.0 in
blue, e012's never does.

## The lantern hung a quarter unit too high

Same save state, a different bug, and the cheapest kind to confirm: the entity
pool is at `DAT_0058beb0` with a `0x1D8` stride, type at `+0x00` and position at
`+0x20`, so hardware's answer can just be read out of `eeMemory.bin` and diffed
against `--actor-report`.

Across the whole scene, **two** entities disagreed. One was the hanging lantern:

```
slot 40  port 0x0299 (4.0, -1.4, 0.25)   hw 0x299 (4.0, -1.4, 0.0)
slot 35  port 0x02b7 (1.74, -2.21, -0.96) hw 0x2b7 (1.737, -2.205, -1.0)
```

s01_e012 has 25 of these lanterns and they all sit at z = 0. Only the one at
(4, -1.4) was lifted, because only that one hangs over a shelf: primitive
`#2223`, an upward face at z = 0.25 whose `+0x00` carries the `0x800`
terrain-sample bit, so the ground scan accepts it. Nothing wrong with the scan.

The fault was in the placement spawn. `FUN_0025e7c0` writes the record's z into
`+0x28` and `+0x4C` and stops -- **it does not sample terrain** -- and the port's
comment said exactly that, immediately above a line that wrote the sample into
`+0x4C` anyway. The sample is only meant to tell `--scr-report` whether the
placement landed on anything. Hardware confirms it from the other side: every
lantern has `+0x4C = 0.000`, and `+0x4C` equals the spawn z for every other
placed prop in the scene, including slot 35's -1.000.

With that line reduced to `spawnRecord.grounded = height.has_value()`, **all 67
reported entities match the save state's pool.** s01_e024 is byte-identical --
the 20000-frame `--actor-report`/`--scr-report` and the f600 frame both -- and
s01_e012 f6000 moves 1.2% of pixels, which is the lantern coming down.

Worth keeping in mind for the rest of the placed props: a prop is only visibly
wrong when it happens to sit over geometry, so this class of bug hides until the
camera finds the one instance that does. (Slot 35 was the other half of
[Physics](physics.md)'s placement-grounding note.)

