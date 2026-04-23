/*
 * Opcodes 0x127 / 0x128 — audio_play_positional (shared FUN_002613d8)
 *
 * Reads one inline 16-bit tag, then 3 coords scaled by DAT_00352c38
 * (or DAT_00352c3c for 0x128). 0x127 uses a fixed volume of 100;
 * 0x128 reads an additional volume expression. Forwards to
 * FUN_00267a80(x, y, z, tag, volume).
 */

#include <stdint.h>

extern short          DAT_00355cd8;
extern unsigned char *DAT_00355cd0;
extern float          DAT_00352c38, DAT_00352c3c;
extern void           script_eval_expression(void *out);
extern void           FUN_00267a80(float x, float y, float z,
                                   int tag, unsigned int volume);

unsigned int op_0x127_0x128_audio_play_positional(void) {
    short opcode = DAT_00355cd8;
    int   tag    = (int)((((unsigned int)DAT_00355cd0[0]
                          + (unsigned int)DAT_00355cd0[1] * 0x100) << 16)) >> 16;
    DAT_00355cd0 += 2;
    int  x, y, z;
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    if (opcode == 0x127) {
        FUN_00267a80((float)x / DAT_00352c38,
                     (float)y / DAT_00352c38,
                     (float)z / DAT_00352c38, tag, 100);
    } else {
        unsigned int vol;
        script_eval_expression(&vol);
        FUN_00267a80((float)x / DAT_00352c3c,
                     (float)y / DAT_00352c3c,
                     (float)z / DAT_00352c3c, tag, vol);
    }
    return 0;
}
