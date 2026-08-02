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

  // One entry of the map's object placement table: PSM2 header word 13, parsed
  // by FUN_0022b5a8 into DAT_003556e8 with count DAT_003556e4, stride 0x10.
  // Script opcode 0x51 walks this table and spawns every record whose group byte
  // matches its operand -- so this, not the script, is where scene objects are
  // positioned. See analyzed/ops/0x51_set_pw_all_dispatch.c.
  struct ObjectPlacementRecord
  {
    Vec3 position;              // +0x00, +0x04, +0x08 as floats
    std::int8_t angle = 0;      // +0x0C: angle * (pi/4) + (pi/2)
    std::int8_t group = 0;      // +0x0D: matched against opcode 0x51's operand
    std::int8_t id = 0;         // +0x0E: matched against the 0x4E lookup table
    std::uint8_t param = 0;     // +0x0F
  };

  struct Psm2Stats
  {
    std::size_t positionRecordCount = 0;
    std::size_t sectionBRecordCount = 0;
    std::size_t primitiveRecordCount = 0;
    std::size_t triangleCount = 0;
    std::size_t skippedPrimitiveCount = 0;
    std::size_t objectPlacementCount = 0;
  };

  struct Psm2RuntimeState
  {
    std::vector<SectionCRecord> DAT_0035569c_sectionCRecords;
    std::vector<SectionBRecord> DAT_003556a4_sectionBRecords;
    std::vector<DRecord80> DAT_003556ac_dRecords80;
    std::vector<DRecord78> DAT_003556b0_dRecords78;
    std::vector<SectionERecord> DAT_003556b4_sectionERecords;
    std::vector<ObjectPlacementRecord> DAT_003556e8_objectPlacements;
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
