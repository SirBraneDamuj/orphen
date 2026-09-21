/*
 * Slope-map controller - FUN_0022dd60
 *
 * The debug menu calls this "MINI MAP DISP" (bit 2 of DAT_003555DD), but what
 * it builds is a slope map: the list of edges of the PSM2 collision mesh where
 * the surface turns by more than 49 degrees, plus every mesh boundary edge.
 * See docs/minimap_disp_is_a_slope_map.md for the whole feature.
 *
 * Mode 0 - build. Takes a vertex->primitive adjacency table, an 8-byte
 *   per-primitive edge scratch and the output edge list off the arena, runs the
 *   three passes, then restores the arena cursor. Every caller in the binary
 *   passes 0: FUN_002239c8 at 0x00223A68 (scene transition) and 0x00223BAC
 *   (the pad toggle), and the debug menu's toggle at 0x00269F8C.
 *
 * Mode 1 - draw. DEAD CODE: nothing in the binary calls it, and it is also
 *   unfinished. FUN_0022e578 and FUN_0022e7b8 fill the scratchpad staging
 *   packet at DAT_00355724 but never call FUN_00207DE8 to submit it, and they
 *   write 0xC80 into +0x0C where every working builder on this path writes a
 *   DMA target. FUN_0022e7b0 is a stub returning 0. Forced to run on hardware
 *   it prints "SLOPE>50" and draws nothing.
 *
 * The adjacency table goes at 0x01849A00, which is past the top of the
 * 0x00DC9A00..0x01849A00 arena and is the shared file-load staging buffer, so
 * it survives only until the next disc read.
 *
 * Original function: FUN_0022dd60
 */

#include "orphen_globals.h"

// Forward declarations for analyzed functions
extern void initialize_minimap_data_arrays(void);     // FUN_0022de88 - clear the adjacency table
extern void setup_minimap_grid_structure(void);       // FUN_0022def0 - fill the adjacency table
extern void finalize_minimap_setup(void);             // FUN_0022dfb0 - extract the slope edges
extern void FUN_0022e7b0(int addr);                   // stub, returns 0
extern void FUN_0022e638(void);                       // build the overlay matrix at 0x0058BC80
extern void FUN_0022e7b8(void);                       // stage the player marker quad
extern void FUN_0022e528(void);                       // stage one line per extracted edge
extern void FUN_0020bc78(int src_addr, int dst_addr); // copy a matrix

// Debug logging function (already identified)
extern void debug_output_formatter(int format_addr, ...); // debug_output_formatter (FUN_002681c0)

extern unsigned int uGpffffb7bc; // 0x0035572C - the GWORK arena bump cursor
extern unsigned int uGpffffbc78; // 0x00355BE8 - per-primitive 8-byte edge scratch
extern unsigned int uGpffffbc7c; // 0x00355BEC - extracted edge list, 8 bytes per edge
extern void *puGpffffbc74;       // 0x00355BE4 - vertex->primitive adjacency table
extern unsigned int uGpffffbc80; // 0x00355BF0 - extracted edge count
extern unsigned int uGpffffbc82; // 0x00355BF2
extern int iGpffffb718;          // 0x00355688 - PSM2 collision primitive count
extern int DAT_0031c210;         // overlay pan X, written to 0 here and read by nothing else
extern int DAT_0031c214;         // overlay pan Y, likewise
extern float DAT_0031c21c;       // overlay yaw, mode 1 sets it to the negated camera yaw
extern int DAT_0031c218;         // overlay zoom bias, added to a base scale of 10
extern float fGpffffb6d4;        // camera yaw
extern float fGpffff8580;        // yaw reference

/*
 * mode 0: build the slope edge list. mode 1: draw it - dead and unfinished.
 */
void minimap_display_controller(int mode)
{
  int saved_memory_ptr;

  saved_memory_ptr = uGpffffb7bc;

  if (mode == 0)
  {
    // 8 bytes of edge scratch per collision primitive, off the arena
    uGpffffbc78 = uGpffffb7bc + 3 & 0xfffffffc;
    uGpffffb7bc = uGpffffbc78 + iGpffffb718 * 8;

    // The adjacency table is parked past the top of the arena, on the shared
    // file-load staging buffer, so the next disc read stomps it.
    puGpffffbc74 = (void *)0x01849a00;

    uGpffffbc80 = 0; // no edges extracted yet

    initialize_minimap_data_arrays(); // pass 1: 0xFFFF the adjacency rows
    setup_minimap_grid_structure();   // pass 2: fill them from the primitives

    uGpffffbc82 = 0;
    DAT_0031c210 = 0; // reset the overlay pan/zoom/yaw
    uGpffffb7bc = uGpffffb7bc + 3 & 0xfffffffc;
    DAT_0031c214 = 0;
    DAT_0031c21c = 0.0;
    DAT_0031c218 = 0;
    uGpffffbc7c = uGpffffb7bc; // the edge list starts after the scratch

    finalize_minimap_setup(); // pass 3: extract the edges over 49 degrees
  }
  else if (mode == 1)
  {
    // "SLOPE>%d
" with 50 - the threshold, stated as the next whole degree
    debug_output_formatter(0x34c1c8, 0x32);

    DAT_0031c21c = -(fGpffffb6d4 - fGpffff8580); // face the overlay with the camera

    FUN_0022e7b0(0x31c210); // stub
    FUN_0022e638();         // compose translate * scale * rotate into 0x0058BC80
    FUN_0022e7b8();         // player marker
    FUN_0022e528();         // one line per edge
    FUN_0020bc78(0x58bd40, 0x58bc80);

    return;
  }

  // The arena is handed back: everything above it is scratch for the frame.
  uGpffffb7bc = saved_memory_ptr;
}
