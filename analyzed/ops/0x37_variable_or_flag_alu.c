// Opcode 0x37 — variable_or_flag_alu (analyzed)
// Original: FUN_0025d818 (also used by opcode 0x39 via the same handler)
//
// Summary:
// - Evaluates two expressions via the main VM:
//     idx = VM();
//     rhs = VM();
// - Reads the next byte in the stream as an ALU op selector (0x25..0x2F):
//     0x25=assign, 0x26=mul, 0x27=div, 0x28=mod, 0x29=add, 0x2A=sub,
//     0x2B=and, 0x2C=xor, 0x2D=or, 0x2E=inc, 0x2F=dec.
// - Target depends on the current opcode (sGpffffbd68):
//     • If 0x37: target is a 32-bit slot at iGpffffb0f0[idx], with idx in [0, 0x7F].
//     • Else (0x39 uses the same function): target is a byte "bucket" in the global flag bitmap DAT_00342b70.
//       The idx is a bit index; must be <= 0x47F8 and aligned to 8 (idx % 8 == 0). The operation is applied to
//       the addressed bucket byte (read/modify/write), not an individual bit.
// - Returns the resultant value (32-bit), and, for the flag case, writes back only the low byte to the bucket.
// - On invalid indices or bad op selector, the original calls FUN_0026bfc0 with a debug string address (see below).
//
// Notes:
// - This single handler serves both opcodes 0x37 and 0x39; the branch is determined by sGpffffbd68 at runtime.
// - Division/modulo by zero trigger a MIPS trap(7) in the original; we guard them here and leave the value unchanged.
// - Error strings referenced in the original via FUN_0026bfc0 (see strings.json):
//     • 0x34CDF0: "script work over" — raised when variable index is out of range (> 0x7F).
//     • 0x34CE08: "scenario flag work error" — raised when flag bit index is out-of-range or misaligned.
//     • 0x34CE28: "script work error" — raised when the ALU selector byte is invalid.
//
// PS2/engine context:
// - iGpffffb0f0 points to a small per-script integer array (at least 0x80 dwords) used as temporary/script variables.
// - DAT_00342b70 is a ~0x900-byte flag bitmap; this opcode operates on a whole byte when addressing it in 0x39 mode.
//
// Keep unresolved externs by their original labels for traceability. Do not rename them here until analyzed elsewhere.

#include <stdint.h>

typedef unsigned int uint;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Current opcode (determines mode: 0x37 = var array, 0x39 = flag bucket)
extern short sGpffffbd68;

// Instruction pointer to the bytecode stream; used here to fetch the ALU selector byte
extern unsigned char *puGpffffbd60;

// Per-script variable base (dword array)
extern int iGpffffb0f0;

// Global flag bitmap (byte array)
extern unsigned char DAT_00342b70[];

// Debug/assert helper used by original for error paths
extern void FUN_0026bfc0(unsigned int debugStrAddr);

// Original signature: undefined4 FUN_0025d818(void)
unsigned int opcode_0x37_variable_or_flag_alu(void)
{
  uint idx; // first VM value (index or bit index)
  uint rhs; // second VM value (operand)
  bytecode_interpreter(&idx);
  bytecode_interpreter(&rhs);

  uint result_tmp = 0; // mirrors auStack_38[0]
  uint *target = &result_tmp;

  // Select target based on opcode
  if (sGpffffbd68 == 0x37)
  {
    // Variable array mode
    if ((int)idx > 0x7F)
    {
      FUN_0026bfc0(0x34CDF0); // out-of-range variable index
    }
    target = (uint *)(iGpffffb0f0 + idx * 4);
  }
  else
  {
    // Flag bucket mode (opcode 0x39)
    if ((int)idx > 0x47F8 || (idx & 7) != 0)
    {
      FUN_0026bfc0(0x34CE08); // out-of-range or misaligned bit index
    }
    // Compute bucket index handling negative values similarly to the original
    uint adj = idx + 7;
    if ((int)idx >= 0)
      adj = idx;
    // Load the bucket byte into result_tmp for RMW ops
    result_tmp = (uint)DAT_00342b70[(int)adj >> 3];
    target = &result_tmp;
  }

  // Fetch ALU selector from the stream
  unsigned char sel = *puGpffffbd60++;

  switch (sel)
  {
  case 0x25:
    *target = rhs;
    break; // assign
  case 0x26:
    *target *= rhs;
    break; // mul
  case 0x27:
    if (rhs != 0)
      *target = (int)(*target) / (int)rhs;
    break; // div (trap on zero in original)
  case 0x28:
    if (rhs != 0)
      *target = (int)(*target) % (int)rhs;
    break; // mod (trap on zero in original)
  case 0x29:
    *target += rhs;
    break; // add
  case 0x2A:
    *target -= rhs;
    break; // sub
  case 0x2B:
    *target &= rhs;
    break; // and
  case 0x2C:
    *target ^= rhs;
    break; // xor
  case 0x2D:
    *target |= rhs;
    break; // or
  case 0x2E:
    *target += 1;
    break; // inc
  case 0x2F:
    *target -= 1;
    break; // dec
  default:
    FUN_0026bfc0(0x34CE28); // invalid selector
    break;
  }

  if (sGpffffbd68 == 0x37)
  {
    // Return the array slot value after operation
    result_tmp = *target;
  }
  else
  {
    // Write the result back to the flag bucket (low byte only)
    uint adj = idx + 7;
    if ((int)idx >= 0)
      adj = idx;
    DAT_00342b70[(int)adj >> 3] = (unsigned char)result_tmp;
  }

  return result_tmp;
}
