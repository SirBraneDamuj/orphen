# SCR.BIN File Format Analysis

## Overview

SCR.BIN is the script archive file for Orphen: Scion of Sorcery on PlayStation 2. This document details the discovery and analysis of its DVD sector-based file format structure.

## Key Discovery: DVD Sector-Based Layout

### Initial Observations

The file was initially observed to contain "chunks that are separated by lots of 00 bytes, and then begin with a `20` byte" with approximately 220 such separators. This led to the creation of extraction scripts that revealed the true structure.

### DVD Sector Structure

**Critical Finding**: SCR.BIN uses a DVD sector-based layout optimized for PS2 disc loading.

- **DVD Sector Size**: 2048 bytes (standard DVD-ROM sector size)
- **Total File Size**: 1,290,240 bytes (exactly 630 sectors)
- **Perfect Alignment**: All 221 script chunks start exactly at sector boundaries (offset 0 within their sector)

### File Layout Pattern

```
Sector 0: [Chunk 0 - starts with 0xE3] + null padding
Sector 1: [0x20][Script 1 data] + null padding to sector boundary
Sector 6: [0x20][Script 2 data] + null padding to sector boundary
Sector 7: [0x20][Script 3 data] + null padding to sector boundary
...
Sector 629: [0x20][Script 220 data] + null padding
```

## Script Chunk Structure

### Chunk Headers

- **Chunk 0**: Starts with `0xE3` (special case - different data structure)
- **Chunks 1-220**: Start with `0x20` (script chunk header marker)

### Sector Alignment Analysis

| Metric                     | Value              |
| -------------------------- | ------------------ |
| Total Chunks               | 221 (chunks 0-220) |
| Sector-Aligned Chunks      | 221/221 (100%)     |
| Average Padding per Sector | ~18.7%             |
| Total Padding Removed      | 241,118 bytes      |

## Technical Implementation

### DVD Loading Optimization

The sector-based design provides several advantages for PS2 hardware:

1. **Efficient Reading**: Whole sector reads (2048 bytes) are much faster than arbitrary byte seeks
2. **Predictable Layout**: Scripts can be loaded by sector address without parsing delimiters
3. **Memory Alignment**: Sector boundaries align with PS2 memory management
4. **Cache Optimization**: DVD drive can prefetch complete sectors

### Chunk Size Distribution

Script sizes vary significantly:

- **Smallest Scripts**: 55 bytes (with significant sector padding)
- **Largest Scripts**: ~30,000 bytes (spanning multiple sectors)
- **Common Range**: 2,000-8,000 bytes per script

## File System Integration

### Loading Mechanism

Based on decompiled code analysis:

- **Archive Type**: 6 (SCR.BIN in the file system)
- **Lookup Function**: `FUN_00221c90` - Simple index-based lookup
- **Main Loader**: `FUN_00223268` - Primary file loading dispatcher
- **Access Pattern**: Scripts loaded by ID, not by parsing chunk delimiters at runtime

### Text Processing Connection

The `0x20` byte serves dual purposes:

1. **Script Chunk Header**: Marks the start of script data within a sector
2. **ASCII Space Character**: Used in text rendering functions like `FUN_00237de8`

## Unsectoring Process

### Creating SCR_UNSECTORED.BIN

To facilitate analysis, the sector-based format can be "unsectored" by:

1. **Identifying Chunks**: Find all sectors starting with script data (0x20 or 0xE3)
2. **Removing Padding**: Strip null bytes at the end of each chunk
3. **Concatenating**: Join all script data into a contiguous file

### Results

```
Original SCR.BIN:      1,290,240 bytes (with sector padding)
Unsectored Version:    1,049,122 bytes (pure script data)
Padding Removed:         241,118 bytes (18.7% reduction)
```

## Analysis Tools

### Scripts Created

1. **extract_scr_chunks.py**: Extracts individual script chunks (scr0.bin - scr220.bin)
2. **Unsectoring Script**: Creates contiguous SCR_UNSECTORED.BIN for analysis

### File Outputs

- **scr/**: Directory containing 221 individual script chunk files
- **SCR_UNSECTORED.BIN**: Contiguous script data without sector padding

## Implications for Reverse Engineering

### Format Understanding

The sector-based layout explains:

- Why chunk boundaries appear at "random" byte offsets
- The presence of extensive null byte padding
- The consistent 0x20 header pattern
- The relationship between file format and PS2 hardware optimization

### Analysis Strategy

For script analysis:

- **Use SCR_UNSECTORED.BIN** for bytecode analysis (no padding interference)
- **Use original SCR.BIN** for understanding loading behavior
- **Reference sector boundaries** when analyzing memory layout

## Related Documentation

- `disc_file_system_analysis.md` - Overall file system structure
- `map_bin_format.h` - Similar sector-based format for MAP.BIN
- `scripting_system_analysis.md` - Script execution and bytecode interpretation

## Conclusion

SCR.BIN represents a sophisticated file format designed specifically for PS2 DVD performance characteristics. The sector-based layout prioritizes loading efficiency over storage efficiency, reflecting the hardware constraints and optimization priorities of PlayStation 2 game development.

The discovery of this structure resolves the mystery of the "0x20 delimiters" and provides a foundation for deeper script analysis using both the original sector-aligned format and the derived unsectored version.
