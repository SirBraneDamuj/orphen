#include "ported/sound/original_sound_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orphen::ported::sound
{
  namespace
  {
    constexpr std::size_t kCueRecordBytes = 8;

    // FUN_0030bd20: float to int, truncating toward zero.
    int FUN_0030bd20_toInt(float value)
    {
      return static_cast<int>(value);
    }

    // FUN_00216690.
    float wrapAngle(float radians)
    {
      constexpr float kTwoPi = 6.28318530718f;
      while (radians > 3.14159265359f)
      {
        radians -= kTwoPi;
      }
      while (radians < -3.14159265359f)
      {
        radians += kTwoPi;
      }
      return radians;
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }
  } // namespace

  bool SoundEngine::FUN_00228e28_load_cue_table(std::span<const std::uint8_t> resource199)
  {
    cues_.clear();
    if (resource199.size() < 0x2C)
    {
      diagnostic_ = "SCR resource 199 is too short to hold a cue table";
      return false;
    }

    // FUN_00228e28:85-88. The word at +0x28 is an offset into the resource; the
    // dword there is the table's byte length and the records follow it.
    const std::size_t directory = u32At(resource199, 0x28);
    if (directory + 4 > resource199.size())
    {
      diagnostic_ = "SCR resource 199's cue directory is out of range";
      return false;
    }
    const std::size_t bytes = u32At(resource199, directory);
    const std::size_t first = directory + 4;
    if (bytes == 0 || first + bytes > resource199.size())
    {
      diagnostic_ = "SCR resource 199's cue table runs past the resource";
      return false;
    }

    cues_.resize(bytes / kCueRecordBytes);
    for (std::size_t index = 0; index < cues_.size(); ++index)
    {
      const std::size_t at = first + index * kCueRecordBytes;
      SoundCue &cue = cues_[index];
      cue.bank = resource199[at + 0];
      cue.program = resource199[at + 1];
      cue.note = resource199[at + 2];
      cue.volume = resource199[at + 3];
      cue.cap = resource199[at + 5];
      cue.alternateBank = resource199[at + 7];
    }
    return true;
  }

  bool SoundEngine::FUN_00205310_load_bank(std::size_t bank, std::span<const std::uint8_t> resource)
  {
    if (bank >= kBankCount)
    {
      return false;
    }
    if (!banks_[bank].load(resource))
    {
      diagnostic_ = banks_[bank].diagnostic();
      return false;
    }
    return true;
  }

  void SoundEngine::FUN_00267d38_play_flat(std::uint16_t cue)
  {
    // FUN_00267d38's null-entity branch reaches FUN_002057c8 with both channels
    // wide open.
    FUN_002057c8_key_on(cue, 0x7F, 0x7F);
  }

  void SoundEngine::FUN_00267d38_play_at(std::uint16_t cue, float x, float y, float z)
  {
    // FUN_00267a80(x, y, z, cue, 100).
    constexpr int kScale = 100;

    const float toX = x - listener_.x;
    const float toY = y - listener_.y;
    const float toZ = z - listener_.z;

    const float distance = std::sqrt(toX * toX + toY * toY + toZ * toZ);
    if (distance >= kAudibleRange)
    {
      cueLog_.push_back({cue, frame_, 0, 0, 0, 0, 0, 0, 0, 0.0f, "out of range"});
      return;
    }

    const int level =
        (FUN_0030bd20_toInt((kAudibleRange - distance) * 128.0f / kAudibleRange) * kScale) / 100;

    const float flat = std::sqrt(toX * toX + toY * toY);
    int nearLevel = FUN_0030bd20_toInt((kNearField - flat) * 100.0f / kNearField);
    if (nearLevel < 0)
    {
      nearLevel = 0;
    }
    else if (level < nearLevel)
    {
      nearLevel = level;
    }

    int pan = 0;
    if (flat > fGpffff8da0_panDeadZone)
    {
      const float relative = wrapAngle(std::atan2(toY, toX) - listener_.yawRadians);
      pan = FUN_0030bd20_toInt((std::cos(relative + relative) - 1.0f) * static_cast<float>(kPanSwing));
      if (relative > 0.0f)
      {
        pan = -pan;
      }
    }

    const int left = ((pan + kPanCentre) * level) / 128;
    const int right = ((kPanCentre - pan) * level) / 128;

    int volumeLeft = nearLevel;
    if (nearLevel <= left)
    {
      volumeLeft = std::min(left, 0x7F);
    }
    int volumeRight = nearLevel;
    if (nearLevel <= right)
    {
      volumeRight = std::min(right, 0x7F);
    }

    FUN_002057c8_key_on(cue, volumeLeft, volumeRight);
  }

  void SoundEngine::FUN_002057c8_key_on(std::uint16_t cue, int volumeLeft, int volumeRight)
  {
    if (cue >= cues_.size())
    {
      cueLog_.push_back({cue, frame_, 0, 0, 0, 0, 0, 0, 0, 0.0f, "no such cue"});
      return;
    }
    const SoundCue &record = cues_[cue];

    if (record.alternateBank != 0)
    {
      // FUN_00205778's scene-streamed banks. Not ported.
      cueLog_.push_back({cue, frame_, record.alternateBank, record.program, record.note, 0, 0, 0, 0,
                         0.0f, "alternate bank not ported"});
      return;
    }

    // The scaling, with the compiler's truncate-toward-zero divide by 0x4000.
    const auto scale = [&](int channel) {
      int scaled = (channel * record.volume * DAT_003555d5_masterVolume) / 0x4000;
      if (record.cap != 0)
      {
        scaled = std::min<int>(scaled, record.cap);
      }
      return static_cast<std::uint8_t>(std::clamp(scaled, 0, 0x7F));
    };

    KeyOn request;
    request.cue = cue;
    request.bank = record.bank;
    request.program = record.program;
    request.note = record.note;
    request.volumeLeft = scale(volumeLeft);
    request.volumeRight = scale(volumeRight);

    const char *outcome = "played";
    std::int16_t waveform = 0;
    std::size_t samples = 0;
    float rate = 0.0f;
    if (record.bank >= kBankCount || !banks_[record.bank].valid())
    {
      outcome = "bank not loaded";
    }
    else if (const VabTone *tone = banks_[record.bank].tone(record.program, record.note);
             tone == nullptr)
    {
      outcome = "no tone for that note";
    }
    else
    {
      waveform = tone->waveform;
      rate = SoundBank::FUN_00205e50_sample_rate(*tone, record.note);
      if (const std::vector<std::int16_t> *pcm = banks_[record.bank].waveform(tone->waveform);
          pcm != nullptr)
      {
        samples = pcm->size();
      }
      const std::lock_guard<std::mutex> guard(mixLock_);
      pending_.push_back(request);
    }

    cueLog_.push_back({cue, frame_, request.bank, request.program, request.note, request.volumeLeft,
                       request.volumeRight, waveform, samples, rate, outcome});
  }

  void SoundEngine::startVoice(const KeyOn &request)
  {
    const SoundBank &bank = banks_[request.bank];
    const VabTone *tone = bank.tone(request.program, request.note);
    if (tone == nullptr)
    {
      return;
    }
    const std::vector<std::int16_t> *pcm = bank.waveform(tone->waveform);
    if (pcm == nullptr || pcm->empty())
    {
      return;
    }

    Voice *slot = nullptr;
    for (Voice &voice : voices_)
    {
      if (!voice.active)
      {
        slot = &voice;
        break;
      }
    }
    if (slot == nullptr)
    {
      return; // FUN_002057c8 drops the cue when no voice is free
    }

    // The IOP multiplies the tone's own volume and its program's back in.
    const float toneGain = static_cast<float>(tone->volume) / 127.0f;
    const float programGain =
        static_cast<float>(bank.program(request.program)->volume) / 127.0f;
    const float masterGain = static_cast<float>(bank.masterVolume()) / 127.0f;
    const float common = toneGain * programGain * masterGain / 127.0f;

    slot->pcm = pcm;
    slot->position = 0.0;
    slot->step = SoundBank::FUN_00205e50_sample_rate(*tone, request.note) / kSpuBaseSampleRate;
    slot->gainLeft = static_cast<float>(request.volumeLeft) * common;
    slot->gainRight = static_cast<float>(request.volumeRight) * common;
    slot->active = true;
  }

  void SoundEngine::drainPendingKeyOns()
  {
    std::vector<KeyOn> requests;
    {
      const std::lock_guard<std::mutex> guard(mixLock_);
      requests.swap(pending_);
    }
    for (const KeyOn &request : requests)
    {
      startVoice(request);
    }
  }

  void SoundEngine::mix(float *interleavedStereo, std::size_t frames)
  {
    std::memset(interleavedStereo, 0, frames * 2 * sizeof(float));
    drainPendingKeyOns();

    for (Voice &voice : voices_)
    {
      if (!voice.active)
      {
        continue;
      }
      const std::vector<std::int16_t> &pcm = *voice.pcm;
      for (std::size_t frame = 0; frame < frames; ++frame)
      {
        const auto index = static_cast<std::size_t>(voice.position);
        if (index + 1 >= pcm.size())
        {
          voice.active = false;
          break;
        }
        // Linear interpolation: the waveforms play well below the output rate,
        // so nearest-neighbour would alias badly.
        const float fraction = static_cast<float>(voice.position - static_cast<double>(index));
        const float sample = (static_cast<float>(pcm[index]) * (1.0f - fraction) +
                              static_cast<float>(pcm[index + 1]) * fraction) /
                             32768.0f;
        interleavedStereo[frame * 2] += sample * voice.gainLeft;
        interleavedStereo[frame * 2 + 1] += sample * voice.gainRight;
        voice.position += voice.step;
      }
    }

    for (std::size_t sample = 0; sample < frames * 2; ++sample)
    {
      interleavedStereo[sample] = std::clamp(interleavedStereo[sample], -1.0f, 1.0f);
    }
  }

} // namespace orphen::ported::sound
