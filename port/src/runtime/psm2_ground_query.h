#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace orphen::port
{

  struct Psm2GroundHit
  {
    std::size_t triangleIndex = 0;
    std::size_t primitiveIndex = 0;
    float height = 0.0f;
    std::uint32_t leadingWord = 0;
    std::uint32_t terrainFlags = 0;
    bool sampledByOriginalTerrain = false;
    std::array<orphen::ported::psm2::Vec3, 3> vertices{};
    orphen::ported::psm2::Vec3 normal{};
  };

  struct Psm2TerrainQueryOptions
  {
    std::uint32_t rejectTerrainMask = 0;
    bool requireOriginalTerrainSample = false;
  };

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight);

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight,
                                                 const Psm2TerrainQueryOptions &options);

} // namespace orphen::port
