#include "ported/render/original_screen_layers.h"

namespace orphen::ported::render
{
  namespace
  {
    // DAT_003149A8: the four corners in GS 12.4, as FUN_00202ff8 copies them.
    constexpr std::int32_t kCornerGsX[4] = {0x6C00, 0x6C00, 0x9400, 0x9400};
    constexpr std::int32_t kCornerGsY[4] = {0x7900, 0x8700, 0x8700, 0x7900};
    // The GS origin and the units HudQuad's 640x448 screen is in (see
    // original_hud_quads.h): x is 1/16 pixel, y 1/8 line.
    constexpr std::int32_t kGsOriginX = 0x6C00;
    constexpr std::int32_t kGsOriginY = 0x7900;
    constexpr float kGsXPerPixel = 16.0f;
    constexpr float kGsYPerLine = 8.0f;

    // DAT_003149C8, indexed by flag bit 2 with an 8-*byte* stride
    // (0x00203130 `sll a1,v0,3` onto a byte address), so the second table is
    // the first one shifted by one corner: the same square, flipped in V.
    constexpr float kCornerUv[6][2] = {
        {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f},
    };

    // FUN_00207de8 emits a UV as `uv * 4096` in 4.12: one unit is 256 texels.
    constexpr float kTexelsPerUnit = 256.0f;
    // FUN_00202f00's 1.5258789e-05, 1/65536.
    constexpr float kRateScale = 1.0f / 65536.0f;

    // FUN_00202ff8's wrap: once, and only past +/-1.
    float wrapScroll(float value)
    {
      if (1.0f < value)
      {
        return value - 1.0f;
      }
      if (value < -1.0f)
      {
        return value + 1.0f;
      }
      return value;
    }
  } // namespace

  void ScreenLayers::FUN_00202f00_set(std::int32_t slot,
                                      std::uint8_t flags,
                                      std::uint16_t texture,
                                      std::uint32_t colour,
                                      std::int32_t uOffset,
                                      std::int32_t vOffset,
                                      std::int32_t uRate,
                                      std::int32_t vRate)
  {
    // The original indexes the table unchecked; no retail call passes
    // anything but 0.
    if (slot < 0 || slot >= kSlotCount)
    {
      return;
    }
    Slot &entry = DAT_00356100_slots_[static_cast<std::size_t>(slot)];
    if (entry.flags == 0)
    {
      ++cGpffffba90_liveCount_;
    }
    entry.flags = static_cast<std::uint8_t>(flags | 0x80u);
    entry.uOffset = static_cast<float>(uOffset);
    entry.vOffset = static_cast<float>(vOffset);
    entry.uRate = static_cast<float>(uRate) * kRateScale;
    entry.texture = texture;
    entry.colour = colour;
    entry.vRate = static_cast<float>(vRate) * kRateScale;
  }

  void ScreenLayers::FUN_00202f88_clear(std::int32_t slot)
  {
    if (slot < 0 || slot >= kSlotCount)
    {
      return;
    }
    Slot &entry = DAT_00356100_slots_[static_cast<std::size_t>(slot)];
    if (entry.flags != 0)
    {
      entry.flags = 0;
      --cGpffffba90_liveCount_;
    }
  }

  std::vector<HudQuad> ScreenLayers::FUN_00202ff8_step()
  {
    std::vector<HudQuad> quads;
    for (Slot &entry : DAT_00356100_slots_)
    {
      if (entry.flags == 0)
      {
        continue;
      }
      entry.uOffset = wrapScroll(entry.uOffset + entry.uRate);
      entry.vOffset = wrapScroll(entry.vOffset + entry.vRate);

      HudQuad quad;
      const int table = (entry.flags >> 2) & 1;
      for (int corner = 0; corner < 4; ++corner)
      {
        quad.x[corner] = static_cast<float>(kCornerGsX[corner] - kGsOriginX) / kGsXPerPixel;
        quad.y[corner] = static_cast<float>(kCornerGsY[corner] - kGsOriginY) / kGsYPerLine;
        quad.u[corner] = (kCornerUv[corner + table][0] + entry.uOffset) * kTexelsPerUnit;
        quad.v[corner] = (kCornerUv[corner + table][1] + entry.vOffset) * kTexelsPerUnit;
      }
      // 0xFFFF is FUN_00207de8's "untextured" (its +1 wraps to zero). The HUD
      // pass only draws textured quads, so such a layer is dropped there; no
      // retail call passes one.
      quad.textureSlot = entry.texture == 0xFFFFu ? -1 : static_cast<int>(entry.texture & 0xFFu);
      // FUN_00207de8 writes `bank + 1` beside a slot at or above 0x18.
      quad.clutBank = quad.textureSlot >= 0x18 ? static_cast<int>(entry.texture >> 8) : -1;
      quad.color = entry.colour;
      quad.blendMode = entry.flags & 3;
      quads.push_back(quad);
    }
    return quads;
  }

} // namespace orphen::ported::render
