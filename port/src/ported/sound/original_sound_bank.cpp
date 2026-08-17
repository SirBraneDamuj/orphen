#include "ported/sound/original_sound_bank.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace orphen::ported::sound
{
  namespace
  {
    // The five PS-ADPCM predictor pairs, in 1/64ths as the hardware stores them.
    constexpr std::array<std::pair<float, float>, 5> kPredictors{{
        {0.0f, 0.0f},
        {60.0f / 64.0f, 0.0f},
        {115.0f / 64.0f, -52.0f / 64.0f},
        {98.0f / 64.0f, -55.0f / 64.0f},
        {122.0f / 64.0f, -60.0f / 64.0f},
    }};

    constexpr std::size_t kVabHeaderBytes = 32;
    constexpr std::size_t kProgramSlots = 128;
    constexpr std::size_t kProgramAttrBytes = 16;
    constexpr std::size_t kTonesPerProgram = 16;
    constexpr std::size_t kToneAttrBytes = 32;
    constexpr std::size_t kWaveformTableEntries = 256;

    std::uint16_t u16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }
  } // namespace

  WaveformPcm decodePsAdpcm(std::span<const std::uint8_t> blocks)
  {
    WaveformPcm result;
    std::vector<std::int16_t> &pcm = result.samples;
    pcm.reserve(blocks.size() / 16 * 28);

    float previous = 0.0f;
    float beforeThat = 0.0f;
    bool sawLoopStart = false;

    for (std::size_t at = 0; at + 16 <= blocks.size(); at += 16)
    {
      const int shift = blocks[at] & 0x0F;
      std::size_t filter = static_cast<std::size_t>((blocks[at] >> 4) & 0x0F);
      const std::uint8_t flags = blocks[at + 1];
      if (filter >= kPredictors.size())
      {
        filter = 0;
      }
      const auto [first, second] = kPredictors[filter];

      // Bit 2 marks the block a repeat returns to. Recorded before the samples
      // so the loop point is this block's first sample, not its last.
      if ((flags & 4) != 0 && !sawLoopStart)
      {
        result.loopStart = pcm.size();
        sawLoopStart = true;
      }

      for (int index = 0; index < 28; ++index)
      {
        const std::uint8_t packed = blocks[at + 2 + static_cast<std::size_t>(index) / 2];
        int nibble = (index & 1) == 0 ? (packed & 0x0F) : (packed >> 4);
        if (nibble > 7)
        {
          nibble -= 16;
        }

        const float sample =
            static_cast<float>(nibble << (12 - shift)) + previous * first + beforeThat * second;
        beforeThat = previous;
        previous = sample;

        const float clamped = sample < -32768.0f ? -32768.0f : (sample > 32767.0f ? 32767.0f : sample);
        pcm.push_back(static_cast<std::int16_t>(std::lround(clamped)));
      }

      // Bit 0 of the flag byte is the end of the waveform.
      if ((flags & 1) != 0)
      {
        // Bit 1 says the end block repeats. A one-shot's terminator is 0x07,
        // which sets bit 1 *and* points the loop at its own final block -- the
        // hardware's way of spelling "stop", so a loop that starts inside the
        // last block is not a loop.
        const bool repeats = (flags & 2) != 0;
        result.loops = repeats && sawLoopStart && result.loopStart + 28 < pcm.size();
        break;
      }
    }

    if (!result.loops)
    {
      result.loopStart = 0;
    }
    return result;
  }

  void AdsrEnvelope::keyOn(std::uint16_t adsr1, std::uint16_t adsr2)
  {
    sustainLevel_ = static_cast<std::int32_t>(((adsr1 & 0x000Fu) + 1u) * 0x800u);
    if (sustainLevel_ > 0x7FFF)
    {
      sustainLevel_ = 0x7FFF;
    }
    // The decay rate is stored in four bits but indexes the same table as the
    // seven-bit rates, at four times the step.
    decayRate_ = static_cast<std::uint8_t>(((adsr1 >> 4) & 0x0Fu) * 4u);
    attackRate_ = static_cast<std::uint8_t>((adsr1 >> 8) & 0x7Fu);
    attackExponential_ = (adsr1 & 0x8000u) != 0;

    releaseRate_ = static_cast<std::uint8_t>((adsr2 & 0x1Fu) * 4u);
    releaseExponential_ = (adsr2 & 0x0020u) != 0;
    sustainRate_ = static_cast<std::uint8_t>((adsr2 >> 6) & 0x7Fu);
    sustainDecreasing_ = (adsr2 & 0x4000u) != 0;
    sustainExponential_ = (adsr2 & 0x8000u) != 0;

    level_ = 0;
    counter_ = 0;
    phase_ = EnvelopePhase::Attack;
  }

  void AdsrEnvelope::keyOff()
  {
    if (phase_ != EnvelopePhase::Off)
    {
      phase_ = EnvelopePhase::Release;
      counter_ = 0;
    }
  }

  float AdsrEnvelope::step()
  {
    if (phase_ == EnvelopePhase::Off)
    {
      return 0.0f;
    }

    std::uint8_t rate = 0;
    bool increasing = false;
    bool exponential = false;
    switch (phase_)
    {
    case EnvelopePhase::Attack:
      rate = attackRate_;
      increasing = true;
      exponential = attackExponential_;
      break;
    case EnvelopePhase::Decay:
      rate = decayRate_;
      exponential = true; // decay is always exponential on the SPU
      break;
    case EnvelopePhase::Sustain:
      rate = sustainRate_;
      increasing = !sustainDecreasing_;
      exponential = sustainExponential_;
      break;
    case EnvelopePhase::Release:
      rate = releaseRate_;
      exponential = releaseExponential_;
      break;
    case EnvelopePhase::Off:
      return 0.0f;
    }

    const std::int32_t shift = static_cast<std::int32_t>(rate >> 2);
    std::int32_t step = increasing ? (7 - static_cast<std::int32_t>(rate & 3))
                                   : (-8 + static_cast<std::int32_t>(rate & 3));
    std::int32_t cycles = 1 << std::max(0, shift - 11);
    step <<= std::max(0, 11 - shift);

    if (exponential)
    {
      if (increasing && level_ > 0x6000)
      {
        cycles *= 4;
      }
      if (!increasing)
      {
        step = static_cast<std::int32_t>(static_cast<std::int64_t>(step) * level_ / 0x8000);
        if (step == 0)
        {
          step = -1; // never stall a falling exponential
        }
      }
    }

    if (++counter_ >= cycles)
    {
      counter_ = 0;
      level_ = std::clamp(level_ + step, 0, 0x7FFF);
    }

    switch (phase_)
    {
    case EnvelopePhase::Attack:
      if (level_ >= 0x7FFF)
      {
        phase_ = EnvelopePhase::Decay;
        counter_ = 0;
      }
      break;
    case EnvelopePhase::Decay:
      if (level_ <= sustainLevel_)
      {
        phase_ = EnvelopePhase::Sustain;
        counter_ = 0;
      }
      break;
    case EnvelopePhase::Release:
      if (level_ <= 0)
      {
        phase_ = EnvelopePhase::Off;
      }
      break;
    default:
      break;
    }

    return static_cast<float>(level_) / 32767.0f;
  }

  bool SoundBank::load(std::span<const std::uint8_t> resource)
  {
    valid_ = false;
    programs_.clear();
    waveformOffset_.clear();
    waveformSize_.clear();
    decoded_.clear();
    decodedValid_.clear();
    body_.clear();
    diagnostic_.clear();

    if (resource.size() < 16)
    {
      diagnostic_ = "bank resource is shorter than its container header";
      return false;
    }

    // FUN_00205548's three sections.
    const std::uint32_t sectionCount = u32At(resource, 0);
    if (sectionCount < 2)
    {
      diagnostic_ = "bank resource has fewer than two sections";
      return false;
    }

    const auto section = [&](std::size_t index) -> std::span<const std::uint8_t> {
      const std::uint32_t word = u32At(resource, 4 + index * 4);
      const std::size_t offset = static_cast<std::size_t>(word >> 16) * 16u;
      const std::size_t size = static_cast<std::size_t>(word & 0xFFFFu) * 16u;
      if (offset > resource.size() || size > resource.size() - offset)
      {
        return {};
      }
      return resource.subspan(offset, size);
    };

    const std::span<const std::uint8_t> waveforms = section(0);
    const std::span<const std::uint8_t> header = section(1);
    if (header.size() < kVabHeaderBytes + kProgramSlots * kProgramAttrBytes)
    {
      diagnostic_ = "bank has no usable VAB header";
      return false;
    }
    if (header[0] != 'p' || header[1] != 'B' || header[2] != 'A' || header[3] != 'V')
    {
      // 'VABp' as a little-endian word reads back "pBAV" byte by byte.
      diagnostic_ = "bank section 1 is not a VAB header";
      return false;
    }

    const std::uint16_t programsPresent = u16At(header, 18);
    const std::uint16_t waveformCount = u16At(header, 22);
    masterVolume_ = header[24];

    const std::size_t toneBase = kVabHeaderBytes + kProgramSlots * kProgramAttrBytes;
    const std::size_t tableBase = toneBase + static_cast<std::size_t>(programsPresent) *
                                                 kTonesPerProgram * kToneAttrBytes;
    if (header.size() < tableBase + kWaveformTableEntries * 2)
    {
      diagnostic_ = "VAB header is shorter than its program count implies";
      return false;
    }

    // ProgAtr[128]. Tone blocks are stored only for the programs that have
    // tones, in order, which is what `used` counts off.
    programs_.resize(kProgramSlots);
    std::size_t used = 0;
    for (std::size_t index = 0; index < kProgramSlots; ++index)
    {
      const std::size_t at = kVabHeaderBytes + index * kProgramAttrBytes;
      VabProgram &program = programs_[index];
      program.toneCount = header[at];
      program.volume = header[at + 1];
      program.pan = header[at + 4];
      if (program.toneCount == 0)
      {
        continue;
      }

      const std::size_t block = toneBase + used * kTonesPerProgram * kToneAttrBytes;
      ++used;
      const std::size_t tones = std::min<std::size_t>(program.toneCount, kTonesPerProgram);
      for (std::size_t toneIndex = 0; toneIndex < tones; ++toneIndex)
      {
        const std::size_t entry = block + toneIndex * kToneAttrBytes;
        if (entry + kToneAttrBytes > header.size())
        {
          break;
        }
        VabTone tone;
        tone.volume = header[entry + 2];
        tone.pan = header[entry + 3];
        tone.centreNote = header[entry + 4];
        tone.fineShift = header[entry + 5];
        tone.noteLow = header[entry + 6];
        tone.noteHigh = header[entry + 7];
        tone.adsr1 = u16At(header, entry + 16);
        tone.adsr2 = u16At(header, entry + 18);
        tone.waveform = static_cast<std::int16_t>(u16At(header, entry + 22));
        program.tones.push_back(tone);
      }
    }

    // The waveform table: entry 0 is a lead-in, entries 1..vs are the sizes in
    // eight-byte units, and the waveforms tile the body in that order.
    waveformOffset_.assign(static_cast<std::size_t>(waveformCount) + 1, 0);
    waveformSize_.assign(static_cast<std::size_t>(waveformCount) + 1, 0);
    std::size_t running = static_cast<std::size_t>(u16At(header, tableBase)) * 8u;
    for (std::size_t index = 1; index <= waveformCount; ++index)
    {
      const std::size_t size = static_cast<std::size_t>(u16At(header, tableBase + index * 2)) * 8u;
      waveformOffset_[index] = running;
      waveformSize_[index] = size;
      running += size;
    }

    body_.assign(waveforms.begin(), waveforms.end());
    decoded_.resize(static_cast<std::size_t>(waveformCount) + 1);
    decodedValid_.assign(static_cast<std::size_t>(waveformCount) + 1, false);
    valid_ = true;
    return true;
  }

  std::size_t SoundBank::usedProgramCount() const
  {
    std::size_t used = 0;
    for (const VabProgram &program : programs_)
    {
      if (program.toneCount != 0)
      {
        ++used;
      }
    }
    return used;
  }

  const VabProgram *SoundBank::program(std::size_t index) const
  {
    return index < programs_.size() ? &programs_[index] : nullptr;
  }

  std::size_t SoundBank::tones(std::size_t program, std::uint8_t note, const VabTone **out,
                               std::size_t max) const
  {
    const VabProgram *entry = this->program(program);
    if (entry == nullptr || out == nullptr)
    {
      return 0;
    }
    std::size_t found = 0;
    for (const VabTone &tone : entry->tones)
    {
      if (note >= tone.noteLow && note <= tone.noteHigh)
      {
        if (found >= max)
        {
          break;
        }
        out[found++] = &tone;
      }
    }
    return found;
  }

  const VabTone *SoundBank::tone(std::size_t program, std::uint8_t note) const
  {
    const VabTone *first = nullptr;
    return tones(program, note, &first, 1) == 1 ? first : nullptr;
  }

  const WaveformPcm *SoundBank::waveform(std::int16_t index) const
  {
    const auto slot = static_cast<std::size_t>(index);
    if (index <= 0 || slot >= waveformSize_.size() || waveformSize_[slot] == 0)
    {
      return nullptr;
    }
    if (!decodedValid_[slot])
    {
      const std::size_t offset = waveformOffset_[slot];
      const std::size_t size = waveformSize_[slot];
      if (offset + size > body_.size())
      {
        return nullptr;
      }
      decoded_[slot] = decodePsAdpcm(std::span<const std::uint8_t>(body_.data() + offset, size));
      decodedValid_[slot] = true;
    }
    return &decoded_[slot];
  }

  float SoundBank::FUN_00205e50_sample_rate(const VabTone &tone, std::uint8_t note)
  {
    // The SPU pitch register in 1/128ths of a semitone away from the tone's
    // centre. A sound-effect record carries only a whole note, so the fine half
    // of the IOP's argument is zero and only the tone's own shift applies.
    //
    // **The shift is added, not subtracted.** VagAtr's `shift` reads like a
    // fine tune on the centre note, which would make the sample's natural
    // pitch `centre + shift/128` and put a minus here -- and that is a
    // semitone flat. Two independent checks say otherwise: with it added, 86
    // of the 280 resolvable cues land within 1% of a standard authoring rate
    // against 9 the other way, and five of the SPU2 voice pitch registers in a
    // PCSX2 savestate are reproduced *exactly* against none. So the natural
    // pitch is `centre - shift/128`. Cue 8 comes out at 22055 Hz and cue 159
    // at 11027, which are 22050 and 11025 to within a cent.
    const int distance = (static_cast<int>(note) - static_cast<int>(tone.centreNote)) * 128 +
                         static_cast<int>(tone.fineShift);
    return kSpuBaseSampleRate * std::pow(2.0f, static_cast<float>(distance) / (12.0f * 128.0f));
  }

  float SoundBank::sampleRateWithBend(const VabTone &tone, std::uint8_t note, float cents)
  {
    return FUN_00205e50_sample_rate(tone, note) * std::pow(2.0f, cents / 1200.0f);
  }

} // namespace orphen::ported::sound
