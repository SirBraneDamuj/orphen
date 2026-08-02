#pragma once

// Native counterpart of src/FUN_0022c3d8.c (0x0022c3d8), the pass
// FUN_0022b5a8 runs over every 0x80-record once the sections are staged and
// before FUN_0022c6e8 derives the plane data.
//
// It does two things the port used to do neither of:
//
//   * turns the primitive's colour index into four packed per-vertex colours
//     out of the map's RGB palette, honouring flag 0x4 (per-vertex vs flat),
//   * expands each of the four material-slot selectors into the section E
//     record it names, including the two negative-selector forms that mean
//     "no texture, use this flat colour" rather than naming a slot at all.

#include "ported/psm2/psm2_runtime.h"

namespace orphen::ported::psm2
{

  void expandPsm2Materials(Psm2RuntimeState &state);

} // namespace orphen::ported::psm2
