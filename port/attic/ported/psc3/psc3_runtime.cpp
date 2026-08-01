#include "ported/psc3/psc3_runtime.h"

#include "ported/resource/mcb_runtime.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace orphen::ported::psc3
{
  namespace
  {

    constexpr std::array<std::uint8_t, 4> kPsc3Magic{'P', 'S', 'C', '3'};
    constexpr float kPositionScale = 1.0f / 2048.0f;

    std::uint16_t readLeU16(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      if (offset > bytes.size() || sizeof(std::uint16_t) > bytes.size() - offset)
      {
        throw std::runtime_error("u16 read outside PSC3 buffer");
      }

      return static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::int16_t readLeS16(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int16_t>(readLeU16(bytes, offset));
    }

    bool offsetWithin(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t neededBytes)
    {
      return offset <= bytes.size() && neededBytes <= bytes.size() - offset;
    }

    orphen::ported::psm2::Vec3 readVertex(std::span<const std::uint8_t> bytes, std::uint32_t vertexTableOffset, std::int16_t vertexIndex)
    {
      if (vertexIndex < 0)
      {
        throw std::runtime_error("negative PSC3 vertex index");
      }

      const std::size_t recordOffset = static_cast<std::size_t>(vertexTableOffset) + static_cast<std::size_t>(vertexIndex) * 10;
      if (!offsetWithin(bytes, recordOffset, 6))
      {
        throw std::runtime_error("PSC3 vertex index outside vertex table");
      }

      return {static_cast<float>(readLeS16(bytes, recordOffset + 0)) * kPositionScale,
              static_cast<float>(readLeS16(bytes, recordOffset + 2)) * kPositionScale,
              static_cast<float>(readLeS16(bytes, recordOffset + 4)) * kPositionScale};
    }

    void appendTriangle(Psc3RuntimeModel &model,
                        const orphen::ported::psm2::Vec3 &first,
                        const orphen::ported::psm2::Vec3 &second,
                        const orphen::ported::psm2::Vec3 &third)
    {
      model.triangles.push_back({{first, second, third}});
      orphen::ported::psm2::includePoint(model.bounds, first);
      orphen::ported::psm2::includePoint(model.bounds, second);
      orphen::ported::psm2::includePoint(model.bounds, third);
    }

  } // namespace

  bool hasPsc3Magic(std::span<const std::uint8_t> bytes)
  {
    return bytes.size() >= kPsc3Magic.size() && std::equal(kPsc3Magic.begin(), kPsc3Magic.end(), bytes.begin());
  }

  Psc3RuntimeModel loadPsc3Model(std::span<const std::uint8_t> bytes)
  {
    if (!hasPsc3Magic(bytes))
    {
      throw std::runtime_error("PSC3 model missing magic");
    }

    Psc3RuntimeModel model;
    const std::uint16_t submeshCount = readLeU16(bytes, 0x04);
    const std::uint32_t submeshTableOffset = orphen::ported::resource::readLeU32(bytes, 0x08);
    const std::uint32_t vertexTableOffset = orphen::ported::resource::readLeU32(bytes, 0x14);
    const std::uint32_t drawDescriptorOffset = orphen::ported::resource::readLeU32(bytes, 0x1c);

    if (submeshTableOffset == 0 || vertexTableOffset == 0 || drawDescriptorOffset == 0)
    {
      return model;
    }
    if (!offsetWithin(bytes, submeshTableOffset, static_cast<std::size_t>(submeshCount) * 0x14))
    {
      throw std::runtime_error("PSC3 submesh table outside buffer");
    }

    std::uint16_t drawDescriptorCount = 0;
    for (std::uint16_t submeshIndex = 0; submeshIndex < submeshCount; ++submeshIndex)
    {
      const std::size_t submeshOffset = static_cast<std::size_t>(submeshTableOffset) + static_cast<std::size_t>(submeshIndex) * 0x14;
      drawDescriptorCount = std::max(drawDescriptorCount, readLeU16(bytes, submeshOffset + 0x06));
    }
    model.drawDescriptorCount = drawDescriptorCount;

    for (std::uint16_t descriptorIndex = 0; descriptorIndex < drawDescriptorCount; ++descriptorIndex)
    {
      const std::size_t descriptorOffset = static_cast<std::size_t>(drawDescriptorOffset) + static_cast<std::size_t>(descriptorIndex) * 0x18;
      if (!offsetWithin(bytes, descriptorOffset, 0x18))
      {
        ++model.skippedDescriptorCount;
        continue;
      }

      const std::uint16_t flags = readLeU16(bytes, descriptorOffset + 0x08);
      if ((flags & 0x20) != 0)
      {
        ++model.skippedDescriptorCount;
        continue;
      }

      const std::array<std::int16_t, 4> vertexIndices{readLeS16(bytes, descriptorOffset + 0x00),
                                                      readLeS16(bytes, descriptorOffset + 0x02),
                                                      readLeS16(bytes, descriptorOffset + 0x04),
                                                      readLeS16(bytes, descriptorOffset + 0x06)};
      try
      {
        const auto first = readVertex(bytes, vertexTableOffset, vertexIndices[0]);
        const auto second = readVertex(bytes, vertexTableOffset, vertexIndices[1]);
        const auto third = readVertex(bytes, vertexTableOffset, vertexIndices[2]);
        appendTriangle(model, first, second, third);

        if (vertexIndices[2] != vertexIndices[3])
        {
          const auto fourth = readVertex(bytes, vertexTableOffset, vertexIndices[3]);
          appendTriangle(model, first, third, fourth);
        }
      }
      catch (const std::exception &)
      {
        ++model.skippedDescriptorCount;
      }
    }

    return model;
  }

} // namespace orphen::ported::psc3
