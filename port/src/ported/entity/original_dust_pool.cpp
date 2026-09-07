#include "ported/entity/original_dust_pool.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // fGpffff8394 / 8398: the rise and the slide, both per tick.
    inline constexpr float kFGpffff8394_rise = 0.00300000002607703f;
    inline constexpr float kFGpffff8398_slide = 0.00999999977648258f;
    // fGpffff8388 and DAT_00352300, both a full turn -- two separate words, one
    // for each ring stepper.
    inline constexpr float kFGpffff8388_turn = 6.28318548202515f;
    inline constexpr float kDAT_00352300_turn = 6.28318548202515f;
    // The colour a zero entry draws with.
    inline constexpr std::uint32_t kFUN_0021a820_defaultColour = 0xF0F0F0u;
    // The sheet the packet names, and the CLUT bank a zero colour selects.
    inline constexpr int kDustTextureSlot = 0x21;
    inline constexpr int kDustDefaultClutBank = 2;
    // DAT_0031553C's four ST pairs, as texels on a 256-wide sheet.
    inline constexpr float kDustU0 = 0.501562476158142f * 256.0f;
    inline constexpr float kDustV0 = 0.001562500023283f * 256.0f;
    inline constexpr float kDustU1 = 0.653906285762787f * 256.0f;
    inline constexpr float kDustV1 = 0.153906241059303f * 256.0f;
    // FUN_0021A820:0x0021ab84 writes 0x10004580 into the packet's +0x0C, and
    // FUN_00207de8's `& 0x1C000` ladder reads bit 0x4000 first: mode 1, the
    // alpha blend. Not 2. The neighbouring pools write 0x10008080 and
    // 0x10008580, which is why they are additive and this is not.
    inline constexpr int kDustBlendMode = 1;
    inline constexpr int kDustDisplayListBucket = 0x1000;

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    std::int32_t roll(const std::function<std::uint32_t()> &random)
    {
      return static_cast<std::int32_t>(random ? random() : 0);
    }

    // FUN_0030BD20 truncates toward zero, so a jitter range under a hundredth
    // becomes no jitter at all rather than a very small one.
    std::int32_t hundredths(float value)
    {
      return static_cast<std::int32_t>(value * 100.0f);
    }

    float jitter(std::int32_t range, const std::function<std::uint32_t()> &random)
    {
      if (range == 0)
      {
        return 0.0f;
      }
      return static_cast<float>(static_cast<std::uint32_t>(roll(random)) %
                                static_cast<std::uint32_t>(range)) /
             100.0f;
    }
  } // namespace

  void DustPool::FUN_0021a698_reset()
  {
    for (DustPuff &puff : puffs_)
    {
      puff = DustPuff{};
    }
    live_ = 0;
  }

  DustPuff *DustPool::FUN_0021a730_allocate()
  {
    for (DustPuff &puff : puffs_)
    {
      if (!puff.alive())
      {
        return &puff;
      }
    }
    return nullptr;
  }

  std::size_t DustPool::aliveCount() const
  {
    std::size_t alive = 0;
    for (const DustPuff &puff : puffs_)
    {
      if (puff.alive())
      {
        ++alive;
      }
    }
    return alive;
  }

  void DustPool::FUN_0021a760_step(std::uint32_t frameTicks)
  {
    // FUN_0021A760's own guard: the walk does not run at all while the live
    // count is zero.
    if (live_ <= 0)
    {
      return;
    }
    for (DustPuff &puff : puffs_)
    {
      if (!puff.alive())
      {
        continue;
      }
      const auto remaining = static_cast<std::int16_t>(
          puff.remaining1c - static_cast<std::int16_t>(frameTicks & 0xFFFFu));
      puff.remaining1c = remaining;
      if (remaining < 1)
      {
        puff.alive20 = -1;
        --live_;
        continue;
      }
      // The slide is gated on the heading being *exactly* zero -- a float test,
      // not a flag -- so the first puff of every ring does not slide.
      if (puff.heading0c != 0.0f)
      {
        puff.x00 += kFGpffff8398_slide * cos_of(puff.heading0c);
        puff.y04 += kFGpffff8398_slide * sin_of(puff.heading0c);
      }
      puff.z08 += kFGpffff8394_rise;
    }
  }

  void DustPool::FUN_0021a4f8_spawn_ring(float x, float y, float z,
                                         float size,
                                         float jitterX,
                                         float jitterY,
                                         float radius,
                                         std::int16_t lifeSpread,
                                         int count,
                                         std::uint32_t colour,
                                         std::uint8_t shape,
                                         const std::function<std::uint32_t()> &random)
  {
    if (count <= 0)
    {
      return;
    }
    // No jitter here: FUN_0021A4F8 only *stores* the two ranges. Whoever called
    // it has already rolled them into the position it was handed.
    const float step = (360.0f / static_cast<float>(count)) * kDAT_00352300_turn;
    float heading = 0.0f;

    for (int index = 0; index < count; ++index)
    {
      DustPuff *puff = FUN_0021a730_allocate();
      if (puff == nullptr)
      {
        return;
      }
      puff->alive20 = 1;
      puff->x00 = x + radius * cos_of(heading);
      puff->y04 = y + radius * sin_of(heading);
      puff->z08 = z;
      puff->heading0c = heading;
      puff->jitterX10 = jitterX;
      puff->jitterY14 = jitterY;
      puff->size18 = size;
      puff->shape21 = shape;
      puff->colour24 = colour;
      const std::int32_t spread = lifeSpread != 0 ? lifeSpread : 1;
      const auto life = static_cast<std::int16_t>(((roll(random) % spread) + 5) << 5);
      puff->remaining1c = life;
      puff->total1e = life;
      ++live_;
      heading += step / 360.0f;
    }
  }

  void DustPool::FUN_00219af0_spawn_impact(float x, float y, float z,
                                           float size,
                                           float jitterX,
                                           float jitterY,
                                           float radius,
                                           std::int16_t lifeSpread,
                                           int outerCount,
                                           int innerCount,
                                           std::uint8_t shape,
                                           bool lit,
                                           const std::function<std::uint32_t()> &random)
  {
    if (outerCount <= 0)
    {
      return;
    }
    // Two nested rings. The outer one is walked here, rolling both jitters per
    // step and folding the first into the position, and each step hands a whole
    // inner ring of `innerCount` to FUN_0021A4F8 -- so a call with 5 and 20
    // puts a hundred puffs down.
    const std::uint32_t colour = lit ? 0xFFFFFFu : 0u;
    const float step = (360.0f / static_cast<float>(outerCount)) * kFGpffff8388_turn / 360.0f;
    const std::int32_t rangeX = hundredths(jitterX);
    const std::int32_t rangeY = hundredths(jitterY);
    float heading = 0.0f;

    for (int index = 0; index < outerCount; ++index)
    {
      const float offsetXY = jitter(rangeX, random);
      const float offsetZ = jitter(rangeY, random);
      const float px = x + offsetXY * sin_of(heading);
      const float py = y + offsetXY * cos_of(heading);
      FUN_0021a4f8_spawn_ring(px, py, z + offsetZ, size, jitterX, jitterY, radius, lifeSpread,
                              innerCount, colour, shape, random);
      heading += step;
    }
  }

  void DustPool::FUN_0021a170_spawn_one(float x, float y, float z,
                                        float size,
                                        float jitterX,
                                        float jitterY,
                                        std::int16_t lifeSpread,
                                        int count,
                                        std::uint8_t shape,
                                        bool lit,
                                        const std::function<std::uint32_t()> &random)
  {
    // FUN_0021A170 rolls both ranges and then hands the *first* roll in as both
    // the x jitter and the ring radius, which is what scatters a single puff
    // inside its range instead of dropping it on the point.
    const std::int32_t rangeX = hundredths(jitterX);
    const std::int32_t rangeY = hundredths(jitterY);
    const float offsetXY = jitter(rangeX, random);
    const float offsetZ = jitter(rangeY, random);
    const std::uint32_t colour = lit ? 0xFFFFFFu : 0u;
    FUN_0021a4f8_spawn_ring(x, y, z, size, offsetXY, offsetZ, offsetXY, lifeSpread, count, colour,
                            shape, random);
  }

  orphen::ported::render::SpriteQuad FUN_0021a820_build_dust_quad(const DustQuadInputs &inputs)
  {
    orphen::ported::render::SpriteQuad quad;

    const float viewZ = inputs.viewZ > orphen::ported::render::kDAT_0035209c_spriteNearClip
                            ? inputs.viewZ
                            : orphen::ported::render::kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / viewZ;

    // Sixteen units of +0x18, then the 400 the packet scales every corner by.
    const float halfWidth = inputs.size * 16.0f * 400.0f * q;
    const float halfHeight = halfWidth * 0.5f;

    // Shape 0 hangs from the anchor -- y from -16*size to 0. Shape 1 is centred
    // on it at half the height.
    const float topOffset = inputs.shape == 0 ? -halfWidth : -halfHeight;
    const float bottomOffset = inputs.shape == 0 ? 0.0f : halfHeight;

    const auto gsX0 = static_cast<int>(static_cast<float>(inputs.gsOriginX) - halfWidth);
    const auto gsX1 = static_cast<int>(static_cast<float>(inputs.gsOriginX) + halfWidth);
    const auto gsY0 = static_cast<int>(static_cast<float>(inputs.gsOriginY) + topOffset);
    const auto gsY1 = static_cast<int>(static_cast<float>(inputs.gsOriginY) + bottomOffset);

    const float perX = inputs.projectionScaleX != 0.0f ? viewZ / inputs.projectionScaleX : 0.0f;
    const float perY = inputs.projectionScaleY != 0.0f ? viewZ / inputs.projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
    quad.viewZ = viewZ;

    quad.u0 = kDustU0;
    quad.v0 = kDustV0;
    quad.u1 = kDustU1;
    quad.v1 = kDustV1;

    const std::uint32_t colour = inputs.colour != 0 ? inputs.colour : kFUN_0021a820_defaultColour;
    // FUN_00207de8:130-141. The packet's texture halfword is non-zero here --
    // 0x0221 or 0x0021 -- so the fold takes the textured branch and halves
    // *all four* channels of every vertex, `(c & 0xFEFEFEFE) >> 1`, not just
    // the alpha. 0x80 is 1.0 on the GS, so 0xF0 reaches it as 0.9375 and a
    // freshly spawned puff's 0x80 alpha as 0.5.
    const auto folded = [](std::uint32_t component) {
      return static_cast<float>((component & 0xFEu) >> 1) / 128.0f;
    };
    quad.colour[0] = folded(colour & 0xFFu);
    quad.colour[1] = folded((colour >> 8) & 0xFFu);
    quad.colour[2] = folded((colour >> 16) & 0xFFu);
    quad.colour[3] = folded(static_cast<std::uint32_t>(inputs.alpha) & 0xFFu);

    quad.blendMode = kDustBlendMode;
    quad.textureSlot = kDustTextureSlot;
    // FUN_0021A820's tail is FUN_00207de8(0x1000) -- the same bucket the
    // DAT_00355620 particles and the hit sparks land in, not the default 1.
    quad.displayListBucket = kDustDisplayListBucket;
    // The packet's texture halfword is 0x0221 for a colourless puff and 0x0021
    // for a coloured one; the high byte is the CLUT bank.
    quad.clutBank = inputs.colour == 0 ? kDustDefaultClutBank : -1;
    return quad;
  }

} // namespace orphen::ported::entity
