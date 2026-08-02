/*
 * Actor behavior for type 0x3A -- the treasure chest
 * Original: FUN_002d1ea8 (0x002d1ea8)
 * Reached from: FUN_00239ce0 -> PTR_FUN_0031c6c0[0x3A - 1] == 0x002D1EA8
 * Spawned by:   FUN_0025eb48 (opcode 0x51) group 3
 *
 * Summary
 * - Three visual states driven by one event flag: closed, opening, opened. The
 *   flag id is baked into the entity at spawn time, so a chest already looted on
 *   a previous visit comes up open with no special-casing.
 * - Unusually for a type handler, this one has no state table: it switches on the
 *   animation id at +0xA0 directly and never calls FUN_0023a068. That makes it
 *   the cheapest actor behavior in the game to port.
 * - It never writes facing (+0x5C). Chests keep whatever angle the placement
 *   record gave them; they do not turn to face anything.
 *
 * ---------------------------------------------------------------------------
 * How a chest is built
 * ---------------------------------------------------------------------------
 *
 * FUN_0025eb48, the 0x51 dispatcher, walks the map's placement table and for
 * every record whose group byte (+0x0D) equals 3:
 *
 *     entity = FUN_00265e28(0x3a);                       // always type 0x3A
 *     facing = FUN_00216690(record[+0x0C] * (pi/4) + pi/2);
 *     FUN_00266240(record.x, record.y, record.z, facing, entity, 0, 0, 0);
 *     entity[+0x98]  = recordIndex;                      // which record built it
 *     entity[+0x130] = record[+0x0E];                    // "id" byte, <= 0x7F
 *     entity[+0x198] = record[+0x0F] + 0x400;            // the event flag id
 *
 * (The decompilation writes these through an `undefined2 *`, so the printed
 * indices 0x4C, 0x98 and 0xCC are halfwords; the byte offsets are 0x98, 0x130
 * and 0x198.)
 *
 * So +0x198 is an **event flag id**, not a pointer. Records carry a param byte
 * in 0x00-0xFF and the engine biases it into the 0x400 flag band. In s01_e024
 * the seven chest records carry params 255..249, giving flags 0x4FF..0x4F9.
 *
 * FUN_00266240's last argument lands in +0x94, and group 3 passes 0, which is
 * what makes the handler's first branch fire on the entity's first tick.
 *
 * FUN_00266368(flagId) is the event-flag read: bounds-checks flagId >> 3 against
 * 0x8FF and returns `DAT_00342b70[flagId >> 3] & (1 << (flagId & 7))`. It
 * returns the masked bit, not a normalised 0/1.
 *
 * ---------------------------------------------------------------------------
 * FUN_002d1ea8
 * ---------------------------------------------------------------------------
 *
 *     if (entity[+0x94] == 0) {                       // first tick
 *       entity[+0xA0] = FUN_00266368(entity[+0x198]) ? 6 : 4;
 *       entity[+0x94] = 1;
 *       return;
 *     }
 *
 *     if (entity[+0xA0] == 4) {                       // closed, watching the flag
 *       if (FUN_00266368(entity[+0x198]))
 *         FUN_00225bc8(entity, 5);                    // -> opening
 *       return;
 *     }
 *
 *     if (entity[+0xA0] != 5) return;                 // 6 (opened) is terminal
 *
 *     /-* animation 5: opening. Emit the contents effect once, then run a timer. *-/
 *
 * So: 4 = closed, 5 = opening, 6 = open. Nothing here sets the flag -- the chest
 * only observes it. Something else (the interaction path, header word 3) opens
 * the chest by setting flag +0x198, and this handler reacts on the next frame.
 *
 * FUN_00225bc8(entity, anim) is the shared animation-state setter:
 *
 *     entity[+0xA0] = anim;      // animation id
 *     entity[+0xA4] = 999;       // state timer reset sentinel
 *     entity[+0xA2] = 0xFFFF;    // previous substate
 *     entity[+0x06] &= 0xFF38;   // clear motion/contact bits
 *     entity[+0xA8] = 0;         // substate frame
 *
 * ---------------------------------------------------------------------------
 * The opening branch in full
 * ---------------------------------------------------------------------------
 *
 *     if ((entity[+0xAA] & 0x100) && (entity[+0x06] & 8)) {
 *       FUN_002d59e0(entity);                  // == FUN_00267d38(0x9F, entity), a sound cue
 *       if (entity[+0x130] >= 0) {
 *         if (cGpffffb6e1 == 0x23) {           // only in one global mode
 *           /-* build a 0x14-quadword packet at DAT_70000000 describing a beam
 *              from the chest up toward the camera-relative point, then
 *              FUN_00217e18(0) + FUN_00217fe8(...) to submit it *-/
 *           entity[+0x19C] = 0;                // start the timer
 *           entity[+0x19E] = 1;                // timer active
 *         }
 *       }
 *     }
 *
 *     if (entity[+0x19E]) {                    // the tail, run every frame
 *       FUN_00218158(entity[+0x19C], 0x1680);
 *       if (entity[+0x19C] < 0x1680) entity[+0x19C] += DAT_003555bc;
 *       else                         entity[+0x19E] = 0;
 *     }
 *
 * The two gates on the effect are worth reading carefully:
 *
 * - `entity[+0x06] & 8` is one of the bits FUN_00225bc8 *clears* on the
 *   transition into animation 5, so the effect cannot fire on the frame the
 *   chest starts opening. It fires once the animation system sets the bit again,
 *   which is how the effect syncs to the lid actually being up.
 * - `cGpffffb6e1 == 0x23` is a global mode selector. The port never enters it, so
 *   the whole packet-building body is dead in a port that has no such mode -- and
 *   with it, +0x19E is never set, so the timer tail never runs either.
 *
 * 0x1680 is 5760 = 0x120 frames at the nominal 0x20 tick, i.e. about 4.8 seconds
 * at 60 Hz.
 *
 * ---------------------------------------------------------------------------
 * Notes for the port
 * ---------------------------------------------------------------------------
 *
 * - Implement the three-state logic and the timer tail. Where the original
 *   builds a GS packet, do nothing: the port has no primitive submission path,
 *   and the branch is unreachable anyway without the 0x23 mode.
 * - The observable behavior in a port with no flags set is: every chest picks
 *   animation 4 on its first tick and then sits there. That is correct, and it
 *   is what the original does too until something opens the chest.
 * - Do not call FUN_0023a068 here. This handler genuinely skips the freeze gate,
 *   so a frozen chest keeps ticking.
 */

/* Signatures, for reference; bodies are in src/. */

void FUN_002d1ea8(int entity);            /* the behavior */
void FUN_00225bc8(int entity, short anim); /* shared animation-state setter */
unsigned int FUN_00266368(unsigned int flagId); /* event-flag read, DAT_00342b70 */
