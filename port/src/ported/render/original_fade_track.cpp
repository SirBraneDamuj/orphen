#include "ported/render/original_fade_track.h"

namespace orphen::ported::render
{
  namespace
  {
    // FUN_0025d408 seeds the tick counter at 0x20 rather than 0, so the very
    // first FUN_0025d480 already interpolates one frame in. A track armed for
    // `n` frames therefore never emits its own start colour -- the colour it
    // replaced was on screen for that frame instead.
    constexpr std::int32_t kInitialElapsedTicks = 0x20;

    std::size_t clampTrack(std::size_t track)
    {
      return track < FadeTrackTable::kTrackCount ? track : 0;
    }
  } // namespace

  void FadeTrackTable::FUN_0025d408_arm(std::size_t track,
                                        std::uint32_t colourA,
                                        std::uint32_t colourB,
                                        std::int32_t durationFrames)
  {
    Track &slot = tracks_[clampTrack(track)];

    slot.elapsedTicks = kInitialElapsedTicks;
    slot.durationFrames = durationFrames;

    slot.start[0] = static_cast<std::uint8_t>(colourA);
    slot.start[1] = static_cast<std::uint8_t>(colourA >> 8);
    slot.start[2] = static_cast<std::uint8_t>(colourA >> 16);
    slot.end[0] = static_cast<std::uint8_t>(colourB);
    slot.end[1] = static_cast<std::uint8_t>(colourB >> 8);
    slot.end[2] = static_cast<std::uint8_t>(colourB >> 16);

    // The original publishes the start colour immediately, so a script that
    // arms and reads in the same frame sees the new ramp rather than the tail
    // of the previous one.
    slot.current = slot.start;

    slot.armed = true;
    ++slot.arms;
  }

  bool FadeTrackTable::FUN_0025d480_step(std::size_t track, std::uint32_t frameTicks)
  {
    Track &slot = tracks_[clampTrack(track)];

    // `iVar5 = iVar1 + 0x1f; if (-1 < iVar1) iVar5 = iVar1; iVar5 >> 5` is the
    // compiler's signed divide by 32 -- it truncates toward zero rather than
    // flooring, which only differs for a negative counter.
    const std::int32_t elapsed = slot.elapsedTicks;
    const std::int32_t elapsedFrames = elapsed / 32;

    // FUN_0025d480 reports ER_PARAM on a zero duration and then divides anyway,
    // which traps. Nothing in either scene arms a zero-length track; the port
    // leaves the colour alone rather than dividing.
    const std::int32_t duration = slot.durationFrames;
    if (duration == 0)
    {
      return false;
    }

    const std::int32_t remainingFrames = duration - elapsedFrames;
    for (std::size_t channel = 0; channel < kChannelCount; ++channel)
    {
      // Both weights are applied to *unsigned* bytes and the sum is divided as
      // a signed int, truncating toward zero. Past the end of the ramp
      // `remainingFrames` goes negative and the result runs past the end
      // colour; the original does not clamp and neither does this, because the
      // scripts re-arm on the frame that reports finished.
      const std::int32_t weighted =
          static_cast<std::int32_t>(slot.start[channel]) * remainingFrames +
          static_cast<std::int32_t>(slot.end[channel]) * elapsedFrames;
      slot.current[channel] = static_cast<std::uint8_t>(weighted / duration);
    }

    slot.elapsedTicks = elapsed + static_cast<std::int32_t>(frameTicks);
    return (duration * 32) < slot.elapsedTicks;
  }

  std::uint8_t FadeTrackTable::FUN_0025d590_channel(std::size_t track, std::size_t channel) const
  {
    // The original indexes `(&DAT_00572086)[channel + track * 0x14]` with no
    // bound on `channel` beyond opcode 0x9C's own `> 2` diagnostic, so an
    // out-of-range channel reads into the next slot. The port refuses instead;
    // nothing in either scene passes one.
    if (channel >= kChannelCount)
    {
      return 0;
    }
    return tracks_[clampTrack(track)].current[channel];
  }

  void FadeTrackTable::reset()
  {
    tracks_ = {};
  }

} // namespace orphen::ported::render
