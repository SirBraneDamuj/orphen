/*
 * Full featured formatted output core (reverse engineered) from original function FUN_0030c8d0.
 *
 * Original signature (decompiled):
 *   int FUN_0030c8d0(undefined4 param_1, undefined8 param_2, undefined8 param_3, ulong *param_4);
 *
 * Working (inferred) prototype:
 *   int game_vfprintf_core(int locale_token,
 *                          void *output_ctx,
 *                          const char *format,
 *                          const unsigned long *arg_slots);
 *
 * High-Level Purpose:
 *   This is the primary, fully featured printf / vfprintf style formatter used by the game for
 *   debug and possibly UI text rendering. It supports the standard C conversion specifiers
 *   (d i u o x X p s c e f g E F G) plus length modifiers and width / precision handling,
 *   unlike the smaller minimalist helper (FUN_002f6e60 -> game_printf_minimal).
 *
 * Parameter Roles (inferred):
 *   locale_token (param_1): Passed through to certain floating-point helper functions
 *     (e.g. FUN_0030de70). Likely selects / carries decimal separator, locale or formatting context.
 *   output_ctx (param_2): Pointer to an output descriptor structure. Fields accessed via offsets:
 *       +0x0c : status / mode flags (bit 0x8 checked early)
 *       +0x0e : short (maybe error state / buffer fullness threshold)
 *       +0x10 : pointer (non-null required when bit 0x8 set)
 *       +0x54 : pointer to an internal IO descriptor; lazily initialized to PTR_DAT_0034b01c.
 *     This structure is flushed by helper FUN_0030c7a0 when segment batch fills or on boundaries.
 *   format (param_3): Pointer to NUL terminated format string.
 *   arg_slots (param_4): Pointer to homogeneously 64-bit aligned vararg area (each argument consumes
 *     8 bytes regardless of actual type, matching PS2 EE vararg alignment) advanced one element per
 *     consumed argument (u64). Narrow types (short, int) are read from the low portion.
 *
 * Return Value:
 *   Total characters written, or -1 on error (subject to (uVar9 & 0x40) flag from output_ctx controlling
 *   whether -1 is suppressed in favor of partial count). Mirrors C library semantics of vfprintf-like routines.
 *
 * Internal Pipeline Overview:
 *   1. Initialize / validate IO descriptor (lazy allocation, calling FUN_0030f8d0 if required).
 *   2. Quick path: If certain mode bits set ( (flags & 0x1a)==0x0a and 0 <= *(short*)(ctx+0xe) ) call
 *      FUN_0030c7e8 for an optimized formatting path and return.
 *   3. Main loop: Use FUN_00310e60 to scan from current format cursor up to the next '%' or end; accumulate
 *      literal span as a segment (pointer,length) into a small fixed segment array (aiStack_2d0). Flush via
 *      FUN_0030c7a0 whenever 8 segments are buffered.
 *   4. On encountering '%', parse flags / width / precision / length modifiers. Flags bitfield (uStack_104):
 *        0x0001  '#' alternate form
 *        0x0002  prefix requested (internal use: '0x' or '0X' for hex) (set when '#' with non-zero value)
 *        0x0004  '-' left alignment
 *        0x0008  'L' long double (float handling path) (observed but both branches read the same slot)
 *        0x0010  'l' length (long) (second 'l' adds 0x20 for 'll')
 *        0x0020  'll' / extended length (set by encountering 'q' (0x71) or second 'l')
 *        0x0040  'h' short length modifier
 *        0x0080  '0' zero padding
 *        0x0100  Internal flag: floating conversion in progress (set before specialized float formatting)
 *      Width: stored in pcStack_fc (char* used as integer) (supports '*').
 *      Precision: stored in pcStack_e4 (char* used as integer) (supports '.' / '.*'), default -1 sentinel (0xffffffff).
 *   5. Fetch argument(s) from arg_slots according to conversion and length flags.
 *   6. Convert value into a staging buffer (acStack_134 for numeric digits, acStack_290 for generic char staging,
 *      auStack_2f0 for some floating intermediary).
 *   7. Determine sign/prefix (acStack_11f holds sign or leading space/plus; presence influences padding placement).
 *   8. Calculate left/right padding using constant tables:
 *        0x351b50: 16 spaces + 16 zeros (used for block padding) ["                0000000000000000"]
 *        0x351b70: "Inf" / padding
 *        0x351b78: "NaN"
 *        0x351b80: lowercase hex digits
 *        0x351b90: "(null)"
 *        0x351ba0: uppercase hex digits
 *        0x351bb0: (padding)
 *        0x351bb8: "bug in vfprintf: bad bas" (truncated; diagnostic string)
 *        0x351bd0: 'e'  (scientific exponent char)
 *        0x351bd4: '0'  (zero char literal)
 *        0x351be0: '.'  (decimal point)
 *      Additional tables at 0x351bf0++ appear to be function pointer pairs / small dispatch table entries.
 *   9. Assemble output segments: order is (left pad) (sign/prefix) (zero pad) (core digits / string body) (right pad)
 *      depending on flags. Rather than emitting characters individually, the function batches pointers+lengths.
 *  10. Flush when needed via FUN_0030c7a0; update running total iStack_100.
 *  11. Loop until end of format string.
 *
 * Floating-Point Handling:
 *   - Conversions e/E/f/F/g/G handled in common case branch. Precision defaults to 6 similar to C if absent.
 *   - Uses helpers: FUN_00312058, FUN_003120a0, FUN_0030de70, FUN_0030e018, FUN_0030b018 for classification,
 *     sign extraction, digit generation, and rounding logic. 'Inf'/'NaN' path selects constant strings.
 *   - Internal bit 0x100 marks path expecting multi-stage assembly (exponent decision for g/G, decimal placement).
 *
 * Noteworthy Behaviors vs Standard printf:
 *   - Single segment batching with limit 8; large paddings produced via repeated 16-byte chunk references to
 *     prebuilt space / zero tables (performance optimization).
 *   - '(null)' literal substituted when %s argument pointer is NULL (standard-like), whereas minimal formatter
 *     used empty-string check for printing '(null)'.
 *   - Supports %n (writes number of chars output so far) with length modifiers (short, int, long).
 *   - Accepts 'q' (0x71) as length modifier setting 0x20 (treat as 'll'); likely from BSD or early GCC extension.
 *   - Pads via either left spaces, zeroes, or right spaces depending on '-' and '0'. Combination logic mirrors
 *     typical vfprintf precedence: sign/prefix before zero padding when zero flag active.
 *
 * Error / Flush Semantics:
 *   - FUN_0030c7a0 returns non-zero on flush error; function aborts and returns -1 (unless (flags & 0x40) forces
 *     returning partial count). That controlling flag arises from *(ushort*)(output_ctx+0x0c) & 0x40.
 *   - Early negative return from FUN_0030e150 leads directly to -1.
 *
 * Data Structure (segment buffer) (conceptual):
 *   struct Segment { const char *ptr; int len; }; // stored interleaved as len then ptr or ptr then len depending on pattern
 *   Local array capacity: 8 segments before flush (aiStack_2d0 area). (Decompiled layout stores pointer/length pairs as ints.)
 *
 * Identified Helper Functions (names pending future detailed analysis):
 *   FUN_00310318()   -> returns pointer to locale / env struct; first int maybe decimal point char (iStack_f8)
 *   FUN_0030f8d0()   -> IO descriptor initialization
 *   FUN_0030e150(ctx)-> state validation / fallback path readiness check
 *   FUN_0030c7e8()   -> specialized fast formatting path (when certain context flags set)
 *   FUN_00310e60(locale, out_char_ptr, src_ptr, limit, state_ptr) -> scanning / copying until '%' (likely locale-aware)
 *   FUN_0030c7a0(ctx, &segment_array_ptr) -> flush batched segments to destination (updates ctx state)
 *   FUN_0030c4a8(str) -> strlen (or length computation supporting precision clamp)
 *   FUN_00310e9c(str,start,limit) -> bounded pointer advance (memchr / strnlen style)
 *   FUN_0030a108 / FUN_00309b48 -> unsigned decimal remainder / quotient helpers
 *   FUN_0030b018(float_ctx,flag) -> sign or classification check for float (negativity or exponent sign)
 *   FUN_00312058 / FUN_003120a0 -> classify float (isInf / isNaN?)
 *   FUN_0030de70(locale, value, precision, flags, &sign_flag, &exp_ptr, conv_char, &digits_count)
 *   FUN_0030e018(buffer,digits_start,conv_char) -> possibly exponent formatting into auStack_2f0
 *
 * Forward Plan:
 *   This file serves as a documentation scaffold. A future step will be a clean reimplementation akin to
 *   game_printf_minimal, but due to complexity we first map flag semantics, segment batching, and helper
 *   interfaces. Further passes will:
 *     - Enumerate all helper functions with dedicated analyzed files.
 *     - Validate exact ordering of prefix/zero/sign emission via targeted test harness referencing runtime tables.
 *     - Confirm 'q' modifier semantics and potential 'll' conflation.
 *
 * NOTE: We intentionally do not copy the enormous control-flow verbatim; we rely on the original FUN_* for ground truth
 * while documenting components. This reduces maintenance noise while still capturing semantics for other systems.
 */

#include <stddef.h>
#include <stdint.h>

/* Original function (extern) so other analyzed code can still reference runtime behavior. */
extern int FUN_0030c8d0(uint32_t locale_token, void *output_ctx, const char *format, unsigned long *arg_slots);

/* Flag bits (uStack_104) reconstructed */
enum vf_flag_bits
{
  VF_FLAG_ALT_FORM = 0x0001,     /* '#' */
  VF_FLAG_PREFIX_USED = 0x0002,  /* internal: '0x'/exponent prefix accounted */
  VF_FLAG_LEFT_ADJUST = 0x0004,  /* '-' */
  VF_FLAG_LONG_DOUBLE = 0x0008,  /* 'L' (float extended) */
  VF_FLAG_LENGTH_LONG = 0x0010,  /* 'l' */
  VF_FLAG_LENGTH_LLONG = 0x0020, /* 'll' or 'q' */
  VF_FLAG_LENGTH_SHORT = 0x0040, /* 'h' */
  VF_FLAG_ZERO_PAD = 0x0080,     /* '0' */
  VF_FLAG_FLOAT_ACTIVE = 0x0100  /* internal: float formatting in progress */
};

/* Documentation helper structure (not used by original code). */
struct vf_segment
{
  const char *ptr;
  int len;
};

/* Wrapper with clearer name for callers once refactors begin. */
int game_vfprintf_core(uint32_t locale_token, void *output_ctx, const char *format, const unsigned long *arg_slots)
{
  /* For now just forward; analysis retains original semantics. */
  return FUN_0030c8d0(locale_token, output_ctx, format, (unsigned long *)arg_slots);
}

/*
 * Future Work Items (tracking checklist):
 *   [ ] Produce analyzed helper: FUN_00310e60 (format span scanner)
 *   [ ] Produce analyzed helper: FUN_0030c7a0 (segment flush mechanism)
 *   [ ] Float classification helpers (FUN_00312058 / FUN_003120a0 / FUN_0030de70) mapping to IEEE categories
 *   [ ] Derive exact output_ctx struct layout (offset commentary & field purposes)
 *   [ ] Validate behavior of %n across length modifiers with targeted instrumentation
 *   [ ] Extract and label constant tables (space/zero chunk, digit sets) in a header for reuse in reimplementation
 *   [ ] Implement test harness feeding synthetic format strings against both original and clean version
 */

/* Original retained for traceability:
   int FUN_0030c8d0(undefined4 p1, undefined8 p2, undefined8 p3, ulong *p4); */
