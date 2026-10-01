/*
 * Climbing: FUN_00252DE0 and lead states 3, 4 and 5.
 *
 *   FUN_00257610  build DAT_00355020 at map load (FUN_0022A418:328)
 *   FUN_00257160  the climbable face in front of the entity
 *   FUN_002573D8  the neighbouring face in one of four directions
 *   FUN_00252DE0  Cross at a face -> state 3 (called by states 0 and 1 after
 *                 FUN_00256BB8, unless an interaction cleared the pad)
 *   FUN_002537A0  PTR_FUN_0031E0E8[3]  hang, read the stick
 *   FUN_00253BE8  PTR_FUN_0031E0E8[4]  move onto the chosen face
 *   FUN_002540D0  PTR_FUN_0031E0E8[5]  climb over the lip
 *   FUN_00254278  PTR_FUN_0031E0E8[6]  camera path, then state 4 logic; nothing
 *                 found in the ELF writes state 6, so it is not ported
 *
 * Climbable faces are map primitives, not entities. The 0x78 record's terrain
 * word (+0x04) has a top nibble of 0xE or 0xF; 0xE is a lip (up from it runs
 * state 5). The list is 5-halfword records {prim, n0..n3}, -1 terminated; two
 * faces are neighbours when exactly two of the first face's four corner slots
 * coincide with a corner of the second (a triangle's repeated corner counts
 * twice). A neighbour whose centre equals one already taken is skipped. A
 * fifth neighbour overflows into the next record and is lost.
 *
 * 0x78 record bounds: +0x18/+0x1C x min/max, +0x20/+0x24 y, +0x28/+0x2C z
 * (FUN_0022C6E8). 0x80 record: +0x00 normal, +0x60 centre.
 *
 * FUN_00257160: facing +0x5C turned by pi within +-60 deg (DAT_003529C8/CC) of
 * atan2(n.y, n.x); body middle (+0x28 + +0x58/2) in [minZ, maxZ); entity within
 * 1.0 of the centre across the ground and on the normal's side (+-60 deg). The
 * last face in list order that passes wins. Its FUN_0020B810 result is unused.
 *
 * FUN_00252DE0 (Cross, +0x1BA == 0, +0x44 == 0):
 *   +0x1B4 = face (even -1); +0x0C &= ~1; +0x62 = 0; +0x04 |= 8 (no gravity);
 *   +0x0A = -1; +0x08 &= ~4; +0x1B0 = 0.0003; +0x1AC = +0x4C;
 *   FUN_00225BF0(3, 0x19); +0x1A4 = heading; +0x5C = heading + pi;
 *   pos = centre + (+0x54 + 0.15) * dir(heading), z = centre.z - +0x58/2;
 *   manual camera 2.5 out along the normal: class 3 from z + 4*height,
 *   others from z or the ground there if higher; DAT_003555D1 = 1.
 *
 * FUN_002537A0: Cross -> FUN_00252D88, FUN_002536A8, FUN_00217E18(1).
 *   Stick magnitude < 80 or between sectors -> hang (anim 0x19); non-class-3
 *   drifts the camera eye after 0x60 frames. Sectors on DAT_003555E4:
 *   up (1.134, 2.007), down (-2.182, -1.134), right (-0.436, 0.436),
 *   left (2.705, pi] or (-pi, -2.705). That "pi" is 0x40490FD8 (3.14159202),
 *   below the 3.1415925 the game's own atan2f (FUN_00306218) returns for a
 *   dead-level left stick, so a perfectly level push left does nothing on
 *   hardware either; any off-level byte gets in. Up on a lip face -> state 5 (anim 0x2E,
 *   +0x1A8 = +0x7C, +0x7C = 128.0). Found neighbour -> anim 0x18 / 0x1B / 0x1A,
 *   +0x1A0 = 0.004 / 0.02 / 0.06 by the turn between normals (<20, <60, more),
 *   +0x1B4 = neighbour, +0x1B0 = 0.0003, bare +0x60 = 4, cue 0x10 or 0x11.
 *
 * FUN_00253BE8: step = +0x1B0 * ticks. +0x154 eases to the face pitch (0.005),
 *   +0x5C = heading + pi, +0x1A4 eases to heading at +0x1A0. Across first, then
 *   up/down toward centre.z - height/2 (down only while above +0x4C). Both
 *   arrived -> +0x154 = pitch, +0x1B6 = 0, state 3, run FUN_002537A0 now.
 *
 * FUN_002540D0: while cursor < 7 rise at +0x1B0; entering entry 8 restores
 *   gravity with +0x44 = 0.053 and cue 8; airborne pushes forward 0.0009375 a
 *   tick; landing sets anim 0x10, restores +0x7C, surface cue 3; anim != 0x2E
 *   -> FUN_002560E8.
 *
 * FUN_002536A8 clears +0x1A0 (0x00253778), not +0x1B0: a hit on a wall goes to
 * the knockback at climbing speed.
 *
 * s03_e001 has 286 climbable faces and no lip faces. The tower "hole" is a
 * floor at z = 13 south of the wall carrying terrain bit 0x1000, which the
 * scene script tests (0x61 at 0x2380) on the lead's +0x6C.
 */
