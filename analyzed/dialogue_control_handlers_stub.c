// Control Handler Table Mapping Stub (PTR_FUN_0031c640)
// Purpose: Centralize emerging knowledge of control opcodes (<0x1F) dispatched by FUN_00237de8.
// Raw dispatch logic: if (*pbGpffffaec0 < 0x1F) jump via PTR_FUN_0031c640 + (opcode * 0x40) (each slot ~0x40 bytes).
// The 0x40 stride suggests each control opcode owns an inline code block region instead of a flat pointer array.
//
// Known / Partially Identified Handlers:
//   (Hex)  LenTable  Meaning (hypothesis)                 Evidence / Notes
//   0x00   ?         STREAM_END / terminator?             Many scanners treat opcode <2 as termination (see FUN_00237ca0)
//   0x01   ?         Possibly soft end / continue marker  Same termination logic threshold (<2) in recursive parser.
//   0x0C   2         Delay / timing control               Extractor attaches param; appears as 0x0C <ticks>
//   0x13   0         Speaker / nested block start         Recursive handling and name parsing in FUN_00237ca0
//   0x15   0         Multi-string / branching block       Counted string group loop in FUN_00237ca0
//   0x16   5         Voice playback packet                Mode, sep, 16-bit voice id (already identified)
//   0x1A   2         Scene / speaker advance              0x1A 00 next speaker, 0x1A 01 new scene
//   0x1E   0         Unknown variable-length control      Zero length in table, likely a structural opcode
//   0x??   ?         Special glyph/icon insertion         FUN_00239b00 consumes 2 bytes after opcode; likely one of low codes
//   0x??   4         List/scroll header                   FUN_00239848 reads 4 bytes header -> sets DAT_005716c8/c4/c0 etc.
//
// Unmapped Goals:
//   - Camera control opcodes
//   - Entity animation triggers
//   - Branch / conditional flow beyond 0x15
//   - Window / position relocation commands
//
// Suggested Dynamic Mapping Process:
//   1. Instrument dispatch: log opcode + next N bytes (e.g., 8) before executing handler (debug build only).
//   2. Correlate with visual events (camera pans, animation starts) while stepping through a cutscene.
//   3. Mark handlers that write to known entity/camera globals (search for patterns around entity arrays) and record offsets.
//   4. Populate the table below iteratively.
//
// Pending Table (fill as identified):
//   Opcode | StrideAddr (base + opcode*0x40) | Handler FUN_* (if isolated) | Summary | Params
//   -------+---------------------------------+-----------------------------+---------+--------
//
// This file intentionally contains no executable code; it is a living documentation artifact.

// (Add future helper utilities below if needed, e.g., a debug hook macro guarded by #ifdef.)
