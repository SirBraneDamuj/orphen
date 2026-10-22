# s14_e004: the gear room

## s14_e004: the gear-room battle and its two cutscenes

Where Magnus's path lands after the lifts. A preamble (Orphen alone, "Who's
there!?"), a battle against three type 0x8B, then -- on the first hit -- an
intermission: a tentacle monster rises behind the lift, Magnus and Sephy ride
it up, and the fight resumes with them off to smash the gear. Checked against
`savestates/s14_e004_preamble_start.p2s`, `s14_e004_battle_start.p2s` and
`s14_e004_intermission_start.p2s`, all cut from a debug MAP SELECT load
(MP14, BG04, **Circle** confirms).

**A debug load skips the preamble unless BFLG 0 is cleared first.** The init
entry tests flag 800 (`0x320`) and arms the short stream `0x7300` instead of
`0x7230` when it is set. A section-14 request does not clear BFLG
(`FUN_0022A418:120-124`), so whatever the previous map left there survives;
`entry_stable.p2s` carries it set. Write `0x342BD4 = 0` before confirming.

Six things were wrong, and every one of them was visible.

### Parked entities: opcode 0xBD method 0x75

`FUN_00244B40(entity, mode)`. Mode 2 negates the type id and the
`DAT_005A96B0` byte (1 to -1), mode 1 puts both back. `FUN_00242A18` hands an
entity method the entity in `a0` and arg3/arg4 in `a1`/`a2`
(`0x00242A28`, `0x00242A38`) -- the mode Ghidra's one-argument call drops.
The init parks Magnus, Sephy, the three enemies and the monster; the port had
no method 0x75 and drew all six from frame 0, which is the "party members
visible right off the bat". `EntityPool::FUN_00244b40_park`.

The draw walk also had to learn the byte. `FUN_0020C5A8:44` queues a slot only
when it is above zero, the same signed test `FUN_00239CE0` ticks on; the port's
walk took every non-free slot, so a parked one stopped ticking but kept
drawing.

### Bit 4 of DAT_00354FC2: opcode 0xBD method 0x76

`FUN_00244BF0(entity, mode)`, the battle's scripted-beat bit. Mode 1 raises it
(and zeroes the smear), mode 2 drops it and returns the player to idle. While
it is up, `FUN_0023C340:51-61` does not drive the battle camera at all -- the
shot is the scene script's -- and shuts an open target display; and
`FUN_002D73E8` draws a target cursor only for `state & 5 == 1`, so the
reticles go. The intermission calls mode 1 at `0x4EE0` and mode 2 at
`0x30C8`. Without it the battle splines flew over the whole intermission,
reticles and all, and the Magnus close-up never got the camera.

### A yield parked by opcode 10 leaked into the next timed wait

`FUN_0023FD30` stores every handler's return in `+0x2E` and zeroes it when the
return is negative, before the next opcode runs. The port's `continue` paths
skipped that, so opcode 5 sub-op 1 -- which loads `operand * 2` only when it
arrives with zero -- inherited the 200 frames the master had spent parked
waiting for the first hit, and its 240-tick wait ran out after 100. Hardware
reads yield 466 at `0x5DF0` 7 frames into the wait. The zeroing is now in the
loop's increment, where no `continue` can miss it. This changes the pacing of
every AI script that waits after a `continue`, `s14_e003`'s dummies included.

### Every line here is a call, and the window was handed the callee's end

The scene's `0x33` sites are all `33 <rel32> 10 <rel32> 00`: a text-code call
into a shared record that ends `0C nn 02`. `DialogueStream`'s pre-scan
follows the call by re-aiming its local `end`, and a callee that closes on
`0x02` never unwinds it -- so the window was opened on an end *below its own
cursor* and finished every record before placing a glyph. No subtitles at all.

Then the ninth line drew `SÊÿÿ`: its call's displacement bytes. Each call into
a `0x02`-terminated record leaks a stack frame, and `FUN_00237B38:42` resets
the depth (`uGpffffbcf4 = 8`, gp-0x430C, `DAT_00355C64`) on every open. The
port never did, so the stack filled after eight lines and the call was refused.

### 0x1B runs where the walk reaches it

"Who's there!?" is `0C 0F | 1B 2B 03 | ...`: flag `0x32B` fifteen frames into
the record, and the next beat is gated on it. The port collected a record's
`0x1B` codes up front and set them when it closed -- right for a flag at the
tail, which is every site `s01_e012` has, and 87 frames late here. The window
now queues each one as its walk steps over it and `DialogueStream` sets them
after the step, the way `LAB_00239AA0` does inside `FUN_00237FC0`. The long
Orphen line's camera cuts (`0x32B`, `0x32C` mid-record) come back with it.

Side effect elsewhere, and it is the original's: a tail flag now lands when
the walk reaches it rather than on the close a step or two later. `s01_e012`'s
chain from Volcan's `0x6A` moves 2 frames earlier, `s03_e001`'s lines about a
frame each.

The same pass made `0x1A` wait on one voice flag, as the original's
`DAT_00356788` is one byte: a record's own clip **or** a bank clip that opcode
0xBD method 0x77 started. Battle scenes voice their lines with 0x77, not 0x18.
An estimated hold -- a record with no clip -- no longer blocks `0x1A`.

### The monster's record must stay empty until it ticks

The group-2 placement path bound every battle placement into the actor table
at the spawn, standing in for the enemy's own state 0 (`FUN_0023F8B8`). For a
parked entity that is early: the monster (type 0x93, record `0x21`) has no
binding on hardware until the intermission wakes it, and the master's opcode
17 at `0x5E38` ("wait for record 0x21 to reach action 6, or be gone") passes
only because of that. Bound at spawn, the master waited forever for an action
an unported monster never takes, so Orphen's control block never had flags 8
and 4 cleared and nothing got its post-intermission AI. The stand-in bind now
happens on the entity's first tick (`OriginalEntity::deferredBattleBind`),
and a ported state 0 that binds first takes precedence.

### Verified, and what is still out

With Hand of Pyro held 1400-1440 and hardware aligned on the intermission's
record `0x7348` (hw frame = port frame + 2743), the preamble matches hardware
to the frame (gate `0x32B` at 977, `0x324` at 1110), and the intermission
matches shot for shot to "Get moving!": first shot, the monster's rise,
Orphen's recoil, the party on the lift, the Magnus close-up, the camera cuts.
The port's typewriter runs 3-14 frames ahead of hardware per line, most likely
clip-end slack on the IOP; not chased.

`--battle-vm-trace` prints every battle-VM instruction with its frame, block,
pc, bytes and incoming yield; it is how the yield leak showed.

### Type 0x93, the tentacle monster: a lap, not a home spot

`FUN_00296030`, seventeen states at `PTR_FUN_00325c80`, now in
`original_tentacle_monster.cpp`. Its rest state is 14, which rides an
**encounter actor path**: the placement sub-blob's `+0x64`/`+0x68` is an array
of 0x210-byte path records (`+3` kind, `+4` waypoint list, `+8` lap length in
ticks, `+0xC` a chord-knot spline `FUN_00266A78(...,1)` builds), and
`+0x6C`/`+0x70` a 16-byte lookup table `FUN_002475B8(type, id, key, nth,
field)` searches. `BattleEncounter::FUN_002471e8_build_paths` builds them.
s14_e004 has one, `(0x93, 0x21, 0xFF)`: thirteen points round the room at
head height, `0x6900` ticks (840 frames) a lap. States 2 and 12 knock it off
the path and state 8 flies it back to where the lap has got to. Kind-0 records
would also spawn a type 0x192 rider (`FUN_002D8808`), which is not ported; the
runtime prints a notice if a scene has one, and s14_e004 does not.

**It never attacks, and neither does it on hardware.** Record `0x21`'s AI
script at `0x5EE4` is a closed loop -- gate on action 6, random beat, order 6,
eight 60-tick waits -- closed by an opcode 3 with mode `0xFF`, which
`FUN_00242000` takes unconditionally. The grab (state 2) is ported and
unreachable here.

Checked against hardware, aligned at hw = port + 2750 after the intermission:
the wake-up (state 0, position and facing), the flight back to the path, the
position on the lap at tick 13952, a flinch (state 12 then 6 then 14, 42
frames) and the death fade (state 13, slot freed 124 frames later). The hit
and death were driven by writing `+0xBE` on both sides, not by real spells.
`savestates/s14_e004_monster_riding.p2s` has it mid-lap.

### The Square guard softlocked, and the lizardmen never guarded

**A blocked hit is +0xBE negated, and something has to spend it.**
`FUN_00216140` negates the damage when the hit lands inside `+0x124`, and
`FUN_0024A360` drops every pending request while `+0xBE` is non-zero -- the
Square release included. State 117 (`FUN_0024CBA0:39-130`) spends it: cue
0xE8, a 0x1C7 spark 0.2 in front along the bearing to the attacker, and half
the blow let through (HP bar only; nothing at `0x0024CE7C` stores `+0x12A`)
while the guard entity's `+0xAA` bit 0x100 is up. The port stopped short of
that, but the softlock was one step earlier: `FUN_00249610`'s death test read
`+0xBE` unsigned, so a blocked 5 was 65531 and the lead took the "dying"
return every frame. The original skips that test for the lead when `+0xBE` is
negative. Both guard arcs, `0x3527A8` (state 115) and `0x3527AC` (state 117),
are pi in the ELF; the port had 60 degrees for 117 and nothing for 115.

`--damage` stamps `+0xBE` directly and skips the arc, so it cannot test a
block. A real swing can: with no input s14_e004 is hit at 1751, 2688 and 3224,
and `--hold-square 1700-1900 --battle-report` blocks the first.

**Type 0x8B guards against what it sees coming.** `FUN_0028C298` (state 1),
once it faces where it wants, asks `FUN_0023ECE8` what the party is doing to
it -- 1 for a current action 0x84 (sword), 2 for 0x8B (a kind-12 release), 3
for 0x8D (kind -2) -- and on any of them rolls `random % 1000` (unsigned) under
ten times record `+0x25`. A hit is state 9 on animation 0x0F for record
`+0x24 * 4` frames, and the wrapper stands up the type 0x11E marker
(`FUN_002F1420` / `FUN_002F1558`) on the way in. Action 9 is the same roll for
codes 1 and 2 only. `FUN_002493B8` negates a target below -3; the port's copy
did not.

### A weak block, the lead's death in battle, and `--scenario`

**The guard's first ~50 frames are a weak block.** The `+0xAA` that
`FUN_0024CBA0` tests is the *shield's* (type 0x1C7, `DAT_0031DABC[member]`),
and `+0xAA` is the trailing word of the current animation keyframe
(`FUN_00225C90`). The shield's hold animation 0 carries bit 0x100 on keyframes
1-4, so a hit inside roughly 50 frames of pressing Square leaks half its
damage. The spark plays animation 4, and the rumble is 9 instead of 8. A leak
below `+0x12A` only arms the HP bar (`0x0024CE7C`). A lethal one goes into
`+0xBE`, with action 0x83 and state 0x66, and lands as a real hit. In s14_e004,
holding Square from 1730 or 1700 is a weak block of the 1751 swing; from 1600
it is a clean one.

**The lead's lethal hit ends the battle.** `FUN_00249610:84-101`: when
`+0x12A - +0xBE` drops below 1, any member counts `FUN_0023F620(0xB, 1)`. For
the lead with a non-negative `+0xBE`:
- `+0x04` bit 3 is cleared and `+0x44` gets `DAT_00352740` (0.00145) if it was
  0.
- `FUN_002445C8(lead, 0, 1)` frees its walk.
- `FUN_00243D18` tears the battle down then and there.

The field controller then spends the hit and plays the death (0x18, then
0x1B). The port did only the first step and returned every frame, so **any**
lethal hit froze the lead in its battle state with the damage never taken. With
no input, s14_e001, s14_e003 and s14_e012 all reach one inside 6000 frames
(4587, 4543 and 5511). They now end in the game over where they used to sit
frozen. Their 3000-frame regressions are unchanged.

`FUN_00243D18` came up to the disassembly at the same time:
- It no longer writes `+0x04`. The record keeps it at `+0x08`, but the restore
  never reads it.
- It zeroes `+0x96`, `+0x134` and the 42-byte bone mode table at `+0x168`.
- When the lead is dying, it skips the other members whose own hit is lethal.
- It zeroes the summon smear (`DAT_0034387C/E`, `DAT_00355661`) and
  `DAT_00354E96`.
- It releases the camera (`FUN_00217E18(0)`).

`uGpffffaf58` (0x00354EC8) and the voice stop are still unported. s14_e031's
ordinary teardown is unchanged over 6000 frames.

**`--scenario <file>`** is a timed battle test: held buttons, a starting HP and
expectations checked as the run goes. It prints PASS/FAIL per line and exits 1
on a failure. Syntax and rules are in `src/harness/battle_scenario.h`; the
short version is that it may set the pad and a starting condition, never an
outcome. `port/scenarios/` has the two weak-block cases above, at 50 HP and at
1 HP.
