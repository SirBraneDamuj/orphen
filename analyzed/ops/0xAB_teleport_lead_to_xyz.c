/*
 * Opcode 0xAB — teleport_lead_to_xyz
 *
 * Original: FUN_00263148
 *
 * Bytecode layout: inline byte `mode` (0 = also detach camera), then three
 * expressions x/y/z. Each coordinate is divided by DAT_00352ca0 (4096.0)
 * to convert into world units. When mode == 0 calls camera_detach_tracking
 * (FUN_00257fc0) before teleporting both lead and camera to the new spot
 * via teleport_lead_and_camera (FUN_002582d0).
 */

extern unsigned char *script_byte_cursor;       /* DAT_00355cd0 */
extern void  script_eval_expression(void *out); /* FUN_0025c258 */
extern void  camera_detach_tracking(void);      /* FUN_00257fc0 */
extern void  teleport_lead_and_camera(float x, float y, float z); /* FUN_002582d0 */
extern float DAT_00352ca0;

int op_0xAB_teleport_lead_to_xyz(void) {
    char mode = (char)*script_byte_cursor++;
    int  x, y, z;
    script_eval_expression(&x);
    script_eval_expression(&y);
    script_eval_expression(&z);
    if (mode == 0) camera_detach_tracking();
    teleport_lead_and_camera((float)x / DAT_00352ca0,
                             (float)y / DAT_00352ca0,
                             (float)z / DAT_00352ca0);
    return 0;
}
