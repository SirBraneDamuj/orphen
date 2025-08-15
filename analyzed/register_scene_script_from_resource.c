// Analyzed re-expression of FUN_0025d380
// Original signature: void FUN_0025d380(undefined8 param_1)
// Purpose: Insert a script pointer (from a resource record keyed by DAT_00354d50)
//          into the first free slot of the scene object scripts array (DAT_00355cf4),
//          then emit a system/event notification via FUN_0025d1c0 and FUN_002663a0.
//
// Summary of behavior (inferred from src/FUN_0025d380.c and neighbors):
// - Resolves a resource block using FUN_00267f90(DAT_00354d50). This returns a base pointer to a
//   variable-length record. Offset +8 within that record holds a pointer to a structural script.
// - Writes that script pointer into DAT_00355cf4[0] if empty, else scans indices 1..0x3d for the
//   first zero entry and writes it there. If no free slot found (all 0..0x3d occupied), it skips write.
// - Calls FUN_0025d1c0(0, event_code, 0). The original passes param_1 (seen as 0x0C from caller
//   FUN_0025b728) as the second (ushort) argument. We treat this as an event code.
// - Calls FUN_002663a0(0x510). Purpose not yet analyzed (likely a system tick/flush).
//
// Side effects:
// - Writes to global DAT_00355cf4 array (scene object scripts), placing a new pointer at the first
//   free index among [0..0x3d].
// - Triggers system/event functions FUN_0025d1c0 and FUN_002663a0.
//
// Parameters:
// - event_code: a small integer (ushort range) forwarded to FUN_0025d1c0; observed caller uses 0x0C.
//
// PS2-specific notes:
// - FUN_00267f90 walks a table in scratch/working memory rooted at puGpffffbdcc. It uses
//   4-byte length fields (rounded up) and terminators (0xFFFFFFFF) typical of PS2-era resource
//   lists. The returned pointer addresses are in EE RAM.
//
// Cross-references and globals (see globals.json):
// - DAT_00354d50: resource ID or selector used with FUN_00267f90 to fetch the active block.
// - DAT_00355cf4: base of scene object scripts array (0x3e entries), initialized in FUN_0025b390.
// - FUN_0025d1c0: event/notification helper (sets fields at 0x571dc0/0x571dd0 and calls FUN_0025d0e0).
// - FUN_002663a0: unknown system call; invoked with 0x510 here.
// - Related display: FUN_0025b778 prints "Subproc:%3d [%5d]" using index and *(ptr-4) before
//   calling FUN_0025bc68(ptr). This places this function on the path that populates those entries.
//
// Keep unresolved FUN_* names and DAT_* globals as-is until analyzed elsewhere.

#include <stdint.h>
#include <stddef.h>

// Unanalyzed externs (preserve original names)
extern uint8_t *FUN_00267f90(uint32_t param_1);
extern void FUN_0025d1c0(long which_buffer, uint16_t event_code, int arg);
extern void FUN_002663a0(uint32_t code);

// Globals (unalias until confidently named elsewhere)
extern uint32_t DAT_00354d50; // resource selector feeding FUN_00267f90
extern int *DAT_00355cf4;     // base pointer to scene object scripts array (int pointers)

// Descriptive wrapper for FUN_0025d380
void register_scene_script_from_resource(uint16_t event_code)
{
  // Original: int iVar1 = FUN_00267f90(DAT_00354d50);
  uint8_t *resource_base = FUN_00267f90(DAT_00354d50);
  if (!resource_base)
  {
    // Nothing to register
    FUN_0025d1c0(0, event_code, 0);
    FUN_002663a0(0x510);
    return;
  }

  // At offset +8: pointer to structural script (interpreted later by FUN_0025bc68)
  int script_ptr = *(int *)(resource_base + 8);

  if (DAT_00355cf4)
  {
    // Slot 0 fast-path
    if (DAT_00355cf4[0] == 0)
    {
      DAT_00355cf4[0] = script_ptr;
    }
    else
    {
      // Search first free in [1..0x3d]
      for (int idx = 1; idx <= 0x3d; ++idx)
      {
        if (DAT_00355cf4[idx] == 0)
        {
          DAT_00355cf4[idx] = script_ptr;
          goto notify;
        }
      }
      // All full: fallthrough to notify without insertion
    }
  }

notify:
  // Original tail: FUN_0025d1c0(0, param_1, 0); FUN_002663a0(0x510);
  FUN_0025d1c0(0, event_code, 0);
  FUN_002663a0(0x510);
}
