# Bite of Lightning (Circle) — the full path, and what the port is missing

Working notes for porting the kind &lt; 0 elemental arm: states **113** (hold) and
**114** (release), plus the three effect entities they drive. Everything below is
read out of `src/` and `SLUS_200.11`; nothing is inferred from the port.

The summon (max charge with a live target) is **out of scope here** — its entry
point is identified at the bottom so it can be picked up next.

> **Status: everything below except section 8 is ported** (2026-09-03).
> `port/README.md` carries the write-up of what landed and how to exercise it;
> this file is kept as the read of `src/` it was built from. Section 7's gap list
> is retained as a record of what the work was. Two corrections have been folded
> in: `FUN_00305130` is `cosf` and `FUN_00305218` is `sinf` (section 2 had them
> the wrong way round), and the port needed one fix that is not in this document
> at all — `uGpffffb052` and `DAT_00354fc2` are the same halfword at
> `0x00354FC2`, and modelling them apart made `FUN_00243f80` rebuild the party a
> second time and leak a second `0x1E3`, which ate every `FUN_002f1380` request.

---

## 1. The chain, end to end

```
FUN_002462c8   Circle press -> pending 0x8C (hold) / 0x8D (release)
FUN_0024a360   pending + 0x3FE5 -> state 113 / 114, bit 0x4000 = "entered"
FUN_00249610   dispatch through class-1 table 0x0031DD60
   113  FUN_0024c538   hold: spawn the hand effect, capture the target, charge
   114  FUN_0024c910   release: level = charge/0x780, hand off to the effect
FUN_002deae8   type 0x174, the hand effect. On state60 == 1 it fires:
FUN_002de650   the launch: damage box + burst + per-victim sparks
```

Three entities are involved and they are **not** the same thing:

| what | type | table | handler | ported? |
|---|---|---|---|---|
| the ring at the **caster's feet** | `0x18F` | `DAT_0031da8c[member]` | `FUN_002d9c88` | **yes**, fully |
| the effect in the caster's **hand** | `0x174` | `DAT_0031daac[member]` | `FUN_002deae8` | **no** — `--actor-report` says UNIMPLEMENTED |
| the growing marker at the **target** | `0x1E3` | `DAT_0031dad0` (one, shared) | `FUN_002f13d0` | handler yes, **setter `FUN_002f1380` no** |

`FUN_00242df0` pre-spawns the first two per member at battle start, hidden by
`+0x08` bit 0; `FUN_002432d8` spawns the third once for the whole battle.

---

## 2. Where the circle goes when there is no target — answered

`FUN_002493f0` (`src/FUN_002493f0.c`) is the whole rule, and there is no
randomness in it:

```c
long FUN_002493f0(entity, Vec3 *out)
{
  long target = controlBlock(entity->byte95 - 1)->target2c;   // DAT_0031d7a0 + byte95*0x3C
  if (target < 2) {                       // no target
    out->x = 2 * cosf(entity->facing5c) + entity->posX;   // FUN_00305130 is cosf
    out->y = 2 * sinf(entity->facing5c) + entity->posZ;   // FUN_00305218 is sinf
    out->z = entity->posY;
  } else {                                // a target
    *out = partyRecord(byte95 - 1) + 0x28;   // DAT_0031d38c + byte95*100
  }
  return target;
}
```

- **With a target**: the position is `partyRecord + 0x28`, which state 113 has
  been tracking onto the target every frame while the aim marker is still `-1`.
  Port constant `record::kTargetPos28` already exists (added for the sword).
- **With no target**: **two world units directly in front of the caster**, along
  their facing. It "varies by battlefield" because it varies with which way
  Orphen happens to be facing when the spell goes off — not because it is
  undefined.

Note the threshold is `target < 2`, whereas `FUN_00249610`'s face-the-target
block uses `< 3`. They are genuinely different numbers; do not unify them.

`FUN_002493f0` has no port equivalent yet. It is needed by both `FUN_002deae8`
and `FUN_002de650`.

---

## 3. State 113 — `FUN_0024c538`

The port's `stateChargeSpellB113` covers roughly the first third of this. Line
by line against `src/FUN_0024c538.c`:

```
if (DAT_0031daac[member] == 0) { state60 = 0x4078; +0x06 |= 0x10; action = 6; return 0; }
slot = DAT_0031da65[byte95];                     // the selected loadout slot

if (state60 & 0x4000) {                          // entry
    state60 &= 0xBFFF;
    type = DAT_0031da3a[member*3 + slot];
    FUN_00265ec0(DAT_0031daac[member]);          // destroy the old effect
    DAT_0031daac[member] = FUN_00245a00(type);   // spawn the new one
    FUN_00248e98(entity, 0x3A);                  // the cast pose
    if (FUN_00249348(entity)) DAT_0031da60[slot] = 1;      // >>> MISSING: ask for the voice bank
    effect->+0x192 = casterSlot;
    effect->+0x194 = 0x12 (class 1) / 0x0E (class 4);
    FUN_00249108(entity);                        // charge = 0
    partyRecord+0x45 = 0xFF;                     // aim marker: not yet
    FUN_00248ee0(effect, 1);  effect->+0x08 |= 0x10;
}

if (+0xAA & 0x200)   partyRecord+0x45 = (short)entity->+0xA8 / 2;

if (partyRecord+0x45 == -1)                      // >>> MISSING (whole branch)
    partyRecord+0x28..0x30 = pool[controlBlock->target2c].position;
else
    FUN_00249128(entity);                        // accumulate charge

if (FUN_00249348(entity)) {                      // >>> MISSING: the voice walk
    if (DAT_0031da60[slot]==1 && FUN_00206ae0(DAT_0031da54[slot], slot, 0)==0) DAT_0031da60[slot]=2;
    if (DAT_0031da60[slot]==2 && FUN_00206c28()==1)                            DAT_0031da60[slot]=3;
}

if (partyRecord+0x45 != -1) {                    // >>> MISSING (whole block)
    if (FUN_002d9b78(entity, 1) == 0) goto tail;               // the ring gate
    if (FUN_00249348(entity)) {
        if (DAT_0031da60[slot] != 3)      goto tail;
        if (FUN_00206a90() != 0)          goto tail;
        if (FUN_00206f08(slot, 0) != 0)   goto tail;
        DAT_0031da60[slot] = 100;                              // the chant is speaking
    }
}
tail:
if ((+0xAA & 0x400) && (+0x06 & 4))              // >>> MISSING: the pose pin
    entity->+0xA8 = partyRecord+0x45 << 1;
return 0;
```

Two things worth stating plainly:

- **`partyRecord+0x45` is a frame number, not a flag.** It is captured off the
  animation cursor `+0xA8` when the cast animation raises `+0xAA` bit `0x200`,
  and the last line writes it *back* into `+0xA8` every frame afterwards. That
  is what freezes the character in the cast pose for as long as Circle is held.
  Nothing accumulates charge before the pose is reached.
- **`FUN_002d9b78` is a gate, not just a resize.** It returns 0 while the ring
  is not at animation 0, and 113 uses that to hold the voice back. The port
  already has `FUN_002d9b78_drive_cast_ring`, but 113 never calls it — so today
  the Circle charge builds with no ring at all.

Compare state 111 (`FUN_0024c058`, Hand of Pyro), which the port does have:
111 captures the target position **once, on entry**, and gates on `+0xAA` bit
`0x800` rather than 113's `0x200`/`0x400` pair. 113 tracks the target *every
frame until the pose lands*. Do not copy 111's shape onto 113.

---

## 4. State 114 — `FUN_0024c910`

The port currently has:

```cpp
std::uint16_t stateReleaseSpellB114(const StateContext &context, std::uint16_t charge)
{
  return stateReleaseSpell109(context, charge);
}
```

That is wrong. **114 does not tail-call `FUN_0024bae0`** — 112 does, 114 does
not. The real handler:

```
if (DAT_0031daac[member] == 0) { state60 = 0x4078; action = 6; return 0; }

if (state60 & 0x4000) { state60 &= 0xBFFF; FUN_00248e98(entity, 0x3A); FUN_00215e48(entity); }

if (FUN_00249348(entity)) {                       // the release voice line
    if (DAT_0031da60[slot] == 100) {
        if (FUN_00206a90() == 0) { FUN_00206f08(slot, 1); DAT_0031da60[slot] = 101; }
        else                     {                        DAT_0031da60[slot] = 102; }
    }
}

if ((+0xAA & 0x100) && (+0x06 & 4)) {             // the release marker frame
    level = FUN_00249270(entity, 0x780);          // min(charge, 0x2580) / 0x780  ->  0..5
    if (level >= 5) FUN_0023f620(2, byte95);      // a statistics counter
    effect->+0x12C = entity->+0x12C + partyRecord[0x14 + slot];
    effect->+0x198 = partyRecord + 0x18 + slot*4;
    effect->+0x60  = 1;                           // <<< this is the trigger
    effect->+0x94  = level;
    FUN_002d9b78(entity, 0);                      // close the ring
}

if ((+0x06 & 1) == 0) return 1;                   // animation still running: input locked
// finished
if (effect->+0xA0 != 2) { FUN_00248ee0(effect, 2); effect->+0x08 |= 0x10; }
action = 6;
entity->+0xA0 = (FUN_00216868() & 1) ? 0x13 : 0x2F;   // one of two recovery poses
state60 = 0x78;
return 0;
```

`FUN_00215e48` on entry clears the effect's hit-set (`+0xEC`..`+0x10C`) and
`+0x06` bit `0x40` — without it a second cast cannot hit anything it already hit.

`FUN_00249270(entity, 0x780)`: the charge caps at `0x2580` for this read (not
the `0x2D00` the accumulator caps at), so **level runs 0..5** and 5 is only
reachable at a genuinely full charge.

---

## 5. Type `0x174` — `FUN_002deae8`, the effect in the hand

Not ported. Structurally the twin of `FUN_002da8a0` (type `0x13D`, Hand of
Pyro), which **is** ported at `actor_frame_update.cpp:~2840` — copy that shape.

```c
FUN_0030bfac(bonePos, 0, 12);  FUN_0030bfac(boneOff, 0, 12);
effect->+0x133 = 0xF4;                    // depth bias -12
effect->+0x08 |= 0x4000;
effect->+0x5C  = 0;
caster = effect->+0x192;   if (caster < 0) caster = (int16)caster;

if (effect->anim != 2) {
    if (controlBlock(caster).pending0e == 0x0B) { effect->+0x08 = entry08 | 0x4001; anim = 2; }
    if ((uint8)(current0f + 0x74) > 1)          { effect->+0x08 |= 1;  anim = 2; }   // outside 0x8C..0x8D
}

if (anim == 0 or 1) {
    if (caster == 0)                                  // the player only
        FUN_002f1380(FUN_00249270(pool[0], 1)/1000.0f + 1.5f, 1.0f, FUN_002493f0(pool[0]));
    FUN_002da220(effect, pool[caster], 0x54, 0x8D, 0xFE, 1000);   // the blue hand light
}

if (anim == 1) { +0x08 &= ~1; +0x06 &= ~0x10; if (+0x06 & 1) anim = 0; }
else if (anim == 0) { +0x06 &= ~0x10; +0x08 &= ~1; }
else if (anim == 2 && (+0x06 & 1)) {
    +0x192 = caster; +0x06 |= 0x10; +0x08 |= 1; +0x14C = +0x150 = 1.0f; FUN_00266098(effect);
}

if (effect->+0x60 == 1) {                            // 114 wrote this
    +0x06 &= ~0x10;  +0x08 &= ~1;  anim = 2;  +0x60 = 0;
    FUN_0020dc88(pool[caster], effect->+0x194, bonePos, boneOff);
    target = FUN_002493f0(pool[caster], castPos);
    FUN_002de650(0, effect->+0x94, effect->+0x12C, target,
                 effect->+0x198, boneOff, caster, castPos);
}
```

**`FUN_002f1380` is the growing circle at the target.** It is the setter for the
shared `0x1E3` effect the port already spawns into `DAT_0031dad0` and whose
behaviour it already has (`FUN_002f13d0`, which hides itself unless
`DAT_00355588` is raised). Three lines:

```c
int e = DAT_0031dad0;  if (!e) return -1;
e->+0x20 = pos.x;  e->+0x24 = pos.y;  e->+0x28 = e->+0x4C = e->+0x50 = pos.z;
e->+0x14C = scale;  e->+0x150 = height;
DAT_00355588 |= 1;
```

Called every frame with `scale = charge/1000 + 1.5` — so it grows from **1.5 at
zero charge to about 11 at a full `0x2580`**, centred on the target (or two
units in front of the caster when there is none). That is the mechanic the user
described, and the port has every piece of it except this setter.

`FUN_002da220` is already ported as `FUN_002da220_spell_light`; call it with
`(0x54, 0x8D, 0xFE, 1000)` instead of pyro's `(0x80, 0x40, 0x00, 500)`.

---

## 6. `FUN_002de650` — the launch

```c
long FUN_002de650(elem=0, level, power, target, hitParams, boneOff, casterSlot, castPos)
{
  if (level == 5 && target > 1)                    // <<< THE SUMMON. Out of scope.
      return FUN_002deef0(elem, 5, power, target, hitParams, boneOff, casterSlot);

  if (casterSlot == 0) FUN_0023bbd8(0, 7, level, power);      // shake / rumble
  level = clamp(level == 0 ? 1 : level, 1, 5);

  // (a) the damage box -- invisible, 32 frames
  e = FUN_002d6c68(0x174);  e->typeId = 0x15C;     // spawned as 0x174, RETYPED to 0x15C
  e->+0x133 = level * -0x0C;
  e->+0x5C  = pool[casterSlot].facing5c;
  e->+0x02 |= 0x1000;   e->+0x04 = 0x19;
  e->position = castPos;   e->+0x4C = e->+0x50 = castPos.z;
  e->+0x12C = power + level*4;
  FUN_00267da0(e->+0x198, hitParams, 4);           // four attack bytes copied INLINE
  e->+0x1AC = target;  e->+0x1AE = casterSlot;  e->+0x1B3 = level;  e->+0x1B2 = 0;
  e->+0x62 = 0;  e->+0xA0 = 0;
  e->+0x14C = level * 2.0f;   e->+0x150 = 0.3f;    // DAT_00354914
  e->+0x1B0 = FUN_00248e48(0x20);                  // 32 frames
  FUN_00215e48(e);
  if (casterSlot == 0 && target > 2 && pool[target].+0x96 & 0x40) e->+0x96 |= 0x40;
  hit = FUN_00215ac8(e, box, e->+0x198);           // box = castPos +- (1.5L, 1.5L, 0.5L)
  FUN_00267d38(0xE1, e);                           // the thunder crack

  // (b) the visible burst -- one-shot, self-destructing
  b = FUN_002d6c68(0x174);  b->typeId = 0x178;
  ...same position / hitParams / target / caster / level...
  b->+0xA0 = (DAT_00354ECC != 0) ? 4 : 3;          // DAT_00354ECC is 0 in the ELF
  b->+0x1B0 = FUN_00248e48(0x20);

  // (c) one spark per victim
  for (v in DAT_003151C8[0..], while > 0, max 0x15 entries / 0x100 iterations)
      FUN_002de9e8(0, level, 0, v, e->+0x198, e->+0x19C, casterSlot, pool[v].position);
  return b;
}
```

**The retype trick matters.** Both entities are allocated with type `0x174`'s
descriptor and model, then have `+0x00` overwritten. The port resolves the
behaviour handler from `entity.typeId00` on every frame
(`actor_frame_update.cpp:3270`), so this works with no special handling — but a
"cache the handler at spawn" optimisation would silently break it.

Handlers for the two spawns:

- **`0x15C` — `FUN_002dee08`** (in `src/`, 36 lines). Steps `+0x1B0` and
  destroys itself at zero; otherwise spins `+0x5C` by `0.34906578` per tick and
  flips between `FUN_0020d9c8(e, 0)` and `FUN_0020d8c0(e, 0, {0.5235987}, 2)`
  on a coin flip each frame. It is the ground disc.
- **`0x178` — `FUN_002e4c00`**, not in `src/` and not defined in Ghidra;
  recovered from the ELF at `0x002E4C00`, and it is four instructions:

  ```c
  void FUN_002e4c00(entity) { if (entity->flags06 & 1) FUN_00265ec0(entity); }
  ```

  A pure one-shot: play the animation, destroy on the finished flag.

`FUN_002de9e8` (in `src/`) is `0x15C` again at scale 2.0/2.0 with `+0x1B3` =
level, planted on each victim's position.

---

## 7. Gap list, in implementation order — **all done**

Everything here is additive to `port/src/ported/battle/` and
`port/src/ported/entity/`; none of it touches the field path.

1. **`FUN_002493f0`** — new helper. Returns the target index and fills a
   position; the no-target branch is section 2. Needed by two callers, so put it
   next to `FUN_002d9b78_drive_cast_ring` in `battle_character_update.cpp` and
   expose it on `ActorEnvironment` for the entity side.
2. **`FUN_002f1380`** — new setter on `BattleParty` (it owns `DAT_0031dad0_`)
   plus a `DAT_00355588` raise. The port already has the read side.
3. **State 113**, the five missing blocks marked `>>> MISSING` in section 3.
4. **State 114**, written out in full; delete the delegation to 109.
5. **`FUN_002deae8`** (type `0x174`) in `actor_frame_update.cpp`, modelled on
   the `0x13D` handler directly above it. Register `0x002DEAE8` in
   `actorHandlerIsImplemented`, `actorHandlerName` and the dispatch switch.
6. **`FUN_002de650` + `FUN_002de9e8`**, the launch. `FUN_00215ac8_box_hit_test`
   and `FUN_00267da0` already exist.
7. **`FUN_002dee08`** (type `0x15C`) and **`FUN_002e4c00`** (type `0x178`).
8. Optional: `FUN_0023bbd8` (shake/rumble) and `FUN_0023f620(2, member)` (a
   statistics counter) can be stubs; neither affects anything visible.

### Verification

- `--battle-report` already prints `anim=`. Add a `ring=` column (the `0x18F`
  marker's `scale14c`) and a `hitfx=` column (`DAT_0031dad0`'s `scale14c`), or
  the growth is invisible headless.
- `--hold-attack 200-320` on `s14_e012`: expect action `0x8C` -> `0x8D`, states
  113 -> 114, `partyRecord+0x45` leaving `-1` a few frames in, charge climbing
  to `0x2D00`, and a `0x15C` + `0x178` pair appearing on the release frame.
- With no enemy table the target stays `-1`, so section 2's no-target branch is
  the one that runs: the burst should land two units in front of Orphen, and
  `--screenshot` at the release frame should show it there.
- The determinism guard is unchanged: `--frames 3000 --actor-report
  --scr-report` on `s01_e024` and `s01_e012` must stay byte-identical.

---

## 8. The summon — where to pick it up

`FUN_002deef0` is the only entry, taken from `FUN_002de650` on `level == 5 &&
target > 1`. It spawns type **`0x13E`** at animation 5, copies the same four
attack bytes, arms a 32-frame timer at `+0x1AC`, and then:

```c
uGpffffaf5c = 1;        // the battle-interrupt flag
FUN_002de4a8();
FUN_002de640(spawned);
FUN_002de640(0x58beb0);  // and the player
```

`0x13E`'s behaviour is `FUN_002df018` (in `src/`, 241 lines) — that is the
summon animation and the all-enemies damage sweep. `uGpffffaf5c` is what stops
the rest of the battle while it plays; find its readers before starting.
