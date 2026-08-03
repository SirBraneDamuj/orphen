#include "ported/model/psc3_model.h"

#include <algorithm>
#include <array>

namespace orphen::ported::model
{
  namespace
  {
    constexpr std::array<std::uint8_t, 4> kPsc3Magic{'P', 'S', 'C', '3'};

    constexpr std::size_t kSubmeshStride = 0x14;
    constexpr std::size_t kPrimitiveStride = 0x18;
    constexpr std::size_t kSubdrawStride = 10;
    constexpr std::size_t kVertexStride = 10;
    constexpr std::size_t kNormalStride = 16;
    constexpr std::size_t kColourStride = 3;

    bool fits(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t needed)
    {
      return offset <= bytes.size() && needed <= bytes.size() - offset;
    }

    std::uint16_t u16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::int16_t s16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int16_t>(u16At(bytes, offset));
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    float f32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      const std::uint32_t raw = u32At(bytes, offset);
      float value = 0.0f;
      static_assert(sizeof(value) == sizeof(raw));
      std::copy_n(reinterpret_cast<const std::uint8_t *>(&raw), sizeof(raw),
                  reinterpret_cast<std::uint8_t *>(&value));
      return value;
    }

    Psc3Model failure(const char *why)
    {
      Psc3Model model;
      model.diagnostic = why;
      return model;
    }

  } // namespace

  bool hasPsc3Magic(std::span<const std::uint8_t> bytes)
  {
    return bytes.size() >= kPsc3Magic.size() &&
           std::equal(kPsc3Magic.begin(), kPsc3Magic.end(), bytes.begin());
  }

  Psc3Model loadPsc3Model(std::span<const std::uint8_t> bytes)
  {
    if (!hasPsc3Magic(bytes))
    {
      return failure("missing PSC3 magic");
    }
    if (!fits(bytes, 0, 0x30))
    {
      return failure("truncated header");
    }

    Psc3Model model;
    const std::uint16_t submeshCount = u16At(bytes, 0x04);
    const std::uint32_t submeshOffset = u32At(bytes, 0x08);
    const std::uint32_t vertexOffset = u32At(bytes, 0x14);
    const std::uint32_t boneIndexOffset = u32At(bytes, 0x18);
    const std::uint32_t primitiveOffset = u32At(bytes, 0x1C);
    const std::uint32_t colourOffset = u32At(bytes, 0x20);
    const std::uint32_t subdrawOffset = u32At(bytes, 0x24);
    const std::uint32_t normalOffset = u32At(bytes, 0x28);

    model.animationTableOffset = u32At(bytes, 0x0C);
    model.animationCount = u16At(bytes, 0x06);
    model.keyframePoolOffset = u32At(bytes, 0x2C);

    if (submeshCount == 0 || submeshOffset == 0 || vertexOffset == 0 || primitiveOffset == 0)
    {
      return failure("model has no geometry sections");
    }
    if (!fits(bytes, submeshOffset, static_cast<std::size_t>(submeshCount) * kSubmeshStride))
    {
      return failure("submesh table outside buffer");
    }

    // FUN_00212058 lines 58-71: the primitive loop bound is the largest
    // prim_end across submeshes, not a stored count.
    model.submeshes.reserve(submeshCount);
    std::size_t primitiveCount = 0;
    for (std::uint16_t index = 0; index < submeshCount; ++index)
    {
      const std::size_t at = submeshOffset + static_cast<std::size_t>(index) * kSubmeshStride;
      Psc3Submesh submesh;
      submesh.vertexStreamStart = u16At(bytes, at + 0x00);
      submesh.vertexStreamEnd = u16At(bytes, at + 0x02);
      submesh.primitiveStart = u16At(bytes, at + 0x04);
      submesh.primitiveEnd = u16At(bytes, at + 0x06);
      submesh.byteLength = u16At(bytes, at + 0x08);
      submesh.field0a = u16At(bytes, at + 0x0A);
      submesh.childListOffset = u16At(bytes, at + 0x0C);
      submesh.sectionAOffset = u32At(bytes, at + 0x10);

      primitiveCount = std::max<std::size_t>(primitiveCount, submesh.primitiveEnd);
      model.submeshes.push_back(submesh);
    }

    if (!fits(bytes, primitiveOffset, primitiveCount * kPrimitiveStride))
    {
      return failure("primitive table outside buffer");
    }

    // Pass one over the primitives, purely to size the tables the header does
    // not count. Every bound here comes from what the primitives actually
    // reference, which is how FUN_00212058 uses them.
    model.primitives.reserve(primitiveCount);
    std::size_t vertexCount = 0;
    std::size_t subdrawCount = 0;
    std::size_t normalCount = 0;
    std::size_t colourEntryCount = 0;

    for (std::size_t index = 0; index < primitiveCount; ++index)
    {
      const std::size_t at = primitiveOffset + index * kPrimitiveStride;
      Psc3Primitive primitive;
      for (std::size_t corner = 0; corner < 4; ++corner)
      {
        primitive.vertexIndices[corner] = u16At(bytes, at + corner * 2);
      }
      primitive.flags = u16At(bytes, at + 0x08);
      primitive.colourIndex = u16At(bytes, at + 0x0A);
      primitive.fogByte = bytes[at + 0x0C];
      primitive.alphaByte = bytes[at + 0x0D];
      for (std::size_t pass = 0; pass < 4; ++pass)
      {
        primitive.subdrawIndices[pass] = s16At(bytes, at + 0x0E + pass * 2);
      }
      primitive.flatNormalIndex = u16At(bytes, at + 0x16);

      if (primitive.skipped())
      {
        ++model.skippedPrimitives;
        model.primitives.push_back(primitive);
        continue;
      }

      const std::size_t corners = primitive.cornerCount();
      for (std::size_t corner = 0; corner < corners; ++corner)
      {
        vertexCount = std::max<std::size_t>(vertexCount, primitive.vertexIndices[corner] + 1u);
      }
      normalCount = std::max<std::size_t>(normalCount, primitive.flatNormalIndex + 1u);
      colourEntryCount = std::max<std::size_t>(colourEntryCount, primitive.colourIndex + corners);

      for (const std::int16_t subdraw : primitive.subdrawIndices)
      {
        if (subdraw == -1)
        {
          continue;
        }
        if (subdraw < 0)
        {
          // FUN_002129b8 lines 86-111: bit 15 set means "no texture", and the
          // remaining bits are a colour index rather than a subdraw index.
          ++model.untexturedPasses;
          colourEntryCount =
              std::max<std::size_t>(colourEntryCount, (subdraw & 0x7FFF) + corners);
          continue;
        }
        ++model.texturedPasses;
        subdrawCount = std::max<std::size_t>(subdrawCount, static_cast<std::size_t>(subdraw) + 1u);
      }

      model.primitives.push_back(primitive);
    }

    // Per-vertex normals index the same table through vertex record +0x06, so
    // the vertex table has to be read before the normal table can be sized.
    if (!fits(bytes, vertexOffset, vertexCount * kVertexStride))
    {
      return failure("vertex table outside buffer");
    }

    model.vertices.resize(vertexCount);
    for (std::size_t index = 0; index < vertexCount; ++index)
    {
      const std::size_t at = vertexOffset + index * kVertexStride;
      Psc3Vertex &vertex = model.vertices[index];
      vertex.position = Vec3{static_cast<float>(s16At(bytes, at + 0)) * kPositionScale,
                             static_cast<float>(s16At(bytes, at + 2)) * kPositionScale,
                             static_cast<float>(s16At(bytes, at + 4)) * kPositionScale};
      vertex.normalIndex = u16At(bytes, at + 6);
      vertex.trailing = u16At(bytes, at + 8);
      normalCount = std::max<std::size_t>(normalCount, vertex.normalIndex + 1u);

      // The byte table is not present in every model; a missing one leaves
      // every vertex on bone 0, which is the root, which is what an unskinned
      // model wants anyway.
      if (boneIndexOffset != 0 && fits(bytes, boneIndexOffset + index, 1))
      {
        vertex.boneIndex = bytes[boneIndexOffset + index];
      }

      orphen::ported::psm2::includePoint(model.bounds, vertex.position);
    }

    if (subdrawCount != 0)
    {
      if (subdrawOffset == 0 || !fits(bytes, subdrawOffset, subdrawCount * kSubdrawStride))
      {
        return failure("subdraw table outside buffer");
      }
      model.subdraws.resize(subdrawCount);
      for (std::size_t index = 0; index < subdrawCount; ++index)
      {
        const std::size_t at = subdrawOffset + index * kSubdrawStride;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          model.subdraws[index].packedUv[corner] = u16At(bytes, at + corner * 2);
        }
        model.subdraws[index].texFlags = u16At(bytes, at + 0x08);
      }
    }

    if (normalCount != 0)
    {
      if (normalOffset == 0 || !fits(bytes, normalOffset, normalCount * kNormalStride))
      {
        return failure("normal table outside buffer");
      }
      model.normals.resize(normalCount);
      for (std::size_t index = 0; index < normalCount; ++index)
      {
        const std::size_t at = normalOffset + index * kNormalStride;
        // FUN_002129b8 lines 78-82 copies xyz and forces w to zero.
        model.normals[index] = Vec3{f32At(bytes, at + 0), f32At(bytes, at + 4), f32At(bytes, at + 8)};
      }
    }

    if (colourEntryCount != 0)
    {
      if (colourOffset == 0 || !fits(bytes, colourOffset, colourEntryCount * kColourStride))
      {
        return failure("colour table outside buffer");
      }
      model.colours.assign(bytes.begin() + colourOffset,
                           bytes.begin() + colourOffset + colourEntryCount * kColourStride);
    }

    // The skeleton. Header +0x10 points at a region of bone-index byte lists,
    // each terminated by a byte with bit 7 set (FUN_0020c810 tests `< 0x80`).
    // The list at +0x10 itself holds the roots; every other list is pointed at
    // by some bone's +0x0C. They all live in the same region, so bounding each
    // read by the submesh count is enough.
    const auto readBoneList = [&](std::size_t at) {
      std::vector<std::uint8_t> list;
      for (std::size_t index = 0; index < submeshCount && fits(bytes, at + index, 1); ++index)
      {
        const std::uint8_t bone = bytes[at + index];
        if ((bone & 0x80) != 0)
        {
          break;
        }
        if (bone >= submeshCount)
        {
          // A bone index past the palette would index a matrix that does not
          // exist. Treat it as the end of the list rather than trusting it.
          break;
        }
        list.push_back(bone);
      }
      return list;
    };

    const std::uint32_t rootBoneOffset = u32At(bytes, 0x10);
    if (rootBoneOffset != 0)
    {
      model.rootBones = readBoneList(rootBoneOffset);
    }
    for (Psc3Submesh &submesh : model.submeshes)
    {
      if (submesh.childListOffset != 0)
      {
        submesh.children = readBoneList(submesh.childListOffset);
      }
    }

    // Depth-first from the roots, the same order FUN_0020c810 walks and
    // FUN_0020d618 recurses in, so a parent's matrix is always composed before
    // its children need it. `visited` also guards against a malformed file
    // producing a cycle.
    std::vector<bool> visited(submeshCount, false);
    model.boneOrder.reserve(submeshCount);
    const auto walk = [&](auto &&self, std::uint8_t bone, int parent) -> void {
      if (visited[bone])
      {
        return;
      }
      visited[bone] = true;
      model.submeshes[bone].parentIndex = parent;
      model.boneOrder.push_back(bone);
      for (const std::uint8_t child : model.submeshes[bone].children)
      {
        self(self, child, static_cast<int>(bone));
      }
    };
    for (const std::uint8_t root : model.rootBones)
    {
      walk(walk, root, -1);
    }
    model.unreachableBones =
        static_cast<std::size_t>(std::count(visited.begin(), visited.end(), false));

    model.blob.assign(bytes.begin(), bytes.end());
    model.valid = true;
    return model;
  }

} // namespace orphen::ported::model
