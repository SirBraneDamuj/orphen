# Scene Data Analysis Tools

This directory contains tools for analyzing scene data from the Orphen PS2 disc image.

## Scripts

### Ghidra Tools (`ghidra/`)

- `export_funs.py` - Export decompiled functions from Ghidra
- `export_globals.py` - Export global variables metadata
- `export_strings.py` - Export string data and references

### Scene Analysis Tools

- `scene_data_parser.py` - Main scene data parser for BIN files
- `test_scene_parser.py` - Test script to analyze disc image files

## Scene Data Parser

The scene data parser is based on reverse engineering of the scene processing system, specifically:

- `process_scene_with_work_flags()` - Main scene processing function
- `scene_command_interpreter()` - Scene bytecode virtual machine
- Scene data structures identified through analysis

### Expected Scene Structure

Based on the reverse-engineered code:

```
Scene Header:
  +0x08: Main scene command sequence offset

Scene Objects Array:
  - Up to 62 scene objects
  - Each object: 4-byte pointer to command sequence
  - Object metadata stored 4 bytes before each object

Scene Work Data:
  - 128 entries × 4 bytes = 512 bytes
  - Indexed by scene work flags (0-127)

Scene Work Flags:
  - 4 × 32-bit values = 128 bits total
  - Controls which scene elements are active
```

### Scene Commands

The scene command interpreter processes bytecode with these formats:

- `0x00-0x0A`: Basic commands (jump table)
- `0xFF + byte`: Extended commands (0x100-0x1FF range)
- `0x32`: Subroutine call (followed by 4-byte offset)
- `0x04`: Return from subroutine
- `0x33+`: High commands (extended operations)

## Usage

### Analyze a single BIN file:

```bash
python scripts/scene_data_parser.py path/to/file.bin
```

### Analyze with JSON output:

```bash
python scripts/scene_data_parser.py path/to/MCB1.BIN -o mcb1_analysis.json
```

### Test on disc image files:

```bash
python scripts/test_scene_parser.py
```

This will automatically find and analyze:

- `MCB1.BIN` - Primary scene data (295 MB)
- `MAP.BIN` - Map/level data (17.7 MB)
- `SCR.BIN` - Script data (1.26 MB)
- `MCB0.BIN` - Secondary scene data (12 KB)
- `TEX.BIN` - Texture data (25.4 MB)

## Target Files

Based on disc image analysis, these files likely contain scene data:

| File     | Size    | Purpose                                                       |
| -------- | ------- | ------------------------------------------------------------- |
| MCB1.BIN | 295 MB  | **Primary scene archive** - Main scene data, models, bytecode |
| MAP.BIN  | 17.7 MB | **Map data** - Scene layout, collision, positioning           |
| SCR.BIN  | 1.26 MB | **Scripts** - Scene scripts, dialogue, events                 |
| MCB0.BIN | 12 KB   | **Scene headers** - Small scene metadata                      |
| TEX.BIN  | 25.4 MB | **Textures** - Referenced by scene objects                    |

## Output

The parser generates:

- Console output showing discovered scene structures
- JSON analysis files with detailed breakdowns
- Statistics on command usage and scene composition

Example output shows:

- Potential scene headers and their locations
- Scene object arrays with valid command sequences
- Command statistics and bytecode analysis
- Cross-references between scene elements

## Integration with Reverse Engineering

This tool complements the decompiled code analysis:

- Validates scene structure assumptions from code analysis
- Provides real scene data to test against interpreted functions
- Helps identify scene file formats and encoding
- Enables extraction of individual scene elements for study
