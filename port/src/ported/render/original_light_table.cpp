#include "ported/render/original_light_table.h"

#include <algorithm>

namespace orphen::ported::render
{

  std::int32_t LightTable::allocateFrom(std::size_t first) const
  {
    // Both originals are the same loop with a different starting index, and
    // both stop at 16 rather than at 16 - first, so the high allocator has
    // thirteen slots to give out.
    for (std::size_t index = first; index < kSlotCount; ++index)
    {
      if (slots_[index].radius == 0.0f)
      {
        return static_cast<std::int32_t>(index);
      }
    }
    return -1;
  }

  void LightTable::noteRadius(std::uint32_t index, float radius)
  {
    if (radius == 0.0f || index >= kSlotCount)
    {
      return;
    }
    everLive_ |= 1u << index;
    peakRadius_ = std::max(peakRadius_, radius);
  }

  void LightTable::reset()
  {
    slots_ = {};
    everLive_ = 0;
    peakRadius_ = 0.0f;
  }

} // namespace orphen::ported::render
