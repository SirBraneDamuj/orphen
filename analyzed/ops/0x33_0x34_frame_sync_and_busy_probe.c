/*
 * Opcodes 0x33, 0x34 — frame sync + system-busy probe
 *
 * 0x33 FUN_0025d6f8 advance_ip_and_sync_frame:
 *   Reads 4 bytes from DAT_00355cd0 (script IP), calls FUN_00237b38 with IP+4
 *   (installs a script-driven sync/wait handler) and then FUN_0025c220 to
 *   finalize/advance the interpreter. Effectively "yield until handler fires".
 *
 * 0x34 FUN_0025d728 probe_system_busy:
 *   Thin wrapper around FUN_00237c60. Returns whether the dialogue/stream
 *   subsystem reports busy (see query_dialogue_stream_complete 0x35 for the
 *   related dialogue predicate).
 */

extern void FUN_00237b38(void *);
extern void FUN_0025c220(void);
extern void FUN_00237c60(void);
extern unsigned char *DAT_00355cd0; /* script instruction pointer */

void op_0x33_advance_ip_and_sync_frame(void)
{
  FUN_00237b38(DAT_00355cd0 + 4);
  FUN_0025c220();
}

void op_0x34_probe_system_busy(void) { FUN_00237c60(); }
