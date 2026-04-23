/*
 * Opcode 0xD6 — set_renderer_byte_b
 *
 * Original: FUN_00264d68
 *
 * Reads one expression and stores its low byte into uGpffffb661 (a
 * one-byte renderer/text state register).
 */

extern unsigned char uGpffffb661;
extern void script_eval_expression(void *out);

int op_0xD6_set_renderer_byte_b(void) {
    unsigned char buf[16];
    script_eval_expression(buf);
    uGpffffb661 = buf[0];
    return 0;
}
