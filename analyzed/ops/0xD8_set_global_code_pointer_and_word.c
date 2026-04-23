/*
 * Opcode 0xD8 — set_global_code_pointer_and_word
 *
 * Original: FUN_00264de8
 *
 * Reads an ip-relative offset, then one expression `value`. Stores
 *   iGpffffb0b8 = offset + script_code_base
 *   uGpffffb0bc = value (low short)
 * Used to register a script-relative resource pointer + tag pair
 * consulted later by the game loop.
 */

extern int            iGpffffb0b8;
extern unsigned short uGpffffb0bc;
extern int            iGpffffb0e8;
extern int            script_read_ip_offset(void);
extern void           script_eval_expression(void *out);

int op_0xD8_set_global_code_pointer_and_word(void) {
    unsigned short value[8];
    int rel = script_read_ip_offset();
    int ptr = rel + iGpffffb0e8;
    script_eval_expression(value);
    iGpffffb0b8 = ptr;
    uGpffffb0bc = value[0];
    return 0;
}
