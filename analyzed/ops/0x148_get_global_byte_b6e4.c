/*
 * Opcode 0x148 — get_global_byte_b6e4
 *
 * Original: FUN_00265788 (LAB before manual function creation)
 *
 * Returns the current value of uGpffffb6e4 (read-only accessor).
 */

extern unsigned char uGpffffb6e4;

unsigned char op_0x148_get_global_byte_b6e4(void)
{
    return uGpffffb6e4;
}
