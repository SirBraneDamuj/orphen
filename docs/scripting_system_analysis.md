# Orphen: Scion of Sorcery - Scripting System Analysis

## Overview

The PS2 game "Orphen: Scion of Sorcery" contains a sophisticated scripting system built around a stack-based virtual machine. This document summarizes our reverse engineering findings about how the scripting engine works.

## Architecture

### Core Components

1. **Bytecode Interpreter** (`FUN_0025c258` → `bytecode_interpreter`)

   - Stack-based virtual machine executing script bytecode
   - Instruction pointer at `DAT_00355cd0` (points to current script location)
   - Current opcode stored in `DAT_00355cd8`
   - Local execution stack for intermediate values
   - Three-tier instruction processing system

2. **Instruction Dispatch System**

   - **Basic Instructions (0x00-0x31)**: Handled by `FUN_0025bf70`

     - Arithmetic operations (add, subtract, multiply, divide, modulo)
     - Logical operations (AND, OR, XOR, NOT)
     - Comparison operations (equals, not equals, less than, greater/equal)
     - Stack manipulation and control flow

   - **Standard Instructions (0x32-0xFE)**: Jump table `PTR_LAB_0031e228`

     - 196 instruction functions
     - Complex game operations and system calls

   - **Extended Instructions (0xFF + parameter)**: Jump table `PTR_LAB_0031e538`
     - 75 instruction functions
     - Advanced scripting capabilities
     - Opcode format: 0xFF followed by parameter byte (0x100-0x174 range)

3. **Total Instruction Set**: 271 unique instruction functions

## Memory Layout

### Script Execution Environment

- **Script Memory Range**: `0x01C40000` - `0x01C8FFFF` (327,680 bytes)
- **Instruction Pointer**: Currently points to this range during gameplay
- **Execution Model**: Scripts are loaded and executed from this dedicated memory region

### State Management

#### Work Memory Array (`DAT_00355060`)

- 32-bit value storage for temporary script variables
- Maximum 128 entries (512 bytes total)
- Accessed via opcode 0x36 in `script_read_flag_or_work_memory`
- Used for computed values and temporary data

#### Flag Bitfield System (`DAT_00342b70`)

- Persistent boolean game state storage
- Maximum 18,424 flags (2,303 bytes)
- Single-bit precision for memory efficiency
- Likely stores: story progress, item collection, trigger states, etc.
- Accessed via various opcodes in `script_read_flag_or_work_memory`

## Analyzed Functions

### 1. Bytecode Interpreter (`FUN_0025c258`)

**File**: `analyzed/bytecode_interpreter.c`

**Purpose**: Core virtual machine that executes script bytecode

**Key Features**:

- Stack-based execution model
- Automatic instruction dispatch via jump tables
- Built-in arithmetic and logical operations
- Error handling with division-by-zero protection
- Support for 271 different instruction types

**Execution Flow**:

1. Process high-level instructions (0x32+) via jump tables
2. Handle basic operations (0x00-0x31) via dedicated function
3. Maintain execution stack for intermediate values
4. Continue until exit instruction (0x0B)

### 2. Flag/Work Memory Reader (`FUN_0025d768`)

**File**: `analyzed/script_read_flag_or_work_memory.c`

**Purpose**: Script instruction for accessing game state data

**Two Operating Modes**:

- **Work Array Mode (opcode 0x36)**: Reads 32-bit values from work memory
- **Flag Mode (other opcodes)**: Reads single bits from persistent flag array

**Error Handling**:

- "script work over" - Work array index > 127
- "scenario flag work error" - Flag index > 18423 or not aligned

## File System Analysis

### MCB1.BIN

- **Suspected Type**: Script container
- **Status**: Not yet analyzed
- **Likelihood**: High probability of containing actual script bytecode

## Instruction Set Scope

Based on jump table analysis, the scripting system supports:

### Standard Instructions (196 functions)

- Examples: `FUN_0025d768`, `FUN_0025d6f8`, `FUN_0025d728`
- Core game operations and state manipulation

### Extended Instructions (75 functions)

- Examples: `FUN_00262118`, `FUN_00262250`, `FUN_002620a8`
- Advanced features and complex operations

### Instruction Categories (Inferred)

- **State Management**: Flag reading/writing, work memory access
- **Game Logic**: Entity manipulation, scene control
- **Audio/Visual**: Sound playback, graphics rendering
- **Control Flow**: Conditional branching, function calls
- **Math Operations**: Coordinate transformations, calculations

## Current Analysis Status

### ✅ Completed

- Bytecode interpreter core architecture
- Basic arithmetic/logical instruction set (0x00-0x31)
- Flag and work memory access patterns
- Jump table structure and instruction count
- Memory layout understanding

### 🔄 In Progress

- Individual instruction function analysis
- Extended instruction set capabilities
- Script file format identification

### ❓ Unknown/Pending

- Script compilation/loading process
- Inter-script communication mechanisms
- Integration with game engine systems
- Actual script content and purpose
- Performance characteristics

## Key Insights

1. **Sophistication**: This is a full scripting language, not a simple expression evaluator
2. **Scale**: 271 instruction functions indicate comprehensive game scripting capabilities
3. **Architecture**: Professional-grade VM design with proper error handling
4. **State Management**: Dual-tier system (flags + work memory) for different data types
5. **Memory Safety**: Bounds checking and error reporting for invalid accesses

## Next Analysis Priorities

1. **High-frequency Instructions**: Analyze `FUN_00262118`, `FUN_00262250` (appear 3+ times in tables)
2. **Extended Instruction Set**: Focus on 0x100+ range instructions
3. **Script File Analysis**: Examine MCB1.BIN for actual script bytecode
4. **Memory Investigation**: Run memory analysis script during gameplay
5. **Cross-reference Mapping**: Understand how instructions interact with game systems

## Technical Notes

- All original `FUN_*` names preserved in comments for cross-reference
- Global variable addresses maintained for debugging
- Error message strings documented for runtime debugging
- Stack manipulation patterns identified for instruction analysis

---

_Last Updated: August 5, 2025_
_Analysis Status: Foundation Complete - Beginning Instruction Set Analysis_

## Script File Format & Subprocedure (Subproc) ID Extraction

### Pointer Table Region (SCR\* files)

Preliminary layout (from empirical parsing of `scr*.out` exports):

- File header: 11 x 32-bit words. Word[5] = pointer_table_start, word[6] = pointer_table_end, word[7] = footer_base.
- Pointer table: sequence of 32-bit offsets into script region (ends at word[6]).
- Post-pointer region: Mixed bytecode records containing script bodies and metadata.

### Delimiter & Field Tokens

- Byte `0x0B` acts as a ubiquitous field/record/parameter delimiter. Nearly all higher-level structural boundaries end with `0x0B`.
- Byte `0x0E` frequently precedes parameter (literal) values; pattern `0x0E <value> 00 00 00 0B` commonly terminates numeric parameter blocks.

### Repeating Structural Patterns (Heuristic Labels)

Pattern A:

```
25 37 0e <...variable payload...> 0b <...payload...> 0b
```

Pattern B (two forms interleaved):

```
25 01 52 0e ...
01 52 0e ...
```

Often followed by optional subheader:

```
0b 0b 34|44 00 00 00 37 0e <idx> 0b 59 0b
```

Then zero or more subrecords:

```
[25] 79 0e <u32> 0b (param groups of 0e <val> 00 00 00 0b)
```

These patterns delineate compound script structures; exact semantic mapping still pending.

### Subproc ID Records ("0x04 ID16")

Empirically discovered records encoding 16-bit subprocedure identifiers. Detection relies on preceding signature bytes:

- Signature Class SIG_A: `9e 0c 01 1e 0b` immediately before an `0x04 <id_lo> <id_hi>` pair.
- Signature Class SIG_B: Alternative context containing `0b 0b 02 92 0c` before the ID sequence.

Extraction heuristic (implemented in `scripts/analyze_post_pointer_region.py`):

1. Scan post-pointer region for byte `0x04` followed by two non-zero bytes (candidate ID16) and a plausible trailing delimiter.
2. Validate presence of one of the known prologue signatures (SIG_A / SIG_B) within a small backward window.
3. Reject candidates whose trailing context fails plausibility checks (next control byte not in {0x04,0x11,0x00}, or misaligned with preceding delimiter `0x0B`).
4. De-duplicate by 16-bit value; track first occurrence offset and signature class.

### Heuristic Reliability & Filtering

- False positives reduced substantially by requiring signature match plus delimiter alignment.
- Additional filter: adjacency to literal dword `0x00000001` in specific alignment contexts (observed near valid IDs) used to raise confidence but not mandatory.
- Current pass produced a stable ID set (no spurious additions across successive tool revisions), indicating detection logic is near-complete for the sampled files.

### Data Model Captured

For each accepted ID:

- 16-bit value (displayed hex)
- First occurrence file offset
- Signature class (SIG_A, SIG_B)
- Local structural bytes (snippet for manual verification)

Planned future enrichment: associate each ID with its enclosing Pattern A/B block to infer grouping or call graph semantics.

### Open Questions

- Semantic role of Pattern A vs Pattern B in relation to subproc invocation.
- Whether IDs map 1:1 to exported functions in the interpreter or represent data-driven state machine labels.
- Presence of any higher-order index table linking pointer table entries to subproc ID clusters.

### Current Status Summary (Subproc IDs)

- Discovery: COMPLETE (stable enumeration with dual signature classes)
- False Positive Rate: Low (manual spot checks confirm structural alignment)
- Next Steps: Correlate IDs with runtime call paths and interpreter jump table usage.

_Section Added: August 11, 2025_

## Revision History

- Aug 11 2025: Added subprocedure ID detection methodology, patterns (A/B), delimiter semantics.
- Aug 05 2025: Initial scripting system architecture document.
