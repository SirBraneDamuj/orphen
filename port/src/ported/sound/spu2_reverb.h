#pragma once

// The SPU2 reverb, the last stage of the music path the port was missing.
//
//   src/FUN_00205938.c:90-113  the only place a reverb is ever selected
//   scripts/extract_spu2_reverb_presets.py  where the coefficients come from
//
// == How a scene asks for reverb ==
//
// A music record is eight bytes; `+4` is a reverb *type* and `+6` a reverb
// *depth*. FUN_00205938 forwards the type to the IOP as command 0x7314 with
// `type | 0x100` and the depth as `FUN_00204ca8(10, depth << 8, depth << 8)`,
// one effect volume per channel. Both are cached in sGpffffbab4/sGpffffbab6, so
// a slot asking for what is already set sends nothing, and a `+4` of -1 skips
// the block outright and leaves whatever the last scene chose. `setPreset` and
// `setDepth` keep both halves of that behaviour.
//
// The type indexes a ten-entry table that is *not* in the EE image -- it lives
// in the game's IOP sound driver, RSPU2DRV.IRX, at .data vaddr 0x163C0. The
// modes are the stock Sony set: 0 off, 1 room, 2-4 studio small/medium/large,
// 5 hall, 6 space echo, 7 echo, 8 delay, 9 pipe. Across all 285 music records
// the game only ever asks for 4 (x71), 3 (x4), 5 (x2), 2 (x1) and 0 (x2); the
// other 205 pass -1.
//
// == Which voices are wet ==
//
// Routing is per voice, not per slot: a VagAtr's `mode` byte at +1 is 4 for a
// tone that goes through the effect bus and 0 for one that does not. Every tone
// in the game is one or the other -- no bank uses any other value -- and the
// three boot banks are 400 tones of solid 0, which is why sound effects have
// never wanted this. SND resource 170, the piece under s14_e031, is 11 wet
// tones and one dry.
//
// == What this is, and is not ==
//
// The topology below is the documented SPU reverb: two cross-coupled IIR comb
// lines feeding a four-tap early-echo comb, then two all-pass sections, all of
// them reading and writing one delay buffer that scrolls a sample per tick.
// The register names are the ones the driver writes, so `sameLDst` here is the
// same address the hardware's SAME_L_DST holds.
//
// Two deliberate departures, both audible only under a null test:
//
//   * it runs in float, where the hardware is 16-bit fixed point with a
//     saturating multiply. The rest of this mixer is float for the same reason.
//   * the hardware ticks the reverb once per two output samples. So does this,
//     but the decimation either side of that tick is a two-tap mean going in
//     and a linear interpolation coming out, where the hardware runs a FIR.

#include "ported/sound/spu2_reverb_presets.h"

#include <cstdint>
#include <vector>

namespace orphen::ported::sound
{

  class Spu2Reverb
  {
  public:
    // FUN_00205938:92-103. A type below zero means "leave the reverb alone";
    // anything else selects a preset, and reselecting the current one is free.
    // Returns true when the setting actually changed.
    bool setPreset(int type);
    // FUN_00205938:108-112, the `+6 & 0xFFFE` the record carries. Bit 0 is a
    // flag rather than part of the depth, and the driver sends `depth << 8` as
    // a Q15 effect volume on both channels.
    bool setDepth(std::uint16_t depth);

    // Off, or on with nothing to return.
    bool active() const { return presetIndex_ > 0 && effectVolume_ != 0.0f; }
    int preset() const { return presetIndex_; }
    std::uint16_t depth() const { return depth_; }

    // Clears the delay buffer without disturbing the selection. The driver does
    // this too, on the 0x100 bit the EE always sets.
    void clear();

    // One output frame in, one wet frame out, at kSpuBaseSampleRate. `outL` and
    // `outR` already carry the effect volume, so the caller adds them straight
    // into the mix.
    void process(float inL, float inR, float &outL, float &outR);

  private:
    void tick(float inL, float inR);
    float read(std::uint32_t offset) const;
    void write(std::uint32_t offset, float value);

    int presetIndex_ = 0;
    std::uint16_t depth_ = 0;
    float effectVolume_ = 0.0f;

    std::vector<float> buffer_;
    std::uint32_t cursor_ = 0;

    // The two most recent tick outputs, which the odd sample interpolates
    // between.
    float lastL_ = 0.0f;
    float lastR_ = 0.0f;
    float previousL_ = 0.0f;
    float previousR_ = 0.0f;
    // The previous input sample, so a tick sees the mean of the pair it covers
    // rather than one of the two.
    float heldL_ = 0.0f;
    float heldR_ = 0.0f;
    bool tockPhase_ = false;
  };

} // namespace orphen::ported::sound
