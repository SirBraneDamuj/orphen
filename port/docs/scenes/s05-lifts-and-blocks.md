# Chapter 5: lifts, hazards and turning blocks

### `s05_e011`'s lift: 0x11A, 0x120, 0x148 and 0xCF

After the reunion the party steps onto a lift, which in the port never moved.
The stream at blob offset 0x6468 launches two scripts when they board. Slot 3
(0x48A2) drives the lift through 0x7E, and slot 4 (0x4991) waits for the ride
to finish, then raises flag 0x139, which the stream's record 99 waits on. Slot
3's first instruction was **0x11A**, which was unported, so the script halted
there every frame and the lift's collision group never got a 0x7E write. The
scene uses four extended opcodes the port lacked:

- **0x119/0x11A/0x11B (`FUN_00264A60` -> `FUN_002257E0`)** take two one-based
  track indices and copy the first track's (u, v) offset onto the second.
  0x11A aims at the map's UV animation block. The start entry also ends in one,
  so it had been halting before its last instruction since the scene was first
  loaded.
- **0x11F/0x120/0x121 (`FUN_00264BE8` -> `FUN_00225740`)** rewind a track
  (all of them for a negative index) without touching its running flag.
- **0x148 (`FUN_00265788`)** has no operands and returns `cGpffffb6e4`, the
  camera's free-look latch. Slot 1 (0x45B9) polls it.
- **0xCF (`FUN_00264740`)** takes a selector and an inline blob offset, and runs
  the selected entity's `FUN_002148A8` swept hit test with the four parameter
  bytes at that offset. The hazards run it every frame.

Replayed with `--arm-stream 6730:5 --set-event-flag 310 --set-event-flag 311`,
slot 3 runs its group move from frame 103 and flag 0x139 rises at frame 415.
Armed that late, the party is not standing on the lift, so the replay says
nothing about riding it. In play the lift goes on to `s05_e013`.

### `s05_e013`: riding the lift, and when the lead's physics runs

The lift carries the party from `s05_e011` into `s05_e013`, which opens with the
lift lowered to -3 (raw -3000 through 0x0F's x100 literal) and raises it 0.008 a
frame to its rest height. In the port the party stayed where it was dropped:
the lead fell through to the floor at -4, Magnus and Cleo hung at -2.70 while
the lift rose through them, and the three walk-to-mark scripts that follow
waited forever for them to arrive. That was two divergences:

- **`FUN_002262c0:41-91`, the riding block, was unported in both physics
  copies.** If the primitive an actor last stood on (+0x0A) has record78
  `+0x13` set and belongs to a group (`+0x14`) whose dirty byte is live, it
  resamples the floor with `FUN_00227070`. A floor that rose lifts `+0x28` onto
  it, as long as the sample is still on the same group. A floor that dropped
  lowers `+0x4C`, and takes a settled actor with it if the drop is under
  `DAT_00352424` (0.3). Record78 `+0x14` comes from descriptor[`+0x10`]
  `+0x1A`, which group resolution fills with the group's index. The port's
  `DRecord78` had stopped at `+0x13`.
- **The lead's physics ran in the wrong place.** The port called
  `FUN_002262c0` at the end of the lead controller. The original controller,
  `FUN_00251ED8`, never integrates. `FUN_002261E0` does slot 0 along with the
  rest of the pool, at `FUN_002239C8:135`, after `FUN_00208450` has moved the
  groups. On `s05_e013`'s first frame the start entry has just lowered the
  lift, so a lead integrated before the move stood on the lift's old position,
  found nothing and fell. The other handlers order it the other way round:
  `FUN_002245D8` (the cutscene frame) and `FUN_00224218` (the pause, save
  prompt and Equip tail) run `FUN_002261E0` before `FUN_00208450`, so the port
  now picks the order by frame kind.

Arriving from `s05_e011`, the party rides from -2.69 to 0.30 by frame 380 and
walks off. The stream runs on into Magnus and Orphen's "nice shelter from the
storm" exchange, and the lead is back under player control by frame 3000.
Moving the lead's physics changes `s14_e002` as well. Its script report is
identical, but the lead now spends its battle movement request after the
battle tick, as `FUN_002239C8` does, and at frame 3000 the lead faces
slightly differently and the mast boss sits 1.0 lower on y. None of that is
checked against hardware yet.

### `s05_e013`'s west lift: 0xA9, and 0x13B in `s05_e018`

The room has a second lift at its west end. Its trigger is a floor test, not
an interaction: the per-frame entry's opcode 0x61 tests the lead's terrain
word, and a floor carrying bit 0x1 sets work[0x11] to 2. That picks the second
of four state machines, which switches on work[0x12]. Case 0 frames the
camera; case 10, at 0x200B, loops on opcode 0xA9 until it returns 1. The
port halted on that 0xA9.

0xA9 (`FUN_00262F80`) takes x and y expressions and an inline walk/run byte.
Each frame it adds one step toward the point to the lead's +0x30/+0x34 and
aims +0x5C at it. The step is `iGpffffb64c * 64 * DAT_00352C9C` walking and
`* 128 * DAT_00352C98` running, both scales being 1/81920 in the retail ELF.
It writes animation +0xA0 directly (0xB walking, 0xE running), with no
`FUN_00225BF0` call. Once the point is closer than one step it sets +0xA0 to 1
and returns 1. It does not snap onto the point. `analyzed/ops/0xA9_*` says it
does, and that note is wrong. While walking it plays a footstep on a keyframe
carrying 0x100, the same check `FUN_00256FF8` makes, but without its dust.

Headless: `--scene s05_e013 --from-scene s05_e011 --place-slot
0,-6.0,0,0.5:3100-3105` walks the lead from -6.0 to -6.5 in 20 frames (0.025
per frame), rides the lift down, and loads `s05_e018` at frame 3498. There the
lead rides up from -8 to -4, walks off, and has control again by frame 4200.

`s05_e018`'s start entry opens with 0x13B (`FUN_002653A0`), which stores one
expression as the byte `DAT_00355657`. The string `FUN_0026BFC0` prints for a
value over 6 names it: "set_bokasi_lev", the blur level. `FUN_002000C0` reads
it once a frame to draw that many `FUN_002020A8` passes. Each pass takes a
depth threshold between 4.0 and 19.0 and redraws the frame offset by a few
pixels, depth-tested, so distant geometry blurs: a depth of field.
`FUN_0022A418:284` resets the level to 6 between the init entry and the start
entry, so every scene in the game runs with all six passes. **The port draws
none of them.** It stores the level and nothing reads it yet.

### `s05_e011`'s spinning hazards, and why nothing could hit the lead

The four type 0x276 props (slots 10-13, each with a partner in slots 42-45)
are driven entirely by the per-frame entry; the type's own behaviour,
`FUN_002CFE08`, is a plain streamed prop. Every frame the script does the
following for each hazard:

- Adds work[6..9] to +0x154 (register 0x1F) and copies the result to the
  partner.
- Changes that speed by 0.1° per step with `0x37 ... 0x29` / `0x2A`. It speeds
  up or slows down depending on whether +0x154 lies in (0, π), and keeps the
  speed between bounds.
- Counts down +0x1B4 (register 0x3F) on both entities while it is non-zero.
- Runs `0xCF` (`FUN_00264740` → `FUN_002148A8`) when the countdown is zero. On
  a contact it sets both countdowns to 15, clears both hit sets with `0xD0`,
  and writes register 0x17 (+0xC4) on the lead.

Four port bugs broke that:

- **Registers 0x1F/0x20 (and 0x17) wrap.** `FUN_0025C8F8` passes all three
  through `FUN_00216690`, as it does 0x0D. The port stored 0x1F and 0x20 raw,
  so after the first turn the angle never fell in (0, π) again and every
  hazard was stuck at whatever speed it had then.
- **0xCF returns the contact count.** `FUN_00264740` ends on the `jal`, so v0
  still holds `FUN_002148A8`'s result. Ghidra types it `void`, and the port
  returned 0, so the script never saw a hit.
- **Register writes 0x2C-0x2F and 0x3F were missing.** The damage (15) goes
  into +0x12C through 0x2E, and the cooldown lives in +0x1B4. On a streamed
  prop that word is the port's `pushHeading1b4`, so 0x3F stores the dword bit
  for bit. `0xD0` (`FUN_00264790`, which runs `FUN_00215E48` on the selected
  entity) was also missing.
- **The lead was never a hit candidate.** `FUN_0022A418:201` runs
  `FUN_00229C40` on slot 0, which sets status byte 1 and copies descriptor
  +0x04 into +0x02 (type 1: 0x0001). The port builds the lead from struct
  defaults and left slot 0 Free with +0x02 = 0. `candidateAccepted` refuses
  both, so outside a battle that retypes the lead, nothing in the game could
  hit the player. `FUN_0022a418_stamp_lead_player_flags` now sets both.

Headless, with the lead pinned at (1, 0) and then released: hazard 11 hits it
for 15 a contact and knocks it back to x = -2. Magnus, standing near hazard
10, takes 30 a contact. The regression guard moves as a result: enemies in
`s14_e001`, `s14_e002` and `s14_e012` now land 3-4 hits on the lead in the
first 3000 frames, where they landed none before. None of that is checked
against hardware.

### `s05_e013`'s caged chest, and the push-out the lead never had

Chest slot 20 sits at (5.5, -2.5) under hanging cage slot 16. The cage's
collision is static map geometry, not a group: an invisible octagon (record78
leading word bit 0x20; 551, 552, 554 and 555 among its faces) from an underside at 0.799988 to a
top at 3.6. FUN_00227840 stops scanning at the first face `<= head`
(`c.le.s` at 0x002279B4), and the lead's head is 0 + 0.8, so a corner under the
cage ends the scan on the underside with the 3.6 top latched. To an 0.8-tall
lead standing on the floor the cage is a pillar. Walking up to the chest stops
at y = -1.78, which is still in Cross reach.

State 0x13 (0x002552B0, disassembled from the ELF and matching the port) puts
the lead back one radius sum in front of the chest: (5.5, -2.15). Three of its
four footprint corners land inside the octagon. Every destination near there
has a corner reading 3.6, so every move and all five retries were refused, and
the lead stood there for good.

Hardware does not get stuck. A PCSX2 run (`savestates/s05_e013_entry.p2s`,
then `s05_e013_chest_message.p2s` at the item message) ends the exit fade
with the lead free at (5.14, -1.79): two (-0.18, +0.18) pushes. Three pieces
were missing from the port, all in `FUN_002262C0` and its callees:

- **The lead's push-out.** `FUN_002262C0:111-209` runs for slot 0 every 64th
  frame (`(DAT_003555B4 & 0x3F) == 0`). It also runs, like for every actor,
  on any frame a collision group moved (`DAT_003555D0`), if `+0x08` bit 0x20
  is set. The non-player copy had it; the lead's did not. Its mask-0xF
  fallback is not the non-player copy's either: it moves the lead outright on
  the first heading where *any one* probe corner is at or below the feet,
  re-runs the test, and only on a second pass turns the offset into a request.
- **FUN_00227390 skips the buried corners.** The push-out builds its mask in
  workspace +0x162. FUN_00227390 tests that byte before each corner (`((+0x162
  ^ 1) & 1) != 0` and so on), so for the rest of that solve a destination is
  judged only by the corners that were not buried. With 0xF it returns 1 at
  once with +0x4C. Without the skip the push itself was refused: the hardware
  cell lists, read out of RAM, match the port's exactly, and the buried corner
  reads 3.6 on both.
- **Lead `+0x08` was 0.** `FUN_00229C40` seeds it from descriptor +0x16 and
  ORs 0x20 in for types below 0x272. Every field dump reads 0x26. Without
  0x20, the lead waited for the 64th-frame leg and took about two seconds to
  get out. With it, the port ejects during state 0x14 as hardware does.
  `FUN_0022a418_stamp_lead_player_flags` ORs it in. The 0x10 pose-snap bit is
  left out, because no field dump carries it. The remaining 0x04 is set on the
  respawn path.

The non-player copy (`actor_frame_update.cpp`) had the same two gaps. Its
fallback also waited for all four probe corners to be clear, where the
original takes any one and moves first. It now matches.
`FUN_00227070_sample_ground` takes the mask as an optional last argument (0
for every other caller), and `ActorEnvironment::terrainSurfaceMasked` is the
physics step's way in. The mask covers the movement loop's destinations and
the rise test at :486, and 0xF accepts at +0x4C without sampling. A
single-point entity (+0x04 bit 1) takes FUN_00227390's own path ahead of the
mask test, so the mask never reaches it.

That moves one thing in the guard: `s14_e001`'s crab walk-in (frames 11-100).
The crab climbs a ramp with its uphill corners above its feet, so it is pushed
every frame (mask 12, `DAT_003555D0` is 1 every frame on hardware too). Judged
on its downhill corners, it now sinks up to ~0.35 into the ramp. Every 8
frames or so the 0xF fallback throws it half a unit back for a frame, before
the script's next position write. That is the disassembly's behaviour
(0x00227424-0x00227644) on the port's inputs, but it is **not
hardware-checked**. Entered from the debug menu, hardware has no walk-in at
all: the crab is at its start spot, (0, -3.18, -1.0), by the port's frame 6.
Whether the port should be walking it in there is a separate question.
`s01_e012`'s Magnus-off-the-crate push, the hardware-checked case, is
unchanged. Slots 59/60 in `s14_e001`, 0.0004 below a deck at frame 1, stay
put and settle onto it, as the `crab_start` dump has them.

Regression guard against 161bafd: `s03_e001` shows the new `+0x08` bits in a
register-4 write (306 = 0x110 | 0x22). `s14_e001` diverges from about frame
900. The animatic places the lead at (1.2, -3.2, -0.5) on a deck that samples
-0.499588. All four corners read 0.0004 above the feet, the 0xF fallback moves
him half a unit, and he is pushed into the pool. Both builds then reach the
hardware dump's crab-start position, (-0.08, -0.25, -0.5), by frame 1500. That
stretch is **not hardware-checked**: entering `s14_e001` from the debug menu
skips it, and is at crab start 300 frames after load.

### `s05_e018`: the texture table, the overhead camera and the turning blocks

Three separate gaps, all checked against `savestates/s05_e018_entry.p2s`.

**Every surface drew one sheet over.** `FUN_0025B600` copies sixteen halfwords
from script header word 6 + 2 into `DAT_00325394`, and `FUN_0022A178` loads
entry N into global slot N, *skipping* a zero. s05_e018's table is
`{0, 005e, 021d, 0059, 021b, 0218, 0060, 005d}`. Its pages go into slots 1..7
and its primitives' material types run 1..7. The port loaded the bundle's
pages from slot 0 and indexed them by material type, so the floor drew from the
wall sheet and the block tops from the gold trim. A GS dump confirms hardware
draws the floor from slot 2 (tbp 6280) and the block emblems from slot 4.
`SceneScript::textureSlotTable` now keeps the table. `FUN_0022a178_bind_map_textures`
runs right after the script loads, as `FUN_0022A418` orders it, and gives
`MapViewer` a slot → page map. A zero slot falls back to the global slot cache,
which is the previous scene's page on hardware too. Every scene in the
regression guard has its table starting at slot 0, which is why this never
showed before.

**The camera was the follow camera.** The per-frame entry runs `0xBE(1, 0x1E)`.
`PTR_FUN_0031E730[1]` is `FUN_00267288`, one `sb` of the argument into
`cGpffffb6e1`. Submodes 0x1B..0x1E go to `FUN_00218710`, which rebuilds the
pose from the target every frame with no smoothing:

| submode | distance | pitch |
| --- | --- | --- |
| 0x1B | `DAT_00354C88` | `DAT_00354C90` (set by `FUN_002186E0`) |
| 0x1C | 3.0 | `DAT_00352290` = -0.977 (-56°) |
| 0x1E | 5.0 | same |
| 0x1D | 2.0 back along `DAT_0058BF0C`, slope -0.5 | — |

The look-at is the target + 0.8, and the stored eye is 0.4 below the true one,
like the follow camera. L1/R1 turn the shared yaw `DAT_00354C8C` by
0.0015625/tick, and nothing else writes it. Hardware in s05_e018 matches the
port to four places: look-at (0.284, 0.719, -3.2), eye (-2.5120, 0.719,
0.5452). `s05_e013` runs `0xBE(1, 0x1C)` and hardware holds 0x1C there too; its
eye also matches. That is the only other change in the guard: its smoke cloud's
visible count moves with the camera.

**The turning blocks did not carry the lead.** Groups 2..6 are each four
blocks at different heights around a post. The script turns them about z at
0.2, 0.3 or 0.4° a frame. Groups 7..11 are their type-4 visual copies.
`FUN_00208450:301-327` carries the lead when the group it just applied is the
one under the lead's `+0x0A` and `+0x0C` bit 0 is set. It composes the change
in rotation and translation since `DAT_00354C4C[group]`, applies it to the
lead's position about the pivot, and adds the result to `+0x30`/`+0x34`. The
rotation's z change is added to the facing. `DAT_00354C4C` is 0x18 bytes per
group, zero at load, and is stored after every applied pass for a group that
is not type 4. The vertical half was already in `FUN_002262c0`'s riding block.

Checked with the lead's `+0x0A` set to block primitive 955 on hardware: 61
frames turn the facing by 0.2125 and move him round the post. The port, nudged
onto primitive 958 with `--place-slot 0,2.75,4.3,-2.0:3-3 --hold-stick 0,60,6-7`,
turns by 0.209 over 60 frames and moves (2.71, 4.25) → (2.40, 4.21). That is
the same 0.209 rad about the pivot (2.75, 2.75). Only the lead is carried:
the original has no such step for any other slot.

Two traps found on the way. `--place-slot` on frame 1 samples the ground
before the groups' first pass has placed them, so put the lead down on frame 3
or later. And a teleported lead keeps its old `+0x0A` until it walks, on
hardware as well as in the port, so it will not ride until it moves.

### `s05_e019` sent the lead straight back down: placement keeps the height

Riding `s05_e018`'s east lift loaded `s05_e019`, which loaded `s05_e018` again
on its first frame. `s05_e019`'s per-frame entry has a fall check at `0x526`:
`if (0x53(0, 2) <= -3.5) 0x8C(5, 18, 0, x, y, 9.0)`, which drops a lead that
fell down the shaft back into `s05_e018` from above. It runs before the arrival
machine and before `FUN_002261E0`, so on the first frame it reads wherever the
load put the lead.

The lift's warp (`s05_e018` `0x1296`) asks for (6.5, 0, -3.0).
`FUN_0022A418:215-224` copies that into `+0x20..+0x28` and writes only
`FUN_00227070`'s ground sample into `+0x4C`/`+0x50`. `FUN_00229c40` has just
rebuilt the entity, so `+0x2C` is zero and `+0x0C` has no grounded bit, and
the first physics step does the settling. The port's `resetAt` snapped `+0x28`
onto the sample instead, here the lift's floor at -4, so the check fired.

`standLeadAt` now passes `keepPlacedHeight`. `--spawn`, the script teleport and
the port's spawn guess still snap. On hardware, with the warp staged by hand
from `savestates/s05_e018_entry.p2s`, the lead is at -2.60 and rising 13 frames
in, and ends at (5.0, 0, 0) standing, facing π. The port now does the same
with `--scene s05_e019 --from-scene s05_e018 --arrive-at 6.5,0,-3.0`.
`--arrive-at` is new: it boots a scene the way a `0x8C` warp would, with
request bit 1 and `DAT_00325340` staged.

Every boot placement changes with this, because the defaults block's z is now
kept. Most scenes differ only in the spawn line. `s05_e019`'s own default is
(-1.249, 5.162, 0.875), over the shaft, so a cold boot there falls and is sent
to `s05_e018`. Hardware does the same: a `0x2001` load lands on -4 and leaves
for entry 18 as the save prompt closes. `s14_e002`'s default (0, 1.5, -0.5)
used to snap to the sea floor at -45. The lead now falls from -0.5 until the
script puts it on deck, and the run's later state (boss position, facing)
diverges from there. That scene is not hardware-checked.

