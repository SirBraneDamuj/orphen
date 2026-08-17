#include "ported/sound/original_sound_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orphen::ported::sound
{
  namespace
  {
    constexpr std::size_t kCueRecordBytes = 8;
    constexpr std::size_t kMusicRecordBytes = 8;

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

  bool SoundEngine::loadMusicTables(std::span<const std::uint8_t> resource199)
  {
    for (std::vector<MusicRecord> &table : musicTables_)
    {
      table.clear();
    }
    if (resource199.size() < 0x20)
    {
      diagnostic_ = "SCR resource 199 is too short to hold the music tables";
      return false;
    }

    // FUN_00228e28:92. Header word 7 points at three offsets, one per category.
    const std::size_t directory = u32At(resource199, 0x1C);
    if (directory + kMusicCategoryCount * 4 > resource199.size())
    {
      diagnostic_ = "SCR resource 199's music directory is out of range";
      return false;
    }

    for (std::size_t category = 0; category < kMusicCategoryCount; ++category)
    {
      const std::size_t base = u32At(resource199, directory + category * 4);
      if (base + kMusicRecordBytes > resource199.size())
      {
        diagnostic_ = "a music category table falls outside resource 199";
        return false;
      }
      // :101-108 walks eight-byte records until one whose resource id is zero.
      for (std::size_t index = 0;; ++index)
      {
        const std::size_t at = base + index * kMusicRecordBytes;
        if (at + kMusicRecordBytes > resource199.size())
        {
          break;
        }
        const auto resourceId =
            static_cast<std::uint16_t>(resource199[at] | (resource199[at + 1] << 8));
        if (resourceId == 0)
        {
          break;
        }
        MusicRecord record;
        record.sndResource = resourceId;
        record.volume = resource199[at + 2];
        record.reverbType =
            static_cast<std::int16_t>(resource199[at + 4] | (resource199[at + 5] << 8));
        record.reverbDepth =
            static_cast<std::uint16_t>(resource199[at + 6] | (resource199[at + 7] << 8));
        musicTables_[category].push_back(record);
      }
    }
    return true;
  }

  const MusicRecord *SoundEngine::musicRecord(std::size_t category, std::size_t index) const
  {
    if (category >= kMusicCategoryCount || index >= musicTables_[category].size())
    {
      return nullptr;
    }
    return &musicTables_[category][index];
  }

  std::size_t SoundEngine::musicRecordCount(std::size_t category) const
  {
    return category < kMusicCategoryCount ? musicTables_[category].size() : 0;
  }

  bool SoundEngine::FUN_00205938_load_slot(std::size_t slot, std::uint16_t sndResource,
                                           std::uint8_t baseVolume,
                                           std::span<const std::uint8_t> bankResource)
  {
    if (slot >= kMusicSlotCount)
    {
      return false;
    }
    const std::lock_guard<std::mutex> guard(mixLock_);
    slotResource_[slot] = sndResource;
    return musicSlots_[slot].FUN_00205938_load(bankResource, baseVolume);
  }

  void SoundEngine::FUN_00205d90_play_slot(std::size_t slot, int fader)
  {
    if (slot >= kMusicSlotCount)
    {
      return;
    }
    {
      const std::lock_guard<std::mutex> guard(mixLock_);
      musicSlots_[slot].FUN_00205d90_play(fader);
    }
    musicEventLog_.push_back({frame_, slot, "play", slotResource_[slot], 0, fader});
  }

  void SoundEngine::FUN_002063c8_ramp_up_slot(std::size_t slot, int speed, int targetFader)
  {
    if (slot >= kMusicSlotCount)
    {
      return;
    }
    {
      const std::lock_guard<std::mutex> guard(mixLock_);
      musicSlots_[slot].ramp(targetFader, speed, true);
    }
    musicEventLog_.push_back({frame_, slot, "ramp up", slotResource_[slot], speed, targetFader});
  }

  void SoundEngine::FUN_00206260_ramp_down_slot(std::size_t slot, int speed, int targetFader)
  {
    if (slot >= kMusicSlotCount)
    {
      return;
    }
    {
      const std::lock_guard<std::mutex> guard(mixLock_);
      musicSlots_[slot].ramp(targetFader, speed, false);
    }
    musicEventLog_.push_back({frame_, slot, "ramp down", slotResource_[slot], speed, targetFader});
  }

  void SoundEngine::setSlotRequestIndex(std::size_t slot, std::uint16_t index)
  {
    if (slot < kMusicSlotCount)
    {
      DAT_00356a18_slotRequestIndex_[slot] = index;
    }
  }

  std::int8_t SoundEngine::FUN_00205778_resolve_alternate_bank(std::uint8_t alternateId) const
  {
    // FUN_00205778 starts at DAT_00356a1c -- slot 2, not slot 0 -- and compares
    // the low 15 bits, because bit 15 of a request is the "play it" flag.
    for (std::size_t slot = 2; slot < kMusicSlotCount; ++slot)
    {
      if ((DAT_00356a18_slotRequestIndex_[slot] & 0x7FFFu) == alternateId)
      {
        return static_cast<std::int8_t>(slot);
      }
    }
    return -1;
  }

  const SoundBank *SoundEngine::bankFor(const KeyOn &request) const
  {
    if (request.musicSlot >= 0)
    {
      const auto slot = static_cast<std::size_t>(request.musicSlot);
      if (slot >= kMusicSlotCount || !musicSlots_[slot].bank().valid())
      {
        return nullptr;
      }
      return &musicSlots_[slot].bank();
    }
    if (request.bank >= kBankCount || !banks_[request.bank].valid())
    {
      return nullptr;
    }
    return &banks_[request.bank];
  }

  bool SoundEngine::slotPlaying(std::size_t slot) const
  {
    return slot < kMusicSlotCount && musicSlots_[slot].playing();
  }

  bool SoundEngine::slotHasSequence(std::size_t slot) const
  {
    return slot < kMusicSlotCount && musicSlots_[slot].hasSequence();
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
    lastDistance_ = -1.0f;
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
    lastDistance_ = distance;
    lastSource_ = {x, y, z};
    lastListener_ = {listener_.x, listener_.y, listener_.z};
    if (distance >= kAudibleRange)
    {
      cueLog_.push_back({cue, frame_, 0, 0, 0, 0, 0, 0, 0, 0.0f, distance,
                         lastSource_[0], lastSource_[1], lastSource_[2],
                         lastListener_[0], lastListener_[1], lastListener_[2], "out of range"});
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
      cueLog_.push_back({cue, frame_, 0, 0, 0, 0, 0, 0, 0, 0.0f, -1.0f, 0,0,0, 0,0,0, "no such cue"});
      return;
    }
    const SoundCue &record = cues_[cue];

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

    // FUN_002057c8:56. Byte +7 replaces the bank rather than adding to it.
    const char *outcome = "played";
    if (record.alternateBank != 0)
    {
      request.musicSlot = FUN_00205778_resolve_alternate_bank(record.alternateBank);
      if (request.musicSlot < 0)
      {
        // Where the original calls FUN_002683a8 and gives up: the scene did not
        // load a slot carrying this bank.
        cueLog_.push_back({cue, frame_, record.alternateBank, record.program, record.note, 0, 0, 0,
                           0, 0.0f, -1.0f, 0,0,0, 0,0,0, "no scene slot holds that bank"});
        return;
      }
    }

    std::int16_t waveform = 0;
    std::size_t samples = 0;
    float rate = 0.0f;
    const SoundBank *bank = bankFor(request);
    if (bank == nullptr)
    {
      outcome = "bank not loaded";
    }
    else if (const VabTone *tone = bank->tone(request.program, request.note); tone == nullptr)
    {
      outcome = "no tone for that note";
    }
    else
    {
      waveform = tone->waveform;
      rate = SoundBank::FUN_00205e50_sample_rate(*tone, request.note);
      if (const WaveformPcm *pcm = bank->waveform(tone->waveform); pcm != nullptr)
      {
        samples = pcm->samples.size();
      }
      const std::lock_guard<std::mutex> guard(mixLock_);
      pending_.push_back(request);
    }

    cueLog_.push_back({cue, frame_,
                       request.musicSlot >= 0 ? record.alternateBank : request.bank, request.program,
                       request.note, request.volumeLeft, request.volumeRight, waveform, samples,
                       rate, lastDistance_, lastSource_[0], lastSource_[1], lastSource_[2],
                       lastListener_[0], lastListener_[1], lastListener_[2], outcome});
  }

  void SoundEngine::startVoice(const KeyOn &request)
  {
    const SoundBank *resolved = bankFor(request);
    if (resolved == nullptr)
    {
      return;
    }
    const SoundBank &bank = *resolved;
    // A program layers -- see SoundBank::tones. Bank 0 is key split so this is
    // always one tone there, but banks 1 and 2 both hold programs whose tones
    // overlap, and those are two-waveform sounds that were playing at half
    // their content.
    const VabTone *matching[kMaxTonesPerProgram] = {};
    const std::size_t layers =
        bank.tones(request.program, request.note, matching, kMaxTonesPerProgram);

    for (std::size_t layer = 0; layer < layers; ++layer)
    {
      const VabTone *tone = matching[layer];
      const WaveformPcm *pcm = bank.waveform(tone->waveform);
      if (pcm == nullptr || pcm->empty())
      {
        continue;
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
      // The cue's own left/right already carry FUN_00267a80's 3D pan, so the
      // tone's pan is deliberately not applied again here; every layered
      // sound-effect tone in the boot banks is centred anyway.
      slot->gainLeft = static_cast<float>(request.volumeLeft) * common;
      slot->gainRight = static_cast<float>(request.volumeRight) * common;
      slot->active = true;
    }
  }

  void SoundEngine::drainPendingKeyOns()
  {
    // The lock covers startVoice too, not just the swap: a cue can now take its
    // waveform from a music slot's bank, and a scene load rewrites those from
    // the simulation thread.
    const std::lock_guard<std::mutex> guard(mixLock_);
    std::vector<KeyOn> requests;
    requests.swap(pending_);
    for (const KeyOn &request : requests)
    {
      startVoice(request);
    }
  }

  void SoundEngine::FUN_00207010_play_voice_line(std::vector<std::int16_t> pcm, float sampleRate)
  {
    const std::lock_guard<std::mutex> guard(mixLock_);
    // FUN_00206d98 refuses to start while DAT_00356788 is set, so a line never
    // overlaps the one before it. Here the caller has already waited out the
    // hold, and cutting the old buffer off is what the original's single
    // reserved voice does anyway.
    voiceLinePcm_ = std::move(pcm);
    voiceLinePosition_ = 0.0;
    voiceLineStep_ = sampleRate > 0.0f ? sampleRate / kSpuBaseSampleRate : 1.0;
    voiceLineActive_ = !voiceLinePcm_.empty();
  }

  void SoundEngine::FUN_00206a48_stop_voice_line()
  {
    const std::lock_guard<std::mutex> guard(mixLock_);
    voiceLineActive_ = false;
    voiceLinePosition_ = 0.0;
  }

  void SoundEngine::mix(float *interleavedStereo, std::size_t frames)
  {
    std::memset(interleavedStereo, 0, frames * 2 * sizeof(float));
    drainPendingKeyOns();

    {
      const std::lock_guard<std::mutex> guard(mixLock_);

      // The eight sequence slots. These run on the mixer's clock rather than the
      // simulation's, exactly like the voice line: nothing here can reach the
      // simulation, so --frames stays byte-identical with or without a device.
      for (SequencePlayer &slot : musicSlots_)
      {
        if (slot.audible())
        {
          slot.render(interleavedStereo, frames);
        }
      }

      if (voiceLineActive_ && !musicSolo_)
      {
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
          const auto index = static_cast<std::size_t>(voiceLinePosition_);
          if (index + 1 >= voiceLinePcm_.size())
          {
            voiceLineActive_ = false;
            break;
          }
          const float fraction =
              static_cast<float>(voiceLinePosition_ - static_cast<double>(index));
          const float sample = (static_cast<float>(voiceLinePcm_[index]) * (1.0f - fraction) +
                                static_cast<float>(voiceLinePcm_[index + 1]) * fraction) /
                               32768.0f;
          // Dialogue is centred: FUN_00207010 programs both volumes from the
          // same uGpffffac60.
          interleavedStereo[frame * 2] += sample;
          interleavedStereo[frame * 2 + 1] += sample;
          voiceLinePosition_ += voiceLineStep_;
        }
      }
    }

    for (Voice &voice : voices_)
    {
      if (!voice.active || musicSolo_)
      {
        continue;
      }
      const std::vector<std::int16_t> &pcm = voice.pcm->samples;
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
