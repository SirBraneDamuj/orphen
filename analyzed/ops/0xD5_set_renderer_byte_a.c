/*
 * Opcode 0xD5 — set_renderer_byte_a
 *
 * Original: FUN_00264d40
 *
 * Reads one expression and stores its low byte into uGpffffb084
 * (DAT_00354FF4). FUN_00251cd0 resets it to 1 at scene load.
 *
 * Not a renderer byte: the only reader is FUN_002d58e8, type 0x6B's
 * behaviour, which adds (uGpffffb084 != 0) into the animation id it picks
 * (0x33..0x3E). s03_e001's forest-fire cutscene writes 0 going in, 1 coming
 * out.
 */

extern unsigned char uGpffffb084;
extern void script_eval_expression(void *out);

int op_0xD5_set_renderer_byte_a(void) {
    unsigned char buf[16];
    script_eval_expression(buf);
    uGpffffb084 = buf[0];
    return 0;
}
