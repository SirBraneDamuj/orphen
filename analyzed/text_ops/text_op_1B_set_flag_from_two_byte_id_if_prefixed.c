/*
 * Opcode 0x1B / 0x1C handler — original label: LAB_00239aa0 (shared)
 *
 * Disassembly (LAB_00239aa0):
 *   v0 = *(gp-0x5140)              ; load current dialogue/script cursor pointer
 *   a2 = 0x1B                      ; constant used for comparison
 *   t0 = *(s8*)v0                  ; read first payload/control byte at cursor
 *   v0 = v0 + 1
 *   *(gp-0x5140) = v0              ; advance cursor by 1
 *   a0 = v0 + 1
 *   a1 = v0 + 2
 *   a3 = *(u8*)v0                  ; low byte of 16-bit id
 *   *(gp-0x5140) = a0              ; advance cursor again (+1)
 *   v1 = *(u8*)(v0+1)              ; high byte
 *   *(gp-0x5140) = a1              ; advance cursor third time (+1) — net +3 from original
 *   v1 <<= 8
 *   a0 = a3 | v1                   ; combined 16-bit id value
 *   if (t0 != 0x1B) branch to fall-through return
 *   else call FUN_002663a0(a0)     ; set bit flag indexed by the 16-bit id
 *
 * Summary:
 *   Consumes three bytes following opcode 0x1B/0x1C. Interprets bytes 1 and 2 after the first
 *   consumed payload byte as a little-endian 16-bit flag id and, ONLY if the first consumed
 *   byte equals 0x1B, invokes the global flag set function FUN_002663a0(id). If the first
 *   consumed byte differs (i.e., 0x1C invocation path), the constructed id is ignored (acts
 *   as a no-op aside from cursor advancement). This explains why both opcode entries 0x1B and
 *   0x1C point to the same code: opcode byte selects entry; first payload byte gates the
 *   flag-set side effect allowing a dual-form or possibly redundancy / versioning scheme.
 *
 * Side Effects:
 *   - Advances dialogue cursor (gp-0x5140) by 3 bytes.
 *   - Optionally sets a global bit flag via FUN_002663a0(flag_id) when payload[0] == 0x1B.
 *
 * Open Questions:
 *   - Are 0x1B and 0x1C script opcodes that both route here, or is the first payload byte
 *     actually the opcode byte (making upstream dispatch treat subsequent byte as first
 *     parameter)? Given dispatch indexing, 0x1B/0x1C already selected this handler; thus the
 *     re-check for 0x1B suggests the first parameter acts as a sub-op selector (0x1B = set flag,
 *     other values including 0x1C = skip). Need to correlate with raw script dumps to confirm.
 *   - Endianness confirmation: shifting second of the two data bytes by 8 implies little-endian
 *     16-bit composition (low then high) which is consistent with other parameter loaders.
 *
 * References:
 *   - FUN_002663a0: sets bit in global flag bitmap ((id>>3) index, 1<<(id&7)). Used in many
 *     systems for enabling features/states.
 *   - Shared cursor global at gp-0x5140 (pending formal name; see other opcode analyses).
 */

// Flag-setting bitfield function (extern until named).
extern void FUN_002663a0(unsigned int id);

// Dialogue/script cursor global (gp-0x5140). Using int as raw address.
extern int GP_NEG_0x5140;

void text_op_1B_set_flag_from_two_byte_id_if_prefixed(void)
{
  int cursor = GP_NEG_0x5140;                     // base pointer
  unsigned char first = *(unsigned char *)cursor; // potential sub-op selector
  cursor += 1;
  GP_NEG_0x5140 = cursor; // advance 1

  unsigned char low = *(unsigned char *)cursor; // low byte of id
  cursor += 1;
  GP_NEG_0x5140 = cursor;                        // advance 2
  unsigned char high = *(unsigned char *)cursor; // high byte of id
  cursor += 1;
  GP_NEG_0x5140 = cursor; // advance 3 total

  unsigned int flag_id = (unsigned int)low | ((unsigned int)high << 8);
  if (first == 0x1B)
  {
    FUN_002663a0(flag_id);
  }
}
