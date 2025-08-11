/*
 * Reversed Function: FUN_0020a2c0
 * Proposed Name: build_segmented_render_packet (WIP – particle/trail or ribbon style renderer)
 *
 * Original Prototype:
 *   uint * FUN_0020a2c0(int param_1, undefined8 param_2, int param_3, uint *param_4);
 *
 * High-Level Summary:
 *   Constructs PS2 VIF/GIF packet data for a capped number (<=10) of dynamically interpolated segments
 *   derived from a circular keyframe buffer. Performs interpolation, compaction, optional reordering /
 *   culling, builds bounding volumes, allocates a render job node, and emits encoded command / data
 *   words into an output buffer. Returns advanced packet pointer.
 *
 * See extensive commentary inside for inferred structure field meanings and pipeline phases.
 */

#include <stdint.h>

typedef unsigned int uint;
typedef unsigned long ulong;
typedef long long undefined8;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;

// External (unanalyzed) functions
extern void FUN_0020b748(int, undefined8, undefined4);
extern ulong FUN_0020b600(void *, undefined8, void *);
extern long FUN_0020a270(undefined8, int);
extern void FUN_00209f90(undefined8, int);
extern int FUN_0030bd20(float);
extern void FUN_0020c240(undefined8, void *);
extern void FUN_0026bf90(int);

// Globals (extern) – real definitions in raw dump units
extern float DAT_00351fe8, DAT_00351fec, DAT_00351ff0, DAT_00351ff4, DAT_00351ff8;
extern float DAT_003555a0, DAT_003555a4, DAT_0058bed8;
extern uint DAT_0035567c;
extern int DAT_003556ac, DAT_003556b0, DAT_7000000c;
extern long *DAT_70000000;

// Original symbol (delegated to preserve single executable copy)
extern uint *FUN_0020a2c0(int, undefined8, int, uint *);

/*
 * build_segmented_render_packet:
 *   Thin analyzed wrapper retaining original behavior by delegation. Future passes will migrate and
 *   rename internals here once structure layouts are formalized.
 */
uint *build_segmented_render_packet(int entityPtr, undefined8 trailSystemPtr, int controlPtr, uint *packetOut)
{
  return FUN_0020a2c0(entityPtr, trailSystemPtr, controlPtr, packetOut);
}

/*
 * Inference Snapshot (Phase Breakdown):
 *   Phase 1: Update base keyframe arrays (FUN_0020b748) using current time & count at +0x6f8.
 *   Phase 2: Iterate keyframes performing conditional interpolation to produce new blended entries
 *            (writes factor to +0x6f0/+0x6f4, outputs to arrays at +0x460/+0x4e0 mirrored into +0x140/+0x280 when active).
 *   Phase 3: Compact & duplicate active or special-marked segments into contiguous leading region.
 *   Phase 4: Reorder / cull via FUN_0020b600 and FUN_0020a270 producing index list at +0x710 (cap 10).
 *   Phase 5: Reorder data blocks in-place to match final index ordering.
 *   Phase 6: Compute / merge bounding boxes (+0x5a0.. +0x5bc) from segment vectors (first vs subsequent logic).
 *   Phase 7: Allocate job node from DAT_70000000 (stride 0x40-like) and fill mode / flags / pointers.
 *   Phase 8: Emit VIF/GIF style packet words & per-segment data to packetOut (multiple blocks with
 *            opcodes 0x6eXX / 0x6cXX / 0x1400XXXX). Color packing differs for mode==2 vs others.
 *   Phase 9: Distance / LOD scaling using bounding extents + entity distance/height producing clamp 1..0xFFF.
 *   Phase 10: Final pointer patch & return advanced packet pointer.
 *
 * TODO / Open Questions:
 *   - Exact semantics of arrays at +0x460 vs +0x4e0 vs +0x520 for secondary path when activation mask set.
 *   - Meaning of plVar8[0] type states 2 vs 3 and plVar8[1] base values 0x2d or 0x0d plus bit or'ing.
 *   - Confirm that emitted 0x6e00c024 / 0x6e00c02e / 0x6e00c039 correspond to UNPACK counts for vectors,
 *     colors, and attribute halfwords respectively; formalize enumeration.
 *   - Identify param_1 structure fields at +0x2c (flag), +0x2d (color toggle?), +0x2e (counter), +0x70 (bit flags).
 *   - Replace magic scales 126.0 and 1024.0 with named constants (e.g., COLOR_SCALE, LERP_FP_SCALE).
 */
