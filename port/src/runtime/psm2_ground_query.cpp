#include "runtime/psm2_ground_query.h"

#include <cmath>
#include <limits>

namespace orphen::port
{
  namespace
  {

    constexpr float kBarycentricEpsilon = -0.0005f;
    constexpr std::uint32_t kOriginalTerrainSampleBit = 0x800;

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

    float candidateScore(float height, float referenceHeight, bool sampledByOriginalTerrain)
    {
      const float terrainPenalty = sampledByOriginalTerrain ? 0.0f : 100000.0f;
      return terrainPenalty + std::abs(height - referenceHeight);
    }

  } // namespace

  std::optional<Psm2GroundHit> queryPsm2GroundAt(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                 float x,
                                                 float y,
                                                 float referenceHeight)
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

      const float height = first.z * w + second.z * u + third.z * v;
      const auto &record78 = map.DAT_003556b0_dRecords78[triangle.primitiveIndex];
      const bool sampledByOriginalTerrain = (record78.leadingWord & kOriginalTerrainSampleBit) != 0;
      const float score = candidateScore(height, referenceHeight, sampledByOriginalTerrain);
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

} // namespace orphen::port
