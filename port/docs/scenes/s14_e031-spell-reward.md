# s14_e031: the spell-reward cutscene

## s14_e031, the spell-reward cutscene

One scene plays the "you learned a spell" cutscene for all eleven learnable
spells. Event flags **850..860** (BFLG 50..60) pick which; the ladder is in the
*init* entry, so `--set-event-flag <id>:0` is the only form that works -- a flag
raised on frame 1 is already too late.

```
port\build\msvc-Release\orphen_port.exe --disc-root . --scene s14_e031 \
  --no-audio --frames 4000 --set-event-flag 856:0 --battle-report --actor-report
```

| flag | spell | id | hand effect | hands off to |
|---|---|---|---|---|
| 850 | Feathers of the Hurricane | 3 | `0x13B` | s14_e026 |
| 851 | Smoke of Pain | 4 | `0x13C` | s14_e045 |
| 852 | Coldness of Destruction | 6 | `0x194` | s14_e042 |
| 853 | Bolt of Thunder | 2 | `0x13A` | s14_e002 |
| 854 | Falcon of Death | 8 | `0x175` | s14_e028 |
| 855 | Hammer of Evil | 9 | `0x177` | s14_e029 |
| 856 | Pinnacle of the Sun | 10 | `0x179` | s14_e024 |
| 857 | Hail of Heavens | 11 | `0x17C` | s14_e013 |
| 858 | Shield of Immunity | 12 | `0x143` | s14_e001 |
| 859 | Shield of Inferno | 13 | `0x127` | s14_e027 |
| 860 | Armor of Purity | 14 | `0x144` | s14_e025 |

The spell ids are the master record table at `0x00324FC8`, stride `0x12`, and
the demo entity the scene spawns into pool slot 10 is `0x1F1 + spellId` -- which
is why `0x1F6` and `0x1F8` are skipped. Those two are Hand of Pyro and Bite of
Lightning, the spells Orphen already has.

### The three things that made it run

**The scene has a target, and it is a type `0x8B` training dummy.** It stands at
`(3.5, 0, 0)`; the script gives it `+0x95 = 50` through object register `0x11`
and its own state 0 fills `+0x12A = 62` and binds an actor record. Both are
needed before `FUN_00249610` will keep a target, and all three save states show
`target = 15` for the whole cast. Without it the control block sat on the 1 that
state 120 parks it on, the spell landed two units in front of the caster instead
of on the dummy, and `level == 5 && target > 1` -- the summon -- could not fire.

**The beat gate is scene module 18, mode 4.** `FUN_0026C980` is two lines:

```c
if (FUN_00266368(0x35D) && sGpffffaf5c == 0) FUN_002663A0(0x35E);
```

The script raises flag 861 when the cast goes off and then parks scheduler
channel 1 on the record at blob `0x1B78`, gated on flag **862** -- which nothing
in the script ever sets. `sGpffffaf5c` is `DAT_00354ECC`, held at 1 by a level-5
summon for its whole run, so the beat waits exactly as long as the summon is on
screen. Before this hook the four kind -2 arms hung there forever.

**The barrier's animation order is 1 -> 0 -> 2.** `LAB_002DE0B8` (types `0x127`,
`0x143`, `0x144`; `0x2DE0A8` and `0x2DE0B0` are two-instruction thunks into it)
is a Ghidra LAB with no `src/` file, recovered from `SLUS_200.11` at
`0x002DE0B8..0x002DE36C`. 1 is the rise, 0 the hold, 2 the drop, and state 115
ends the cast the moment it reads 2 -- so spawning the barrier at animation 0
released it on the first frame, which is what "the shield demos play with nothing
on screen" actually was. State 115 also has to stamp `+0x94` (the caster's pool
slot, which the barrier rides every frame), `+0x12C` and `+0x198`; none of the
four were being written.

### GRP.BIN, and why no spell effect draws

`--model-report` reports `NO MODEL: missing PSC3 magic` for every hand effect,
projectile and barrier in this scene, and it is not a behaviour problem --
Bite of Lightning is just as affected here.

An entity model does not come from a scene bundle in the original.
`FUN_00222498` loads it straight out of a flat archive by mesh id, and the model
record's `+0x04` **bit 6** picks which: set means `MAP.BIN`, clear means
`GRP.BIN`. Every spell effect record carries `flags04 = 0x2D`, bit 6 clear.

`GRP.BIN` is not in the disc root. `EntityModelStore::loadModel` now takes the
record's flags and falls back bundle -> (`GRP.BIN` | `MAP.BIN`) -> `ITM.BIN`, and
the texture loader falls back to `TEX.BIN` -- which *is* present, and which on its
own cleared every "no pixels" slot. Drop `GRP.BIN` in beside the other archives
and the effects become visible with no further code change.

Do not shortcut this with a blanket `MAP.BIN` fallback: `MAP.BIN` id `0xBD` is a
perfectly valid but completely unrelated PSC3, so the guess returns a plausible
wrong mesh rather than a miss.

### The four kind-12 spells

Bolt of Thunder, Feathers of the Hurricane, Smoke of Pain and Coldness of
Destruction are Hand of Pyro's shape four more times, and the only family where
the **projectile** is genuinely per-element. The hands come in two shapes --
Bolt and Feathers take the bone index negative and ride the caster's facing,
Smoke and Cold park a fixed pose and tilt it by class -- and the four launches
are one function. The steering is what makes each spell look like itself:

- **Bolt of Thunder** fans by a *time-scaled* yaw, so the bolts splay wider the
  longer they fly, and each homes on a `+0x1B4` that tracks the target
  separately from the yaw it is drawn at. It is also the one that moves its
  chain and charge to `+0x1C8`/`+0x1C9` so it can keep `+0x1C6` as the aimed
  shot's costed flight time.
- **Feathers** takes one fixed yaw step, flies blind until it has turned, then
  homes -- and its volley **fans across up to five different enemies** rather
  than stacking on one. The picker is pool slot 2 first, then every live entity
  whose `+0x96` bit 0 is up.
- **Smoke of Pain** holds its yaw as a *bias* for the whole flight rather than
  folding it into the facing, so the shots corkscrew around the aim line; each
  throws exactly one successor and then halves its own speed.
- **Coldness** is Feathers' fan plus a hop as the spread ends, and it throws the
  fan when it *lands* rather than on a timer.

Three of the four compute a vertical trim as `spread * tau / 360 / N * elapsed`
with the spread multiplied by a literal zero -- the compiler kept the multiply
-- so only Bolt actually climbs or dips.

The bursts cost nothing: types `0x170`, `0x171`, `0x172` and `0x196` are
`j 0x2DB230`, the same two instructions Hand of Pyro's `0x173` runs, so they are
four extra labels on a case that was already there.

Not ported, and called out where they would go: `FUN_0023C220`, the per-victim
offer that swings the battle camera onto whatever was hit, and `FUN_0023BBD8`'s
rumble. `FUN_002D6BD0`, the volley's own hit cue, is here.

