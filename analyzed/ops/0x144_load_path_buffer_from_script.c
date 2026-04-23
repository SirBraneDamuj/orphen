/*
 * Opcode 0x144 — load_path_buffer_from_script
 *
 * Original: FUN_002654f8
 *
 * Reads two exprs (offset, mode), then `step = FUN_0025b9c0()` to align
 * to the inline payload. Calls FUN_0025d618(offset+step, 0x1849a00) to
 * blit the script payload into a fixed buffer at 0x01849A00. The first
 * dword there is the count `n` (capped at 0x10 with a diagnostic at
 * 0x34d420). The next n*3 ints are read as int triples and converted to
 * floats via /1000.0 into 0x01889A00. Finally calls
 * FUN_00266a78(0x571e70, 0x1889a00, n, mode).
 */

#include <stdint.h>

extern void script_eval_expression(void *out);
extern int FUN_0025b9c0(void);
extern void FUN_0025d618(int dest_offset, unsigned int dst_addr);
extern void script_diagnostic(unsigned int msg_addr);
extern void FUN_00266a78(unsigned int sink, unsigned int floats,
                         unsigned int count, unsigned int mode);
extern unsigned int DAT_01849a00;
extern int DAT_01849a04;

unsigned int op_0x144_load_path_buffer_from_script(void)
{
  float *fp = (float *)0x01889a00;
  int offset;
  unsigned int mode;
  script_eval_expression(&offset);
  script_eval_expression(&mode);
  int step = FUN_0025b9c0();
  int *src = &DAT_01849a04;
  offset += step;
  FUN_0025d618(offset, 0x01849a00);
  unsigned int n = DAT_01849a00;
  if (n > 0x10)
    script_diagnostic(0x34d420);
  unsigned int total = n * 3;
  for (unsigned int i = 0; i < total; i += 3)
  {
    int a = src[0], b = src[1], c = src[2];
    src += 3;
    fp[0] = (float)a / 1000.0f;
    fp[1] = (float)b / 1000.0f;
    fp[2] = (float)c / 1000.0f;
    fp += 3;
  }
  FUN_00266a78(0x571e70, 0x1889a00, n, mode);
  return 0;
}
