/*
 * Opcode 0xD3 — write_inline_uint_le
 *
 * Original: FUN_002648f8
 *
 * Args: expr `byte_ptr_offset`, inline byte `length` (1..4; >4 → diag
 * 0x34d388), expr `value`. Writes the low `length` bytes of `value`
 * into script-data memory at (byte_ptr_offset + script_code_base) in
 * little-endian order. The mirror image of 0xD2.
 */

#include <stdint.h>

extern unsigned char *pcGpffffbd60;
extern int            iGpffffb0e8;
extern void           script_eval_expression(void *out);
extern void           script_diagnostic(unsigned int);

int op_0xD3_write_inline_uint_le(void) {
    unsigned char *dst;
    unsigned int   value;
    script_eval_expression(&dst);
    char length = (char)*pcGpffffbd60++;
    script_eval_expression(&value);
    dst = (unsigned char *)((intptr_t)dst + iGpffffb0e8);

    unsigned char remaining = (unsigned char)(length - 1);
    if (remaining >= 4) script_diagnostic(0x34d388);
    while (remaining != 0xFF) {
        *dst++ = (unsigned char)value;
        value >>= 8;
        remaining--;
    }
    return 0;
}
