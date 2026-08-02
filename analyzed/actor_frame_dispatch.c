/*
 * Per-frame actor dispatch: how every non-player entity gets its behavior run
 * Original: FUN_00239ce0 (0x00239ce0)  the loop
 *           FUN_0023a068 (0x0023a068)  the freeze gate every handler opens with
 *           FUN_0023a568 (0x0023a568)  the fade-in/fade-out path
 * Caller:   FUN_002239c8 (0x002239c8)  the main-loop frame function
 *
 * Summary
 * - Actor behavior in this engine is NOT script. It is native code selected by
 *   the entity's type id through four function-pointer tables compiled into
 *   SLUS_200.11, and each of those handlers then dispatches a second time on the
 *   entity's state (+0x60) through a per-type-family table.
 * - The scene's SCR only spawns and configures. Once an entity exists, what it
 *   does each frame comes from here.
 *
 * ---------------------------------------------------------------------------
 * Where it sits in the frame
 * ---------------------------------------------------------------------------
 *
 * FUN_002239c8, in order:
 *
 *     (*DAT_0032536c)(4);              // profiler marker
 *     FUN_0025b778();                  // per-frame scene script + object-script slots
 *     FUN_00251ed8(0x58beb0, ...);     // the lead player, slot 0, on its own
 *     FUN_00224ff0();                  // pause menu
 *     FUN_00239ce0();                  // <-- this file: every other actor
 *     ...
 *     FUN_0025b918();                  // object-script slots 0x3E/0x3F, run late
 *
 * Note the lead player is updated by a *different* function (FUN_00251ed8) and
 * is not visited by FUN_00239ce0 at all. Slot 1 is skipped too: the loop starts
 * at &DAT_0058c260, which is 0x58BEB0 + 2 * 0x1D8, i.e. slot 2.
 *
 * iGpffffb64c and DAT_003555bc are the same address (gp = 0x00359F70, so
 * gp - 0x49B4 = 0x003555BC): the per-frame tick count, nominally 0x20. Handlers
 * use both spellings interchangeably.
 *
 * ---------------------------------------------------------------------------
 * FUN_00239ce0
 * ---------------------------------------------------------------------------
 *
 *     for (slot = 2; slot < 0x100; slot++) {
 *       iGpffffb650 = slot;                        // "current slot" global
 *       if (DAT_005a96b0[slot] <= 0)      continue; // free or merely allocated
 *       if (entity[+0x02] & 0x0800)       continue; // hidden
 *       if (entity[+0x04] & 0x4000)       continue; // suspended
 *       if (entity[+0x04] & 0x0800)       { FUN_0023a568(entity); continue; }
 *       dispatch_on_type(entity[+0x00])(entity);
 *     }
 *
 * The status test is `'\0' < (char)status`, a *signed* compare, so status 0xFF
 * (Allocated, from FUN_00265dc0 before FUN_00229c40 runs) is skipped as well as
 * status 0 (Free). Only status 1 -- a fully initialised, positive-typed entity
 * -- is ticked.
 *
 * ---------------------------------------------------------------------------
 * The type dispatch
 * ---------------------------------------------------------------------------
 *
 * Transcribed from the decompilation, preserving the branch order, because the
 * order is what decides the overlapping bands:
 *
 *     if      (type - 0x272 <= 0xFF)  FUN_002cfe08(e);   // map-streamed band A
 *     else if (type - 0x373 <= 0xFF)  FUN_002cfe08(e);   // map-streamed band B
 *     else if (type - 0x474 <= 0xFF)  FUN_002cfe08(e);   // map-streamed band C
 *     else if (type - 0x1F1 <  0x80)  PTR_LAB_0031ce90[type - 0x1F1](e);
 *     else if (type - 0x7C  <  0x7F)  PTR_FUN_0031c8b0[type - 0x7C ](e);
 *     else if (type - 0xFC  <= 0xF3)  PTR_LAB_0031cab0[type - 0xFC ](e);
 *     else if (type > 0)              PTR_FUN_0031c6c0[type - 1   ](e);
 *
 * Every subtraction is unsigned, and that matters:
 *
 * - type 0xFB is 0x7F past 0x7C, so it fails `< 0x7F`; it then underflows the
 *   0xFC test; so it lands in PTR_FUN_0031c6c0[0xFA]. This is almost certainly a
 *   table-boundary bug in the original, but it is observable behavior and a port
 *   has to reproduce it.
 * - type 0 and negative types reach none of the branches and are simply skipped.
 *
 * Table geometry, all four in the executable's data segment:
 *
 *     PTR_FUN_0031c6c0  0x0031C6C0  index type - 1       types 0x01-0x7B (+0xFB)
 *     PTR_FUN_0031c8b0  0x0031C8B0  index type - 0x7C    types 0x7C-0xFA
 *     PTR_LAB_0031cab0  0x0031CAB0  index type - 0xFC    types 0xFC-0x1EF
 *     PTR_LAB_0031ce90  0x0031CE90  index type - 0x1F1   types 0x1F1-0x270
 *
 * They are contiguous: 0x31C6C0 + 0x7B*4 = 0x31C8AC, and 0x31C8B0 + 0x7F*4 =
 * 0x31CAAC. So this is one table split by the compiler into four base pointers
 * with biased indices, which is why the seams have the 0xFB quirk.
 *
 * 0x00239E78 is the do-nothing default that most entries point at. It has no
 * decompiled body in src/ because it is a bare `jr ra`; treat it as an
 * implemented no-op rather than as an unknown handler.
 *
 * Handlers for the types s01_e024 spawns, read out of SLUS_200.11:
 *
 *     type 0x03,0x04,0x05,0x06,0x07 -> 0x0025AB68  FUN_0025ab68, party members
 *     type 0x3A                     -> 0x002D1EA8  FUN_002d1ea8, treasure chest
 *     type 0x62                     -> 0x002CD0A0  FUN_002cd0a0, an enemy
 *     type 0x272                    -> FUN_002cfe08 via the streamed band
 *
 * ---------------------------------------------------------------------------
 * The second dispatch, on state
 * ---------------------------------------------------------------------------
 *
 * Most type handlers are a thin shell: open the freeze gate, then index a state
 * table with entity +0x60.
 *
 *     FUN_0025ab68 (party):     PTR_LAB_0031e1d0[state]   12 entries
 *     FUN_002cd0a0 (type 0x62): PTR_FUN_00326660[state]   20+ entries
 *     FUN_002cfe08 (streamed):  PTR_LAB_003266d0[state]   10 entries, then data
 *
 * FUN_002d1ea8 is the exception: it switches on the animation id at +0xA0
 * directly and has no state table, which is what makes it the cheapest handler
 * to port first.
 *
 * ---------------------------------------------------------------------------
 * FUN_0023a068 -- the freeze gate
 * ---------------------------------------------------------------------------
 *
 *     bool FUN_0023a068(entity) {
 *       char remaining = entity[+0xBD];
 *       bool frozen = remaining != 0;
 *       if (frozen) {
 *         entity[+0xBD] = remaining - 1;
 *         entity[+0xA4] += DAT_003555bc;      // ticks accumulated while frozen
 *       }
 *       return frozen && remaining != 1;      // true => caller returns early
 *     }
 *
 * +0xBD is a hit-stop / freeze countdown in frames. The `remaining != 1` term
 * means the *last* frozen frame still runs the handler, so a behavior resumes on
 * the frame the counter reaches zero rather than one frame later.
 *
 * +0xA4 is the same halfword FUN_00225bc8 sets to 999 when it changes animation
 * state; the freeze path advances it so a state that is waiting on elapsed time
 * does not lose the frozen frames.
 *
 * ---------------------------------------------------------------------------
 * FUN_0023a568 -- the fade path
 * ---------------------------------------------------------------------------
 *
 * Taken instead of the type handler when entity +0x04 has bit 0x800. A two-phase
 * colour ramp using +0x62 as the ramp position and +0x138 as a packed RGB:
 *
 *     if (entity[+0x138] != 0x00FFFFFF) {          // fading IN
 *       entity[+0x62] += DAT_003555bc * 4;
 *       if (entity[+0x62] < 0x2000) {
 *         v = entity[+0x62] >> 5;                  // arithmetic, rounds toward 0
 *         entity[+0x138] = v << 16 | v << 8 | v;   // grey ramp
 *       } else {
 *         entity[+0x138] = 0x00FFFFFF;             // fully in; switch phase
 *         entity[+0x62] = 0x0FE0;
 *       }
 *     } else {                                     // fading OUT
 *       entity[+0x62] -= DAT_003555bc * 2;
 *       if (entity[+0x62] > 0x80) entity[+0x134] = entity[+0x62] >> 5;
 *       else                      FUN_00265ec0(entity);   // despawn
 *     }
 *
 * The `>> 5` divides by the nominal 0x20 tick, so the ramp is expressed in
 * frames. Note the two phases move +0x62 at different rates (4x in, 2x out).
 *
 * ---------------------------------------------------------------------------
 * Notes for the port
 * ---------------------------------------------------------------------------
 *
 * - The four tables are static data, so a port can read them out of the retail
 *   executable rather than transcribing 700-odd pointers. See
 *   port/src/ported/entity/actor_dispatch_table.cpp.
 * - Reproduce the branch order literally. Rewriting it as a switch or as sorted
 *   range checks silently fixes the 0xFB seam.
 * - iGpffffb650 (the current-slot global) is read by handlers deeper in the
 *   tree; a port that only implements leaf handlers can skip it, but it should
 *   be set if anything downstream is ported.
 */

/* Signatures, for reference; bodies are in src/. */

void FUN_00239ce0(void);                 /* the loop */
bool FUN_0023a068(int entity);           /* freeze gate; true => skip handler */
void FUN_0023a568(int entity);           /* fade in, then fade out, then despawn */
void FUN_002cfe08(int entity);           /* map-streamed props, ids >= 0x272 */
