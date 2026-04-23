/*
 * Opcode 0x100 — clear_global_param_by_index
 *
 * Original: FUN_002620a8 (LAB before manual function creation)
 *
 * Reads one inline byte from the script stream (pbGpffffbd60) and clears
 * one of six global params:
 *   1 → uGpffffad38
 *   2 → uGpffffad3c
 *   3 → uGpffffad40
 *   4 → uGpffffad44
 *   5 → uGpffffad48
 *   6 → uGpffffad4c
 * Other index values are ignored. Always returns 0.
 *
 * These six globals form a contiguous block at +0xad38..+0xad4c that other
 * opcodes write/read; this is the "reset slot N" handler.
 */

extern unsigned char *puGpffffbd60;  /* script byte cursor */
extern unsigned int   uGpffffad38;
extern unsigned int   uGpffffad3c;
extern unsigned int   uGpffffad40;
extern unsigned int   uGpffffad44;
extern unsigned int   uGpffffad48;
extern unsigned int   uGpffffad4c;

int op_0x100_clear_global_param_by_index(void)
{
    unsigned char idx = *puGpffffbd60;
    puGpffffbd60 += 1;
    switch (idx) {
        case 1: uGpffffad38 = 0; break;
        case 2: uGpffffad3c = 0; break;
        case 3: uGpffffad40 = 0; break;
        case 4: uGpffffad44 = 0; break;
        case 5: uGpffffad48 = 0; break;
        case 6: uGpffffad4c = 0; break;
    }
    return 0;
}
