#include "runtime/psm2_ground_query.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orphen::port
{
  namespace
  {

    constexpr float kBarycentricEpsilon = -0.0005f;

    // FUN_00227840:44. Only a primitive carrying this is offered to the overlap
    // test at all; everything else in the cell is skipped before any geometry
    // work. It is the "participates in collision" bit, not a hint.
    constexpr std::uint32_t kOriginalTerrainSampleBit = 0x800;

    // FUN_00228090:12. The height of a primitive carrying this is the constant
    // at record78 +0x2C -- which FUN_0022c6e8:100 fills with the **maximum** z of
    // the primitive's corners -- and the plane equation is never evaluated. Most
    // of a room's floor is flat and takes this path.
    constexpr std::uint32_t kFlatHeightBit = 0x200;

    // FUN_00228090:11. Dynamic primitives resolve their plane through
    // FUN_002281a0 against the live vertices, which is what makes an animated
    // collision group answer a ground query correctly while it is moving.
    constexpr std::uint32_t kDynamicPrimitiveBit = 0x10000;

    // FUN_00227840 splits the sampled primitives on this bit: it sets the
    // workspace's +0x22 winding selector to 0xFF for them, and FUN_00227d28
    // then reverses every edge test (`0.0 < cross` rejects instead of
    // `cross < 0.0`). Reversed winding in the XY projection means the surface
    // faces down -- these are the ceilings.
    //
    // What the original does with a ceiling hit is *not* "reject". At
    // 0x0022799c it clears the have-recorded latch at +0x23 and falls through
    // without writing +0x50, so the ceiling itself is never the answer but the
    // next front-facing hit below it overwrites whatever was latched above.
    // That is the whole mechanism -- see the scan loop below.
    constexpr std::uint32_t kCeilingBit = 0x100;
    constexpr std::uint32_t kRecord80HiddenBit = 0x20;

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

    // FUN_00228058. The 2D edge function every containment test in
    // FUN_00227d28 is built out of, with the point taken from workspace
    // +0x24/+0x28.
    float FUN_00228058_edge(const Vec2 &first, const Vec2 &second, const Vec2 &point)
    {
      return (second.x - first.x) * (point.y - first.y) -
             (second.y - first.y) * (point.x - first.x);
    }

    // FUN_00227d28. The bbox reject at +0x18..+0x24, then one winding-sensitive
    // edge test per half.
    //
    // A triangle -- corner 2 == corner 3 -- is the single half (0,1,2). A quad
    // is split on the 1--3 diagonal, and the diagonal's own edge function
    // *selects* which half to test rather than both being tried: on or left of
    // it is half 0 = (3,0,1), right of it is half 1 = (1,2,3). Which half was
    // hit is +0xD0, and folds into entity +0x0A as `primitive | (half << 14)`.
    //
    // The sense of every comparison flips for a kCeilingBit primitive, which is
    // what FUN_00227840 stages in +0x22 ahead of each call: a front face wants
    // every edge `>= 0`, a reversed one wants every edge `<= 0`.
    //
    // **There is no tolerance anywhere in it.** The port used to run a
    // winding-agnostic barycentric test with -0.0005 of slack, on the reasoning
    // that a primitive whose authored winding disagreed with its ceiling bit
    // should still collide. That slack is a real behavioural difference: at
    // s01_e012's crates it is 1e-4 of world space, and a party member's
    // footprint corner grazing a crate edge from *outside* by that much was
    // enough for the four-corner sample to take the crate top as his ground and
    // stand him on it.
    bool overlapsPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                           const orphen::ported::psm2::DRecord78 &record78,
                           float x,
                           float y,
                           std::size_t &halfOut)
    {
      if (!record78.bounds.valid)
      {
        return false;
      }
      if (x < record78.bounds.min.x || x > record78.bounds.max.x ||
          y < record78.bounds.min.y || y > record78.bounds.max.y)
      {
        return false;
      }

      const auto corner = [&](std::size_t index) {
        const auto &position = positionForIndex(map, record78.vertexIndices[index]);
        return Vec2{position.x, position.y};
      };

      const Vec2 point{x, y};
      const Vec2 first = corner(0);
      const Vec2 second = corner(1);
      const Vec2 third = corner(2);
      const Vec2 fourth = corner(3);

      const bool reversed = (record78.leadingWord & kCeilingBit) != 0;
      const auto inside = [reversed, &point](const Vec2 &edgeStart, const Vec2 &edgeEnd) {
        const float edge = FUN_00228058_edge(edgeStart, edgeEnd, point);
        return reversed ? !(0.0f < edge) : !(edge < 0.0f);
      };

      halfOut = 0;
      if (record78.vertexIndices[2] == record78.vertexIndices[3])
      {
        return inside(first, second) && inside(second, third) && inside(third, first);
      }

      const float diagonal = FUN_00228058_edge(second, fourth, point);
      if (reversed ? !(0.0f < diagonal) : !(diagonal < 0.0f))
      {
        return inside(fourth, first) && inside(first, second);
      }

      if (inside(second, third) && inside(third, fourth))
      {
        halfOut = 1;
        return true;
      }
      return false;
    }

    // FUN_00228090. A flat or dynamic primitive answers with the constant at
    // record78 +0x2C, which FUN_0022c6e8 fills with the maximum corner z.
    // Everything else evaluates its half's plane, anchored at the corner
    // FUN_0022caf8 recorded.
    float heightOnPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                            const orphen::ported::psm2::DRecord78 &record78,
                            std::size_t half,
                            float x,
                            float y)
    {
      const float flatHeight = record78.bounds.valid ? record78.bounds.max.z : 0.0f;

      // FUN_00228090:11-19. A dynamic primitive is tested **before** the flat
      // bit and never takes the flat path: its stored plane went stale the
      // moment FUN_00208450 moved its group, so FUN_002281a0 rebuilds the
      // normal from the live vertices on every query. Falling back to
      // bounds.max.z here instead -- which is what the port used to do, back
      // when it had no moving collision -- hands the ground scan a door's
      // bounding-box lid as if it were floor.
      orphen::ported::psm2::Vec3 normal = record78.planeNormal[half];
      std::size_t originCorner = record78.planeOriginCorner[half] & 3u;
      if ((record78.leadingWord & kDynamicPrimitiveBit) != 0)
      {
        // The triple and its anchor, exactly as FUN_002281a0 picks them: a
        // triangle takes (0,1,2) anchored at 1, a quad's half 0 takes (3,0,1)
        // anchored at 0 and its half 1 takes (1,2,3) anchored at 2. The anchor
        // is the middle corner of the triple, which is what +0x124 records.
        const bool isTriangle = record78.vertexIndices[2] == record78.vertexIndices[3];
        std::array<std::size_t, 3> triple{0, 1, 2};
        if (isTriangle)
        {
          originCorner = 1;
        }
        else if (half == 0)
        {
          triple = {3, 0, 1};
          originCorner = 0;
        }
        else
        {
          triple = {1, 2, 3};
          originCorner = 2;
        }

        const auto &p0 = positionForIndex(map, record78.vertexIndices[triple[0]]);
        const auto &p1 = positionForIndex(map, record78.vertexIndices[triple[1]]);
        const auto &p2 = positionForIndex(map, record78.vertexIndices[triple[2]]);
        normal = cross({p1.x - p0.x, p1.y - p0.y, p1.z - p0.z},
                       {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z});
      }
      else if ((record78.leadingWord & kFlatHeightBit) != 0)
      {
        return flatHeight;
      }

      if (normal.z == 0.0f)
      {
        return flatHeight;
      }

      const auto &origin = positionForIndex(map, record78.vertexIndices[originCorner]);
      return origin.z - ((x - origin.x) * normal.x + (y - origin.y) * normal.y) / normal.z;
    }

    Psm2GroundHit makeHit(const orphen::ported::psm2::Psm2RuntimeState &map,
                          const orphen::ported::psm2::DRecord78 &record78,
                          std::size_t primitiveIndex,
                          std::size_t half,
                          float height)
    {
      const bool isTriangle = record78.vertexIndices[2] == record78.vertexIndices[3];
      const std::array<std::uint8_t, 3> corners =
          isTriangle ? std::array<std::uint8_t, 3>{0, 1, 2}
                     : (half == 0 ? std::array<std::uint8_t, 3>{3, 0, 1}
                                  : std::array<std::uint8_t, 3>{1, 2, 3});

      const auto first = positionForIndex(map, record78.vertexIndices[corners[0]]);
      const auto second = positionForIndex(map, record78.vertexIndices[corners[1]]);
      const auto third = positionForIndex(map, record78.vertexIndices[corners[2]]);
      const auto firstEdge = orphen::ported::psm2::subtract(second, first);
      const auto secondEdge = orphen::ported::psm2::subtract(third, first);

      Psm2GroundHit hit;
      hit.primitiveIndex = primitiveIndex;
      hit.subTriangle = half;
      hit.height = height;
      hit.leadingWord = record78.leadingWord;
      hit.terrainFlags = record78.terrainFlags;
      hit.sampledByOriginalTerrain = true;
      hit.slopeAngle = record78.slopeAngle[half < record78.slopeAngle.size() ? half : 0];
      hit.vertices = {first, second, third};
      hit.normal = normalize(cross(firstEdge, secondEdge));

      const auto &record80Triangles = map.DAT_003556ac_dRecords80;
      if (primitiveIndex < record80Triangles.size())
      {
        hit.triangleIndex = record80Triangles[primitiveIndex].firstTriangle +
                            std::min<std::size_t>(half, record80Triangles[primitiveIndex].triangleCount > 0
                                                            ? record80Triangles[primitiveIndex].triangleCount - 1
                                                            : 0);
      }
      return hit;
    }

  } // namespace

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight)
  {
    return queryPsm2GroundAt(map, x, y, referenceHeight, {});
  }

  // FUN_00227840's first loop, which is the whole of the static terrain answer.
  //
  // The scan is *ordered*, not scored. It walks the run the map file authored
  // for this cell and stops at the first hit at or below the head; the latch at
  // +0x23 means only the first front-facing hit since the last ceiling is kept.
  // Every heuristic the port used before this -- nearest height, highest
  // eligible, a step-up bit invented to break the tie -- was an attempt to guess
  // an order that the map ships explicitly.
  //
  // Not modelled: FUN_00227840's *second* loop over the 0x74-stride collision
  // groups at DAT_003556e0, which is where movable collision (doors, lifts)
  // lives. In s01_e012 every entity position that came from a ground query
  // matches using loop one alone.
  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight,
                                                 const Psm2TerrainQueryOptions &options)
  {
    if (map.hasCollisionGrid())
    {
      namespace psm2 = orphen::ported::psm2;

      const int cellX = static_cast<int>((x + psm2::kCollisionGridOrigin) * psm2::kCollisionGridScale);
      const int cellY = static_cast<int>((y + psm2::kCollisionGridOrigin) * psm2::kCollisionGridScale);
      const int gridSide = static_cast<int>(psm2::kCollisionGridSide);
      if (cellX < 0 || cellX >= gridSide || cellY < 0 || cellY >= gridSide)
      {
        return std::nullopt;
      }

      // FUN_00227840:23 / 61 / 72. The scan settles at the first candidate at or
      // below the top of the head; a body-less caller (FUN_00227798) passes its
      // own height as that limit.
      const float headLimit = options.body.has_value() ? options.body->headHeight : referenceHeight;

      std::optional<Psm2GroundHit> latchedHit;
      bool latched = false;
      // FUN_00227840's +0x5E: every overlap this loop saw, ceilings included.
      int cellHitCount = 0;

      // An empty cell skips the walk but still falls through to the group loop:
      // FUN_00227840's `if (-1 < head)` guards only the first loop, and
      // LAB_00227a70 runs either way. Only an out-of-grid query returns early.
      const std::int16_t cellHead =
          map.DAT_00343a18_collisionGrid[static_cast<std::size_t>(cellX) +
                                         static_cast<std::size_t>(cellY) * psm2::kCollisionGridSide];
      std::size_t cursor = cellHead < 0 ? map.DAT_003556f0_collisionCellList.size()
                                        : static_cast<std::size_t>(cellHead);

      while (cursor < map.DAT_003556f0_collisionCellList.size())
      {
        const std::int16_t entry = map.DAT_003556f0_collisionCellList[cursor];
        ++cursor;
        if (entry < 0)
        {
          break;
        }

        const std::size_t primitiveIndex = static_cast<std::size_t>(entry);
        if (primitiveIndex >= map.DAT_003556b0_dRecords78.size())
        {
          continue;
        }
        const auto &record78 = map.DAT_003556b0_dRecords78[primitiveIndex];

        // FUN_00227840:41. The reject mask is entity +0x74, not a constant.
        if ((record78.terrainFlags & options.rejectTerrainMask) != 0)
        {
          continue;
        }
        if ((record78.leadingWord & kOriginalTerrainSampleBit) == 0)
        {
          continue;
        }

        std::size_t half = 0;
        if (!overlapsPrimitive(map, record78, x, y, half))
        {
          continue;
        }

        const float height = heightOnPrimitive(map, record78, half, x, y);
        ++cellHitCount;

        if ((record78.leadingWord & kCeilingBit) != 0)
        {
          // A ceiling clears the latch without recording. Whatever was latched
          // above it stays in place unless a later front face replaces it.
          latched = false;
        }
        else if (!latched)
        {
          latched = true;
          latchedHit = makeHit(map, record78, primitiveIndex, half, height);
        }

        if (height <= headLimit)
        {
          break;
        }
      }

      // Loop one's hit count, FUN_00227840's +0x5E. It counts ceilings too, and
      // the group merge below reads it, so it is not the same as "latched".
      const bool loopOneHitAnything = cellHitCount != 0;

      // ---- FUN_00227840's second loop, LAB_00227a70 ----------------------
      //
      // The collision groups. Their primitives live in a block the cell grid
      // never indexes, so this is the only thing that reaches them.
      for (const auto &group : map.DAT_003556e0_collisionGroups)
      {
        if (group.type == psm2::kCollisionGroupTypeSkipped)
        {
          continue;
        }
        if (!group.boundsValid || x < group.minX || x > group.maxX || y < group.minY || y > group.maxY)
        {
          continue;
        }

        std::optional<Psm2GroundHit> groupHit;
        bool groupLatched = false;
        int groupHitCount = 0;
        float groupHeight = kNoGroundHeight;

        for (std::size_t offset = 0; offset < group.primitiveCount; ++offset)
        {
          const std::size_t primitiveIndex = static_cast<std::size_t>(group.firstPrimitive) + offset;
          if (primitiveIndex >= map.DAT_003556b0_dRecords78.size())
          {
            break;
          }
          const auto &record78 = map.DAT_003556b0_dRecords78[primitiveIndex];

          if ((record78.terrainFlags & options.rejectTerrainMask) != 0)
          {
            continue;
          }
          if ((record78.leadingWord & kOriginalTerrainSampleBit) == 0)
          {
            continue;
          }

          std::size_t half = 0;
          if (!overlapsPrimitive(map, record78, x, y, half))
          {
            continue;
          }

          const float height = heightOnPrimitive(map, record78, half, x, y);

          if ((record78.leadingWord & kCeilingBit) != 0)
          {
            groupLatched = false;
            ++groupHitCount;
            if (height > headLimit)
            {
              // FUN_00227840:161. A ceiling above the head does not just fail to
              // record -- it wipes the group's hit count, which is what the
              // merge below gates on.
              groupHitCount = 0;
              continue;
            }
            break;
          }

          ++groupHitCount;
          if (!groupLatched)
          {
            groupLatched = true;
            groupHeight = height;
            groupHit = makeHit(map, record78, primitiveIndex, half, height);
          }
          if (height > headLimit)
          {
            continue;
          }
          break;
        }

        // FUN_00227840:176. The merge is gated on the hit **count** alone, and
        // that count includes ceilings. A group whose only overlapping
        // primitive is a ceiling at or below the head therefore reaches the
        // merge having recorded nothing, with its height still seeded to the
        // 128 sentinel -- and adopts it, wiping whatever floor loop one found.
        // That is not an edge case: it is how a doorway reads as a hole while
        // the door is swinging through it, which is what the push-out needs in
        // order to eject an actor standing in the opening.
        if (groupHitCount == 0)
        {
          continue;
        }

        // FUN_00227840:178-192. A group wins if it is strictly higher than what
        // is held, or if loop one never hit anything at all.
        const float held = latchedHit.has_value() ? latchedHit->height : kNoGroundHeight;
        if (groupHeight <= held && loopOneHitAnything)
        {
          continue;
        }
        // Adopting an unrecorded group is the original writing +0x50 = +0x60
        // (128), +0x5C = 0xFFFF and +0x58 = 0 -- every seed it set on entry, so
        // "nothing found" is exactly the empty optional.
        latchedHit = groupHit;
      }

      return latchedHit;
    }

    // No grid in this map: fall back to the old unordered scan so a map the
    // loader could not read still reports something walkable.
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
        // keeps the underside of the floor being stood on out of it, since this
        // path has no cell list whose order would have found that floor first.
        if (options.body.has_value() && height > options.body->feetHeight && height <= options.body->headHeight)
        {
          return std::nullopt;
        }
        continue;
      }

      // FUN_00227840 will not settle on a surface above the head, so an upper
      // storey cannot be mistaken for the ground under the actor's feet.
      const float headLimit = options.body.has_value() ? options.body->headHeight : referenceHeight;
      if (options.body.has_value() && height > headLimit)
      {
        continue;
      }

      const float terrainPenalty = sampledByOriginalTerrain ? 0.0f : 100000.0f;
      const float score = terrainPenalty + std::abs(height - referenceHeight);
      if (score >= bestScore)
      {
        continue;
      }

      const auto firstEdge = orphen::ported::psm2::subtract(second, first);
      const auto secondEdge = orphen::ported::psm2::subtract(third, first);
      bestScore = score;
      Psm2GroundHit fallbackHit;
      fallbackHit.triangleIndex = triangleIndex;
      fallbackHit.primitiveIndex = triangle.primitiveIndex;
      fallbackHit.height = height;
      fallbackHit.leadingWord = record78.leadingWord;
      fallbackHit.terrainFlags = record78.terrainFlags;
      fallbackHit.sampledByOriginalTerrain = sampledByOriginalTerrain;
      fallbackHit.vertices = {first, second, third};
      fallbackHit.normal = normalize(cross(firstEdge, secondEdge));
      bestHit = fallbackHit;
    }

    return bestHit;
  }

  // FUN_00227070. The single-point path is the `entity +0x04 & 2` case; the rest
  // of the function is the four-corner sample, which is what nearly every prop
  // and NPC in the game actually uses (0xD8 / 0xD0 flags, and the player's
  // 0x312C). Sampling one point instead is why an actor would not stand on the
  // edge of anything: the original asks at (x-r,y-r), (x+r,y-r), (x+r,y+r),
  // (x-r,y+r) and keeps the highest.
  Psm2GroundSample FUN_00227070_sample_ground(const orphen::ported::psm2::Psm2RuntimeState &map,
                                              float x,
                                              float y,
                                              float feetHeight,
                                              float bodyHeight,
                                              float radius,
                                              std::uint16_t entityFlags04,
                                              std::uint32_t rejectTerrainMask)
  {
    // FUN_00227070:32. The head limit is staged once and shared by every sample.
    const Psm2TerrainQueryOptions options{rejectTerrainMask, false,
                                          Psm2ActorBody{feetHeight, feetHeight + bodyHeight}};

    const auto runScan = [&](float sampleX, float sampleY) {
      return queryPsm2GroundAt(map, sampleX, sampleY, feetHeight, options);
    };

    Psm2GroundSample sample;

    // FUN_00227070:37-40 and :47-49 adopt the first sample's primitive and flags
    // unconditionally, even when it found nothing.
    const auto adopt = [&sample](const std::optional<Psm2GroundHit> &hit, bool onlyIfFound) {
      if (hit.has_value())
      {
        sample.primitiveIndex = static_cast<std::int32_t>(hit->primitiveIndex);
        sample.subTriangle = hit->subTriangle;
        sample.packedPrimitive = static_cast<std::int32_t>(hit->primitiveIndex) |
                                 (static_cast<std::int32_t>(hit->subTriangle) << 14);
      }
      else if (!onlyIfFound)
      {
        sample.primitiveIndex = -1;
        sample.subTriangle = 0;
        sample.packedPrimitive = -1;
      }
    };

    const auto heightOf = [](const std::optional<Psm2GroundHit> &hit) {
      return hit.has_value() ? hit->height : kNoGroundHeight;
    };
    const auto flagsOf = [](const std::optional<Psm2GroundHit> &hit) -> std::uint32_t {
      return hit.has_value() ? hit->terrainFlags : 0u;
    };
    // uGpffff8504 = pi/2: a corner that found nothing reads as vertical, which
    // is what FUN_00227840 seeds +0x54 with before it scans.
    const auto slopeOf = [](const std::optional<Psm2GroundHit> &hit) {
      return hit.has_value() ? hit->slopeAngle : 1.570796012878418f;
    };

    if ((entityFlags04 & 2u) != 0)
    {
      const auto hit = runScan(x, y);
      adopt(hit, false);
      sample.height = heightOf(hit);
      sample.terrainFlagsWinning = flagsOf(hit);
      sample.terrainFlagsAll = flagsOf(hit);
      sample.slopeAngle = slopeOf(hit);
      sample.found = sample.height < kNoGroundHeight;
      return sample;
    }

    // FUN_00227070:43-116. Corner order matters only for which primitive wins a
    // tie, but it is cheap to keep faithful.
    const std::array<std::pair<float, float>, 4> corners{{{x - radius, y - radius},
                                                          {x + radius, y - radius},
                                                          {x + radius, y + radius},
                                                          {x - radius, y + radius}}};

    float running = kNoGroundHeight;
    for (std::size_t cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex)
    {
      const auto hit = runScan(corners[cornerIndex].first, corners[cornerIndex].second);
      const float height = heightOf(hit);
      sample.cornerHeights[cornerIndex] = height;
      sample.cornerPrimitives[cornerIndex] =
          hit.has_value() ? static_cast<std::int32_t>(hit->primitiveIndex) : -1;

      if (cornerIndex == 0)
      {
        adopt(hit, false);
        running = height;
        sample.terrainFlagsWinning = flagsOf(hit);
        sample.terrainFlagsAll = flagsOf(hit);
        sample.slopeAngle = slopeOf(hit);
        continue;
      }

      // FUN_00227070:55 -- +0x70 is the AND across every sample, taken before
      // the comparison and regardless of which one wins.
      sample.terrainFlagsAll &= flagsOf(hit);

      if (running < height)
      {
        // A strictly higher corner takes over, but only hands over its primitive
        // if it actually found one (`-1 < (short)puVar4[0x17]`).
        adopt(hit, true);
        sample.terrainFlagsWinning = flagsOf(hit);
        // FUN_00227390 sets workspace +0x08 from +0x54 on the same branch that
        // adopts the flags, so the slope follows the winning corner.
        sample.slopeAngle = slopeOf(hit);
        running = height;
      }
      else if (height == running)
      {
        sample.terrainFlagsWinning |= flagsOf(hit);
      }
    }

    sample.height = running;
    sample.found = running < kNoGroundHeight;
    sample.sampledFourCorners = true;
    return sample;
  }

} // namespace orphen::port
