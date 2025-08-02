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

- `analyzed/orphen_globals.h` - Global variable declarations
- `src/FUN_002239c8.c` - Main game loop with mode selection logic
- `src/FUN_00251ed8.c` - Field mode player update function
- `src/FUN_00249610.c` - Battle mode player update function

## Discovery Context

This discovery was made during analysis of `FUN_00251ed8`, initially thought to be a generic character entity update function. Investigation revealed it to be specifically the field mode player update, leading to the identification of the battle mode switching mechanism in the main game loop.
