# Orphen Disc File System Analysis

## Overview

Based on reverse engineering analysis, Orphen: Scion of Sorcery uses a multi-archive file system with different file types stored in separate `.BIN` archive files. The game loads content from these archives using a sophisticated file system with lookup tables and error handling.

## File System Architecture

### Archive Types Discovered

The game uses several archive types, each with a specific file extension:

1. **GRP.BIN** (param_1 = 0) - Graphics/textures archive
2. **Unknown** (param_1 = 1) - Unknown archive type
3. **MAP.BIN** (param_1 = 2) - Map data archive ⭐ **KEY FOR MAP ANALYSIS**
4. **Unknown** (param_1 =3) - Unknown archive type
5. **ITM.BIN** (param_1 = 4) - Item data archive
6. **SCR.BIN** (param_1 = 6) - Script archive

### Key Functions

#### Main File Loading Function: `FUN_00223268`

- **Address**: `0x00223268`
- **Purpose**: Primary file loading dispatcher
- **Parameters**:
  - `param_1`: Archive type (0-6)
  - `param_2`: File ID within archive
  - `param_3`: Destination buffer
- **Return**: Number of bytes loaded, or negative error code

#### Map Loading Function: `FUN_0022b540`

- **Address**: `0x0022b540`
- **Purpose**: Map-specific loading wrapper
- **Error Strings**:
  - `"CAN'T_MAPPREV"` (0x0034c168) - Map preview error
  - `"MAP_LOAD_ERR"` (0x0034c178) - Map loading error

### File Type Lookup System

The file loading system uses lookup tables for each archive type:

- **GRP.BIN**: `FUN_00221b18(param_2)` - Graphics file lookup
- **MAP.BIN**: `FUN_00221b48(param_2)` - Map file lookup ⭐
- **ITM.BIN**: `FUN_00221b78(param_2)` - Item file lookup
- **SCR.BIN**: `FUN_00221c90(param_2)` - Script file lookup

## Map System Findings

### Map Naming Convention

From debug menu analysis, maps follow the pattern: **`MP####`** where #### is a 4-digit number.

Examples found in strings:

- `mp0000` - Referenced in memory allocation error
- `mp0432` - Referenced in error string
- `mp0721` - Referenced in buffer overflow error
- `mp1241` - Referenced in buffer overflow error

### Map Debug Output

The format string `"MAP>(MP%02d%02d)\n"` (0x0034bea8) suggests maps are displayed as:

- `MAP>(MP0111)`
- `MAP>(MP0112)`
- etc.

This indicates maps are organized in groups, possibly:

- First 2 digits: Area/region number
- Last 2 digits: Map number within area

### Map Loading Process

1. **Map ID Validation**: Check if map ID exists in lookup table
2. **Archive Access**: Call `FUN_00223268` with param_1=2 (MAP.BIN)
3. **File Retrieval**: Load map data from MAP.BIN archive
4. **Error Handling**: Display appropriate error messages on failure

## Next Steps for Cutscene Script Analysis

### 1. Analyze SCR.BIN Archive Structure

**Priority: HIGH - CUTSCENE SCRIPTS**

- Examine `FUN_00221c90` script lookup function
- Find the script lookup table for SCR.BIN
- Understand script ID to file offset mapping
- **Target**: Cutscene timing and skip functionality

### 2. Extract MAP.BIN from Disc

**Tools Needed:**

- PS2 disc extraction tool (e.g., IsoBuster, CDMage)
- Custom tool to parse MAP.BIN archive format

**Process:**

1. Mount Orphen disc image
2. Locate MAP.BIN file
3. Extract and analyze header structure
4. Build map extraction tool

### 3. Reverse Engineer Map File Format

**Key Areas:**

- Map geometry data
- Collision information
- Texture references
- Entity/object placement
- Script trigger areas

### 4. Find Script Loading for Cutscenes

**Related Functions:**

- `FUN_00221c90` - Script file lookup in SCR.BIN
- Functions referencing "load_script" error strings
- Script timing/delay functions for cutscene skipping

## Error Strings Reference

| Address    | String                     | Context                 |
| ---------- | -------------------------- | ----------------------- |
| 0x0034c168 | "CAN'T_MAPPREV"            | Map preview failure     |
| 0x0034c178 | "MAP_LOAD_ERR"             | Map loading failure     |
| 0x0034bc88 | "error load_file"          | General file load error |
| 0x0034bd18 | "File Read Error : %s(%X)" | File read failure       |

## File System Constants

| Archive | Type ID | Lookup Function | Description              |
| ------- | ------- | --------------- | ------------------------ |
| GRP.BIN | 0       | FUN_00221b18    | Graphics/Textures        |
| ???     | 1       | FUN_00221b30    | Unknown                  |
| MAP.BIN | 2       | FUN_00221b48    | **Map Data**             |
| ???     | 3       | FUN_00221b60    | Unknown                  |
| ITM.BIN | 4       | FUN_00221b78    | Item Data                |
| ???     | 5       | ???             | Invalid (triggers error) |
| SCR.BIN | 6       | FUN_00221c90    | Scripts                  |

---

_This analysis provides the foundation for extracting and analyzing map files from the Orphen disc image._
