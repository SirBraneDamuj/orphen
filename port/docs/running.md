# Running the port

## Running The PSM2 Slice

From the repository root after building:

```sh
port/build/msvc-Release/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2
```

Or load from extracted disc files in a directory containing `MCB0.BIN` and `MCB1.BIN`:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012
```

**A scene does carry its own spawn point**, and it is in the SCR. `FUN_0025b600`,
called from `FUN_0022a418` with the per-scene defaults struct at `0x325368`,
reads a block sitting immediately after header word 6's texture page list: skip
one halfword, copy sixteen halfwords, align to 4, then four ints scaled by
**1000.0** -- not the 100000.0 the coordinate opcodes use.

```
s01_e024 blob 0x614:  -3250, -12750, 0, 32000
              /1000 = -3.25, -12.75, 0.0, 32.0
```

The first three are the spawn, landing at struct `+0x4C` (`0x3253B4`); the fourth
is `DAT_0032538c`, the scene's **draw distance**, which `FUN_0022a360` seeds to
32.0 and this overrides per scene. Both are wired up now.

`FUN_0022a418` copies that backup into `DAT_00325340` when `DAT_003551ec` has bit
`0x2000`, and applies it to pool slot 0 when it has bit `1`. `FUN_002000c0` sets
`0x2001` at boot, so arriving *without* an explicit warp target -- which is what
loading a map from the debug menu does -- lands on the script's own spawn. A warp
from another map overrides it through `FUN_0022b2c0` (opcodes `0x8B`/`0x8C`).

Confirmed against an EE memory dump of `s01_e024`: `DAT_00325340` and its backup
both read (-3.25, -12.75) and pool slot 0 is there.

So the port picks, in order: `--spawn x,y,z`; a script teleport (`0xAB`); **the
scene script's own spawn**; the first group 2 placement record; otherwise the
walkable triangle nearest the map's horizontal centre. The console says which was
used. The group 2 fallback was previously the default and gave (-5.5, -12) on
`s01_e024` -- 2.3 units from the real spawn -- so it is a guess, kept only for
scenes with no defaults block.

To validate the loader without opening a window:

```sh
port/build/msvc-Release/orphen_port.exe --psm2 out/target_all/s01_e012/map_0002.psm2 --load-only
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012 --load-only
```

To inspect the resources loaded by a disc scene:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e012 --scene-tree --load-only
```

The scene tree currently groups MCB bundle records by category and prints record ids, bundle offsets, packed/decoded sizes, and known decoded signatures such as PSM2, BMPA, SCR, and PSC3. `s01_e024` is a useful early exploratory scene because it is much smaller than `s01_e012` and appears to be a debug scene:

```sh
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --frames 60
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only --scr-report
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --load-only --actor-report
port/build/msvc-Release/orphen_port.exe --disc-root . --scene s01_e024 --frames 120 --scr-tick --scr-report --actor-report
```

`--scr-tick` runs the per-frame script entry and the object-script slots.
`--actor-report` lists every live entity, the behavior address its type resolves
to, and whether that behavior is ported; with `--frames` it also reports tick
counts per type. Both reports resolve straight from the pool, so `--load-only`
gives a useful answer before a single frame has run.

`--probe x,y,z[,radius]` dumps every primitive whose bounds come within radius
of a world point -- flags, terrain flags, centre, radius, plane normal, corner
count and material slots. It is a hypothesis-testing tool, not part of the port.

`--render-report` prints what the map visibility pass culled, faded and drew,
how many primitives straddle the near plane (which GL clips rather than
`FUN_0020a2c0`), and two oracles that can be checked without looking at a
picture: whether the plane normals agree with the map's own `0x100` ceiling
flag, and how much of the drawn set faces the camera.

`--frames` runs the runtime update loop without opening a window. The old `--script-frames` spelling is still accepted as a compatibility alias, but it no longer executes script frames.

A gamepad is used when one is present: left stick moves, shoulder buttons orbit
the camera, and the face buttons map by position onto the PS2 layout, so on an
Xbox pad Y is Triangle, B is Circle, A is Cross, and X (SDL "face west") is
Square. Square is the jump binding, matching the PS2 game. The keyboard remains
available as a digital fallback.

The left stick goes through the ported `FUN_0023b3f0`, so it inherits the
original's response curve: a deadzone of 60 out of 128 raw units means nothing
happens below roughly 47 percent deflection, and the remaining 68 units rescale
to 0..128 so the walk/run threshold of 100 lands near 88 percent. That deadzone
is why there is a perceptible delay between pushing the stick and the character
moving -- the stick has to travel almost halfway first. The keyboard follows
FUN_0023b5d8's digital branch instead, which writes 128.0 outright, so a held
key always runs.

Controls:

Game inputs -- these reach the ported code as raw pad bits and nothing else:

- `W/A/S/D` moves the runtime lead player relative to the current camera yaw.
- `Space`, or gamepad X / face west (Square), jumps when the lead player is grounded.
- `C`, or gamepad B (Circle), swings the sword when the lead player is grounded. See The sword attack, above.
- `V`, or gamepad Y / face north (Triangle), casts the homing magic projectile, also only when grounded. See The magic cast, above.
- Holding `C` re-arms the jump in mid-air. This one is a harness debug affordance, not something the original's airborne state does; it restarts state 2 / animation `0x0C` through the same startup and `+0x44` seed as a grounded jump, which is how you get up to a ceiling to test against. It shares Circle with the attack because the original's own gate on it is `input_flags & 0x20` (`FUN_00251ed8`, `uGpffffbd54`).
- `Return`, or gamepad A (Cross), confirms -- the interaction probe, and dialogue advance.
- `J/L` orbits the player camera, mapped onto the original's L1/R1 raw pad bits (0x04/0x08). With no player active they rotate the free viewer camera instead.
- Holding `P`, or the gamepad right trigger (R2), fast forwards. See below.

Harness controls, which the simulation cannot see:

- Left/right arrows cycle maps when running from `--disc-root`.
- `R` resets the viewer camera.
- `F` toggles wireframe.
- `H` toggles the game's own debug readout, the `FUN_00268270` overlay.
- `B` toggles the in-world debug overlay: collision boxes, entity labels, origin axes.
- `O` toggles SCR SUBPROC DISP. It was `P` until fast forward took that key.
- `G` dumps a pose/draw-list snapshot of the current frame and photographs it.
- `F1` toggles the fly camera, `F2` puts it back on the game camera, `F3`
  swaps the game's draw list for the whole map, `F4` toggles the game view
  inset, `F5` toggles the entity tree. See Fly camera, below.

`C` was `B` until the sword attack landed on it, at which point one key was
firing two gameplay actions and a harness toggle. The free viewer's own pitch
(`I/K`) and zoom (`Q/E`) are gone with the same tidy-up: they were map-viewer
holdovers with no pad button behind them, and the free viewer keeps yaw and pan
off the game's own axes. `R` still restores its default framing.

### Fly camera

`F1` detaches the view from the game camera (`src/harness/fly_camera.*`). It
has no counterpart in the original. It starts exactly on the game camera's eye
and heading, fills the whole window instead of the 4:3 box, and draws the game
camera into the world as a yellow gizmo: a pyramid from the eye to the 0.4
near clip, a filled triangle on the near plane's top edge for up, the frustum
out to the draw distance (`DAT_00355628`), and the view axis in orange. Each
piece is drawn twice, faintly through geometry and solid where nothing is in
front of it.

- Hold the right mouse button to look; `W/A/S/D` fly along the view, `Q/E` go
  down and up. Shift is x4, Ctrl x0.25, and the wheel scales the base speed.
- While it is up, WASD stops reaching the movement stick, so the lead stands
  still. The other game keys and a pad still drive the game, and the
  simulation keeps running.
- `F2` snaps back onto the game camera. `F3` toggles between the map list the
  game camera's visibility pass kept (the default, so flying out of the
  frustum shows what `FUN_00209140` threw away) and every non-hidden map
  primitive, drawn opaque in record order.
- Fog, the smear, the letterbox bars, the fade and the game-over underlay are
  left out: they belong to the game's picture, and the fog is measured from the
  game camera's eye. Billboards are put back in the world through the game
  camera's inverse view, because their quads arrive in its view space. The
  game's flat screen UI is left out too and shows only in the inset: text, HUD
  sprites and pips, the field menu and the Equip screen's bars. Models still
  draw, the overlay bucket's (`0x1005`) included, because they are placed in
  the world in front of the game camera. That covers the title logo (entity
  `0x48`), the Equip screen's spell icons and its ring, which is an ordinary
  entity. The original's debug text
  (`H`) also still draws, since the inset's picture is copied before it.
- `F4` toggles the inset, on by default: the game camera's own frame, a third
  of the window wide in the top-right corner. It is not a second camera. While
  it is up, `render()` runs the ordinary game frame first, whose last step
  already copies the finished picture into the smear's frame-feedback texture
  (world, smear, bars, fade, subtitles, no harness text). It then clears and
  draws the fly view, and puts that texture in the corner. That costs a second
  full render per frame, and only the game pass feeds `--frame-stats`.
- A left click probes through the fly camera, inset or not, except over the
  entity tree. It also picks: see Map tile pick, below.

#### Entity tree and inspector

The fly view lists every occupied pool slot down its left side
(`src/harness/entity_tree.*`, filled by `src/runtime/port_runtime_inspector.cpp`).
It is a tree because the pool has one: entity `+0x192` is the slot an entity is
attached to, the link `FUN_0020dc88` walks to the root of a chain, so the
lead's bandana sits under the lead and a held weapon under whoever holds it.
Slot 0, the lead, is listed although it is built outside the allocator. Each
row is the slot, the type id, and the noun from the type's handler name; a type
0x38 role also shows the type opcode 0x66 parked at `+0x1CE`.

- `F5` hides and shows it. The wheel scrolls it while the pointer is over it.
- Clicking a row selects the entity and frames it: the heading and pitch stay
  put and the eye backs off along the view until the entity's collision volume
  sits in the middle of the picture. Clicking the selected row again re-frames
  it, which is how to catch something that has walked off. The selection is
  drawn in the world as a cyan cylinder at its `+0x54`/`+0x58` size.
- Selecting opens a second window, the inspector, next to the main one. It
  shows what the port knows about the entity, live: status, the type's handler
  and whether it is ported, parent and children, position and world origin,
  facing, velocity, floor and contact, state, animation and pose, the flag
  words, combat numbers when there are any, links to other slots, and the
  descriptor, mesh and texture ids. The wheel scrolls it. Closing it clears the
  selection, as does the entity leaving the pool or `F1` turning the fly
  camera off.
- The inspector shares the main window's GL context, so it draws with the
  textures already uploaded. Its swap runs with a swap interval of 0, after the
  main window's, so it does not wait for a second refresh; the frame rate with
  it open measured the same as without.
- An attached entity's `+0x20..+0x28` are bone-local, so the tree frames the
  world origin its `SceneObjectView` was given, or its parent's when it is not
  drawn.

`--select-entity <slot>` does the click from the command line once the fly
camera is up. With `--screenshot` the inspector is written beside the capture
as `<path>-inspector.ppm`.

#### Menu bar and Player Params

A menu bar runs along the top of the fly view (`src/harness/harness_menu.*`,
drawn with the tree's panel kit in `src/harness/harness_panel.*`). Click a title
to open it. With one open, sliding along the bar opens the next, and a click
anywhere else closes it without reaching the world.

- **VIEW** has the `F2`..`F5` toggles, with a check box showing which are on.
  Keys and items go through the same `MapViewer::runMenuCommand`, so they
  cannot disagree.
- **PLAYER** opens the Player Params panel, and has Heal and the five test hits
  the `0`..`5` keys already did (`PortRuntime::applyDebugDamageKeys`). Those
  hits are stamped straight into the lead's hit mailbox, so they skip
  `FUN_00216140` and its defence subtraction. Only a real enemy hit shows a
  DEF edit.

The Player Params panel is the debug menu's SET PLAYER PARAM page,
`FUN_0026bc50`. Its rows come from the pointer table at `0x0034E228`, read out of
`SLUS_200.11`: `0x0058BFDA`/`DC`/`DE`, the lead's (pool slot 0's) `+0x12A` hit
points, `+0x12C` strength and `+0x12E` defence. All three are live in the port:
`+0x12C` seeds every attack the lead throws, and `+0x12E` is what
`original_hit_test.cpp` subtracts from an incoming hit. The four buttons per row
are the page's four pad bits, and each click is one call of the page's step
rule, ported as `FUN_0026bc50_step_player_param`
(`src/ported/debug/original_player_param_menu.*`):

- `+1` / `-1` (Right / Left) wrap 999 to 0 and 0 to 999.
- `+10` / `-10` (R1 / L1) are not a modulo: past 999 lands on old - 989, so 995
  goes to 6, and below 0 lands on old + 989.
- After every step `+0x128` takes `+0x12A`, floored at 1, so an HP edit sets
  the maximum too. The original makes that copy on every frame the page is
  open; the panel only makes it when a button is pressed.

Edits are applied between simulation steps, never inside one, so `--frames`
runs cannot see them. A battle reseeds the lead's HP and strength from the
roster when it starts (`battle_party.cpp`), so an edit made in the field does
not survive into one.

PLAYER > SPELLS opens a second panel, stacked under Player Params. It is a
shortcut to what the Equip screen and opcode `0xBC` do, and writes the same two
tables the same way (see the Equip screen section for both):

- **Spells.** Every item the lead could put in a spell slot is listed: the ring's
  own filter in `FUN_00230910`, an item record carrying the lead's roster bit
  and `0x100`, without its "count is not zero". Next to each is its
  `DAT_003437B8` count. There is no unlock bitfield: a spell is learned when
  its count is above zero, or when it is equipped, since equipping takes it out
  of the count. Not learned is drawn dim. `-` and `+` step the count between 0
  and 99, opcode `0xBC`'s cap.
- **Equipped.** The lead's `DAT_003437A0` row, Triangle / Circle / Cross, with
  the row picked by `FUN_002298D0` as the Equip screen picks it. `<` and `>` swap
  in the previous or next held spell by id, with `FUN_0022F620`'s three writes:
  the incoming count goes down one, the loadout byte takes it, the outgoing
  count goes up one. Nothing else is written, which is all the real swap
  writes too.

- **Character tabs.** A row of tabs across the top picks whose row the panel
  edits: Orphen, Cleo, Magnus, Sephy, Zeus and Mar, loadout rows 0..5, named
  by `FUN_0025B9E8(row + 0x86)` as the Equip screen names them. The lead's tab
  carries a `*` and is the one shown until another is clicked. Row 6 (type
  `0x16`, not a fighter) has no tab: its name message reads "No location
  name". The ITEMS panel follows the same tab, since its filter is the
  character's roster bit too; the counts are one table for everybody.

PLAYER > ITEMS is the same for the Item screen's list: the ring built as slot 3,
which asks the record for `0x80` in place of `0x100`. Twelve records carry it,
all with `0x00BF` (every roster bit 0..5), so every lead sees the same list:
the incenses and lanterns at `3C`..`41`, and `50`..`55`, Bug Attractor to Herb.
`-` and `+` step the `DAT_003437B8` count as on the spell panel. Items are never
equipped, so there is no loadout half. Using one is still a log line
(`FUN_0022FF20:28-50`), so the count is all an item does in the port so far.

A panel that would run off the bottom of the window starts a new column beside
the last, and the columns are centred together.

`--inventory-report` hexdumps both tables at the end of a run, laid out from
`0x003437A0` exactly as `pcsx2_read` prints the same span, so the two diff line
for line. A new game's `s01_e012` matches hardware byte for byte:
`05 07 01 | 00 00 00 | 05 07 01 | 18 1C 14 | 25 2A 21 | 2D 31 35 | 00 00 00`,
then 128 zero counts. Two things had to change to get there:

- `FUN_0022A418`'s copy of row 0 into rows 1, 2 and 6 is inside
  `if (DAT_003555D3 != 0)`, so it happens on section-14 (battle) scenes only.
  The port copied on every load, which put `05 07 01` on rows 1 and 6 in the
  field.
- `FUN_002239C8:35-67` refills empty slots **every frame**, not at load, and
  not from the defaults. Each of rows 0, 3, 4 and 5 has one item it puts in any
  empty slot (`01` the Sword for Orphen, then `14`, `21`, `2D`).

Equipped spells are not counted, so a new game shows every count at zero with
Pyro, Lightning and the Sword equipped, on hardware too. The spell-earn scene
(`s14_e031`) equips the spell it demonstrates into Triangle with `0xBD` method
`0x78`, which is the same swap, so after the crab Hand of Pyro reads one.

A battle builds its button bindings from the loadout when it starts
(`FUN_002432D8`), so a loadout change made mid-battle shows in the next one.

The panels are drawn after the game view inset, so they lie over it, and the
menu bar is drawn after the panels, so an open menu lies over them.

`--pick-pixel` is repeatable, one click a frame, which is how a capture drives
the menus: `--pick-pixel 90,12 --pick-pixel 130,32` opens PLAYER and then
Player Params, at 960x720. The rows below it are 23 pixels apart: SPELLS at
`130,55`, ITEMS at `130,78`.

#### Map tile pick

The map primitive under the pointer is tinted amber every frame
(`src/harness/map_pick.*`). A left click in the fly view selects whatever is
nearest along the ray: a map primitive, or an entity triangle that was drawn.
The camera stays put. A selected primitive is tinted cyan, and its outline shows
faintly through walls. The inspector then shows it instead of an entity:

- shape, centre, normal and slope, and whether the fly view drew it this frame
- the 0x78 collision record: `+0x00` with the ground-scan bits named
  (`0x800` ground, `0x100` ceiling, `0x200` flat-height, `0x10000` dynamic),
  the terrain word at `+0x04` (climbable, climb-lip, the `0x1000000` hazard),
  and the selector
- the 0x80 draw record: `+0x70` flags, alpha and fade, and each material slot
  with its UVs or flat colour
- the corners as vertex indices and positions, which are live, so a moving door
  reads where it is now
- every entity whose `+0x0A` (the primitive its last ground sample landed on)
  names this one

The pick tests the triangles the renderer draws, against the list the fly view
is drawing: the game's culled list by default, or the whole map under `F3`. So
a primitive the view is not drawing cannot be picked through. The hover tests
map primitives only. Posing every entity triangle each frame is not worth it,
so a primitive behind a character still tints. A pick costs about 13 us on
`s01_e012`'s 3,496-primitive whole map in Release.

`--pick-pixel <x>,<y>` does the click from the command line, two frames after the
fly camera comes up, and prints `[pick] map primitive #N` or
`[pick] entity slot N`.

It runs once per rendered frame on wall-clock time, outside `PortRuntime::update`,
so the simulation cannot see it and `--frames` runs never touch it. Entering and
leaving print the pose as a `--fly-camera x,y,z,yaw,pitch` argument (game space,
degrees, yaw 0 along +x, positive pitch looking up). Pass that back with
`--screenshot` to photograph the same view; `--fly-camera game` starts on the
game camera.

### Fast forward

Holding `P`, or the pad's right trigger, runs the simulation as fast as the
machine will carry it. It is the original's own debug affordance: `FUN_002000c0`
enters its vsync-wait block only while `DAT_003555db == 0 || (DAT_003555f4 & 2)
== 0`, so with the cheat flag set (see `analyzed/`) holding R2 drops the wait and
the present together and the game free-runs.

The port does the same thing in the same place -- `main()` keeps calling
`PortRuntime::update` with `render` and `swapBuffers` skipped. That is sound
because the headless `--frames` path already runs `update` with no renderer at
all; nothing in a step reads back from a draw. On this machine a step costs
about 0.34 ms against a presented frame's ~16.6 ms, so the ceiling is roughly
40x.

Three things differ from the original, all of them harness concerns:

- **Steps run against a wall-clock budget**, 20 ms, rather than truly uncapped.
  One press cannot then starve event polling, so the key can still be let go of.
- **A frame is still presented about every 33 ms**, at whatever point the
  simulation has reached. A window that goes black until release is worse to
  work with than one that updates at ~20 Hz while the scene sprints, and it is
  the only way to see where to stop.
- **Audio is muted while held.** The mixer runs at 1x on its own thread, so
  every cue the sprinting simulation fires would key on over the top of the last
  one. The device stays open and `mix` still runs -- that is what drains the
  pending key-on queue -- and the harness zeroes the buffer after it.

Releasing prints what the burst achieved:

```
[ff] 2612 steps in 1.04 s (2511.5 steps/s, 41.9x)
```

`--screenshot` ignores the key outright. That path promises one simulation step
per rendered frame so two builds photograph the same instant, and a held key
must not be able to move it.

