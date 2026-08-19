#include "ported/psm2/psm2_geometry_builder.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::psm2
{
  namespace
  {

    const Vec3 &positionForIndex(const Psm2RuntimeState &state, std::uint16_t index)
    {
      return state.DAT_0035569c_sectionCRecords.at(index).position;
    }

    bool primitiveIndicesAreValid(const Psm2RuntimeState &state, const std::array<std::uint16_t, 4> &indices)
    {
      const std::size_t positionCount = state.DAT_0035569c_sectionCRecords.size();
      return std::all_of(indices.begin(), indices.end(), [positionCount](std::uint16_t index)
                         { return static_cast<std::size_t>(index) < positionCount; });
    }

    // fGpffff8578 (0x003524e8) = pi/2, the constant FUN_0022cbd8 subtracts the
    // elevation from to get a slope angle.
    constexpr float kHalfPi = 1.57079601f;

    // DAT_003524e4 (0.000174532892, one hundredth of a degree).
    constexpr float kSlopeTieBreak = 0.000174532892f;

    // FUN_0022caf8. The raw, unnormalized plane normal: (v1 - v0) x (v2 - v0),
    // with the components written back in the order z, y, x. This is what
    // fixes the winding, and so what backface culling has to agree with.
    Vec3 planeNormalFor(const Vec3 &first, const Vec3 &second, const Vec3 &third)
    {
      const Vec3 toSecond = subtract(second, first);
      const Vec3 toThird = subtract(third, first);
      return {toSecond.y * toThird.z - toSecond.z * toThird.y,
              toSecond.z * toThird.x - toSecond.x * toThird.z,
              toSecond.x * toThird.y - toSecond.y * toThird.x};
    }

    // FUN_0022cbd8. Same plane, but built from the consecutive edges
    // (v1 - v0) x (v2 - v1) and then normalized. A degenerate triangle falls
    // back to straight up, exactly as the original does.
    Vec3 unitNormalFor(const Vec3 &first, const Vec3 &second, const Vec3 &third)
    {
      const Vec3 firstEdge = subtract(second, first);
      const Vec3 secondEdge = subtract(third, second);
      const Vec3 normal{firstEdge.y * secondEdge.z - firstEdge.z * secondEdge.y,
                        firstEdge.z * secondEdge.x - firstEdge.x * secondEdge.z,
                        firstEdge.x * secondEdge.y - firstEdge.y * secondEdge.x};

      const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
      if (length == 0.0f)
      {
        return {0.0f, 0.0f, 1.0f};
      }
      return {normal.x / length, normal.y / length, normal.z / length};
    }

    // The angle FUN_0022cbd8 stores at 0x78 +0x70 / +0x74. FUN_0022d258
    // compares it against DAT_003524ec (0.872664452, 50 degrees) to decide
    // whether a surface is walkable, so it is a slope, not an elevation.
    float slopeAngleFor(const Vec3 &unitNormal)
    {
      const float horizontal = std::sqrt(unitNormal.x * unitNormal.x + unitNormal.y * unitNormal.y);
      return kHalfPi - std::atan2(unitNormal.z, horizontal);
    }

    void appendTriangle(Psm2RuntimeState &state,
                        std::size_t primitiveIndex,
                        std::uint16_t firstIndex,
                        std::uint16_t secondIndex,
                        std::uint16_t thirdIndex,
                        std::uint8_t firstCorner,
                        std::uint8_t secondCorner,
                        std::uint8_t thirdCorner)
    {
      state.derivedTriangles.push_back({primitiveIndex, {firstIndex, secondIndex, thirdIndex}, {firstCorner, secondCorner, thirdCorner}});
    }

  } // namespace

  void includePoint(Bounds3 &bounds, const Vec3 &point)
  {
    if (!bounds.valid)
    {
      bounds.min = point;
      bounds.max = point;
      bounds.valid = true;
      return;
    }

    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
  }

  Vec3 add(const Vec3 &left, const Vec3 &right)
  {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
  }

  Vec3 subtract(const Vec3 &left, const Vec3 &right)
  {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
  }

  Vec3 scale(const Vec3 &value, float factor)
  {
    return {value.x * factor, value.y * factor, value.z * factor};
  }

  float distance(const Vec3 &left, const Vec3 &right)
  {
    const Vec3 delta = subtract(left, right);
    return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
  }

  // FUN_0022c6e8's per-primitive body, shared by the load pass and by
  // FUN_00208450's rebuild of a group it has just moved. Returns false for a
  // primitive whose corner indices are out of range, which the load pass counts
  // as skipped.
  bool rebuildPsm2Primitive(Psm2RuntimeState &state, std::size_t primitiveIndex)
  {
    DRecord80 &record80 = state.DAT_003556ac_dRecords80[primitiveIndex];
    DRecord78 &record78 = state.DAT_003556b0_dRecords78[primitiveIndex];
    const auto &indices = record80.vertexIndices;

    if (!primitiveIndicesAreValid(state, indices))
    {
      return false;
    }

    const bool isTriangle = indices[2] == indices[3];
    const std::size_t vertexCount = isTriangle ? 3 : 4;
    Vec3 center{};
    record78.bounds = {};

    for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
      const Vec3 &position = positionForIndex(state, indices[vertexIndex]);
      includePoint(record78.bounds, position);
      center = add(center, position);
    }

    center = scale(center, 1.0f / static_cast<float>(vertexCount));
    record80.center = center;

    float radius = 0.0f;
    for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
      radius = std::max(radius, distance(center, positionForIndex(state, indices[vertexIndex])));
    }
    record80.radius = radius;

    // FUN_0022c6e8:106-126. A triangle takes corners (0, 1, 2); a quad is
    // split as (3, 0, 1) and (1, 2, 3), and each half gets its own plane.
    // The corner order here is what defines the winding for the whole
    // renderer, so it must not be "tidied".
    const auto plane = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::size_t slot)
    {
      const Vec3 &first = positionForIndex(state, indices[a]);
      const Vec3 &second = positionForIndex(state, indices[b]);
      const Vec3 &third = positionForIndex(state, indices[c]);
      record78.planeNormal[slot] = planeNormalFor(first, second, third);
      record78.unitNormal[slot] = unitNormalFor(first, second, third);
      record78.slopeAngle[slot] = slopeAngleFor(record78.unitNormal[slot]);
      // FUN_0022caf8 anchors the plane at the middle corner, not the first.
      record78.planeOriginCorner[slot] = b;
    };

    if (isTriangle)
    {
      plane(0, 1, 2, 0);
      record78.planeNormal[1] = record78.planeNormal[0];
      record78.unitNormal[1] = record78.unitNormal[0];
      record78.slopeAngle[1] = record78.slopeAngle[0];
      record78.planeOriginCorner[1] = record78.planeOriginCorner[0];
    }
    else
    {
      plane(3, 0, 1, 0);
      plane(1, 2, 3, 1);
      // FUN_0022c6e8:122-124: identical slopes on the two halves are nudged
      // apart by DAT_003524e4 so the terrain query can tell them apart.
      if (record78.slopeAngle[0] == record78.slopeAngle[1])
      {
        record78.slopeAngle[1] += kSlopeTieBreak;
      }
    }
    return true;
  }

  void buildPsm2DerivedGeometry(Psm2RuntimeState &state)
  {
    state.derivedTriangles.clear();
    state.bounds = {};
    state.stats.skippedPrimitiveCount = 0;

    for (std::size_t primitiveIndex = 0; primitiveIndex < state.DAT_003556ac_dRecords80.size(); ++primitiveIndex)
    {
      if (!rebuildPsm2Primitive(state, primitiveIndex))
      {
        ++state.stats.skippedPrimitiveCount;
        continue;
      }

      DRecord80 &record80 = state.DAT_003556ac_dRecords80[primitiveIndex];
      const DRecord78 &record78 = state.DAT_003556b0_dRecords78[primitiveIndex];
      const auto &indices = record80.vertexIndices;
      const bool isTriangle = indices[2] == indices[3];

      // The map's own bounds are the load pass's business, not the rebuild's --
      // a door that swings must not grow them.
      for (std::size_t vertexIndex = 0; vertexIndex < (isTriangle ? 3u : 4u); ++vertexIndex)
      {
        includePoint(state.bounds, positionForIndex(state, indices[vertexIndex]));
      }
      (void)record78;

      record80.firstTriangle = state.derivedTriangles.size();
      if (isTriangle)
      {
        appendTriangle(state, primitiveIndex, indices[0], indices[1], indices[2], 0, 1, 2);
      }
      else
      {
        appendTriangle(state, primitiveIndex, indices[3], indices[0], indices[1], 3, 0, 1);
        appendTriangle(state, primitiveIndex, indices[1], indices[2], indices[3], 1, 2, 3);
      }
      record80.triangleCount = state.derivedTriangles.size() - record80.firstTriangle;
    }
  }

} // namespace orphen::ported::psm2
