#pragma once

#include "ported/psm2/psm2_runtime.h"

namespace orphen::ported::psm2
{

  // Native counterpart of src/FUN_0022c6e8.c (0x0022c6e8), reduced to the derived geometry fields the harness consumes.
  void buildPsm2DerivedGeometry(Psm2RuntimeState &state);

  // FUN_0022c6e8's per-primitive body on its own: bounds, centre, radius and
  // the one or two plane records, recomputed from wherever the vertices are
  // now. The triangle list is not rebuilt, because a primitive's corner
  // indices do not change when it moves.
  //
  // FUN_00208450 needs exactly this for every primitive of a group it has just
  // transformed. Its own version is inlined and works off VU0 for the normal,
  // but computes the same fields from the same corners.
  bool rebuildPsm2Primitive(Psm2RuntimeState &state, std::size_t primitiveIndex);

} // namespace orphen::ported::psm2
