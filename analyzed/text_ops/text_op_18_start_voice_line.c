// Text opcodes 0x18 and 0x19 — LAB_00239a30 (shared handler)
//
// **START THE VOICE LINE.** An earlier reading of this handler called it a
// "conditional control byte set" that consumed a byte and returned. It does
// consume the byte — and then tail-calls the voice player with it. The last two
// instructions were missing from that reading, and they are the whole point:
//
//   00239a30: 8f85aec0  lw    a1, -0x5140(gp)   ; a1 = stream cursor
//   00239a34: 24040019  li    a0, 0x19
//   00239a38: 80a30000  lb    v1, 0(a1)         ; v1 = the opcode byte itself
//   00239a3c: 24a50001  addiu a1, a1, 1
//   00239a40: af85aec0  sw    a1, -0x5140(gp)   ; cursor += 1
//   00239a44: 24a20001  addiu v0, a1, 1
//   00239a48: 80a60000  lb    a2, 0(a1)         ; a2 = the operand byte
//   00239a4c: 14640005  bne   v1, a0, 00239a64  ; opcode 0x19?
//   00239a50: af82aec0  sw    v0, -0x5140(gp)   ; delay slot: cursor += 1 again
//   00239a54: 80a30001  lb    v1, 1(a1)
//   00239a58: 24a20002  addiu v0, a1, 2
//   00239a5c: af82aec0  sw    v0, -0x5140(gp)   ; 0x19 only: cursor += 1 more
//   00239a60: a383b667  sb    v1, -0x4999(gp)
//   00239a64: 08081b66  j     0x00206d98        ; FUN_00206d98(channel)
//   00239a68: 00c0202d  daddu a0, a2, zero      ; delay slot: a0 = operand byte
//
// The store in the delay slot at 00239a50 runs whichever way the branch goes, so
// both opcodes advance at least 2. Totals:
//
//   0x18 <channel>          cursor += 2
//   0x19 <channel> <byte>   cursor += 3, and <byte> goes to gp-0x4999
//
// The operand is a channel index, 0..2, matching FUN_00206ae0's three cached
// slots. FUN_00206d98 refuses the call unless DAT_00356788 is clear (nothing
// already playing) and DAT_00356480[channel] is non-zero (a clip is cached
// there); otherwise it programs the reserved streaming voice through
// FUN_00207010, which sets DAT_00356788 to 1.
//
// == Why this is the pacing of every cutscene ==
//
// A dialogue record is built as:
//
//   13 <name> 00      speaker
//   17                wait out the load the previous record started
//   16 ch w id32      cache the clip for the NEXT record on the other channel
//   18 ch             start the clip cached for THIS record
//   <text>
//   1a                block until DAT_00356788 falls back to zero
//
// so the stream double-buffers, and what a record plays was armed a record
// earlier. Confirmed against eeMemory.bin, which was taken during s01_e012's
// opening: DAT_00356480 reads {50, 79, 0} while record 1 is up — channel 1 holds
// what record 1 is speaking, channel 0 what record 2 will speak.
//
// Walking all 83 records of scr2.out this way, every one has exactly one
// 0x18/0x19 and exactly one 0x1A, and the channel it starts is always already
// armed. The clip's length is therefore the record's hold, and it comes from
// VOICE.BIN's table (FUN_00221c40) with no audio needed to read it.
//
// Side effects:
//   - Advances the dialogue cursor by 2 (0x18) or 3 (0x19).
//   - 0x19 only: stores one byte to gp-0x4999.
//   - Tail-calls FUN_00206d98, which may start the reserved SPU2 voice.
//
// Open:
//   - gp-0x4999's readers are still unidentified, so what distinguishes 0x19
//     from 0x18 beyond that store is unknown. s01_e012 uses 0x19 once.

#include <stdint.h>

extern unsigned char *GP_NEG_0x5140; // dialogue control cursor
extern unsigned char GP_NEG_0x4999;  // unknown; only 0x19 writes it
extern long FUN_00206d98(int channel);

void text_op_18_start_voice_line(void)
{
  unsigned char *cursor = GP_NEG_0x5140;
  const unsigned char opcode = cursor[0];
  const unsigned char channel = cursor[1];

  GP_NEG_0x5140 = cursor + 2;
  if (opcode == 0x19)
  {
    GP_NEG_0x4999 = cursor[2];
    GP_NEG_0x5140 = cursor + 3;
  }

  FUN_00206d98(channel);
}

// 0x19 shares the label; the handler branches on the opcode byte it re-reads.
void text_op_19_start_voice_line_with_byte(void)
{
  text_op_18_start_voice_line();
}
