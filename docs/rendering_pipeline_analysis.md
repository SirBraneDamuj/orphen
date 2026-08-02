# Map Rendering Pipeline

How the field map gets from a PSM2 chunk onto the screen, and where the two
things that are easy to get wrong -- the field of view and the see-through
walls -- actually come from.

Addresses are US (`SLUS_200.11`). `gp = 0x00359F70`. Constant values are read
out of `eeMemory.bin` unless stated otherwise.

## Frame path

```
FUN_002239c8 (and the other per-mode frame functions)
  FUN_00208ee8            build the camera matrices
    iGpffffadbc == 0xC ? FUN_00214300 : FUN_0020bec8
    FUN_0020b430
  FUN_00208f28
    FUN_00225940          (when iGpffffb788 != 0)
    FUN_00208f58
    FUN_00209140          per-primitive visibility, fade, depth sort, DMA emit
```

`FUN_00208450` is a separate pass over the section J records: it re-transforms
the map sections that move (lifts, doors), and is not part of the static draw.

## Two packet builders

The per-primitive GS packets are built **once, at map load**, by `FUN_00211230`
(called from `FUN_0022b5a8`'s tail). Each primitive's packet ends in a DMA tag
that `FUN_00211230:363` flips from `cnt` to `ret` with `^ 0x70000000`, so every
packet is a callable DMA subroutine. Its address is cached in the 0x80-record
at `+0x7C`.

`FUN_00209140` then does no geometry work per frame beyond deciding *which*
subroutines to call and in what order: it emits `call` tags (`0x50000000`) into
a 4096-entry bucket table at `DAT_7000000C`.

The actual transform, lighting and rasterisation live in VU1 microprograms,
which are not in the decompilation. Known entry points:

| `vcallms` | Called from | What it appears to do |
|---|---|---|
| `0x20` | `FUN_00216510` | normalise a vector |
| `0x60` | `FUN_0020bb58` | 4x4 matrix multiply |
| `0xE0` | `FUN_00209140:205` | transform a primitive's corners, hand back per-vertex bytes and a view-space AABB |
| `0x13B` / `0x14B` | `FUN_00211230:341/344` | the two map draw programs |

## The projection

`FUN_0020bec8` is the field camera composer; `FUN_00214300` is the same job for
mode `0xC`. Both build the projection through `FUN_0020bd58`.

Ghidra renders `FUN_0020bd58` with a shifted argument list. The disassembly at
`0x0020bd58` shows the output pointer arrives in `a0` (`move s0,a0`) and *nine*
floats follow in `f12`..`f19` plus one stack slot (`lwc1 f0,0x50(sp)`):

```
FUN_0020bd58(MATRIX *out, float scale, float horizontalRatio, float verticalRatio,
             float centreX, float centreY, float screenZAtNear, float screenZAtFar,
             float near, float far)

  m00 = horizontalRatio * scale
  m11 = verticalRatio   * scale
  m20 = centreX,  m21 = centreY
  m22 = (far*screenZAtFar - near*screenZAtNear) / (far - near)
  m32 = (near*far*(screenZAtFar - screenZAtNear)) / (near - far)
  m23 = 1.0,  m33 = 0.0
```

Matrices are row-major in row-vector order (`v' = v * M`), matching the VU0
macro-mode sequence in `FUN_0020bb58`. So `z_screen = m22 + m32 / z`, which is
exactly the expression `FUN_00209140:361` uses for its sort key; the two terms
are cached in `DAT_003555A0` and `DAT_003555A4`.

Shipped arguments (`FUN_0020bec8` at `0x0020c11c`, the `cGpffffb66e == 0` path):

| value | source | meaning |
|---|---|---|
| `scale` | `powf(2, fGpffffb6e8) * 3840` | `fGpffffb6e8` is a **log2 zoom**; the default 1.0 gives 7680 |
| `horizontalRatio` | 1.0, or `0.77` when `cGpffffb66e != 0` | the widescreen squeeze |
| `verticalRatio` | 0.45 (`0x0035201C`) | |
| `centreX/Y` | 32768.0 | GS 12.4 fixed point, i.e. 2048 px |
| `screenZAtNear` | 65534.0 | |
| `screenZAtFar` | 1.0 | |
| `near` | 0.3 (`0x00352024`) | |
| `far` | 128.0 (`0x43000000`, the stack argument) | |

### Field of view

The projection alone does not give an angle -- it needs the GS screen geometry.
From the GS dump in the repo root:

```
SCISSOR_1   scax0=0 scax1=639 scay0=0 scay1=223     640 x 224, field rendered
XYOFFSET_1  OFX=1728.0 OFY=1936.0 px                centre 2048 -> half 320 x 112
FRAME_1     FBW=10 PSM=1                            640-wide PSMCT24
```

so `tan(half angle) = halfExtentPixels * 16 / scale`:

| | tangent | FOV at the default zoom |
|---|---|---|
| horizontal | 320 * 16 / 7680 = 0.6667 | **67.4 deg** |
| vertical | 112 * 16 / 3456 = 0.5185 | **54.8 deg** |
| horizontal, `cGpffffb66e != 0` | 320 * 16 / 5913.6 = 0.8658 | 81.1 deg |

The tangent ratio is 0.778 against the 0.75 a true 4:3 square-pixel projection
wants, so the shipped picture is about 3.7 percent vertically stretched. That
is authentic, not a rounding artifact.

### View matrix

`FUN_0020bec8:0x0020bff0-0x0020c078`, in row-vector order:

```
view = translate(-eyeX, -eyeY, -(eyeZ + 0.4))     fGpffff808c = 0.4
     * rotateZ(fGpffffb6d4 + pi/2)                yaw
     * rotateX(-pi/2 - fGpffffb6d8)               pitch
     * rotateZ(uGpffffb6dc)                       roll
     * scale(-1, 1, -1)
```

The result is a right-handed view space with +x right, **+y down** and +z into
the screen -- the GS convention, which is why the projection can write straight
into screen coordinates.

## Visibility, fade and draw order

`FUN_00209140`, per 0x80-record. `flags` is the word at `+0x70`.

**Skip outright**

- `flags & 0x20` -- the hidden bit. `FUN_00211230:68` also *sets* it on any
  primitive that turned out to have no usable material slot.
- `DAT_00354d2c == 0xC` and `0x78 +0x04 & 0x400000` -- geometry hidden in that
  mode.

**Sphere against the frustum**, using the centroid at `+0x60` and the radius at
`+0x74` that `FUN_0022c6e8` derived:

```
reject if  z < 0.4 - r                  DAT_00351FC8
reject if  z > DAT_00355628 + r         the draw distance
reject if  |x| > z + r  or  |y| > z + r
```

The side test is a fixed 90 degrees, deliberately looser than the 67.4 the
projection actually shows.

**Near plane.** If `z < r + 0.6` (`DAT_00351FCC + DAT_00351FD0`) the primitive
goes down `FUN_00209ca0` -> `FUN_0020b600` -> `FUN_00209e18` / `FUN_0020a2c0`
instead. `FUN_0020b600` is not a polygon clipper -- it transforms, divides,
tracks a screen AABB and returns VU clip flags, which `FUN_00209ca0` rejects on
with `& 0xE0`.

**The occlusion fade -- why walls go see-through.** A byte at 0x80-record
`+0x2E`, seeded to `0x80` by `FUN_0022c3d8`, walks down to a floor of `0x5C`
and back up to a ceiling of `0x7E` at one step per frame. At the ceiling the
emitted value is `0`, so zero means "not fading". It reaches VU1 through the
`0x6E00C02E` unpack alongside the per-vertex bytes.

Two probe points stand in for the player (`FUN_00209140:97-126`): one at the
feet plus `DAT_00351FC4` (0.2) and one at the head (`DAT_0058BF08`, entity
`+0x58`), nudged apart in view space by the same 0.2 and then perspective
divided. Together they form a small rectangle.

The main path fades only when **all** of these hold:

- `DAT_00355641` is clear (a global "never fade" gate),
- `flags & 0x40` is clear -- blended materials never fade,
- the primitive's view-space AABB max z is nearer than the player's head,
- its AABB overlaps the screen band `[-0.1, 0.1]` (`DAT_00351FD8` /
  `DAT_00351FD4`) and the vertical span between the two probes,
- the 0x78-record's world AABB max z is above `playerZ + 0.38`
  (`DAT_00351FDC`) -- floors are excluded, only things that stand up,
- `FUN_002099d8` reports a screen overlap.

The near-plane twin at `FUN_0020a2c0:668-683` uses a **looser** set: the blend
flag, the overlap and the height, and nothing else. That is the path a wall
between an outside camera and the player usually takes.

`FUN_002099d8` walks the polygon's edges and calls `FUN_00209928` on each;
overlap means every edge failed to separate the probe rectangle. Because the
sign convention assumes a consistent screen-space winding, **a back-facing
primitive passes every edge trivially and always reads as covering the player**.
That, plus backface culling, is the whole mechanism: with the camera outside a
room the near wall is back-facing and covers the player, so it fades.

One quirk is reproduced rather than corrected: `FUN_002099d8` transforms the
polygon into *view* space while the probe rectangle is *perspective divided*.
Both are small numbers near the origin for a primitive that actually covers the
player, so the test behaves, but it is not a like-for-like comparison.

**Depth sort.** Survivors go into one of 4096 buckets keyed by
`(m22 + m32/z) >> 4`, clamped to `[1, 0xFFF]`, where `z` is the AABB max plus
the primitive's blend term at `+0x78` (or minus 1.0 when `DAT_00355700` caps
the fade). Buckets are walked low to high, which is far to near.

## Draw distance and fog

`DAT_00355628` is set at map load from `DAT_0032538c` (`FUN_0022a418:185`),
which defaults to **32.0** (`FUN_0022a360`) and is overridden per scene by
script through `FUN_00263cb8` (and `FUN_0023a860`). `FUN_0022a418:344-354`
derives fog start `= far * 0.25`, fog end `= far`, and `DAT_0035562C = far - 5`.
The light and fog colour block sits at `DAT_0035566C`..`DAT_00355680`; the light
direction is `(1, 0, -1)` normalised and the colour is `0x505050`.

Fog is per-primitive, through the PRIM word's FGE bit: `FUN_00211230:131` and
`FUN_0020a2c0:499` choose `0x2D` (FGE set) or `0x0D` (clear), and the rule is
**fog on only when the fog start is below 5.0**, with `flags & 0x8000` forcing
it off. At the default 32.0 draw distance the fog start is 8.0, so a stock map
renders unfogged.

## PSM2 record layout, corrected

`FUN_0022b5a8:184-245` parses section D as 16 `ushort`s per record:

| word | goes to | meaning |
|---|---|---|
| w0..w3 | `0x78 +0x08`, `0x80 +0x24` | corner indices |
| w4 | `0x78 +0x00`, `0x80 +0x70` | flags, **zero-extended**; the high half is runtime-only |
| w5 | `0x80 +0x10` | colour palette index |
| w6..w9 | `0x80 +0x30/+0x3C/+0x48/+0x54` | four material slot selectors |
| w10, w11 | `0x78 +0x04` | terrain flags (32 bit) |
| w12 | `0x78 +0x10` | section A selector |
| w13 | `0x78 +0x12`, `+0x13` | two bytes |
| w14 | `0x80 +0x2C`, `+0x2D` | blend parameter, static alpha |
| w15 | `0x80 +0x0C` | **section B index**; `sectionB[w15]` is copied to `0x80 +0x00` as the face normal |

### The flag word at 0x80 `+0x70`

| bit | set by | meaning |
|---|---|---|
| `0x1` | file (w4) | **two-sided** -- `FUN_00211230:190` writes it to the VU1 parameter block as a byte of its own (`packet + 9`), beside the vertex count and material params |
| `0x4` | file | per-vertex normals and colours rather than one shared entry |
| `0x20` | file, or `FUN_00211230:68` | hidden; the builder also sets it on any primitive that turned out to have no usable material slot |
| `0x40` | `FUN_00211230:146-164` | alpha blended -- set when *any* slot has `flags & 0x70`, at the same point the PRIM word gets its ABE bit. `FUN_00209140` reads it as "never fade" |
| `0x80` | file | billboard; `FUN_00209b20` spins the corners about their own XY centroid to face the camera |
| `0x100` | file | ceiling / downward facing (`FUN_00227840`) |
| `0x2000` | file | written to the VU1 block at `packet + 15` |
| `0x4000` | file | three corners rather than four |
| `0x8000` | file | fog off for this primitive |
| `0x20000` | `FUN_0022c3d8` | slot 0 asked for a blend; pairs with the `+0x78` term |
| `0x40000` | `FUN_00211230` | positions are supplied per frame instead of being baked into the packet |

The two-sided bit is worth calling out because the data makes it unambiguous.
On `s01_e024`, exactly **32 of 1630** primitives carry `0x1`, and they are
exactly 16 coincident perpendicular pairs -- the four hanging chains at
(±0.45, ±4.30), four vertical segments each, built as crossed planes. Nothing
else in the map sets it. Backface culling those makes each plane disappear from
one side.

Header word `0x10` is a colour palette: an s16 count then that many 3-byte
entries, staged at `DAT_00355BDC` with the count in `DAT_00355BE0`.

`FUN_0022c3d8` expands each slot selector:

- `>= 0` -- copy 12 bytes from the section E record (0x10 stride in memory,
  12 on disc), then remap type `0x0F` to `0x09` and alpha `0xFF` to `0x80`,
  else halve the alpha. Slot 0 with `flags & 0x70` sets the `0x20000` bit and
  writes `-2.0` or `-0.5` into `+0x78`.
- `== -1` -- no texture, flat `0x404040`.
- any other negative -- no texture, colour from palette entry
  `selector & 0x7FFF`.

Vertex colours come from the same palette: one shared entry, or four
consecutive entries when `flags & 0x4`.

## Winding

`FUN_0022caf8` computes `(v1 - v0) x (v2 - v0)` per triangle -- for a quad, the
halves are `(3, 0, 1)` and `(1, 2, 3)`. `FUN_0022cbd8` computes the normalised
`(v1 - v0) x (v2 - v1)` and the slope angle `pi/2 - atan2(nz, hypot(nx, ny))`
that `FUN_0022d258` tests for walkability.

The corner order is what fixes the winding, and the map's own data confirms it:
on `s01_e024`, **509 of 509** primitives carrying the `0x100` ceiling bit
(`FUN_00227840`'s downward-facing marker) have a plane normal pointing down,
and no non-ceiling primitive points down. `--render-report` prints that count.

The GS has no backface culling hardware; the original culls in the VU1 program
at `0xE0`. The winding is fully determined by the data above, so a host
renderer can reproduce it.
