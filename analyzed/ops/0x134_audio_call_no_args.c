/*
 * Opcode 0x134 — audio_call_no_args
 *
 * Original: FUN_002617c0
 *
 * Forwards to FUN_00206c28 with no arguments.
 */

extern void FUN_00206c28(void);

void op_0x134_audio_call_no_args(void) { FUN_00206c28(); }
