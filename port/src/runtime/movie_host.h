#pragma once

// What PortRuntime hands whoever can put a movie on screen.
//
// FUN_002F1808 is a blocking session: the scene loader calls it between the
// fade-out and the load, and it runs its own decode/present loop until the
// film ends. The runtime keeps the parts of it that change game state -- the
// flag, the chain, the music -- and hands the picture and sound to a host that
// main() installs. A headless run installs none, and the runtime logs the
// movie instead; either way the simulation sees the same thing.

#include <cstdint>
#include <filesystem>
#include <functional>

namespace orphen::port
{

  struct MovieRequest
  {
    int movieId = 0;
    std::filesystem::path path;
    // DAT_00326FD0[id - 1], the SPU2 BVOL it plays at.
    std::int16_t volume = 0;
    // DAT_00355E35: Start does nothing.
    bool skipLocked = false;
    // FUN_00206840's wait: vblanks still owed to the music fade FUN_00206680
    // started before the host may start the movie's own sound.
    int musicFadeHoldVblanks = 0;
    // FUN_00206840 proper, called by the host once that wait is over. It is
    // what finally replaces the scene's music slots.
    std::function<void()> FUN_00206840_replace_music;
  };

  enum class MovieOutcome
  {
    Played,
    Skipped,
    // The file or the decoder was missing; nothing was shown.
    Unavailable,
    // The window was closed mid-film.
    QuitRequested,
  };

  using MovieHost = std::function<MovieOutcome(const MovieRequest &)>;

} // namespace orphen::port
