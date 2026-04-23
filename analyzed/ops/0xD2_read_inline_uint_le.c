/*
 * Opcode 0xD2 — read_inline_uint_le
 *
 * Original: FUN_00264858
 *
 * Reads expr `byte_ptr_offset` (offset added to script_code_base
 * iGpffffb0e8 to get a pointer into script data), then one inline byte
 * `length` (1..4; >4 → diag 0x34d368). Reads `length` bytes
 * little-endian from the resolved pointer and returns the assembled
 * unsigned integer.
 */

#include <stdint.h>

extern unsigned char *pcGpffffbd60; /* script_byte_cursor */
extern int iGpffffb0e8;             /* script_code_base */
extern void script_eval_expression(void *out);
extern void script_diagnostic(unsigned int);

unsigned long op_0xD2_read_inline_uint_le(void)
{
  unsigned char *ptr_arr[4];
  script_eval_expression(ptr_arr);
  char length = (char)*pcGpffffbd60++;
  ptr_arr[0] = (unsigned char *)((intptr_t)ptr_arr[0] + iGpffffb0e8);

  unsigned char remaining = (unsigned char)(length - 1);
  if (remaining >= 4)
  {
    script_diagnostic(0x34d368);
    return 0;
  }
  unsigned long value = 0;
  int shift = 0;
  while (remaining != 0xFF)
  {
    unsigned char b = *ptr_arr[0]++;
    value += (unsigned long)b << shift;
    shift += 8;
    remaining--;
  }
  return value;
}
