// Analyzed from: src/FUN_002f3118.c
// Original symbol: FUN_002f3118
//
// Purpose
//   Stream decoder for the engine’s custom LZ-like compression used for PSC3 and other assets.
//   Writes decompressed bytes to DAT_0035571c until it sees a zero control byte. Updates
//   DAT_00355720 on completion to the total number of bytes produced.
//
// Notes
//   The exact token grammar is preserved in the original decompiled function; this analyzed file
//   documents its role within the PSC3 loader. It is not a reimplementation here to avoid drift.
//
// Observed globals (from decomp):
//   DAT_00355718: input cursor; DAT_0035571c: output cursor; DAT_00355720: output byte count
//
// Contract
// - Input param_1: source buffer (staging, e.g., 0x1849A00)
// - Input param_2: destination buffer (current decoded arena pointer)
// - Effect: Decodes until terminator; sets DAT_00355720 = bytes_written

// Keep original signature and behavior in src; this file serves as documentation and call-site context.
