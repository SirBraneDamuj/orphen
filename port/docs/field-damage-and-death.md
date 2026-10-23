# Field damage and death

## The field HP gauge, and the hit reaction under it

The orb in the top left corner of a field scene is pool slot 8, entity type
`0x58`, and it is an entity like any other. `FUN_0022A418:388` builds it on
every scene load that is not a battle section, not the title stage and not the
arena mode, and parks it at screen `(48, 32)` with `DAT_003524E0` — 65534.0 —
in `+0x28`. Its descriptor at `0x003196E4` carries `+0x04 = 0x0600` and
`+0x16 = 0x5040`:

| bit | what it does |
| --- | --- |
| `+0x02` `0x200` | `FUN_0020C5A8` refuses it, so `FUN_0020F3E0`'s billboard pass takes it instead |
| `+0x08` `0x1000` | that pass's screen-space branch: `+0x20`/`+0x24` are already pixels and `+0x28` is a GS depth word |

So nothing new draws it. It is a sprite strip — model record `0x57`, `grp_398`,
`tex_374` statically bound to texture slot `0x2C` — and the existing sprite pass
renders it exactly as it renders the two type `0x68` battle bars.

`FUN_002D0EA8` is the behaviour, and its tail is the whole readout:

```
+0xA0 = 30 - min(30, (lead +0x12A * 30) / lead +0x128)
```

31 animations, each an eight-frame loop of eight columns. Animation 0 is a full
orb and 30 an empty one, so the bar is spelled as an animation id rather than as
geometry — the same trick the battle bar plays with its five pips.

**It is on screen essentially all the time.** The gate at the top hides it
(`+0x08` bit 0) for a game mode other than 0, for letterbox bars, while a scene
change is pending, for a lead that is itself hidden, for a lead type at or above
`0x3A`, and for event flag `0x50B`. Past the gate the function never raises the
bit again: the lead's state-0 branch clears it once `+0x98` has counted up to
`0xF00`, and both item-pickup callers slam `+0x98` to `0xF00` and clear it
outright. The one thing that does raise it is `FUN_00234468`, the menu, and that
is a game-mode change anyway. So in a cutscene the gauge goes down at the first
gate failure and stays down, because a cutscene parks the lead in state 10 and
the "hold down" branch has no way back up — which is why `s01_e013` runs its
whole animatic without one.

### The alpha test the sprite pass never had

The gauge draws four records: a green liquid sphere, a black drain cap that
grows down from the top as the level falls, an ornate ring, and a glass
highlight. Stacked back to front that is a liquid level you can read at a
glance.

It came out as a plain brown box. The ring is record 1, 62x47, **blend mode 0**,
with an alpha-0 hole in the middle for the orb to show through — and
`MapViewer::drawSpriteQuads` had no `GL_ALPHA_TEST`. A blended record hides its
transparent texels through the blend, which is why no effect had ever noticed;
a mode-0 record does not, so the ring painted its own hole opaque over the two
records beneath it. The GS runs ATST GREATER for the whole frame (see the
cutout section of [Rendering](rendering.md)), and this pass was the last one not
modelling it. One `glEnable(GL_ALPHA_TEST)` and the gauge has a fill level again.

### Damage reaches the lead through `FUN_00251ED8`, not through an enemy

`FUN_00251ED8:97-227` is the lead's own `+0xBE` drain — the counterpart of the
block every enemy type wrapper opens with, and the only place the field player's
hit points move. It runs ahead of the state dispatch and can replace the state
outright, so a hit taken mid-swing ends the swing. Ported with it:

| state | handler | what it is |
| --- | --- | --- |
| `0x16` / anim `0x1F` | `FUN_002554D8` | the stagger: drift backwards until the animation ends |
| `0x17` / anim `0x22` | `FUN_002555A8` | the flatten: `+0x150` and `+0x58` to zero for `+0xC0` frames, parked in `+0x1A4`/`+0x1A8` |
| `0x18` / anim `0x20` | `FUN_002555D8` | the knockback: flying, down (`0x21`), getting up (`0x23`) |
| `0x19` / anim `0x0D` | `FUN_002557A0` | **not** death: the terrain hazard, sink the body and respawn it |
| `0x1A` | `FUN_00255820` | the game over, staged once |
| `0x1B` | `FUN_002559E8` | the game over, held |

`+0xC0` is the reaction's length. `FUN_00251ED8` reads it as a frame count,
copies it into `+0x62` and then multiplies it by 32 on the way out, so from the
next frame it is a tick count like every other countdown in the engine.

The red flash on a hit is real and is now reproduced: `FUN_00266008` takes a
`DAT_00343888` slot from 3 upward, writes `(255, 0, 0)` with radius 1.0 and
carries it on the body for fifteen frames. Slots 3 and above are the flat-tint
band, so the whole character goes red for a quarter of a second.

Left out, and named rather than silent: `FUN_00257B00`'s pad rumble (the port
has no rumble path) and `FUN_00251ED8:104-110`, which detaches the object a
state-9 carry is holding — the port has no state 9. The death sting
(`FUN_00205938(7, 0x2F, 0)`) and `FUN_00255E40`'s respawn were on this list and
are both ported now; see below.

### Dying is a knockback, and then the room goes out

The port used to run the death through state `0x19` and stop: the body faded
out, `FUN_002557A0` reached its respawn call with nothing installed, and the
frame never moved again. That state is the wrong one.

`FUN_00251ED8`'s death branch ends on `FUN_00225bf0(entity, 0x18, 0x20)` — the
**knockback**, with `uGpffff88c0` of horizontal speed and `uGpffff88c8` of
pop-up. Dying looks like being hit very hard: the body tumbles backwards through
the air, lands, and does not get up. `FUN_002555D8` is what notices, when
animation `0x21`'s `+0x62` frames run out with `+0x12A` at zero, and it writes
state `0x1A`.

State `0x19` is the **terrain hazard**: `FUN_00251ED8:60-71` writes it when the
surface under the player carries `0x1000000` — lava, or a hole — and
`FUN_002557A0` sinks the body and hands it to `FUN_00255E40`, the respawn that
puts the player back on the lead trail with one hit point. The entry test and the
respawn are both ported now (`original_player_controller.cpp`).

Two more things the death branch had wrong, both from the same misreading:
`psVar8` is a `short *`, so `psVar8[2] |= 0x11` is **+0x04**, not +0x02 — and
`+0x134` goes to **zero**, not `0x7C`, because `FUN_002555D8` ramps it back up
two a frame while the body is in the air.

### The game over: `FUN_00255820` and `FUN_002559E8`

`FUN_00255820` (state `0x1A`) runs once and leaves the entity in `0x1B`. It
stages everything: a white light on the body out of the high half of
`DAT_00343888`, pool slots 4 and 7 released, `FUN_002D36F8`'s spark pool
installed on `DAT_00355620`, every weather pool stopped, the scene's lighting
replaced with ambient `0x202020` and light 0 `0x808080` pointing straight down,
the manual camera dropped and the follow distance pushed to 3.2, and all seven
sound channels ramped down.

`FUN_002559E8` (state `0x1B`) then runs three ramps at once over about a second:

| field | from → to | what it drives |
| --- | --- | --- |
| `+0x62` | 0 → `0x2000` at 4x ticks | `/32` is the body light's brightness **and** `FUN_00255CE8`'s black quad alpha |
| `DAT_00355674` | × `(255 - level)/256` a frame | the fog colour, and with it the backdrop |
| `+0x1B6` | `0xFE0` → `0x60` at 2x ticks | `/32` goes to `DAT_00355700` and to every entity's `+0x134` from slot 10 up |

**The room does not fade out — it stops being drawn.** `FUN_00209140:127` guards
the entire map primitive walk with `cap == 0 || cap > 3`, and `FUN_002559E8`
parks `DAT_00355700` at exactly 3. That is one branch, before the loop, and it
was the missing piece: the port modelled the cap as an alpha and drew the room
at 3/127 instead of not at all. Confirmed on hardware by writing `0x7F` back
into `DAT_00355700` mid-sequence, which brings the whole room straight back
while the quad, the fog and the lights stay exactly where they are.

That also settles where the quad goes. `FUN_00255CE8` submits into GS sort
bucket **2**, not the fade's `0x1007`: with the alpha saturated at `0xFF` and
the cap forced back up, the room is still visible, so the quad is under the map
and under every entity. All it can cover is the backdrop, and that is where the
port draws it.

`FUN_002D3320`, the spark column, is the other half of the picture. It is the
second behaviour the `DAT_00355620` pool can carry and it has exactly one
caller, so it exists for this and nothing else: a thousand of the 1536 entries
re-seed themselves off bone 0 of the body, four in five in the blue of
`DAT_00806428` and the rest near-white, rising by `height/300000` a tick and
curling sideways on a heading taken from their own countdown.

Once both ramps are done the state holds. `+0x1A4` counts 19200 ticks — ten
seconds — down by the frame tick, and either that or a press of Circle or Cross
hands off to `FUN_00237A08`, the return to the title screen. That is a fade
and a scene load of `s12_e010`, not a game mode -- an earlier reading here had it
setting mode `0xC`, which is the scene *section*. The hand-off now goes through
the same code as the field menu's Return to Title (see [Menus](menus.md)).

Also left out and named: `DAT_00343A10 = -1000.0`, which gates `FUN_002025E0`;
`FUN_00212DB0(0, 0, 0)`, which sets the star field's count to zero rather than
spawning one; and `cGpffffb664`, a sound-suppression latch nothing in the port
reads. (`cGpffffb6d0`, the death latch that closes the pause gate in
`FUN_00224ff0`, used to be on this list; the pause is ported and the port raises
the latch now.)

### The music is loaded by the death, not by the scene

`FUN_002063C8(7, 0xF, 1000)` at the end of `FUN_00255820` is a volume ramp, and
a ramp on its own cannot say what plays. The piece is named thirty lines
earlier, in `FUN_00251ED8`'s death branch: **`FUN_00205938(7, 0x2F, 0)`**, which
reloads music slot 7 from category 2 record 47 and leaves it stopped. The ramp
then starts it, because `FUN_002063C8:12` plays a slot it finds idle.

So slot 7 is the game over's, scene-wide, and whatever a scene parked there is
overwritten the moment the player dies. Reading the live category-2 table at
`(&DAT_00314BA0)[2]`, record 47 is **SND.BIN resource 133** at volume 0x46 —
2080 bytes of sequence.

Leaving the load out does not fail loudly, which is what made it worth writing
down. s01_e024's own slot 7 request is `0xB2`, category 2 record 178, resource
264: a 283 KB sample bank whose section 2 is a *seventeen-byte* sequence — one
program change and an end-of-track. The ramp starts it, it ends on its first
event, and the game over plays in silence. `--sound-dump` is how that reads: RMS
per second across `--damage 20:5` went `2370, 1068, 381, 0, 0, 0` before the load
was added and `2370, 1459, 1990, 2325, 1928, 2470` after.

### The plume is the landing, not the game over

The pale cloud that comes up around the body is **`FUN_002262C0`'s landing
dust**, and it has nothing to do with dying. The death launches the body with
`uGpffff88c8` of pop-up, so it falls and lands like any jump, and the touchdown
throws the same ring a jump does.

It sits at `FUN_002262C0:576-601`, right before `+0x0C` is written, so it is
shared by every actor the generic physics moves:

```
FUN_00219AF0(x, y, z - 0.15, 0.4, r, 0, r, 0x1E, 1, 5, 0, kind != 0)
```

with `r` the actor's own `+0x54` — one outer step of five puffs, each living
5..35 frames. The top nibble of `+0x6C` is the surface kind and only 0 and 3
raise dust; the rest are water, which gets `FUN_002D4108`'s ripple instead. The
kind doubles as the colour, 3 being lit white and 0 the pool's own default.

The speed gate is `FUN_0030BD20(v * 128.0) < -4`, and `FUN_0030BD20` **truncates
toward zero** — it shifts the mantissa down before applying the sign — so the
real threshold is `v <= -5/128`, not `-4/128`. That boundary does real work: the
death's fall reaches -0.045 and throws dust, a plain knockback's reaches -0.034
(which is -4.35, truncating to -4) and does not. Rounding instead of truncating
would put dust under the knockback as well.

The port's touchdown test lives only in the lead's physics copy. The non-player
copy of `FUN_002262C0` (`actor_frame_update.cpp`) does not have it yet, so no
other actor raises dust on landing -- a fidelity gap, since the original's is
shared.

### A faded model still writes depth

`+0x134` rides in the draw header — `FUN_0020C810:140` copies it to `+0x1FC`,
substituting `0x80` for zero, and `FUN_0020DFB0:79` hands VU1 `+0x1FC >> 2`. The
port turns that into a vertex alpha and, because an opaque pass would otherwise
throw it away, promotes the pass to blending. That promotion used to select
register block 1.

**It has to stay on block 0.** The fade never touches the block index; only a
pass's own texFlags mode rewrites it. Block 0 is the one block with ZMSK clear,
so a faded model goes on writing depth, and block 1 does not. Promoting to 1
turned the depth mask off and let every triangle of the body draw over every
other in submission order. At the game over's `0x7C` that is invisible in the
colour — 124/128 is within 3% of opaque — and ruinous in the structure: the far
arm showed through the torso and the body read as a ghost.

Confirmed on hardware, mid-game-over, by poking the lead's `+0x134` down from
`0x7C`: at `0x08` the body blends with the floor tiles behind it, so ABE really
does come on, and its own limbs stay correctly sorted the whole way down. The
fix is to pass `mode` through unchanged and only raise `blend`.

### Checking it

Ported enemies do hit the lead now, but to test a reaction on demand the
harness writes `FUN_00216140`'s four output fields directly — `+0xBE` the
damage, `+0xBC` the reaction, `+0xC0` its length, `+0xC4` the direction — and
lets `FUN_00251ED8` spend them.

Keys `1`..`5` in the window, and `--damage <frame>[:<kind>][,...]` for a
headless or captured run; `0` heals. The kinds are

| kind | damage | `+0xBC` | reaction |
| --- | --- | --- | --- |
| 1 | 3 | — | stagger |
| 2 | 12 | — | stagger |
| 3 | 12 | `0x12` | knockback |
| 4 | 12 | `0x13` | flatten |
| 5 | 999 | — | lethal: the knockback, then the game over |

```
orphen_port --disc-root disc --scene s01_e024 --no-audio \
  --damage 30:1,50:1,70:1,90:1 --screenshot out.ppm:160
```

The whole death takes about twelve seconds, so a lethal capture wants a late
frame: `--damage 20:5 --screenshot out.ppm:260` is the black room with the spark
column, and running to frame 800 with `--actor-report` reaches the hand-off.

