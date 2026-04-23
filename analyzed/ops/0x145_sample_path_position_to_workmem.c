/*
 * Opcode 0x145 — sample_path_position_to_workmem
 *
 * Original: FUN_00265620
 *
 * Args: expr num, expr den, expr slot_x, expr slot_y, expr slot_z.
 * Each slot must be <=0x80 (else diagnostic at 0x34d450).
 *
 * Calls FUN_00266ce8(num/den, 0x571e70, &out[3]) to sample the path
 * loaded by 0x144 at parameter t = num/den. Stores the three components
 * — each multiplied by DAT_00352cdc and round-converted via FUN_0030bd20
 * — into work_mem[slot*4].
 */

extern void          script_eval_expression(void *out);
extern int           DAT_00355060;
extern float         DAT_00352cdc;
extern void          script_diagnostic(unsigned int msg_addr);
extern void          FUN_00266ce8(float t, unsigned int sink, float out[3]);
extern unsigned int  FUN_0030bd20(float v);

unsigned int op_0x145_sample_path_position_to_workmem(void) {
    int          num, den;
    unsigned int slot_x, slot_y, slot_z;
    script_eval_expression(&num);
    script_eval_expression(&den);
    script_eval_expression(&slot_x);
    script_eval_expression(&slot_y);
    script_eval_expression(&slot_z);
    if (slot_x > 0x80 || slot_y > 0x80 || slot_z > 0x80)
        script_diagnostic(0x34d450);
    float scale = DAT_00352cdc;
    float out[3];
    FUN_00266ce8((float)num / (float)den, 0x571e70, out);
    *(unsigned int *)(slot_x * 4 + DAT_00355060) = FUN_0030bd20(out[0] * scale);
    *(unsigned int *)(slot_y * 4 + DAT_00355060) = FUN_0030bd20(out[1] * scale);
    *(unsigned int *)(slot_z * 4 + DAT_00355060) = FUN_0030bd20(out[2] * scale);
    return 0;
}
