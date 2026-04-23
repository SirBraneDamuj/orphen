/*
 * Opcode 0x132 — audio_call_with_offset_mode1
 *
 * Original: FUN_00261700
 *
 * Reads one expr `mode` (must be <=2 else diagnostic at 0x34d0d8) and
 * one inline IP offset, then forwards to FUN_00206ae0(offset, mode, 1).
 */

extern void script_eval_expression(void *out);
extern unsigned long script_read_ip_offset(void); /* FUN_0025c1d0 */
extern void script_diagnostic(unsigned int msg, unsigned int v);
extern void FUN_00206ae0(unsigned long offset,
                         unsigned int mode, int flag);

unsigned int op_0x132_audio_call_with_offset_mode1(void)
{
  unsigned int v[4];
  script_eval_expression(v);
  unsigned long off = script_read_ip_offset();
  if (v[0] > 2)
    script_diagnostic(0x34d0d8, v[0]);
  FUN_00206ae0(off, v[0], 1);
  return 0;
}
