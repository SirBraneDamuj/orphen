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
#include <mutex>
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

    // == The movie stream ==
    //
    // FUN_00207408 starts SPU2 core 0's AutoDMA input and FUN_002F2110 keeps
    // it fed from the MV3 PCM ring. On hardware that input is mixed in beside
    // the voices at BVOL, so here it is added on top of the engine's output
    // rather than replacing it -- the movie's music fade-out still plays under
    // the first second. 48 kHz interleaved stereo, the device's own rate.
    void queueMoviePcm(const std::vector<std::int16_t> &interleaved);
    // BVOL as a linear gain, 1.0 at 0x8000 (see movie::bvolGain);
    // FUN_00207580 moves it during a skip.
    void setMovieGain(float gain) { movieGain_.store(gain, std::memory_order_relaxed); }
    // Loudest movie sample mixed since the last stopMovie(), after the gain:
    // the one number that says whether a film was audible.
    float moviePeak() const { return moviePeak_.load(std::memory_order_relaxed); }
    // FUN_002074C8: stop the stream and drop whatever is still queued.
    void stopMovie();
    // Stereo frames of movie PCM played so far: the movie's audio clock.
    std::uint64_t movieFramesPlayed() const { return movieFramesPlayed_.load(std::memory_order_relaxed); }

    // For the callback.
    void mixMovie(float *interleavedStereo, std::size_t frames);

  private:
    std::uint32_t deviceId_ = 0;
    orphen::ported::sound::SoundEngine *engine_ = nullptr;
    // Read on SDL's audio thread.
    std::atomic<bool> muted_{false};

    std::mutex movieLock_;
    std::vector<std::int16_t> moviePcm_;
    std::size_t movieReadAt_ = 0;
    std::atomic<float> movieGain_{0.0f};
    std::atomic<std::uint64_t> movieFramesPlayed_{0};
    std::atomic<float> moviePeak_{0.0f};
  };

} // namespace orphen::harness
