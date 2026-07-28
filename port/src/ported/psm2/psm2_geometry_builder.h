#pragma once

#include "ported/psm2/psm2_runtime.h"

namespace orphen::ported::psm2
{

  // Native counterpart of src/FUN_0022c6e8.c (0x0022c6e8), reduced to the derived geometry fields the harness consumes.
  void buildPsm2DerivedGeometry(Psm2RuntimeState &state);

} // namespace orphen::ported::psm2
