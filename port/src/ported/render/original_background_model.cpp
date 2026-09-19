#include "ported/render/original_background_model.h"

#include "ported/render/original_view_projection.h"

#include <cstring>

namespace orphen::ported::render
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    std::uint32_t readU32(std::span<const std::uint8_t> blob, std::size_t offset)
    {
      std::uint32_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }

    std::int16_t readS16(std::span<const std::uint8_t> blob, std::size_t offset)
    {
      std::int16_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }

    std::uint16_t readU16(std::span<const std::uint8_t> blob, std::size_t offset)
    {
      std::uint16_t value = 0;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }

    float readF32(std::span<const std::uint8_t> blob, std::size_t offset)
    {
      float value = 0.0f;
      std::memcpy(&value, blob.data() + offset, sizeof(value));
      return value;
    }

    // 'PSB4'. FUN_0022CE60:22 bails to the diagnostic at 0x34c1b8 without it.
    constexpr std::uint32_t kPsb4Magic = 0x34425350u;
    constexpr std::size_t kVertexStride = 12;
    constexpr std::size_t kPrimitiveStride = 22;
  } // namespace

  Psb4Model FUN_0022ce60_parse_psb4(std::span<const std::uint8_t> decoded)
  {
    Psb4Model model;
    if (decoded.size() < 0x10)
    {
      model.diagnostic = "shorter than a PSB4 header";
      return model;
    }
    if (readU32(decoded, 0) != kPsb4Magic)
    {
      model.diagnostic = "not PSB4";
      return model;
    }

    const std::uint32_t vertexSection = readU32(decoded, 4);
    const std::uint32_t primitiveSection = readU32(decoded, 8);

    // FUN_0022CE60:36-60. A zero offset is a legal empty section, and the count
    // is a *signed* halfword the original tests with `0 < lVar9`.
    if (vertexSection != 0)
    {
      if (vertexSection + 4 > decoded.size())
      {
        model.diagnostic = "vertex section past the end";
        return model;
      }
      const std::int16_t rawCount = readS16(decoded, vertexSection);
      const std::size_t count = rawCount > 0 ? static_cast<std::size_t>(rawCount) : 0u;
      if (vertexSection + 4 + count * kVertexStride > decoded.size())
      {
        model.diagnostic = "vertex data past the end";
        return model;
      }
      model.vertices.reserve(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        // The original reads x from the record's +0, y from +4 and z from +8,
        // spelled across three pointers that all advance by 12.
        const std::size_t base = vertexSection + 4 + index * kVertexStride;
        model.vertices.push_back(
            Vec3{readF32(decoded, base), readF32(decoded, base + 4), readF32(decoded, base + 8)});
      }
    }

    if (primitiveSection != 0)
    {
      if (primitiveSection + 4 > decoded.size())
      {
        model.diagnostic = "primitive section past the end";
        return model;
      }
      const std::int16_t rawCount = readS16(decoded, primitiveSection);
      const std::size_t count = rawCount > 0 ? static_cast<std::size_t>(rawCount) : 0u;
      if (primitiveSection + 4 + count * kPrimitiveStride > decoded.size())
      {
        model.diagnostic = "primitive data past the end";
        return model;
      }
      model.primitives.reserve(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        const std::size_t base = primitiveSection + 4 + index * kPrimitiveStride;
        Psb4Primitive primitive;
        primitive.flags = readU16(decoded, base);
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          primitive.vertexIndices[corner] = readU16(decoded, base + 2 + corner * 2);
        }
        // FUN_0022CE60:86-110. Three bytes a corner, filed two different ways.
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          const std::size_t group = base + 10 + corner * 3;
          if (primitive.textured())
          {
            primitive.textureCoordinates[corner * 2] = decoded[group];
            primitive.textureCoordinates[corner * 2 + 1] = decoded[group + 1];
            // Only corner 0's third byte is ever read back: FUN_00211B80:67
            // takes record +0x08, which is where the first corner lands.
            if (corner == 0)
            {
              primitive.textureSlot = decoded[group + 2];
            }
          }
          else if (corner == 0)
          {
            primitive.flatColour = {decoded[group], decoded[group + 1], decoded[group + 2]};
          }
        }
        model.primitives.push_back(primitive);
      }
    }

    model.valid = true;
    return model;
  }

  void FUN_0020c2f0_build_background_quads(const BackgroundSlot &slot,
                                           const Vec3 &origin,
                                           std::vector<BackgroundQuad> &out)
  {
    // FUN_0020C2F0:18. The shade byte is tested before anything else is built.
    if (!slot.drawable())
    {
      return;
    }

    // FUN_0020C2F0:34-38, built with the same helpers rather than by hand:
    // identity, Rz about the model's own axis, then the origin, multiplied in
    // that order so the turn does not move the origin.
    Matrix4 turn = FUN_0020bc38_identity();
    FUN_0020bae0_setRotationZ(turn, slot.DAT_00345a34_angleZ);
    Matrix4 place = FUN_0020bc38_identity();
    FUN_0020bb48_setTranslation(place, origin.x, origin.y, origin.z);
    const Matrix4 model = FUN_0020bb58_multiply(turn, place);

    const Psb4Model &mesh = slot.model;
    for (const Psb4Primitive &primitive : mesh.primitives)
    {
      BackgroundQuad quad;
      quad.cornerCount = primitive.cornerCount();
      quad.blendMode = primitive.blendMode();
      quad.textureSlot = primitive.textured() ? static_cast<int>(primitive.textureSlot) : -1;

      bool resolved = true;
      for (std::size_t corner = 0; corner < quad.cornerCount; ++corner)
      {
        const std::size_t index = primitive.vertexIndices[corner];
        if (index >= mesh.vertices.size())
        {
          resolved = false;
          break;
        }
        const Vec3 &local = mesh.vertices[index];
        // FUN_00218EB0 for one point, the 3x4 the VU0 microprogram applies.
        quad.corner[corner] =
            Vec3{local.x * model.at(0, 0) + local.y * model.at(1, 0) + local.z * model.at(2, 0) +
                     model.at(3, 0),
                 local.x * model.at(0, 1) + local.y * model.at(1, 1) + local.z * model.at(2, 1) +
                     model.at(3, 1),
                 local.x * model.at(0, 2) + local.y * model.at(1, 2) + local.z * model.at(2, 2) +
                     model.at(3, 2)};
        quad.u[corner] = static_cast<float>(primitive.textureCoordinates[corner * 2]);
        quad.v[corner] = static_cast<float>(primitive.textureCoordinates[corner * 2 + 1]);
      }
      if (!resolved)
      {
        continue;
      }

      // FUN_00211B80:120-131. A textured primitive is handed the literal
      // DAT_00808080 -- neutral modulate -- and an untextured one its own three
      // bytes. Both are GS bytes where 0x80 is x1.0, which is the unit
      // everything downstream of here works in.
      const std::array<std::uint8_t, 3> rgb =
          primitive.textured() ? std::array<std::uint8_t, 3>{0x80, 0x80, 0x80}
                               : primitive.flatColour;
      quad.colour = {rgb[0], rgb[1], rgb[2], primitive.alpha()};

      out.push_back(quad);
    }
  }

} // namespace orphen::ported::render
