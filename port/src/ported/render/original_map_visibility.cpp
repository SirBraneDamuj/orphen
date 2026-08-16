#include "ported/render/original_map_visibility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace orphen::ported::render
{
  namespace
  {
    using orphen::ported::psm2::DRecord78;
    using orphen::ported::psm2::DRecord80;
    using orphen::ported::psm2::kFadeCeiling;
    using orphen::ported::psm2::kFadeFloor;
    using orphen::ported::psm2::Psm2RuntimeState;

    // Primitives carrying flag 0x80 are billboards: FUN_00209b20 spins their
    // vertices about their own XY centroid to face the camera before anything
    // else looks at them. fGpffff8074 (0x00351FE4) is pi/2.
    constexpr float kBillboardYawBias = 1.57079601f;
    constexpr std::uint32_t kBillboardBit = 0x80;

    struct PrimitiveCorners
    {
      std::array<Vec3, 4> world{};
      std::array<Vec3, 4> view{};
      std::size_t count = 3;
    };

    // FUN_00209b20.
    void FUN_00209b20_billboard(PrimitiveCorners &corners, float cameraYaw)
    {
      float centreX = 0.0f;
      float centreY = 0.0f;
      for (std::size_t index = 0; index < corners.count; ++index)
      {
        centreX += corners.world[index].x;
        centreY += corners.world[index].y;
      }
      centreX /= static_cast<float>(corners.count);
      centreY /= static_cast<float>(corners.count);

      const float angle = cameraYaw - kBillboardYawBias;
      const float sine = std::sin(angle);
      const float cosine = std::cos(angle);

      for (std::size_t index = 0; index < corners.count; ++index)
      {
        const float offsetX = corners.world[index].x - centreX;
        const float offsetY = corners.world[index].y - centreY;
        corners.world[index].x = centreX + offsetX * cosine - offsetY * sine;
        corners.world[index].y = centreY + offsetX * sine + offsetY * cosine;
      }
    }

    // FUN_00209928. One edge of the polygon against the four corners of the
    // player's probe rectangle. Returns true when at least one corner is not
    // strictly outside, i.e. the edge fails to separate.
    //
    // Note the space mismatch, which is in the original: the polygon comes in
    // as view-space XY while the probe rectangle is the perspective-divided
    // player position. Both are small numbers near the origin for a primitive
    // that actually covers the player, so the test behaves, but it is not a
    // like-for-like comparison and is reproduced rather than corrected.
    bool FUN_00209928_edgeOverlaps(const Vec3 &edgeStart,
                                   const Vec3 &edgeEnd,
                                   const std::array<Vec3, 2> &probe)
    {
      // The original's index arithmetic walks the rectangle corners as
      // (x0,y0), (x1,y0), (x0,y1), (x1,y1).
      static constexpr std::array<std::pair<int, int>, 4> kCornerOrder{
          std::pair<int, int>{0, 0}, {1, 0}, {0, 1}, {1, 1}};

      for (const auto &[xIndex, yIndex] : kCornerOrder)
      {
        const float cross = (edgeEnd.x - edgeStart.x) * (probe[yIndex].y - edgeStart.y) -
                            (edgeEnd.y - edgeStart.y) * (probe[xIndex].x - edgeStart.x);
        if (cross <= 0.0f)
        {
          return true;
        }
      }
      return false;
    }

    // FUN_002099d8. Overlap only when every edge fails to separate.
    //
    // The sign convention assumes a consistent screen-space winding, so a
    // back-facing primitive passes every edge trivially. That is what makes
    // the near wall of a room fade when the camera is outside it.
    bool FUN_002099d8_overlapsPlayer(const PrimitiveCorners &corners, const std::array<Vec3, 2> &probe)
    {
      for (std::size_t index = 0; index < corners.count; ++index)
      {
        const std::size_t next = (index + 1) % corners.count;
        if (!FUN_00209928_edgeOverlaps(corners.view[index], corners.view[next], probe))
        {
          return false;
        }
      }
      return true;
    }

  } // namespace

  std::vector<MapDrawItem> FUN_00209140_buildDrawList(Psm2RuntimeState &map,
                                                      const ViewProjection &viewProjection,
                                                      const MapVisibilityInput &input,
                                                      MapVisibilityReport *report)
  {
    MapVisibilityReport localReport;
    MapVisibilityReport &stats = report != nullptr ? *report : localReport;
    stats = {};
    stats.primitiveCount = map.DAT_003556ac_dRecords80.size();

    // FUN_00209140:97-126. Two probe points on the player: one at the feet
    // plus DAT_00351FC4 and one at the head, nudged apart in view space by
    // the same amount, then perspective divided.
    std::array<Vec3, 2> probeView{};
    std::array<Vec3, 2> probe{};
    {
      const Vec3 &player = input.DAT_0058bed0_playerPosition;
      probeView[0] = viewProjection.toViewSpace({player.x, player.y, player.z + visibility::kPlayerProbeOffset});
      probeView[0].x -= visibility::kPlayerProbeOffset;
      probeView[1] = viewProjection.toViewSpace({player.x, player.y, player.z + input.DAT_0058bf08_playerHeadOffset});
      probeView[1].x += visibility::kPlayerProbeOffset;

      for (std::size_t index = 0; index < probe.size(); ++index)
      {
        const float depth = probeView[index].z;
        const float inverseDepth = depth != 0.0f ? 1.0f / depth : 0.0f;
        probe[index] = {probeView[index].x * inverseDepth, probeView[index].y * inverseDepth, depth};
      }
    }

    // The camera yaw the billboard fixup needs, recovered from the view
    // matrix rather than threaded through as another global.
    const float cameraYaw = std::atan2(viewProjection.view.at(0, 1), viewProjection.view.at(0, 0));

    std::vector<std::vector<MapDrawItem>> buckets(visibility::kDepthBucketCount);
    std::size_t drawCount = 0;

    for (std::size_t primitiveIndex = 0; primitiveIndex < map.DAT_003556ac_dRecords80.size(); ++primitiveIndex)
    {
      DRecord80 &record80 = map.DAT_003556ac_dRecords80[primitiveIndex];
      const DRecord78 &record78 = map.DAT_003556b0_dRecords78[primitiveIndex];

      if ((record80.primitiveFlags & visibility::kHiddenBit) != 0)
      {
        ++stats.hiddenSkipped;
        continue;
      }

      // FUN_00209140:135-136. Only in mode 0xC.
      if (input.battleMode && (record78.terrainFlags & visibility::kBattleHideBit) != 0)
      {
        ++stats.hiddenSkipped;
        continue;
      }

      const Vec3 viewCentre = viewProjection.toViewSpace(record80.center);
      const float radius = record80.radius;

      // FUN_00209140:149-155. Sphere against the near plane, the draw
      // distance, and the four side planes.
      if (viewCentre.z < visibility::kNearReject - radius)
      {
        ++stats.nearRejected;
        continue;
      }
      if (viewCentre.z > input.drawDistance + radius)
      {
        ++stats.drawDistanceRejected;
        continue;
      }
      const float horizontalLimit = (radius + viewCentre.z) * input.horizontalCullHalfTangent;
      const float verticalLimit = (radius + viewCentre.z) * input.verticalCullHalfTangent;
      if (viewCentre.x > horizontalLimit || viewCentre.y > verticalLimit ||
          viewCentre.x < -horizontalLimit || viewCentre.y < -verticalLimit)
      {
        ++stats.sideRejected;
        continue;
      }

      // FUN_00209140:156-181. Corner count comes from flag 0x4000, not from
      // comparing indices.
      PrimitiveCorners corners;
      corners.count = (record80.primitiveFlags & visibility::kTriangleBit) != 0 ? 3 : 4;
      for (std::size_t corner = 0; corner < corners.count; ++corner)
      {
        const std::uint16_t vertexIndex = record80.vertexIndices[corner];
        if (static_cast<std::size_t>(vertexIndex) < map.DAT_0035569c_sectionCRecords.size())
        {
          corners.world[corner] = map.DAT_0035569c_sectionCRecords[vertexIndex].position;
        }
      }

      if ((record80.primitiveFlags & kBillboardBit) != 0)
      {
        FUN_00209b20_billboard(corners, cameraYaw);
      }

      for (std::size_t corner = 0; corner < corners.count; ++corner)
      {
        corners.view[corner] = viewProjection.toViewSpace(corners.world[corner]);
      }

      // The view-space AABB the VU microprogram at 0xE0 hands back in
      // pauVar5[0xe] / pauVar5[0xf].
      Vec3 minimum = corners.view[0];
      Vec3 maximum = corners.view[0];
      for (std::size_t corner = 1; corner < corners.count; ++corner)
      {
        minimum.x = std::min(minimum.x, corners.view[corner].x);
        minimum.y = std::min(minimum.y, corners.view[corner].y);
        minimum.z = std::min(minimum.z, corners.view[corner].z);
        maximum.x = std::max(maximum.x, corners.view[corner].x);
        maximum.y = std::max(maximum.y, corners.view[corner].y);
        maximum.z = std::max(maximum.z, corners.view[corner].z);
      }

      const bool nearClipped = viewCentre.z < radius + visibility::kNearClipMargin;
      if (nearClipped)
      {
        ++stats.nearClipped;
      }

      // The occlusion fade. The two paths do not share a condition set: the
      // near-plane path (FUN_0020a2c0:668-683) drops the screen-band and
      // depth tests and asks only about the blend flag, the overlap and the
      // height. That is the looser of the two, and it is the one a wall
      // between an outside camera and the player usually takes.
      const bool aboveFadeHeight =
          record78.bounds.max.z > input.DAT_0058bed0_playerPosition.z + visibility::kFadeHeightBias;
      const bool blended = (record80.primitiveFlags & visibility::kNeverFadeBit) != 0;

      bool shouldFade = false;
      if (!input.fadeDisabled)
      {
        ++stats.fadeCandidates;
        if (blended)
        {
          ++stats.fadeBlockedByBlend;
        }
        else if (!aboveFadeHeight)
        {
          ++stats.fadeBlockedByHeight;
        }
        else
        {
          const bool inBand = nearClipped ||
                              (probeView[1].z > maximum.z &&
                               minimum.x < visibility::kScreenBandRight &&
                               maximum.x > visibility::kScreenBandLeft &&
                               probeView[0].y > minimum.y &&
                               maximum.y > probeView[1].y);
          if (!inBand)
          {
            ++stats.fadeBlockedByBand;
          }
          else
          {
            shouldFade = FUN_002099d8_overlapsPlayer(corners, probe);
            if (!shouldFade)
            {
              ++stats.fadeBlockedByOverlap;
            }
          }
        }
      }

      std::uint8_t emittedFade = 0;
      if (shouldFade)
      {
        if (record80.dynamicFade > kFadeFloor)
        {
          --record80.dynamicFade;
        }
        emittedFade = record80.dynamicFade;
        ++stats.faded;
      }
      else if (record80.dynamicFade < kFadeCeiling)
      {
        ++record80.dynamicFade;
        emittedFade = record80.dynamicFade;
      }

      // DAT_00355700 caps everything, and also changes the sort key.
      bool globalFadeCapped = false;
      if (input.globalFadeCap != 0 && input.globalFadeCap < record80.dynamicFade)
      {
        emittedFade = input.globalFadeCap;
        globalFadeCapped = true;
      }

      // FUN_00209140:351-377. The bucket key is the projected screen depth,
      // shifted down by four and clamped into the 4096-entry table.
      const float sortDepth = input.globalFadeCap != 0
                                  ? maximum.z - 1.0f
                                  : maximum.z + record80.blendTerm;

      int bucket = 0;
      if (sortDepth < visibility::kMinimumSortDepth)
      {
        bucket = 0x7fffffff;
      }
      else
      {
        const float key = viewProjection.screenDepth(sortDepth);
        bucket = key > 0.0f ? static_cast<int>(key) : 0;
      }
      bucket >>= 4;
      bucket = std::clamp(bucket, 1, visibility::kDepthBucketCount - 1);

      // Rotate the plane normal into view space -- rows 0..2 only, since a
      // normal ignores the translation row. Front-facing means it points back
      // toward the camera, which with +z forward is a negative z.
      {
        const Vec3 &normal = record78.unitNormal[0];
        const float normalViewZ = normal.x * viewProjection.view.at(0, 2) +
                                  normal.y * viewProjection.view.at(1, 2) +
                                  normal.z * viewProjection.view.at(2, 2);
        if (normalViewZ < 0.0f)
        {
          ++stats.drawnFrontFacing;
        }
        else
        {
          ++stats.drawnBackFacing;
        }
      }

      buckets[static_cast<std::size_t>(bucket)].push_back(
          {primitiveIndex, emittedFade, bucket, nearClipped, globalFadeCapped});
      ++drawCount;
    }

    // Buckets are walked low to high, which is far to near: the sort key grows
    // as the primitive gets closer. That is the original's painter order.
    std::vector<MapDrawItem> drawList;
    drawList.reserve(drawCount);
    for (const auto &bucket : buckets)
    {
      drawList.insert(drawList.end(), bucket.begin(), bucket.end());
    }

    stats.drawn = drawList.size();
    return drawList;
  }

} // namespace orphen::ported::render
