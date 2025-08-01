# Debug System Documentation

## Overview

Orphen: Scion of Sorcery contains a comprehensive debug system that was used during development to monitor and control various game systems. The debug functionality is accessible through an in-game debug menu and provides real-time visualization and control over game state.

## Debug Menu Structure

The main debug menu (`debug_menu_handler.c` - FUN_00268d30) provides three primary debug options:

1. **POSITION_DISP** - Position display system
2. **MINI_MAP_DISP** - Mini-map debug visualization
3. **SCR_SUBPROC_DISP** - Scene subprocess display (leads to SCEN WORK DISP submenu)

### Controls

- **Up/Down D-pad**: Navigate between options
- **Circle button**: Toggle selected option ON/OFF
- **Start button**: Exit debug menu

## Debug Output System

### Core Function: `debug_output_formatter` (FUN_002681c0)

The debug output system provides formatted on-screen display of debug information. This function works similarly to `sprintf` but outputs directly to the game's display overlay when debug output is enabled.

**Key Features:**

- Format string processing with variable arguments
- Conditional output based on debug flags
- Real-time on-screen overlay rendering
- Buffer management for debug text

**Global Control Flag:**

- `DAT_003555dc` - Master debug output enable flag (0=disabled, 1=enabled)

**Memory Address:** `0x003555dc`

### Usage Pattern

```c
// Example debug output call
debug_output_formatter(format_string_address, parameter1, parameter2, ...);
```

## Scene Work Display System (SCEN WORK DISP)

### Overview

The Scene Work Display is a powerful debugging submenu that allows developers to toggle individual scene processing flags in real-time. This system controls the visibility and processing of various scene elements.

### Function: `scene_work_display_debug_menu` (FUN_0026a508)

**Features:**

- 128 individual toggleable flags (indices 0-127)
- Real-time flag state visualization
- Immediate effect on scene processing
- Wraparound navigation for easy access

**Controls:**

- **Up/Down D-pad**: Navigate flag index ±1
- **Left/Right D-pad**: Navigate flag index ±10
- **Triangle/X buttons**: Navigate flag index ±10
- **Circle button**: Toggle selected flag ON/OFF
- **Start button**: Exit submenu

### Flag Storage

**Memory Location:** `DAT_0031e770` (Address: `0x0031e770`)
**Format:** Array of 4 32-bit integers (128 bits total)
**Size:** 16 bytes (4 × 32-bit integers)

```
Flags 0-31:   DAT_0031e770[0] (bits 0-31)
Flags 32-63:  DAT_0031e770[1] (bits 0-31)
Flags 64-95:  DAT_0031e770[2] (bits 0-31)
Flags 96-127: DAT_0031e770[3] (bits 0-31)
```

### Flag Access Algorithm

```c
// Determine array index and bit position for flag N
int array_index = flag_number >> 5;        // Divide by 32
int bit_position = flag_number & 0x1f;     // Modulo 32

// Check if flag is set
bool is_set = (DAT_0031e770[array_index] >> bit_position) & 1;

// Toggle flag
DAT_0031e770[array_index] ^= (1 << bit_position);
```

### Visual Output

When SCEN WORK flags are enabled and debug output is active, the system displays:

```
10:0(0)
11:0(0)
12:0(0)
...
29:0(0)
```

Format: `{flag_index}:0(0)` where the flag index corresponds to the enabled SCEN WORK flag.

## Mini-Map Debug System

### Function: `minimap_display_controller` (FUN_0022dd60)

The mini-map debug system provides visualization of game world coordinates and scene layout for development purposes.

**Operation Modes:**

- **Mode 0**: Initialize mini-map display system
- **Mode 1**: Update and render mini-map display

**Key Features:**

- Memory buffer management with 4-byte alignment
- Coordinate system processing
- Grid-based layout rendering
- Real-time position tracking

**Helper Functions:**

- `initialize_minimap_data_arrays` - Set up mini-map data structures
- `setup_minimap_grid_structure` - Configure grid layout
- `finalize_minimap_setup` - Complete initialization

## Position Display System

The position display system (POSITION_DISP option) provides real-time coordinate and positioning information during gameplay. Implementation details are still being analyzed.

## Memory Locations Summary

| System       | Variable       | Address      | Type      | Purpose                  |
| ------------ | -------------- | ------------ | --------- | ------------------------ |
| Debug Output | `DAT_003555dc` | `0x003555dc` | uint32    | Master debug output flag |
| Scene Work   | `DAT_0031e770` | `0x0031e770` | uint32[4] | Scene work flags array   |
| Scene Work   | `DAT_00355128` | `0x00355128` | uint32    | Current selected index   |

## Development Usage

### Accessing Debug Menu

The debug menu can be accessed through specific controller input combinations during gameplay (exact combination still being analyzed).

### Viewing Debug Output

1. Enable debug output flag at `0x003555dc` (set to 1)
2. Enable desired debug options through the debug menu
3. Debug information appears as on-screen overlay text

### PCSX2 Memory Debugging

For emulator-based analysis:

- Navigate to `0x0031e770` in memory view to see scene work flags
- Monitor `0x003555dc` for debug output state
- Each scene work flag corresponds to a specific bit in the 128-bit array

## Technical Implementation Notes

### PS2-Specific Considerations

- Memory alignment requirements (4-byte boundaries)
- Fixed-point coordinate systems
- DMA packet integration for graphics
- Real-time constraints for debug overlay rendering

### Flag System Architecture

The debug system uses efficient bitfield operations for flag management:

- Single bit per flag minimizes memory usage
- Bitwise operations for fast toggle/check operations
- Array-based storage allows for easy iteration and bulk operations

### Integration with Game Systems

Debug output integrates seamlessly with the game's rendering pipeline:

- Overlay rendering doesn't interfere with normal gameplay
- Conditional compilation allows debug code to be easily disabled
- Minimal performance impact when debug features are disabled
