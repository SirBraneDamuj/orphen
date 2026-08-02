/*
 * Scene script expression evaluator
 * Original: FUN_0025c258 (0x0025c258), with literal decoder FUN_0025bf70 (0x0025bf70)
 *
 * Summary
 * - Evaluates one expression out of the SCR bytecode stream and stores the 32-bit
 *   result through the caller's pointer. This is the function every statement
 *   opcode calls to read its arguments.
 * - The expression language is postfix (RPN) over a 4-entry operand stack that
 *   grows downward. Terminator 0x0B pops the top of stack into *param_1 and
 *   returns.
 * - Three kinds of token appear in an expression:
 *     >= 0x32  a *statement* opcode, dispatched through the same two tables the
 *              structural interpreter uses; its return value is pushed. This is
 *              how script expressions call things like 0x59 get_pw_slot_index.
 *     0x0C..0x11, 0x30, 0x31  immediates, decoded by FUN_0025bf70.
 *     0x12..0x24  operators, applied in place to the top one or two entries.
 * - Opcodes 0x0B..0x31 therefore never appear as statements. FUN_0025bc68 would
 *   index PTR_LAB_0031e228 at a negative offset for them; they are expression
 *   tokens only. This is worth stating plainly because the byte stream looks
 *   like it mixes the two.
 *
 * Stack layout
 * - auStack_90[8] is initialised to 0xFFFFFFFF and is the *result* area the four
 *   sub-expression slots of 0x30/0x31 write into; auStack_70/auStack_60 are the
 *   working stack. puVar6 starts at auStack_70 and is pre-decremented on push,
 *   so puVar6[0] is the top and puVar6[1] the one below it.
 * - Binary operators write puVar6[1] and then post-increment puVar6, i.e. pop
 *   one. Unary operators (0x18, 0x19, 0x1E) rewrite puVar6[0] in place and jump
 *   straight back to the token loop without popping.
 *
 * Parameter semantics
 * - param_1 is a pointer to the destination word. Several callers pass
 *   ((uint)&local | 4), ((uint)&local | 8), ((uint)&local | 0xC) to fill
 *   successive words of one local block with successive expressions -- that is
 *   ordinary pointer arithmetic written oddly by the decompiler, not a flag.
 *
 * Side effects
 * - DAT_00355cd0: the bytecode stream pointer, advanced past everything consumed.
 * - DAT_00355cd8: set to the id of the statement opcode currently being
 *   dispatched from inside an expression (0x100 + next byte for 0xFF-extended).
 *   Handlers that behave differently per opcode read this to find out which
 *   alias they were reached through -- e.g. 0x5E vs 0x5F picking cos vs sin.
 * - uGpffffbd64 (FUN_0025bf70): the literal token id just decoded.
 *
 * PS2 notes
 * - Division and modulo trap(7) on a zero divisor rather than checking; that is
 *   the MIPS divide-by-zero break, so a malformed script is fatal by design.
 * - Ghidra reports a "removing unreachable block" warning in FUN_0025bf70; the
 *   switch is complete as written.
 *
 * Keep unresolved callees and globals under their original labels.
 */

#include <stdint.h>

// Bytecode stream pointer. Two names for one global: FUN_0025c258 and
// FUN_0025bf70 are compiled with different pointer types over it.
extern uint8_t *DAT_00355cd0;
extern uint8_t *pbGpffffbd60;

extern uint32_t DAT_00355cd8;  // opcode id currently dispatching (expression path)
extern uint32_t uGpffffbd64;   // literal token id last decoded
extern uint32_t uGpffffbd68;   // opcode id currently dispatching (statement path)

// Statement dispatch tables, shared with FUN_0025bc68.
extern void *PTR_LAB_0031e228[]; // standard opcodes, indexed by [op - 0x32]
extern void *PTR_LAB_0031e538[]; // extended opcodes, reached via the 0xFF prefix

typedef uint32_t (*script_opcode_handler_t)(void);

/*
 * Literal / composite token decoder.
 *
 * Returns 1 and writes *param_1 when the next token is a literal, 0 when it is
 * not (leaving the stream pointer untouched, so the caller can fall through to
 * the operator switch).
 *
 * Token          Encoding                      Value
 * 0x0C  u8       [0C][b0]                      b0
 * 0x0D  u16      [0D][b0][b1]                  b0 | b1<<8
 * 0x0E  u32      [0E][b0][b1][b2][b3]          little-endian word
 * 0x0F  fixed    [0F][s32]                     s32 * 100
 * 0x10  milli    [10][s16]                     s16 * 1000
 * 0x11  angle    [11][s16]                     s16 * 0xF570 / 0x168
 * 0x30  rgb      [30] expr expr expr           b | g<<8 | r<<16
 * 0x31  rgba     [31] expr expr expr expr      b | g<<8 | r<<16 | a<<24
 *
 * On 0x11: 0xF570 / 0x168 is 62832 / 360, and 62832 is 2*pi scaled by 10000.
 * So 0x11 is a degrees literal converted to the engine's angle units.
 *
 * On 0x0F: the *100 is why world coordinates in the stream read as 1000 for
 * 0.244 units. The literal becomes 100000 and the position opcodes divide by
 * fGpffff8c40, which is the 4096 fixed-point scale times the same 100.
 */
// NOTE: Original signature: undefined4 FUN_0025bf70(uint *param_1)
static uint32_t script_decode_literal(uint32_t *param_1)
{
  const uint32_t token = *pbGpffffbd60;
  uGpffffbd64 = token;

  switch (token)
  {
  case 0x0C:
    *param_1 = pbGpffffbd60[1];
    pbGpffffbd60 += 2;
    return 1;

  case 0x0D:
    *param_1 = (uint32_t)pbGpffffbd60[1] | ((uint32_t)pbGpffffbd60[2] << 8);
    pbGpffffbd60 += 3;
    return 1;

  case 0x0E:
    // Decompiled as a uint3 load plus the top byte; a plain little-endian word.
    *param_1 = (uint32_t)pbGpffffbd60[1] | ((uint32_t)pbGpffffbd60[2] << 8) |
               ((uint32_t)pbGpffffbd60[3] << 16) | ((uint32_t)pbGpffffbd60[4] << 24);
    pbGpffffbd60 += 5;
    return 1;

  case 0x0F:
    *param_1 = (uint32_t)(*(int32_t *)(pbGpffffbd60 + 1) * 100);
    pbGpffffbd60 += 5;
    return 1;

  case 0x10:
    *param_1 = (uint32_t)(*(int16_t *)(pbGpffffbd60 + 1) * 1000);
    pbGpffffbd60 += 3;
    return 1;

  case 0x11:
    *param_1 = (uint32_t)((*(int16_t *)(pbGpffffbd60 + 1) * 0xF570) / 0x168);
    pbGpffffbd60 += 3;
    return 1;

  case 0x30:
  case 0x31:
  {
    uint32_t component[4] = {0, 0, 0, 0};
    pbGpffffbd60 += 1;
    FUN_0025c258(&component[0]);
    FUN_0025c258(&component[1]);
    FUN_0025c258(&component[2]);
    if (token == 0x31)
    {
      FUN_0025c258(&component[3]);
    }
    *param_1 = component[0] | (component[1] << 8) | (component[2] << 16) | (component[3] << 24);
    return 1;
  }

  default:
    return 0;
  }
}

// NOTE: Original signature: void FUN_0025c258(uint *param_1)
void script_evaluate_expression(uint32_t *param_1)
{
  uint32_t results[8];
  uint32_t stack[8];
  uint32_t *top = &stack[4]; // grows downward; puVar6 in the original

  for (int index = 0; index < 8; ++index)
  {
    results[index] = 0xFFFFFFFFu;
  }
  stack[4] = 0;

  for (;;)
  {
    // Statement opcodes appearing inside an expression: dispatch and push the
    // return value. 0xFF selects the extended table and the id becomes
    // 0x100 + the following byte.
    while (*DAT_00355cd0 > 0x31)
    {
      const uint8_t opcode = *DAT_00355cd0;
      script_opcode_handler_t handler;

      if (opcode == 0xFF)
      {
        DAT_00355cd8 = (uint32_t)DAT_00355cd0[1] + 0x100;
        handler = (script_opcode_handler_t)PTR_LAB_0031e538[DAT_00355cd0[1]];
        DAT_00355cd0 += 2;
      }
      else
      {
        DAT_00355cd8 = opcode;
        handler = (script_opcode_handler_t)PTR_LAB_0031e228[opcode - 0x32];
        DAT_00355cd0 += 1;
      }

      --top;
      *top = handler();
    }

    // Literals push; anything else falls through to the operator switch.
    {
      uint32_t literal = 0;
      if (script_decode_literal(&literal) != 0)
      {
        --top;
        *top = literal;
        continue;
      }
    }

    switch (*DAT_00355cd0)
    {
    case 0x0B: // end of expression: pop into the destination
      *param_1 = top[0];
      DAT_00355cd0 += 1;
      return;

    case 0x12: top[1] = (uint32_t)(top[1] == top[0]); break;              // ==
    case 0x13: top[1] = (uint32_t)(top[1] != top[0]); break;              // !=
    case 0x14: top[1] = (uint32_t)((int32_t)top[1] < (int32_t)top[0]); break; // <
    case 0x15: top[1] = (uint32_t)((int32_t)top[0] < (int32_t)top[1]); break; // >
    case 0x16: top[1] = (uint32_t)!((int32_t)top[0] < (int32_t)top[1]); break; // <=
    case 0x17: top[1] = (uint32_t)!((int32_t)top[1] < (int32_t)top[0]); break; // >=

    // Unary: rewrite the top in place, do not pop.
    case 0x18: top[0] = (uint32_t)(top[0] == 0); DAT_00355cd0 += 1; continue; // logical not
    case 0x19: top[0] = ~top[0];                 DAT_00355cd0 += 1; continue; // bitwise not
    case 0x1E: top[0] = (uint32_t)(-(int32_t)top[0]); DAT_00355cd0 += 1; continue; // negate

    case 0x1A: top[1] = (top[1] == 0) ? 0u : (uint32_t)(top[0] != 0); break; // logical and
    case 0x1B:
    case 0x21: top[1] = top[1] | top[0]; break;
    case 0x1C: top[1] = top[1] + top[0]; break;
    case 0x1D: top[1] = top[1] - top[0]; break;
    case 0x1F: top[1] = top[1] ^ top[0]; break;
    case 0x20: top[1] = top[1] & top[0]; break;

    case 0x22: // divide; MIPS break on zero divisor
      if (top[0] == 0)
      {
        trap(7);
      }
      top[1] = (uint32_t)((int32_t)top[1] / (int32_t)top[0]);
      break;

    case 0x23: top[1] = top[1] * top[0]; break;

    case 0x24: // modulo; same trap
      if (top[0] == 0)
      {
        trap(7);
      }
      top[1] = (uint32_t)((int32_t)top[1] % (int32_t)top[0]);
      break;

    default:
      break;
    }

    ++top; // pop one operand
    DAT_00355cd0 += 1;
  }
}
