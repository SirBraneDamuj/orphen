# Rendering

`src/ported/render/` is the draw side: the map pipeline, the entity (PSC3) and
sprite passes, and the camera-locked backdrop. The renderer is fixed-function
OpenGL; where the GS does something GL cannot express directly, the section
says what the port does instead. [Lighting](lighting.md) has the light model;
[Effects](effects.md) has the particle pools and the screen smear.

## The map draw pipeline

`src/ported/render/` is a port of the map draw pipeline. `docs/rendering_pipeline_analysis.md`
is the reading it is built from; the short version is that the original builds
each primitive's GS packet once at load (`FUN_00211230`) and then, per frame,
decides which of them to call and in what order (`FUN_00209140`).

- `original_view_projection.*` ports `FUN_0020bec8` and `FUN_0020bd58` with the
  matrix helpers. The view is `translate(-eye) * rotZ(yaw + pi/2) *
  rotX(-pi/2 - pitch) * rotZ(roll) * scale(-1, 1, -1)`, in row-vector order.
  The projection's x scale is `powf(2, fGpffffb6e8) * 3840`, so `fGpffffb6e8` is
  a **log2 zoom** and its default of 1.0 gives the shipped 7680.
- Combined with the GS geometry measured from the repo's GS dump
  (`SCISSOR_1 = 640x224`, `XYOFFSET_1` centre 320 x 112), that is **67.4 degrees
  horizontal and 54.8 vertical** -- not the 60 vertical the harness used to
  assume. The port keeps the vertical exactly and widens horizontally with the
  window ("Hor+"), so a 4:3 window reproduces the shipped framing and a wider
  one reveals more to the sides. That deliberately differs from the game's own
  widescreen path (`cGpffffb66e`, a fixed 0.77 x squeeze).
- The 3D viewport is a **4:3 box centred in the window**, with the leftover as
  bars. Anything that belongs to the game's picture is confined to that box:
  the fog-colour clear is scissored to it, and so is the screen fade
  (`FUN_0025d0e0`'s sprite is a GS primitive inside the 640x224 frame, so there
  is nothing outside the frame for it to cover -- drawn full-window, the
  cutscene's fade to white washed the bars out with it). `--window <w>x<h>` is
  the way to see that: the default 960x720 is already 4:3 and has no bars.
- `original_map_visibility.*` ports the per-frame loop: the sphere-vs-frustum
  reject, the per-primitive occlusion fade, and the 4096-bucket back-to-front
  depth sort. It runs on the fixed simulation step, not in `render()`, because
  the fade byte is per-frame state.

### Why walls go see-through

Two things together, and the port has both now. **Backface culling** makes a
wall single-sided -- the GS has no culling hardware, the original does it in the
VU1 program at `0xE0`, but the winding is fully determined by `FUN_0022c6e8`'s
corner order, so GL reproduces it. And the **occlusion fade**: a byte at
0x80-record `+0x2E` walks from `0x80` down to `0x5C` and back up to `0x7E` at
one step per frame, and any primitive standing in front of the player, above
`playerZ + 0.38`, that covers the player's projected rectangle, fades.

The overlap test (`FUN_002099d8` / `FUN_00209928`) assumes a consistent
screen-space winding, so a back-facing primitive passes every edge trivially and
always reads as covering the player. That is why a room's near wall goes
translucent when the camera is outside it.

The near-plane path has its *own*, looser condition set (`FUN_0020a2c0`): blend
flag, overlap and height only. That path is ported; the polygon clip it wraps
is not -- GL clips those primitives instead, at the plane the next section is
about, and `--render-report` counts them rather than staying quiet about it.

**Not everything is single-sided.** Flag `0x1` on the record is the two-sided
bit: `FUN_00211230:190` hands it to VU1 as a byte of its own, `packet + 9`, next
to the vertex count. On `s01_e024` exactly 32 of 1630 primitives carry it and
they are exactly 16 coincident perpendicular pairs -- the hanging chains, built
as crossed planes. Culling those makes each plane vanish from one side, so
`drawPrimitive` skips culling for them. `--probe x,y,z[,r]` dumps the records
around a world point with their flags, which is how that was pinned down.

### The near plane is 0.4, and it is not the projection's 0.3

`s01_e012`'s "Big ones! They're all over the place!" cut rests its camera at
`(-2.793, 3.424, -0.792)`, **0.31 m behind the cabin's near wall** -- primitive
2672, the quad standing at `x = -3.100`. On hardware the shot is clear. The port
filled the entire frame with that wall.

The camera was not the problem. At frame 13260 the port reaches
`cPOS>(-2793, 3424, -792)` against the save state's `(-2793, 3424, -791)`, and
the eye's z is the same float `0xBF4AC083` in both -- the last unit is a print,
not a position, and the millimetre note below is the whole of it. What differed
was where the wall got cut off.

Two 0.3/0.4 pairs live in this engine and only one of them clips anything:

- `uGpffff80b4` (0x00352024) **= 0.3** is `FUN_0020bd58`'s near argument. It
  fixes the depth mapping `z_screen = m22 + m32/z` and nothing else. No
  geometry is removed by it.
- `DAT_00351FE8` / `DAT_00351FEC` / `DAT_00351FF0` **= 0.4** is
  `FUN_0020a2c0`'s Sutherland-Hodgman pass: every edge crossing 0.4 is split at
  it and every vertex behind it dropped. Nothing else clips the map.
  `FUN_00209140` routes any primitive whose bounding sphere comes within
  `DAT_00351FCC + DAT_00351FD0` of the eye through `FUN_00209ca0` into that
  clipper, and everything it keeps instead is wholly beyond 0.6 by
  construction, so no unclipped primitive can reach a near plane at all.

`glCameraFor` was being handed the first one. The 0.1 m between them decides the
whole shot, because the wall is nearly edge-on and the cut line sweeps across it
fast:

    prim 2672, view depths at its four corners:  0.039  0.279  0.409  0.559
      clipped at 0.30  ->  covers 100% of the 640x224 frame
      clipped at 0.40  ->  the surviving sliver projects to x in
                           [-2792, -158] px, entirely past the left edge: 0%

`constants::kGeometryNearClip` is that 0.4 and both `glCameraFor` callers use
it. `kNearPlane` stays 0.3 and stays where it belongs, in
`FUN_0020bec8_build`'s projection. Ordinary play does not notice: the follow
camera sits 3 m back, and frame 13400 of the same run -- the shot after this cut
hands the camera back -- is byte-identical across the change.

**The millimetre.** The EE core's FPU has no rounding-mode control; every result
is truncated toward zero. `-0.792f * 1000.0f` therefore stays at `-791.9999957`
on hardware and becomes exactly `-792.0` on a round-to-nearest host, which is a
whole unit apart once `FUN_0030bd20` truncates. `original_position_display.cpp`
now does that multiply in double and truncates the exact product, which lands on
the same integer the EE does -- truncating to float and then to int only ever
moves toward zero twice and cannot cross an integer the one-step version keeps.

### The cutscene fade cap, and the room that would not go dark

`DAT_00355700` is a global cap on that same `+0x2E` fade. `FUN_002340e0:32`
leaves it at **3** for the chest cutscene, and `FUN_00209140:344` overwrites the
emitted fade byte with it — a fade of 3 against a `0x80` = x1.0 scale is the
black room the item reveal happens in. `FUN_002342c0` does the rest: it hides
every entity from pool slot 2 up, zeroes the fog colour and drops both light
colours, so what shows through the near-transparent map is the fog-colour clear.

All of that was ported and none of it reached the screen. The cap was computed,
emitted, folded into the vertex colour — **and then discarded, because a map
primitive only blends when its material slot says so, and an opaque primitive
never reads its vertex alpha.** So the chest cutscene played out over a fully lit
room, the item reveal showed the floor behind it, and the scene read as suddenly
"warmer" because `FUN_002342c0`'s neutral 0x808080 light had replaced the room's
own without the room going away.

The entity path had already hit this and solved it — "a fading entity has to
blend whatever its passes say", `drawObjectModel`'s `mode == 0 && fade < 1`. The
map path now does the same, scoped to the capped case: `MapDrawItem` carries
`globalFadeCapped`, set only on the `FUN_00209140:344` branch, and
`drawPrimitiveSlot` promotes blend mode 0 to 1 for those. The ordinary occlusion
fade is untouched, so nothing outside a cutscene moves — `s01_e024` at frame 300
and `s01_e012` at frame 1500 are both byte-identical across the change.

The full cycle checks out: black room from state `0x0D`, item reveal on black,
and the room, the other six chests and the scene's own lighting all back after
`0x15`. Reproduce it with

```
orphen_port --disc-root disc --scene s01_e024 --spawn -4.5,-10.5,0 \
            --press-confirm 60,500 --screenshot out/c.ppm:680
```

Two frames matter: the second confirm is what dismisses the caption, and without
it the run stops at state `0x12` and the last four states never run.

### Draw distance and fog

`DAT_00355628` defaults to 32.0 (`FUN_0022a360`) and is overridden per scene:
by the scene script's defaults block (word 3, read with the spawn) and by opcode
`0xB8` (`FUN_00263cb8`), which writes it alongside the camera distance. Both are
wired (`port_runtime.cpp`); `--draw-distance` overrides both.

Fog defaults to starting at a quarter of the draw distance and ending at it;
opcode `0xBB` (`FUN_00263db0`) sets the near and far radii explicitly, and
`0xB9`/`0xBA` the colours. The PRIM word only carries the fog bit when the start
is below 5.0, so a stock 32.0 map with no `0xBB` renders unfogged.

### PSM2 record fixes this needed

Reading `FUN_0022b5a8:184-245` properly turned up three things the loader had
wrong: the flag word is w4 alone (w5 is a colour index, and the high half of the
flags is runtime-only), w6..w9 are four material-slot selectors rather than one
section E index, and w15 is the section B index whose entry is the primitive's
**face normal**. `psm2_material_expansion.*` ports `FUN_0022c3d8`, which expands
those selectors and turns the colour index into real per-vertex colours out of
the map's palette (PSM2 header word `0x10`), replacing the placeholder shading.

### The map draws a pass per material slot

`FUN_00211230:104-360` loops material slots 0..3 and emits a GS packet for every
slot whose `type` is `>= -1`, each with its own texture page, UVs, flat colour,
alpha and blend mode. The port drew slot 0 only. `s01_e012` has 202 primitives
with two slots and 68 with three; drawing them all takes the shot at frame 200
from 437 to 518 map triangles for the same 226 primitives, at no measurable cost.

Honest caveat: this is faithful to the decompilation and the extra passes are
demonstrably emitted, but **no shot checked so far looks different with them on**
(`--map-base-slot` renders identically at f1500 and f3600, and `s01_e024` is
byte-identical). The layers are there in the data; what they are for has not been
seen yet.

### Map blending, and the black slabs around the lanterns

`FUN_00211230` builds each map primitive's GS packet once at load, and part of
that is deciding whether the PRIM word gets its ABE bit. Lines 143-158, reading
the *base* material slot's byte +0x0B for flags and +0x0A for alpha:

```
flags & 0x70 == 0            -> mode 0, opaque, ABE never enabled
flags & 0x40                 -> mode 1, alpha blend -- but alpha 0x80 is fully
                                opaque, so it folds straight back to mode 0
flags & 0x40 == 0, & 0x10    -> mode 3
flags & 0x40 == 0, & 0x10==0 -> mode 2, additive
```

then `plVar8[1] |= 0x40` (PRIM bit 6, ABE) and `pfVar27[0x1c] |= 0x40` on the
primitive. That last flag is the one `psm2_material_expansion` was already
computing for `FUN_00209140`'s "already blended, never fade" gate.

**The draw path never read it.** `drawPrimitive`'s batch key was texture and
cull mode only, so all 841 of `s01_e012`'s flagged primitives -- 21% of the map
-- drew opaque. The visible result was a black stair-stepped slab behind every
hanging lantern: the glow quads are `flags=0x20, alpha=0x1F`, so mode 2 at 24%,
and drawing an additive glow opaquely paints its black surround over the wall.
(The stair-stepping was first put down to the alpha test; the map path had no
alpha test then. It came from an inherited blend state -- see the next section.)

The mode numbering is the same 0..3 the PSC3 path uses, and both feed VU1
programs that select GS state by it, so `setMapBlendMode` mirrors
`drawObjectModel`'s `setBlendMode` case for case. Ordering needs nothing new:
map primitives and entities already merge into one shared far-to-near bucket
table, which is how the original sorts them too.

Slot 0 is the slot asked here. The original emits a pass per material slot and
blends each on its own flags; the port now does the same (see "The map draws a
pass per material slot" below), which covers the 158 primitives whose untextured
slot 0 left them opaque at the time.

`--map-no-blend` restores the old look for an A/B. At `s01_e012` f3000 the two
differ in 25.2% of pixels, at f6000 12.0%; `s01_e024` is **byte-identical**, so
the room the port was developed against is untouched. Cost is 23 extra batches
and 0.80 -> 0.92 ms of map time, entities unchanged.

### The cutout discard the map path was missing

Making the map's blend state explicit above had a second-order cost that only
showed up later, in `s01_e024`'s hanging chains. Each chain is four crossed
quads -- primitives 1618-1621 and 1626-1629, at `(-+0.45, -4.30, 1..3)` --
sampling a 16x64 strip at `u 0..15, v 144..207` of map page 0. That strip's
alpha is strictly binary: the link ring is 255, the surround is 0. Its single
material slot reads `type=0x00 a=0x00 f=0x00`, so `mapBlendMode` correctly
returns **0, opaque**, and an opaque draw of a cutout texture paints the
surround. Every chain link came back as a black stair-stepped slab.

`drawObjectModel` has discarded fully transparent texels since the grp_0003
hair-through-jeans fix -- `GL_ALPHA_TEST` at `GL_GREATER 0`, the fixed-function
stand-in for the GS alpha test. **The map path never had one.** Both paths feed
the same VU1 program and select GS state out of the same register blocks at
`608 + mode*3`, so whatever removes an alpha-0 texel for a model removes it for
a map primitive too; the port simply applied it on one side only.

It went unnoticed because the map draw used to *inherit* its blend state.
`drawObjectModel` called `setBlendMode` per subdraw and never reset it on exit,
and until `setMapBlendMode` existed nothing in `drawPrimitiveSlot` touched
`GL_BLEND` at all -- so map primitives drew under whatever the last entity
subdraw happened to leave enabled. An inherited alpha blend hides a cutout's
surround by accident. Building `cd24333^` and walking to the chains shows it
exactly: no black slab, but the whole chain is translucent enough to read the
floor and the player through it. Correct blend state removed the accident and
exposed the real gap.

So `drawPrimitiveSlot` now enables the alpha test with the rest of its batch
state and `flushMapBatch` clears it, which also re-arms it after each
interleaved entity (`drawObjectModel` disables it on the way out). Fragment
alpha here is the slot alpha times the occlusion fade, and `alphaForFade`
returns 1.0 for "no fade", so nothing opaque can reach exactly zero and vanish;
the cases that can are already invisible under their own blend.

The 20000-frame `--actor-report`/`--scr-report` is byte-identical. `s01_e012`
f6000 is unchanged, f3000 differs in 11 pixels (max delta 6, in a near-black
corner), and `s01_e024` at f600 differs in 0.35% -- a second cutout slab, in the
floor grating at the bottom right, going away for the same reason.

**Settled by a GS dump taken at the chains** (`s01_e024`, player standing in the
doorway, four captured frames, 1212 draws each). Both chains are `tbp 5760`
trifans, two crossed quads deep at any pixel through a link:

| | |
|---|---|
| `PRIM` | `0x03d` -- trifan, IIP, TME, FGE, **`ABE` 0** |
| `ALPHA_1` | `0x44` (register block 0) |
| `ZBUF.ZMSK` | 0 -- depth written |
| `TEST_1` | `0x5000d` |
| `TEX1_1` | `0x60` -- bilinear, as everywhere else |

So the chains do **not** blend. `mapBlendMode` returning 0 for `slot0 f=0x00` was
right, and the alternative floated below -- that the port misreads the material
byte and the primitives really blend -- is dead.

What removes the surround is the alpha test, and the earlier doubt about it was a
decode slip on my part. `ATST` occupies bits 1..3 of `TEST`, so `0x5000d`'s `0xd`
is `110` = **GREATER**, not GEQUAL; with `AREF` 0 and `AFAIL` KEEP that discards
exactly the alpha-zero texels and passes everything else. It is `0x5000d` on
4836 of the dump's 4848 draws and `0x3000d` on the other 12 (`ZTST` ALWAYS, an
overlay), and every one of the 4848 has `ATE` 1 / GREATER / `AREF` 0. The page's
CLUT at `CBP 6016` carries **exactly one** alpha-zero index among 256 -- the
chain surround -- and 234 at alpha 128.

`glAlphaFunc(GL_GREATER, 0.0f)` on both paths is therefore not a stand-in for
something else; it is what the GS is configured to do, and the port already had
it right on both sides. Nothing to change.

Two smaller things the dump confirmed in passing. `ZTST` is GEQUAL against a
reversed Z on every draw, which is why the specular pass -- coplanar with the
base pass it follows -- needs `GL_LEQUAL` and gets it. And the entity's per-face
additive second pass is there in the capture exactly as `drawObjectModel` builds
it: 365 untextured `ALPHA 0x48` `ZMSK 1` quads interleaved one-for-one with the
player's textured faces, in a constant `(32, 80, 160)` with the intensity in the
vertex alpha.

### `ABE` is not the register-block index

`s01_e012`, Sephy's close-up: her bangs drew as a set of solid pink strips laid
over her face where hardware feathers them into it.

The port had one `mode` variable standing for two independent things.
`FUN_00212058:137-141`:

```c
if ((texFlags & 0xc000) != 0) {
    plVar5[0x2c] = texFlags & 0x7f;          // pass alpha
    plVar5[6]    = texFlags >> 0xe;          // GS register-block index
    plVar5[1]   |= 0x40;                     // PRIM.ABE -- set here, and kept
    if ((texFlags & 0x7f) == 0x7f && (plVar5[0x2c] = 0x80, texFlags >> 0xe == 1))
        plVar5[6] = 0;                       // only the *index* folds
}
```

`plVar5[6]` picks the GS state at `608 + mode*3`; `plVar5[1] |= 0x40` is PRIM
bit 6. The full-alpha fold rewrites the index and leaves ABE standing, so block
0 is **not "the opaque block"** -- it is `ALPHA_1 0x44` with `ZBUF.ZMSK` clear,
differing from block 1 only in that it writes depth. The port read the fold as
"switch blending off".

Read out of the save state, the four blocks and how each is actually reached:

| block | `ALPHA_1` | `ZMSK` | reached by | draws in the dump |
|---|---|---|---|---|
| 0 | `0x44` | 0 | mode 0 (ABE off), or mode 1 folded at full alpha (ABE on) | 540 off, 84 on |
| 1 | `0x44` | 1 | mode 1 below full alpha | 28 |
| 2 | `0x48` | 1 | mode 2, additive | 9 |
| 3 | `0xa1` | 1 | mode 3, reverse subtract | 0 |

The 84 are her hair: `PRIM.ABE 1`, `ALPHA_1 0x44`, `ZMSK 0`, vertex alpha `0x80`
on all 332 vertices. With `TEX0.TCC = 1` and `TFX = 0` (MODULATE) the GS's `As`
is `(Av * At) >> 7`, so at `Av = 0x80` the blend factor **is the texel's alpha**
-- and her sheet's CLUT at `CBP 0x24b4` carries a full `0..128` ramp. That is
the whole effect, and the fold was throwing it away.

It hid for so long because it is a no-op on an opaque sheet: `As = 128` gives
`(Cs-Cd)*128>>7 + Cd = Cs` exactly. grp_0172's 60 chest passes are the same
fold case and look identical either way, which is why folding to opaque seemed
to work when it was introduced -- the chests that went translucent back then did
so because the port sent them to block *1*, losing the depth write, not because
they blended.

`setBlendMode(mode)` is now `setBlendState(mode, blend)`, carrying the two
separately, and block 0 sets `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` with
`glDepthMask(GL_TRUE)`.

**The map path had the identical shape, and now carries the two separately
too.** `FUN_00211230:152-161` folds `plVar8[6]` to 0 when byte `+0x0A` is `0x80`
and then sets `plVar8[1] |= 0x40` *outside* the branch, exactly as the PSC3 path
does. `mapBlendMode` still returns the block index; `mapBlendEnabled` is the new
`ABE`, `(flags & 0x70) != 0` on a textured slot, and `setMapBlendState(mode,
blend)` mirrors `setBlendState`. The slot alpha is now folded into the vertex
colour whenever the slot names a mode rather than only when the mode survives --
`plVar8[4]` is written before the fold, and the folded case is `0x80`, so the two
agree.

**It changes nothing in either scene, and that is a measured claim, not an
assumption.** Counting the fold case (`flags & 0x70` set *and* alpha `0x80`) over
the first 20,000 map slot draws: `s01_e024` `fold=0 blend=0 plain=20000` -- no
map primitive blends there at all -- and `s01_e012` `fold=0 blend=4367
plain=15633`. Zero either way, so the path exists but no shipped material reaches
it. `s01_e024` and `s01_e012` at f600 and f3000 are byte-identical before and
after, and the 20000-frame `--actor-report`/`--scr-report` diff is empty. Worth
having anyway: it was a real divergence, and it is the same one line of reasoning
as the entity path rather than a second, different rule.

### Texture filtering is bilinear, and the port was sampling nearest

`TEX1_1 = 0x0000000000000060` -- `MMAG 1`, `MMIN 1`, both LINEAR, with `MXL 0`
and `LCM 0` so there are no mipmaps and minification is plain bilinear. It is
written 160 times across the capture and never takes another value. `CLAMP_1` is
`0`, `WMS`/`WMT` both REPEAT, which the port already had.

Every texture the port uploads was `GL_NEAREST`. `SCISSOR_1` is `640x224` -- one
interlaced field -- so every 256x256 sheet is magnified several times over on
its way to the screen, and nearest sampling turns a graded alpha ramp into hard
strand edges. The GS filters the RGBA the CLUT lookup produced, so filtering an
already-expanded RGBA8 page is the same operation; `GL_LINEAR` with no mipmap
levels is the exact equivalent.

`s01_e024` f600 moves 90% of its pixels and its mean goes 58.75 -> 57.63. The
20000-frame `--actor-report`/`--scr-report` is byte-identical, as it must be:
neither this nor the ABE change is visible to the simulation.

### What is left on the bangs, and what it is not

They are still slightly heavier than hardware. Two things it is **not**:

- **The `GL_RGB_SCALE 2.0` combiner.** GS MODULATE is `C = (Ct * Cv) >> 7`; the
  port computes `tex * (Cv/256) * 2`, which is the same expression. Nothing to
  change.
- **Fog.** All 332 hair vertices carry `F = 255` in `XYZF2`, and `FOGCOL` is 0.
  The hair is unfogged on hardware.

What remains is the lit vertex colour feeding the card: hardware's hair vertices
are median RGB `(70, 51, 25)`, alpha 128 throughout. So the residual is a
scene-lighting brightness question -- the same thread as the lantern work's
"the port is overall too bright" -- and not a property of the hair at all.

The GS does discard here: `TEST_1` is `0x5000d` on every draw in the capture,
which is GREATER against `AREF` 0 (`ATST` is bits 1..3), and the port's
`glAlphaFunc(GL_GREATER, 0)` on both paths is the exact equivalent.

### Texture slots are global, and a pass picks its own

The window curtains in `s01_e012` drew as the shop's gold medallion instead of
white sheer fabric. Neither the model nor the texture binding was wrong —
`eeMemory.bin` agrees with the port on both, entity by entity. What was wrong is
that **the port drew every textured pass with the entity's one bound texture**.

Two facts, and the second follows from the first.

**`DAT_003429a8` is one global array of 64 texture slots.** Entity models take
slots 10..23 and 24..39 through `FUN_00266118`'s two banks, the boot binds take
32..48 — and the *map's own texture pages* take slots 0..9, loaded by
`FUN_0022a178` from a ten-entry table at `DAT_00325394`. The dump confirms both:
`DAT_00325394` = `{0286, 0002, 02c8, 0253, 000d, 02c9, 000f}` and
`DAT_003429a8[0..6]` holds the same ids in the same order, which is exactly the
port's map page list. So a PSM2 material slot's `type` byte was never a page
index — it is a slot index that happens to start at 0. `FUN_0022a178_bind_map_textures`
now loads them, which is what makes the rest of this resolvable.

**Every PSC3 subdraw names its own texture.** `FUN_00212058:180-208` reads
`texFlags` bits 10..7 and writes packet byte 6 from it:

| selector | primitive flag 0x800 | byte 6 | means |
|---|---|---|---|
| 0 | either | 0x3F | the entity's bound slot |
| 0xF | either | 0x3E, byte 5 = 0x11 | the special mode the map path reaches through its own type 9 |
| 1..0xE | clear | the selector | **global slot `selector - 1`** |
| 1..0xE | set | 0x3F, selector rides in byte 5 | the bound slot still wins |

Byte 6 is `globalSlot + 1` on the map path too (`FUN_00211230:186`), which is what
pins the arithmetic down.

`--model-report` prints the histogram per entity as `passes={…}`. For `s01_e012`:

| model | passes | drew with | should draw with |
|---|---|---|---|
| grp_01d5 window curtain | `gslot3:48` | tex_0133, the gold medallion | tex_0253, the white sheer curtain |
| grp_00ae window frame | `gslot3:96` | tex_0130 | tex_0253 |
| grp_01d6 window surround | `gslot0:30` | tex_0133 | tex_0286, the sea |
| grp_00c6 lantern | `bound:49 gslot1:4` | all tex_012f | four passes on tex_0002, the flame sheet |
| grp_0172 chest | `bound+4/6/8` | tex_0179 | tex_0179 — the 0x800 form, already right |

The frames looked plausible before because tex_0130 and tex_0253 share their top
half; only the bottom differs, and the curtain lives there. The lanterns were
unlit for the same reason — their flame pass was being drawn with the lantern's
own sheet.

`s01_e024` renders **byte-identical** (nothing in it uses a selector),
`s01_e012` stays deterministic, flag `0x515` still lands at 13122, and the chest
cutscene still runs its ten states.

## Entity models

### The shop's additive props: a bundle-lookup order bug

s01_e012 drew white/pink sheets and a rope swirl above the shop counter that the
original does not show. **Fixed** — `EntityModelStore::loadModel` was asking the
wrong bundle.

Bisecting pool slots against an all-entities-hidden render at f3000 — which by
itself matches the emulator frame, so this is entirely entity draws:

| slot | type | model | share of upper frame |
|---|---|---|---|
| **14** | 0x27b | grp_00a8 | **34.6%** |
| 28-32 | 0x27c-0x280 | grp_00a9..00ad | 10.7% |
| 11 | 0x27d | grp_00aa | 0.2% |

`--pose-report <slot>` prints one entity's bone palette beside an unfiltered
rebuild of it, plus the subdraw blend histogram. It says grp_00a8 is **158
subdraws, every one blend mode 2 (additive), mean alpha 105/128**. These are
effect models, not props.

Six leads are ruled out, and are recorded so they are not re-derived:

- **Not the model parser.** This entry used to describe these as "stretched
  shards from a model-parser bug". The parse is fine.
- **Not the pose filter.** live palette == unfiltered rebuild on every bone,
  gap 0.000; +0x13C is 1.0, so the filter is a no-op here regardless.
- **Not the pose column.** The renderer uses entity +0xAC = 0 and `eeMemory.bin`
  reads +0xAC = 0 too. An earlier "live=2.44 vs posed=0.83" was a diagnostic
  comparing different columns: `--model-report`'s bbox uses
  `firstPoseColumnForAnimation`, the renderer uses +0xAC.
- **Not entity visibility.** `DAT_005a96b0[14] = 1` and +0x04 = 0x00D8, so both
  of FUN_0020c5a8's first-pass tests pass in the original.
- **Not the fade byte.** +0x134 is 0.0 on every entity in the dump, player
  included; 0 means "not fading" and the port already maps it to 1.0.
- **The zero bone matrices are an artefact.** 9 of 19 matrices at
  `0x357E00 + 14*0xA80` read zero, but the dump is the bed shot and slot 14 has
  never been drawn there, so that bank is untouched BSS.

**Root cause: the port binds the wrong mesh.** `FUN_00229c40:28` stores the
bound model's base pointer at entity **+0x15C**, and a loaded PSC3 keeps its
magic, so an EE dump names the mesh the original actually drew and its bone
count at +0x04:

| type | port binds | port bones | real bones | |
|---|---|---|---|---|
| 0x275 | grp_00a2 | 31 | 31 | ok |
| 0x279 | grp_00a6 | *parse fails* | 30 | **wrong** |
| 0x27b | grp_00a8 | 19 | 2 | **wrong** |
| 0x27c | grp_00a9 | 18 | 2 | **wrong** |
| 0x27d | grp_00aa | 10 | 10 | ok |
| 0x27e | grp_00ab | 2 | 2 | ok |
| 0x27f | grp_00ac | 41 | 2 | **wrong** |
| 0x280 | grp_00ad | 11 | 2 | **wrong** |
| 0x281 | grp_00ae | *parse fails* | 17 | **wrong** |
| 0x299 | grp_00c6 | 4 | 4 | ok |
| 0x2c0 | grp_01cb | 31 | 31 | ok |

The wrong ones are exactly the slots a hide-slot bisect had found. The **type ->
mesh id mapping is fine** (`id = type - 0x1D3` across this band, confirmed by the
five that match), and so is the descriptor table (contiguous at 0x00F467xx,
stride 0x2C, ordered by type).

The fault was **lookup order**. `loadModel` asked for category 0 across *both*
bundles and only then tried category 2:

```cpp
bytes = decodeResource(kGrpCategory, meshId);   // scene cat 0, then BOOT cat 0
if (bytes.empty())
  bytes = decodeResource(kMapCategory, meshId); // scene cat 2
```

That rested on ids being scene-private, which the old comment admitted was only
"in practice". s01_e012 breaks it: the six models it wants live in its own
category 2, and the boot bundle answers for those same ids out of category 0
first. Two of those answers are a different model — which parsed and drew as a
plausible but wrong mesh — and two are not models at all, which is why the
"missing PSC3 magic" failures always sat right next to the "shards". One bug,
two symptoms.

The fix exhausts the **bundle** before the category: for each provider, scene
then boot, try category 0 then category 2. After it, all eleven types in the
band match the dump's bone counts, and `s01_e012`'s model failures go from 16 to
**0** — grp_00a6 (30 bones) and grp_00ae (17) load for the first time and put
the shop's crates and side counter on screen.

The real props are 2-bone meshes. The port had been drawing 19- and 41-bone ones
whose subdraws are 100% additive, which is why they read as translucent sheets
rather than as obviously broken geometry.

`s01_e024` renders **byte-identical**, `s01_e012` stays deterministic, flag
`0x515` still lands at 13122, and the chest cutscene still runs its ten states.

## The billboard pass, and why there are two entity draws

`FUN_0020c5a8` -- the skeletal entity pass -- refuses any entity whose `+0x02`
carries bit `0x200`. `FUN_0020f3e0` then walks the same 256 slots for exactly
those. The two passes **partition** the pool rather than filtering it: an effect
entity is never a candidate for the skinned path, and a character is never a
candidate for this one. The port had only ever had the first, which is why every
sprite effect was invisible.

`FUN_0020f3e0`'s guards are the mirror image: a live type id, `+0x02` bit
`0x200` set, `+0x08` bit 0 clear. It also clears `+0x0C` bit `0x1000` on the way
in, and `FUN_0020f510` sets it back on anything it submits -- a "was drawn this
frame" latch nothing else reads.

### The model is a strip of sprite records

Parsed by `loadSpriteStripModel`. No PSC3 magic, and the header's `+0x00` is a
**column** count, not a record count:

```
+0x00 u16  column count
+0x02 u16  animation count
+0x04 u32  column table:  {u16 firstRecord, u16 recordCount} per column
+0x08 u32  sprite record table, 16 bytes each
+0x0C u32  animation table, one u32 timeline offset per animation
```

The animation picks a column (entity `+0xAC`) and the column names a run of
records, drawn **back to front** -- `FUN_0020f510` seeds its cursor at the last
record of the run and walks down. `grp_017e`, the magic projectile's, is twelve
columns of one record each.

Each record:

| off | what |
|---|---|
| `+0x00` | bits 0-1 blend mode, `0x10` flip X, `0x20` flip Y |
| `+0x01`/`+0x02` | texel origin |
| `+0x03`/`+0x04` | extent, **one more** than the real width and height |
| `+0x05` | twice the alpha byte |
| `+0x06`/`+0x07` | offset in whole sprite units, signed |
| `+0x08` | depth bias, over 100, added to the view depth |
| `+0x0C` | float scale |

The blend mode is the same 0..3 the map and PSC3 paths already use, so it goes
straight into `setMapBlendMode`. That is not a guess: `FUN_0020f510` packs the
GS PRIM word as `0x0D` or `0x1D` (untextured / textured triangle strip) and ORs
bit `0x40` -- **ABE** -- for any non-zero mode. Mode 0 does not blend at all,
which is also why its alpha byte is forced to `0x80` and never read.

### The quad is built on the projected origin

`FUN_0020b600` projects the entity origin to an integer GS screen position, and
**every corner is an offset from it**, in GS units:

```
projScaleX = trunc(entity+0x14C * 280.0 * G / viewZ)      G = 2 * DAT_00355658
gsX = originX + (spriteX * projScaleX >> 8)               spriteX in 1/16 units
gsY = originY + (spriteY * projScaleY >> 9)               note the extra shift
```

The quad is axis-aligned in screen space, which is why a sprite never rotates
with the camera. Both `projScale`s are truncated to integers before being used
as fixed point, so a distant sprite's size quantises in visible steps rather
than shrinking smoothly; the record's own scale at `+0x0C` truncates the same
way. The extra shift on Y is not a bug to correct: the GS output is 640x224
shown at 4:3, so its pixels are 2:1 and a square sprite has to be twice as wide
in pixels. `X` uses `+0x14C` and `Y` uses `+0x150`, which is why a non-uniform
entity scale stretches the sprite.

The port runs that arithmetic in GS integers and then un-projects the four
finished corners back to view space, because it draws through the same
projection the world does instead of writing GIF packets. Re-projecting them
gives back the same GS numbers -- it is the inverse of the transform above, at
the depth the GS z was keyed off. That is the only adaptation in the function,
and `drawSpriteQuads` accordingly runs with the modelview at identity, the
scene's own projection, and depth writes off. PRIM's `FGE` bit is clear in both
of `FUN_0020f510`'s words, so a sprite is never fogged however far away it is.

> The first cut of this pass emitted the offsets *without* the origin, on the
> reasoning that the perspective divide made the quad a fixed size in view space
> and the origin would come out in the wash. It does not: the offsets are
> relative to a point that has to be added back. Every sprite landed dead centre
> of the screen at its own depth, so the bolt sat on Orphen and vanished behind
> him as it flew away. Reformulating the maths is how that got missed; the port
> now follows `FUN_0020f510`'s own shape and adapts only at the last step.

### Depth is keyed off a different number than the position

```
depth = viewZ + (char)entity+0x133 * DAT_003520a0        (0.08)
              + (char)record+0x08 / 100.0
gsZ   = trunc(DAT_003555a4 / depth + DAT_003555a0)       floored at 0
```

Both biases move the sprite in depth only -- the position and size came out of
`projScale`, which used the unbiased `viewZ`. Entity `+0x133` is copied from
descriptor `+0x02` by `FUN_00229c40`, and most effect descriptors carry `-12`, so
the usual effect sits about a unit nearer the camera than it stands. Entity
`+0x08` bit `0x40` throws all of it away for a flat `0xFFFF`, nearer than
anything the world writes.

Submission is a 4096-bucket display list on `gsZ >> 4` clamped to `1..0xFFF`,
plus the `0x1005` bucket above it for the `bit 0x40` sprites. `gsZ` rises as a
sprite comes nearer, so ascending bucket is back to front, and the port sorts
the same way. The one thing it does not reproduce is that the original links
each packet in at the *head* of its bucket, so co-bucketed sprites come out
reversed among themselves -- invisible for the additive blending everything
drawn so far uses.

### The colour

`FUN_0020f3e0` stages `max(DAT_0035566c, DAT_00355670)` per channel -- the scene
ambient against light 0's colour -- and `FUN_0020f510` averages that with the VU0
point-light contribution at the sprite's own position, saturating. `+0x08` bit
`0x4000` skips the whole thing for a flat `0x80`. GS bytes throughout, `0x80` for
1.0, so a sprite can be brighter than white before the blend.

The near clip here is `DAT_0035209c`, **0.3** -- not the `0.4` the geometry path
uses. The far one is the scene's draw distance.

### The rotation and HUD branches

Both landed with the battle target cursor, which is the one thing in the
executable that takes them together:

- **The rotation branch**, `+0x08` bit `0x400`, rebuilds the quad as four
  rotated corners off the angle table at entity `+0x168`, for record indices
  under nine. Angle 0 swings the quad's *centre* about the sprite origin; angle
  N -- N being the record's own descending index -- spins the quad about its own
  centre. Everything is done with Y doubled, because the GS output is 640x224 at
  4:3 and rotating in raw GS units would shear the sprite; the sine term is
  halved on the way back out.

  `FUN_002D73E8` writes those angles: `DAT_003547A4`, which is pi/4 to the bit,
  while the cursor is the player's target, and an accumulation of `DAT_003555BC
  * DAT_003547A8` -- about 25 degrees a second -- when it is not. That is the
  difference between the selected bracket sitting corner-up and the rest slowly
  drifting.
- **The HUD branch**, `+0x08` bit `0x1000`, takes a position that is already in
  screen space rather than projecting one: a flat `+0x14C * 256` scale with no
  perspective divide, and a GS z read back out of `+0x28` as an integer.

Note the aliasing on `+0x168`. For a skinned entity those 42 bytes are the
per-bone override modes (see "`+0x168` is inside the slot" in
[Entities](entities-and-party.md)); for a sprite entity
-- which carries `+0x02` bit `0x200` and can never reach `FUN_0020C5A8` -- they
are seven floats of quad angles. The two never coexist. The port splits them:
bone modes live in `EntityBoneOverrides`, angles in `OriginalEntity`.

### Checking it

```sh
port/build/msvc-Release/orphen_port.exe --disc-root disc --scene s01_e024 \
    --no-audio --spawn -2.5,-8,0 --press-magic 60 --screenshot shot.png:112
```

catches the bolt as a glowing orb in Orphen's hands; frames 124 onward have it
clear of him and travelling with its trail. Four casts over 900 frames still
report `live actors: 20`, and `--render-bench 6` reports the same primitive and
batch counts as it did before the pass existed -- which is the check worth
making, because absolute frame times on this machine move by 30% run to run for
reasons that have nothing to do with the port. Compare the counts, not the
milliseconds.

## A hidden parent must stay "queued", not "posed"

`FUN_0020c5a8`'s second pass has four outcomes per slot, and the status byte it
writes -- or does not write -- is what the *children* read:

```
+0x08 bit 0 set        clear +0x0C bits 0x3000, raise +0x08 bit 0x10,
                       **leave the status byte at 0**
parent < 0             pose and draw, status = 1
status[parent] == 0    push this slot on the back of the queue
status[parent] == 1    pose and draw, status = 1
status[parent] == 0xFF neither; dropped this frame
```

The port had the hidden test inside its publish helper, which returned early --
and then the walk wrote `status = 1` over the top anyway. A hidden slot
therefore read as *posed*, and every child bound to it was drawn.

In s01_e012 that child is **slot 61**, a type `0x2C4` parented to slot 83
(Dortin, `+0x08 = 0x0133`, hidden from the first frame). With no parent palette
to resolve against it fell back to its own `+0x20`, which for a bone-local
attachment is near enough the world origin -- putting the sack Dortin is
supposed to be carrying on the floor of the middle doorway for the whole scene,
and then again in his hand once he was drawn. The EE dump agrees with the port
on every field of slots 61-64, so this was never a spawn or placement problem;
only the draw walk's bookkeeping.

The hidden branch's third write, `+0xB0 = 0`, is the pose blend's "no previous
column" (`FUN_0020e840:60` reads it next to `+0xAA`). The port's pose filter
keeps that in its own state, so there is no field to write.

## The backdrop is a model, and `FUN_0020C290` draws it

`src/ported/render/original_background_model.*` is `FUN_0022CE60` and
`FUN_0020C2F0`: the fog sphere `s01_e013`'s boat deck stands inside.

`FUN_0020C290` reads like a display-list kick sitting next to `FUN_0020C5A8`
and `FUN_0020F3E0` in `FUN_002239C8`'s draw block. It is not. It walks four
descriptors at `DAT_00345A18` -- stride `0x24`, counted *down* from
`DAT_00345A84` -- and hands each to `FUN_0020C2F0`, which draws a whole model.
It was the last call in that block the port did not have, and it is why the
deck sat in a black void where the retail game has weather.

It was found by patching `jr ra; nop` over one candidate function at a time in
PCSX2 and screenshotting. **EE code patches through the debugger interface do
take effect**, despite the MCP tool's warning about recompiled blocks, which
made the search cheap. The fog survived the map, the entity models, the sprite
pass, `FUN_002192C0`, the actor loop, the scene script and `FUN_00207DE8`
before `FUN_0020C2F0` turned out to be the last thing standing.

### Where the model comes from

A **PSB4** record in the scene bundle's **category 2** -- the same category the
PSM2 map lives in. `s01_e013`'s is id `0x009E`, and it is the *first* category-2
record, which is why `loadFirstPsm2FromSceneResources` walked straight past it
looking for PSM2 magic. `s14_e001`'s pier uses the same one.

No script installs it. `FUN_0022A418:134` calls `FUN_0022CDE8(descriptor, 0)`,
which reads the **halfword at scene descriptor +0x08** and loads that id into
slot 0 with its shade byte seeded to `0x80`. Opcode `0xE5` is the other way in,
and `0xE6` sets a slot's shade and Z rotation -- see
`analyzed/ops/0xE5_load_backdrop_model.c`, which used to be filed as an audio
opcode.

The file is a 16-byte header and three sections: vertices (`s16` count, then
xyz floats at a stride of 12), primitives (22 bytes each), and a UV animation
script in the format the map's section G already uses. A primitive is a flags
halfword, four vertex indices and four three-byte groups, and `flags & 0x800`
picks how those three bytes read -- the same bit the map and PSC3 builders
test. Set, they are `(u, v, textureSlot)`; clear, a flat RGB.

Everything else is in the flags word, and every bit of it is confirmed against
a GS dump of the live frame: `0x7000` the blend mode, `0x0700` a CLUT bank
(dead for an 8-bit page), `0x00FE` **the vertex alpha, halved**, and `0x8000`
the UV animation selector. The colour is not in the file at all: `FUN_00211B80`
hands VU1 the literal `DAT_00808080`, which is why every backdrop vertex in the
dump reads `(128, 128, 128)`.

`s01_e013`'s model is a sphere: 312 vertices, 391 quads, 24 segments around and
13 rings, radius 35 at the equator closing to 10 at the poles. Its cloud band
is above the equator and its teal mist below, which is the order the dump's
quads land on screen. Every primitive names **global texture slot 8** -- the
map's ninth and last texture page, which no map primitive uses. That page being
referenced by nothing was the first hard evidence the backdrop existed.

### Camera-locked, and never in front of anything

`FUN_0020BEC8:43-45` puts the model at **the camera eye's x and y**, with
`z = eye z + fGpffff808c` = 0.4. It rides the camera exactly, so a mesh 35 units
across reads as a cylinder wrapped around the whole scene however far you walk.

Its packets end in `MSCAL 0x228`, a different VU1 program from the map's
`0x13B`/`0x14B`, and the dump shows what that program does differently: every
backdrop vertex reaches the GS at **Z = 2** -- the far end of that frame's
2..1108 range, whatever the geometry's real depth -- with `PRIM.FGE` clear where
every map primitive has it set, and `ZBUF.ZMSK` set where the map clears it.
Fog off, depth write off, always behind.

The port draws it before the map with the depth test and the depth mask both
off, which is the same picture by the only means GL offers. **It needs its own
projection.** The world is drawn with a far plane of `drawDistance + 8` -- 28
units in `s01_e013` -- and this model reaches 50 units from the eye, so the
frustum the map uses clips every one of its primitives away. The first build
loaded, parsed, transformed and submitted all 391 quads and drew nothing at
all. The backdrop pass pushes the far plane to 4096 instead, which costs
nothing because it neither tests nor writes depth.

### What is checked, and what is not

`--render-report` prints the model and the quads it builds; `--no-background`
turns it off for an A/B. `--dump-scene-resources <dir>` writes every decoded
record of a scene bundle, which is how the PSB4 was read in the first place.

Brightness lines up: a backdrop-only frame measures `(27, 36, 37)` mean RGB in
the port against `(23, 32, 33)` in a retail frame with everything else patched
away. `s01_e024`, `s01_e012` and `s14_e001` reports are unchanged but for the
new load line.

Not checked: a pixel-for-pixel frame against retail. The one player position
where the port's camera lands exactly on the retail camera -- `(14.112, 2.5,
5.5)`, where `s01_e013`'s forced-camera zone engages -- renders a near-white
frame in the port, and `--no-background` shows the backdrop is not what causes
it. That white-out is its own bug and is not chased here.

## Notes on GS dumps

`port/attic/gsparse.py` is the parser the dumps above were read with (see
`port/attic/README.md`); it snapshots TEX0 only, so a PSMT4 page's real format
arrives in a TEX2 write it does not show. `tools/gs_dump_parse.py` drops VSync
boundaries, so re-walk the packets if you need per-frame draw lists. The field is **640x224** with XYOFFSET
(2048-320, 2048-112) — the measured vertex y span of 226.6 settles it. ABE alone is
not a useful discriminator in this game: most of the scene is blended, nearly
every texture shows opaque ~= abe because each primitive gets a second packet,
and the register-block index is a separate thing (see "`ABE` is not the
register-block index").
