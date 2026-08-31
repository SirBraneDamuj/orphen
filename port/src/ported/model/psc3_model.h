#pragma once

// PSC3 character/prop model parse.
//
// Grounded in the original's own consumers rather than in offline guesswork:
//
//   src/FUN_00212058.c   builds the per-primitive GIF command buffer at load
//   src/FUN_002129b8.c   builds one primitive's vertex stream for VU1
//   src/FUN_0020c810.c   walks the skeleton and caches the section offsets
//   src/FUN_00229c40.c   binds a loaded model to an entity
//
// docs/psc3_format_and_loader_notes.md predates those reads and is wrong in
// three places; the header map below is the corrected one and the doc has been
// updated to match.
//
//   +0x00  'PSC3'
//   +0x04  u16 submesh count == BONE count (one matrix per submesh)
//   +0x08  u32 submesh table        stride 0x14
//   +0x0C  u32 animation table      entity +0x9C points here
//   +0x10  u32 root bone list       bytes; a byte with bit 7 set terminates
//   +0x14  u32 vertex records       stride 10
//   +0x18  u32 vertex bone indices  one byte per vertex
//   +0x1C  u32 primitive table      stride 0x18
//   +0x20  u32 colour table         3 bytes per entry
//   +0x24  u32 subdraw table        stride 10, UVs + texture flags
//   +0x28  u32 normal table         float4
//   +0x2C  u32 keyframe pool        s16 quaternion / translation keys
//   +0x30  u32 hit-volume column map bytes, one per pose column
//   +0x34  u32 hit-volume records     stride 0x10
//
// The last two are only on weapon-effect models -- grp_0179, the sword blade,
// has them and grp_0001, the player, does not. FUN_002148a8 reads +0x30 first
// and returns immediately when it is zero, which is why the swept hit test does
// nothing when it is handed anything but a weapon.
//
// Note that +0x1C..+0x28 are rewritten to absolute addresses when the game
// relocates a model (FUN_00221f60), so a PSC3 read out of an EE dump has four
// pointers where a PSC3 read off disc has four offsets. This parser only ever
// sees the on-disc form.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::model
{

  using orphen::ported::psm2::Bounds3;
  using orphen::ported::psm2::Vec3;

  // FUN_002129b8 line 53: s16 * 0.00048828125.
  inline constexpr float kPositionScale = 1.0f / 2048.0f;

  // Primitive flag bits, from FUN_00212058 / FUN_002129b8.
  inline constexpr std::uint16_t kPrimitiveSkip = 0x0020;        // not drawn at all
  inline constexpr std::uint16_t kPrimitivePerVertex = 0x0008;   // per-vertex normal and colour
  // Bit 8 does two jobs. FUN_00212058:84 suppresses the +0x0C specular byte
  // with it, and :229 copies it into draw header byte 15, which VU1 0x01ba reads
  // to branch past the lighting block -- so the primitive's authored vertex
  // colour reaches the GS untouched. In grp_0172 the 72 bit-8 primitives are the
  // chest lid, and their colour entry ramps 0 -> 128 across the quad, which is
  // what makes the lid read as a lit highlight without being lit.
  inline constexpr std::uint16_t kPrimitiveUnlit = 0x0100;
  inline constexpr std::uint16_t kPrimitiveUntexturedColour = 0x0200;
  inline constexpr std::uint16_t kPrimitiveNoAlphaBlend = 0x0400;

  struct Psc3Vertex
  {
    Vec3 position;                 // +0x00 three s16, scaled by kPositionScale
    std::uint16_t normalIndex = 0; // +0x06 index into the float4 normal table
    // +0x08. Never read by FUN_002129b8 or FUN_00212058; every vertex in every
    // s01_e024 model has it at zero. Kept so the record round-trips, not used.
    std::uint16_t trailing = 0;
    // From the byte table at header +0x18. FUN_002129b8 line 56 writes
    // `byte * 4 + 0x20` into the stream's w component: that is a VU1 memory
    // address, palette base 0x20 plus four quadwords per matrix, so the byte is
    // this vertex's bone index. Skinning is one bone per vertex, rigid, with no
    // weights -- which is why FUN_0020eec0 uploads exactly submeshCount
    // matrices.
    std::uint8_t boneIndex = 0;
  };

  struct Psc3Subdraw
  {
    // +0x00..+0x06, one per primitive corner, packed (U << 8) | V as 8-bit
    // texel coordinates over a 256x256 page. FUN_00212058 line 149 hands the
    // record straight to FUN_002129b8, which copies `vertexCount` halfwords.
    std::array<std::uint16_t, 4> packedUv{};
    // +0x08. FUN_00212058 splits this three ways:
    //   bits 15..14  alpha blend mode      (line 139: >> 0xE)
    //   bits 13..11  texture bank selector (line 183: >> 0xB & 7, +7 when set)
    //   bits 10..7   texture slot          (line 181: >> 7 & 0xF, 0xF = none)
    //   bits  6..0   alpha value           (line 138: & 0x7F, 0x7F means 0x80)
    // psc3_full.py reads bits 14..8 as one "atlas slot" field; that grouping
    // does not survive contact with the decompiled code.
    std::uint16_t texFlags = 0;

    std::uint16_t textureSlot() const { return static_cast<std::uint16_t>((texFlags >> 7) & 0xF); }
    std::uint16_t textureBank() const { return static_cast<std::uint16_t>((texFlags >> 11) & 0x7); }
    std::uint16_t blendMode() const { return static_cast<std::uint16_t>(texFlags >> 14); }
    std::uint16_t alpha() const { return static_cast<std::uint16_t>(texFlags & 0x7F); }
  };

  struct Psc3Primitive
  {
    std::array<std::uint16_t, 4> vertexIndices{}; // +0x00..+0x06
    std::uint16_t flags = 0;                      // +0x08
    std::uint16_t colourIndex = 0;                // +0x0A, base entry in the colour table
    std::uint8_t fogByte = 0;                     // +0x0C, only used on the last pass
    std::uint8_t alphaByte = 0;                   // +0x0D
    // +0x0E..+0x14. -1 means the pass is unused. A *negative but not -1* value
    // is not a subdraw index at all: FUN_002129b8 masks off bit 15 and treats
    // the rest as a colour index, drawing the pass untextured.
    std::array<std::int16_t, 4> subdrawIndices{};
    std::uint16_t flatNormalIndex = 0; // +0x16, used when kPrimitivePerVertex is clear

    // FUN_00212058 line 108: three corners when v2 == v3.
    bool triangle() const { return vertexIndices[2] == vertexIndices[3]; }
    std::size_t cornerCount() const { return triangle() ? 3u : 4u; }
    bool skipped() const { return (flags & kPrimitiveSkip) != 0; }
    // FUN_002129b8 lines 133-141: with this bit clear only corner 0 reads the
    // colour table, at colourIndex, and every later corner reuses that colour.
    // With it set each corner reads colourIndex + corner. Reading per-corner
    // regardless picks up colours belonging to neighbouring primitives.
    bool perVertexColour() const { return (flags & kPrimitivePerVertex) != 0; }
  };

  // One submesh == one bone. FUN_0020eec0 uploads exactly submeshCount 64-byte
  // matrices, and the vertex bone byte indexes that palette.
  struct Psc3Submesh
  {
    std::uint16_t vertexStreamStart = 0; // +0x00
    std::uint16_t vertexStreamEnd = 0;   // +0x02
    std::uint16_t primitiveStart = 0;    // +0x04
    std::uint16_t primitiveEnd = 0;      // +0x06, FUN_00212058 loops to the max of these
    std::uint16_t byteLength = 0;        // +0x08
    // +0x0A. For every non-root bone in every s01_e024 model the low byte does
    // hold the parent index, which is what psc3_full.py reads -- but the game
    // never reads it, root bones carry junk in it (grp_0091's bone 0 is a root
    // and says 15), and the hierarchy below is built the way FUN_0020d618
    // builds it instead. Kept only so the record round-trips.
    std::uint16_t field0a = 0;
    // The *high* byte of +0x0A is read, though. FUN_0020dd78 scans the submesh
    // table for the first bone whose `+0x0B & 0xF` equals a requested role, so
    // this nibble is how native code finds a semantic bone -- the hand it puts a
    // weapon in, the head it turns to look at you, the neck it hangs the
    // bandana from. Role 0 means "no role"; FUN_0020dd78 returns bone 0 when
    // nothing matches, so a caller cannot tell a miss from a hit on bone 0.
    //
    // grp_0001 (Orphen): 9 = foot L, 8 = foot R, 2 = chest, 4 = right hand,
    // 10 = left hand, 3/5/11 = fingers, 7 = neck, 1 = head.
    std::uint8_t boneRole() const { return static_cast<std::uint8_t>((field0a >> 8) & 0x0F); }
    // +0x0C. Byte offset, from the model base, of this bone's child list.
    // Zero means no children. FUN_0020d618 recurses over it.
    std::uint16_t childListOffset = 0;
    std::uint32_t sectionAOffset = 0; // +0x10

    // Derived by walking the tree from the root list, not read from the file.
    std::vector<std::uint8_t> children;
    int parentIndex = -1;
  };

  // One record of the section at header +0x34, 0x10 bytes. FUN_002148a8 builds
  // the swing's swept volume out of a pair of these, one for the pose column
  // the animation is leaving and one for the column it is entering.
  //
  // grp_0179's five records, as the file stores them:
  //
  //   0: steps 1  radius 14  A (0,0,217)  B (0,0,-16)
  //   1: steps 4  radius  2  A (1,-2,8)   B (1,-2,118)
  //   2: steps 2  radius 12  A (0,1,304)  B (-2,0,-12)
  //   3: steps 4  radius  4  A (-5,0,-40) B (-5,0,88)
  //   4: steps 4  radius  4  A (0,0,-36)  B (0,0,204)
  struct Psc3HitVolume
  {
    // +0x00: how many boxes to lay down along the segment. The sweep starts at
    // this many and grows, capped at 31.
    std::int16_t steps = 0;
    // +0x02: the box half-extent, in fortieths of a unit before the entity's
    // +0x14C scale. FUN_002148a8's expression is `(v * 256 / 40) / 256`, an
    // integer divide by 40 with a redundant round trip through 8.8 fixed point.
    std::int16_t radius = 0;
    // +0x04 and +0x0A: the segment's two endpoints in the model's own space,
    // s16 over 256.
    std::array<std::int16_t, 3> pointA{};
    std::array<std::int16_t, 3> pointB{};
  };

  struct Psc3Model
  {
    bool valid = false;
    std::string diagnostic; // why it is not valid, when it is not

    // The decoded resource, kept because the skeleton and animation sections are
    // read by offset against it rather than copied out. Costs a couple of
    // megabytes across a scene's models and saves threading a second span
    // through every call.
    std::vector<std::uint8_t> blob;

    std::vector<Psc3Submesh> submeshes;
    std::vector<Psc3Vertex> vertices;
    std::vector<Psc3Primitive> primitives;
    std::vector<Psc3Subdraw> subdraws;
    std::vector<Vec3> normals;
    std::vector<std::uint8_t> colours; // raw, three bytes per entry

    // The list at header +0x10, and the depth-first traversal of it through
    // each bone's child list. `boneOrder` is the order FUN_0020c810 composes
    // matrices in, so a parent is always visited before its children -- which
    // is exactly what the matrix palette build needs.
    std::vector<std::uint8_t> rootBones;
    std::vector<std::uint8_t> boneOrder;
    // Bones the traversal never reached. Non-zero means the hierarchy read is
    // wrong somewhere, so it is reported rather than silently tolerated.
    std::size_t unreachableBones = 0;

    // Kept as offsets: the skeleton and animation slices are consumed by R3/R4
    // against the same blob, and copying them here would just duplicate it.
    std::uint32_t animationTableOffset = 0; // header +0x0C
    // Header +0x06. FUN_00225c90 line 27 bounds-checks the entity's animation
    // id against it, so it is the animation count, not the reserved field the
    // docs called it.
    std::uint16_t animationCount = 0;
    std::uint32_t keyframePoolOffset = 0;   // header +0x2C

    // Header +0x30 and +0x34, empty on a model that carries neither.
    //
    // The map is indexed by pose column -- entity +0xAC and +0xAE -- and holds
    // a record index, or 0xFF for "this column has no hit volume". grp_0179's
    // is 16 bytes: ff 00 ff 01 02 ff ff ff ff ff ff 03 04 00 00 00, so the
    // blade is only dangerous on columns 1, 3, 4, 11 and 12.
    //
    // Neither section is counted anywhere in the header, so the extents come
    // from the following section offset. The original never bounds-checks
    // either read; the port clamps, and a clamped read means the model is
    // malformed rather than that the game would have done something else.
    std::vector<std::uint8_t> hitVolumeColumnMap;
    std::vector<Psc3HitVolume> hitVolumes;

    // A **sprite strip**, not a skeletal model: no magic, no geometry, and a
    // four-byte animation timeline instead of a six-byte one. `grp_017e`, the
    // magic projectile's, is 324 bytes of it. See loadSpriteStripModel.
    //
    // Which of the two an entity is comes from its descriptor rather than from
    // the blob: +0x02 bit 0x200. FUN_00225c90 branches on it at its very first
    // line and FUN_0020c5a8 refuses to put such an entity in the skeletal draw
    // list at all -- they are drawn by FUN_0020f3e0's separate billboard pass.
    bool spriteStrip = false;

    Bounds3 bounds;

    // Counters the report prints. `skippedPrimitives` is the kPrimitiveSkip
    // count, not an error.
    std::size_t skippedPrimitives = 0;
    std::size_t texturedPasses = 0;
    std::size_t untexturedPasses = 0;
  };

  // FUN_0020dd78: the first bone carrying `role` in its +0x0B nibble, or 0.
  // The original walks the submesh table in order and returns the running index,
  // so ties go to the lowest bone index.
  std::size_t FUN_0020dd78_bone_for_role(const Psc3Model &model, std::uint8_t role);

  bool hasPsc3Magic(std::span<const std::uint8_t> bytes);

  // Never throws and never partially reports success: on any malformed field the
  // result comes back with valid == false and diagnostic set.
  Psc3Model loadPsc3Model(std::span<const std::uint8_t> bytes);

  // The sprite-strip kind, for a blob with no PSC3 magic. Header, all of it:
  //
  //   +0x00 u16  sprite record count
  //   +0x02 u16  animation count
  //   +0x04 u32  (unread here)
  //   +0x08 u32  sprite record table, 16 bytes each
  //   +0x0C u32  animation table, one u32 offset per animation
  //
  // The animation table is at the same header offset a PSC3's is, which is what
  // lets FUN_00225c90 read `+0x9C` the same way for both.
  Psc3Model loadSpriteStripModel(std::span<const std::uint8_t> bytes);

} // namespace orphen::ported::model
