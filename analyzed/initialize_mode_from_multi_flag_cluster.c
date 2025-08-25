// Analysis of FUN_0026d5b0
// Role:
//   Initializes a specific game/system mode when ANY of a cluster of flags (0x50C, 0x50D, 0x50E) is set.
//   On activation it assigns a cohesive set of global state values:
//     - DAT_003551f4 = 0x0C   (interpreted elsewhere as a map/scene or mode ID; frequently compared to 0x0C)
//     - DAT_003551f0 = 10     (companion value; often paired with 0x0C in conditionals)
//     - DAT_003551ec = 0x2001 (bitfield of engine/system state flags; 0x2001 pattern appears in normal processing contexts)
//     - DAT_003555de = get_flag_state(0x50F) (captures an auxiliary boolean mode bit; later branches test DAT_003555de == 0)
//
// Flag Cluster Behavior:
//   0x50C / 0x50D / 0x50E: any one being set triggers assignment of a consistent state tuple
//     (DAT_003551f4=0x0C, DAT_003551f0=10, DAT_003551ec=0x2001).
//   0x50F: secondary flag captured into DAT_003555de for later conditional logic.
//
// Downstream Usage (factual observations):
//   - The (0x0C,10) pair is tested in multiple functions (e.g., FUN_002356b8, FUN_002036d8, FUN_002374d0).
//   - DAT_003551ec value 0x2001 appears as a baseline state bit pattern in other initialization routines.
//   - DAT_003555de is read in FUN_00214300 and FUN_002f2198 affecting conditional branches.
//
// Side Effects:
//   Writes four global variables; no parameters and no returns. Safe no-op if none of the flags are set.
//
// TODO Naming:
//   - Replace raw global names with semantic identifiers once mode/scene taxonomy clarified (e.g., current_mode_id, current_mode_variant, system_state_flags, aux_mode_feature_enabled).
//   - Introduce named constants for FLAG_ENABLE_MODE_A/B/C (0x50C–0x50E) and FLAG_MODE_SUBFEATURE (0x50F).
//
// Original signature preserved below for cross-reference.

#include <stdint.h>

extern unsigned int FUN_00266368(unsigned int flag_id); // get_flag_state

// Raw globals (provisional)
extern int DAT_003551f4;           // mode/map ID
extern int DAT_003551f0;           // mode parameter / variant
extern unsigned int DAT_003551ec;  // system state bitfield
extern unsigned char DAT_003555de; // auxiliary mode flag

void initialize_mode_from_multi_flag_cluster(void)
{
  if (FUN_00266368(0x50C) || FUN_00266368(0x50D) || FUN_00266368(0x50E))
  {
    DAT_003551f4 = 0x0C;
    DAT_003551f0 = 10;
    DAT_003555de = (unsigned char)FUN_00266368(0x50F);
    DAT_003551ec = 0x2001; // baseline state pattern
  }
}

// Original function name wrapper
void FUN_0026d5b0(void)
{
  initialize_mode_from_multi_flag_cluster();
}
