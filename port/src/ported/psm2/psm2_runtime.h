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

  // PSM2 header word 0x10: an s16 count followed by count 3-byte entries. The
  // original stages it at DAT_00355bdc / DAT_00355be0 (FUN_0022b5a8:535-563).
  // The bytes are kept in file order because FUN_0022c3d8 reads them two
  // different ways -- packed() is the vertex-colour form, and the flat-colour
  // slot path uses byte0 and byte1 in a different channel order.
  struct PaletteColour
  {
    std::uint8_t byte0 = 0;
    std::uint8_t byte1 = 0;
    std::uint8_t byte2 = 0;

    // FUN_0022c3d8:29-34: (byte2 << 16) | (byte1 << 8) | byte0, i.e. red in
    // the low byte, matching the GS RGBAQ layout.
    std::uint32_t packed() const
    {
      return static_cast<std::uint32_t>(byte0) |
             (static_cast<std::uint32_t>(byte1) << 8) |
             (static_cast<std::uint32_t>(byte2) << 16);
    }
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

    // FUN_0022c6e8's plane data. planeNormal is FUN_0022caf8's raw
    // (v1-v0) x (v2-v0) at +0x30 / +0x40; unitNormal is FUN_0022cbd8's
    // normalized (v1-v0) x (v2-v1) at +0x50 / +0x60; slopeAngle is the
    // pi/2 - atan2(nz, hypot(nx, ny)) it stores at +0x70 / +0x74. Index 1 is
    // only meaningful for quads.
    std::array<Vec3, 2> planeNormal{};
    std::array<Vec3, 2> unitNormal{};
    std::array<float, 2> slopeAngle{};
  };

  // One of the four material slots a primitive can carry: 0x80-record
  // +0x30 + slot * 0x0C. FUN_0022b5a8 seeds the first halfword with a
  // selector and FUN_0022c3d8 expands it in place, so the fields below are
  // the post-expansion view.
  struct MaterialSlot
  {
    // Section E bytes 0..7 -- four corners of (u, v). For an untextured slot
    // (type < 0) FUN_0022c3d8 overwrites the first three with a flat colour
    // instead, which FUN_00211230 then modulates against the vertex colour.
    std::array<std::uint8_t, 8> textureCoordinates{};
    std::uint8_t type = 0xfe;  // +8, section E byte 8. 0x0F is remapped to 0x09.
    std::uint8_t byte9 = 0;    // +9,  section E byte 9
    std::uint8_t alpha = 0;    // +10, section E byte 10. 0xFF becomes 0x80, else >>= 1.
    std::uint8_t flags = 0;    // +11, section E byte 11. Bits 0x70 / 0x40 drive blendTerm.

    // FUN_00211230:137 tests `type < 0` as a signed char to pick the
    // untextured path, and FUN_00211230:60 counts `-2 <= type` slots.
    bool present() const { return static_cast<std::int8_t>(type) >= -1; }
    bool textured() const { return static_cast<std::int8_t>(type) >= 0; }
    std::uint32_t flatColour() const
    {
      return static_cast<std::uint32_t>(textureCoordinates[0]) |
             (static_cast<std::uint32_t>(textureCoordinates[1]) << 8) |
             (static_cast<std::uint32_t>(textureCoordinates[2]) << 16);
    }
  };

  struct DRecord80
  {
    // +0x00..+0x08 and +0x0C. FUN_0022b5a8:237-244 copies section B's entry
    // at normalIndex in as the face normal. Flag 0x4 switches the renderer to
    // per-vertex normals via SectionCRecord::sectionBIndex instead.
    Vec3 normal;
    std::uint16_t normalIndex = 0;

    std::array<std::uint16_t, 4> vertexIndices{};  // +0x24..+0x2A
    std::uint16_t colourIndex = 0;                 // seeds +0x10; a palette index

    // +0x10..+0x1C after FUN_0022c3d8: one packed colour per corner.
    std::array<std::uint32_t, 4> vertexColours{};

    // +0x30 / +0x3C / +0x48 / +0x54, and the selectors FUN_0022b5a8 seeds
    // them with before FUN_0022c3d8 expands them.
    std::array<MaterialSlot, 4> materialSlots{};
    std::array<std::int16_t, 4> slotSelectors{};

    std::uint8_t blendParam = 0;    // +0x2C
    std::uint8_t staticAlpha = 0;   // +0x2D

    // +0x2E: the per-primitive occlusion fade, ramped by FUN_00209140 and
    // FUN_0020a2c0 between kFadeFloor and kFadeCeiling. Load value is 0x80.
    std::uint8_t dynamicFade = 0x80;

    // +0x70. w4 zero-extended at load; FUN_0022c3d8 and FUN_00211230 then OR
    // runtime-only bits into the high half, which is why this is not just a
    // copy of DRecord78::leadingWord after load.
    std::uint32_t primitiveFlags = 0;

    Vec3 center;                  // +0x60
    float radius = 0.0f;          // +0x74
    float blendTerm = 0.0f;       // +0x78

    // Not in the original: where this primitive's entries land in
    // derivedTriangles. The original never needs it because each primitive
    // owns a prebuilt GS packet; the port draws from a shared triangle list
    // and would otherwise have to search it.
    std::size_t firstTriangle = 0;
    std::size_t triangleCount = 0;
  };

  // FUN_00209140:324-339 / FUN_0020a2c0:672-683.
  inline constexpr std::uint8_t kFadeFloor = 0x5c;
  inline constexpr std::uint8_t kFadeCeiling = 0x7e;
  inline constexpr std::uint8_t kFadeLoadValue = 0x80;

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
    std::size_t paletteColourCount = 0;
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
    std::vector<PaletteColour> DAT_00355bdc_palette;
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
