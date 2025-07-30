# Orphen: Scion of Sorcery - Reverse Engineering Project

This project contains reverse-engineered analysis of the PS2 game "Orphen: Scion of Sorcery" using Ghidra decompilation.

## Project Structure

- `analyzed/` - Analyzed and documented functions with meaningful names
- `src/` - Raw decompiled functions exported from Ghidra (2922 functions, not tracked in git)
- `scripts/` - Ghidra automation scripts
- `.github/instructions/` - Detailed project instructions and context

## Quick Start

### Exporting Functions from Ghidra

1. Open your Orphen binary in Ghidra
2. Go to **Window → Script Manager**
3. Load and run `scripts/export_funs.py` (Jython script)
4. All `FUN_*` functions will be exported to `src/` directory

### Analysis Workflow

1. **Identify** a function to analyze from `src/`
2. **Analyze** the function to understand its purpose
3. **Create** a properly documented version in `analyzed/` with:
   - Meaningful function name
   - Parameter documentation
   - Code comments explaining PS2-specific details
   - Cross-references to related functions
4. **Update** existing analyzed code to use the new function name
5. **Commit** only the analyzed code (raw `src/` files are ignored)

## Key Technical Context

- **PS2 MIPS Architecture**: Custom implementations optimized for PS2 hardware
- **Flag System**: ~18,424 flags controlling game behavior via bit manipulation
- **Graphics Pipeline**: DMA packets and GPU command buffers for rendering
- **Controller System**: Dual controller support with 64-entry input history
- **Fixed-Point Math**: 4096.0 scaling for coordinate transformation

## Functions Analyzed

See the `analyzed/` directory for completed function analysis. Each file contains:

- Original `FUN_*` address in comments
- Detailed technical documentation
- Cross-references to related functions
- PS2-specific implementation notes

## Contributing

When analyzing functions:

1. Keep original `FUN_*` names in comments for reference
2. Use descriptive variable names based on functionality
3. Document PS2-specific patterns and hardware interactions
4. Maintain function call hierarchies and dependencies
