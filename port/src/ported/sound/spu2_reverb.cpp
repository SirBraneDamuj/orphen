#include "ported/sound/spu2_reverb.h"

#include <algorithm>
#include <cstddef>

namespace orphen::ported::sound
{
  namespace
  {
    // Every coefficient in the preset bank is a signed Q15 fraction, so 0x8000
    // is exactly -1.0 and 0x7FFF is one LSB short of +1. Several presets ship
    // genuinely negative wall and comb coefficients, and both inCoef fields are
    // 0x8000 throughout -- the wet bus is inverted relative to the dry one on
    // real hardware too, and inverting it back here would be a divergence, not
    // a fix.
    constexpr float kQ15 = 1.0f / 32768.0f;

    inline float q15(std::int16_t coefficient)
    {
      return static_cast<float>(coefficient) * kQ15;
    }

    // Preset offsets are in eight-byte units and the buffer is a run of 16-bit
    // samples, so a unit is four samples -- for the buffer length and for every
    // tap into it alike.
    constexpr std::uint32_t kSamplesPerUnit = 4;
  } // namespace

  bool Spu2Reverb::setPreset(int type)
  {
    if (type < 0)
    {
      // sGpffffbab4's guard: the record declined to choose, so whatever the
      // last scene set stays.
      return false;
    }
    if (type >= static_cast<int>(kSpu2ReverbPresetCount))
    {
      // The driver rejects a mode of ten or more outright rather than clamping
      // it (`sltiu $v0, $s0, 0xa` at 0xF23C), and no record in the game asks
      // for one.
      return false;
    }
    if (type == presetIndex_)
    {
      return false;
    }
    presetIndex_ = type;

    const Spu2ReverbPreset &preset = kSpu2ReverbPresets[static_cast<std::size_t>(type)];
    buffer_.assign(static_cast<std::size_t>(preset.sizeUnits) * kSamplesPerUnit, 0.0f);
    cursor_ = 0;
    lastL_ = lastR_ = previousL_ = previousR_ = 0.0f;
    heldL_ = heldR_ = 0.0f;
    tockPhase_ = false;
    return true;
  }

  bool Spu2Reverb::setDepth(std::uint16_t depth)
  {
    const std::uint16_t masked = static_cast<std::uint16_t>(depth & 0xFFFEu);
    if (masked == depth_)
    {
      return false;
    }
    depth_ = masked;
    // FUN_00204ca8(10, depth << 8, depth << 8): the same Q15 effect volume in
    // both channels. A depth of 60 is 0x3C00 out of 0x7FFF, a little under half.
    effectVolume_ = static_cast<float>(static_cast<std::uint32_t>(masked) << 8) * kQ15;
    return true;
  }

  void Spu2Reverb::clear()
  {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    cursor_ = 0;
    lastL_ = lastR_ = previousL_ = previousR_ = 0.0f;
    heldL_ = heldR_ = 0.0f;
    tockPhase_ = false;
  }

  float Spu2Reverb::read(std::uint32_t offset) const
  {
    const auto length = static_cast<std::uint32_t>(buffer_.size());
    return buffer_[(cursor_ + offset) % length];
  }

  void Spu2Reverb::write(std::uint32_t offset, float value)
  {
    const auto length = static_cast<std::uint32_t>(buffer_.size());
    buffer_[(cursor_ + offset) % length] = value;
  }

  void Spu2Reverb::tick(float inL, float inR)
  {
    const Spu2ReverbPreset &p = kSpu2ReverbPresets[static_cast<std::size_t>(presetIndex_)];
    const auto length = static_cast<std::uint32_t>(buffer_.size());

    // Every address in the preset is scaled the same way the driver scales it
    // on the way to the hardware register.
    const auto tap = [length](std::uint16_t units) -> std::uint32_t {
      return (static_cast<std::uint32_t>(units) * kSamplesPerUnit) % length;
    };
    // "One sample earlier in the line", which is last tick's value at the same
    // address, because the buffer scrolls forward by a sample per tick.
    const auto before = [length](std::uint32_t at) -> std::uint32_t {
      return (at + length - 1) % length;
    };

    const float lin = inL * q15(p.inCoefL);
    const float rin = inR * q15(p.inCoefR);
    const float wall = q15(p.wallVol);
    const float iir = q15(p.iirVol);

    // Same-side reflection: each channel's comb line is driven by its own input
    // plus its own delayed tap.
    const std::uint32_t sameLDst = tap(p.sameLDst);
    const std::uint32_t sameRDst = tap(p.sameRDst);
    const float sameLPrev = read(before(sameLDst));
    const float sameRPrev = read(before(sameRDst));
    write(sameLDst, (lin + read(tap(p.sameLSrc)) * wall - sameLPrev) * iir + sameLPrev);
    write(sameRDst, (rin + read(tap(p.sameRSrc)) * wall - sameRPrev) * iir + sameRPrev);

    // Different-side reflection, and this is the crossing: the *left* line
    // feeds back off the *right* source tap. Straightening it out collapses the
    // stereo image, which is how you notice it has been got wrong.
    const std::uint32_t diffLDst = tap(p.diffLDst);
    const std::uint32_t diffRDst = tap(p.diffRDst);
    const float diffLPrev = read(before(diffLDst));
    const float diffRPrev = read(before(diffRDst));
    write(diffLDst, (lin + read(tap(p.diffRSrc)) * wall - diffLPrev) * iir + diffLPrev);
    write(diffRDst, (rin + read(tap(p.diffLSrc)) * wall - diffRPrev) * iir + diffRPrev);

    // Early echo: four taps per channel, mixed by their own coefficients.
    float outL = read(tap(p.comb1LSrc)) * q15(p.comb1Vol) +
                 read(tap(p.comb2LSrc)) * q15(p.comb2Vol) +
                 read(tap(p.comb3LSrc)) * q15(p.comb3Vol) +
                 read(tap(p.comb4LSrc)) * q15(p.comb4Vol);
    float outR = read(tap(p.comb1RSrc)) * q15(p.comb1Vol) +
                 read(tap(p.comb2RSrc)) * q15(p.comb2Vol) +
                 read(tap(p.comb3RSrc)) * q15(p.comb3Vol) +
                 read(tap(p.comb4RSrc)) * q15(p.comb4Vol);

    // Two all-pass sections in series. Each writes its own input back into the
    // line and reads the tap `size` units behind where it writes.
    const auto allPass = [&](float sample, std::uint16_t dstUnits, std::uint16_t sizeUnits,
                             float coefficient) -> float {
      const std::uint32_t dst = tap(dstUnits);
      const std::uint32_t src = (dst + length - tap(sizeUnits)) % length;
      const float delayed = read(src);
      const float stored = sample - delayed * coefficient;
      write(dst, stored);
      return stored * coefficient + delayed;
    };

    outL = allPass(outL, p.apf1LDst, p.apf1Size, q15(p.apf1Vol));
    outR = allPass(outR, p.apf1RDst, p.apf1Size, q15(p.apf1Vol));
    outL = allPass(outL, p.apf2LDst, p.apf2Size, q15(p.apf2Vol));
    outR = allPass(outR, p.apf2RDst, p.apf2Size, q15(p.apf2Vol));

    previousL_ = lastL_;
    previousR_ = lastR_;
    lastL_ = outL * effectVolume_;
    lastR_ = outR * effectVolume_;

    cursor_ = (cursor_ + 1) % length;
  }

  void Spu2Reverb::process(float inL, float inR, float &outL, float &outR)
  {
    if (!active() || buffer_.empty())
    {
      outL = 0.0f;
      outR = 0.0f;
      return;
    }

    // The tick consumes a sample pair, so feed it the mean of the two rather
    // than one of them: dropping the odd sample outright would fold everything
    // above 12 kHz back into the send. A two-tap average is not the hardware's
    // decimation filter, but it is the right shape and costs nothing.
    const float pairL = 0.5f * (heldL_ + inL);
    const float pairR = 0.5f * (heldR_ + inR);
    heldL_ = inL;
    heldR_ = inR;

    if (!tockPhase_)
    {
      tick(pairL, pairR);
      // Halfway between this tick and the one before it: the pair of output
      // samples a single tick covers.
      outL = 0.5f * (previousL_ + lastL_);
      outR = 0.5f * (previousR_ + lastR_);
    }
    else
    {
      outL = lastL_;
      outR = lastR_;
    }
    tockPhase_ = !tockPhase_;
  }

} // namespace orphen::ported::sound
