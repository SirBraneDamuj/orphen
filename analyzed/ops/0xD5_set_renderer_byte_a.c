/*
 * Opcode 0xD5 — set_renderer_byte_a
 *
 * Original: FUN_00264d40
 *
 * Reads one expression and stores its low byte into uGpffffb084 (a
 * one-byte renderer/HUD state register).
 */

extern unsigned char uGpffffb084;
extern void script_eval_expression(void *out);

int op_0xD5_set_renderer_byte_a(void) {
    unsigned char buf[16];
    script_eval_expression(buf);
    uGpffffb084 = buf[0];
    return 0;
}
