/*
 * The scene script's per-frame entry and the object-script slot table
 * Original: FUN_0025b778 (0x0025b778)  run every frame from FUN_002239c8
 *           FUN_0025b918 (0x0025b918)  the two late slots, run later in the frame
 *           FUN_0025bc30 (0x0025bc30)  clear all 65 slots
 *           FUN_0025ce30 (0x0025ce30)  the 4-entry deferred trigger queue
 * Slot opcodes: 0x9D FUN_00261cb8, 0x9E FUN_00261d18, 0x9F FUN_00261d88,
 *               0xA0 FUN_00261de0, 0xA8 FUN_00262f38, 0xAA FUN_00263118
 *
 * Summary
 * - Header word 2 is the scene's per-frame script. Alongside it the engine keeps
 *   a table of 65 additional script entry points -- "object scripts" -- and runs
 *   every occupied one, in order, once per frame.
 * - The single most important fact for a port: these are NOT coroutines.
 *
 * ---------------------------------------------------------------------------
 * There is no yield
 * ---------------------------------------------------------------------------
 *
 * FUN_0025bc68's loop terminates on `pbGpffffbd60 != 0`, which reads like a
 * suspend mechanism. It is not used as one. Nothing in the executable ever
 * assigns 0 to pbGpffffbd60 (equivalently DAT_00355cd0, gp - 0x42A0): every
 * write either advances it, takes a jump, or restores a saved value. The only
 * way out of the interpreter is the top-level 0x04 block end.
 *
 * So a slot holds a *fixed* entry address, and that address is re-entered from
 * the beginning every frame and runs to completion every frame. Anything a slot
 * needs to remember across frames lives in the script work array (DAT_00355060)
 * or in the event-flag banks, not in an instruction pointer.
 *
 * That makes the per-frame path far cheaper to port than a resumable VM: no
 * continuation capture, no stack save, no scheduler.
 *
 * ---------------------------------------------------------------------------
 * FUN_0025b778
 * ---------------------------------------------------------------------------
 *
 *     DAT_0035503c = 0;
 *     FUN_0025bc68(DAT_00355058 + header[2]);   // the scene's per-frame entry
 *     FUN_0025ce30();                           // deferred triggers, below
 *
 *     if (DAT_00355cf4 != 0) {
 *       for (i = 0; i < 0x3E; i++) {            // 62 general slots
 *         if (DAT_00355cf4[i] == 0) continue;
 *         if (DAT_003555dd & 0x80)              // debug trace flag
 *           FUN_002681c0("...", i, DAT_00355cf4[i][-1]);
 *         DAT_00355cf8 = i;                     // "current slot"
 *         FUN_0025bc68(DAT_00355cf4[i]);
 *       }
 *       DAT_00355cf8 = -1;
 *
 *       if (DAT_00355cf4[0x40] != 0) {          // byte offset 0x100
 *         DAT_00355044 = DAT_00355048 = &DAT_0058beb0;   // select slot 0, the lead
 *         FUN_0025bc68(DAT_00355cf4[0x40]);
 *       }
 *     }
 *
 *     FUN_0025cfb8();                           // the cinematic letterbox bars,
 *                                               // see analyzed/cinematic_letterbox_bars.c
 *     /-* debug-only dump of the scene work flags at DAT_0031e770 *-/
 *
 * Slot 0x40 is special twice over: it is skipped by the 0..0x3D loop, and before
 * it runs both entity selection globals are pointed at pool slot 0. It is the
 * lead player's script hook, installed by opcode 0xA8 and torn down by 0xAA.
 *
 * Slots 0x3E and 0x3F are also skipped here. They belong to FUN_0025b918, which
 * the frame function calls later, after the entity updates and rendering setup:
 *
 *     if (DAT_00355cf4 != 0) {
 *       DAT_00355cf8 = 0x3E;
 *       if (DAT_00355cf4[0x3E] != 0) FUN_0025bc68(DAT_00355cf4[0x3E]);
 *       DAT_00355cf8 = 0x3F;
 *       if (DAT_00355cf4[0x3F] != 0) FUN_0025bc68(DAT_00355cf4[0x3F]);
 *     }
 *
 * (The decompilation of FUN_0025b918 shows the second test returning early; the
 * shape above is what the two guarded calls amount to. The distinction only
 * matters if both late slots are ever occupied at once.)
 *
 * So the 65 dwords partition as: 0x00-0x3D general, 0x3E-0x3F late,
 * 0x40 lead-bound. FUN_0025b390 clears all 65 at scene load; FUN_0025bc30
 * clears them again on demand.
 *
 * ---------------------------------------------------------------------------
 * Aliases
 * ---------------------------------------------------------------------------
 *
 * Ghidra names the same addresses differently depending on whether the access
 * was gp-relative. With gp = 0x00359F70:
 *
 *     pbGpffffbd60 == DAT_00355cd0    0x00355CD0   script instruction pointer
 *     iGpffffbd84  == DAT_00355cf4    0x00355CF4   slot table base
 *     iGpffffbd88  == DAT_00355cf8    0x00355CF8   current slot index
 *     iGpffffb64c  == DAT_003555bc    0x003555BC   per-frame tick count
 *
 * Anything that appears to touch only one of a pair is touching both.
 *
 * ---------------------------------------------------------------------------
 * The slot opcodes
 * ---------------------------------------------------------------------------
 *
 * 0x9D  FUN_00261cb8   slot = expr; offset = next_u32();
 *                      if (slot < 0x40) table[slot] = scriptBase + offset;
 *                      else fatal. Note the bound is 0x40, so 0x9D cannot write
 *                      the lead slot -- that is 0xA8's job.
 *
 * 0x9E  FUN_00261d18   sel = expr;
 *                      if (sel < 0 && currentSlot >= 0) slot = currentSlot;
 *                      else if (sel < 0x41)             slot = sel;
 *                      else fatal;
 *                      table[slot] = 0;
 *                      A negative selector means "retire the slot I am running
 *                      in", which is how a one-shot object script ends itself.
 *                      The bound here is 0x41, so 0x9E *can* clear slot 0x40.
 *
 * 0x9F  FUN_00261d88   slot = next_u8();   // a raw stream byte, not an expr
 *                      return table[slot] != 0;   fatal if slot >= 0x41.
 *
 * 0xA0  FUN_00261de0   first index in 0..0x3D whose entry is 0; -1 and a fatal
 *                      diagnostic when all 62 are taken.
 *
 * 0xA8  FUN_00262f38   offset = next_u32();
 *                      table[0x40] = scriptBase + offset;
 *                      FUN_00225bf0(&DAT_0058beb0, 10, 1);   // put the lead in
 *                      the state that runs it.
 *
 * 0xAA  FUN_00263118   FUN_00252d88(&DAT_0058beb0);   // cancel lead motion
 *                      table[0x40] = 0;
 *
 * scriptBase is iGpffffb0e8, the loaded script blob's base; slot entries are
 * absolute pointers, not offsets, once installed.
 *
 * ---------------------------------------------------------------------------
 * FUN_0025ce30 -- deferred triggers
 * ---------------------------------------------------------------------------
 *
 * Four fixed entries of 12 bytes at DAT_00571e40: { queue, cursor, count }.
 * Each frame, for each entry with a live queue, it looks at the record the
 * cursor points at:
 *
 *   - record[+0x02] == 0                     -> fire now
 *   - record[+0x02] bit 15 clear             -> fire when flag record[+0x02] is set
 *   - record[+0x02] bit 15 set               -> fire when the bits in uGpffffb0f4 match
 *
 * Firing advances a countdown at record[+0x00] by iGpffffb64c until it passes
 * record's threshold, then dispatches record[+0x04]: either into FUN_00237b38
 * (the dialogue/message-window driver, when the target lies inside the range
 * uGpffffbd70..uGpffffbd74) or into a free script slot via FUN_00261de0.
 *
 * This is a timed cutscene trigger table. Nothing in s01_e024's load-time
 * entries fills it, so a first port can stub it -- but it must be stubbed
 * visibly, because a scene that relies on it will silently do nothing.
 *
 * ---------------------------------------------------------------------------
 * Header words 3 and 4 are not per-frame
 * ---------------------------------------------------------------------------
 *
 * Worth stating because their names invite the mistake:
 *
 *   word 3, FUN_0025b978, is called from FUN_00252828 -- the *player's
 *   interaction probe*. It sets psGpffffb0d4 to the probed entity, special-cases
 *   type 0x38 by setting the entity's +0x1CC, then runs the entry. This is the
 *   "talk to / open / examine" hook.
 *
 *   word 4, FUN_0025b9a8, is called from FUN_00265ec0 -- entity *teardown*, and
 *   only when the entity's +0x02 has bit 0x8000. This is the "on destroy" hook.
 *
 * Neither belongs in the frame tick.
 */

/* Signatures, for reference; bodies are in src/. */

void FUN_0025b778(void); /* per-frame entry + slots 0x00-0x3D + slot 0x40 */
void FUN_0025b918(void); /* slots 0x3E and 0x3F */
void FUN_0025bc30(void); /* clear all 65 slots */
void FUN_0025ce30(void); /* deferred trigger queue */
