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

#include <atomic>
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

    // Silences the output without stopping the device. Used by fast forward,
    // where the simulation runs tens of steps per real frame and every cue it
    // fires would key on at that rate against a mixer still running at 1x.
    // Pausing the device instead would stop the callback, and with it the
    // drainPendingKeyOns that keeps the queue from growing without bound.
    void setMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }

    // For the callback, which is handed the device rather than the engine.
    orphen::ported::sound::SoundEngine *engine() const { return engine_; }
    bool muted() const { return muted_.load(std::memory_order_relaxed); }

  private:
    std::uint32_t deviceId_ = 0;
    orphen::ported::sound::SoundEngine *engine_ = nullptr;
    // Read on SDL's audio thread.
    std::atomic<bool> muted_{false};
  };

} // namespace orphen::harness
