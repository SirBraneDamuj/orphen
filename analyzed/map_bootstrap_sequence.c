/*
 * Map bootstrap: what happens when the game changes scene
 * Original: FUN_0022a418 (0x0022a418), 423 lines
 * Supporting: FUN_0025b390 (0x0025b390) scene/SCR loader
 *             FUN_0025b6d0 (0x0025b6d0) run SCR header word 0
 *             FUN_0025b728 (0x0025b728) run SCR header word 1
 *             FUN_0022b2c0 (0x0022b2c0) request a map change
 *
 * Summary
 * - FUN_0022a418 is the real in-game scene loader, driven from the main loop
 *   FUN_002239c8 when a map change is pending. The debug-menu path documented in
 *   docs/map_loading_flow.md is a different, shorter route into the same data.
 * - It takes no parameters. The destination is already staged in globals by
 *   FUN_0022b2c0 before the pending bit is raised.
 *
 * ---------------------------------------------------------------------------
 * The pending map change
 * ---------------------------------------------------------------------------
 *
 * FUN_0022b2c0(x, y, z, majorMap, minorMap, flags):
 *
 *     DAT_00325340 = x;  DAT_00325344 = y;  DAT_00325348 = z;
 *     DAT_00354d80 = DAT_003551f4;   // remember where we came from
 *     DAT_00354d84 = DAT_003551f0;
 *     DAT_003551f4 = majorMap;       // where we are going
 *     DAT_003551f0 = minorMap;
 *     DAT_003551ec = flags | 1;      // bit 0 = "a position was supplied"
 *
 * So **the spawn position travels with the map change request**, not with the
 * destination scene. A scene has no inherent player start; it inherits one from
 * whatever door, warp or cutscene sent you there.
 *
 * This is worth stating loudly for the port: booting cold into a scene has no
 * spawn point to read. Either the destination's script teleports the lead with
 * opcode 0xAB, or the position has to come from somewhere else entirely.
 *
 * Correction: analyzed/ops/0x8B_trigger_positional_audio_with_coords.c and
 * analyzed/ops/0x8C_trigger_positional_audio_simple.c both describe FUN_0022b2c0
 * as spatial audio. It is not; it is the map-change request above. Those two
 * opcodes are scripted warps, and their "3 audio params" are majorMap, minorMap
 * and flags.
 *
 * ---------------------------------------------------------------------------
 * Ordered bootstrap sequence
 * ---------------------------------------------------------------------------
 *
 *  1. Drain streaming. Spin on FUN_00206c28 / FUN_00206a90 until pending DVD
 *     work is finished, pumping FUN_00203b20 meanwhile.
 *  2. Mode select. DAT_003555d3 = (DAT_003551ec & 0x20000) != 0. This is the
 *     "dual mode" flag that shifts several later lookups; it also decides
 *     whether the SCR blob is loaded at the base of the script arena or
 *     appended after an existing one.
 *  3. Per-map handler stage 7. DAT_0032536c is a function pointer chosen from
 *     PTR_LAB_003252b8 by the MCB entry's type; it is called with stage codes
 *     through the whole sequence (7, 2, 0, 1, 3 here; 4, 5, 6 per frame from
 *     FUN_002239c8). This is a per-map class callback, entirely separate from
 *     the SCR VM.
 *  4. Terrain stream. FUN_00222898(major, minor, 0x20000) reads the map data
 *     through the sector table at DAT_00315b00 / DAT_00315b04.
 *  5. MCB entry lookup. FUN_0022a288(major, minor) walks the section returned by
 *     FUN_0022a238 in 0x10-byte records until short[0] matches minor.
 *     FUN_0022a360 then caches fields out of it and picks the handler pointer.
 *  6. SCR load, via FUN_0025b390 -- see below.
 *  7. Descriptor cleanup. Two passes walk resource chains in 0x2C strides
 *     resetting load states below 'd' and releasing handles via FUN_00221ce8.
 *  8. Handler stage 2.
 *  9. Lead player rebuild. DAT_0058beb0 is the *type id* of pool slot 0 (the
 *     first halfword of the entity). FUN_002661a8 loads its model, then
 *     FUN_00229c40(0x58beb0, type) initialises slot 0 from the descriptor, and:
 *
 *         DAT_0058beb2 = 1;            // entity +0x02 flags
 *         DAT_0058bf50 = 1;            // entity +0xA0 animation id = stand
 *         DAT_0058beb4 = (DAT_0058beb4 & 0xffe7) | 0x3000;   // +0x04
 *         DAT_0058bf24 = 0x8000000;    // +0x74
 *
 * 10. Spawn placement. If DAT_003551ec bit 0 is set, the staged position is
 *     copied into slot 0:
 *
 *         DAT_0058bed0 = DAT_00325340;   // entity +0x20  world X
 *         DAT_0058bed4 = DAT_00325344;   // entity +0x24  world Y
 *         DAT_0058bed8 = DAT_00325348;   // entity +0x28  vertical
 *
 *     Then unconditionally DAT_0058befc = FUN_00227070(x, y, slot0), i.e.
 *     entity +0x4C is resampled against terrain, and +0x50 (DAT_0058bf00) is set
 *     to match. Collision flags at +0x0C (DAT_0058bebc) are cleared.
 * 11. Pool reset. Slots 10..255 are cleared: the loop starts at DAT_0058d120
 *     (= 0x58BEB0 + 10 * 0x1D8) and the status bytes at DAT_005a96ba
 *     (= 0x5A96B0 + 10), running 0xF6 times with FUN_00267e78(slot, 0x1d8).
 *     This is a third independent confirmation that the entity stride is 0x1D8
 *     and that the script-visible range is [10, 256) -- see
 *     analyzed/entity_pool_and_descriptors.c.
 * 12. Handler stage 0, then **FUN_0025b6d0** (SCR header word 0), then stage 1.
 * 13. A long block of per-scene state resets (fades, colours, counters).
 * 14. Handler stage 3, then **FUN_0025b728** (SCR header word 1).
 * 15. Camera and lead placement, skipped entirely on major map 0x0C:
 *
 *         FUN_00257fc0();                                     // detach camera
 *         FUN_002582d0(DAT_0058bed0, DAT_0058bed4, DAT_0058bed8);
 *
 *     FUN_002582d0 is the same teleport_lead_and_camera that script opcode 0xAB
 *     calls, so the bootstrap default and a scripted teleport share one path.
 * 16. Fixed system entities are initialised at known addresses -- 0x58C260 and
 *     0x58C438 as type 0x68, 0x58C9C0 as type 0x6A, and on the field also
 *     0x58CD70 as type 0x58 positioned at (48.0, 40.0, DAT_003524e0).
 * 17. DAT_00354d78/DAT_00354d7c record the map that is now current.
 *
 * Two script entrypoints therefore run at load, from different places in the
 * sequence and with a large amount of state setup between them. A port that
 * runs only one of them will be missing half the initialisation.
 *
 * ---------------------------------------------------------------------------
 * FUN_0025b390 -- loading the scene script
 * ---------------------------------------------------------------------------
 *
 * Called as FUN_0025b390(mcbEntry, mode).
 *
 *   scrFileId = *(short *)(mcbEntry + 4);
 *   FUN_00223268(1, scrFileId, 0x1849a00);     // archive read, compressed
 *   FUN_002f3118(0x1849a00, 0x1859a00);        // headerless LZ decode
 *   alignedSize = (DAT_00355720 + 3) & ~3;     // DAT_00355720 = decoded size
 *   FUN_00267da0(DAT_00355058, 0x1859a00, alignedSize);   // copy into the arena
 *
 * So the MCB entry's halfword at +4 is the SCR file index, and the decode uses
 * the same headerless LZ as every other resource. The arena is capped at
 * 0x20000 bytes including the trailing tables; overflow is a fatal
 * FUN_0026bfc0.
 *
 * After the copy:
 *   DAT_00355058 = script base -- all header offsets are relative to this
 *   DAT_00355cf4 = script base + alignedSize -- the object-script pointer table,
 *                  cleared as 65 dwords (offsets 0x00..0x100)
 *   DAT_00355060 = work data, cleared as 128 dwords (offsets 0x00..0x1FC)
 *
 * When mcbEntry is null or its file id is zero, a 0x2C-byte stub script is
 * synthesised in place whose every header word points at offset 0x2C and whose
 * body is the single byte 0x04 (block end) -- an empty scene that still runs.
 *
 * ---------------------------------------------------------------------------
 * The five entrypoints
 * ---------------------------------------------------------------------------
 *
 *   word 0  FUN_0025b6d0  scene init, at load. Also zeroes 0x30 bytes at
 *                         0x571E40 and resets DAT_0035504c, the 0x4E lookup
 *                         table counter, so the table starts empty each scene.
 *   word 1  FUN_0025b728  scene start, at load. Afterwards, if DAT_003551ec
 *                         bit 2 is set and DAT_00354d2c != 0xF, registers
 *                         script 0xC via FUN_0025d380.
 *   word 2  FUN_0025b778  per-frame tick; also walks the DAT_00355cf4 table.
 *   word 3  FUN_0025b978  actor state primary.
 *   word 4  FUN_0025b9a8  actor state secondary.
 *
 * Each is `FUN_0025bc68(*(int *)(DAT_00355058 + word*4) + DAT_00355058)`.
 *
 * PS2 notes
 * - 0x1849A00 and 0x1859A00 are fixed staging buffers in the script memory
 *   window (0x01C40000-0x01C8FFFF is the script arena proper; these sit below
 *   it), used compressed-in / decoded-out for every scene load.
 * - The spin loops in step 1 call FUN_00203b20 rather than blocking, so the
 *   frame pump keeps running while the DVD catches up.
 *
 * Unresolved callees keep their original labels. The step ordering above is read
 * directly from the decompiled control flow; the semantic labels on the handler
 * stage codes (7/2/0/1/3) are inferred from call position only, since
 * PTR_LAB_003252b8's targets are not yet analyzed.
 */

#include <stdint.h>

/* Destination staged by the map-change request. */
extern float DAT_00325340; // spawn X
extern float DAT_00325344; // spawn Y
extern float DAT_00325348; // spawn Z
extern uint32_t DAT_003551ec; // request flags; bit 0 = position supplied
extern uint16_t DAT_003551f4; // destination major map
extern uint16_t DAT_003551f0; // destination minor map
extern uint16_t DAT_00354d80; // previous major map
extern uint16_t DAT_00354d84; // previous minor map

/* Entity pool slot 0 -- the lead player. */
extern int16_t DAT_0058beb0; // +0x00 type id
extern float DAT_0058bed0;   // +0x20 world X
extern float DAT_0058bed4;   // +0x24 world Y
extern float DAT_0058bed8;   // +0x28 vertical
extern float DAT_0058befc;   // +0x4C ground height
extern float DAT_0058bf00;   // +0x50 ground height, previous

/* Scene script state. */
extern uint32_t *DAT_00355058; // script base; header words are relative to this
extern int DAT_00355cf4;       // object-script pointer table, 65 entries
extern uint8_t *DAT_00355060;  // work data, 128 dwords
extern int DAT_0035504c;       // 0x4E lookup table entry count

extern void FUN_0025bc68(long entryAddress);
extern void FUN_00257fc0(void);                       // detach camera tracking
extern void FUN_002582d0(float x, float y, float z);  // teleport lead and camera
extern float FUN_00227070(float x, float y, void *entity);

// NOTE: Original signature:
//   void FUN_0022b2c0(undefined4, undefined4, undefined4, undefined4, undefined4, uint)
void request_map_change(float x, float y, float z,
                        uint16_t majorMap, uint16_t minorMap, uint32_t flags)
{
  DAT_00325340 = x;
  DAT_00325348 = z;
  DAT_00354d80 = DAT_003551f4;
  DAT_00354d84 = DAT_003551f0;
  DAT_003551f4 = majorMap;
  DAT_003551f0 = minorMap;
  DAT_003551ec = flags | 1;
  DAT_00325344 = y;
}

// NOTE: Original signature: void FUN_0025b6d0(void)
void run_scene_script_init(void)
{
  extern char DAT_003555d3;
  extern uint32_t DAT_00355064;
  extern void FUN_00267e78(void *destination, uint32_t byteCount);

  if (DAT_003555d3 != '\0')
  {
    *(uint32_t *)(DAT_00355060 + 0x68) = 0x32;
  }
  FUN_00267e78((void *)0x571e40, 0x30);
  DAT_0035504c = 0; // the 0x4E lookup table starts empty every scene
  DAT_00355064 |= 0x6000;
  FUN_0025bc68((long)(DAT_00355058[0] + (int)DAT_00355058)); // header word 0
}

// NOTE: Original signature: void FUN_0025b728(void)
void run_scene_script_start(void)
{
  extern uint32_t DAT_00354d2c;
  extern void FUN_0025d380(int scriptId);

  FUN_0025bc68((long)(DAT_00355058[1] + (int)DAT_00355058)); // header word 1

  if ((DAT_003551ec & 4) != 0 && DAT_00354d2c != 0xF)
  {
    FUN_0025d380(0xC);
  }
}
