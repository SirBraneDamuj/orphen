#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace orphen::ported::psm2
{

  struct Vec3
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct Bounds3
  {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool valid = false;
  };

  struct SectionCRecord
  {
    Vec3 position;
    std::uint16_t sectionBIndex = 0;
    std::uint8_t styleFlags = 0;
  };

  struct SectionBRecord
  {
    Vec3 normal;
  };

  struct SectionERecord
  {
    std::array<std::uint8_t, 12> bytes{};
  };

  struct DRecord78
  {
    std::uint32_t leadingWord = 0;
    std::uint32_t terrainFlags = 0;
    std::array<std::uint16_t, 4> vertexIndices{};
    std::uint16_t selector = 0;
    std::uint8_t byte12 = 0;
    std::uint8_t byte13 = 0;
    Bounds3 bounds;
  };

  struct DRecord80
  {
    std::array<std::uint16_t, 4> vertexIndices{};
    std::uint16_t sectionEIndex = 0xffff;
    std::uint32_t terrainFlags = 0;
    Vec3 center;
    float radius = 0.0f;
  };

  struct TriangleRecord
  {
    std::size_t primitiveIndex = 0;
    std::array<std::uint16_t, 3> vertexIndices{};
    std::array<std::uint8_t, 3> cornerIndices{};
  };

  struct Psm2Stats
  {
    std::size_t positionRecordCount = 0;
    std::size_t sectionBRecordCount = 0;
    std::size_t primitiveRecordCount = 0;
    std::size_t triangleCount = 0;
    std::size_t skippedPrimitiveCount = 0;
  };

  struct Psm2RuntimeState
  {
    std::vector<SectionCRecord> DAT_0035569c_sectionCRecords;
    std::vector<SectionBRecord> DAT_003556a4_sectionBRecords;
    std::vector<DRecord80> DAT_003556ac_dRecords80;
    std::vector<DRecord78> DAT_003556b0_dRecords78;
    std::vector<SectionERecord> DAT_003556b4_sectionERecords;
    std::vector<TriangleRecord> derivedTriangles;
    Bounds3 bounds;
    Psm2Stats stats;
  };

  void includePoint(Bounds3 &bounds, const Vec3 &point);
  Vec3 add(const Vec3 &left, const Vec3 &right);
  Vec3 subtract(const Vec3 &left, const Vec3 &right);
  Vec3 scale(const Vec3 &value, float factor);
  float distance(const Vec3 &left, const Vec3 &right);

} // namespace orphen::ported::psm2
