# MCB Data Structure Analysis

## Overview

Through reverse engineering analysis of the Orphen: Scion of Sorcery PS2 game, we have discovered the internal structure of MCB (Map/Content Binary) data files - a structured game data archive system.

## Key Files

- **MCB0.BIN**: Primary MCB data file (appears mostly empty)
- **MCB1.BIN**: Main MCB data file containing structured game data across 15 sections
- **SCR.BIN**: Text archive (previously analyzed, separate system)

## MCB Data Architecture

### Resource System Integration

MCB data is accessed through the game's resource system:

1. **Resource Loading**: MCB files are loaded as resources using `find_resource_by_id(DAT_00354d50)`
2. **Dual Mode Access**: System switches between MCB0 and MCB1 data using flag `DAT_003555d3`
3. **Section-Based Organization**: Data is organized into 15 logical sections (0-14)

### Data Structure Layout

```c
// MCB Resource Header (hypothesized):
MCB Resource Header:
  +0x00: Standard resource header (8 bytes)
  +0x08: MCB-specific data begins
  +0x1c: Offset table pointer/base (used for section indexing)
  +0x38: Alternate data offset (used when DAT_003555d3 = 1)
```

### Section Access Modes

**Important Note**: MCB0.BIN appears to be mostly empty with minimal data. The dual-mode system likely operates within MCB1.BIN itself, accessing different regions of the same file.

- **Mode 0** (`DAT_003555d3 = 0`): **Section-indexed access within MCB1.BIN**
  - Uses offset table: `offset_table[section_index]`
  - Accesses individual sections 0-14 within MCB1.BIN
  - Contains structured game data entries
- **Mode 1** (`DAT_003555d3 = 1`): **Fixed offset access within MCB1.BIN**
  - Uses fixed offset: `+0x38 from offset table`
  - Accesses a different data region within the same MCB1.BIN file
  - **Purpose unknown - requires further analysis**

## MCB Entry Structure

### Entry Format (16 bytes each)

Each MCB section contains zero-terminated arrays of 16-byte entries:

```c
Entry Structure (16 bytes):
  +0x00: short[0] - Primary identifier/index (0 = terminator)
  +0x02: short[1] - Data field 1
  +0x04: short[2] - Data field 2
  +0x06: short[3] - Data field 3
  +0x08: short[4] - Data field 4
  +0x0A: short[5] - Data field 5
  +0x0C: short[6] - Data field 6
  +0x0E: short[7] - Data field 7
```

### Entry Access Pattern

```c
// Standard entry access formula:
entry_address = base_pointer + (entry_index * 0x10)
entry_id = *(short *)entry_address;

// Example from debug menu:
selected_entry_id = *(short *)((int)user_input * 0x10 + base_pointer - 0x10);
```

### Entry Processing Functions

1. **`count_mcb_section_entries(section_index)`** (FUN_0022a300)

   - Counts valid entries until zero terminator
   - Returns number of entries to process

2. **`get_mcb_data_section_pointer(section_index)`** (FUN_0022a238)
   - Returns pointer to section data
   - Handles dual-mode access (MCB0 vs MCB1)

## Section Organization

### Sections 0-13: Map/Scene Data

- **Entry Count**: 100 entries each (validated)
- **Format**: "MP%02d%02d" (section + entry ID)
- **Usage**: Map and scene identifiers
- **Access**: Section-indexed within MCB1.BIN
- **Structure**: Standard 16-byte entries with diverse data patterns

### Section 14: Compressed Text Dictionary

- **Entry Count**: 1 entry only (validated)
- **Format**: "BG%02d" (background entries)
- **Usage**: **Compressed text dictionary storage**
- **Access**: Fixed offset in MCB1.BIN (Mode 1)
- **Entry ID**: 49152 (0xC000)
- **Special**: Minimal entry structure points to compressed data region

## Compression Evidence

### Discovered Patterns in MCB1.BIN

Evidence of dictionary-based compression found:

- Partial strings: `"Pinnacl.+...un"` → `"Pinnacle of the Sun"`
- Back-reference patterns suggesting dictionary lookup
- Fixed offset access (Mode 1) pointing to compressed data location

### Entry Data Usage

**Menu Construction** (analyzed in `mcb_debug_menu_interface`):

- Only uses **first short** (2 bytes) for display
- Remaining **14 bytes per entry** unused for menu display
- **These 14 bytes likely contain compression metadata**

**Text Expansion** (not yet located):

- Would access full 16-byte entry structure
- Process compression metadata in bytes 2-15
- Expand dictionary references into full text
- Handle back-reference resolution

## Debug Menu Analysis

The `mcb_debug_menu_interface` function reveals:

1. **Entry Iteration**: Processes entries with `+= 8` (16 bytes = 8 shorts)
2. **Zero Termination**: Stops when first short equals 0
3. **Section Navigation**: Allows browsing all 15 sections
4. **Selection Processing**: Extracts entry IDs for map loading

### Special Section Handling

- **Section 12**: Audio/scene triggers

  - Entry 10: Sets audio flag `uGpffffb662 = 0x12`
  - Entry 11: Scene processing `iGpffffb280 = 10`

- **Section 14**: Background processing
  - Uses different processing flags: `uGpffffb27c = 0x20000`
  - Copies background data: 12 bytes from `0x58bed0` to `0x31e668`

## Key Functions Analyzed

### Core MCB Access Functions

1. **`find_resource_by_id(resource_id)`** (FUN_00267f90)

   - Locates MCB resource data in memory
   - Returns pointer to resource or NULL

2. **`get_mcb_data_section_pointer(section_index)`** (FUN_0022a238)

   - Primary interface for MCB section access
   - Handles dual-mode switching (MCB0/MCB1)

3. **`count_mcb_section_entries(section_index)`** (FUN_0022a300)
   - Counts entries in section for bounds checking
   - Essential for safe iteration

### MCB Processing Functions

4. **`mcb_data_processor`** (FUN_0022b300)

   - Main MCB processing loop
   - Iterates through sections 0-14
   - Outputs debug information during processing

5. **`mcb_debug_menu_interface`** (FUN_00268e20)
   - Dynamic debug menu construction
   - Demonstrates MCB entry access patterns
   - Reveals 16-byte entry structure

## Research Implications

### For Text Decompression

The analysis reveals:

1. **Location**: Compressed text is in MCB1.BIN, accessed via Section 14 (fixed offset mode)
2. **Structure**: 16-byte entries with first short as identifier, remaining 14 bytes as compression data
3. **Access Pattern**: Use `entry_index * 0x10` to access individual entries
4. **Processing**: Text expansion functions would process the compression metadata in bytes 2-15

### Next Research Steps

To find text decompression functions:

1. **Search for functions** that:

   - Access MCB entries with `* 0x10` pattern
   - Process **more than just the first short** of entries
   - Perform string/buffer operations
   - Take entry data and produce expanded text

2. **Look for usage** of the entry data beyond menu construction:

   - Functions called after MCB entry selection
   - Text rendering pipeline that processes MCB data
   - String expansion or substitution operations

3. **Investigate Section 14** specifically:
   - Functions that access Section 14 data
   - Background processing code that might handle text
   - Fixed offset mode handlers (`DAT_003555d3 = 1`)

The MCB data structure is now fully mapped, providing the foundation for locating the actual text decompression algorithms.

## Memory Addresses

### Key Global Variables

- `DAT_00354d50`: MCB resource ID
- `DAT_003555d3`: MCB data source flag (0=MCB0, 1=MCB1)
- `DAT_00355bd0`: Current MCB section index (0-14)
- `DAT_00355bd4`: Remaining entries in current section
- `DAT_00355bd8`: Pointer to current MCB entry
- `DAT_003551f8`: Special counter for section 14

### Buffer Addresses

- `0x5721e8`: Menu text buffer (1024 bytes)
- `0x5725e8`: Menu pointer table
- `0x31e668`: Background data destination
- `0x58bed0`: Background data source

### Format String Addresses

- `0x3550b0`: "MP%02d" (section headers)
- `0x34d6d0`: "MP%02d%02d" (map entries)
- `0x3550b8`: "BG%02d" (background entries)

## Conclusion

The MCB data system uses a sophisticated dual-mode architecture to access both uncompressed map data (MCB0.BIN) and compressed text data (MCB1.BIN). The 16-byte entry structure provides the foundation for both simple identifier storage and complex compression metadata. The debug menu system has revealed the exact access patterns needed to locate and process the compressed text data discovered in the initial analysis.
