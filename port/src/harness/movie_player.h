#pragma once

// The picture-and-sound half of FUN_002F2198: decode, present at the film's
// own rate, read the pad once per picture, and fade out on Start.
//
//   src/FUN_002f2198.c  the loop
//   src/FUN_002f2820.c  pacing: picture n goes up on vblank 2n, and a late
//                       picture resets the clock instead of being dropped
//
// == What is on screen ==
//
// FUN_002F2198's display environment is 640x448 at DBX 40, DBY 16 of a
// 720x480 buffer, and each picture is drawn at x = (720 - 640) / 2, y = 0. So
// the TV shows picture rows 16..463 -- sixteen lines are lost top and bottom --
// and that 640x448 fills the same 4:3 box the game's own frame does. (The
// DAT_003555DE branch is the widescreen option, which the port does not have.)
//
// Not ported: the double-buffered GIF chains, the IPU DMA bookkeeping and the
// interrupt handler at 0x002F27B0. Pacing is by the wall clock, the same
// 1001/30000 s per picture the vblank count gives.

#include "harness/audio_device.h"
#include "platform/sdl_gl_window.h"
#include "runtime/movie_host.h"

namespace orphen::harness
{

  class MoviePlayer
  {
  public:
    MoviePlayer(orphen::port::SdlGlWindow &window, AudioDevice &audio);
    ~MoviePlayer();

    // Blocks until the film ends, is skipped, or the window is closed.
    orphen::port::MovieOutcome play(const orphen::port::MovieRequest &request);

  private:
    void present(bool havePicture, bool drawFade, std::uint8_t fadeAlpha);

    orphen::port::SdlGlWindow &window_;
    AudioDevice &audio_;
    unsigned int texture_ = 0;
  };

} // namespace orphen::harness
