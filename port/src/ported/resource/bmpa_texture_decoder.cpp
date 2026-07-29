#include "ported/resource/bmpa_texture_decoder.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace orphen::ported::resource
{
  namespace
  {

    constexpr std::array<std::uint8_t, 4> kBmpaMagic{'B', 'M', 'P', 'A'};
    constexpr std::size_t kPaletteOffset = 4;
    constexpr std::size_t kPaletteEntryCount = 256;
    constexpr std::size_t kPaletteBytes = kPaletteEntryCount * 4;
    constexpr std::size_t kIndexOffset = kPaletteOffset + kPaletteBytes;
    constexpr std::size_t kImageBytes = BmpaTexture::kWidth * BmpaTexture::kHeight;
    constexpr std::size_t kTotalBytes = kIndexOffset + kImageBytes;

  } // namespace

  BmpaTexture decodeBmpaTexture(std::span<const std::uint8_t> bmpaBytes)
  {
    if (bmpaBytes.size() < kTotalBytes)
    {
      throw std::runtime_error("BMPA texture is shorter than the expected 256x256 indexed payload");
    }
    if (!std::equal(kBmpaMagic.begin(), kBmpaMagic.end(), bmpaBytes.begin()))
    {
      throw std::runtime_error("BMPA texture has an invalid magic");
    }

    BmpaTexture texture;
    texture.rgbaPixels.resize(kImageBytes * 4);

    for (std::size_t y = 0; y < BmpaTexture::kHeight; ++y)
    {
      const std::size_t sourceY = BmpaTexture::kHeight - 1 - y;
      for (std::size_t x = 0; x < BmpaTexture::kWidth; ++x)
      {
        const std::uint8_t paletteIndex = bmpaBytes[kIndexOffset + sourceY * BmpaTexture::kWidth + x];
        const std::size_t paletteOffset = kPaletteOffset + static_cast<std::size_t>(paletteIndex) * 4;
        const std::size_t pixelOffset = (y * BmpaTexture::kWidth + x) * 4;

        texture.rgbaPixels[pixelOffset] = bmpaBytes[paletteOffset + 2];
        texture.rgbaPixels[pixelOffset + 1] = bmpaBytes[paletteOffset + 1];
        texture.rgbaPixels[pixelOffset + 2] = bmpaBytes[paletteOffset];
        texture.rgbaPixels[pixelOffset + 3] = bmpaBytes[paletteOffset + 3];
      }
    }

    return texture;
  }

} // namespace orphen::ported::resource
