/*
 * Terrain ground query -- the static collision scan.
 *
 * Original functions:
 *   FUN_00227840  the scan itself
 *   FUN_00227d28  per-primitive XY overlap (bbox + winding)
 *   FUN_00228090  height of a primitive at a point
 *   FUN_00227070  the actor-facing wrapper (4-corner sample, entity writeback)
 *   FUN_00227798  the body-less point query
 *   FUN_0022b5a8  loads the broadphase grid out of the map file
 *   FUN_0022c6e8  builds per-primitive bounds and plane records
 *   FUN_0022caf8  one plane record
 *
 * Everything below is read out of the decompilation and then checked against
 * eeMemory.bin (the s01_e012 bed shot) and out/mapbin/0001.psm2. Where a claim
 * is inference rather than transcription it says so.
 *
 * ---------------------------------------------------------------------------
 * 1. The broadphase is data, not code
 * ---------------------------------------------------------------------------
 *
 * PSM2 header word 6 (+0x18) is copied verbatim by FUN_0022b5a8:305-325 --
 * nothing is derived:
 *
 *     +0x0000  int16 grid[64 * 64]     -> DAT_00343a18   (cell head, <0 = empty)
 *     +0x2000  int16 listLength        -> DAT_003556ec
 *     +0x2004  int16 list[listLength]  -> DAT_003556f0   (runs, <0 terminates)
 *
 * The original's cursor advances by 3 shorts past the grid, so the two shorts at
 * +0x2002 are skipped.
 *
 * Cell selection, FUN_00227840:25-28:
 *     cx = (int)((x + 64.0f) * 0.5f);   if (cx >= 64) -> no ground
 *     cy = (int)((y + 64.0f) * 0.5f);   if (cy >= 64) -> no ground
 * Out of range returns 128.0f (the "no ground" sentinel), it does not clamp.
 *
 * Verified: out/mapbin/0001.psm2 reproduces DAT_00343a18 4096/4096 and the cell
 * list 1022/1022 against the live dump. All 165 PSM2 maps carry the section.
 *
 * ---------------------------------------------------------------------------
 * 2. The scan is ordered, not scored
 * ---------------------------------------------------------------------------
 *
 * This is the part that matters, and it is easy to get wrong by trying to be
 * clever. The scan walks the cell's run in the order the map authored and keeps
 * the FIRST front-facing hit since the last ceiling. It stops at the first hit
 * at or below the head. There is no "best", no distance, no preference.
 *
 *     recorded = false;                       // workspace +0x23
 *     result   = 128.0f;                      // workspace +0x50
 *     for (prim in cellRun) {
 *         if (prim.terrainFlags & entity[+0x74]) continue;      // reject mask
 *         if ((prim.leadingWord & 0x800) == 0)  continue;       // not collidable
 *         if (!overlaps(prim, x, y, &half))     continue;       // FUN_00227d28
 *         h = heightAt(prim, half, x, y);                       // FUN_00228090
 *         if (prim.leadingWord & 0x100) {
 *             recorded = false;               // a ceiling CANCELS, it does not
 *                                             // reject, and never writes result
 *         } else if (!recorded) {
 *             recorded = true;
 *             result   = h;
 *             entity[+0x0A] staged as prim | (half << 14);
 *         }
 *         if (h <= headLimit) break;          // headLimit = workspace +0x30
 *     }
 *     return result;
 *
 * Note the asymmetry: a ceiling clears the latch but leaves `result` alone, so
 * if the run ends with nothing after it, the earlier value still stands.
 *
 * headLimit is entity +0x28 + entity +0x58 (feet plus body height) for
 * FUN_00227070, or simply the caller's z for FUN_00227798.
 *
 * ---------------------------------------------------------------------------
 * 3. leadingWord (record78 +0x00) bits used by the scan
 * ---------------------------------------------------------------------------
 *
 *   0x00800  participates in collision at all. Tested first; nothing else about
 *            a primitive is examined without it.
 *   0x00200  FLAT. FUN_00228090:12 returns the constant at record78 +0x2C and
 *            never touches the plane. FUN_0022c6e8:100 fills +0x28/+0x2C with
 *            the primitive's min/max corner z, so the constant is the MAXIMUM.
 *   0x00100  ceiling / reversed winding. FUN_00227840 sets the workspace's
 *            +0x22 selector to 0xFF and FUN_00227d28 inverts every edge test.
 *   0x10000  dynamic; height comes from FUN_002281a0 against a live transform.
 *
 * The reject mask is entity +0x74 -- per entity, not a constant. FUN_00266240
 * seeds it as `param_7 | 0x04000000`; FUN_002cb9a8 ORs 0x08000000 (the player
 * reads 0x08000000 in the dump, everyone else 0x04000000); FUN_0026fb08 clears
 * 0x04000000. A primitive is skipped when `terrainFlags & mask` is non-zero.
 *
 * ---------------------------------------------------------------------------
 * 4. Overlap and height
 * ---------------------------------------------------------------------------
 *
 * FUN_00227d28: reject on the XY bbox at record78 +0x18..+0x24, then a winding
 * test. A triangle (corner 2 == corner 3) is corners (0,1,2). A quad is split on
 * the 1--3 diagonal into (3,0,1) and (1,2,3); which half hit goes to workspace
 * +0xD0 and ends up in entity +0x0A as `primitive | (half << 14)`.
 *
 * FUN_00228090's plane form:
 *     h = origin.z - ((x - origin.x) * n.x + (y - origin.y) * n.y) / n.z
 * with n.z == 0 falling back to the +0x2C constant. `origin` is the MIDDLE
 * corner of the triple FUN_0022caf8 was handed, stored as a raw int in the
 * plane record's 4th word -- the decompiler shows it as a float because its
 * neighbours are floats. Triangle (0,1,2) -> 1; quad halves -> 0 and 2.
 * Confirmed: every quad in s01_e012 reads 0 and 2.
 *
 * ---------------------------------------------------------------------------
 * 5. Who actually queries, and what lifts an actor
 * ---------------------------------------------------------------------------
 *
 * FUN_002262c0 reaches the ground query from only two places:
 *   - the velocity loop at :230-450, which runs only when +0x30 / +0x34 are
 *     non-zero, i.e. the actor is moving horizontally;
 *   - the stationary branch at :112-122, gated on
 *     `DAT_003555d0 != 0 && (entity[+0x08] & 0x20)`.
 * DAT_003555d0 is 0 in eeMemory.bin, so the stationary branch never runs for
 * anyone; only the player gets the periodic refresh at :114, every 64 frames.
 *
 * => A stationary non-player actor NEVER resamples the ground. It keeps the
 *    +0x4C its placement opcode gave it for the entire scene.
 *
 * The resample block at :41-85 is not the general case either. Its chain is:
 *   +0x0A >= 0, cached prim's +0x13 != 0, cached prim's +0x14 (material index)
 *   >= 0, DAT_003556e0[matIdx * 0x74 + 0x5A] != 0, then after resampling the
 *   material index must be unchanged.
 * record78 +0x13 is 0 for every primitive in s01_e012, so it never fires there.
 *
 * What actually raises +0x28 is the landing snap at :502-520:
 *     pos += dz;
 *     if (pos <= entity[+0x4C]) { pos = entity[+0x4C]; entity[+0x44] = 0; }
 *
 * Worked example, checked against the dump. Script opcode 0x55 (FUN_0025eeb0:32)
 * calls FUN_00227070 and stores the result in entity +0x4C; opcode 0x54 does
 * not, leaving +0x4C mirroring the authored z. s01_e012 places Magnus with 0x55
 * at (-8.000, -3.150, -1.500). The scan finds primitive 9 -- a hidden flat quad
 * over the bed at -1.200, leadingWord 0x0a20 -- so +0x4C becomes -1.200 while
 * +0x28 is still -1.500. The next frame's landing snap lifts him onto the bed.
 * The dump agrees exactly: +0x0A = 0x4009 (primitive 9, half 1), +0x6C =
 * 0x50620100, +0x4C = +0x28 = -1.200.
 *
 * Slots 82 and 84 have +0x0A = -1 and +0x6C = 0: never ground-queried, sitting
 * at heights their 0x54 authored. Any implementation that samples them every
 * frame puts them somewhere else.
 *
 * ---------------------------------------------------------------------------
 * 6. The second loop: collision groups
 * ---------------------------------------------------------------------------
 *
 * LAB_00227a70 walks DAT_003556e0 (stride 0x74, count DAT_003556dc). These own a
 * block of primitives at the TOP of the record78 array that the cell grid never
 * indexes -- in s01_e012 the grid stops near 2500 and the groups run 3131..3948
 * -- so this loop is the only thing that reaches them.
 *
 *   - A group whose +0x02 is 4 is skipped outright.
 *   - Otherwise its XY box at +0x24/+0x28 (x) and +0x2C/+0x30 (y) must contain
 *     the query point.
 *   - Its primitive span comes from DAT_003556d8[group +0x00], reading u16 at
 *     descriptor +0x04 (first) and +0x06 (count).
 *   - The span is scanned with the same latch logic as loop one, into a parallel
 *     set of workspace fields (+0x60/+0x64/+0x68/+0x6C/+0x6E).
 *
 * One asymmetry worth keeping: in the group scan a CEILING found above the head
 * (FUN_00227840:161) sets the group's hit count back to 0, which suppresses the
 * merge below. The floor branch has no such reset.
 *
 * Merge, :178-192 -- the group's answer is adopted when it is strictly HIGHER
 * than what is held, or when loop one hit nothing at all (+0x5E == 0):
 *
 *     if (groupHitCount != 0) {
 *         if (groupHeight <= held && loopOneHitCount != 0) keep held;
 *         else                                             adopt group;
 *     }
 *
 * Section layout, both confirmed against out/mapbin/0001.psm2:
 *
 *   header word 1 (+0x04) -> descriptors. int16 count, int16 at +2, then records
 *     from +4 with a FILE stride of 24 (FUN_0022b5a8:69-82 copies 6 u32 into a
 *     0x20 memory slot and zeroes the tail, setting memory +0x1A to 0xFFFF).
 *     `4 + count * 24` lands exactly on the next section, which is the proof.
 *     FUN_0022c6e8:74 later writes the owning group index into memory +0x1A, and
 *     that is what becomes record78 +0x14.
 *   header word 7 (+0x1C) -> groups. int16 count then 14 int16 per group (file
 *     stride 28) expanded into 0x74, tail zeroed, byte +0x5A seeded to 3.
 *
 * The +0x24..+0x30 box is NOT in the file -- FUN_00208450 recomputes it per
 * frame from the group's transform. For a group that never moves it is exactly
 * the union of its own primitives' record78 bounds: verified 20/20 against
 * eeMemory.bin. A port that lets a group move has to recompute it.
 *
 * ---------------------------------------------------------------------------
 * 7. FUN_00227070's four-corner sample
 * ---------------------------------------------------------------------------
 *
 * When `entity[+0x04] & 2` is CLEAR the scan runs FOUR times, at
 * (x-r, y-r), (x+r, y-r), (x+r, y+r), (x-r, y+r) with r = entity +0x54, and the
 * MAXIMUM wins. Only when the bit is SET is it a single point at (x, y). Party
 * members read 0xAF (single); props and NPCs read 0xD8 / 0xD0 and the player
 * 0x312C (all four-corner).
 *
 * The bookkeeping is not just "take the max":
 *   - entity +0x6C gets the WINNING sample's terrain flags; samples that tie the
 *     running maximum OR their flags into it.
 *   - entity +0x70 gets the AND across all four samples, taken before the
 *     comparison, regardless of which wins.
 *   - entity +0x0A only takes the new sample's primitive when that sample
 *     actually found one (`-1 < (short)puVar4[0x17]`).
 *   - entity +0x84..+0x90 get the four heights in the order above, and only on
 *     this path.
 *
 * ---------------------------------------------------------------------------
 * 8. Still not transcribed
 * ---------------------------------------------------------------------------
 *
 * - FUN_00228cf0, consulted after the scan; if it answers higher it wins and
 *   sets entity +0x0C bit 0x100. This is the ride-on-an-entity case.
 * - FUN_002281a0, the dynamic (leadingWord 0x10000) plane resolve.
 * - FUN_00208450, which animates a collision group and recomputes its box.
 *
 * gp aliases used above (gp = 0x00359F70):
 *   iGpffffb72c = DAT_0035569c  vertex array, stride 0x10 (x, y, z, pad)
 *   iGpffffb740 = DAT_003556b0  record78 array, stride 0x78
 *   iGpffffb768 = DAT_003556d8  group descriptors, stride 0x20
 *   iGpffffb770 = DAT_003556e0  group table, stride 0x74
 *   iGpffffb780 = DAT_003556f0  cell index list
 */
