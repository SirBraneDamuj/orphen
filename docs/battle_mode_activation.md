# Battle Mode Activation Memory Addresses

## Overview

During reverse engineering analysis of the main game loop (`FUN_002239c8`), we discovered the memory addresses that control battle mode activation in Orphen: Scion of Sorcery. These addresses can be manually modified in PCSX2 to force the game into battle mode for debugging purposes.

## Memory Addresses

### Primary Battle Mode Flags

| Variable      | GP Offset | Absolute Address | Type  | Description                     |
| ------------- | --------- | ---------------- | ----- | ------------------------------- |
| `cGpffffb663` | -0x9AD    | 0x003555D3       | char  | Primary battle mode state flag  |
| `sGpffffb052` | -0xF4E    | 0x00355022       | short | Secondary battle mode condition |

**Global Pointer (GP) Value:** 0x00359F70

### Address Calculation

```
Absolute Address = GP + GP_Offset
0x003555D3 = 0x00359F70 + (-0x9AD)
0x00355022 = 0x00359F70 + (-0xF4E)
```

## Battle Mode Activation

### PCSX2 Debugging Steps

1. **Open Memory Editor** in PCSX2 debugger
2. **Navigate to addresses:**
   - `0x003555D3` (cGpffffb663)
   - `0x00355022` (sGpffffb052)
3. **Set both values to non-zero** (any value > 0)
4. **Verify activation** by checking pause menu text changes

### Expected Behavior

**Normal Field Mode:**

- Pause menu shows: "Paused"
- Uses player update function `FUN_00251ed8` (field mode)

**Activated Battle Mode:**

- Pause menu shows: "Return to Battle" and "Change Equipment and Return to Battle"
- Uses player update function `FUN_00249610` (battle mode)
- Battle-specific UI elements appear

## Code Analysis Context

### Main Game Loop Logic

The battle mode check occurs in `FUN_002239c8` (main game loop):

```c
// Simplified conditional logic
if ((cGpffffb663 != 0) && (sGpffffb052 != 0)) {
    // Battle mode: call FUN_00249610
    FUN_00249610(entity_pointer);
} else {
    // Field mode: call FUN_00251ed8
    FUN_00251ed8(entity_pointer);
}
```

### Function Differences

**Field Mode (`FUN_00251ed8`):**

- 753 lines of complex player update logic
- Input processing, death sequences, state machines
- Comprehensive field exploration mechanics

**Battle Mode (`FUN_00249610`):**

- Simpler battle-focused update logic
- Character stats table lookups using entity offset 0x95
- Battle-specific state management

## Global Variables Documentation

The battle mode flags are documented in `analyzed/orphen_globals.h`:

```c
extern char cGpffffb663;          // Debug mode state flag / Battle mode flag
extern short sGpffffb052;         // Secondary battle mode condition
```

**Note:** The variable `cGpffffb663` appears to serve dual purposes as both a debug mode state flag and battle mode activation flag, suggesting potential overlap between debug and battle systems.

## Limitations and Notes

- **Incomplete Initialization:** Forcing battle mode via memory manipulation may not properly initialize all battle-related systems
- **State Inconsistency:** Other game systems may remain in field mode state
- **Debug Tool Only:** This method is primarily useful for code analysis and debugging, not gameplay
- **PCSX2 Specific:** These absolute addresses are specific to the PS2 version running in PCSX2

## Related Files

### Core Battle Mode Logic

- [orphen_globals.h](../analyzed/orphen_globals.h) - Global variable declarations
- [main_game_loop.c](../analyzed/main_game_loop.c) - Main game loop with mode selection logic (analyzed `FUN_002239c8`)
- [update_main_character_entity.c](../analyzed/update_main_character_entity.c) - Field mode player update (`FUN_00251ed8`)
- [src/FUN_00249610.c](../src/FUN_00249610.c) - Battle mode player update function

### SCR Opcodes Related to Battles

- [ops/0x6D_control_character_ai_mode.c](../analyzed/ops/0x6D_control_character_ai_mode.c) - Controls character battle state flags (not global mode)
- [ops/0xDF_initialize_camera_entity.c](../analyzed/ops/0xDF_initialize_camera_entity.c) - Battle logo initialization
- [ops/0xBE_call_function_table_entry.c](../analyzed/ops/0xBE_call_function_table_entry.c) - Function table dispatcher

### Debug/Test Systems

- [mcb_debug_menu_interface.c](../analyzed/mcb_debug_menu_interface.c) - MCB debug menu (`FUN_00268e20`) - **Only code that writes to `cGpffffb663`**
- [debug_menu_handler.c](../analyzed/debug_menu_handler.c) - Debug menu system (`FUN_00268d30`)
- [src/FUN_00268ce0.c](../src/FUN_00268ce0.c) - Debug menu dispatcher using `PTR_FUN_0031edd8`

## SCR Opcode Connection

### Battle Triggering Mechanism

The battle mode flags are **not** directly set by SCR opcodes. Instead, the game uses a **debug menu system** that can indirectly trigger battle mode:

**Key Discovery:** `FUN_00268e20` (MCB Debug Menu Interface - index 1 in `PTR_FUN_0031edd8`) writes to `cGpffffb663`:

```c
// From src/FUN_00268e20.c
cGpffffb663 = 0;  // Clear battle/debug flag
// ... menu processing ...
cGpffffb663 = cVar3;  // Restore or set battle/debug flag
```

This function is part of the debug menu dispatch table at `PTR_FUN_0031edd8[1]`, invoked by the debug system through `FUN_00268ce0` when `iGpffffb124 < iGpffffb13c`.

### Relevant Opcodes

**Opcode 0x6D** - Control Character AI Mode ([0x6D_control_character_ai_mode.c](../analyzed/ops/0x6D_control_character_ai_mode.c))

While this opcode doesn't set `cGpffffb663`, it **does** control battle-related character state:

```c
// When parameter < -2 (e.g., -3, -4):
DAT_0058beb8 |= 1;      // Enable battle mode flag (character-level)
DAT_0058c614 |= 0x4000; // Set battle active flag
DAT_0058c618 |= 1;      // Enable attack

// When parameter == 1 (exit battle):
DAT_0058c614 &= 0xBFFF; // Clear battle active flag
DAT_0058c618 &= 0xFFFE; // Disable attack
```

**Key Insight:** The game separates **global battle mode** (`cGpffffb663`/`sGpffffb052` - checked in main loop) from **character battle state** (DAT_0058c614 bit 0x4000 - set by opcode 0x6D). The character battle flags at `0x58c614` and `0x58c618` control combat behavior, while the global flags determine which player update function runs.

### Why Battles Aren't Script-Triggered

The architecture suggests battles are **event-driven** rather than script-commanded:

1. **Main Loop Decision**: `FUN_002239c8` checks `cGpffffb663 != 0 && sGpffffb052 != 0`
2. **Character Setup**: SCR opcode 0x6D prepares characters for combat (animation, attack flags)
3. **System Initialization**: Debug/test menus can force battle mode via `FUN_00268e20`

The lack of SCR opcodes that directly write to `cGpffffb663`/`sGpffffb052` implies battles may be triggered by:

- **Game engine events** (encounter triggers, area transitions)
- **Flag-based logic** (game state checks in native code)
- **Debug/test systems** (developer menus, testing harnesses)

### Related Opcodes

**Opcode 0xBE** - Call Function Table Entry ([0xBE_call_function_table_entry.c](../analyzed/ops/0xBE_call_function_table_entry.c))

Dispatches to function pointer table `PTR_FUN_0031e730[index](arg)`. While this could theoretically call debug menu functions, we found no evidence of it being used to trigger battle mode transitions in normal gameplay scripts.

**Opcode 0xDF** - Initialize Battle Logo ([0xDF_initialize_camera_entity.c](../analyzed/ops/0xDF_initialize_camera_entity.c))

Sets up the "SORCEROUS STABBER ORPHEN" battle logo entity, indicating an imminent battle, but does not set battle mode flags.

## Discovery Context

This discovery was made during analysis of `FUN_00251ed8`, initially thought to be a generic character entity update function. Investigation revealed it to be specifically the field mode player update, leading to the identification of the battle mode switching mechanism in the main game loop.

Further investigation into SCR opcodes revealed that while character-level battle state is controlled by opcode 0x6D, the global battle mode flags appear to be managed by the game engine itself rather than script commands, with the debug menu system (`FUN_00268e20`) being the only code path found that directly writes to `cGpffffb663`.
