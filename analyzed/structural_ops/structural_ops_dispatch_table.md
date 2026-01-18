# Structural Opcodes Dispatch Table (PTR_LAB_0031e1f8)

Dispatch table address: `PTR_LAB_0031e1f8` at 0x0031e1f8

Referenced by: `script_block_structure_interpreter` (FUN_0025bc68)

## Overview

These are low-value opcodes (0x00-0x0A) that control structural flow in the script system, distinct from the main VM arithmetic/logic opcodes. They handle block nesting, conditional branching, switches, and data skipping.

## Opcode Entries

| Opcode | Address    | Function/Label   | Analysis File                 | Purpose                                                  |
| ------ | ---------- | ---------------- | ----------------------------- | -------------------------------------------------------- |
| 0x00   | 0x0025bdc8 | LAB_0025bdc8     | 0x00_noop.c                   | No-operation (return immediately)                        |
| 0x01   | 0x0025bdd0 | FUN_0025bdd0     | 0x01_conditional_vm_advance.c | Conditional VM advance (if expr==0 advance, else skip 4) |
| 0x02   | 0x0025be10 | FUN_0025be10     | 0x02_switch_case_dispatch.c   | Switch/case dispatch (search key table, jump to target)  |
| 0x03   | 0x0025bea0 | LAB_0025bea0     | 0x03_vm_advance.c             | Unconditional VM advance (call FUN_0025c220)             |
| 0x04   | —          | (inline handler) | 0x04_block_end.c              | Block end delimiter (pop stack or terminate)             |
| 0x05   | 0x0025bdc8 | LAB_0025bdc8     | (alias of 0x00)               | No-op (same as 0x00)                                     |
| 0x06   | 0x0025bdc8 | LAB_0025bdc8     | (alias of 0x00)               | No-op (same as 0x00)                                     |
| 0x07   | 0x0025bea8 | LAB_0025bea8     | 0x07_advance_4bytes.c         | Skip 4 bytes (iGpffffbd60 += 4)                          |
| 0x08   | 0x0025beb8 | LAB_0025beb8     | (alias of 0x03)               | VM advance (tail call to FUN_0025c220)                   |
| 0x09   | 0x0025bec0 | LAB_0025bec0     | (alias of 0x07)               | Skip 4 bytes (same as 0x07)                              |
| 0x0A   | 0x0025bed0 | LAB_0025bed0     | (alias of 0x03)               | VM advance (tail call to FUN_0025c220)                   |

## Special Handling

### Opcode 0x04 (Block End)

This opcode is NOT dispatched through the table. It's handled specially before table lookup in the main interpreter loop because it affects control flow (stack pop/return).

### Opcode 0x32 (Block Begin)

Not in this table - handled separately in interpreter. Pushes return address (current+5) onto stack, decrements depth, and calls FUN_0025c220 for relative jump to block body.

## Aliases

Several opcodes share implementations:

- **0x00, 0x05, 0x06**: All no-ops (LAB_0025bdc8)
- **0x03, 0x08, 0x0A**: All VM advance (call FUN_0025c220)
- **0x07, 0x09**: Both skip 4 bytes

These aliases may represent:

- Reserved opcodes for future use
- Semantic variants (different meaning in different contexts)
- Legacy opcodes maintained for backward compatibility

## Relationship to Main VM

The structural opcodes work at a different abstraction level than main VM opcodes (0x32+):

- **Structural layer** (these opcodes): Control script blocks, nesting, and multi-way branches
- **VM layer** (0x32+ opcodes): Arithmetic, logic, entity manipulation, game state

Both layers share the same function pointer dispatch mechanism but operate on different aspects of script execution.

## Usage Patterns

### Block Nesting

```
0x32          # Begin block (push return address, depth--)
  [opcodes]   # Block body
0x04          # End block (pop return address, depth++)
```

### Conditional Branch

```
0x01          # If VM expr == 0, advance; else skip 4 bytes
[4-byte offset if false branch]
```

### Switch Dispatch

```
0x02                    # Switch opcode
[count:byte]            # Number of cases
[padding to align 4]
[key0:int] [target0:int]
[key1:int] [target1:int]
...
```

### Subproc Chaining

```
9E 0C 01 1E 0B         # finish_process_slot(-1)
0x04                    # End block
[id16-le]              # Next subproc ID (consumed by scheduler)
```

## Key Globals

- `pbGpffffbd60`: Script pointer for structural interpreter
- `iGpffffbd60` / `piGpffffbd60`: Instruction stream pointer variants
- `uGpffffbd68`: Current opcode value (set before dispatch)
- `DAT_00355cd0`: VM execution pointer (modified by FUN_0025c220)
- `return_stack`: Manual stack for 0x32/0x04 block nesting

## Related Files

- `analyzed/script_block_structure_interpreter.c`: Main interpreter loop
- `analyzed/bytecode_interpreter.c`: VM-level opcode interpreter
- `analyzed/advance_relative_code_pointer.c`: FUN_0025c220 analysis
