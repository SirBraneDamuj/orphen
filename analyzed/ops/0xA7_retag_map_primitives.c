/*
 * Opcode 0xA7 — retag_map_primitives
 *
 * Original: FUN_00261fd8
 *
 * Reads two expressions: a 32-bit `mask` and a `tag_nibble`. Walks the PSM2
 * collision mesh — base iGpffffb740 (DAT_003556B0), count iGpffffb718
 * (DAT_00355688), stride 0x78 — and for every primitive whose +0x04 terrain
 * flag word has any bit of `mask` set, replaces the top nibble of that word:
 *
 *   flags = (flags & 0x0FFFFFFF) | (tag_nibble << 28)
 *
 * This is a bulk surface-material change, not an entity operation. The 0x78
 * records are the map's, the same ones FUN_00227840's ground scan walks, and
 * +0x04 is the port's DRecord78::terrainFlags — the word the ground query tests
 * against an entity's reject mask, and the one FUN_00253080 reads the surface
 * class out of (`& 0xF0000000`, 0xD being the drift surface). See
 * analyzed/ops/0xCC_get_lead_terrain_flags.c, which reads the same field.
 *
 * In s01_e012 this is how the hold becomes the flooded hold.
 *
 * (This file used to describe the array as the active-entity pool. It is not;
 * the entity pool is stride 0x1D8.)
 */

#include <stdint.h>

extern int   iGpffffb718;        /* collision primitive count */
extern int   iGpffffb740;        /* collision primitive base, stride 0x78 */
extern void  script_eval_expression(void *out); /* FUN_0025c258 */

int op_0xA7_retag_map_primitives(void) {
    struct { unsigned int mask; int tag_nibble; } args;
    script_eval_expression(&args.mask);
    script_eval_expression(&args.tag_nibble);

    int            remaining  = iGpffffb718;
    unsigned char *primitive  = (unsigned char *)(intptr_t)iGpffffb740;
    while (remaining-- > 0) {
        unsigned int *flags = (unsigned int *)(primitive + 4);
        if (*flags & args.mask) {
            *flags = (*flags & 0x0FFFFFFFu) |
                     ((unsigned int)args.tag_nibble << 28);
        }
        primitive += 0x78;
    }
    return 0;
}
