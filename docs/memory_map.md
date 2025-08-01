# Memory Map and Important Addresses

## Debug System Memory Locations

### Primary Debug Control

| Address      | Variable       | Type   | Size    | Purpose                                |
| ------------ | -------------- | ------ | ------- | -------------------------------------- |
| `0x003555dc` | `DAT_003555dc` | uint32 | 4 bytes | Master debug output enable flag        |
| `0x003555f4` | `DAT_003555f4` | uint32 | 4 bytes | Controller input state (current frame) |
| `0x003555f6` | `DAT_003555f6` | uint32 | 4 bytes | Controller input state (extended)      |

### Scene Work Debug System

| Address      | Variable          | Type   | Size    | Purpose                                   |
| ------------ | ----------------- | ------ | ------- | ----------------------------------------- |
| `0x0031e770` | `DAT_0031e770[0]` | uint32 | 4 bytes | Scene work flags 0-31                     |
| `0x0031e774` | `DAT_0031e770[1]` | uint32 | 4 bytes | Scene work flags 32-63                    |
| `0x0031e778` | `DAT_0031e770[2]` | uint32 | 4 bytes | Scene work flags 64-95                    |
| `0x0031e77c` | `DAT_0031e770[3]` | uint32 | 4 bytes | Scene work flags 96-127                   |
| `0x00355128` | `DAT_00355128`    | uint32 | 4 bytes | Current selected scene work index (0-127) |

### Mini-Map Debug System

| Address      | Variable       | Type  | Size    | Purpose                         |
| ------------ | -------------- | ----- | ------- | ------------------------------- |
| `0x0031c210` | `DAT_0031c210` | int32 | 4 bytes | Mini-map display parameter      |
| `0x0031c214` | `DAT_0031c214` | int32 | 4 bytes | Mini-map display parameter      |
| `0x0031c218` | `DAT_0031c218` | int32 | 4 bytes | Mini-map display parameter      |
| `0x0031c21c` | `DAT_0031c21c` | float | 4 bytes | Mini-map coordinate/scale value |

### String Resources (Debug Menu Text)

| Address    | Content             | Purpose                         |
| ---------- | ------------------- | ------------------------------- |
| `0x34d610` | "SCEN WORK DISP"    | Scene work submenu title        |
| `0x355130` | Format string       | Scene work index display format |
| `0x355138` | "ON"                | Flag enabled text               |
| `0x355140` | "OFF"               | Flag disabled text              |
| `0x34c1c8` | Debug format string | Mini-map debug message format   |

## Controller Input Bit Masks

### DAT_003555f4 (Primary Input)

| Bit | Hex Mask | Purpose         |
| --- | -------- | --------------- |
| 2   | `0x0004` | Triangle button |
| 3   | `0x0008` | X button        |
| 12  | `0x1000` | Up D-pad        |
| 14  | `0x4000` | Down D-pad      |

### DAT_003555f6 (Extended Input)

| Bit | Hex Mask | Purpose       |
| --- | -------- | ------------- |
| 5   | `0x0020` | Circle button |
| 8   | `0x0100` | Start button  |

## Memory Layout Patterns

### PS2 Memory Regions

```
0x00000000 - 0x01FFFFFF : Main RAM (32MB)
0x30000000 - 0x31FFFFFF : Cached main memory access
0x70000000 - 0x71FFFFFF : Uncached main memory access
```

### Orphen-Specific Memory Usage

```
0x003xxxxx : Global variables and game state
0x0031xxxx : Graphics and display parameters
0x0035xxxx : Input state and debug variables
```

## Buffer and Data Structure Locations

### Mini-Map Buffers

| Address      | Variable           | Purpose                        |
| ------------ | ------------------ | ------------------------------ |
| `0x01849a00` | Mini-map data base | Static mini-map data structure |
| `0x58bd40`   | Source buffer      | Mini-map render source         |
| `0x58bc80`   | Destination buffer | Mini-map display destination   |

### Debug Output Areas

- Debug text overlay buffers (addresses dynamically allocated)
- Format string storage at fixed addresses
- Temporary string buffers for formatted output

## Scene Work Flag Bit Mapping

### Array Structure

```c
// 128 flags stored across 4 32-bit integers
uint32 scene_work_flags[4];

// Flag N is located at:
// Array index: N >> 5 (N / 32)
// Bit position: N & 0x1f (N % 32)

// Examples:
// Flag 0:   scene_work_flags[0], bit 0
// Flag 10:  scene_work_flags[0], bit 10
// Flag 32:  scene_work_flags[1], bit 0
// Flag 127: scene_work_flags[3], bit 31
```

### Validated Active Flags

Based on PCSX2 testing, flags 10-29 are confirmed functional and display as:

```
10:0(0) - Flag index 10, array[0] bit 10
11:0(0) - Flag index 11, array[0] bit 11
...
29:0(0) - Flag index 29, array[0] bit 29
```

## PCSX2 Debugging Tips

### Memory Viewing

1. **Scene Work Flags**: Navigate to `0x0031e770` in memory view
2. **Debug Output Flag**: Check `0x003555dc` (should be 1 when enabled)
3. **Current Selection**: Monitor `0x00355128` for selected flag index

### Breakpoint Locations

- `debug_output_formatter` entry point for debug message capture
- Scene work flag toggle operations for real-time monitoring
- Mini-map update cycles for coordinate tracking

### Value Interpretation

- **Flag Arrays**: View as 32-bit hex to see bit patterns
- **Coordinates**: Fixed-point values may need division by 4096.0
- **State Flags**: Often use single bits within larger integers

## Development Context

### Memory Alignment Requirements

- **4-byte alignment**: Critical for PS2 DMA operations
- **Buffer boundaries**: Must respect cache line alignment
- **Pointer arithmetic**: Uses masking for alignment maintenance

### Performance Considerations

- **Debug overhead**: Minimal when debug flags disabled
- **Memory usage**: Efficient bit-packing for flag storage
- **Real-time constraints**: Debug display doesn't impact gameplay framerate
