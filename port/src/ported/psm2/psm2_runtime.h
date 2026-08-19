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

    // The 4th word of each plane record at +0x3C / +0x4C. FUN_0022caf8 stores
    // the **middle** corner of the triple it was handed -- a triangle (0,1,2)
    // gives 1, a quad's halves (3,0,1) and (1,2,3) give 0 and 2 -- as a raw
    // int, which the decompiler shows as a float because the surrounding
    // fields are floats. FUN_00228090 reads it back as the vertex the plane
    // equation is anchored at. Confirmed against eeMemory.bin: every quad in
    // s01_e012 reads 0 and 2.
    std::array<std::uint8_t, 2> planeOriginCorner{};
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

  // The collision broadphase, PSM2 header word 6. FUN_0022b5a8:305-325 copies it
  // out of the file verbatim -- nothing is computed -- so the port can hold the
  // identical structure: a 64x64 grid of int16 offsets into a shared index list,
  // each run terminated by a negative entry.
  //
  // This is what makes FUN_00227840 answerable. Its scan is not "pick the best
  // candidate", it is "walk this cell's run in order and take the first hit at
  // or below the head", so the run's authored order *is* the tie-break. Without
  // it the port had to invent a scoring rule, and every scoring rule it tried
  // was wrong somewhere.
  inline constexpr std::size_t kCollisionGridSide = 64;
  inline constexpr std::size_t kCollisionGridCells = kCollisionGridSide * kCollisionGridSide;

  // FUN_00227840:25-28. Cell = (world + 64) * 0.5, and a query outside the grid
  // returns "no ground" rather than clamping.
  inline constexpr float kCollisionGridOrigin = 64.0f;
  inline constexpr float kCollisionGridScale = 0.5f;

  // PSM2 header word 1. FUN_0022b5a8:56-89 copies 24 bytes per record out of the
  // file into a 0x20 stride in memory, so the file stride is **24** -- which is
  // what makes `4 + count * 24` land exactly on the next section.
  struct CollisionDescriptor
  {
    // The file record is 24 bytes; the original blits it into a 0x20 slot.
    std::uint16_t firstVertex = 0;     // memory +0x00
    std::uint16_t vertexCount = 0;     // memory +0x02
    std::uint16_t firstPrimitive = 0;  // memory +0x04
    std::uint16_t primitiveCount = 0;  // memory +0x06

    // +0x08..+0x10. The group's centre, in the same space as the vertices.
    // FUN_00208450 rewrites it every time the group moves, from a rest copy the
    // loader parks one past the group's own vertices.
    Vec3 center{};
  };

  // PSM2 header word 7, FUN_0022b5a8:443-517: an int16 count then 14 int16 per
  // group (file stride 28) expanded into a 0x74 stride.
  //
  // These own a **disjoint** block of primitives at the top of the record78
  // array that the cell grid never references -- in s01_e012 the grid stops
  // around 2500 and the groups run 3131..3948. FUN_00227840's second loop is the
  // only thing that reaches them, so without it those primitives do not collide
  // at all.
  // A movable map sub-object. Doors are these: in s01_e012 the six doorways are
  // groups 0..5, sixteen primitives each. The script writes the transform
  // channels through opcodes 0x7D / 0x7E and FUN_00208450 spends them.
  struct CollisionGroup
  {
    std::uint16_t descriptorIndex = 0;  // +0x00
    std::int16_t type = 0;              // +0x02; the terrain scan skips type 4

    // +0x04..+0x0C. The pivot the group rotates about, and the origin its rest
    // vertices are stored relative to. File data, three floats.
    Vec3 pivot{};

    std::uint16_t firstVertex = 0;      // +0x00 of the descriptor
    std::uint16_t vertexCount = 0;      // +0x54, copied from the descriptor
    std::uint16_t firstPrimitive = 0;   // resolved through the descriptor
    std::uint16_t primitiveCount = 0;

    // +0x3C..+0x44 and +0x48..+0x50, three channels each. Opcode 0x7D writes a
    // rotation channel, 0x7E a translation channel, and each raises its bit in
    // the dirty byte below.
    Vec3 rotation{};
    Vec3 translation{};

    // +0x5A, and it is a *signed* char in the original, which is the whole
    // trick. FUN_00208450 leaves 0xFF behind after it applies a transform; the
    // next pass reads bit 7, clears the byte and stops. But 0x7D's update is
    // `status < 2 ? 2 : status | 2`, and 0xFF is negative, so a fresh write
    // resets it to exactly 2 -- clearing bit 7 and re-arming the pass. That is
    // how a door keeps swinging while a script drives it every frame, and how
    // it settles exactly one frame after the script stops.
    std::int8_t dirty5a = 0;

    // The rest pose: every vertex of the group, and its centre one past the
    // end, stored relative to `pivot`. FUN_0022b5a8:487-514 builds this at load
    // by subtracting the pivot from the live positions, so the group's first
    // transform reproduces them exactly.
    std::vector<Vec3> restVertices;
    Vec3 restCenter{};

    // +0x24..+0x38, the live box FUN_00227840:99-102 rejects against.
    // FUN_00208450 recomputes it from the moved primitives every time.
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    bool boundsValid = false;
  };

  // FUN_00227840:94. A group of this type is skipped by the terrain scan.
  inline constexpr std::int16_t kCollisionGroupTypeSkipped = 4;

  // ---------------------------------------------------------------------------
  // The UV animation script -- PSM2 header +0x2C, section G.
  //
  //   src/FUN_0022b5a8.c:331-374  copies the script and allocates the state
  //   src/FUN_002256d0.c          the state record's size
  //   src/FUN_002256f0.c          seeds it
  //   src/FUN_00225940.c          steps it, once per frame
  //   src/FUN_0020eec0.c:97-110   uploads the result to VU1
  //
  // This is how every animated texture in the game moves, and nothing about it
  // touches the geometry: FUN_00211230 bakes each primitive's UVs into its DMA
  // packet once, and this subsystem adds a per-frame **offset** that lives in
  // VU1 registers. So a memory diff of the map, the materials, the packets, the
  // CLUTs or GS texture memory shows a scrolling texture as completely static.
  // The only trace is the 0x44-byte state record.
  //
  // A track is a little timeline. `duration >= 0` is an absolute keyframe --
  // the offset becomes (u << 6, v << 6) and holds for `duration` * 32 ticks.
  // `duration < 0` is a continuous scroll: the offset accumulates (u, v) every
  // frame and wraps at a modulus the duration selects, -1..-5 giving 0x4000,
  // 0x2000, 0x1000, 0x800, 0x400. Offsets are in **1/64 texel** units, so the
  // -1 modulus is exactly one 256-texel page.
  //
  // Which track a primitive uses is section E byte 9 -- `MaterialSlot::byte9`.
  // Zero means "no offset"; N selects track N-1. In s01_e012 all 78 of the rain
  // sheets outside the windows carry byte9 = 2, sharing one scroller, and the
  // lantern glows carry the byte that selects an eight-frame strip.
  struct UvAnimationFrame
  {
    std::int16_t duration = 0;  // >= 0 keyframe hold, < 0 scroll mode
    std::int16_t u = 0;
    std::int16_t v = 0;
  };

  struct UvAnimationTrack
  {
    std::vector<UvAnimationFrame> frames;

    // FUN_002256f0's seed. frameIndex starts at 0xFF so the first step's
    // `(int8)(frameIndex + 1)` lands on frame 0, and flags starts at 1 -- the
    // "running" bit the stepper gates on.
    std::int16_t u = 0;
    std::int16_t v = 0;
    std::int16_t timer = 0;
    std::uint8_t frameIndex = 0xff;
    std::uint8_t flags = 1;
    std::uint8_t link = 0;
    std::uint8_t repeat = 0;
  };

  // FUN_0020eec0 unpacks seven pairs whatever the script's real length, reading
  // past a shorter record. Reproducing the read would only copy junk into a
  // register nothing selects, so the port just stops at the tracks that exist.
  inline constexpr std::size_t kUvAnimationUploadSlots = 7;

  struct Psm2Stats
  {
    std::size_t positionRecordCount = 0;
    std::size_t sectionBRecordCount = 0;
    std::size_t primitiveRecordCount = 0;
    std::size_t paletteColourCount = 0;
    std::size_t triangleCount = 0;
    std::size_t skippedPrimitiveCount = 0;
    std::size_t objectPlacementCount = 0;
    std::size_t collisionCellListLength = 0;
    std::size_t occupiedCollisionCells = 0;
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

    // DAT_00343a18 and DAT_003556f0 / DAT_003556ec.
    std::vector<std::int16_t> DAT_00343a18_collisionGrid;
    std::vector<std::int16_t> DAT_003556f0_collisionCellList;

    // DAT_003556d8 / DAT_003556d4 and DAT_003556e0 / DAT_003556dc.
    std::vector<CollisionDescriptor> DAT_003556d8_collisionDescriptors;
    std::vector<CollisionGroup> DAT_003556e0_collisionGroups;

    // DAT_003556F4 (the script) and DAT_003556F8 (the state) folded into one.
    // iGpffffb788 is DAT_003556F8, and FUN_00208F28 gates the whole per-frame
    // step on it being non-null -- a map with no section G simply has none.
    std::vector<UvAnimationTrack> DAT_003556f4_uvAnimation;

    bool hasCollisionGrid() const { return DAT_00343a18_collisionGrid.size() == kCollisionGridCells; }

    Bounds3 bounds;
    Psm2Stats stats;
  };

  void includePoint(Bounds3 &bounds, const Vec3 &point);
  Vec3 add(const Vec3 &left, const Vec3 &right);
  Vec3 subtract(const Vec3 &left, const Vec3 &right);
  Vec3 scale(const Vec3 &value, float factor);
  float distance(const Vec3 &left, const Vec3 &right);

} // namespace orphen::ported::psm2
