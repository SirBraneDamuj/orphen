#pragma once

// The indexed colour-ramp tracks, `DAT_00572078`: sixteen slots of 0x14 bytes.
//
//   src/FUN_0025d408.c  arm a track: two colours and a duration
//   src/FUN_0025d480.c  step it one frame; returns "finished"
//   src/FUN_0025d590.c  read one channel of the interpolated colour
//
// Reached from script opcodes 0x9A (arm), 0x9B (step) and 0x9C (read). Both
// 0x9B and 0x9C are *expression* opcodes -- their handlers `FUN_00261c38` and
// `FUN_00261c60` end in a tail position that leaves the callee's `v0` alone, so
// the value propagates even though Ghidra types both as `void`. That matters:
// the script branches on 0x9B and passes 0x9C straight into a colour operand.
//
// This is not the fullscreen fade (`ScreenFade`, `DAT_00571DC0`). Nothing here
// touches the screen. A track is a general s32-duration lerp between two packed
// colours that the script polls and applies wherever it likes -- in `s01_e012`
// two of them run permanently, one driving the scene's directional light
// through 0x97 and one driving a light slot through 0xC3, each re-armed in the
// opposite direction every time it reports finished. That ping-pong is the
// slow brightening and darkening over the whole opening.
//
// Channel order is positional, not semantic: `FUN_0025d408` packs its first
// operand into the low byte and `FUN_0025d590` indexes the same three bytes
// back out, so whatever the script put in first comes out at index 0.

#include <array>
#include <cstdint>

namespace orphen::ported::render
{

  class FadeTrackTable
  {
  public:
    // FUN_00261b80 reports ER_PARAM above this and then indexes anyway; the
    // port clamps instead, the same way the work-array reads do.
    static constexpr std::size_t kTrackCount = 16;
    static constexpr std::size_t kChannelCount = 3;

    // FUN_0025d408. `colourA`/`colourB` are packed as `(c2 << 16) | (c1 << 8) | c0`
    // by the caller; `durationFrames` is `FUN_00261b80`'s eighth operand.
    void FUN_0025d408_arm(std::size_t track,
                          std::uint32_t colourA,
                          std::uint32_t colourB,
                          std::int32_t durationFrames);

    // FUN_0025d480. Interpolates from the elapsed-tick counter, then advances it
    // by `frameTicks` (DAT_003555bc). Returns `duration * 32 < elapsed`, i.e.
    // the frame *after* the last one -- which is when the script re-arms.
    bool FUN_0025d480_step(std::size_t track, std::uint32_t frameTicks);

    // FUN_0025d590. One byte of the current colour; `channel` is 0..2.
    std::uint8_t FUN_0025d590_channel(std::size_t track, std::size_t channel) const;

    // Not an original entry point. The original's table is boot-cleared BSS and
    // no scene load touches it; the port zeroes it per scene so a reload cannot
    // inherit a half-finished ramp.
    void reset();

    // Reporting only.
    struct Track
    {
      std::int32_t elapsedTicks = 0;
      std::int32_t durationFrames = 0;
      std::array<std::uint8_t, kChannelCount> start{};
      std::array<std::uint8_t, kChannelCount> end{};
      std::array<std::uint8_t, kChannelCount> current{};
      bool armed = false;
      std::uint32_t arms = 0;
    };
    const Track &track(std::size_t index) const { return tracks_[index < kTrackCount ? index : 0]; }

  private:
    std::array<Track, kTrackCount> tracks_{};
  };

} // namespace orphen::ported::render
