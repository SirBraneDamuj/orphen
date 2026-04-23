/*
 * Opcode 0x133 — audio_call_with_offset_mode0
 *
 * Original: FUN_00261760
 *
 * Same as 0x132 but trailing flag is 0.
 */

extern void          script_eval_expression(void *out);
extern unsigned long script_read_ip_offset(void);
extern void          script_diagnostic(unsigned int msg, unsigned int v);
extern void          FUN_00206ae0(unsigned long offset,
                                  unsigned int mode, int flag);

unsigned int op_0x133_audio_call_with_offset_mode0(void) {
    unsigned int  v[4];
    script_eval_expression(v);
    unsigned long off = script_read_ip_offset();
    if (v[0] > 2) script_diagnostic(0x34d0d8, v[0]);
    FUN_00206ae0(off, v[0], 0);
    return 0;
}
