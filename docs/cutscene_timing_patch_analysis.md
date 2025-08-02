# Cutscene Timing Patch Analysis

## Summary

Successfully analyzed SCR.BIN and identified timing patterns for cutscene acceleration. Created automated patch script to reduce cutscene delays.

## Findings

### SCR.BIN Structure

- **Total scripts**: 227 files
- **Size**: Varies from 1 byte to 29 KB per script
- **Format**: 4-byte lookup table entries with offset/size data

### Timing Pattern Analysis

The scripts use consistent timing values that appear to be frame counts at 60 FPS:

| Original (frames) | Original (seconds) | Occurrences | Patch Target | Acceleration |
| ----------------- | ------------------ | ----------- | ------------ | ------------ |
| 300               | 5.0s               | 539         | 30 frames    | 10x faster   |
| 360               | 6.0s               | 1,238       | 30 frames    | 12x faster   |
| 480               | 8.0s               | 208         | 60 frames    | 8x faster    |
| 600               | 10.0s              | 299         | 60 frames    | 10x faster   |
| 720               | 12.0s              | 105         | 60 frames    | 12x faster   |
| 900               | 15.0s              | 258         | 90 frames    | 10x faster   |
| 1200              | 20.0s              | 81          | 120 frames   | 10x faster   |
| 1800              | 30.0s              | 236         | 180 frames   | 10x faster   |

### Timing Instruction Opcodes

Identified potential timing opcodes with extended 0xFF prefix:

- **0xFF25**: 36 timing instructions (common)
- **0xFF29**: 91 timing instructions (very common)
- **0xFFA0**: 87 timing instructions (very common)
- **0xFFE0**: 41 timing instructions (common)
- **0xFFFF**: 111 timing instructions (most common)

## Patch Implementation

### Generated Patch Script

Created `patch_scr_timing.py` that:

1. Creates backup of original SCR.BIN
2. Applies binary replacements for timing values
3. Reduces cutscene delays by 8-12x factor

### Byte-level Patches

- **300 frames** (0x2C01) → **30 frames** (0x1E00)
- **360 frames** (0x6801) → **30 frames** (0x1E00)
- **480 frames** (0xE001) → **60 frames** (0x3C00)
- **600 frames** (0x5802) → **60 frames** (0x3C00)
- **720 frames** (0xD002) → **60 frames** (0x3C00)
- **900 frames** (0x8403) → **90 frames** (0x5A00)
- **1200 frames** (0xB004) → **120 frames** (0x7800)
- **1800 frames** (0x0807) → **180 frames** (0xB400)

## Usage

To apply the cutscene acceleration patch:

```bash
# Place SCR.BIN in same directory as patch script
python patch_scr_timing.py
```

This creates `SCR.BIN.backup` and modifies SCR.BIN with faster timing values.

## Technical Notes

- All values stored in little-endian format
- 16-bit timing values for delays under 65536 frames
- Conservative acceleration maintains visual pacing while reducing wait times
- Total affected timing points: **2,964 locations**

## Related Analysis

- See `disc_file_system_analysis.md` for SCR.BIN loading functions
- File system functions: `FUN_00223268` (main loader), `FUN_00221c90` (SCR lookup)
- Script files numbered 0-226, with variable sizes and complexity
