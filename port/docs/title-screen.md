# The title screen

`--scene` is now optional: `--disc-root <dir>` on its own boots into **s12_e010**,
which is what `FUN_002000C0:163-165` does at power-on --

```
DAT_003551F4 = 0xC;  DAT_003551F0 = 10;  DAT_003551EC = 0x2001;
```

section 12, entry 10, with the same request bits any cold boot uses. **There is no
title game mode.** `DAT_00354D2C` reads 0 at the prompt on hardware, so the title
screen is an ordinary field frame; everything that makes it one belongs to scene
module 12, `FUN_00271220`, which `FUN_0022A360` picks out of `PTR_LAB_003252B8`.
`FUN_0022A418:371` even skips the navmesh build for `DAT_003551F4 == 0xC`.

The module is ported in `src/ported/scene/title_screen.h` plus the mode 3 and
mode 4 arms of `PortRuntime::FUN_0032536c_scene_module`:

- **Mode 3** (`FUN_0022A418:369`, between the init and start script entries)
  builds the shot. The subject is the entity named by script work word 1 -- slot
  10 here, the kneeling actor -- and the camera is an ordinary manual camera
  (`FUN_00217D70`) at the field follow radius and pitch, 3.0 and 0.36651909, that
  `FUN_00216930` seeds. The actor's facing is set once to `wrap(cameraYaw + pi)`
  and never moved again, which is why the orbit slowly comes round to his side.
- **Mode 4** walks `PTR_FUN_003256F8`. State 0 (`FUN_00271470`) starts music slot
  1 and clears the counters; state 1 (`FUN_00271558`) is the title screen, and it
  draws the prompt, steps the orbit, and watches `uGpffffb686 & 0x840`.

The orbit itself is `FUN_00272010`: `fGpffff8f58 = 0.0001875` radians per frame
tick, so a revolution is about 1047 frames.

## The logo is an entity, not an overlay

Mode 3 spawns entity type **0x48** into the record at `0x0058C7E8` -- pool slot 5
-- and parks it at `DAT_00352EC4 = 1.6` over the pedestal, with `+0x04 |= 0x100`
so `FUN_002262C0` leaves it there. Its actor handler is `FUN_00239E78`, the
no-op: it is a billboard and nothing else.

What turns it is **`FUN_002255B8`**, three lines called from `FUN_002239C8:166`,
after the camera update. It rewrites slot 5's facing from the camera every frame,
by type: `0x49` takes the camera yaw outright (that is `FUN_0025D5B8`'s cut-in,
which the port has no other half of), and `0x48` takes `wrap(cameraYaw + pi)`.
Miss it and the logo is a flat quad seen edge-on, filling the screen in
perspective. It was found by poking a sentinel into `0x0058C844` on hardware and
stepping one frame -- the camera's angle came straight back.

That is only the yaw. **`FUN_00271558:35-38`** -- title state 1 -- copies the
other two every frame: `+0x154` takes the camera pitch (`DAT_00355648`,
`-0.157` at the prompt on hardware) and `+0x158` the roll (`DAT_0035564C`). The
port missed it for a while, and the logo stood bolt upright under a camera
looking down at it: a faint trapezoid, easiest to see as the lean in the
vertical bar of the O.

## The prompt is one sprite off a map page

`FUN_00272100` draws the `FUN_00239020` entry at `0x00325738`: texture slot 5,
256x40, entry `(-128, -112)`, which is `(192, 336)` in the 640x448 screen the
subtitles land in. Slot 5 is an ordinary map texture page -- `0x0103` for this
scene -- whose top 256x40 band is the words "Press START button". The rest of
that page is the copyright block the legal screen uses.

## START is New Game

START or Cross runs the original's states straight through, with one shim in
the middle. `FUN_00271558` state 1 sees the press, and in the same call puts the
eye back at orbit angle 0 around slot 0 and releases the logo and the two
entities in script work words 8 and 9. State 2 (`FUN_00271858`) waits for the
logo's slot to be free, raises flag `0x511`, and would open the new game / load
menu (`FUN_00236780`) as state 3.

**State 3 is the shim.** The menu is not ported, so the port takes its first
frame as a pick of the "New Game" row -- `FUN_002718F0` case 0:

```
DAT_00342B7E = DAT_00342C8E = 0;  FUN_002663A0(0);  FUN_002663A0(0x500);
uGpffffad54 = uGpffffad48 = 1;    state 5
```

Nothing after that is the port's. State 5 does nothing on retail (its orbit is
behind the `DAT_003555C7` cheat byte). **s12_e010's own script is waiting on
flag 0**: it stands the actor up, fades, queues movie 2 (which chains to 13) and
requests s01_e012 with `0x2001`. `FUN_0022A418:42` then sees flag `0x500` and
runs `FUN_002294D0`, the new-game reset: every event flag cleared except
`0x50C..0x50F` and the `DAT_00342C8F` byte, the party records reloaded, the
first 0x80 item counts zeroed, the default loadout copied back, and flags
`0x501`, `0x513` and `0x7BC` raised.

Checked on PCSX2 from `savestates/title_menu_new_game.p2s` (the menu, cursor on
New Game): the same stand-and-fade, the anime opening, s01_e012 with request
`0x2001`, and flag bytes `0xA0..0xA3` reading `02 00 08 00` after the reset on
both. Two of `FUN_002294D0`'s writes have nowhere to land in the port -- the
64-byte counter table at `0x00343838` and `DAT_00355638`.

Also not ported: the idle hand-off to the attract demo at `0xE101` ticks
(reported once instead), the cheat-code button sequence `FUN_00271558` watches
for -- the one that sets `DAT_003555DB` -- and every other row of the menu.

Servicing a scene-change request that carries bit 1 used to hang: the port
handed `FUN_0025D238` a literal zero for `DAT_003555BC`, so the fade level never
climbed and the request waited for ever. Nothing set that bit before this.

One known difference against hardware, and it is not the module's: s12_e010's
steam is far too thick in the port. The scene arms 28 plume emitters (opcode
`0x10E`, 77 live puffs) and a 15-record haze field (`0x109`); at the same orbit
angle hardware draws thin wisps where the port draws a wall of white.

