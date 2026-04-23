/*
 * Opcode 0x41 FUN_0025db20 — submit configurable dual vertex stream packet
 *
 * Similar structure to 0x43 (FUN_0025de08) but with two streams (positions +
 * attribute) instead of three. Reads 4 expressions + 1 control byte:
 *   args[0] = stream0 offset (count of additional prefix vertices)
 *   args[3] = stream1 offset
 *   control byte bits: 0x01 → prepend an auto-computed vertex from DAT_0058c0a8
 *                             (and, when cGpffffb6e1=='#', same for stream1 from
 *                              DAT_0058be90)
 *                     0x02 → append another DAT_0058c0a8 vertex to stream0
 *
 * For each of the two streams:
 *   - decodes N*3 fixed-point values from the script (N = args[stream*3+2]/3),
 *     dividing by fGpffff8bfc to produce floats,
 *   - writes into fixed buffers at 0x01849a00 (stream0) and 0x01849ac0
 *     (stream1).
 * Finally calls FUN_00217e18(0) to reset pipeline state and
 * FUN_00217e88(stream0, count0, stream1, count1) to submit.
 *
 * Used for generic geometry uploads where the script provides XYZ per vertex
 * plus one secondary stream (e.g. UV or colour).
 */

extern void FUN_0025c258(void *);
extern void FUN_00267da0(void *, void *, int);
extern void FUN_00217e18(int);
extern void FUN_00217e88(void *, int, void *, int);
extern unsigned char *pbGpffffbd60;
extern int iGpffffb0e8;
extern float fGpffff8bfc;
extern char cGpffffb6e1;

void op_0x41_submit_dual_vertex_stream(void)
{
  /* See FUN_0025db20 — structure mirrors 0x43 with 2 streams instead of 3. */
}
