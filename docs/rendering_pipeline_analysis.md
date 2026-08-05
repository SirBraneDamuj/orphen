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

## Scene lighting: why the port renders untinted

`s01_e024` reads distinctly blue-grey and dim in game; the port renders the same
geometry at full "true" colour. The cause is that the scene's two light colours
are parsed and then unused.

```
DAT_0035566c (light 1) = 0x708090   R=112 G=128 B=144   blue-grey
DAT_00355670 (light 2) = 0x2050A0   R=32  G=80  B=160   strongly blue
```

`FUN_0022a360` defaults them to `0x606060` / `0xffffff` -- neutral ambient plus a
white key -- so an unlit render looks exactly like a scene using the defaults,
which is why nothing looks obviously broken.

**The model, measured rather than assumed.** Sampling the PCSX2 save-state
screenshot and normalising each surface to its green channel:

```
surface              R/G     B/G
left wall           0.849   1.126
distant arch        0.846   1.119
right wall          0.699   1.181
ceiling             0.762   1.261
floor centre        0.597   1.316

light 1 0x708090    0.875   1.125
light 2 0x2050A0    0.400   2.000
```

Every surface falls between the two lights and slides from light 1 toward
light 2 as it turns to face the key. Surfaces edge-on to the key sit almost
exactly on light 1. So light 1 is the constant (ambient) term and light 2 the
directional one, and the spread across surfaces is the `N.L` factor:

```
out = base * (light1 + clamp(dot(N, L)) * light2)
```

> Superseded by the microcode -- see "The combining function" below. The shape
> is right and the identification of light 1 as ambient and light 2 as
> directional holds, but the diffuse term is half-Lambert rather than a clamp,
> and the channel-scale argument two paragraphs down conflates two separate
> divisions. Kept because it is how the lights were first identified.

**Channel scale is 0..255, not 0..128.** The left wall at RGB (57.4, 67.5, 76.0)
divided by light 1 at /255 = (0.44, 0.50, 0.56) back-solves to a base texel of
(130, 136, 136), a plausible neutral stone. At /128 the base would have to be
(66, 68, 68), far too dark for that texture, and the frame would not be dim --
a /128 light averages to a multiplier near 1.0.

**Direction.** `FUN_00200e38:121-167` builds the VU packet: negated light
directions from `DAT_003439c8` and `DAT_00343a08`, then each colour written as
three separate bytes into the high lanes of a float slot (`+8/+9/+10` = R/G/B,
`+11` = 0). `FUN_00216510` normalises the direction through `_vcallms(0x20)`.
Script opcodes `0x96` (light 1), `0x97`/`0x98` (light 2 + direction) and `0x99`
(second direction) set them, so a scene can change lighting partway through --
`s03_e001` does exactly that, cool outdoors then warm indoors.

**Confirmed against other scenes.** `s05_e021`-`e026` carry light 1 = `0x961414`
and read visibly red in game; `s05_e016` has light 1 = `0x000000` with a warm
`0xC88060` key and reads as warm-lit with black shadow.

### The combining function, read out of the microprogram

The measurements above got the shape right but could not settle the details.
They are superseded by the microcode, disassembled from the PCSX2 save state's
`vu1MicroMem.bin` with `scripts/vu_disasm.py`.

**Locating the right program.** VU1 micro memory is 2048 instructions with nine
E-bit program ends, at `000f 0079 0082 00b7 00e5 01fa 01fe 0226 0228`. The EE
carries nine MSCAL VIFcodes -- `0x11 0x15 0x7b 0x84 0xb9 0x12c 0x13b 0x14b
0x228` -- and each one lands two instructions after an end, which is what an
E-bit plus its two trailing slots produces. That correspondence comes from two
independent sources and is what confirms the disassembly is framed correctly.

Entries `0x12c`, `0x13b` and `0x14b` share one body. `0x14b` is used by
`FUN_0020a2c0` and `FUN_00211230` (map geometry) and `0x12c` by `FUN_00212058`
(PSC3 entity geometry), so **map and entity lighting are the same code**.

**The vertex loop, VU1 `0x01b2`..`0x01e0`:**

```
i_k  = max((dot(n, L_k) + 1) * 0.5, floor)          k = 0..3
out  = min(colour/256 * (ambient + sum_k C_k * i_k), 255)
```

`n` is the vertex normal (`packed * 1/128`, optionally rotated by the bone
matrix when the draw header flags the model as skinned). The `+1` is `ADDw` on
`vf00w`, and the `0.5` is `vf01.y` -- so the diffuse term is **half-Lambert, not
a clamped `N.L`**, which is the one thing the screenshot fit could not have
distinguished. `vf01` is `LQ.xyzw vf01, 28(vi00)` at instruction `0x0000` and
reads `(1/256, 0.5, 1/320, 0.01)`.

**Where the constants live**, and what the save state has in them for `s01_e024`:

```
0x2a0..0x2a2  light directions, as matrix rows   (-0.7071, 0, 0.7071) in slot 0
0x2a3         light 0 colour                     (32, 80, 160)   = 0x2050A0
0x2a4..0x2a6  lights 1..3 colour                 zero
0x2a7         ambient                            (112, 128, 144) = 0x708090
0x0014        output saturation                  (255, 255, 255)
0x001c        vf01                               (1/256, 0.5, 1/320, 0.01)
```

The colours match `DAT_0035566c` / `DAT_00355670` exactly, which is what ties
the microprogram back to the scene block.

**Byte-valued, not normalised.** `FUN_00200e38:121-167` uploads the colours with
VIFcode `0x6e0542a3` -- UNPACK V4-8 **unsigned**, five quadwords -- and the VU's
own init program at `0x0000`..`0x000f` converts them in place with `ITOF0`. So
`0x708090` arrives as the floats `(112, 128, 144)`; there is no `/255` anywhere
in the microprogram. The earlier "/255 not /128" measurement was reading the
combined effect of the `/256` on the vertex colour and the GS's `/128` modulate.

**Reproducing it in GL.** The VU's output is a GS vertex colour, which MODULATE
applies as `texel * out / 128`. The port's draw paths already divide their
vertex colours by 128, so what they multiply in is exactly
`(ambient + sum_k C_k * i_k) / 256`. That is `SceneLighting::modulator` in
`port/src/ported/render/scene_lighting.h`.

**One caveat.** The GS allows the modulate factor to reach `255/128`, just under
2x. Fixed-function GL clamps `glColor` to 1.0, so surfaces the game renders
overbright flatten instead. `GL_COMBINE` with `GL_RGB_SCALE = 2` would restore
it; the port does not do this yet.

### The VU input streams, from the VIFcodes

`FUN_00212058` writes one UNPACK per vertex attribute. Reading the CMD field
(`0x60 | vn<<2 | vl`, plus bit 15 = TOPS-relative and bit 14 = unsigned) against
the register each stream is read into by the microprogram pins every attribute's
element count, width and signedness:

```
VIFcode      format          addr   read by                  attribute
0x6e03c000   V4-8  unsigned  +0x00  ILWR/LQ vf15 (vi01)      draw header, 3 qw
0x6d008006   V4-16 signed    +0x06  LQI vf21 (vi14++)        position
0x6e00801a   V4-8  SIGNED    +0x1a  LQI vf16 (vi08++)        normal
0x6e00c024   V4-8  unsigned  +0x24  LQI vf17 (vi04++)        colour
0x6600c039   V2-8  unsigned  +0x39  LQI vf23 (vi05++)        2 bytes per vertex
```

Two of these settle open questions:

- **The normal stream is V4-8 signed.** Combined with the microprogram's
  `* 1/128`, that is `s8 / 128` -> `[-1, +0.992]`, a genuine unit normal, and
  its `.w` lane is the bone index `MTIR` reads. The normal decode was not
  previously grounded in a width.
- **The colour stream is V4-8 unsigned**, so vertex colours really are 0..255
  bytes, not the 0..128 the port's `/128` divisor might suggest. The 128 is the
  GS's MODULATE convention, a separate thing that happens later.

### Per-vertex alpha, and why the fog curve is right

`0x0112`..`0x0129` is **alpha**, not fog -- it writes `vf17.w`, and `vf17` is the
RGBA the loop stores through `SQI.xyzw vf17, (vi07++)`. Two modes, selected by
the counter at `46(vi01)`:

```
0113  LQ vf25, -21(vi08)        the vertex position
0115  SUB.xyz vf25, vf25, vf26  minus the reference point at qw 26
0116  ELENG P, vf25             P = |vf25.xyz|
0117  WAITP
0118  MFP.w vf25, P
0119  SUB.w vf25, vf26, vf25    (reference.w - distance)
011a  MULi.w vf25, vf25, I      * 16
011b  MAX.w  vf25, vf25, vf00   clamped to [0, 128]
011c  MINIi.w vf25, vf25, I
```

so one mode is a **linear-in-distance** fade, and the other (`0x0121`..`0x0129`)
dots the stored world normal against a vector at qw 27 and takes `ABS` -- a
facing-based fade.

Neither is the fog. **The fog is GS hardware**, set up on the EE at
`FUN_00200e38:66-67`:

```
DAT_003147fc = (near * 255 * far) / (far - near)      the A coefficient
DAT_003147f0 = (far * 0 - near * 255) / (far - near)  the B coefficient
```

The GS evaluates `F = A/z + B`, which gives `F = 255` at the near edge and `0`
at the far edge, linear in `1/z`. Working `1 - F/255` through algebraically:

```
1 - F/255  =  f * (1 - n/z) / (f - n)  =  (1/n - 1/z) / (1/n - 1/f)
```

which is exactly the curve `emitFogCoord` supplies. **The port's fog is the
hardware's fog**, not an approximation -- previously it was believed right but
only argued from the shape of the coefficients.

### Backface culling is in VU1, and it is gated

MAC flag layout, from `pcsx2/VUops.cpp:44-79`: the sign nibble is `0x0010 <<
shift` with shift 3/2/1/0 for x/y/z/w, so `Sx=0x80 Sy=0x40 Sz=0x20 Sw=0x10`.
That decodes the two flag tests in the microprogram:

```
016f..0177  two SUBs, FMOR accumulated, IAND 0xe0 (= Sx|Sy|Sz), IBNE -> reject
            a bounding-extent reject: any of x/y/z negative and the draw dies

0181  OPMULA.xyz ACC,  vf28, vf29     cross of two edge vectors,
0182  OPMSUB.xyz vf00, vf29, vf28     discarded into vf00 -- flags only
0183  IBNE vi05, vi00 -> 018f         header[1].y non-zero: skip the test
0186  FMAND vi14, vi09  (vi09 = 0x20) sign of the cross product's Z
0188  IBEQ vi14, vi00 -> 018f         Z >= 0: front-facing, accept
018a  IBEQ vi10, vi06  (vi06 = 3)     3 vertices: no second half, reject
018b  FMAND vi14, vi09                sign of the second half's cross
018d  IBNE vi14, vi00 -> 01fc         also backfacing: reject
```

The projected position carries `.z = 1.0` (`ADDw.z vf23, vf00, vf00w`), so the
cross product's Z is the 2D signed area -- this is a **screen-space winding
test**, run after transform, on the draw's first three or four vertices, and
rejecting only when both halves of a quad fail.

**This does not mean the port should simply enable `GL_CULL_FACE`.** The
observation behind `map_viewer.cpp:576` still holds: assets genuinely ship with
primitives whose winding opposes their stored normal, and culling them all
produced holes. What the microcode adds is that the test is **conditional** --
`IBNE vi05, vi00` skips it entirely when `header[1].y` is non-zero, which is a
per-draw two-sided flag the port does not read. So the promising change is to
read that flag and cull only when it is clear, not to cull unconditionally.
What the EE writes into `header[1].y` has not been traced yet, so this is a
lead rather than a finding.

### Open

- **The intensity floor.** `MAXz.xyzw vf16, vf16, vf15z` floors every intensity
  at `itof(header[2].z) * 1/320`. The header is VIFcode `0x6e03c000` -- UNPACK
  V4-8 unsigned, three quadwords -- so that is a 0..255 byte and the floor spans
  0..0.797. `FUN_00212058:228` writes it as `~*(byte *)(iVar19 + 0xd)`, the
  complement of a per-draw record byte; byte 13 gets the uncomplemented value
  and lands in `vf15.y`. What the source byte means is the remaining unknown.
  The port leaves the floor at zero, which is what a source byte of 255 gives.
- **The second additive term.** When draw header `[1].w` is non-zero, VU1
  `0x01da`..`0x01e0` adds `itof(extra) * colour * 1/128` from a second
  per-vertex stream at `20(vi08)` before the saturation clamp. Unported.
- **Lights 1..3.** The upload path only ever writes a direction into slot 0 and
  zero into the other three colours, so the remaining capacity is unused in
  every scene observed. The port implements all four anyway, since that is what
  the hardware does.
