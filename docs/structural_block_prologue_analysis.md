# Structural Block 0x32 Self-Relative Cell

This note supersedes the older "four-byte prologue mini-program" interpretation.

## Source-grounded behavior

`FUN_0025bc68` handles structural opcode `0x32` like this:

```c
push(pc + 5);
pc = pc + 1;
FUN_0025c220();
```

`FUN_0025c220` then performs:

```c
DAT_00355cd0 = (int *)((int)DAT_00355cd0 + *DAT_00355cd0);
```

The important aliasing point is that `pbGpffffbd60` and `DAT_00355cd0` appear to be the same GP-relative global rendered under two decompiler names. Likewise, `uGpffffbd68` and `DAT_00355cd8` are the same current-opcode word. With that alias in mind, the four bytes after `0x32` are read as a signed 32-bit self-relative delta from `pc + 1`.

Effective offline rule:

```text
0x32 <rel32-le>
  push continuation = opcode_offset + 5
  target = opcode_offset + 1 + signed_rel32
  pc = target
```

When a later `0x04` block end unwinds, the structural interpreter pops the saved continuation and resumes at `opcode_offset + 5`, skipping the already-used rel32 cell.

## Why the mini-program model was wrong

Several real `0x32` cells begin with bytes such as `0x0B`. If those four bytes were executed as top-level structural opcodes, `0x0B` would fall into the high-dispatch path with a negative table index (`0x0B - 0x32`). Treating the cell as a rel32 read by `FUN_0025c220` avoids that impossible control flow and matches the raw decompiled source.

## Disassembler guidance

- Treat the four bytes after `0x32` as a little-endian signed relative cell.
- Record `pc + 5` as the continuation for later `0x04` unwinds.
- Do not linearly disassemble the rel32 bytes as structural opcodes.
- If `target` lands outside the file in an offline dump, the scanned `0x32` was probably data or the file offset does not correspond to the in-memory pointer being executed.

## Runtime validation

Useful breakpoints:

- `FUN_0025bc68`: stop when `*pbGpffffbd60 == 0x32`; record `pbGpffffbd60`, the next four bytes, and the pushed continuation.
- `FUN_0025c220`: step over and confirm `DAT_00355cd0` becomes `old_pc + signed_rel32`.
- `FUN_0025bc68` at `0x04`: confirm the pop resumes at the saved `pc + 5` continuation.
