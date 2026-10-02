#pragma once

// The fly camera's map tile pick: which map primitive a ray through the fly
// view lands on, and the tint that marks it. PC-only, like the rest of the fly
// view; it reads the map and never writes it.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_map_visibility.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace orphen::harness
{

  struct MapPickHit
  {
    std::size_t primitiveIndex = 0;
    // Along the ray, in viewer units, so it compares with ProbeHit::distance.
    float distance = 0.0f;
  };

  // The nearest primitive in `drawList` the viewer-space ray crosses, tested
  // against the triangles the renderer draws (record80 firstTriangle /
  // triangleCount), so what it picks is what is on screen. The list is the fly
  // view's own -- the game's culled list or F3's whole map -- so a primitive
  // the view is not drawing cannot be picked through.
  std::optional<MapPickHit> pickMapPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                                             const std::vector<orphen::ported::render::MapDrawItem> &drawList,
                                             const orphen::ported::psm2::Vec3 &origin,
                                             const orphen::ported::psm2::Vec3 &direction);

  // Tints a primitive in place: amber for the one under the pointer, cyan with
  // an outline that shows through walls for the selected one. Emits
  // viewer-space vertices; expects the fly camera's matrices to be current.
  void drawMapPrimitiveHighlight(const orphen::ported::psm2::Psm2RuntimeState &map,
                                 std::size_t primitiveIndex,
                                 bool selected);

} // namespace orphen::harness
