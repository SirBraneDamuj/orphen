#include "ported/psm2/psm2_uv_animation.h"

namespace orphen::ported::psm2
{
  namespace
  {

    // FUN_00225940:55-70. The scroll modulus a negative duration selects. -1 is
    // 0x4000, which in 1/64 texel units is exactly one 256-texel page, so a
    // scrolling track walks the page once and starts over. Anything outside
    // -1..-5 falls into the same default the original uses, 0x4000.
    int scrollModulus(int duration)
    {
      switch (duration)
      {
      case -1: return 0x4000;
      case -2: return 0x2000;
      case -3: return 0x1000;
      case -4: return 0x800;
      case -5: return 0x400;
      default: return 0x4000;
      }
    }

    // The original's wrap, which is a single add or subtract rather than a
    // modulo: it assumes the step is smaller than the modulus, and so does this.
    std::int16_t wrapAccumulator(int value, int modulus)
    {
      if (value > modulus)
      {
        value -= modulus;
      }
      else if (value < 0)
      {
        value += modulus;
      }
      return static_cast<std::int16_t>(value);
    }

    // FUN_002257c0(record, track, value): write `value` into a *different*
    // track's flags byte. A track that finishes its repeats hands off to the
    // one its link names, which is how a strip can start another.
    void FUN_002257c0_set_track_flags(std::vector<UvAnimationTrack> &tracks,
                                      std::uint8_t track,
                                      std::uint8_t value)
    {
      if (track == 0 || track > tracks.size())
      {
        return;
      }
      // The original indexes `track * 10 + record + 1`, i.e. the flags byte of
      // the *previous* track -- link N addresses track N-1, the same 1-based
      // numbering material byte 9 uses.
      tracks[static_cast<std::size_t>(track) - 1].flags = value;
    }

  } // namespace

  void FUN_00225940_step_uv_animation(std::vector<UvAnimationTrack> &tracks,
                                      std::uint32_t frameTicks)
  {
    for (std::size_t index = 0; index < tracks.size(); ++index)
    {
      UvAnimationTrack &track = tracks[index];

      // FUN_00225940:26. An empty track and a cleared flags byte are both
      // skipped -- and skipped *before* the link copy at the bottom, so a
      // stopped track stops feeding whatever it was linked to as well.
      if (track.frames.empty() || track.flags == 0)
      {
        continue;
      }

      if (track.timer >= 1)
      {
        track.timer = static_cast<std::int16_t>(track.timer - static_cast<int>(frameTicks));
      }
      else
      {
        // `(int8)(frameIndex + 1)`. The seed is 0xFF, so the first step lands
        // on frame 0 without a special case.
        int next = static_cast<std::int8_t>(static_cast<std::uint8_t>(track.frameIndex + 1));
        bool wrapped = false;
        if (static_cast<int>(track.frames.size()) <= next)
        {
          next = 0;
          wrapped = true;
          // FUN_00225940:31-45. The repeat counter only runs while a link is
          // set, and only the transition to zero retires the track.
          if (track.link != 0 && track.repeat != 0)
          {
            track.repeat = static_cast<std::uint8_t>(track.repeat - 1);
            if (track.repeat == 0)
            {
              if ((track.flags & 4) == 0)
              {
                FUN_002257c0_set_track_flags(tracks, track.link, 1);
              }
              if ((track.flags & 2) == 0)
              {
                track.flags = 0;
              }
              track.link = 0;
            }
          }
        }
        track.frameIndex = static_cast<std::uint8_t>(next);

        const UvAnimationFrame &frame = track.frames[static_cast<std::size_t>(next)];
        int hold = frame.duration;
        if (frame.duration < 0)
        {
          // Scroll. The accumulators take the frame's deltas every frame --
          // the original re-arms the timer with a hold of 1, which at 32 ticks
          // a frame is exactly one frame.
          const int modulus = scrollModulus(frame.duration);
          track.u = wrapAccumulator(static_cast<int>(track.u) + frame.u, modulus);
          track.v = wrapAccumulator(static_cast<int>(track.v) + frame.v, modulus);
          hold = 1;
        }
        else
        {
          // Keyframe. The stored u/v are texels; the accumulator is 1/64 texel.
          track.u = static_cast<std::int16_t>(static_cast<int>(frame.u) << 6);
          track.v = static_cast<std::int16_t>(static_cast<int>(frame.v) << 6);
        }
        (void)wrapped;

        // FUN_00225940:88-92. The add is 16-bit and the result is clamped up to
        // zero rather than down, so an overshoot on a long-overdue track does
        // not push the next step into the past.
        const std::int16_t advanced =
            static_cast<std::int16_t>(static_cast<int>(track.timer) + hold * 0x20);
        track.timer = advanced < 0 ? static_cast<std::int16_t>(0) : advanced;
      }

      // FUN_00225940:96-101. A track can mirror its offset into another one, so
      // two draws share a scroll without the script repeating itself. Same
      // 1-based numbering as the link above.
      if (track.link != 0 && track.link <= tracks.size())
      {
        UvAnimationTrack &target = tracks[static_cast<std::size_t>(track.link) - 1];
        target.u = track.u;
        target.v = track.v;
      }
    }
  }

  UvOffset uvOffsetForMaterialByte9(const std::vector<UvAnimationTrack> &tracks, std::uint8_t byte9)
  {
    if (byte9 == 0 || byte9 > tracks.size())
    {
      return {};
    }
    const UvAnimationTrack &track = tracks[static_cast<std::size_t>(byte9) - 1];
    // 1/64 texel to texels, then texels to the 0..1 the port's UVs use.
    constexpr float kToNormalised = 1.0f / (64.0f * 256.0f);
    return {static_cast<float>(track.u) * kToNormalised,
            static_cast<float>(track.v) * kToNormalised};
  }

} // namespace orphen::ported::psm2
