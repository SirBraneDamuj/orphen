#pragma once

// The numbered-movie session: which file, how loud, what it marks as seen, and
// the Start-button skip.
//
//   src/FUN_002f1808.c  pick the file, set the per-movie flag, decide whether
//                       the movie may be skipped, and walk the chain
//   src/FUN_002f2198.c  the per-picture loop, whose Start test and fade-out
//                       are FUN_002F2198_skip below
//   src/FUN_002f2758.c  the fade quad: black, alpha = counter / 2
//   src/FUN_00207580.c  the stream volume, straight into SPU2 core 0 BVOL
//
// == The skip ==
//
// FUN_002F2198 reads the pad once per decoded picture. The first picture whose
// newly-pressed word has Start (DAT_003555F6 & 0x800) arms DAT_00355E36 at 1,
// unless DAT_00355E35 -- FUN_002F1808's "this one cannot be skipped" byte --
// is set. Every picture after that:
//
//   - counter > 0xFE ends the movie, before the picture is shown;
//   - otherwise the picture gets a black quad at alpha counter / 2 (0x80 is
//     opaque), the volume drops by 1 + volume / 8, and the counter gains 0x20.
//
// So a skip is eight pictures -- about a quarter of a second -- fading from
// alpha 0 to 0x70 while the sound ramps to silence, and then the movie stops.
//
// == Which movies cannot be skipped ==
//
// FUN_002F1808:59-69. Only when the debug-active byte DAT_003555DA is clear:
// movies 10 and 15 never, and movie 0x12 -- the opening -- the first time it
// plays after power-on, which DAT_0035558C remembers. The lock is decided once
// per request, before the chain, so the second film of a two-film request
// inherits it.
//
// The IOP writes the volume word to SPU2 core 0's BVOL register (0x1F90076C,
// RSPU2DRV.IRX's handler for command 0x1023), a signed 16-bit gain on the
// streamed input. So 0x5000 is 0.625 of full scale.

#include <array>
#include <cstdint>
#include <string>

namespace orphen::ported::movie
{

  inline constexpr int kMovieCount = 19;

  // FUN_002F1808's table at 0x00326F80, `\MV3\M01.MV3;1` .. `M19.MV3`, indexed
  // by id - 1.
  inline std::string movieFileName(int movieId)
  {
    std::string name = "M00.MV3";
    name[1] = static_cast<char>('0' + movieId / 10);
    name[2] = static_cast<char>('0' + movieId % 10);
    return name;
  }

  // DAT_00326FD0: the BVOL each movie plays at.
  inline constexpr std::array<std::int16_t, kMovieCount> DAT_00326fd0_movieVolume{
      0x5000, 0x5000, 0x5000, 0x5000, 0x5000, 0x5000, 0x5000, 0x5000, 0x5000, 0x4000,
      0x5000, 0x5000, 0x4000, 0x5000, 0x3800, 0x4000, 0x4000, 0x5000, 0x4000};

  // DAT_00326FF8: the event flag each movie sets before it plays. All in the
  // SFLG bank (1280 and up), so they survive a map change. 18 and 19 share
  // 14's, 0x7BC.
  inline constexpr std::array<std::uint16_t, kMovieCount> DAT_00326ff8_movieFlag{
      0x7C8, 0x7BD, 0x7C9, 0x7BF, 0x7C0, 0x7C1, 0x7C2, 0x7C3, 0x7C4, 0x7C5,
      0x7C6, 0x7C7, 0x7CB, 0x7BC, 0x7BE, 0x7CA, 0x7CC, 0x7BC, 0x7BC};

  inline constexpr int kOpeningMovie = 0x12;

  // FUN_002F1808:59-69, the value it leaves in DAT_00355E35. Also sets
  // DAT_0035558C the first time the opening is asked for.
  inline bool FUN_002f1808_skip_locked(int movieId, bool DAT_003555da_debugActive,
                                       bool &DAT_0035558c_openingShown)
  {
    if (DAT_003555da_debugActive)
    {
      return false;
    }
    bool locked = false;
    if (movieId == kOpeningMovie && !DAT_0035558c_openingShown)
    {
      locked = true;
      DAT_0035558c_openingShown = true;
    }
    if (movieId == 10 || movieId == 15)
    {
      locked = true;
    }
    return locked;
  }

  // Raw pad bit FUN_002F2198 tests, DAT_003555F6 & 0x800.
  inline constexpr std::uint16_t kRawPadStart = 0x0800;

  // FUN_002F2198's skip state, one per movie: FUN_002F1F38 clears DAT_00355E36
  // and DAT_00355E38 on the way in.
  class MovieSkipFade
  {
  public:
    MovieSkipFade(std::int16_t volume, bool locked)
        : DAT_00355e3c_volume_(volume), DAT_00355e35_locked_(locked)
    {
    }

    struct Picture
    {
      // The movie ends here, and this picture is never shown.
      bool endMovie = false;
      // FUN_002F2758: a black quad over the picture, alpha on the GS's
      // 0x80-is-opaque scale.
      bool drawFade = false;
      std::uint8_t fadeAlpha = 0;
    };

    // One pass of FUN_002F2198's loop, after the picture is decoded.
    Picture FUN_002f2198_skip(std::uint16_t pressedPad)
    {
      Picture picture;
      if (DAT_00355e36_counter_ == 0)
      {
        if ((pressedPad & kRawPadStart) != 0 && !DAT_00355e35_locked_)
        {
          DAT_00355e36_counter_ = 1;
        }
        return picture;
      }
      if (DAT_00355e36_counter_ > 0xFE)
      {
        picture.endMovie = true;
        return picture;
      }
      picture.drawFade = true;
      picture.fadeAlpha = static_cast<std::uint8_t>(DAT_00355e36_counter_ / 2);
      // `(v + 7) >> 3` for a negative volume: division toward zero.
      DAT_00355e38_drop_ += 1 + DAT_00355e3c_volume_ / 8;
      DAT_00355e36_counter_ = static_cast<std::int16_t>(DAT_00355e36_counter_ + 0x20);
      return picture;
    }

    // FUN_00207580's argument, clamped the way it clamps it.
    int volume() const
    {
      const int volume = DAT_00355e3c_volume_ - DAT_00355e38_drop_;
      return volume < 0 ? 0 : volume;
    }
    bool locked() const { return DAT_00355e35_locked_; }
    bool skipping() const { return DAT_00355e36_counter_ != 0; }

  private:
    std::int16_t DAT_00355e3c_volume_ = 0;
    bool DAT_00355e35_locked_ = false;
    std::int16_t DAT_00355e36_counter_ = 0;
    int DAT_00355e38_drop_ = 0;
  };

  // SPU2 BVOL as a linear gain.
  inline float bvolGain(int volume) { return static_cast<float>(volume) / 32768.0f; }

  // FUN_00206840:12-17: the music fade FUN_00206680 started gets 60 vblanks
  // (DAT_003555AC, counted by FUN_00203A08) before the slots are replaced.
  inline constexpr int kMusicFadeHoldVblanks = 60;

} // namespace orphen::ported::movie
