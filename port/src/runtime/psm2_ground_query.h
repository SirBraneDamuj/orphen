#pragma once

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

  struct Psm2BlockerHit
  {
    std::size_t triangleIndex = 0;
    std::size_t primitiveIndex = 0;
    std::uint32_t record80Flags = 0;
    std::array<orphen::ported::psm2::Vec3, 3> vertices{};
    orphen::ported::psm2::Vec3 normal{};
  };

  // The actor's vertical extent, staged by FUN_00227390 in its collision
  // workspace: +0x2C is entity +0x28 (the feet) and +0x30 is that plus entity
  // +0x58 (the top of the head). FUN_00227840 reads +0x30 and will not settle
  // on a surface above it (`c.le.S f0,0x30(s0)` at 0x002279b4 / 0x00227a44).
  struct Psm2ActorBody
  {
    float feetHeight = 0.0f;
    float headHeight = 0.0f;
  };

  struct Psm2TerrainQueryOptions
  {
    std::uint32_t rejectTerrainMask = 0;
    bool requireOriginalTerrainSample = false;

    // Unset for callers that are not standing anywhere -- the viewer, the
    // camera ground clamp, spawn selection. Those get the plain ground answer
    // with no head limit and no ceiling test.
    std::optional<Psm2ActorBody> body;
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

  std::optional<Psm2BlockerHit> queryPsm2ActiveBlockerAlong(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                            float startX,
                                                            float startY,
                                                            float endX,
                                                            float endY,
                                                            float baseHeight,
                                                            float characterHeight,
                                                            float radius);

} // namespace orphen::port
