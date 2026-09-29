#include "harness/audio_device.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace orphen::harness
{

  bool writeStereoWav(const std::filesystem::path &path,
                      const std::vector<float> &interleaved,
                      int sampleRate)
  {
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
      return false;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(interleaved.size() * 2);
    const auto put32 = [&](std::uint32_t value) { file.write(reinterpret_cast<const char *>(&value), 4); };
    const auto put16 = [&](std::uint16_t value) { file.write(reinterpret_cast<const char *>(&value), 2); };

    file.write("RIFF", 4);
    put32(36 + dataBytes);
    file.write("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(2);
    put32(static_cast<std::uint32_t>(sampleRate));
    put32(static_cast<std::uint32_t>(sampleRate) * 4);
    put16(4);
    put16(16);
    file.write("data", 4);
    put32(dataBytes);
    for (const float sample : interleaved)
    {
      const float clamped = std::clamp(sample, -1.0f, 1.0f);
      put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped * 32767.0f)));
    }
    return true;
  }
  namespace
  {
    void SDLCALL audioCallback(void *userData, Uint8 *stream, int lengthBytes)
    {
      auto *device = static_cast<AudioDevice *>(userData);
      auto *samples = reinterpret_cast<float *>(stream);
      const std::size_t frames = static_cast<std::size_t>(lengthBytes) / (2 * sizeof(float));
      auto *engine = device != nullptr ? device->engine() : nullptr;
      if (engine == nullptr)
      {
        std::memset(stream, 0, static_cast<std::size_t>(lengthBytes));
        return;
      }
      // Mix either way: mix() is what drains the pending key-on queue, so
      // muting by skipping it would let the queue grow while fast forwarding.
      engine->mix(samples, frames);
      if (device->muted())
      {
        std::memset(stream, 0, static_cast<std::size_t>(lengthBytes));
      }
      device->mixMovie(samples, frames);
    }
  } // namespace

  void AudioDevice::queueMoviePcm(const std::vector<std::int16_t> &interleaved)
  {
    const std::lock_guard<std::mutex> guard(movieLock_);
    // Compact once the played part dominates, so a two-minute film does not
    // keep every sample it has already played.
    if (movieReadAt_ > 0 && movieReadAt_ * 2 > moviePcm_.size())
    {
      moviePcm_.erase(moviePcm_.begin(), moviePcm_.begin() + static_cast<std::ptrdiff_t>(movieReadAt_));
      movieReadAt_ = 0;
    }
    moviePcm_.insert(moviePcm_.end(), interleaved.begin(), interleaved.end());
  }

  void AudioDevice::stopMovie()
  {
    const std::lock_guard<std::mutex> guard(movieLock_);
    moviePcm_.clear();
    movieReadAt_ = 0;
    movieGain_.store(0.0f, std::memory_order_relaxed);
    movieFramesPlayed_.store(0, std::memory_order_relaxed);
    moviePeak_.store(0.0f, std::memory_order_relaxed);
  }

  void AudioDevice::mixMovie(float *interleavedStereo, std::size_t frames)
  {
    const std::lock_guard<std::mutex> guard(movieLock_);
    const std::size_t available = (moviePcm_.size() - movieReadAt_) / 2;
    const std::size_t count = std::min(frames, available);
    if (count == 0)
    {
      return;
    }
    // int16 to the -1..1 the engine mixes in, then BVOL on top.
    const float gain = movieGain_.load(std::memory_order_relaxed) / 32768.0f;
    const std::int16_t *read = moviePcm_.data() + movieReadAt_;
    float peak = moviePeak_.load(std::memory_order_relaxed);
    for (std::size_t sample = 0; sample < count * 2; ++sample)
    {
      const float value = static_cast<float>(read[sample]) * gain;
      interleavedStereo[sample] += value;
      peak = std::max(peak, std::abs(value));
    }
    moviePeak_.store(peak, std::memory_order_relaxed);
    movieReadAt_ += count * 2;
    movieFramesPlayed_.fetch_add(count, std::memory_order_relaxed);
  }

  AudioDevice::~AudioDevice()
  {
    close();
  }

  bool AudioDevice::open(orphen::ported::sound::SoundEngine *engine)
  {
    close();
    if (engine == nullptr)
    {
      return false;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
      return false;
    }

    SDL_AudioSpec want{};
    want.freq = static_cast<int>(orphen::ported::sound::kSpuBaseSampleRate);
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    // ~21 ms. Short enough that a cue lands within a frame or two of the tick
    // that asked for it, long enough not to underrun behind a slow frame.
    want.samples = 1024;
    want.callback = audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device == 0)
    {
      return false;
    }

    deviceId_ = device;
    engine_ = engine;
    SDL_PauseAudioDevice(device, 0);
    return true;
  }

  void AudioDevice::close()
  {
    if (deviceId_ != 0)
    {
      SDL_CloseAudioDevice(deviceId_);
      deviceId_ = 0;
    }
    engine_ = nullptr;
  }

} // namespace orphen::harness
