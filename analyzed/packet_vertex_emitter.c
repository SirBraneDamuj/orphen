/*
 * Packet vertex emitter for PS2 GS/VIF (analysis of FUN_00211230)
 *
 * Original: void FUN_00211230(void)
 *
 * Summary
 * -------
 * Builds one or more GIF/VIF packets for each A/J-record, iterating up to four
 * directional sub-groups ("rings"). Emits vertex positions, colors/params, and
 * indices according to flags and per-ring micro-params embedded in the A/J record.
 *
 * Key flags in A.flags (pfVar27[0x1C] as uint):
 * - 0x0004  USE_SECTION_B_POSITIONS
 *           Emitted positions come from Section B via indirection table at DAT_00355698.
 *           Address = DAT_003556A4 + (u16_map[index] * 0x10). Else positions use Section C.
 * - 0x0040  HAS_ATTR_GROUP
 *           Per-ring attribute control byte (bVar2 = *(i+0x0B)) nonzero (bits 0x70).
 *           Sets attribute count pl[6] to {1,2,3} based on (bVar2 & 0x40) and (bVar2 & 0x10).
 *           Also ORs PRIM control with 0x40 and echoes bit back into A.flags.
 * - 0x4000  RING_VERT_COUNT_3
 *           Per-ring vertex emission count is 3 (else 4). Stored in pl[7].
 * - 0x8000  PRIM_BASE_ALT
 *           Base PRIM field uses 0x0D instead of 0x2D.
 * - 0x40000 PARAM_MODE
 *           Set if any selector nibble (byte from DAT_00355694 at D-index) has (val & 0x0F) != 0,
 *           or forced by external per-record state; cleared when none and conditions allow.
 *           Selects VIF tag 0x1400013B instead of 0x1400014B when >1 active.
 *
 * Other observed behaviors:
 * - Active-subgroup detection: scans four per-ring control bytes at A+0x38.. (pf+0x0E stepping by 3),
 *   collects count into pl[9], and records the last active ring index into *(u8*)(pl+0x4C).
 * - If no rings active, sets A.flags |= 0x20 and skips.
 * - When HAS_ATTR_GROUP path taken, attribute byte at (i+0x0A) sets pl[4] and special-case 0x80 -> pl[6]=0.
 * - For USE_SECTION_B_POSITIONS, positions are 3 floats loaded from Section B row selected by
 *   map[ D_index ] (D_index is a u16 read from the ring-local index stream at A+0x24).
 * - Color/param stream: if *pl == 2 (a PRIM mode), writes packed RGB from A+0x30.. multiplied by
 *   per-vertex weights at A+0x10..; else writes ARGB words using pl[4] in alpha high byte.
 * - Index stream: when *pl != 2, writes u16 indices copied verbatim from ring-local stream (A+0x30..: u16[?]).
 * - Emits per-ring packet header fields into puVar21 (VIF/GIF), including computed GIFtag (pl[2]).
 *
 * Globals (kept with original names, descriptive alias in comments):
 * - DAT_003556AC: base of A/J records (stride 0x80)
 * - DAT_00355688: number of A/J records
 * - DAT_00355694: selector/param table used to test low nibble per D index (selector_table)
 * - DAT_00355698: u16 indirection map from D index -> Section B row index (c_to_b_map)
 * - DAT_0035569C: base of per-index color/params (packed words)
 * - DAT_003556A4: base of Section B rows (float[3] @ +0x10 stride)
 * - DAT_003556B0: per-record external state (flags/bytes), used to force PARAM_MODE
 * - DAT_0035572C: scratch / packet buffer cursor (aligned to 16 bytes)
 * - DAT_70000000: GS packet builder context stack (10 qwords per level)
 *
 * Notes for exporter parity:
 * - Bit 0x0004 is the vertex-source switch we need offline. Runtime chooses B vs C per ring; offline
 *   we can only approximate per-face selection by grouping faces by A/J record and ring membership.
 * - Bit 0x0040 marks attribute payloads present; we don’t currently reproduce those in OBJ.
 * - Bits 0x4000/0x8000 influence PRIM/type and ring vertex count; informative but not critical for OBJ.
 * - Bit 0x40000 identifies a mode that alters later packet selection; not needed for geometry.
 */

// Keep original function name for reference
// void FUN_00211230(void);

// Externs left as-is until separately analyzed
extern void FUN_0026bf90(int);
extern void FUN_0026bfc0(int);
extern void FlushCache(int);

/*
 * Pseudocode sketch with descriptive names (omits packet write details):
 */
void emit_packets_for_j_records(void) {
    // Align packet cursor
    // Iterate A/J records
    //  - read A.flags
    //  - detect up to 4 active rings (stores last active index and count)
    //  - compute PARAM_MODE (0x40000) based on selector_table and external byte unless A.flags has 0x80
    //  - for ring in 0..3:
    //      if ring inactive: continue
    //      setup pl context
    //      ringVerts = (A.flags & 0x4000) ? 3 : 4
    //      primBase  = (A.flags & 0x8000) ? 0x0D : 0x2D
    //      attrCtl = *(u8*)(ringPtr+0x0B), attrA = *(u8*)(ringPtr+0x0A)
    //      if (attrCtl & 0x70) { A.flags|=0x40; set attr count pl[6] = 1/2/3 per (0x40/0x10); set pl[4]=attrA (0x80->0) }
    //      if (ringType < 0) mark pl=2 else pl=3 and OR 0x10 into prim flags
    //      if (A.flags & 0x0004) use Section B position rows via c_to_b_map[D_index] else use Section C
    //      write ringVerts position triples scaled by 126 and packed
    //      write color/params: if (*pl==2) write weighted RGB else write ARGB words (alpha from pl[4])
    //      if (*pl != 2) write u16 index stream copied from ring stream
    //      choose tag 0x1400014B or 0x1400013B depending on PARAM_MODE and number of active rings
    //      close packet, update counters
}
