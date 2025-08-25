/*
 * Opcode 0x1A — original label: LAB_00239a70
 *
 * Disassembly summary:
 *   prologue save RA
 *   v0 = FUN_00206a90()          ; returns byte DAT_00356788 (audio/system ready flag)
 *   if (v0 != 0) return (do not advance cursor)
 *   else: cursor = *(gp-0x5140); cursor += 1; store back (advance by one byte)
 *   epilogue restore RA, return
 *
 * Interpretation:
 *   Wait/poll opcode conditioned on an audio/system status byte (DAT_00356788). Unlike opcode 0x17
 *   which advances the cursor only when the polled function returns non-zero (signaling completion),
 *   this opcode advances when FUN_00206a90 returns zero. So 0x1A appears to represent a wait-until-clear
 *   (advance on 0) vs 0x17’s wait-until-set (advance on 1). The two form complementary gating primitives.
 *
 * Behavior:
 *   - If DAT_00356788 (FUN_00206a90) is non-zero: no cursor movement (re-run next tick).
 *   - If zero: advance dialogue control cursor (gp-0x5140) by one byte and return.
 *
 * Side Effects:
 *   - Conditional +1 advance to global cursor GP_NEG_0x5140.
 *   - No other state mutation.
 *
 * Open Questions:
 *   - Exact semantic name of DAT_00356788: likely an audio channel busy flag, streaming busy, or voice queue active marker.
 *   - Relationship to opcodes 0x16/0x17 sequencing; scripts may combine 0x1A to wait for channel idle before issuing a new load (0x16).
 *
 * References:
 *   - FUN_00206a90: trivial getter for DAT_00356788.
 *   - Cursor global at gp-0x5140 (see other opcode analyses for pending naming).
 */

extern unsigned char FUN_00206a90(void); // returns DAT_00356788
extern int GP_NEG_0x5140;                // dialogue control cursor

void text_op_1A_wait_on_audio_system_flag(void)
{
  if (FUN_00206a90() == 0)
  {
    GP_NEG_0x5140 += 1; // advance only when flag clear
  }
}
