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

  void buildPsm2DerivedGeometry(Psm2RuntimeState &state)
  {
    state.derivedTriangles.clear();
    state.bounds = {};
    state.stats.skippedPrimitiveCount = 0;

    for (std::size_t primitiveIndex = 0; primitiveIndex < state.DAT_003556ac_dRecords80.size(); ++primitiveIndex)
    {
      DRecord80 &record80 = state.DAT_003556ac_dRecords80[primitiveIndex];
      DRecord78 &record78 = state.DAT_003556b0_dRecords78[primitiveIndex];
      const auto &indices = record80.vertexIndices;

      if (!primitiveIndicesAreValid(state, indices))
      {
        ++state.stats.skippedPrimitiveCount;
        continue;
      }

      const bool isTriangle = indices[2] == indices[3];
      const std::size_t vertexCount = isTriangle ? 3 : 4;
      Vec3 center{};
      record78.bounds = {};

      for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
      {
        const Vec3 &position = positionForIndex(state, indices[vertexIndex]);
        includePoint(record78.bounds, position);
        includePoint(state.bounds, position);
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

      if (isTriangle)
      {
        appendTriangle(state, primitiveIndex, indices[0], indices[1], indices[2], 0, 1, 2);
      }
      else
      {
        appendTriangle(state, primitiveIndex, indices[3], indices[0], indices[1], 3, 0, 1);
        appendTriangle(state, primitiveIndex, indices[1], indices[2], indices[3], 1, 2, 3);
      }
    }
  }

} // namespace orphen::ported::psm2
