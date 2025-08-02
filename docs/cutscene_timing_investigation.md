# Cutscene Timing Investigation

## Goal

Find and modify the timing mechanisms that control NPC cutscene actions to enable fast-forwarding cutscenes by patching the disc image.

## Key Findings

### Subproc Debug System

- **Address 0x3555dc**: When set to 1, enables debug display showing "Subproc" information
- **Debug format**: `"Subproc:%3d [%5d]\n"` (string at 0x34ca60)
  - First value: NPC/object index (0-61)
  - Second value: `*(scene_object_ptr + -4)` - likely current script instruction pointer or action state
- **Key insight**: These values are mostly static during NPC actions, NOT timers

### Script System Architecture

#### Main Components

1. **Scene Processing Loop** (`FUN_0025b778` / `process_scene_with_work_flags`)

   - Called once per frame from main game loop (`FUN_002239c8`)
   - Processes up to 62 scene objects from array at `DAT_00355cf4`
   - Each object has an associated script/bytecode sequence

2. **Bytecode Interpreter** (`FUN_0025bc68` / `scene_command_interpreter`)

   - Executes NPC action scripts using virtual machine
   - Uses global pointer `pbGpffffbd60` as instruction pointer
   - **Opcode 4**: Yield/return instruction that saves state and exits interpreter
   - **Key insight**: Timing control happens OUTSIDE the interpreter

3. **Script Instruction Pointer** (`DAT_00355cd0`)
   - Current instruction pointer for the bytecode virtual machine
   - Advanced by script instruction handlers
   - Functions that access it:
     - `FUN_0025c1d0`, `FUN_0025c220`, `FUN_0025c258`
     - `FUN_0025d618`, `FUN_0025d6f8`, `FUN_0025e250`
     - `FUN_0025e560`, `FUN_0025e628`, `FUN_0025f1c8`
     - `FUN_0025f4b8`, `FUN_0025f7d8`, `FUN_00260738`
     - `FUN_00261330`, `FUN_002613d8`, `FUN_00262058`
     - `FUN_00263148`, `FUN_002651a0`

#### Script Instruction Types

- **Basic instructions** (0x00-0x0A): Handled by jump table at `PTR_LAB_0031e1f8`
- **Extended instructions** (0x32+): Handled by jump table at `PTR_LAB_0031e228`
- **Extended instructions** (0xFF xx): Handled by jump table at `PTR_LAB_0031e538`

### Timing Mechanism Analysis

#### Frame-Based Execution

- Scripts execute once per frame when `process_scene_with_work_flags` is called
- When script hits timing instruction, it yields control back to main loop
- Timing values likely stored as frame counters that decrement each frame

#### Potential Timing Variables Found

1. **`uGpffffbd5a`** in `FUN_00251ed8`:

   ```c
   uGpffffbd5a = uGpffffbd5a - 1;
   if ((int)((uint)uGpffffbd5a << 0x10) < 1) {
       bGpffffbd58 = 0xff;
   }
   ```

   - Clear frame counter that decrements and triggers action when reaching 0

2. **Timer checks in `FUN_00251ed8`**:

   ```c
   if (0 < (short)uGpffffbd5e) {
       iVar6 = (uint)uGpffffbd5e - (uGpffffb64c & 0xffff);
       uGpffffbd5e = (ushort)iVar6;
       if (iVar6 * 0x10000 < 1) {
           uGpffffbd5e = 0;
           FUN_00206260(7,0x19,0);
       }
   }
   ```

3. **Various countdown timers**:
   ```c
   psVar8[0x60] = (short)iVar6;  // Timer decrements
   psVar8[0xdc] = (short)iVar6;  // Another timer
   ```

### Strategy for Disc Patching

#### Approach 1: Find Script Timing Instructions

- Identify which script instruction opcodes handle timing/delays
- Look for instructions that read 2-byte or 4-byte timing values from script data
- Examples seen: Functions reading `DAT_00355cd0 + 2` (2-byte values)

#### Approach 2: Locate Timing Data Patterns

- Search disc image for common timing patterns (e.g., 60 frames = 1 second at 60 FPS)
- Replace large timing values with smaller ones (e.g., 60 → 5)

#### Approach 3: Patch Frame Counters

- Modify the frame counter decrement logic to count down faster
- Change `counter = counter - 1` to `counter = counter - 10`

## Next Steps

1. **Complete Script Instruction Analysis**

   - Examine remaining functions that access `DAT_00355cd0`
   - Identify which ones handle timing/delay instructions
   - Look for patterns of reading multi-byte timing values

2. **Identify Timing Data Format**

   - Determine how timing values are encoded in script bytecode
   - Find examples of timing instructions in actual script data

3. **Test Timing Modifications**

   - Try modifying frame counter variables in memory
   - Observe effect on cutscene timing

4. **Disc Image Analysis**
   - Once timing patterns are identified, search disc image for these patterns
   - Create patching tool to reduce timing values systematically

## Technical Notes

- **Debug flags**: `DAT_003555dd & 0x80` controls Subproc debug output
- **Scene object array**: `DAT_00355cf4` contains pointers to up to 62 scene objects
- **Current object**: `DAT_00355cf8` tracks which object is currently being processed
- **Script yield mechanism**: Opcode 4 in bytecode interpreter causes script to yield and save state

## Files Analyzed

- `process_scene_with_work_flags.c` - Main scene processing loop
- `FUN_0025bc68.c` - Bytecode interpreter
- `FUN_0025c*.c` - Various script instruction handlers
- `FUN_00251ed8.c` - Complex function with multiple frame counters
- `FUN_002239c8.c` - Main game loop that calls scene processing
