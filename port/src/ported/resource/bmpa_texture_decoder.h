#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::resource
{

  struct BmpaTexture
  {
    static constexpr std::uint16_t kWidth = 256;
    static constexpr std::uint16_t kHeight = 256;

    std::uint16_t width = kWidth;
    std::uint16_t height = kHeight;
    std::vector<std::uint8_t> rgbaPixels;
  };

  // BMPA texture resource decoder. No single original FUN_* counterpart has been isolated yet.
  BmpaTexture decodeBmpaTexture(std::span<const std::uint8_t> bmpaBytes);

} // namespace orphen::ported::resource
