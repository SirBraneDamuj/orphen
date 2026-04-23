/*
 * Opcode 0xDB — invoke_save_or_resource_op
 *
 * Original: FUN_00264ec8
 *
 * No script args. Calls FUN_00266fc0 (un-analyzed; appears to be a
 * save-state / resource refresh routine called from script).
 */

extern void FUN_00266fc0(void);

int op_0xDB_invoke_save_or_resource_op(void) {
    FUN_00266fc0();
    return 0;
}
