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
| 0x03   | 0x0025bea0 | LAB_0025bea0     | 0x03_vm_advance.c             | Unconditional VM advance (j FUN_0025c220)                |
| 0x04   | 0x0025bdc8 | LAB_0025bdc8     | 0x04_block_end.c              | Table entry is the no-op, but 0x04 never reaches the table -- FUN_0025bc68 handles block end inline |
| 0x05   | 0x0025bdc8 | LAB_0025bdc8     | (alias of 0x00)               | No-op (same as 0x00)                                     |
| 0x06   | 0x0025bea8 | LAB_0025bea8     | 0x07_advance_4bytes.c         | Skip 4 bytes (iGpffffbd60 += 4)                          |
| 0x07   | 0x0025beb8 | LAB_0025beb8     | (alias of 0x03)               | VM advance (j FUN_0025c220)                              |
| 0x08   | 0x0025bec0 | LAB_0025bec0     | (alias of 0x06)               | Skip 4 bytes                                             |
| 0x09   | 0x0025bed0 | LAB_0025bed0     | (alias of 0x03)               | VM advance (j FUN_0025c220)                              |
| 0x0A   | 0x0025bed8 | FUN_0025bed8     | --                            | `FUN_002681c0("Debug Code:%d", iGpffffb0cc)` then `FUN_0026c088` on the same, then `++iGpffffb0cc` |

**This table was wrong before 2026-09-06 and the native port inherited the
error.** The earlier version listed 0x04 as "(inline handler)" with no address
and then read every remaining row one slot early, which made 0x06/0x08 look like
no-ops and swapped the jumps and the skips from 0x07 up. `PTR_LAB_0031e1f8` has
eleven entries indexed by the raw opcode with no bias (`FUN_0025bc68:31`,
`(&PTR_LAB_0031e1f8)[bVar2]`), including a real entry for 0x04 that is simply
never reached. The addresses above are read out of `SLUS_200.11` at
0x0031e1f8; the bodies at 0x25bea0..0x25bee0 are five instruction pairs and
disambiguate themselves (`j 0x25c220` is a jump, `lw/addiu 4/sw` is a skip).

A 0x06 read as a no-op leaves its rel32 in the statement stream, and the next
byte decodes as garbage: that is exactly how s14_e001 halted on "unimplemented
opcode 0xf" at the end of its start entry, where an if/else uses 0x06 as the
else arm's jump over the second branch.

## Special Handling

### Opcode 0x04 (Block End)

This opcode is NOT dispatched through the table. It's handled specially before table lookup in the main interpreter loop because it affects control flow (stack pop/return).

### Opcode 0x32 (Block Begin)

Not in this table - handled separately in interpreter. Pushes return address (current+5) onto stack, decrements depth, and calls FUN_0025c220 for relative jump to block body.

## Aliases

Several opcodes share implementations:

- **0x00, 0x04, 0x05**: All no-ops (LAB_0025bdc8)
- **0x03, 0x07, 0x09**: All VM advance (j FUN_0025c220)
- **0x06, 0x08**: Both skip 4 bytes
- **0x0A**: alone; the debug-counter print

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
