# Orphen: Scion of Sorcery - Reverse Engineering Documentation

This directory contains comprehensive documentation for the reverse engineering efforts on Orphen: Scion of Sorcery's debug systems and game code.

## Documentation Overview

### 📋 [Debug System](debug_system.md)

Complete documentation of the game's debug menu and logging systems:

- Debug menu structure and navigation
- Scene Work Display system (128 toggleable flags)
- Mini-map debug visualization
- Debug output formatting and display
- Memory locations and flag storage

### 🔍 [Reverse Engineering Progress](reverse_engineering_progress.md)

Tracking document for analysis methodology and progress:

- Function analysis workflow
- Completed vs. pending function analysis
- Key discoveries and insights
- PCSX2 validation results
- Future analysis targets

### 🗺️ [Memory Map](memory_map.md)

Reference guide for important memory addresses and data structures:

- Global variable locations and purposes
- Debug system memory layout
- Function pointer tables and callbacks

### 📄 [Disc File System Analysis](disc_file_system_analysis.md)

Documentation of the game's file formats and disc structure:

- BIN archive formats and extraction methods
- File type identification and purposes
- Asset organization and loading systems

### ⚔️ [Battle Mode Activation](battle_mode_activation.md)

- Debug system memory locations
- Controller input bit masks
- Scene work flag bit mapping
- Buffer and data structure addresses
- PCSX2 debugging tips

## Quick Reference

### Key Memory Addresses

- **Debug Output Control**: `0x003555dc` (Master enable flag)
- **Scene Work Flags**: `0x0031e770` (128-bit flag array)
- **Current Selection**: `0x00355128` (Selected flag index)

### Analysis Status

- **Debug Menu System**: ✅ Complete
- **Scene Work Display**: ✅ Complete
- **Mini-Map System**: ✅ Core functions analyzed
- **Position Display**: ⏳ Pending analysis

### Validation Results

- SCEN WORK DISP confirmed functional via PCSX2 testing
- Real-time debug overlay displays enabled flags as `{index}:0(0)`
- Flag toggles have immediate effect on scene processing

## Development Workflow

### Analyzing New Functions

1. Identify function from `src/` directory
2. Gather context using `globals.json` cross-references
3. Analyze code patterns and variable usage
4. Create meaningful function name and documentation
5. Update callsites in `analyzed/` directory
6. Validate through emulator testing when possible

### Documentation Standards

- Maintain original `FUN_*` names in comments
- Use evidence-based analysis over speculation
- Document PS2-specific implementation details
- Include memory addresses and data structure layouts
- Provide PCSX2 debugging guidance

## Tools and Resources

### Analysis Tools

- **Ghidra**: Primary decompilation and reverse engineering
- **PCSX2**: PlayStation 2 emulator with debugging capabilities
- **globals.json**: Generated metadata for variable cross-referencing
- **Git**: Version control for tracking analysis progress

### Generated Data

- `src/`: Raw decompiled functions (gitignored)
- `globals.json`: Global variable metadata (gitignored)
- `analyzed/`: Clean, documented function implementations

### Search Techniques

Use terminal commands to search gitignored files:

```bash
grep -r "pattern" src/        # Search source files
grep "pattern" globals.json   # Search global metadata
```

## Contributing

When adding new documentation:

1. Follow the established format and structure
2. Include cross-references between related systems
3. Provide memory addresses and technical details
4. Update this README with new file descriptions
5. Validate information through code analysis or testing

## Technical Context

This reverse engineering effort focuses on understanding the sophisticated debug systems built into Orphen: Scion of Sorcery. The game contains extensive debugging functionality that was used during development, including:

- Real-time scene element visualization and control
- Position and coordinate debugging displays
- Mini-map development tools
- Comprehensive logging and output systems
- Developer-friendly menu interfaces

The analysis reveals high-quality, well-structured code optimized for PlayStation 2 hardware constraints while maintaining clean separation between debug and release functionality.
