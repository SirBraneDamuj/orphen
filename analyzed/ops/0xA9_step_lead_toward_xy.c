/*
 * Opcode 0xA9 — step_lead_toward_xy
 *
 * Original: FUN_00262f80
 *
 * Args:
 *   expr dst_x, expr dst_y          (target world position, fixed-point/scaled)
 *   inline byte mode                (0 = walk, otherwise = run)
 *
 * Computes the planar delta from the lead character (DAT_0058bed0/d4) to
 * the target. Mode selects:
 *   walk: speed_cap = 64, motion_state = 11, scale = fGpffff8d2c
 *   run : speed_cap = 128, motion_state = 14, scale = fGpffff8d28
 * The per-frame step is `frame_ticks * speed_cap * scale`.
 *
 * If the remaining distance is < step the lead snaps to target, motion
 * state goes to 1, returns 1 (arrived). Otherwise:
 *   - When the lead has follow-cam tracking flags set
 *     (DAT_0058bf5a & 0x100 and (lead.flags & 0x81000) == 0x81000),
 *     calls npc_sync_follow + audio_play_for_entity to keep the camera /
 *     followers in sync.
 *   - Updates lead facing (DAT_0058bf0c) from the angle of the delta
 *     vector and accumulates step * cos/sin into the lead's per-frame XY
 *     translation accumulators (DAT_0058bee0 / DAT_0058bee4).
 * Returns 0 while still moving.
 */

#include <stdint.h>

extern unsigned char *script_byte_cursor;   /* pcGpffffbd60 */
extern void  script_eval_expression(void *out); /* FUN_0025c258 */

extern float DAT_0058bed0, DAT_0058bed4;        /* lead world XY */
extern float DAT_0058bee0, DAT_0058bee4;        /* lead per-frame XY accumulators */
extern float DAT_0058bf0c;                      /* lead facing angle */
extern short DAT_0058bf50;                      /* lead motion state */
extern unsigned short DAT_0058bf5a;             /* lead motion flags */
extern unsigned int   _DAT_0058beb4;            /* lead flags word */
extern int   iGpffffb64c;                       /* frame ticks */
extern float fGpffff8d24, fGpffff8d28, fGpffff8d2c;

extern float vector_magnitude_xy(float x, float y); /* FUN_00216608 */
extern int   angle_from_vector(float dy, float dx); /* FUN_00305408 */
extern float cos_fixed(int angle);                  /* FUN_00305130 */
extern float sin_fixed(int angle);                  /* FUN_00305218 */
extern unsigned long npc_sync_follow(float dy, void *entity, int mode); /* FUN_00255d88 */
extern void  audio_play_for_entity(unsigned long tag, void *entity);    /* FUN_00267d38 */
extern short DAT_0058beb0;                          /* lead entity base */

int op_0xA9_step_lead_toward_xy(void) {
    int dst_x, dst_y;
    script_eval_expression(&dst_x);
    script_eval_expression(&dst_y);
    char mode = (char)*script_byte_cursor++;

    float delta_x = (float)dst_x / fGpffff8d24 - DAT_0058bed0;
    float delta_y = (float)dst_y / fGpffff8d24 - DAT_0058bed4;

    float speed_cap;
    int   walk_state;
    float step_scale;
    if (mode == 0) {
        speed_cap = 64.0f;  walk_state = 11; step_scale = fGpffff8d2c;
    } else {
        speed_cap = 128.0f; walk_state = 14; step_scale = fGpffff8d28;
    }
    float step = (float)iGpffffb64c * speed_cap * step_scale;

    if ((int)DAT_0058bf50 != walk_state) DAT_0058bf50 = (short)walk_state;

    float distance = vector_magnitude_xy(delta_x, delta_y);
    if (distance < step) {
        DAT_0058bf50 = 1;
        return 1; /* arrived */
    }

    if ((DAT_0058bf5a & 0x100) && (_DAT_0058beb4 & 0x81000) == 0x81000) {
        unsigned long tag = npc_sync_follow(delta_y, &DAT_0058beb0, (int)mode);
        audio_play_for_entity(tag, &DAT_0058beb0);
    }

    int packed_angle = angle_from_vector(delta_y, delta_x);
    DAT_0058bf0c  = (float)packed_angle;
    DAT_0058bee0 += step * cos_fixed(packed_angle);
    DAT_0058bee4 += step * sin_fixed(packed_angle);
    return 0;
}
