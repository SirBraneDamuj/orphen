/*
 * Internal formatting tables & string cluster analysis (addresses 0x34fff0–0x350080, 0x350e00+)
 *
 * Source of data: hexdump_range.py over eeMemory.bin
 *   Ranges dumped:
 *     0x34fff0+0x40
 *     0x350000+0x80
 *     0x350e00+0x240 (split over two calls)
 *
 * Context:
 *   These addresses are referenced (some directly, some via immediate loads) by the minimal formatter
 *   (FUN_002f6e60 -> analyzed as game_printf_minimal) and its float helper path (FUN_0030beb0 / FUN_002f6cc0).
 *   They were absent from strings.json because several entries are short formatting tokens or binary constants
 *   rather than longer printable debug messages.
 *
 * Cluster 1 (0x34fff0–0x35002f): short format pattern strings & FP scaling constants
 *   0x34fff0: "0.%d" (bytes: 30 2e 25 64 00)             – format used when printing small fractional values
 *   0x34fff5–0x34fff7: padding zeros (alignment to 8)
 *   0x34fff8: "e+%d" (65 2b 25 64 00)                    – scientific notation with explicit + exponent sign
 *   0x34fffd–0x34ffff: padding
 *   0x350000: "e%d"  (65 25 64 00)                       – scientific notation with implicit sign (negative handled separately)
 *   0x350004–0x350007: padding
 *
 *   0x350008: 0x3fb999999999999a (double 0.1)            – fractional scaling constant for fixed digit loops
 *   0x350010: 0x412e848000000000 (double 1000000.0)      – magnitude threshold / scaling (1e6)
 *   0x350018: 0x3fb999999999999a (double 0.1)            – duplicated layout (likely second slot for re-entrancy or unrolled loop)
 *   0x350020: 0x412e848000000000 (double 1000000.0)      – duplicate
 *   0x350028: 0x0000000000000000                         – unused / terminator padding
 *   0x350030+: function pointer table (see below)
 *
 * Cluster 1B (0x350030–0x35007f): repeated pointer pair table
 *   Sequence pattern:  d8 6e 2f 00  88 73 2f 00  repeated.
 *   Interpreted little-endian 32-bit addresses:
 *     0x002f6ed8, 0x002f7388 repeated (~0x50 bytes total).
 *   Hypothesis:
 *     Jump / dispatch table for repeated stages of float formatting (e.g., per-digit state machine) where
 *     one function (0x002f6ed8) handles core digit emission and another (0x002f7388) is a wrapper / thunk.
 *   Next step (pending): correlate 0x002f6ed8 & 0x002f7388 with decompiled source files, add analyzed wrappers.
 *
 * Cluster 2 (0x350e00–0x35103f): pad / controller / picture debug messages
 *   Strings (NUL terminated, 8-byte alignment with padding):
 *     0x350e00: "picture sutructure" (typo preserved)       – initialization / validation log
 *     0x350e10: (padding)
 *     0x350e18: "Too small buffer size for %dx%d picture." – size validation error (uses two integer params)
 *     0x350e40: "CSC handler error"                        – color space conversion (?) failure
 *     0x350e60: "tPadDma Structure Invalid"                – pad DMA structure error
 *     0x350e80: "WARRNING : Already Initialize"            – repeated init (typo WARRNING)
 *     0x350ea0: "PadPortInit: rpc error"                   – RPC failure diagnostics follow
 *     0x350eb8: "PadClose: rpc error"                      – (two variants appear: open/close)
 *     0x350ed0: "PadPortOpen: addr is not 16 byte align."  – alignment check
 *     0x350f00: "PadPortOpen: rpc error"                   – RPC error
 *     0x350f20: "PadClose: rpc error"                      – duplicate cluster variant
 *     0x350f38: "FINDCTP1"                                  – likely status token
 *     0x350f40: "DISCONNECT"                                – connection state strings
 *     0x350f48: "COMPLETE"                                  – completion state
 *     0x350f60: "PadInfoAct: rpc error"                    – per RPC function variants
 *     0x350f78: "PadInfoComb: rpc error"
 *     0x350f90: "PadInfoMode: rpc error"
 *     0x350fa8: "PadSetMainMode: rpc error"
 *     0x350fd0: "PadSetActDirect: rpc error"
 *     0x350ff8: "PadSetActAlign: rpc error"
 *     0x351010: "PadGetButtonMask: rpc error"
 *     0x351028: "PadSetButtonInfo" (truncated in dump; continuation beyond 0x351040 not captured yet)
 *
 * Usage Implications:
 *   The short format tokens (0x34fff0 cluster) are passed directly as format arguments to game_printf_minimal
 *   inside the floating-point formatting helper (FUN_002f6cc0). They represent scientific notation patterns:
 *     "0.%d"   – leading zero before decimal point
 *     "e+%d"   – positive exponent representation
 *     "e%d"    – generic exponent (negative handled by emitting '-')
 *   Doubling of constants 0.1 and 1e6 suggests either two-phase calculation (e.g., generating mantissa and exponent)
 *   or unrolled loop expecting a pair of references for performance/alignment.
 *
 * Open Questions / TODO:
 *   - Identify exact decompiled functions at 0x002f6ed8 & 0x002f7388 and produce analyzed wrappers.
 *   - Determine whether duplicate constant pairs are due to struct layout (e.g., struct { double a; double b; }[2]).
 *   - Extend hexdump beyond 0x351040 to complete the final Pad* string(s).
 *   - Cross-reference these addresses in strings.json generation pipeline (decide whether to whitelist short tokens).
 */

/* This file intentionally contains only commentary; no compiled code is needed right now. */

#if 0
/* Potential future extern mapping (when functions at 0x002f6ed8 / 0x002f7388 are located): */
extern void FUN_002f6ed8(void); /* suspected float digit emitter */
extern void FUN_002f7388(void); /* suspected wrapper / dispatcher */
#endif
