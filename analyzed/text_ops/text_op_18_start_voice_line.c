// Text opcode 0x18 / 0x19 — LAB_00239a30 (shared handler)
// Raw MIPS (abridged):
//   a1 = *(gp-0x5140)          ; cursor
//   a0 = 0x19                  ; constant for comparison
//   v1 = *(int8_t*)a1          ; first byte
//   a1 += 1; store back to gp-0x5140
//   v0 = a1 + 1 (tentative new cursor)
//   a2 = *(int8_t*)a1          ; second byte candidate
//   if (v1 != 0x19) goto skip_store
//     v1 = *(int8_t*)(a1+1)    ; read third byte
//     v0 = a1 + 2
//     gp-0x5140 = v0           ; advance by extra byte
//     *(gp-0x4999) = v1        ; store control byte
// skip_store:
//   (falls through and returns; gp-0x5140 already advanced by 1 or 2 total)
//
// Behavior:
//   Consumes at least one byte from the dialogue control stream. If that first byte equals 0x19, it treats the
//   second byte as a control value destination and advances cursor by an additional byte while writing that
//   value to a gp-relative byte global at gp-0x4999. If the first byte is anything else, it simply advances
//   past the first parameter byte (cursor +1) and does not modify the gp-0x4999 global.
//
// Notes:
//   - The opcode index (0x18 or 0x19) does not alter behavior; both entries point to the same label.
//   - The internal sentinel constant 0x19 suggests that opcode 0x18 may sometimes carry an inline marker (0x19)
//     indicating presence of a following value; otherwise it acts as a no-op / single-byte skip.
//   - The destination byte at gp-0x4999 is presently unnamed; pending broader cross-reference to assign a
//     semantic (likely a small mode flag or channel selector updated mid-stream).
//
// Side effects:
//   - Advances dialogue cursor by 1 or 2 bytes.
//   - Optionally updates one global byte (gp-0x4999).
//   - No glyph slot creation or timing changes directly.
//
// TODO:
//   - Identify readers of the gp-0x4999 byte to characterize its effect.
//   - Confirm whether sentinel 0x19 ever appears outside this opcode.
//   - Introduce a helper for cursor manipulation once multiple opcodes share this pattern.

#include <stdint.h>

extern int GP_NEG_0x5140;           // dialogue control cursor (pending confirmed name)
extern unsigned char GP_NEG_0x4999; // unknown control byte target (to be named)

void text_op_18_set_control_byte_conditional(void)
{
  int cursor = GP_NEG_0x5140;
  unsigned char first = *(unsigned char *)cursor;
  cursor += 1;
  GP_NEG_0x5140 = cursor; // base advance

  if (first == 0x19)
  {
    unsigned char value = *(unsigned char *)cursor; // second byte
    cursor += 1;
    GP_NEG_0x5140 = cursor; // extra advance
    GP_NEG_0x4999 = value;  // update control byte
  }
  // else: only single-byte consumed
}

// Alias wrapper for clarity if needed by dispatcher for opcode 0x19 (both map to same label originally)
void text_op_19_set_control_byte_conditional(void)
{
  text_op_18_set_control_byte_conditional();
}
