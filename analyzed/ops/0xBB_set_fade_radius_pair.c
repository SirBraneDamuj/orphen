/*
 * Opcode 0xBB — set_fade_radius_pair
 *
 * Original: FUN_00263db0
 *
 * Reads two integer expressions, scales them by 1/fGpffff8d48 and stores
 * the resulting floats into fGpffffb70c / fGpffffb710 (typically a pair
 * of fade or fog radii). Diagnostic 0x34d2b0 fires if the first scaled
 * value is < 2.0 (clamped lower bound).
 */

extern float fGpffff8d48;
extern float fGpffffb70c, fGpffffb710;
extern void  script_eval_expression(void *out);
extern void  script_diagnostic(unsigned int);

int op_0xBB_set_fade_radius_pair(void) {
    int a, b;
    script_eval_expression(&a);
    script_eval_expression(&b);
    fGpffffb70c = (float)a / fGpffff8d48;
    fGpffffb710 = (float)b / fGpffff8d48;
    if (fGpffffb70c < 2.0f) script_diagnostic(0x34d2b0);
    return 0;
}
