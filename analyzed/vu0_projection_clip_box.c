/*
 * FUN_0020B6A0 / FUN_0020B600 - project through VU0 and answer "is it off screen"
 *
 * Every effect pool drops a record on `FUN_0020B6A0(...) & 0xE0`:
 * FUN_0021C288 (the 0x109 haze field), FUN_0021F310 (fountain), FUN_0021A820
 * (dust), FUN_002190F8 (rain) and the rest. What the test actually is was an
 * open question -- the decompilation shows `_cfc2` of something and nothing in
 * src/ writes vf7, vf8 or vf14 -- so here it is, resolved from the binary.
 *
 * == Where the box comes from ==
 *
 * There is exactly one `lqc2 vf7` in the whole executable, at 0x0020B45C, and
 * it is inside FUN_0020B430:
 *
 *     0020b454: lqc2 vf1,  0x00(a0)
 *     0020b458: lqc2 vf2,  0x10(a0)
 *     0020b45c: lqc2 vf7,  0x20(a0)
 *     0020b460: lqc2 vf8,  0x30(a0)
 *     0020b464: lqc2 vf9,  0x40(a0)
 *     0020b468: lqc2 vf14, 0x50(a0)
 *
 * FUN_00208EE8 is its only caller and passes **0x00314F90**:
 *
 *     vf1  (-131072, -131072,     0,   0.01)   seeds the running max
 *     vf2  ( 131072,  131072, 65536, 0.0039)   seeds the running min
 *     vf7  (  27648,   30976,     1,    0.5)   the box's low corner
 *     vf8  (  37888,   34560, 65534,      0)   its high corner
 *     vf9  (    255,     255,   255, 0.0039)
 *     vf14 (   8192,    4096,     0,      0)   how far the box is widened
 *
 * 27648 and 37888 are 32768 -/+ 5120: screen x 0 and 640 in GS 1/16 units.
 * 30976 and 34560 are 32768 -/+ 1792: screen y 0 and 448 at the eight units a
 * line the sprite path also works in. vf14 widens that by 512 pixels in x and
 * 512 in y before anything is tested against it.
 *
 * == The test is on the MAC flag, not the clip flag ==
 *
 * This is the part that is easy to get wrong. The two reads are
 *
 *     0020b710: cfc2 v0, $17
 *     0020b72c: cfc2 t3, $17
 *
 * and VU control register **17 is MACflag**; the clipping flag is 18. The
 * instruction five slots ahead of each read is a subtract whose result is
 * thrown into vf0 -- discarded -- purely so its flags can be read:
 *
 *     0020b6cc: vsub     vf16, vf7, vf14      ; lo = vf7 - vf14
 *     0020b6d0: vadd     vf17, vf8, vf14      ; hi = vf8 + vf14
 *     ...
 *     0020b6f8: vsub.xyz vf0, vf21, vf16      ; max - lo
 *     0020b710: cfc2     v0, $17
 *     0020b714: vsub.xyz vf0, vf17, vf20      ; hi - min
 *     0020b72c: cfc2     t3, $17
 *     0020b744: or       v0, v0, t3
 *
 * MACflag bits 4..7 are the sign bits of w, z, y and x. The destination mask
 * is xyz, so w's flags are cleared and **0xE0 is exactly the sign bits of z, y
 * and x**. A set bit means that subtract came out negative -- max below the low
 * corner, or min above the high one -- on that axis.
 *
 * So the pair is an ordinary box-overlap test and the reject reads:
 *
 *     drop the record when its projected point is outside
 *         x  [19456, 46080]
 *         y  [26880, 38656]
 *         z  [1, 65534]
 *
 * vf20 and vf21 are the running min and max across the n points the caller
 * asked for; with n == 1, which is what every pool passes, they are both just
 * the one projected point.
 *
 * (An earlier note in this repo said bits 6 and 7 were stale, left over from
 * whatever clip ran before. They are not: the masked write clears w's flags and
 * all three bits belong to the subtract in front of the read.)
 *
 * == Why it matters ==
 *
 * The box is wider than the screen, so for a dust mote or a spark this only
 * decides a pixel at the frame edge. For the 0x109 haze field, whose sprite is
 * over a screen wide at the distances it is used at, it is the difference
 * between a record vanishing and a record still washing out half the view.
 */

extern float DAT_00314f90[24]; /* vf1, vf2, vf7, vf8, vf9, vf14 */

/* FUN_0020B6A0 with n == 1, in the form its callers actually use it. */
int FUN_0020b6a0_rejects(float projectedX, float projectedY, float projectedZ) {
    const float loX = 27648.0f - 8192.0f, hiX = 37888.0f + 8192.0f;
    const float loY = 30976.0f - 4096.0f, hiY = 34560.0f + 4096.0f;
    const float loZ = 1.0f,               hiZ = 65534.0f;

    return projectedX < loX || projectedX > hiX ||
           projectedY < loY || projectedY > hiY ||
           projectedZ < loZ || projectedZ > hiZ;
}
