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

### SCR.BIN (1.2MB, 227 entries)

- **Type**: Text archive, NOT script bytecode
- **Contents**: Game text strings and dialogue
- **Format**: Custom archive with embedded text data

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
