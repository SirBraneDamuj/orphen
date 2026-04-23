/*
 * Opcode 0xAA — abort_lead_motion
 *
 * Original: FUN_00263118
 *
 * Calls entity_begin_motion_stop(lead) (FUN_00252d88) and clears the
 * coroutine boot vector at *(script_data_work_ptr + 0x100). Cancels any
 * lead motion currently driven by 0xA9.
 */

#include <stdint.h>

extern void entity_begin_motion_stop(void *entity); /* FUN_00252d88 */
extern int  script_data_work_ptr;                   /* DAT_00355cf4 */
extern short DAT_0058beb0;

int op_0xAA_abort_lead_motion(void) {
    entity_begin_motion_stop(&DAT_0058beb0);
    *(int *)(script_data_work_ptr + 0x100) = 0;
    return 0;
}
