# Reverse Engineering Progress

## Overview

This document tracks the progress of reverse engineering Orphen: Scion of Sorcery's debug systems and provides insights into the analysis methodology.

## Analysis Methodology

### Function Analysis Workflow

1. **Identification**: Start with cryptic `FUN_*` function names from Ghidra decompilation
2. **Context Gathering**: Use globals.json and cross-references to understand function purpose
3. **Code Analysis**: Examine assembly patterns and variable usage
4. **Meaningful Naming**: Convert to descriptive function names based on actual functionality
5. **Documentation**: Add comprehensive comments explaining PS2-specific implementation
6. **Validation**: Test findings through PCSX2 emulator when possible

### Key Principles

- Maintain original `FUN_*` names in comments for reference
- Use evidence-based analysis rather than speculation
- Focus on PS2-specific patterns and constraints
- Validate findings through emulator testing when possible

## Debug System Analysis Progress

### Completed Functions

| Original Name | Analyzed Name                       | Purpose                            | Status      |
| ------------- | ----------------------------------- | ---------------------------------- | ----------- |
| FUN_00268d30  | `debug_menu_handler`                | Main debug menu controller         | ✅ Complete |
| FUN_00268c98  | `set_debug_option_text`             | Sets ON/OFF text for debug options | ✅ Complete |
| FUN_002686a0  | `clear_controller_input_state`      | Clears controller input buffer     | ✅ Complete |
| FUN_002686c8  | `process_menu_input_and_navigation` | Handles menu navigation input      | ✅ Complete |
| FUN_0026a508  | `scene_work_display_debug_menu`     | SCEN WORK DISP submenu handler     | ✅ Complete |
| FUN_0025b778  | `process_scene_with_work_flags`     | Scene processing with debug flags  | ✅ Complete |
| FUN_002681c0  | `debug_output_formatter`            | Debug output formatting system     | ✅ Complete |
| FUN_00268558  | `strcpy_simple`                     | String copy utility function       | ✅ Complete |
| FUN_0022dd60  | `minimap_display_controller`        | Mini-map debug system controller   | ✅ Complete |
| FUN_0022de88  | `initialize_minimap_data_arrays`    | Mini-map data initialization       | ✅ Complete |
| FUN_0022def0  | `setup_minimap_grid_structure`      | Mini-map grid setup                | ✅ Complete |
| FUN_0022dfb0  | `finalize_minimap_setup`            | Mini-map setup completion          | ✅ Complete |

### Functions Still Using FUN\_ Names

#### Mini-Map System

- `FUN_0022e7b0` - process_minimap_data (referenced in minimap_display_controller)
- `FUN_0022e638` - update_minimap_display
- `FUN_0022e7b8` - render_minimap_elements
- `FUN_0022e528` - minimap_post_processing
- `FUN_0020bc78` - copy_minimap_buffer

#### Scene Work Display System

- `FUN_002685e8` - calculate_text_width
- `FUN_00268498` - render_menu_text
- `FUN_0030c1d8` - sprintf_formatted
- `FUN_00268500` - copy_string
- `FUN_00268650` - render_menu_rectangle
- `FUN_0023b9f8` - check_controller_input

#### Debug Output System

- `FUN_0030e0f8` - format_string_processor (used by debug_output_formatter)

## Key Discoveries

### Debug Menu Architecture

- **Hierarchical Structure**: Main menu with submenus for complex options
- **State Management**: Uses global flags to track menu state and selections
- **Input Handling**: Sophisticated controller input processing with state clearing
- **Visual Feedback**: Dynamic ON/OFF text display for option states

### Scene Work Flag System

- **128-Bit Flag Array**: Stored as 4 32-bit integers at `0x0031e770`
- **Bitwise Operations**: Efficient flag toggle using XOR operations
- **Real-Time Updates**: Immediate effect on scene processing when flags change
- **Developer Tool**: Clearly designed for debugging scene element visibility/processing

### Debug Output System

- **Conditional Display**: Master flag at `0x003555dc` controls all debug output
- **Format String Support**: sprintf-like functionality for formatted debug messages
- **On-Screen Overlay**: Renders directly to game display without interfering with gameplay
- **Performance Conscious**: Minimal overhead when debug features disabled

### Mini-Map Debug System

- **Memory Management**: Careful buffer alignment and memory pointer tracking
- **Coordinate Processing**: Real-time coordinate transformation and display
- **Modular Design**: Separate initialization, update, and rendering phases
- **Development Aid**: Provides spatial awareness for level design and debugging

## PCSX2 Validation Results

### SCEN WORK DISP Testing

**Test Environment**: PCSX2 emulator with memory debugging
**Procedure**:

1. Enabled debug output flag (`DAT_003555dc` = 1)
2. Accessed SCEN WORK DISP submenu
3. Enabled flags 10-29 using Circle button
4. Observed real-time on-screen debug output

**Results**:

- Confirmed on-screen overlay display functionality
- Verified flag array storage at `0x0031e770`
- Observed format: `{flag_index}:0(0)` for enabled flags
- Confirmed immediate visual feedback when toggling flags

**Implications**:

- Debug system fully functional in retail builds
- Real-time scene debugging was available to developers
- Flag system affects actual scene processing, not just display

## PS2-Specific Implementation Details

### Memory Alignment

- **4-Byte Boundaries**: Critical for PS2 DMA operations
- **Buffer Management**: Careful pointer arithmetic to maintain alignment
- **Performance Impact**: Misaligned access causes significant slowdowns

### Graphics Integration

- **DMA Packets**: Debug overlays integrated with GPU command buffers
- **Fixed-Point Math**: Coordinate scaling using 4096.0 multiplier
- **Overlay Rendering**: Non-intrusive debug display over normal graphics

### Controller Input

- **Dual Controller Support**: Debug menu accessible on both controllers
- **Input History**: 64-entry buffer for complex input sequences
- **Analog Processing**: Handles both digital and analog stick input

## Future Analysis Targets

### High Priority

1. **Position Display System**: Analyze POSITION_DISP functionality
2. **Debug Menu Access**: Determine how to access debug menu in-game
3. **Scene Processing Core**: Understand how scene work flags affect rendering
4. **Subproc ID Correlation**: Map extracted 0x04 <ID16> records to interpreter call graph

### Medium Priority

1. **Mini-Map Rendering Pipeline**: Analyze remaining mini-map functions
2. **Text Rendering System**: Analyze font/text display functions
3. **Audio Debug Features**: Investigate audio debugging capabilities
4. **Pattern A/B Semantics**: Determine runtime meaning of structural patterns in script files

### Low Priority

1. **Graphics Primitive Functions**: Analyze low-level rendering utilities
2. **Memory Management**: Understand PS2 memory allocation patterns
3. **Game State Management**: Analyze save/load system integration
4. **Subproc Table Visualization**: Build tool to emit annotated subproc ID list with context snippets

## Technical Notes

### Ghidra Decompilation Patterns

- **Function Naming**: `FUN_` prefix with 8-digit hex address
- **Variable Naming**: `DAT_` prefix for global variables, type-based prefixes for locals
- **Cross-References**: Available through Ghidra's reference tracking
- **Assembly Context**: Original assembly helpful for understanding PS2-specific operations

### Analysis Tools

- **Ghidra**: Primary decompilation and analysis tool
- **PCSX2**: Emulator with debugging capabilities for validation
- **globals.json**: Generated metadata for variable cross-referencing
- **Git**: Version control for tracking analysis progress

### Code Quality Patterns

- **Consistent Style**: Game code follows consistent naming and structure patterns
- **Performance Focus**: Code optimized for PS2 hardware constraints
- **Debug Integration**: Debug features cleanly integrated without affecting release performance
- **Modular Design**: Clear separation between systems and subsystems
