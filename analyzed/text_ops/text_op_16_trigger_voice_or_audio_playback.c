// Text opcode 0x16 — LAB_00239990
// Raw MIPS sequence (simplified):
//   v1 = *(gp-0x5140)
//   *(gp-0x5140) = v1 + 1  (then repeatedly rewritten with v1+2, v1+3, v1+4, v1+5, v1+6, v1+7)
//   a1 = *(int8_t*)(v1+1)
//   a2 = *(int8_t*)(v1+2)
//   t0 = byte3 | (byte4<<8) | (byte5<<16) | (byte6<<24)  (bytes at offsets 3..6)
//   a0 = t0 (packed 32-bit value)
//   jump FUN_00206ae0(a0, a1, a2)
//
// Behavior:
//   - Reads a base pointer/cursor from global (gp-0x5140), interprets a parameter block at that address:
//       offset +1 : param_2 (signed byte) → becomes second argument to FUN_00206ae0
//       offset +2 : param_3 (signed byte) → becomes third argument (promoted long)
//       offsets +3..+6 : four bytes combined little-endian into a 32-bit id → first argument
//   - Writes successive interim values (base+1 .. base+7) back into the same global pointer location, but the
//     final stored value after the sequence is base+7 (the last store). Net effect: advance stream pointer by 7.
//   - Calls FUN_00206ae0(id, channel_or_index, wait_flag) with:
//       id      = composed 32-bit from 4 bytes (resource / audio asset id)
//       param_2 = channel / slot index (0..2 enforced inside FUN_00206ae0; it checks param_2 < 3)
//       param_3 = wait flag (0 or 1) controlling synchronous wait loop (FUN_00206c28) if non-zero
//
// FUN_00206ae0 summary (see raw decompile):
//   - Validates param_2 (must be <=2).
//   - Maintains arrays at &DAT_00356480 / &DAT_00356490 (per-channel cached id and maybe handle/pos).
//   - If same id already loaded for that channel, returns immediately.
//   - Otherwise triggers resource request via FUN_00223698 (with size 0x18f9a00), stores handle, sets a busy flag.
//   - If param_3 != 0, busy-waits invoking FUN_00206c28 and FUN_00203b20 until load completion.
//
// Interpretation:
//   Opcode 0x16 embeds an audio/voice (or large asset) load/ensure instruction in the dialogue text stream.
//   The packed 32-bit value is a resource identifier; the second byte selects a channel (0..2); the third byte
//   acts as a sync/wait indicator (non-zero means wait for completion before continuing text processing).
//
// Side effects:
//   - Advances dialogue/script cursor global at gp-0x5140 by 7 bytes.
//   - Initiates (and optionally blocks for) asset load through FUN_00206ae0.
//   - No glyph slot creation.
//
// Outstanding tasks:
//   - Name gp-0x5140 global (e.g., dialogue_control_cursor) once confirmed across other opcodes.
//   - Introduce structured parsing helper for multi-byte parameter extraction to reduce repeated pointer stores.
//
// Original label preserved; implemented as a wrapper for clarity.

#include <stdint.h>

extern int GP_NEG_0x5140; // dialogue control cursor (pending confirmation)
extern unsigned int FUN_00206ae0(int id, int channel, long wait_flag);

void text_op_16_trigger_voice_or_audio_playback(void)
{
  int base = GP_NEG_0x5140;
  // Extract parameters
  int channel = *(int8_t *)(base + 1);
  int wait_flag = *(int8_t *)(base + 2);
  unsigned int b3 = *(unsigned char *)(base + 3);
  unsigned int b4 = *(unsigned char *)(base + 4);
  unsigned int b5 = *(unsigned char *)(base + 5);
  unsigned int b6 = *(unsigned char *)(base + 6);
  int resource_id = (int)(b3 | (b4 << 8) | (b5 << 16) | (b6 << 24));

  // Advance cursor by 7 (final stored value after sequence). Original code redundantly stored each +n.
  GP_NEG_0x5140 = base + 7;

  // Invoke loader
  FUN_00206ae0(resource_id, channel, wait_flag);
}

// NOTE: Original LAB_00239990 ends with a jump to FUN_00206ae0 (tail call). We wrap for dispatch uniformity.
