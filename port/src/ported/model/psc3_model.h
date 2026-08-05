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
  // Bit 8 does two jobs. FUN_00212058:84 suppresses the +0x0C byte with it, and
  // :229 also copies it into draw header byte 15, which VU1 0x01ba reads to
  // branch past the whole lighting block -- so the primitive keeps its authored
  // colour untouched.
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
    // +0x0C. Byte offset, from the model base, of this bone's child list.
    // Zero means no children. FUN_0020d618 recurses over it.
    std::uint16_t childListOffset = 0;
    std::uint32_t sectionAOffset = 0; // +0x10

    // Derived by walking the tree from the root list, not read from the file.
    std::vector<std::uint8_t> children;
    int parentIndex = -1;
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

    Bounds3 bounds;

    // Counters the report prints. `skippedPrimitives` is the kPrimitiveSkip
    // count, not an error.
    std::size_t skippedPrimitives = 0;
    std::size_t texturedPasses = 0;
    std::size_t untexturedPasses = 0;
  };

  bool hasPsc3Magic(std::span<const std::uint8_t> bytes);

  // Never throws and never partially reports success: on any malformed field the
  // result comes back with valid == false and diagnostic set.
  Psc3Model loadPsc3Model(std::span<const std::uint8_t> bytes);

} // namespace orphen::ported::model
