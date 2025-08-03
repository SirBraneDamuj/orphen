# Orphen Disc File System Analysis

## Overview

Based on reverse engineering analysis, Orphen: Scion of Sorcery uses a multi-archive file system with different file types stored in separate `.BIN` archive files. The game loads content from these archives using a sophisticated file system with lookup tables and error handling.

### Key Functions

#### Main File Loading Function: `FUN_00223268`

- **Address**: `0x00223268`
- **Purpose**: Primary file loading dispatcher
- **Parameters**:
  - `param_1`: Archive type (0-6)
  - `param_2`: File ID within archive
  - `param_3`: Destination buffer
- **Return**: Number of bytes loaded, or negative error code

### File Type Lookup System

The file loading system uses lookup tables for each archive type:

- **GRP.BIN**: `FUN_00221b18(param_2)` - Graphics file lookup
- **MAP.BIN**: `FUN_00221b48(param_2)` - Map file lookup ⭐
- **ITM.BIN**: `FUN_00221b78(param_2)` - Item file lookup
- **SCR.BIN**: `FUN_00221c90(param_2)` - Script file lookup

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
