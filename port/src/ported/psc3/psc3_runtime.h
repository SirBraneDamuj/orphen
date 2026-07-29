#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::psc3
{

  struct Psc3Triangle
  {
    std::array<orphen::ported::psm2::Vec3, 3> vertices{};
  };

  struct Psc3RuntimeModel
  {
    std::vector<Psc3Triangle> triangles;
    orphen::ported::psm2::Bounds3 bounds;
    std::size_t drawDescriptorCount = 0;
    std::size_t skippedDescriptorCount = 0;
  };

  bool hasPsc3Magic(std::span<const std::uint8_t> bytes);
  Psc3RuntimeModel loadPsc3Model(std::span<const std::uint8_t> bytes);

} // namespace orphen::ported::psc3
