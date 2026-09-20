/*
 * The plume pool - the tenth particle system, and the fire on the burning ship
 *
 * FUN_00220028 is the tenth call in FUN_002192C0's effect walk, between the
 * fountain (FUN_0021F1A8) and the hit sparks (FUN_00220910). It is the only one
 * of the ten that owns **two** arrays behind one gate, and it is what draws the
 * flames and the smoke columns on the wrecked ship in s01_e013's monster
 * animatic. The script arms it with opcode 0x10E; eighteen other functions --
 * mostly boss and enemy behaviours -- arm it directly.
 *
 *   0x0021FA88  FUN_0021fa88  carve both arrays, mark every record free
 *   0x0021F6E8  FUN_0021f6e8  open an emitter (opcode 0x10E's target)
 *   0x0021F7A8  FUN_0021f7a8  the same, plus a horizontal drift
 *   0x0021F870  FUN_0021f870  shed one smoke puff
 *   0x0021F980  FUN_0021f980  shed one flame
 *   0x0021FB68  FUN_0021fb68  step one emitter
 *   0x00220210  FUN_00220210  step one puff, and draw it
 *   0x00220028  FUN_00220028  walk both arrays
 *   0x00262A98  FUN_00262a98  opcode 0x10E
 *   0x00219368  FUN_00219368  the debug menu's call, which names the arguments
 *
 * == The two arrays ==
 *
 * FUN_0021FA88 takes 36000 bytes for **1000 puffs of 0x24** at puGpffffbc00 and
 * then 6000 for **100 emitters of 0x3C** at puGpffffbbfc, in that order. The
 * live counts are iGpffffbbf4 (DAT_00355B64, puffs) and iGpffffbbf8
 * (DAT_00355B68, emitters); the gate is iGpffffad54 (DAT_00354CC4). Any spawn
 * raises the gate and FUN_00220028 drops it once both counts are out.
 *
 * Emitter, 0x3C:
 *   +0x00  float x, y, z        where it is now
 *   +0x0C  float x, y, z        where it started; every restart puts it back
 *   +0x18  float                rise per tick over 32
 *   +0x1C  float                horizontal drift, FUN_0021F7A8 only
 *   +0x20  float                its heading, FUN_0021F7A8 only
 *   +0x24  s8                   the flame flag
 *   +0x26  s16                  puffs per burst
 *   +0x28  u16                  ticks to the next burst; reloads with 0x140
 *   +0x2A  s16                  ticks left of this cycle
 *   +0x2C  s16                  the cycle's length
 *   +0x30  float                the puff half-extent, before the growth
 *   +0x34  u32                  a packed RGB, or zero
 *   +0x38  s8                   restarts left; negative is free
 *   +0x39  s8                   the one-shot latch
 *
 * Puff, 0x24:
 *   +0x00  float x, y, z
 *   +0x0C  float                rise per tick over 32; drives the drift too
 *   +0x10  float                a random heading, rolled at spawn
 *   +0x14  u16                  ticks elapsed
 *   +0x16  s16                  ticks total
 *   +0x18  float                half-extent
 *   +0x1C  u32                  colour
 *   +0x20  s8                   negative free, 0 smoke, 1 flame
 *   +0x21  u8                   tile index
 *   +0x22  s8                   CLUT bank
 *
 * == Four things here that are easy to get wrong ==
 *
 * **A cycle count of 99 is immortal.** FUN_0021FB68 decrements +0x38 only when
 * it is `< 0x63`, and 99 *is* 0x63:
 *
 *     0021fbc0: lb   v0,0x38(s0)
 *     0021fbc4: slti v0,v0,0x63
 *     0021fbc8: beq  v0,zero,0x0021fbd8      ; 99 skips the decrement
 *
 * Every one of s01_e013's six ship emitters is armed with 99, so none of them
 * ever expires; they burn until the scene drops the gate.
 *
 * **FUN_0021F6E8 does not clear +0x1C or +0x20.** FUN_0021F7A8 writes them
 * (`sw a1,0x1c` / `sw a2,0x20` at 0x0021F7F0 and 0x0021F810) and FUN_0021F6E8
 * has no store to either. A recycled slot therefore inherits the previous
 * occupant's drift, which is live behaviour and not dead state: the drift is
 * read every step.
 *
 * **The `& 0xE0` reject comes before the near cutoff, and the flame strip
 * advances after both.** FUN_00220210 calls FUN_0020B6A0 at 0x0022036C, tests
 * `& 0xE0`, then tests q against DAT_003523EC, and only then does
 *
 *     00220438: lbu  a1,0x21(s8)
 *     0022045c: addiu v1,a1,0x1
 *     00220464: sltiu v0,v1,0x6
 *     00220470: sb   zero,0x21(s8)
 *
 * So a flame that spends a moment off screen resumes on the frame it left off,
 * rather than running on invisibly.
 *
 * **The alpha ramp is not the same for the two kinds.** A smoke puff's is a
 * triangle -- `r * 255` up to r = 0.5 and `255 - r * 255` above it, peaking at
 * 127 in the middle -- and a flame's is the falling half only, from 255 down to
 * 0. The +0x20 test at 0x002202B4 skips the whole movement block *and* the
 * rising half in one branch.
 *
 * == The sheet ==
 *
 * Texture slot 0x20, which in s01_e013 is DAT_003429A8[0x20] = resource 0x19C.
 * Slot >= 0x18 is a 4-bit page, so the packet's high byte is a CLUT bank:
 *
 *     0022056c: lb    v0,0x22(s8)
 *     00220570: sll   v0,v0,0x8
 *     00220574: addiu v0,v0,0x20
 *
 * unless the record carries a colour word, in which case the halfword is a bare
 * 0x0020 and the bank is 0. The banks in use are 1 and 6 for ordinary smoke
 * (picked by a coin flip every emitter step), 2 for smoke under a flame in the
 * first fifth of the emitter's life, and 5 for a flame.
 *
 * DAT_00315898 holds three 63x63 smoke tiles along the bottom of the sheet and
 * DAT_003158F8 six 23x39 flame tiles in a row above them, each as four ST pairs
 * of a quad. In texels on the 256-unit sheet:
 *
 *     smoke  (64.4, 192.4)-(127.4, 255.4)
 *            (128.4, 192.4)-(191.4, 255.4)
 *            (192.4, 192.4)-(255.4, 255.4)
 *     flame  (64.4, 104.4)-(87.4, 143.4), then five more at a stride of 24
 *
 * The two tables overlap -- DAT_003158F8 is DAT_00315898 + 0x60 -- but nothing
 * reads across the join, because a smoke record's tile index is `roll() % 3`
 * and a flame's wraps at 5.
 *
 * == The quad ==
 *
 * FUN_00220028 stages the four corner offsets into the scratchpad before the
 * call, as `+/-size*16` in x and `+/-size*8` in y; FUN_00220210 multiplies each
 * by 400 and by the projected q and adds it to the integer GS origin. The 2:1
 * is the GS's own -- x counts in 1/16 of a pixel and y in 1/8 of a line -- so
 * that is a square, not a wide rectangle. Packet +0x0C is
 * `0x10000080 | 0x4000`, blend mode 1 through FUN_00207DE8's `& 0x1C000`
 * ladder; a colour word with an alpha byte of its own swaps the 0x4000 for
 * 0x8000 and makes it additive.
 *
 * == Measured against hardware ==
 *
 * PCSX2 parked in the animatic (frame 14399): the gate at DAT_00354CC4 reads 1,
 * iGpffffbbf8 reads 6 and iGpffffbbf4 reads 288, and the six live emitters are
 *
 *     ( 0.000, -24.0, ...)  size 0.4  rise 0.03  burst 1  life 0x0C80  99  flame
 *     ( 0.000, -23.5, ...)  size 0.5  rise 0.03  burst 1  life 0x0C80  99  flame
 *     ( 0.000, -24.5, ...)  size 0.4  rise 0.03  burst 1  life 0x0C80  99  flame
 *     ( 0.065,   0.0, ...)  size 1.5  rise 0.04  burst 3  life 0x3200  99  flame
 *     (-0.065,   0.0, ...)  size 1.5  rise 0.04  burst 3  life 0x3200  99  flame
 *     ( 0.000,   0.0, ...)  size 1.5  rise 0.04  burst 3  life 0x3200  99  flame
 *
 * The port reproduces all six exactly, which is what pinned the operand order.
 */

extern int   iGpffffbbf4;      /* DAT_00355B64, live puffs    */
extern int   iGpffffbbf8;      /* DAT_00355B68, live emitters */
extern int   iGpffffad54;      /* DAT_00354CC4, the gate      */
extern void *puGpffffbbfc;     /* 100 emitters of 0x3C        */
extern void *puGpffffbc00;     /* 1000 puffs of 0x24          */
extern unsigned short uGpffffb64c; /* DAT_003555BC, the frame's ticks */

extern float fGpffff8468;      /* 0.8, the bright-bank threshold */
extern float fGpffff846c;      /* 0.9, the flame threshold       */
extern float fGpffff8478;      /* 0.1, a puff's share of the rise */
extern float DAT_003523ec;     /* 0.7, the near cutoff           */
extern float DAT_003523f0;     /* 0.6, where the near fade starts */
extern float DAT_003523f4;     /* 0.1, the fade's span            */

/* FUN_0021FB68, with the scratchpad aliasing folded away. */
void FUN_0021fb68_step_emitter(char *emitter)
{
    const unsigned short ticks = uGpffffb64c;
    short remaining = (short)(*(unsigned short *)(emitter + 0x2A) - ticks);
    *(short *)(emitter + 0x2A) = remaining;

    if (remaining < 0) {
        if (*(signed char *)(emitter + 0x38) < 0x63) {
            *(signed char *)(emitter + 0x38) -= 1;
        }
        if (*(signed char *)(emitter + 0x38) > 0) {
            *(short *)(emitter + 0x2A) = *(short *)(emitter + 0x2C);
            *(float *)(emitter + 0x00) = *(float *)(emitter + 0x0C);
            *(float *)(emitter + 0x04) = *(float *)(emitter + 0x10);
            *(float *)(emitter + 0x08) = *(float *)(emitter + 0x14);
            return;
        }
        *(signed char *)(emitter + 0x38) = -1;
        iGpffffbbf8 = (iGpffffbbf8 - 1 > 0) ? iGpffffbbf8 - 1 : 0;
        return;
    }

    /* The climb, and the drift FUN_0021F7A8 alone ever sets up. */
    *(float *)(emitter + 0x08) += *(float *)(emitter + 0x18) * (float)ticks * 0.03125f;
    if (*(float *)(emitter + 0x1C) > 0.0f) {
        const float step = *(float *)(emitter + 0x1C) * (float)ticks * 0.03125f;
        *(float *)(emitter + 0x00) += step * cosf(*(float *)(emitter + 0x20));
        *(float *)(emitter + 0x04) += step * sinf(*(float *)(emitter + 0x20));
    }

    /* Falls from 1 to 0 over the cycle; the puffs grow as it does. */
    const float ratio = (float)*(short *)(emitter + 0x2A) / (float)*(short *)(emitter + 0x2C);
    const float growth = *(signed char *)(emitter + 0x39) ? 1.0f : (1.0f - ratio) + 1.0f;

    /* Rolled whether or not the flame flag is set, so the generator advances
     * identically either way. */
    unsigned char bank = (FUN_00216868() & 1) ? 1 : 6;
    if (*(signed char *)(emitter + 0x24) != 0) {
        if (fGpffff8468 < ratio) { bank = 2; }
        if (fGpffff846c < ratio) {
            /* angle, then radius -- two words, in that order */
            const float a = (float)(FUN_00216868() % 360) * 6.283184f / 360.0f;
            const float r = (float)(FUN_00216868() % 10) / 100.0f;
            FUN_0021f980(0.0f, *(float *)(emitter + 0x30),
                         *(float *)(emitter + 0x00) + r * cosf(a),
                         *(float *)(emitter + 0x04) + r * sinf(a),
                         *(float *)(emitter + 0x08), 1,
                         *(short *)(emitter + 0x2C), *(unsigned int *)(emitter + 0x34));
        }
    }

    const short puffLife = (short)(*(short *)(emitter + 0x2C) * 1.5f);

    short burst = (short)(*(unsigned short *)(emitter + 0x28) - ticks);
    *(unsigned short *)(emitter + 0x28) = burst;
    if (burst < 0) {
        for (int shed = 0; shed < *(short *)(emitter + 0x26); ++shed) {
            const float a = (float)(FUN_00216868() % 360) * 6.283184f / 360.0f;
            const float r = (float)(FUN_00216868() % 10) / 100.0f;
            FUN_0021f870(*(float *)(emitter + 0x18) * fGpffff8478,
                         *(float *)(emitter + 0x30) * growth,
                         *(float *)(emitter + 0x00) + r * cosf(a),
                         *(float *)(emitter + 0x04) + r * sinf(a),
                         *(float *)(emitter + 0x08), 1, puffLife,
                         *(unsigned int *)(emitter + 0x34),
                         FUN_00216868() % 3, bank);
        }
        *(unsigned short *)(emitter + 0x28) = 0x140;
    }

    /* The one-shot's second job: make the *next* step expire. */
    if (*(signed char *)(emitter + 0x39) != 0) {
        *(short *)(emitter + 0x2A) = 0;
    }
}
