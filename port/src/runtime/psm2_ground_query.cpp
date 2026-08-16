#include "runtime/psm2_ground_query.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orphen::port
{
  namespace
  {

    constexpr float kBarycentricEpsilon = -0.0005f;
    constexpr std::uint32_t kOriginalTerrainSampleBit = 0x800;

    // FUN_00227840 splits the sampled primitives on this bit: it sets the
    // workspace's +0x22 winding selector to 0xFF for them, and FUN_00227d28
    // then reverses every edge test (`0.0 < cross` rejects instead of
    // `cross < 0.0`). Reversed winding in the XY projection means the surface
    // faces down -- these are the ceilings. In s01_e024, 427 of the 435
    // primitives carrying it point straight down and not one up-facing
    // primitive has it.
    //
    // The original never records a ceiling's height as ground: the 0x100 branch
    // at 0x0022799c clears the "have recorded" flag instead of writing +0x50.
    // A ceiling at or below the head only *stops* the scan, so the query falls
    // out holding 128.0 -- no ground -- which is what makes FUN_00227390 fail
    // and bumps the actor's head.
    constexpr std::uint32_t kCeilingBit = 0x100;
    constexpr std::uint32_t kRecord80HiddenBit = 0x20;

    // record78 +0x04 bit 0x100 -- **not** the same field as kCeilingBit, which
    // is bit 0x100 of +0x00. A surface above an actor's feet is a step-up, and
    // this is the bit that says the actor may be stood up onto it.
    //
    // Derived, not read out of the decompilation: FUN_00227840's scan takes the
    // first cell-list candidate at or below the head and the port has no cell
    // list, so it needs some rule to choose. s01_e012 supplies both halves of
    // the answer. Two hidden collision quads sit at z = -1.20 over two beds,
    // identical in every field (`lead=0xa20`, no material slot) except this
    // bit: #9 over Magnus's bed has 0x50620100, #339 over the other has
    // 0x50620000. `eeMemory.bin` puts slot 81 on the first at -1.200 and leaves
    // slot 82 on the deck at -1.500 under the second.
    //
    // Supporting evidence: all 99 primitives in s01_e012 carrying it sit
    // between -1.40 and -0.05, above the -1.50 deck -- the profile of low
    // step-up surfaces. s01_e024 has none, so this cannot move it.
    constexpr std::uint32_t kStepUpBit = 0x100;
    constexpr float kSteepBlockerMaxAbsNormalZ = 0.5f;
    constexpr float kBlockerHeightPadding = 0.05f;
    constexpr float kGeometryEpsilon = 0.000001f;

    struct Vec2
    {
      float x = 0.0f;
      float y = 0.0f;
    };

    const orphen::ported::psm2::Vec3 &positionForIndex(const orphen::ported::psm2::Psm2RuntimeState &map, std::uint16_t index)
    {
      return map.DAT_0035569c_sectionCRecords.at(index).position;
    }

    orphen::ported::psm2::Vec3 cross(const orphen::ported::psm2::Vec3 &left, const orphen::ported::psm2::Vec3 &right)
    {
      return {left.y * right.z - left.z * right.y,
              left.z * right.x - left.x * right.z,
              left.x * right.y - left.y * right.x};
    }

    orphen::ported::psm2::Vec3 normalize(const orphen::ported::psm2::Vec3 &value)
    {
      const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
      if (length <= std::numeric_limits<float>::epsilon())
      {
        return {};
      }
      return {value.x / length, value.y / length, value.z / length};
    }

    // FUN_00227840:115-171 walks the cell's primitive list and takes the **first**
    // candidate whose height is at or below the head (`+0x30 < fVar16` bails to
    // the next one, anything else ends the scan). The port has no cell list, so
    // it cannot reproduce that order and has to choose a rule.
    //
    // For a caller with a body, that rule is **the highest eligible surface**,
    // where a surface above the feet is only eligible if it carries kStepUpBit.
    // Nearest-to-the-reference -- what this used to do -- can never reach a
    // surface an actor is meant to be standing on top of while its own height
    // still reads as the floor below it: s01_e012's script drops Magnus at
    // z = -1.500 with opcode 0x55 and leaves the engine to put him the 0.30 up
    // onto the bed, and `eeMemory.bin` has him at -1.200.
    //
    // Callers with no body -- the viewer, the camera clamp, spawn selection --
    // are not standing anywhere, so they keep the nearest-height answer.
    float candidateScore(float height,
                         float referenceHeight,
                         bool sampledByOriginalTerrain,
                         const std::optional<Psm2ActorBody> &body)
    {
      const float terrainPenalty = sampledByOriginalTerrain ? 0.0f : 100000.0f;
      if (body.has_value())
      {
        return terrainPenalty + (body->headHeight - height);
      }
      return terrainPenalty + std::abs(height - referenceHeight);
    }

    float squaredDistance(const Vec2 &left, const Vec2 &right)
    {
      const float deltaX = left.x - right.x;
      const float deltaY = left.y - right.y;
      return deltaX * deltaX + deltaY * deltaY;
    }

    float cross2d(const Vec2 &origin, const Vec2 &first, const Vec2 &second)
    {
      return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
    }

    bool pointOnSegment2d(const Vec2 &point, const Vec2 &first, const Vec2 &second)
    {
      if (std::abs(cross2d(first, second, point)) > kGeometryEpsilon)
      {
        return false;
      }
      return point.x >= std::min(first.x, second.x) - kGeometryEpsilon &&
             point.x <= std::max(first.x, second.x) + kGeometryEpsilon &&
             point.y >= std::min(first.y, second.y) - kGeometryEpsilon &&
             point.y <= std::max(first.y, second.y) + kGeometryEpsilon;
    }

    bool segmentsIntersect2d(const Vec2 &firstStart, const Vec2 &firstEnd, const Vec2 &secondStart, const Vec2 &secondEnd)
    {
      const float firstCrossStart = cross2d(firstStart, firstEnd, secondStart);
      const float firstCrossEnd = cross2d(firstStart, firstEnd, secondEnd);
      const float secondCrossStart = cross2d(secondStart, secondEnd, firstStart);
      const float secondCrossEnd = cross2d(secondStart, secondEnd, firstEnd);

      if (std::abs(firstCrossStart) <= kGeometryEpsilon && pointOnSegment2d(secondStart, firstStart, firstEnd))
      {
        return true;
      }
      if (std::abs(firstCrossEnd) <= kGeometryEpsilon && pointOnSegment2d(secondEnd, firstStart, firstEnd))
      {
        return true;
      }
      if (std::abs(secondCrossStart) <= kGeometryEpsilon && pointOnSegment2d(firstStart, secondStart, secondEnd))
      {
        return true;
      }
      if (std::abs(secondCrossEnd) <= kGeometryEpsilon && pointOnSegment2d(firstEnd, secondStart, secondEnd))
      {
        return true;
      }

      return ((firstCrossStart < 0.0f && firstCrossEnd > 0.0f) || (firstCrossStart > 0.0f && firstCrossEnd < 0.0f)) &&
             ((secondCrossStart < 0.0f && secondCrossEnd > 0.0f) || (secondCrossStart > 0.0f && secondCrossEnd < 0.0f));
    }

    float squaredDistancePointSegment2d(const Vec2 &point, const Vec2 &segmentStart, const Vec2 &segmentEnd)
    {
      const float segmentX = segmentEnd.x - segmentStart.x;
      const float segmentY = segmentEnd.y - segmentStart.y;
      const float segmentLengthSquared = segmentX * segmentX + segmentY * segmentY;
      if (segmentLengthSquared <= kGeometryEpsilon)
      {
        return squaredDistance(point, segmentStart);
      }

      const float projection = ((point.x - segmentStart.x) * segmentX + (point.y - segmentStart.y) * segmentY) / segmentLengthSquared;
      const float clampedProjection = std::clamp(projection, 0.0f, 1.0f);
      const Vec2 closestPoint{segmentStart.x + segmentX * clampedProjection, segmentStart.y + segmentY * clampedProjection};
      return squaredDistance(point, closestPoint);
    }

    float squaredDistanceSegmentSegment2d(const Vec2 &firstStart, const Vec2 &firstEnd, const Vec2 &secondStart, const Vec2 &secondEnd)
    {
      if (segmentsIntersect2d(firstStart, firstEnd, secondStart, secondEnd))
      {
        return 0.0f;
      }

      return std::min({squaredDistancePointSegment2d(firstStart, secondStart, secondEnd),
                       squaredDistancePointSegment2d(firstEnd, secondStart, secondEnd),
                       squaredDistancePointSegment2d(secondStart, firstStart, firstEnd),
                       squaredDistancePointSegment2d(secondEnd, firstStart, firstEnd)});
    }

    bool pointInsideTriangle2d(const Vec2 &point, const Vec2 &first, const Vec2 &second, const Vec2 &third)
    {
      const float v0x = second.x - first.x;
      const float v0y = second.y - first.y;
      const float v1x = third.x - first.x;
      const float v1y = third.y - first.y;
      const float v2x = point.x - first.x;
      const float v2y = point.y - first.y;
      const float denominator = v0x * v1y - v1x * v0y;
      if (std::abs(denominator) <= kGeometryEpsilon)
      {
        return false;
      }

      const float u = (v2x * v1y - v1x * v2y) / denominator;
      const float v = (v0x * v2y - v2x * v0y) / denominator;
      const float w = 1.0f - u - v;
      return u >= kBarycentricEpsilon && v >= kBarycentricEpsilon && w >= kBarycentricEpsilon;
    }

    bool movementCapsuleIntersectsTriangle2d(const Vec2 &start, const Vec2 &end, const Vec2 &first, const Vec2 &second, const Vec2 &third, float radius)
    {
      if (pointInsideTriangle2d(end, first, second, third))
      {
        return true;
      }

      const float radiusSquared = radius * radius;
      return squaredDistanceSegmentSegment2d(start, end, first, second) <= radiusSquared ||
             squaredDistanceSegmentSegment2d(start, end, second, third) <= radiusSquared ||
             squaredDistanceSegmentSegment2d(start, end, third, first) <= radiusSquared;
    }

  } // namespace

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight)
  {
    return queryPsm2GroundAt(map, x, y, referenceHeight, {});
  }

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight,
                                                 const Psm2TerrainQueryOptions &options)
  {
    std::optional<Psm2GroundHit> bestHit;
    float bestScore = std::numeric_limits<float>::max();

    for (std::size_t triangleIndex = 0; triangleIndex < map.derivedTriangles.size(); ++triangleIndex)
    {
      const auto &triangle = map.derivedTriangles[triangleIndex];
      if (triangle.primitiveIndex >= map.DAT_003556b0_dRecords78.size())
      {
        continue;
      }

      const auto first = positionForIndex(map, triangle.vertexIndices[0]);
      const auto second = positionForIndex(map, triangle.vertexIndices[1]);
      const auto third = positionForIndex(map, triangle.vertexIndices[2]);

      const float v0x = second.x - first.x;
      const float v0y = second.y - first.y;
      const float v1x = third.x - first.x;
      const float v1y = third.y - first.y;
      const float v2x = x - first.x;
      const float v2y = y - first.y;
      const float denominator = v0x * v1y - v1x * v0y;
      if (std::abs(denominator) <= std::numeric_limits<float>::epsilon())
      {
        continue;
      }

      const float u = (v2x * v1y - v1x * v2y) / denominator;
      const float v = (v0x * v2y - v2x * v0y) / denominator;
      const float w = 1.0f - u - v;
      if (u < kBarycentricEpsilon || v < kBarycentricEpsilon || w < kBarycentricEpsilon)
      {
        continue;
      }

      const auto &record78 = map.DAT_003556b0_dRecords78[triangle.primitiveIndex];
      const bool sampledByOriginalTerrain = (record78.leadingWord & kOriginalTerrainSampleBit) != 0;
      if (options.requireOriginalTerrainSample && !sampledByOriginalTerrain)
      {
        continue;
      }
      if ((record78.terrainFlags & options.rejectTerrainMask) != 0)
      {
        continue;
      }

      const float height = first.z * w + second.z * u + third.z * v;

      if ((record78.leadingWord & kCeilingBit) != 0)
      {
        // A ceiling is never ground. One that has come down inside the body
        // kills the sample outright, which is the port's stand-in for the
        // original's scan aborting at it. The original's literal test is only
        // `height <= head`; requiring it to also be above the feet is what
        // keeps the underside of the floor being stood on out of it, since the
        // port has no cell list whose order would have found that floor first.
        if (options.body.has_value() && height > options.body->feetHeight && height <= options.body->headHeight)
        {
          return std::nullopt;
        }
        continue;
      }

      // FUN_00227840 will not settle on a surface above the head, so an upper
      // storey cannot be mistaken for the ground under the actor's feet.
      if (options.body.has_value() && height > options.body->headHeight)
      {
        continue;
      }

      // Nor onto a step-up the map has not marked as one. See kStepUpBit.
      if (options.body.has_value() && height > options.body->feetHeight &&
          (record78.terrainFlags & kStepUpBit) == 0)
      {
        continue;
      }

      const float score =
          candidateScore(height, referenceHeight, sampledByOriginalTerrain, options.body);
      if (score >= bestScore)
      {
        continue;
      }

      const auto firstEdge = orphen::ported::psm2::subtract(second, first);
      const auto secondEdge = orphen::ported::psm2::subtract(third, first);
      bestScore = score;
      bestHit = Psm2GroundHit{triangleIndex,
                              triangle.primitiveIndex,
                              height,
                              record78.leadingWord,
                              record78.terrainFlags,
                              sampledByOriginalTerrain,
                              {first, second, third},
                              normalize(cross(firstEdge, secondEdge))};
    }

    return bestHit;
  }

  std::optional<Psm2BlockerHit> queryPsm2ActiveBlockerAlong(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                            float startX,
                                                            float startY,
                                                            float endX,
                                                            float endY,
                                                            float baseHeight,
                                                            float characterHeight,
                                                            float radius)
  {
    const Vec2 movementStart{startX, startY};
    const Vec2 movementEnd{endX, endY};
    if (squaredDistance(movementStart, movementEnd) <= kGeometryEpsilon)
    {
      return std::nullopt;
    }

    const float actorMinHeight = baseHeight + kBlockerHeightPadding;
    const float actorMaxHeight = baseHeight + std::max(characterHeight, 0.0f);
    const float clampedRadius = std::max(radius, 0.0f);

    for (std::size_t triangleIndex = 0; triangleIndex < map.derivedTriangles.size(); ++triangleIndex)
    {
      const auto &triangle = map.derivedTriangles[triangleIndex];
      if (triangle.primitiveIndex >= map.DAT_003556ac_dRecords80.size())
      {
        continue;
      }

      const auto &record80 = map.DAT_003556ac_dRecords80[triangle.primitiveIndex];
      if ((record80.primitiveFlags & kRecord80HiddenBit) != 0)
      {
        continue;
      }

      const auto first = positionForIndex(map, triangle.vertexIndices[0]);
      const auto second = positionForIndex(map, triangle.vertexIndices[1]);
      const auto third = positionForIndex(map, triangle.vertexIndices[2]);
      const auto firstEdge = orphen::ported::psm2::subtract(second, first);
      const auto secondEdge = orphen::ported::psm2::subtract(third, first);
      const auto normal = normalize(cross(firstEdge, secondEdge));
      if (std::abs(normal.z) > kSteepBlockerMaxAbsNormalZ)
      {
        continue;
      }

      const float triangleMinHeight = std::min({first.z, second.z, third.z});
      const float triangleMaxHeight = std::max({first.z, second.z, third.z});
      if (triangleMaxHeight < actorMinHeight || triangleMinHeight > actorMaxHeight)
      {
        continue;
      }

      const Vec2 firstProjected{first.x, first.y};
      const Vec2 secondProjected{second.x, second.y};
      const Vec2 thirdProjected{third.x, third.y};
      if (!movementCapsuleIntersectsTriangle2d(movementStart, movementEnd, firstProjected, secondProjected, thirdProjected, clampedRadius))
      {
        continue;
      }

      return Psm2BlockerHit{triangleIndex,
                            triangle.primitiveIndex,
                            record80.primitiveFlags,
                            {first, second, third},
                            normal};
    }

    return std::nullopt;
  }

} // namespace orphen::port
