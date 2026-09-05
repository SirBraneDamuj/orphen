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
    texture.indices.resize(kImageBytes);
    texture.palette.assign(bmpaBytes.begin() + kPaletteOffset,
                           bmpaBytes.begin() + kPaletteOffset + kPaletteBytes);

    for (std::size_t y = 0; y < BmpaTexture::kHeight; ++y)
    {
      const std::size_t sourceY = BmpaTexture::kHeight - 1 - y;
      for (std::size_t x = 0; x < BmpaTexture::kWidth; ++x)
      {
        const std::uint8_t paletteIndex = bmpaBytes[kIndexOffset + sourceY * BmpaTexture::kWidth + x];
        const std::size_t paletteOffset = kPaletteOffset + static_cast<std::size_t>(paletteIndex) * 4;
        const std::size_t pixelOffset = (y * BmpaTexture::kWidth + x) * 4;
        texture.indices[y * BmpaTexture::kWidth + x] = paletteIndex;

        // PS2 palette alpha runs 0..0x80, where 0x80 is fully opaque -- not
        // 0..0xFF. Copying it verbatim into an 8-bit GL alpha drew every opaque
        // texel at 128/255, so everything textured was half transparent.
        //
        // tools/resource_extract's PNGs confirm the scale: their alpha values
        // are 0, 180, 192, 254 and 255, which is exactly min(255, raw * 2) of
        // 0, 90, 96, 127 and 128.
        const std::uint8_t rawAlpha = bmpaBytes[paletteOffset + 3];
        const std::uint8_t alpha =
            rawAlpha >= 0x80 ? 0xFF : static_cast<std::uint8_t>(rawAlpha * 2);

        texture.rgbaPixels[pixelOffset] = bmpaBytes[paletteOffset + 2];
        texture.rgbaPixels[pixelOffset + 1] = bmpaBytes[paletteOffset + 1];
        texture.rgbaPixels[pixelOffset + 2] = bmpaBytes[paletteOffset];
        texture.rgbaPixels[pixelOffset + 3] = alpha;
      }
    }

    return texture;
  }

  // The 4-bit read of the same sheet. `indices` is already stored in GS v order,
  // so only the palette lookup changes: mask the index to its low nibble and
  // offset into the palette by the CSA window.
  std::vector<std::uint8_t> BmpaTexture::clutBankPixels(int bank) const
  {
    if (indices.empty() || palette.size() < kPaletteBytes || bank < 0 || bank >= kClutBankCount)
    {
      return {};
    }

    std::vector<std::uint8_t> pixels(indices.size() * 4);
    const std::size_t bankBase = static_cast<std::size_t>(bank) * kClutBankEntries;
    for (std::size_t at = 0; at < indices.size(); ++at)
    {
      const std::size_t entry = (bankBase + (indices[at] & 0x0Fu)) * 4;
      // Same 0..0x80 alpha the 8-bit path expands; see the note above.
      const std::uint8_t rawAlpha = palette[entry + 3];
      pixels[at * 4] = palette[entry + 2];
      pixels[at * 4 + 1] = palette[entry + 1];
      pixels[at * 4 + 2] = palette[entry];
      pixels[at * 4 + 3] =
          rawAlpha >= 0x80 ? 0xFF : static_cast<std::uint8_t>(rawAlpha * 2);
    }
    return pixels;
  }

} // namespace orphen::ported::resource
