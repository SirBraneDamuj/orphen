# The VU1 microprogram

Everything the renderer does between "the EE hands VIF1 a DMA chain" and "the GS
gets a packet" happens here. This document is the reference for that stage, so
that a rendering question never has to stop at *that part runs on VU1*.

Nothing below is inferred from screenshots. It comes from disassembling the
microprogram, reading the packet builders in `src/`, and checking both against a
PCSX2 save state.

## Reproducing this

```sh
# The save state is a zip.
python - <<'PY'
import zipfile
z = zipfile.ZipFile('../orph.p2s')
for n in ('vu1MicroMem.bin', 'vu1Memory.bin', 'vu0MicroMem.bin', 'vu0Memory.bin'):
    open(n, 'wb').write(z.read(n))
PY

python scripts/vu_disasm.py vu1MicroMem.bin --map              # E-bits and branch targets
python scripts/vu_disasm.py vu1MicroMem.bin --start 0 --end 0x29b
python scripts/vu_disasm.py vu1MicroMem.bin --entry 0x12c      # reachability from one entry
```

`scripts/vu_disasm.py`'s opcode tables were checked element by element against
PCSX2's `pcsx2/x86/microVU_Tables.inl`. Two entries were wrong before that check
and are called out in the script's docstring; if a decode ever looks like
nonsense, suspect the tables again before suspecting the game.

## Extent and entry points

Real code occupies instructions **0x000..0x29a**. Everything above that is one
repeated filler word (`080002ff 8000033c`).

The EE selects a program with a VIFcode `MSCAL` (CMD 0x14), whose immediate is
the instruction index. Nine of them appear in the decompilation:

| Entry | Written by | What it is |
|---|---|---|
| `0x011` | `FUN_002020a8:111` | 3-instruction stub: set `vi02`/`vi03` to the "no cached tag" sentinel, then jump to the empty-kick at `0x1fc`. Resets state between frames. |
| `0x015` | `FUN_00212058:76` | Per-entity setup: transform and orthonormalise the bone matrices, install the entity's three extra lights, seed both double-buffer halves. |
| `0x07b` | `FUN_00212058:354` | Per-entity teardown: clear lights 1..3 back to black. |
| `0x084` | `FUN_00207938:176`, `FUN_00207de8:242` | Sprite / billboard draw. |
| `0x0b9` | `FUN_0020f510:355` | Sprite draw with per-vertex colour (the pass `FUN_0020f3e0` drives). |
| `0x12c` | `FUN_00212058:338` | **Entity geometry.** Converts the ITOF12 fixed-point header block, then falls into the shared loop. |
| `0x13b` | `FUN_00211230:341` | Map geometry, "copy to the other buffer" variant. |
| `0x14b` | `FUN_0020a2c0:744`, `FUN_00211230:105,344` | **Map geometry.** Falls into the shared loop. |
| `0x228` | `FUN_00211b80:176` | Object geometry with its own 4x4 at `0x27d..0x280` (`FUN_0020c2f0` uploads it). |

Program ends (`E` bit, plus its two-instruction tail) sit at
`0x00f 0x079 0x082 0x0b7 0x0e5 0x1fa 0x1fe 0x226 0x295 0x299`. Note the trick:
several programs place the `E` so that the tail *overlaps the first two
instructions of the next entry point*, which is why `0x011`, `0x07b`, `0x084`,
`0x0b9` and `0x228` are each exactly two past an `E`. That is code-size
economy, not a bug, and it is why the entry list and the `E` list line up — two
independent sources agreeing.

## Data memory map

Absolute quadword addresses. `TOPS` is the VIF1 double-buffer base; in the save
state the two halves are **0x300** and **0x380** (`OFFSET` = 0x80), which is
directly visible as two mirrored occupied regions in `vu1Memory.bin`.

| Address | Uploaded by | Contents |
|---|---|---|
| `0x000..0x007` | `FUN_00200e38` `0x6c080000` | Two 4x4 matrices: world→view and the projection used by `0x0e7`. |
| `0x010..0x01c` | `FUN_00200e38` `0x6c0d0010` | 13 constants (below). |
| `0x020..0x0??` | program `0x015` | Bone matrices after transform + orthonormalisation. |
| `0x0e0..` | `FUN_0020eec0` `0x6c0000e0` | Raw bone matrices, `NUM` = bone count. |
| `0x0c0..` | (via `0x020`) | Where the vertex loop reads a bone matrix: `LQ 192(vi13)`, three quadwords, `vi13` = the normal's `.w`. |
| `0x1a0`, `0x1df`, `0x21f`, `0x260` | GIF register blocks | Indexed by header bytes 5/6/7 and 8. |
| `0x278` | `FUN_0020eec0` `0x6e054278` | `.x` = bone count (`ctx+0x1E8`), `.y` = `ctx+0x1EA + 1`, `.z` = `ctx+0x1FC` (0 if >= 0x7f). |
| `0x279` | same | `ctx+0x1BC` — the per-draw additive tint, see the second additive term. |
| `0x27a..0x27c` | same | Light colours 1..3, bytes, from `ctx+0xBC / +0xCC / +0xDC`. |
| `0x27d..0x27f` | `FUN_0020eec0` `0x6803027d` | Light directions 1..3, floats, from `ctx+0xB0 / +0xC0 / +0xD0`. **Also** reused as a 4x4 by program `0x228` (`FUN_0020c2f0` `0x6c04027d`). |
| `0x2a0..0x2a2` | `FUN_00200e38` `0x6c0302a0` (lane x only) + program `0x015` (lanes y/z/w) | The light direction matrix. Its **columns** are the four directions. |
| `0x2a3..0x2a7` | `FUN_00200e38` `0x6e0542a3` | Light colours 0..3 and the ambient, as bytes; the init program ITOF0s them in place. |
| `0x2b0..0x2b6` | `FUN_0020eec0` / `FUN_0020c2f0` `0x640702b0` | Seven quadwords of V2-32, a secondary transform. |
| `0x32e..0x332`, `0x3ae..0x3b2` | program `0x015` | `TOPS+0x2e` and `TOPS+0x2f..0x32` written into **both** halves. |

### The constants at 0x010..0x01c

Read from the save state, and the reason several "magic numbers" in the port are
not magic:

| Addr | Value | Used for |
|---|---|---|
| `0x010` / `0x011` | `(-131072, -131072, -65536, 0)` / `(+131072, +131072, +65536, 0)` | Clip bounds; `vf30`/`vf31` accumulate the draw's min/max against them. |
| `0x012` / `0x013` | `(27648, 30976, 1, 0)` / `(37888, 34560, 65534, 0)` | Screen clip window in GS 12.4 coordinates. |
| `0x014` | `(255, 255, 255, 1/256)` | The `MINI` clamp on the lit colour. |
| `0x015` / `0x016` | `(16, 16, 1, 0)` / `(65520, 65520, 65534, 0)` | Second clip window. |
| `0x017` | `(-425, 5, 8, 3400)` | `(B, near, far, A)` of the entity alpha fade, `A/z + B`. |
| `0x018` | `(0.820074, 0.528442, 0.219608, 0)` | **The specular half-vector**, rebuilt every frame. Unit length; not a constant. |
| `0x019` | GIF tag | The one-quadword dummy packet kicked at `0x1fc` when a draw is rejected. |
| `0x01a` | `(-7.115, 2.672, -0.1285, 8)` | Point + radius for the distance alpha at `0x112`. |
| `0x01b` | `(-0.857, -0.301, -0.419, 1)` | Direction for the rim alpha at `0x121`. |
| **`0x01c`** | **`(1/256, 0.5, 1/320, 0.01)`** | **`vf01`.** `.x` scales colours, `.y` is the half-Lambert 0.5, `.z` scales the light floor, `.w` is the minimum `w`. |

## The draw header

Both geometry builders emit the same 12 bytes so the shared loop can serve
either. `FUN_00212058:170` uses `UNPACK V4-8 unsigned, NUM=3` to address 0;
`FUN_00211230:170` uses `NUM=4`. Byte 0..3 of the packet is the VIFcode itself,
so **packet byte `4+j` becomes data byte `j`**, i.e. quadword `j/4` lane `j%4`.

| Packet byte | Lane | Entity (`FUN_00212058`) | Map (`FUN_00211230`) | Read at | Meaning |
|---|---|---|---|---|---|
| 4 | qw0.x | `*(char*)(scratch+0x17C)` | `plVar8[7]` | `ILWR.x vi10` | Vertex count, the loop counter. |
| 5..7 | qw0.y/z/w | GIF/texture selectors | same | `0x08f`, `0x181`, `0x18f` | Tag cache indices; `qw0.z == 0x3e` diverts to the environment-map path at `0x0e7`. |
| 8 | qw1.x | `plVar5[6] * 3` | `plVar8[6] * 3` | `ILW.x vi13, 1(vi01)` | Index (pre-tripled) into the register block at `608`. |
| **9** | qw1.y | primitive flags **bit 0** | `pfVar27[0x1C]` **bit 0** | `ILW.y vi05, 1(vi01)` at `0x181` | **Two-sided.** Non-zero disables the winding test. |
| **10** | qw1.z | constant **1** | constant **0** | `ILW.z vi13, 1(vi01)` at `0x1bb` | **Skinning.** Entities always rotate the normal by the bone matrix; the map never does. |
| **11** | qw1.w | constant **1** | constant **1** | `ILW.w vi13, 1(vi01)` at `0x1d4` | **Second additive term** enable. |
| **12** | qw2.x | primitive `+0x0C`, or 0 | `plVar8[0x4D]`, or 0 | `ILW.x vi11, 2(vi01)` at `0x1f5` | **Specular strength.** Non-zero appends the pass at `0x200`. |
| **13** | qw2.y | primitive `+0x0D` | `pfVar27[0x2D]` | `vf15.y` | **Specular threshold**, `/256`. Same byte as the light floor below. |
| **14** | qw2.z | `~`primitive `+0x0D` | `~pfVar27[0x2D]` | `vf15.z` at `0x1d1` | **Light floor**, scaled by `vf01.z` = 1/320. |
| **15** | qw2.w | primitive flags **bit 8** | `pfVar27[0x1C]` **bit 13** | `ILW.w vi12, 2(vi01)` at `0x1b8` | **Unlit.** Non-zero branches past the whole lighting block. |

Per-vertex streams, all `TOPS`-relative, one quadword per vertex:

| Offset | Entity VIFcode | Map VIFcode | Contents |
|---|---|---|---|
| `+0x06` | `0x6d008006` (V4-16 signed) | `0x6c008006` (V4-32 float) | Position; `.w` is the bone matrix address. |
| `+0x10` | — | — | Loop output: transformed position. |
| `+0x1a` | `0x6e00801a` (V4-8 **signed**) | `0x6e00801a` | Normal, `float * 126` per component, decoded as `s8/128`; `.w` is overwritten by the first loop with the resolved matrix address. |
| `+0x24` | `0x6e00c024` (V4-8 unsigned) | `0x6e00c024` | Vertex colour, 0..255. |
| `+0x2e` | — | `0x6e00c02e`, `NUM = n+1` (`FUN_0020a2c0:667`, `FUN_00209140:243`) | `.x` of `+0x2e` is a mode flag; `+0x2f..` is the second additive term's per-vertex source. |
| `+0x39` | `0x6600c039` (V2-8) | `0x6600c039` / `0x6e00c039` | Texture coordinates. |
| `+0x43` | — | — | Output GIF packet, `XGKICK`ed from here. |

## The shared vertex path

`0x12c`, `0x13b` and `0x14b` all converge, which is why one lighting model
covers both geometry paths.

**First loop, `0x152..0x16e`** — per vertex: resolve the bone matrix address
from the position's `.w`, transform the position, `MAX.w` against `vf01.w`
(0.01) so nothing divides by zero, `DIV Q, vf00w, vf22w`, scale, accumulate the
draw's min/max into `vf30`/`vf31`, and `ISW.w vi08, 19(vi14)` — stash the
resolved matrix address into the normal quadword's `.w` for the second loop.

**Clip and cull, `0x16f..0x18e`** — two `FMOR`/`IAND` tests against the clip
windows reject the whole draw to `0x1fc` (which kicks the dummy packet at
`0x019`). Then `OPMULA`/`OPMSUB` take a cross product of two screen-space edges.
Because the projected `.z` is 1.0, the cross product's Z is the 2D signed area,
so `FMAND vi14, vi09` with `vi09 = 0x20` tests **`Sz`, the sign flag of lane z**
(confirmed from `pcsx2/VUops.cpp:44-79`: sign flags are `0x0010 << shift`, shift
3/2/1/0 for x/y/z/w). This is genuine per-polygon backface culling by screen
winding, and header byte 9 skips it.

**Second loop, `0x1b2..0x1f3`** — per vertex: lighting (below), then
`0x1e1..0x1e7` computes the alpha as `min(colour.w, 255 * clamp(header[4].w + ...))`,
`0x1eb` optionally diverts to a distance or rim alpha, and the results are
stored to the output packet. `XGKICK vi09` at `0x1f6` sends it.

### Lighting, 0x1b2..0x1e0

```
i_k  = max((dot(n, L_k) + 1) * 0.5, floor)          k = 0..3
out  = min(colour/256 * (ambient + sum_k C_k * i_k) + extra, 255)
```

- The diffuse term is **half-Lambert**, not a clamped `N·L`. A screenshot fit
  cannot distinguish the two; the microprogram can.
- `L_k` is column `k` of `0x2a0..0x2a2`, already negated by the EE.
- `C_k` is `0x2a3+k`, and the ambient is `0x2a7`, all in 0..255 byte units.
  There is no `/255` anywhere; the `/128` people expect is the GS's own
  `MODULATE` (`final = texel * vertex / 128`), applied after this.
- `floor` is `~header_byte_13 / 320`.

**Light 0 and the ambient are the scene's**, uploaded once per frame by
`FUN_00200e38:121-167` from `DAT_003439c8..d0` (direction, negated) and the two
packed RGB globals. Verified against the save state:

| Field | EE | VU1 | Match |
|---|---|---|---|
| ambient | `0x35566C = 0x000A1E32` | `0x2a7 = (10, 30, 50)` | ✓ |
| light 0 colour | `0x355670 = 0x001E0F05` | `0x2a3 = (30, 15, 5)` | ✓ |
| light 0 direction | `0x3439C8.. = (-0.7071, -0.7071, 0)` | `0x2a0..2 lane x = (0.7071, 0.7071, -0)` | ✓ (negated on upload) |

So the u32 at `0x35566C` reads `0xRRGGBB`, and `unpack()` in
`port/src/ported/render/scene_lighting.h` is right.

**Lights 1..3 are per draw.** `FUN_0020eec0:112-142` uploads the block at
`ctx+0xB0` — three records of `{float3 direction, u32 rgb}` — and program
`0x015` transposes the directions into lanes y/z/w and ITOF0s the colours into
`0x2a4..0x2a6`. Program `0x07b`, appended after every entity, clears them again,
which is exactly why a save state shows `0x2a4..0x2a6 = (0,0,0,1)` — `vf00`.
Note the colour byte order differs from the scene globals: the entity's u32 is
copied verbatim, so its **low** byte is red.

### The light floor is an authored shading-strength knob

Header byte 14 is the bitwise complement of a per-primitive byte, and `vf01.z`
is 1/320, so a source byte of 255 means no floor and 0 means 0.797. The values
that occur are a clean ladder:

| Source byte | Floor | Entity primitives (19 models) |
|---|---|---|
| `0x00` | 0.797 | 3066 |
| `0xBF` | 0.2 | 1587 |
| `0x7F` | 0.4 | 704 |
| `0x3F` | 0.6 | 400 |
| `0xDF` | 0.1 | 238 |
| `0xEF` | 0.05 | 114 |
| `0x5F` | 0.5 | 74 |

s01_e024's map is almost uniformly 0.2 (`--render-report` prints
`light floor: 0.2..0.796875 mean 0.200366 over 1630 primitives`). Read as a
scalar this looked like nonsense, which is why an earlier pass retracted it; the
control-flow trace settles it. `iVar19` has three assignments in `FUN_00212058`,
and the one live at line 227/228 is **line 81**,
`*(int*)(plVar5+0x1C) + i*0x18` — the PSC3 primitive table at `psc3 + psc3[0x1C]`,
stride 0x18, byte `+0x0D`. Lines 345/349 only refresh the loop bound at the
bottom of the same iteration.

### The second additive term, 0x1da..0x1e0

Enabled by header byte 11, which **both** builders hardcode to 1, so it always
runs:

```
extra = mem[TOPS + 0x2f + i].xyz  (ITOF0)  *  colour  /  128
```

Two different producers fill that region:

- **Map draws.** `FUN_0020a2c0:667` / `FUN_00209140:243` unpack `NUM = n+1`
  quadwords at `TOPS+0x2e`, so the per-vertex data lands at `0x2f..`. The bytes
  come from `_ppacb` of a **VU0** computation: `_vcallms(0xe0)` (byte address, so
  VU0 instruction 0x1c) transforms the vertices, then `0x33..0x41` does
  `RSQRT`-attenuated point lighting against a light record and scales to 0..255.
  This is the map's dynamic point-light contribution — the part of the lighting
  that moves.
- **Entity draws.** No builder unpacks there; instead program `0x015` writes
  `mem[0x279]` into `TOPS+0x2f..0x32` of **both** buffer halves
  (`SQ vf20, 815(vi00)` and `SQ vf20, 943(vi00)`), i.e. `ctx+0x1BC`
  broadcast to every vertex. A per-entity additive tint — a flash or glow. It is
  zero in every scene examined here.

## The other paths

- **`0x0e7`, environment mapping.** Entered from the vertex loop at `0x166` when
  header byte 6 equals `0x3e`. Transforms the normal, squares it, and adds a
  scaled screen position; three constant sets (`30800/32500`, `32768/32768`,
  `31000/32000` with scales `2e-4`, `1e-4`, `1e-5`) are selected by
  `mem[0x01b].w`. The result replaces the texture coordinate — a screen-space
  reflection/shine map.
- **`0x112` and `0x121`, alternate alphas.** Selected by `mem[TOPS+0x2e].x`:
  0 gives a rim alpha, `|dot(normal, mem[0x01b].xyz)| * 128`; 1 or 2 give a
  distance alpha, `clamp((8 - |pos - mem[0x01a].xyz|) * 16, 0, 128)`.
- **`0x200`, the specular pass.** A second directional light, and the sheen on
  the treasure chests. Its own section below.
- **`0x084` / `0x0b9`, sprites.** Header at `TOPS+3`, matrices at `416`/`479`/`543`,
  UV in `ITOF4`/`FTOI4` fixed point. `0x0b9` additionally reads per-vertex colour
  from `TOPS+0x24`.
- **`0x228`.** Reads `0x27d..0x280` as a 4x4 model matrix, transforms, divides
  and emits. Note the aliasing: those quadwords are the entity light directions
  in the other paths.

## VU0, and where the dynamic lights come from

VU0's micro memory holds **126 instructions**, `0x000..0x07d`, and the whole
thing decodes. The `_vcallms` immediate is a byte address, so divide by 8 for
the instruction index -- confirmed independently by the same tail-overlap trick
VU1 uses: three of VU0's five `E` bits end exactly on an entry point.

| `_vcallms` | Instruction | Called from | What it does |
|---|---|---|---|
| — | `0x00` | (no call site found) | Cross product of three points, then normalise: a face normal. |
| `0x20` | `0x04` | `FUN_00216510` | Normalise `vf24`. This is the one the port already reproduces in script opcode 0x97. |
| `0x60` | `0x0c` | `FUN_0020bb58` and 24 others | 4x4 matrix multiply. |
| `0xE0` | `0x1c` | `FUN_00209140:205`, `FUN_0020a2c0` | Transform `vi08` vertices by `vf10..vf13`, accumulate a view-space AABB into VU0 memory 0 and 1, then run the point-light loop. |
| `0x198` | `0x33` | `FUN_0020eec0:81` | **One** light against one point, returning a normalised direction and an attenuated colour. |
| `0x220` | `0x44` | `FUN_0020eec0:48` | Point-light loop over the lights *after* the first `n`. |
| `0x250` | `0x4a` | `FUN_0020f510` | Point-light loop from the start of the list (sprites). |

### The point-light loop, 0x52..0x79

Per vertex, over a light list at VU0 quadword `vi12` with the count in
`mem[2].x`, stride three quadwords `{position, (r, r², 1/r²), colour}`:

```
reject unless |light - vertex| is inside the axis-aligned box of radius r
    -- two SUBx against vf21.x whose only purpose is to set MAC sign flags,
       tested with FMAND against 0xe0 = Sx|Sy|Sz
reject unless |d|² < r²          -- FMAND against 0x10 = Sw
accumulate  colour * clamp(1 - |d|² / r², 0, 1)
```

then `MINIi` the sum to **2.0**, `MULi` by **127.5**, and `FTOI0` to bytes. The
four results rotate through `vf16..vf19` so the EE reads back four quadwords.

That 2.0-to-255 encoding is the other half of VU1's second additive term, which
divides by 128. The two agree to within the byte quantisation, which is the
tightest confirmation available that the chain is read correctly end to end.

### `ctx+0xB0` and `ctx+0x1BC`, resolved

> **`ctx` is not the entity.** `FUN_0020c5a8:21` sets `iVar8 = DAT_70000000` --
> the EE scratchpad -- and passes it as `FUN_0020c810`'s *first* argument with
> the entity second; `FUN_0020c810:245` then calls `FUN_0020eec0(param_2,
> param_1)`, swapping them. So inside `FUN_0020eec0`, `param_1` is the entity
> (0x1D8 stride: `+0x138` bias, `+0x160` model record, `+0x168` submesh bytes)
> and `param_2` is a **per-draw scratchpad context** rebuilt every frame. The
> light block and the tint live in that context, not in the entity -- which is
> also why they read as zero in an entity-pool dump. The give-away is that
> `param_2 + 0x1E8` is past the end of an entity record.


`FUN_0020eec0` fills both, immediately before uploading them:

- **Lines 67-94.** For each of three entries in the global light table at
  `DAT_00343898` (stride 5 floats, first float zero means disabled), load the
  entity's position from `+0xA0`, put the light's record index (0, 3, 6 --
  matching VU0's three-quadword stride) into `vf20`, and `_vcallms(0x198)`.
  VU0 returns the **normalised direction** in `vf16` and the **attenuated
  colour** in `vf17`; the EE `_sqc2`s the direction to `ctx+0xB0+i*0x10` and
  `_ppacb`s the colour into that record's `+0x0C`. Disabled entries are zeroed
  outright. This is exactly the `{float3 direction, u32 rgb}` layout the VU1
  side needs -- producer and consumer now both traced.
- **Lines 44-64.** `_vcallms(0x220)` runs the same loop over *the rest* of the
  lights and packs the sum into `ctx+0x1BC`, then adds a per-entity bias from
  `param_1 + 0x138`, clamped at 255.

So the three nearest lights become directional lights 1..3 and shade the model,
and everything beyond them is summed flat into the additive term. One mechanism,
split by budget.

`FUN_0020b430:44-68` is where the light list is manufactured for VU0 -- note
lines 48-56 building exactly the `(r, r², 1/r²)` triple the loop reads. The
precise packing into VU0 memory (it writes three parallel runs at quadword
offsets 1, 0x11 and 0x21, which does not obviously match the stride-3 read) is
the one thing here still to pin down, and it is the first thing to check if the
dynamic lights are ever ported.

## `0x200`, the specular pass

`FUN_00212058:221-258` appends an entire extra GIF packet to a primitive's draw
whenever its `+0x0C` byte is non-zero and flag bit 8 is clear; `0x200` fills it.

```
alpha = max(0, (dot(N, H) - t) * Q) * vertexAlpha * 0.5        [GS units]
  t = primitive +0x0D / 256          the same byte as the light floor, reused
  Q = (primitive +0x0C / 256) / (1 - t)
```

`N` is the bone-rotated normal, which the main vertex loop wrote back to the
normal buffer at `0x1c9` precisely so this pass could read it. `vertexAlpha` is
the same 0..255 distance fade the main pass uses -- `A/z + B` built from
`fGpffffb70c`/`fGpffffb710`, which in s01_e024 is the same 8..32 band as the fog.

The GIF tag is `0x2026c00000008000 | vertexCount`: **triangle fan, gouraud, no
texture, ABE on, NREG 2 (RGBAQ + XYZF2)**. The GS state it selects is register
block 2, whose `ALPHA_1` is `0x48` -- `(Cs - 0) * As >> 7 + Cd`, pure additive --
with `ZBUF.ZMSK` set, so it tests depth and does not write it. `0x206` loads the
colour from `675` = `0x2a3`, so the highlight is always tinted by the scene's
directional light colour.

**`H` is a Blinn-Phong half-vector.** `mem[0x018]` is neither a constant nor the
luminance weights it resembles; `FUN_00200e38:55-66` rebuilds it every frame as

```
H = normalise(-(DAT_0058bea0 + DAT_003439c8))
```

`DAT_0058bea0` is `normalise(lookAtTarget - eye)`, the camera forward
(`FUN_00216aa0:436-449`, which derives `fGpffffb6d4`/`b6d8` from that same
vector), and `DAT_003439c8` is the scene light vector. Both point away from the
surface, so negating the sum gives `normalise(toEye + toLight)`. Verified in
`s01_e24.bin`: light `(0.7071, 0, -0.7071)`, camera forward
`(0.986399, 0, -0.164370)`, and `normalise(-(V+L))` reproduces
`DAT_00314800 = (-0.889174, -0, 0.457569)` exactly. It also reconstructs from
the camera angles alone -- `yaw 0`, `pitch -0.165120` -- to six decimals, which
is how the port builds it.

The threshold-and-rescale is a specular exponent by another name: it moves the
highlight's onset and renormalises so it still reaches full strength at
`dot == 1`. `grp_0172`, the chest, uses three settings across its 187
primitives:

| `+0x0C` | `+0x0D` | threshold | `Q` | alpha at `dot = 1` | count |
|---|---|---|---|---|---|
| 32 | 0 | 0.000 | 0.125 | 0.125 | 10 |
| 128 | 127 | 0.496 | 0.992 | 0.500 | 27 |
| 255 | 223 | 0.871 | 7.727 | 0.996 | 18 |

-- a broad faint sheen, a medium one, and a tight bright one that only fires
within about 30 degrees of the half-vector. The other 132 primitives have
`+0x0C = 0` and carry no highlight.

Note the consequence for `+0x0D`: one byte drives both the diffuse floor and the
specular threshold, so a primitive with a high floor (flat diffuse) gets a tight
highlight, and vice versa. That is a deliberate authoring pairing, not a
coincidence of packing.

## What the port implements

`port/src/ported/render/scene_lighting.h` plus the two draw sites in
`port/src/harness/map_viewer.cpp`.

**On by default**, confirmed against the real frame:

- Half-Lambert against four directions, the byte-valued colours and ambient, the
  `/256` and the 255 clamp.
- The two-sided flag and the screen-winding cull, on the map path.
- **Every subdraw pass, with its blend mode** taken from the `texFlags` nibble
  and mapped through the register blocks at `608 + mode*3`. Mode 1 at alpha
  `0x7F` folds back to opaque, per `FUN_00212058:141`. This is what makes
  `map_009f` -- the script-spawned chest in s01_e024 -- read correctly: 15 of
  its 35 passes are additive, including a second pass on each of the six lid
  primitives, and drawing only the opaque base left it visibly flat.
- Normals renormalised after the bone transform, matching micro-program `0x015`'s
  orthonormalisation of the palette at `0x0033..0x0055`. Without it the type
  `0x62` enemies rendered with normals about 0.35 long and were nearly unshaded.
- **The specular pass at `0x0200`.** Its presence is confirmed against the
  emulator -- without it the chests lose their sheen entirely.

  One unit trap here, worth knowing because it is not the convention the rest of
  the renderer uses: this pass runs with **TME off**, so there is no texture and
  the RGBAQ value *is* the fragment colour. It enters the blend unit as a plain
  8-bit level, where full scale is **255**, not the 128 that is 1.0 for texture
  modulation. Emitting it as `Cs/128` makes the highlight almost exactly twice
  as bright as the hardware and clips its blue channel. The additive subdraw
  passes are textured, so their modulate output is already a final colour and
  `/128` stays correct there.

**Behind flags, derived but not visually confirmed** -- `--lighting-floor`,
`--lighting-unlit`, or `--lighting-all`:

- The per-primitive light floor (`vf15.z`).
- The unlit flag (draw header byte 15 / primitive flag bit 8).

`--gleam-report` measures the specular pass: per model, how many primitives
reached it, how many corners lit, the maximum `dot(N, H)` and the range of `|N|`.
It needs a windowed run, since the probe fills from `render()`. `|N|` away from
1.0 means the bone transform is scaling, which inflates every dot product it
feeds -- that is how the enemy-normal bug above was found.

Not implemented, deliberately:

- **Lights 1..3.** The VU-side contract is settled; the dynamic light table that feeds `ctx+0xB0` is
  not identified, and the block is zero in every scene examined. The four slots
  exist so this becomes a data question only.
- **The second additive term.** For entities `ctx+0x1BC` is zero. For the map
  it needs the VU0 point-light program and the light list that feeds it, which is
  a separate port — this is the remaining real gap in the lighting.
- **The environment map and the alternate alphas.** Both are fully described
  above; neither has been needed yet. (The specular pass at `0x0200` is
  implemented but flag-gated, above.)
