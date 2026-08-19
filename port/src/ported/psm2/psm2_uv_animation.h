#pragma once

// The UV animation stepper -- how every scrolling and frame-stepped texture in
// the game moves.
//
//   src/FUN_00225940.c          the stepper, one call per frame
//   src/FUN_002257c0.c          the link write it makes when a track retires
//   src/FUN_00208f28.c          the map's call site, just before FUN_00209140
//   src/FUN_0020c810.c:210      the per-entity call site
//   src/FUN_0020eec0.c:97-110   the upload, VIF 0x640702B0 into VU1 0x2B0
//
// The result is seven (u, v) pairs scaled by 1/64 that VU1 adds to the baked
// per-vertex texture coordinates. Nothing else moves: the geometry, the
// material table, the GS texture pages and the CLUTs all stay exactly as they
// were loaded. See the structures in psm2_runtime.h for the script format.

#include "ported/psm2/psm2_runtime.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::psm2
{

  // FUN_00225940. `frameTicks` is sGpffffb64c, the same DAT_003555BC every
  // other per-frame stepper counts down by.
  void FUN_00225940_step_uv_animation(std::vector<UvAnimationTrack> &tracks,
                                      std::uint32_t frameTicks);

  // The offset a material slot's byte 9 selects, in **normalised** texture
  // units ready to add to a 0..1 coordinate -- the original's 1/64 texel
  // divided again by the 256-texel page.
  //
  // Byte 9 is 1-based: zero means "no animation", which is 2907 of s01_e012's
  // 3486 textured slots. Anything past the end of the script also reads as no
  // animation rather than clamping onto the last track.
  struct UvOffset
  {
    float u = 0.0f;
    float v = 0.0f;
  };
  UvOffset uvOffsetForMaterialByte9(const std::vector<UvAnimationTrack> &tracks,
                                    std::uint8_t byte9);

} // namespace orphen::ported::psm2
