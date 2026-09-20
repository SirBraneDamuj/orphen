#include "ported/entity/original_plume_pool.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // fGpffff8460 / 8464 / 8470 / 8474, four separate words all holding
    // **0x40C90FD8** -- 6.283184, three ULP short of float(2*pi). They are kept
    // apart because they are separate words; writing the mathematical constant
    // walks every ring in this file by a different step.
    inline constexpr float kFGpffff8460_puffTurn = 6.283184051513672f;
    inline constexpr float kFGpffff8464_flameTurn = 6.283184051513672f;
    inline constexpr float kFGpffff8470_flareTurn = 6.283184051513672f;
    inline constexpr float kFGpffff8474_burstTurn = 6.283184051513672f;
    // fGpffff8468 / 846C: the two life ratios the flame arm tests, and
    // fGpffff8478, the tenth of the emitter's rise a puff inherits.
    inline constexpr float kFGpffff8468_brightBankAbove = 0.800000011920929f;
    inline constexpr float kFGpffff846c_flameAbove = 0.8999999761581421f;
    inline constexpr float kFGpffff8478_puffRiseShare = 0.10000000149011612f;

    // The literals in the instruction stream.
    inline constexpr float kTickScale = 0.03125f;   // lui 0x3D00, 1/32
    inline constexpr float kDegreesPerTurn = 360.0f;
    inline constexpr float kHundred = 100.0f;       // lui 0x42C8
    inline constexpr float kAlphaSpan = 255.0f;     // lui 0x437F
    inline constexpr float kCornerScale = 400.0f;   // lui 0x43C8
    inline constexpr float kCornerUnits = 16.0f;
    inline constexpr float kPuffLifeFactor = 1.5f;  // lui 0x3FC0
    inline constexpr std::uint16_t kBurstPeriod = 0x0140;
    inline constexpr std::int8_t kImmortalCycles = 0x63;
    inline constexpr std::int8_t kFlameClutBank = 5;
    inline constexpr std::int8_t kEvenClutBank = 6;
    inline constexpr std::int8_t kOddClutBank = 1;
    inline constexpr std::int8_t kBrightClutBank = 2;
    inline constexpr std::uint8_t kSmokeTileCount = 3;
    inline constexpr std::uint8_t kFlameTileCount = 6;

    // The colour a record with no colour word of its own draws with, and the
    // sheet the packet names. 0x20 is at or above 0x18, so it is a 4-bit page
    // and the packet's high byte is a CLUT bank.
    inline constexpr std::uint32_t kPlumeDefaultColour = 0xF0F0F0u;
    inline constexpr int kPlumeTextureSlot = 0x20;
    // FUN_00220210:0x0022057C ors 0x10000080 into the 0x4000 or 0x8000 chosen
    // just above, and FUN_00207DE8's `& 0x1C000` ladder reads bit 0x4000 first.
    inline constexpr int kPlumeAlphaBlendMode = 1;
    inline constexpr int kPlumeAdditiveBlendMode = 2;
    inline constexpr int kPlumeDisplayListBucket = 0x1000;

    // DAT_00315898, three 63x63 smoke tiles along the bottom of the sheet, and
    // DAT_003158F8, six 23x39 flame tiles in a row above them. Both are stored
    // as four ST pairs per tile; these are the first and third of each, times
    // 256 because SpriteQuad takes texels.
    inline constexpr PlumeTexels kDAT_00315898_smokeTiles[kSmokeTileCount] = {
        {64.4f, 192.4f, 127.4f, 255.4f},
        {128.4f, 192.4f, 191.4f, 255.4f},
        {192.4f, 192.4f, 255.4f, 255.4f},
    };
    inline constexpr PlumeTexels kDAT_003158f8_flameTiles[kFlameTileCount] = {
        {64.4f, 104.4f, 87.4f, 143.4f},
        {88.4f, 104.4f, 111.4f, 143.4f},
        {112.4f, 104.4f, 135.4f, 143.4f},
        {136.4f, 104.4f, 159.4f, 143.4f},
        {160.4f, 104.4f, 183.4f, 143.4f},
        {184.4f, 104.4f, 207.4f, 143.4f},
    };

    std::uint32_t roll(const std::function<std::uint32_t()> &random)
    {
      return random ? random() : 0u;
    }

    // `divu` then `mfhi`, so the remainder is unsigned and always in range
    // however the generator's word is signed. The `bltz` ladder around the
    // `cvt.s.w` that follows each one is the compiler's u32-to-float sequence,
    // not a sign test on the remainder.
    std::uint32_t rollModulo(const std::function<std::uint32_t()> &random, std::uint32_t divisor)
    {
      return divisor == 0 ? 0u : roll(random) % divisor;
    }

    // FUN_0030BD20: truncate toward zero, signed.
    std::int32_t FUN_0030bd20_truncate(float value)
    {
      return static_cast<std::int32_t>(value);
    }

    // FUN_0030BDB0, which is `__fixunssfsi`: truncate toward zero, and answer
    // zero for anything that is not a positive finite number.
    std::uint32_t FUN_0030bdb0_truncate(float value)
    {
      if (!(value > 0.0f))
      {
        return 0u;
      }
      return static_cast<std::uint32_t>(value);
    }

    // The shape every scatter in this file takes: a full-turn angle from
    // `roll() % 360` and a radius from `roll() % 10` over a hundred, in that
    // order, because the generator is walked twice and the order is visible.
    struct Scatter
    {
      float x;
      float y;
    };

    Scatter scatter(const std::function<std::uint32_t()> &random, float turn, float x, float y)
    {
      const float angle =
          (static_cast<float>(rollModulo(random, 360u)) * turn) / kDegreesPerTurn;
      const float radius = static_cast<float>(rollModulo(random, 10u)) / kHundred;
      return Scatter{x + radius * std::cos(angle), y + radius * std::sin(angle)};
    }
  } // namespace

  void PlumePool::FUN_0021fa88_reset()
  {
    for (PlumePuff &puff : puffs_)
    {
      puff = PlumePuff{};
    }
    for (PlumeEmitter &emitter : emitters_)
    {
      emitter = PlumeEmitter{};
    }
    draws_.clear();
    puffCount_ = 0;
    emitterCount_ = 0;
    DAT_00354cc4_gate_ = false;
  }

  void PlumePool::FUN_0021f6e8_open_emitter(const PlumeEmitterSpawn &spawn)
  {
    // `movz a1, 1, a1` -- a zero life reads as one, so the shortest cycle is 32
    // ticks rather than a divide by zero in the ratio below.
    const std::int32_t units = spawn.lifeUnits == 0 ? 1 : static_cast<std::int32_t>(spawn.lifeUnits);
    const auto life = static_cast<std::int16_t>(units << 5);

    const bool oneShot = spawn.mode == 2;
    const std::int8_t flameFlag = oneShot ? 0 : spawn.mode;

    for (PlumeEmitter &emitter : emitters_)
    {
      if (emitter.alive())
      {
        continue;
      }
      emitter.oneShot39 = oneShot ? 1 : 0;
      emitter.cycles38 = static_cast<std::int8_t>(spawn.cycles);
      emitter.flameFlag24 = flameFlag;
      emitter.colour34 = spawn.colour;
      emitter.burstCount26 = spawn.burstCount;
      ++emitterCount_;
      emitter.total2c = life;
      emitter.riseSpeed18 = spawn.riseSpeed;
      emitter.x00 = spawn.x;
      emitter.y04 = spawn.y;
      emitter.z08 = spawn.z;
      emitter.size30 = spawn.size;
      // **Only FUN_0021F7A8 writes these two.** An emitter opened through
      // opcode 0x10E inherits whatever the slot's previous occupant left, which
      // is the original's own behaviour and is why they are not cleared here.
      if (spawn.writeDrift)
      {
        emitter.driftSpeed1c = spawn.driftSpeed;
        emitter.driftAngle20 = spawn.driftAngle;
      }
      DAT_00354cc4_gate_ = true;
      emitter.burstTimer28 = 0;
      emitter.remaining2a = life;
      emitter.spawnX0c = spawn.x;
      emitter.spawnY10 = spawn.y;
      emitter.spawnZ14 = spawn.z;
      return;
    }
  }

  void PlumePool::FUN_0021f870_shed_puff(float rise, float size, float x, float y, float z,
                                         std::int16_t life, std::uint32_t colour,
                                         std::uint8_t tile, std::int8_t clutBank,
                                         const std::function<std::uint32_t()> &random)
  {
    for (PlumePuff &puff : puffs_)
    {
      if (puff.alive())
      {
        continue;
      }
      puff.kind20 = 0;
      puff.elapsed14 = 0;
      puff.colour1c = colour;
      puff.clutBank22 = clutBank;
      puff.total16 = life;
      puff.rise0c = rise;
      puff.x00 = x;
      puff.y04 = y;
      puff.z08 = z;
      puff.size18 = size;
      puff.frame21 = tile;
      // The heading roll happens **after** every store, so it is the last word
      // the generator gives up for this record.
      puff.driftAngle10 =
          (static_cast<float>(rollModulo(random, 360u)) * kFGpffff8460_puffTurn) / kDegreesPerTurn;
      ++puffCount_;
      return;
    }
  }

  void PlumePool::FUN_0021f980_shed_flame(float size, float x, float y, float z,
                                          std::int16_t life, std::uint32_t colour,
                                          const std::function<std::uint32_t()> &random)
  {
    for (PlumePuff &puff : puffs_)
    {
      if (puff.alive())
      {
        continue;
      }
      puff.elapsed14 = 0;
      puff.frame21 = 0;
      puff.colour1c = colour;
      puff.kind20 = 1;
      puff.clutBank22 = kFlameClutBank;
      puff.total16 = life;
      // Passed as a literal zero by the only caller, and never read again
      // because FUN_00220210 skips the movement block for a flame.
      puff.rise0c = 0.0f;
      puff.x00 = x;
      puff.y04 = y;
      puff.z08 = z;
      puff.size18 = size;
      puff.driftAngle10 =
          (static_cast<float>(rollModulo(random, 360u)) * kFGpffff8464_flameTurn) / kDegreesPerTurn;
      ++puffCount_;
      return;
    }
  }

  void PlumePool::FUN_0021fb68_step_emitter(PlumeEmitter &emitter, std::uint32_t frameTicks,
                                            const std::function<std::uint32_t()> &random)
  {
    const auto ticks = static_cast<float>(static_cast<std::int32_t>(frameTicks));

    // The countdown is a halfword subtract stored back as a halfword, and the
    // test is on the sign of the result.
    const auto stepped = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(emitter.remaining2a) - static_cast<std::uint16_t>(frameTicks));
    emitter.remaining2a = stepped;

    if (stepped < 0)
    {
      // **0x63 never decrements**: the test is `< 0x63`, and 99 is 0x63, so an
      // emitter armed with 99 cycles restarts until the gate drops.
      if (emitter.cycles38 < kImmortalCycles)
      {
        emitter.cycles38 = static_cast<std::int8_t>(emitter.cycles38 - 1);
      }
      if (emitter.cycles38 > 0)
      {
        emitter.remaining2a = emitter.total2c;
        emitter.x00 = emitter.spawnX0c;
        emitter.y04 = emitter.spawnY10;
        emitter.z08 = emitter.spawnZ14;
        return;
      }
      emitter.cycles38 = -1;
      if (emitterCount_ - 1 > 0)
      {
        --emitterCount_;
      }
      else
      {
        emitterCount_ = 0;
      }
      return;
    }

    const float rise = emitter.riseSpeed18 * ticks * kTickScale;
    emitter.z08 += rise;
    if (emitter.driftSpeed1c > 0.0f)
    {
      const float step = emitter.driftSpeed1c * ticks * kTickScale;
      emitter.x00 += step * std::cos(emitter.driftAngle20);
      emitter.y04 += step * std::sin(emitter.driftAngle20);
    }

    // Remaining over total, so it starts at 1 and falls to 0.
    const float ratio = static_cast<float>(emitter.remaining2a) /
                        static_cast<float>(emitter.total2c);
    const float growth = emitter.oneShot39 != 0 ? 1.0f : (1.0f - ratio) + 1.0f;

    // The bank roll happens whether or not the flame flag is set, so the
    // generator advances the same either way.
    std::int8_t clutBank = (roll(random) & 1u) != 0 ? kOddClutBank : kEvenClutBank;
    if (emitter.flameFlag24 != 0)
    {
      if (kFGpffff8468_brightBankAbove < ratio)
      {
        clutBank = kBrightClutBank;
      }
      if (kFGpffff846c_flameAbove < ratio)
      {
        const Scatter at = scatter(random, kFGpffff8470_flareTurn, emitter.x00, emitter.y04);
        FUN_0021f980_shed_flame(emitter.size30, at.x, at.y, emitter.z08, emitter.total2c,
                                emitter.colour34, random);
      }
    }

    // A puff outlives the emitter's cycle by half again.
    const auto puffLife = static_cast<std::int16_t>(
        FUN_0030bd20_truncate(static_cast<float>(emitter.total2c) * kPuffLifeFactor));

    const auto burstStepped = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(emitter.burstTimer28) - static_cast<std::uint16_t>(frameTicks));
    emitter.burstTimer28 = static_cast<std::uint16_t>(burstStepped);
    if (burstStepped < 0)
    {
      for (std::int16_t shed = 0; shed < emitter.burstCount26; ++shed)
      {
        const Scatter at = scatter(random, kFGpffff8474_burstTurn, emitter.x00, emitter.y04);
        // The tile roll is the third word of the body, after the angle and the
        // radius.
        const auto tile = static_cast<std::uint8_t>(rollModulo(random, kSmokeTileCount));
        FUN_0021f870_shed_puff(emitter.riseSpeed18 * kFGpffff8478_puffRiseShare,
                               emitter.size30 * growth, at.x, at.y, emitter.z08, puffLife,
                               emitter.colour34, tile, clutBank, random);
      }
      emitter.burstTimer28 = kBurstPeriod;
    }

    // The one-shot's second job: zero the countdown so the *next* step takes
    // the expiry branch above.
    if (emitter.oneShot39 != 0)
    {
      emitter.remaining2a = 0;
    }
  }

  void PlumePool::FUN_00220210_step_puff(PlumePuff &puff, std::size_t index,
                                         std::uint32_t frameTicks)
  {
    const auto ticks = static_cast<float>(static_cast<std::int32_t>(frameTicks));

    const auto stepped = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(puff.elapsed14) + static_cast<std::uint16_t>(frameTicks));
    puff.elapsed14 = static_cast<std::uint16_t>(stepped);
    if (puff.total16 < stepped)
    {
      puff.kind20 = -1;
      if (puffCount_ - 1 > 0)
      {
        --puffCount_;
      }
      else
      {
        puffCount_ = 0;
      }
      return;
    }

    // Elapsed over total, so this one climbs from 0 to 1.
    const float ratio = static_cast<float>(stepped) / static_cast<float>(puff.total16);

    float alpha = 0.0f;
    if (puff.kind20 == 0)
    {
      // The rise drives the drift too: the same step is applied to z and, at
      // the record's heading, to x and y, so a smoke puff climbs at 45 degrees.
      const float step = puff.rise0c * ticks * kTickScale;
      puff.z08 += step;
      puff.x00 += step * std::cos(puff.driftAngle10);
      puff.y04 += step * std::sin(puff.driftAngle10);

      alpha = ratio <= 0.5f ? ratio * kAlphaSpan : kAlphaSpan - ratio * kAlphaSpan;
    }
    else
    {
      // A flame neither moves nor fades in; it only fades out.
      alpha = kAlphaSpan - ratio * kAlphaSpan;
    }

    draws_.push_back(PlumePuffDraw{index,
                                   orphen::ported::psm2::Vec3{puff.x00, puff.y04, puff.z08},
                                   puff.size18, puff.colour1c,
                                   static_cast<int>(FUN_0030bdb0_truncate(alpha))});
  }

  void PlumePool::FUN_00220028_step(std::uint32_t frameTicks,
                                    const std::function<std::uint32_t()> &random)
  {
    draws_.clear();
    if (!DAT_00354cc4_gate_)
    {
      return;
    }

    // Both walks cover the whole array; the counts only decide whether the walk
    // happens at all.
    if (emitterCount_ > 0)
    {
      for (PlumeEmitter &emitter : emitters_)
      {
        if (emitter.alive())
        {
          FUN_0021fb68_step_emitter(emitter, frameTicks, random);
        }
      }
    }

    if (puffCount_ > 0)
    {
      for (std::size_t index = 0; index < puffs_.size(); ++index)
      {
        if (puffs_[index].alive())
        {
          FUN_00220210_step_puff(puffs_[index], index, frameTicks);
        }
      }
    }

    if (emitterCount_ < 1 && puffCount_ < 1)
    {
      DAT_00354cc4_gate_ = false;
    }
  }

  PlumeTexels PlumePool::FUN_00220210_take_texels(std::size_t index)
  {
    if (index >= puffs_.size())
    {
      return PlumeTexels{};
    }
    PlumePuff &puff = puffs_[index];
    if (puff.kind20 == 0)
    {
      return kDAT_00315898_smokeTiles[puff.frame21 % kSmokeTileCount];
    }
    // The strip steps by one and wraps past five, and it does so here -- after
    // both rejects -- so a flame that was not drawn does not advance.
    const auto next = static_cast<std::uint8_t>(puff.frame21 + 1);
    puff.frame21 = next > (kFlameTileCount - 1) ? 0 : next;
    return kDAT_003158f8_flameTiles[puff.frame21 % kFlameTileCount];
  }

  std::int8_t PlumePool::clutBankOf(std::size_t index) const
  {
    return index < puffs_.size() ? puffs_[index].clutBank22 : 0;
  }

  orphen::ported::render::SpriteQuad FUN_00220210_build_plume_quad(const PlumeQuadInputs &inputs)
  {
    orphen::ported::render::SpriteQuad quad;

    const float viewZ = inputs.viewZ > orphen::ported::render::kDAT_0035209c_spriteNearClip
                            ? inputs.viewZ
                            : orphen::ported::render::kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / viewZ;

    // Sixteen units of +0x18, then the 400 the packet scales every corner by.
    // Half that in y, because the GS counts y in 1/8 of a line where it counts
    // x in 1/16 of a pixel -- so this is a square, not a wide rectangle.
    const float halfWidth = inputs.size * kCornerUnits * kCornerScale * q;
    const float halfHeight = halfWidth * 0.5f;

    // The original adds a truncated integer offset to an integer origin, so the
    // truncation happens on the offset and not on the sum.
    const int gsX0 = inputs.gsOriginX + FUN_0030bd20_truncate(-halfWidth);
    const int gsX1 = inputs.gsOriginX + FUN_0030bd20_truncate(halfWidth);
    const int gsY0 = inputs.gsOriginY + FUN_0030bd20_truncate(-halfHeight);
    const int gsY1 = inputs.gsOriginY + FUN_0030bd20_truncate(halfHeight);

    const float perX = inputs.projectionScaleX != 0.0f ? viewZ / inputs.projectionScaleX : 0.0f;
    const float perY = inputs.projectionScaleY != 0.0f ? viewZ / inputs.projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
    quad.viewZ = viewZ;

    quad.u0 = inputs.texels.u0;
    quad.v0 = inputs.texels.v0;
    quad.u1 = inputs.texels.u1;
    quad.v1 = inputs.texels.v1;

    // Three cases, in the original's order: no colour word at all takes the
    // sheet's own grey and the record's CLUT bank; a colour word with no alpha
    // byte replaces the grey and drops to bank 0; a colour word *with* an alpha
    // byte is used whole, keeps its own alpha and sends the packet out
    // additive.
    std::uint32_t colour = 0;
    const auto alphaByte = static_cast<std::uint32_t>(inputs.alpha) & 0xFFu;
    if (inputs.colour == 0)
    {
      colour = (alphaByte << 24) | kPlumeDefaultColour;
      quad.blendMode = kPlumeAlphaBlendMode;
      quad.clutBank = inputs.clutBank;
    }
    else if ((inputs.colour & 0xFF000000u) == 0)
    {
      colour = (alphaByte << 24) | inputs.colour;
      quad.blendMode = kPlumeAlphaBlendMode;
      quad.clutBank = 0;
    }
    else
    {
      colour = inputs.colour;
      quad.blendMode = kPlumeAdditiveBlendMode;
      quad.clutBank = 0;
    }

    // FUN_00207DE8:130-141. The packet's texture halfword is non-zero, so the
    // fold halves all four channels on the way past: 0xF0 reaches the GS as
    // 0.9375 of its x1.0, not as white.
    const auto folded = [](std::uint32_t component)
    { return static_cast<float>((component & 0xFEu) >> 1) / 128.0f; };
    quad.colour[0] = folded(colour & 0xFFu);
    quad.colour[1] = folded((colour >> 8) & 0xFFu);
    quad.colour[2] = folded((colour >> 16) & 0xFFu);
    quad.colour[3] = folded((colour >> 24) & 0xFFu);

    quad.textureSlot = kPlumeTextureSlot;
    quad.displayListBucket = kPlumeDisplayListBucket;
    quad.depthTest = true;
    return quad;
  }

} // namespace orphen::ported::entity
