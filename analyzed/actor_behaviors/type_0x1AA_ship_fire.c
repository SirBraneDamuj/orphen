/*
 * Types 0x1AA / 0x1AB / 0x1AC - the burning ship
 *
 * Originals: FUN_002ED3E0, 0x002ED980 (no src/ file), FUN_002ED9A0
 *
 * Three consecutive entries of the tertiary handler table:
 *
 *     PTR_LAB_0031CAB0 + (id - 0xFC) * 4
 *       0x1AA -> 0x002ED3E0
 *       0x1AB -> 0x002ED980
 *       0x1AC -> 0x002ED9A0
 *       0x1AD -> 0x00239E78   (the no-op)
 *
 * Between them they are every flame and puff of smoke on the wreck in
 * s01_e013's monster animatic. The scene script spawns two type 0x1AA with
 * opcode 0x52 and then never touches them again: everything below, these
 * entities do to themselves, including taking themselves out of the pool.
 *
 * == 0x1AA, three entities in one ==
 *
 * +0x60 tells them apart.
 *
 *   0  the root. Every time its animation comes round -- +0xA8 == 0 with
 *      +0x06 bit 4 set, which is "the first timeline entry expired this
 *      frame" -- it showers five type 0x1AB into a 2..6 unit disc about
 *      itself, and on that same beat spawns the other two halves of itself:
 *      a state 1 on animation 2 and a state 2 on animation 3, both at 0.7x
 *      its own scale (DAT_00354AB4 and DAT_00354AB8, two words holding the
 *      same 0.7). It never fades; it goes when its own strip ends.
 *   1  the smoke half. Opens one chained type 0x1AC ring on its first frame
 *      and starts fading over 0xC80 ticks once animation 2 reaches timeline
 *      entry 2.
 *   2  the flame half. The same, over 0x780 ticks at entry 4.
 *
 * The shower loop is `s2 = 4; do { ... } while (--s2 >= 0)` -- five puffs, not
 * six. Each gets scale (rng % 900) / 100, a radius of (rng % 5) + 2 and an
 * angle of (rng % 360) degrees; all three rolls are `divu`/`mfhi`, so the
 * remainder is unsigned however the RNG's top bit fell.
 *
 * == 0x1AC carries a chain ==
 *
 * Each link decrements the byte at +0x198 and spawns the next one while the
 * result, read as a signed char, is still positive. The root seeds it to 2, so
 * a ring is three deep. +0x19A is the ring index, stepped once per link and
 * copied down, and the angle is `+0x19A * 0x78` degrees -- 120 a link, so the
 * three do not sit on top of each other. A link fades over the literal 2240
 * ticks with +0x94 as its latch, then waits for its animation to end.
 *
 * == 0x1AB is eight instructions ==
 *
 *     lhu v0, 6(a0); andi v0, v0, 1; beq v0, zero, +3; nop
 *     j 0x00265EC0; nop; jr ra; nop
 *
 * Destroy me when my animation has run out, and nothing else.
 *
 * == Two details worth writing down ==
 *
 * **Every spawn's `+0x08 |= 0x80` is dead.** Four stores later the parent's
 * whole +0x08 is assigned over it. Both writes are in the instruction stream;
 * only the second one lands.
 *
 * **The 0x1AA fade divides by zero on its last frame.** The ramp reloads +0x19C
 * after the same block may have zeroed it, so the final frame evaluates
 * 124 - (t / 0) * 124 = -inf. FUN_0030BDB0 is __fixunssfsi and answers 0 for
 * anything negative, so the level lands on 3 rather than on something huge.
 *
 * == Where everything stands ==
 *
 * On DAT_003556FC, the float opcode 0xE2 writes -- see
 * analyzed/ops/0xE2_set_global_float_3556fc.c. Every spawn's z, +0x4C and
 * +0x50 come from it; a ring link sits DAT_00354AAC (0.2) above it.
 *
 * == Why it matters to the port ==
 *
 * Without the behaviour the two roots the script spawns never fade and, above
 * all, never reach their own FUN_00265EC0 -- so they stand at the ship for the
 * whole animatic drawing their model at full opacity. On hardware they are
 * gone by the beam shot: at that frame the allocation bytes at DAT_005A96B0
 * read 0 for their slots and every field but the type is the stale data the
 * destroy left behind.
 */

extern int   FUN_00265e28(long typeId);          /* allocate and initialise */
extern void  FUN_00225bc8(int entity, short animation);
extern void  FUN_00265ec0(int entity);           /* destroy, cascading */
extern int   FUN_00216868(void);                 /* the engine RNG */
extern unsigned int FUN_0030bdb0(float value);   /* __fixunssfsi */
extern float FUN_00305130(float radians);        /* cos */
extern float FUN_00305218(float radians);        /* sin */

extern float          DAT_003556fc;   /* fGpffffb78c, the water line */
extern float          DAT_00354aa8;   /* 6.2831855 */
extern float          DAT_00354aac;   /* 0.2 */
extern float          DAT_00354ab0;   /* 6.2831855 */
extern float          DAT_00354ab4;   /* 0.7 */
extern float          DAT_00354ab8;   /* 0.7 */
extern unsigned short DAT_003555bc;   /* the per-frame tick count */

void type_0x1AB_ship_smoke_puff(int entity) {
    if (*(unsigned short *)(entity + 6) & 1) FUN_00265ec0(entity);
}
