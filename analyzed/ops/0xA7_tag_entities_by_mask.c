/*
 * Opcode 0xA7 — tag_entities_by_mask
 *
 * Original: FUN_00261fd8
 *
 * Reads two expressions: a 32-bit `mask` and a `tag_nibble` (used as the
 * top 4 bits of a flag word). Walks the active-entity array (base
 * iGpffffb740, count iGpffffb718, stride 0x78) and for every entity whose
 * +4 flag word has any bit of `mask` set, replaces the top nibble of that
 * flag word with `tag_nibble`.
 *
 *   flag = (flag & 0x0FFFFFFF) | (tag_nibble << 28)
 *
 * Used to tag a group of entities for later script lookup.
 */

#include <stdint.h>

extern int   iGpffffb718;        /* active entity count */
extern int   iGpffffb740;        /* entity array base, stride 0x78 */
extern void  script_eval_expression(void *out); /* FUN_0025c258 */

int op_0xA7_tag_entities_by_mask(void) {
    struct { unsigned int mask; int tag_nibble; } args;
    script_eval_expression(&args.mask);
    script_eval_expression(&args.tag_nibble);

    int            remaining = iGpffffb718;
    unsigned char *entity    = (unsigned char *)(intptr_t)iGpffffb740;
    while (remaining-- > 0) {
        unsigned int *flags = (unsigned int *)(entity + 4);
        if (*flags & args.mask) {
            *flags = (*flags & 0x0FFFFFFFu) |
                     ((unsigned int)args.tag_nibble << 28);
        }
        entity += 0x78;
    }
    return 0;
}
