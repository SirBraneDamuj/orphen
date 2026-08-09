#pragma once

// SDL2 audio output for the ported sound engine.
//
// This has no original counterpart: the PS2's EE never mixed anything. It sent
// a key-on to the IOP over SIF and the IOP's driver drove SPU2. The port keeps
// the split -- SoundEngine ends where FUN_00204d88 does, and this is the
// stand-in for everything past it.
//
// The callback runs on SDL's audio thread and calls SoundEngine::mix, which
// takes the pending queue under its own lock. Nothing in that path touches
// simulation state, so opening a device cannot change what `--frames` reports.

#include "ported/sound/original_sound_engine.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace orphen::harness
{

  // --sound-dump. Renders the mixer to a 16-bit stereo WAV so a headless run
  // can be listened to, and so the mixer is checkable without a speaker.
  bool writeStereoWav(const std::filesystem::path &path,
                      const std::vector<float> &interleaved,
                      int sampleRate);

  class AudioDevice
  {
  public:
    ~AudioDevice();

    // Opens a 48 kHz stereo float device. Returns false and leaves the engine
    // silent when SDL has no audio -- which is not fatal and not reported as an
    // error, because a headless run deliberately never calls this.
    bool open(orphen::ported::sound::SoundEngine *engine);
    void close();
    bool isOpen() const { return deviceId_ != 0; }

  private:
    std::uint32_t deviceId_ = 0;
    orphen::ported::sound::SoundEngine *engine_ = nullptr;
  };

} // namespace orphen::harness
