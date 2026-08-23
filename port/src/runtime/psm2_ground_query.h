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

    // Which half of a quad answered, FUN_00227d28's +0xD0. The original packs
    // it into entity +0x0A as `primitive | (subTriangle << 14)`, so a dump's
    // +0x0A reads back as e.g. 0x4009 for "primitive 9, second half".
    std::size_t subTriangle = 0;

    float height = 0.0f;
    std::uint32_t leadingWord = 0;
    std::uint32_t terrainFlags = 0;
    bool sampledByOriginalTerrain = false;

    // The record's own stored slope, read at `+0x70 + subTriangle * 4`.
    // FUN_00227840:59 copies exactly that into the scan workspace's +0x54:
    //   uVar17 = *(u32 *)(rec + ((+0xD0 << 16) >> 14) + 0x70);
    //   *(u32 *)(ws + 0x54) = uVar17;
    // and the default when nothing is found is uGpffff8504 = pi/2.
    float slopeAngle = 1.570796012878418f;

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

  // FUN_00227070's "no ground" sentinel. The scan seeds workspace +0x50 with it
  // and FUN_002262c0:52 tests `128.0 <= result` to mean nothing was found.
  inline constexpr float kNoGroundHeight = 128.0f;

  // What FUN_00227070 writes back onto the entity.
  struct Psm2GroundSample
  {
    float height = kNoGroundHeight;      // the return value, and entity +0x4C
    bool found = false;                  // height < kNoGroundHeight

    // entity +0x0A, as the original packs it: `primitive | (half << 14)`, or -1.
    std::int32_t packedPrimitive = -1;
    std::int32_t primitiveIndex = -1;
    std::size_t subTriangle = 0;

    std::uint32_t terrainFlagsWinning = 0;  // entity +0x6C
    std::uint32_t terrainFlagsAll = 0;      // entity +0x70, ANDed over the samples

    // Workspace +0x08, which FUN_00227390 sets from +0x54 on the same line it
    // adopts a corner's terrain flags -- so it is the *winning* corner's slope,
    // not the last one scanned. FUN_002262c0 gates the whole upward-step branch
    // on it: `if ((float)puVar11[2] <= *(float *)(entity + 0x80))`.
    float slopeAngle = 1.570796012878418f;

    // entity +0x84..+0x90, written only on the four-corner path.
    std::array<float, 4> cornerHeights{};
    bool sampledFourCorners = false;
  };

  // FUN_00227070. `entityFlags04 & 2` selects a single sample at (x, y);
  // otherwise the scan runs at all four corners of a `radius`-sized square and
  // the **highest** answer wins. Ties OR their terrain flags together, and the
  // AND across every sample goes to entity +0x70.
  //
  // Not modelled: the FUN_00228cf0 pass the original runs afterwards, which can
  // raise the result and sets entity +0x0C bit 0x100 when it does.
  Psm2GroundSample FUN_00227070_sample_ground(const orphen::ported::psm2::Psm2RuntimeState &map,
                                              float x,
                                              float y,
                                              float feetHeight,
                                              float bodyHeight,
                                              float radius,
                                              std::uint16_t entityFlags04,
                                              std::uint32_t rejectTerrainMask);

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
