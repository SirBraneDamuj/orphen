/*
 * Opcode 0x13E — set_global_byte_b655_ff
 *
 * Original: FUN_00265468 (LAB before manual function creation)
 *
 * Unconditionally sets uGpffffb655 = 0xFF. Always returns 0. Likely a
 * "force flag on" trigger for a renderer/audio toggle inspected elsewhere.
 */

extern unsigned char uGpffffb655;

int op_0x13E_set_global_byte_b655_ff(void)
{
    uGpffffb655 = 0xff;
    return 0;
}
