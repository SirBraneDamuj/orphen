# Player model, close-up rig and bandana

## The close-up rig, and the draw walk's deferral queue

The doorway scene swaps Orphen for a close-up model with an animating head, and
in the port the head never appeared and his bandana stayed behind in the corridor.
Three separate things, and the first one is the whole scene:

**Type `0x28` is not a character, it is a mount.** Pool slot 64 becomes one
during the scene, and `FUN_002d2f40` — its entire behaviour — builds a
three-entity rig the first time it ticks:

```c
b = FUN_00265e28(0x26);  b->+0x192 = this;  b->+0x194 =  role1(this)
c = FUN_00265e28(0x19);  c->+0x192 = b;     c->+0x194 = -role2(b)
a = FUN_00265e28(0x27);  a->+0x192 = b;     a->+0x194 =  role1(b)
this->+0x198 = a;  this->+0x19C = b;  this->+0x1A0 = c;  this->+0x94 = 1;
```

So the mount `0x28` (`grp_0021`, 39 bones) is the **torso and arms**, `0x26`
(`grp_001f`, 30 bones) is the **head**, `0x27` (`grp_0020`) is the **hair**, and
`0x19` is a bandana of its own — the same type the field player wears. All three
settled by capture rather than by model size: hiding the `0x27` slot removes the
hair and leaves a headbanded bald head, and hiding `0x26` *and* `0x27` leaves a
headless torso with a bare neck stump, so the mount carries no face of its own
and nothing needs hiding on it the way opcode `0x140` hides the field body's
head bones.

The negated bone index on the cloth is deliberate: it selects `FUN_0020cdc0`'s
middle, position-only branch instead of the rigid one, which is what lets the
rope hang rather than being welded to the bone's orientation. On the live rig the
three resolve to bone 30 of the mount, bone 1 of the body and bone -2 of the
body.

Opcode **`0x13F`** (`FUN_002604a8`) is the script's handle on it: it clears
`+0x94` to force a rebuild, calls `FUN_002d2f40`, and publishes the two pool
indices into work slots — `work[e1]` from `+0x19C` (the bust) and `work[e2]` from
`+0x198` (the hair), which is not the order it reads its operands in. It is now
modelled rather than operands-only, and it runs exactly **once**: the 173 hits it
showed while halting were the body being re-entered, not the opcode firing.

**And `FUN_0020c5a8` walks a deferral queue, not the pool in slot order.** Its
first pass queues every live slot whose `+0x02` bit `0x200` is clear and marks it
0; everything else is `0xFF`. The second pass walks that queue *while it grows*:

| parent | action |
|---|---|
| `< 0` | pose and draw, mark 1 |
| `status[parent] == 0` | push this slot on the back and move on |
| `status[parent] == 1` | pose and draw, mark 1 |
| `status[parent] == 0xFF` | neither — the slot is dropped this frame |

The port had a plain ascending walk, and the comment on the rigid-attach branch
said as much: slot order happens to be right for the head-on-neck attachments in
the opening, so it held up. `FUN_002d2f40` breaks it, because it allocates the
hair **before** the bust — so the hair takes the lower slot and was posed against
a palette its parent had not built yet. `FUN_0020dc88`'s no-palette fallback is
the parent's own `+0x20`, and for a bone-local attachment that is `(0, 0, 0)`:
the hair was at the world origin.

With the queue, the chain resolves in dependency order and the rig lands where it
belongs — mount at `(5.50, -1.20, -1.50)`, head on its bone at
`(5.518, -1.177, -0.692)`, hair on the head's at `(5.518, -1.195, -0.677)`, cloth
at `(5.515, -1.182, -0.593)`. The shot itself is Orphen leaning through the
cargo hatch at the end of the corridor with Dortin and Volcan beyond it, and the
mount stands 0.91 m to the field model's ~1.0 m: "close-up" here means a
higher-detail model with a real face, not a smaller one.

## The field model is hidden with `+0x08` bit 0, and its bandana has to go too

The bandana that stayed behind in the doorway was not the rig's. It was slot 4,
the *field* player's, and the port was drawing it for the whole cutscene after
the field model itself had gone.

The script hides Orphen at the same instant it builds the rig. Tracing the two
`0x79` register ORs that sit between opcode `0x6D` and the `0x52` that spawns the
mount:

```
[scr] f=14795 @0xa3f5 op=0x79    reg 0x3 slot 0: 0x3024 -> 0x3025
[scr] f=14795 @0xa405 op=0x79    reg 0x4 slot 0: 0x100  -> 0x101
```

Register `0x4` is entity `+0x08`, and bit 0 is `FUN_0020c5a8`'s hidden flag — not
`+0x02` bit `0x200`, which the whole scene writes exactly once and never to slot
0. (`--scr-trace-range` now prints the slot, register and before/after value under
every `0x76`..`0x7C`, which is what made this a one-line answer instead of a
guess.)

**`FUN_0020c5a8` gives slot 0 no special treatment.** It is queue entry 0 like
anything else, and its status byte is what decides whether its children draw. The
port posed the lead outside the queue — slot 4 reads slot 0's palette to find the
neck — and then pinned `drawStatus[0]` to "posed" unconditionally, so the
bandana kept drawing against a body that was no longer there. Worse, the mount
walks forward at frame 15149 and the field model does not, so the bandana ends up
hanging alone in the doorway.

Slot 0 now writes its real status, and the three branches are not the same:

| lead state | status | why |
|---|---|---|
| `+0x02 & 0x200` | `0xFF` | children dropped with it |
| `+0x08 & 1` | stays **queued** | the original's `& 1` branch never writes the byte, so children defer against it until the queue's byte counter wraps — not drawn, by a different route |
| drawn | posed | children draw |

Verified by instrumenting the publish: slot 4 reaches the view list on frames
0..14794 and never again, which is exactly the frame the OR lands on. A pre-fix
and post-fix capture of frame 15030 differ only in a red bandana strip beside
Orphen's head.

## `0x6D` hides the bandana too, and a save state proved the head was fine

A PCSX2 save state taken during the close-up settled both halves of this, and
one of them the wrong way round: **the head was never deformed.**

`--arm-stream d780:13400` reaches the same shot at frame 15810 — same camera
(`cPOS 6161,-1226,-996` against the state's `-1228`), same line, same pose. The
head model's palette is not merely close, it is exact. All 30 bones of `grp_001f`
at animation 23 / column 2, port against `0x00357E00 + 64 * 0xA80`:

```
bone  0  port( 5.500 -0.477 -0.637)  real( 5.500 -0.477 -0.637)
bone  9  port( 5.495 -0.550 -0.576)  real( 5.495 -0.550 -0.576)
bone 28  port( 5.522 -0.539 -0.549)  real( 5.522 -0.539 -0.549)
...   worst bone 9, 0.0005 m — the print's own precision
```

and the head's bone 0 equals the mount's bone 30 in *both*, which is
`FUN_0020cdc0`'s rigid branch confirmed against hardware rather than inferred.
The keyframe tables match too: animation 23 is ten entries cycling columns
2,1,3,1,4,1,5,1,6,1, and 36 is a single column-26 key, exactly as the port reads
them.

What I had been looking at was the mount's collar and hood surrounding a head
that is small and tucked into it at that angle. Masking the head's own pixels
(`--hide-slots 71,72,73` differenced against `--hide-slots 71,73`) shows a clean,
compact face. Two lessons: **a low-poly head read through an occluder is not
evidence**, and once the palette is measurably right the search should move to
what is drawn in front of it, not further into the pose pipeline.

**The real bug was the bandana, and `0x6D` is what hides it.** `DAT_0058C610`,
`DAT_0058C614` and `DAT_0058C618` are not globals of their own — `0x0058BEB0 +
4 * 0x1D8` is `0x0058C610`, so they are pool slot 4's `+0x00`, `+0x04` and
`+0x08`. With an operand below -2 `FUN_0025fd10` sets `+0x04 |= 0x4000` and
`+0x08 |= 1` on the bandana whenever the lead is the player and slot 4 is
occupied, and clears both on the `== 1` release (gated on the lead actually
sitting in state 10). The port drove only the lead's state and skipped all of it.

The save state reads slot 4 `+0x04 = 0x4019`, `+0x08 = 0x0031`; the port now
reads `0x4019` / `0x31` at the same point in the scene.

Why it looked like a face bug: the hidden lead's position decides where the
orphaned bandana hangs, and that differs between a forced `--arm-stream` trigger
and real play. The save state has the lead at `(5.140, -0.838, -1.500)` — half a
metre from the close-up head, well inside a shot whose camera sits about 1.2 m
away. Forcing the stream leaves him at `(5.500, -1.199, -1.500)`, behind the
camera, so the same build renders the same frame clean. **A cutscene reproduced
by arming its stream does not put the player where play does**, and anything
parented to the player will lie about it.

## The bandana

Orphen's bandana is not part of his mesh. It is a **separate entity** -- type
`0x19`, model `grp_001E` -- in reserved pool slot 4, attached to the player's
neck bone and simulated as two nine-link ropes. Three original functions, all
ported: `FUN_00251e40` creates it (and only when the lead player is type 1),
`FUN_00213720` is the simulation, and `FUN_00213640` releases its bones.
`analyzed/actor_behaviors/type_0x19_player_bandana.c` is the full reading.

Nothing calls `FUN_00213720` by name. It is `PTR_FUN_0031c6c0[0x19 - 1]`, which
is what identifies it as a behavior rather than a helper -- every neighbouring
entry in that table is the generic no-op or the party-member shell.

Three things the port did not have before:

- **`FUN_0020dd78`, the bone role lookup.** The high byte of a PSC3 submesh's
  `+0x0A` carries a semantic role in its low nibble, and native code finds bones
  by scanning for one. Role 7 is the neck; role 4 is the hand a weapon goes in.
  The docs previously said the game never reads `+0x0A` at all.
- **`FUN_0020cdc0`'s attached branch.** An entity with `+0x192 >= 0` and a
  *negative* `+0x194` rides that bone's position and keeps its own facing, and
  its `+0x20..+0x28` is a bone-local offset rather than a world position. That
  last part is why `SceneObjectView` now carries `worldOrigin` separately: the
  depth sort and the debug box both need a world point, and the entity's own
  position fields are not one.
- **Only `-1` skips a draw pass, and an untextured pass uses a different colour
  scale.** The port was dropping every negative subdraw index; `FUN_00212058:106`
  tests for exactly -1, and `FUN_002129b8` masks bit 15 off anything else and
  draws the pass untextured with the remainder as a colour index. All 20 of
  `grp_001E`'s primitives are that case, which is why the bandana was invisible
  even once it was being simulated -- and 26 passes on `grp_0001` and 43 on
  `grp_0009` were being dropped too. Such a pass never sets TME, so its colour
  goes straight to the framebuffer over 0..255 rather than through the GS's
  `(Ct * Cv) >> 7` where `0x80` means x1.0. `grp_001E`'s one colour entry is
  `(191, 0, 0)`: nonsense as a modulator (x1.49, saturating to pure red) and
  exactly right as a colour, `0xBF`. Divided by 128 the bandana rendered about
  twice as bright as the emulator and lost its shading entirely.

**`FUN_00305218` is `sinf` and `FUN_00305130` is `cosf`**, not the reverse --
0x00305218's small-|x| path calls `__kernel_sin(x, 0, 0)` and its `n&3` switch is
fdlibm's sine. Older files under `analyzed/` had them backwards; the last one,
`battle_logo_state_manager.c`, was corrected on 2026-10-23. It matters:
`FUN_00213720` stores sine at scratch `+0x15` and cosine at `+0x16`, and swapping
them rotates the tail frame a quarter turn, which puts `DAT_003151a0`'s body
clamp -- up to 0.067, six times the 0.011 tail spread -- on the sideways axis. The
tails then drift into the neck instead of trailing.

The resting state is checkable against `s01_e24.bin`, which has slot 4 live:
the anchor sits at (-3.31224, -12.75, 0.93703) with the player at
(-3.25, -12.75, 0), each of the nine links is exactly 0.025 long, and the two
chains hold gravity 0.018 and 0.010 -- both from the `{0.006, 0.010, 0.014,
0.018}` set the sim re-rolls into every 64th frame. Solving the dump's bone
matrices back for each override translation gives `(±0.011, DAT_003151a0[i],
-0.025 * i)` -- the spread in the first slot, the body clamp in the second, which
is the pair the sine/cosine identity gets wrong. `--actor-report` prints the
port's own anchor, tip, span and tip-bone translation beside slot 4.

**Actor behavior dispatches twice.** `FUN_0025ab68` and `FUN_002cd0a0` are shells
that index a per-type state table with `+0x60`; those tables are read out of the
executable too. `--actor-report` shows the second dispatch, because a type can be
dispatched and still do nothing when the *state* it is in has no port:

```
type=0x3  state=0 -> 0x25abb8 ticks=120  implemented   (jr ra; nop -- a real no-op)
type=0x62 state=0 -> 0x2cd210 ticks=1    implemented   (init; hands to state 3)
```

As of 2026-10-23 `s01_e024` over 600 frames reports **zero** unimplemented
behaviours and zero unimplemented states. Non-player physics is ported too (see
[Physics](physics.md)); `integrateNonPlayerMovement`, which once stood in for
it, is now the non-player copy of `FUN_002262c0`.

## The second bandana, and why destroying an entity is not releasing a slot

Orphen wore two bandanas through `s14_e031`'s camera swoop -- one trailing off
the close-up bust correctly, a second forking away from it. The extra one was a
real entity, pool slot 14, type `0x19`, and it had been alive since the scene's
init entry.

`s14_e031` builds the close-up rig **twice**. The init entry runs opcode `0x13F`,
which clears `+0x94` and calls `FUN_002D2F40`; that lands a mount at slot 11 with
hair at 12, bust at 13 and cloth at 14. The scene then tears that first rig down
again with three opcode `0x5C` calls -- slot 13 (the `0x26` bust), slot 12 (the
`0x27` hair), slot 11 (the `0x28` mount) -- and builds a fresh one later at
10/11/12/13.

**It never names the cloth.** It does not have to: `FUN_0025F238`, opcode `0x5C`'s
whole body, is a call to `FUN_00265EC0`, and `FUN_00265EC0` calls `FUN_00265F70`,
which destroys every entity whose `+0x192` names this one **through `FUN_00265EC0`
again**. The cascade is the whole subtree. Destroying the bust takes the cloth
hanging off it, because the cloth is the bust's child.

The port had two separate divergences that added up to the same entity surviving:

- Opcode `0x5C` called `EntityPool::releaseSlot`, which is the map-load blank --
  no light slot given back, no cascade at all.
- `FUN_00265ec0_destroy_entity` did cascade, but only one level: it released each
  direct child with a bare `releaseSlot` rather than recursing. A grandchild
  always survived.

So the first rig's cloth outlived its bust, its `+0x192` kept naming slot 13, and
slot 13 was recycled into the *second* rig's cloth -- which is why the stray
bandana tracked the real one instead of hanging somewhere random.

`FUN_00265EC0` is now one function taking a pool and the light table, with the
`ActorEnvironment` form as a thin wrapper, and `0x5C` and `0x142`
(`FUN_002606D0`, which calls `FUN_00265F70` outright) both go through it. Two
details of it that are easy to miss:

- The status byte is cleared **before** the rescan -- the original zeroes
  `DAT_005A96B0[slot]` at the top of `FUN_00265EC0`, ahead of everything else.
  That is what stops a parent cycle looping, and the port's release-then-scan
  order stands in for it.
- A slot whose type reads `< 1` takes the short branch: `+0x96`, the type and
  `+0x95` are cleared and nothing else happens -- no light freed, no cascade.

Verified: the rig at frame 1400 of arm 853 is four entities (mount 10, hair 11,
bust 12, cloth 13) plus the field bandana in slot 4, where it was five; the swoop
draws one tail; all eleven arms still reach the same destination scenes; and the
`--frames 1200` guard is byte-identical on `s01_e024`, `s14_e001` and `s14_e012`.

