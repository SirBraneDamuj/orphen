# Battle System Analysis

Reverse-engineered architecture of the real-time battle system in Orphen: Scion of Sorcery (US).

See also: `docs/battle_mode_activation.md` for how to force battle mode via memory editing.

## Overview

The battle system has three independent layers:

1. **Battle UI State Machine** — menu/spell selection overlay (DAT_00354da0)
2. **Battle Command Input Handler** — translates button presses into battle commands (FUN_002462c8)
3. **Battle Entity Update** — per-entity state processing, damage, HP (FUN_00249610)

The field mode and battle mode use entirely different player update functions. The switch happens in the main game update function FUN_002239c8 at address 0x002239c8:

```c
if ((cGpffffb663 == '\0') || (sGpffffb052 == 0)) {
    FUN_00251ed8(0x58beb0, held, pressed);  // field mode
} else {
    FUN_00249610(0x58beb0);                 // battle mode
}
```

- `cGpffffb663` (0x003555D3): battle mode flag
- `sGpffffb052` (0x00355022): secondary battle condition

---

## Layer 1: Battle UI State Machine

**State variable:** DAT_00354da0
**Dispatch table:** PTR_LAB_0031c2f0[DAT_00354da0]
**Master tick function:** FUN_0022e910 (0x0022e910)

FUN_0022e910 is the top-level battle UI processor. Each frame it:
1. Checks Triangle (0x10 on DAT_003555f6) for action confirmation
2. Dispatches to the current state handler via `PTR_LAB_0031c2f0[DAT_00354da0]()`
3. Renders HUD elements (spell icons, HP bars, action text)

### UI States

| DAT_00354da0 | Handler | Description |
|---|---|---|
| 1 | (in FUN_0022e910) | Waiting for Triangle to confirm |
| 3 | FUN_0022f2d8 | Post-cast, return to spell select |
| 4 | FUN_0022f588 | Active spell selected, waiting for animation |
| 6 | (set by FUN_0022f408) | Spell cast initiated |
| 7 | FUN_0022f620 | Attack execution and target resolution |
| 8 | (set by FUN_0022f408) | Cast failed / cooldown |
| 0xC | (in FUN_0022e910) | Triangle during active state |
| 0xD | (in FUN_0022e910) | Auto-advance |

### Spell Slot Selection (FUN_0022f408, 0x0022f408)

- Up/Down (0x1000/0x4000) cycles through 3 spell slots stored at entity+0x1C0 (range 0-2)
- X button (0x40) confirms the cast
- On success: sets DAT_00354da0 = 6, entity+0x62 = 0x80, entity+0x60 = 10
- On failure (cooldown): sets DAT_00354da0 = 8
- Spell entities are stored in the `DAT_00570da0[0..2]` array (3 pointers)

### Spell Assignment Menu (FUN_002320a8, 0x002320a8)

The pre-battle spell assignment screen where the player maps spells to buttons:
- State 2 is the active assignment menu
- Face buttons (Triangle=0x10, Circle=0x20, Cross=0x40, Square=0x80 on DAT_003555f6) assign spells
- Calls FUN_00232058 to swap entries in the `DAT_00355608` mapping table (8 bytes)
- `DAT_00355c28` tracks the cursor position (0-4)

---

## Layer 2: Battle Command Input Handler

**Function:** FUN_002462c8 (0x002462c8)

This function reads raw button input and returns command codes that drive entity state transitions. It uses configurable per-character button mask tables.

### Button Input

```c
uVar17 = (uint)DAT_003555f6;  // newly pressed buttons
uVar18 = (uint)DAT_003555f4;  // held buttons
```

### Per-Character Button Mask Table

Base address: `0x0031dc18 + (charIndex * 0x28)` where charIndex is 0-based.

| Offset | Address Pattern | Purpose |
|---|---|---|
| +0x00 | 0x31dc18 | Attack button mask |
| +0x04 | 0x31dc1c | Spell cast buttons |
| +0x08 | 0x31dc20 | Guard-while-charging button |
| +0x0C | 0x31dc24 | Guard initiate buttons |
| +0x10 | 0x31dc28 | Charge hold button |
| +0x14 | 0x31dc2c | Secondary spell buttons |
| +0x18 | 0x31dc30 | Guard-during-attack button |
| +0x1C | 0x31dc34 | Guard trigger buttons |
| +0x20 | 0x31dc38 | Block sustain mask |
| +0x24 | 0x31dc3c | Guard button (Square) |

The 3 spell slot button masks are at `DAT_0031d168[0..2]` (likely 0x10, 0x20, 0x40 for Triangle, Circle, Cross).

### Per-Character Battle State Buffer

Base: `0x0031d7bc + charIndex * 0x3c`

| Offset | Purpose |
|---|---|
| +0x02 | Command byte (written by input handler, consumed by FUN_0024a360) |
| +0x03 | Current battle state |

### Battle States

| Byte Value | Meaning |
|---|---|
| 0x06 | Idle / ready |
| 0x83 | Hit stun |
| 0x84 | Attack active |
| 0x85 | Guard active (holding) |
| 0x86 | Casting spell |
| 0x87 | Death |
| 0x8A | Guard while charging |
| 0x8C | Charge active |
| 0x8E | Secondary spell active |
| 0x90 | Block / guard active |
| 0x96 | Special state |
| 0x0B | Disabled |

### Command Return Values

These are returned by FUN_002462c8 and processed by FUN_0024a360:

| Return | Meaning |
|---|---|
| 0x84 | Attack/spell initiated |
| 0x85 | Guard release |
| 0x86 | Spell cast begin |
| 0x88 | Guard state |
| 0x89 | Charging spell |
| 0x8A | Guard while charging |
| 0x8B | Guard release (from charge) |
| 0x8C | Begin charge |
| 0x8D | Charge release |
| 0x8E | Secondary spell |
| 0x8F | Secondary release |
| 0x90 | Guard/block |
| 0x91 | Guard release |
| 1 | Idle (no relevant input) |

### Input Flows

**Spell Casting:**
```
newly_pressed & spell_cast_mask != 0
  -> iterate DAT_0031d168[0..2] to find which of 3 slots was pressed
  -> store slot index in DAT_0031da65[charIndex]
  -> command = 0x86, return 0x84
```

**Charging:**
```
newly_pressed & charge_mask != 0
  AND per-slot cooldown timer at (0x31dd08 + charIndex*10 + slot*2) == 0
  -> command = 0x8C (begin charge), return 0x89

held_button released during state 0x8C:
  -> command = 0x8D (release charge)
  -> writes cooldown timer for that slot
```

**Guard:**
```
state == 0x06 (idle):
  held_buttons & guard_mask (0x31dc3c + charIndex*10) != 0
    -> command = 0x90, enter guard

state == 0x90 (guarding):
  held_buttons & block_sustain_mask (0x31dc38 + charIndex*0x28) != 0
    -> continue guarding
  else
    -> command = 0x91, guard release
```

### Key Globals

| Address | Variable | Purpose |
|---|---|---|
| 0x00354ebe | DAT_00354ebe | Current active character (1-based) |
| 0x0031d168 | DAT_0031d168[0..2] | Face button masks for 3 spell slots |
| 0x0031da65 | +charIndex | Selected spell slot index (0-2) |
| 0x0031daac | +charIndex | Active spell entity pointer |
| 0x0031dabc | +charIndex | Active secondary entity pointer |
| 0x0031dd08 | +charIndex*10 | Per-slot cooldown timers (5 shorts) |

---

## Layer 3: Battle Entity Update

**Function:** FUN_00249610 (0x00249610)

Called each frame for each battle entity (player and allies). Uses `entity+0x95` (byte, 1-based character index) to look up per-character data tables:

```c
DAT_00355cac = &PTR_FUN_0031d364 + charIndex * 0x19;  // function table
DAT_00355cb0 = &DAT_0031d38c + charIndex * 100;       // character data
DAT_00355cb4 = &DAT_0031d774 + charIndex * 0x3c;      // state data A
DAT_00355cb8 = &DAT_0031d780 + charIndex * 0x3c;      // state data B (battle state buffer)
```

### State Dispatch

Battle entity states start at 100 (0x64). The entity's action state is at entity+0x60 (with bit 0x4000 as a dirty flag, masked off with `& 0xBFFF`).

Each character class has its own function table, selected by `(&DAT_0031d3c8)[(charIndex-1) * 0x32]`:

| Class | Table Address | Description |
|---|---|---|
| 1 | DAT_0031dd60 | Standard melee |
| 3 | DAT_0031de20 | Class 3 |
| 4 | DAT_0031de80 | Special (uses entity+0x62 as parameter) |
| 5 | DAT_0031ddc0 | Class 5 |
| 6 | DAT_0031dee0 | Class 6 |
| 7 | DAT_0031df40 | Class 7 |

Dispatch: `(*table[(state & 0xBFFF) - 100])(entity, entity+0x62)` — the return value is written back to entity+0x62 (sub-state / charge level).

### Damage Processing (FUN_0024a360, 0x0024a360)

Called before the state dispatch to handle incoming damage and command transitions:

- If `entity+0xBE > 0` (damage pending): sets state to 0x4066 (hit reaction), battle state byte to 0x83
- If `entity+0xBE == 0`: processes command bytes from the battle state buffer (+2), converting them to entity states via `entity+0x60 = command + 0x3FE5`

---

## Collision Detection

**Function:** FUN_002148a8 (0x002148a8)

The main collision detection engine for spell/attack hitboxes. Called from individual spell entity update methods.

### Algorithm

1. Builds an attack hitbox from animation keyframe data
   - Interpolates between keyframes using 0x1000 (4096) fixed-point math
   - Scales by `0.00390625` (1/256) to convert to world coordinates
   - Generates a series of AABB segments along the attack's path
2. Iterates every entity in the world (base `DAT_0058beb0`, stride 0xEC)
3. For each candidate entity, performs a 3D AABB overlap test:
   - Entity bounding box: position (+0x10/0x12/0x14) plus extents (+0x88/0x8A/0x8C/0x8E/0x90)
   - Skips entities that already have pending damage (`entity[0x5F] != 0`)
   - Skips entities with invulnerability flag (bit 0x10 at offset +0x04)
   - Skips entities with active cooldown (`entity[0x60] != 0`)
4. On hit: stores attacker reference in `target+0x66`, copies attack angle to `target+0x62`
5. Calls FUN_00216140 to calculate and apply damage

---

## Damage Calculation

**Function:** FUN_00216140 (0x00216140)

Called by the collision engine when a hit is confirmed. Computes final damage and applies it to the target.

### Setup

1. Copies element flags from attack data to target: `target[0x61] = attack_element`
2. Copies damage type byte to `target+0x5E`
3. Builds an element effectiveness table (`acStack_88`, 16 bytes) via FUN_0025bae8
   - Each entry is a percentage: 100 = normal, >100 = weakness, <100 = resistance

### Damage Formula

```
element_index = first set bit in attack element flags

raw_damage = (element_table[element_index] / 100.0)
           * ((attack_power_modifier + 100) / 100.0)
           * base_attack_stat

net_damage = raw_damage - defense
if net_damage < 1: net_damage = 1

target.pending_damage += net_damage
```

Where:
- `element_table[element_index]`: percentage from the effectiveness table (line 81)
- `attack_power_modifier`: `(char)param_2[1]` — a signed byte offset to base power (line 82)
- `base_attack_stat`: `*(short *)(attacker + 300)` — the attacker's attack value at offset 0x12C (line 83)
- `defense`: `target[0x97]` — the target's defense stat (line 94)

### Guard Angle Check

If the target has a nonzero guard arc (`target+0x92` as float, line 99):
- Calculates the angle between the attack direction (`target+0x62`) and the entity's facing (`target+0x2E`)
- If the attack comes from within the guard arc, damage is **negated** (`target[0x5F] = -target[0x5F]`)
- If outside the arc, damage applies normally

### HP Bar Display

After damage is applied, calls FUN_002d5630 to update the HP bar:

```c
FUN_002d5630(
    (target[1] & 0x48) != 0,  // 0 = player, 1 = enemy
    target[0x95],               // max HP
    target[0x94],               // current HP
    net_damage                  // damage dealt
);
```

**FUN_002d5630** (0x002d5630) converts HP into a 5-segment bar and triggers the drain animation. The first argument selects which bar entity to use:
- 0: player HP bar at DAT_0058c260 (screen bottom, Y=456.0)
- 1: enemy HP bar at DAT_0058c438 (screen top, Y=-8.0)

---

## Player Guard Damage Processing

**Function:** FUN_0024cba0 (0x0024cba0)

Called when the player character takes damage while blocking. Confirmed via breakpoint: triggers on block, not on unguarded hits.

1. Reads damage from entity+0xBE
2. If damage == 0, returns
3. Creates a hit effect entity (type 0x1C7) at the impact point
4. **Guard damage reduction:** `reduced_damage = abs(damage) * 0x8000 >> 16` (approximately halves the damage)
5. **Stagger threshold:** compares reduced damage against `entity+0x12A`
   - Below threshold: minor hit, shows damage numbers only (no stagger)
   - Above threshold: full stagger, sets entity state to 0x4066

---

## Enemy Damage Handlers

Enemies process pending damage in their own type-specific entity methods. All follow the same pattern:

```c
if (entity[0xBE] != 0) {
    HP = entity[0x12A] - entity[0xBE];
    entity[0x12A] = (HP < 0) ? 0 : HP;
    entity[0xBE] = 0;  // clear pending damage
    // trigger hit reaction or death
}
```

Known enemy damage handlers:
- FUN_00280980 (0x00280980)
- FUN_00282588 (0x00282588)
- FUN_00283830 (0x00283830)
- FUN_002850b8 (0x002850b8) — does direct `entity[0x12A] -= entity[0xBE]`
- FUN_00286e18 (0x00286e18)
- FUN_002875c0 (0x002875c0)

### Elemental Damage Check (FUN_0024bd30, 0x0024bd30)

For player-side battle entities, this function handles elemental matching:
- Compares the attack's element type (from a lookup table) against entity+0xC2
- If element matches: clears damage, dispatches event 0xD0 (resisted/absorbed)
- If element doesn't match: negates damage value and creates visual effect (damage goes through)

### Status Effect Application (FUN_0024a190, 0x0024a190)

Handles elemental status effects on entities. Checks entity+0xC2 for element bit flags:

| Bit | Value | FUN_002d8b38 param | Likely Element |
|---|---|---|---|
| 1 | 0x02 | 1 | Element A |
| 4 | 0x10 | 4 | Element B |
| 5 | 0x20 | 5 | Element C |
| 6 | 0x40 | 6 | Element D |
| 9 | 0x200 | 9 | Element E |
| 10 | 0x400 | 10 | Element F |
| 12 | 0x1000 | 12 | Element G |

Status application has a random chance component: `(entity+0xBC) < random() % 10` determines if the status takes effect.

---

## Spell Entity Lifecycle

### Creation

When a spell is cast (FUN_0022f408 confirms, FUN_00230910 validates):
1. The active spell slot index (0-2) at entity+0x1C0 selects from `DAT_00570da0[0..2]`
2. Spell entity data is loaded, including element flags, base damage, and animation data
3. Spell entity is placed in the world

### Confirmed Call Chain (from breakpoint)

When a player spell hits an enemy, the call stack is:

```
FUN_00239ce0  — entity update loop (iterates all entities, stride 0xEC from 0x0058c260)
  FUN_002da6f0  — spell entity update method
    FUN_002148a8  — collision detection (AABB test against all entities)
      FUN_00216140  — damage calculation (element table, attack stat, defense)
        FUN_002d5630  — HP bar update (arg0: 0=player, 1=enemy)
```

### Transfer Mechanisms

Some spell types use indirect damage transfer:
- FUN_0028f1e0: spell entity transfers damage via `FUN_0023ef90()` target lookup
- FUN_002907f8: AOE spell distributes damage to all eligible battle characters
- FUN_002ba5a0, FUN_002beb58, FUN_002bfc68: projectile-to-target damage copy

---

## Entity Structure (Battle-Relevant Offsets)

All offsets from entity base (e.g., player at 0x0058beb0). Entity stride is 0x1D8 bytes (472) for the main entity array, 0xEC (236) for the update loop starting at 0x0058c260.

| Byte Offset | Short[n] | Type | Purpose |
|---|---|---|---|
| +0x00 | [0] | short | Entity ID / type |
| +0x02 | [1] | short | Category flags (0x08/0x40 = enemy for HP bar) |
| +0x04 | [2] | short | Status flags (0x10=invulnerable, 0x4000=skip update) |
| +0x08 | [4] | short | More flags |
| +0x0A | [5] | short | Surface ID |
| +0x0C | [6] | short | Physics flags (bit 1 = on ground) |
| +0x20 | [0x10] | float | X position |
| +0x24 | [0x12] | float | Y position |
| +0x28 | [0x14] | float | Z position |
| +0x30 | [0x18] | float | X velocity |
| +0x34 | [0x1A] | float | Y velocity |
| +0x38 | [0x1C] | float | Z velocity |
| +0x44 | [0x22] | float | Gravity |
| +0x54 | [0x2A] | float | Collision radius |
| +0x58 | [0x2C] | float | Entity height |
| +0x5C | [0x2E] | float | Rotation / facing angle |
| +0x60 | [0x30] | short | **Action state** (battle: 100+, 0x4000 = dirty bit) |
| +0x62 | [0x31] | short | **Sub-state / charge level** (passed to state handler) |
| +0x88 | [0x44] | float | Bounding box offset X min |
| +0x8A | [0x45] | float | Bounding box offset Y min |
| +0x8C | [0x46] | float | Bounding box offset Z min |
| +0x8E | [0x47] | float | Bounding box extent X |
| +0x90 | [0x48] | float | Bounding box extent Z |
| +0x95 | — | byte | **Character index** (1-based, used for table lookups) |
| +0xA0 | [0x50] | short | Animation state |
| +0xBC | [0x5E] | byte | Damage type / element application chance |
| +0xBD | — | byte | Hit cooldown counter |
| +0xBE | [0x5F] | short | **Pending damage** (positive=normal, negative=blocked) |
| +0xC0 | [0x60] | short | **Cooldown timer** (counts down each frame) |
| +0xC2 | [0x61] | short | **Element/damage flags** (0x2000=guard flag in field mode) |
| +0xC4 | [0x62] | float | Attack incoming angle |
| +0x124 | [0x92] | float | **Guard arc** (radians, 0=no guard check) |
| +0x128 | [0x94] | short | **Current HP** |
| +0x12A | [0x95] | short | **Max HP** |
| +0x12C | — | short | **Attack stat** (base_attack, offset 300 decimal) |
| +0x12E | [0x97] | short | **Defense stat** |
| +0x134 | [0x9A] | byte | Action state byte |
| +0x192 | — | short | Linked entity index |
| +0x198 | [0xCC] | int | Linked entity pointer (weapon/spell/parent) |
| +0x1A0 | [0xD0] | int | Target entity pointer |
| +0x1B4 | — | short | Timer (counted down by frame delta) |
| +0x1B6 | — | short | Last command received |
| +0x1B8 | [0xDC] | short | **Invincibility timer** |
| +0x1C0 | — | short | **Active spell slot index** (0-2) |

---

## Recommended Breakpoints

| Address | Function | What You'll See |
|---|---|---|
| 0x002462c8 | Battle command input | Return value = command code, watch DAT_0031d168 for button masks |
| 0x0024a360 | Command processor | How commands become entity states |
| 0x00249610 | Battle entity update | Per-frame entity processing, state dispatch at line 347 |
| 0x002148a8 | Collision detection | Hitbox generation and AABB tests against all entities |
| 0x00216140 | Damage calculation | Element table, attack/defense stats, final damage |
| 0x002d5630 | HP bar update | Args: (0=player/1=enemy, current_hp, max_hp, damage) |
| 0x0024cba0 | Player block handler | Guard damage halving, stagger threshold |
| 0x0024bd30 | Element matching | Attack element vs entity element comparison |

---

## Elemental System

### Element Index Mapping

The game uses a 16-byte effectiveness table per entity, loaded by FUN_0025bae8 → FUN_00229688. Each byte is a percentage value (0–255) representing how effective that element is against the entity.

| Byte Index | Element | Bitmask (attack) | Pentagon Position |
|---|---|---|---|
| 0 | Physical (sword) | 0x0001 | Not on pentagon |
| 1 | Lightning | 0x0002 | Top-left |
| 2 | Wind | 0x0004 | Bottom-left |
| 3 | *(unused)* | — | — |
| 4 | Fire | 0x0010 | Bottom |
| 5 | Dark | 0x0020 | Bottom-right |
| 6–9 | *(unused)* | — | — |
| 10 | Ice | 0x0400 | Top-right |
| 11–15 | *(unused)* | — | — |

6 elements total: 1 physical + 5 magical. Unused indices default to `0x64` (100) in effectiveness tables.

### Effectiveness Values

- `0x00` (0) = immune
- `0x01`–`0x22` (1–34) = resistant (pentagon: 1 pip)
- `0x23`–`0x4A` (35–74) = normal (pentagon: 2 pips)
- `0x4B`+ (75+) = weak (pentagon: 3 pips)
- `0x64` (100) = neutral baseline

Some enemies have high physical effectiveness (`0x64`) but low magic, meaning they're designed to be beaten with the sword.

### Attack Element Encoding

Attack element bitmask is at `param_2[0]` in FUN_00216140. The bit POSITION gives the element index (e.g., `0x0010` = bit 4 = Fire). This is different from the pentagon table `DAT_0031c240` which stores plain indices.

### Effectiveness Data Sources

- Per-enemy table: 16 bytes at offset +0x18 in the 0x28-byte structure loaded by FUN_0025bae8
- Enemy pentagon display: reads from `0x5715b8` (populated by FUN_002334e8)
- Cached tables for all 7 battle characters: `DAT_00343688` (stride 0x28), loaded by FUN_002294d0

---

## Targeting Pentagon HUD

### Rendering

- **FUN_0022ec30** (0x0022ec30): Core pentagon renderer. Loops exactly 5 times (indices 0–4), reading `DAT_0031c240[i]` as element indices. Renders counterclockwise starting from top-left.
- **FUN_00233818** (0x00233818): Enemy pentagon display wrapper. Checks `DAT_005715e0 != '\0'` (entity name must be populated), then calls `FUN_0022ec30(0x220, 0x94, 0x5715b8, 1)`.
- **FUN_00230e50** (0x00230e50): Player pentagon display.
- Pentagon point order from `DAT_0031c240`: indices 1, 4, 2, 5, 10 (Lightning, Fire, Wind, Dark, Ice).
- `DAT_0031c2a0` controls rotation angles for each vertex position.

### Weakness Data Population

**FUN_002334e8** (0x002334e8): Populates the weakness structure at `0x5715b8` from a target entity pointer.
- Takes entity pointer as `param_1`
- Reads entity type from `*(short*)(param_1 + 0)` (falls back to `param_1[0xe7]` if type == 0x38)
- Calls FUN_00229688 to load effectiveness data
- Sets entity name at `0x5715e0` (required for FUN_00233818 to render)
- Boss entities (type 0x192) require temporary type fixup to 0x86/0x87 before calling, then restore to 0x192 after

### Normal Battle Targeting

In normal battles, the targeting pentagon is managed by **FUN_0023c340** (0x0023c340), called through entity method 0x68 → FUN_00242a18 → FUN_00242cf0.

Key globals:
| Address | Variable | Purpose |
|---|---|---|
| 0x00354e96 | DAT_00354e96 | Pentagon display timer (set to 0xF00 on D-pad press) |
| 0x00354e90 | DAT_00354e90 | Current target entity pointer |
| 0x00354fac | DAT_00354fac | Dialogue/track data pointer (null during boss fights) |

Timer is decremented each frame via FUN_00248e58 (subtracts frame delta `sGpffffb64c`, clamps to 0).

### Boss Battle Targeting Differences

In boss battles, the pentagon is **not displayed** through the normal path because:

1. FUN_0023c340 is never called — boss entity class doesn't invoke entity method 0x68
2. Even if called, `DAT_00354fac == 0` during boss fights causes an early return (address 0x0023c394: `beq v0,zero,0x0023d2c0`)
3. FUN_002462c8 takes the `goto LAB_00246770` early exit in boss mode, skipping the block that sets `DAT_00354e96 = 0xF00` and `DAT_00354e90 = entity_ptr`
4. `DAT_00354ec0 != 0` during boss battles causes the else branch in FUN_002462c8, which explicitly clears `DAT_00354e96 = 0`

The enemy table (`DAT_00354eb4`, stride 0x3c, entity ptr at offset +8) typically has only 1 slot populated for boss fights even when multiple enemies are visible. `DAT_00354eba` (enemy count) = 1 for bosses vs. higher values for normal battles.

---

## Open Questions

- **Guard flag source:** In field mode, entity+0xC2 bit 0x2000 acts as a guard flag checked in FUN_00251ed8. In battle mode, the guard arc check at entity+0x124 in FUN_00216140 serves this purpose instead. The relationship between these two systems is unclear.
- **Charge mechanics:** Charging modifies entity+0x62 (passed in/out of state handlers). The exact scaling for projectile count, weapon length, and AOE radius is inside the per-class state handler functions (tables at 0x31dd60 etc.) which haven't been fully traced.
- **Spell data source:** Base damage, element type, and power modifier appear to come from attack data structs passed through the collision system. These are likely loaded from SCR script data.
- **Cooldown system:** Per-slot cooldown timers at `0x31dd08 + charIndex*10` are set when spells are released. The exact values and how they relate to the cooldown animation haven't been confirmed.
- **Enemy AI:** Enemy attack decisions are likely driven by bytecode scripts (SCR files), not C code. The C code handles execution (collision, damage) but not decision-making.
- **Boss enemy table:** `DAT_00354eba` reports 1 slot for boss fights with 3-4 visible enemies, and 6 slots for normal fights with 5 enemies. The relationship between slot count and visible enemy count is unclear — bosses may use a parallel entity structure.
