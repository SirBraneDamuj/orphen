#include "ported/sound/original_sequence_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orphen::ported::sound
{
  namespace
  {
    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    // Sony's loop convention, on CC99 / CC6.
    constexpr std::uint8_t kLoopMarkerController = 99;
    constexpr std::uint8_t kLoopCountController = 6;
    constexpr std::uint8_t kLoopStartMarker = 20;
    constexpr std::uint8_t kLoopEndMarker = 30;
    constexpr std::uint8_t kLoopForever = 127;
  } // namespace

  bool SequencePlayer::FUN_00205938_load(std::span<const std::uint8_t> bankResource,
                                         std::uint8_t baseVolume)
  {
    reset();
    events_.clear();
    loaded_ = false;
    baseVolume_ = baseVolume;

    if (!bank_.load(bankResource))
    {
      return false;
    }
    loaded_ = true;

    if (bankResource.size() < 16)
    {
      return false;
    }
    // FUN_00205548's section 2.
    const std::uint32_t word = u32At(bankResource, 4 + 2 * 4);
    const std::size_t offset = static_cast<std::size_t>(word >> 16) * 16u;
    const std::size_t size = static_cast<std::size_t>(word & 0xFFFFu) * 16u;
    if (offset > bankResource.size() || size > bankResource.size() - offset || size < 16)
    {
      return false;
    }
    const std::span<const std::uint8_t> seq = bankResource.subspan(offset, size);

    // "SEQp" stored little-endian reads back as "pQES"; "NSEQ" is FUN_00205548's
    // marker for a bank that carries no sequence at all.
    if (!(seq[0] == 'p' && seq[1] == 'Q' && seq[2] == 'E' && seq[3] == 'S'))
    {
      return false;
    }

    // The header is big-endian, unlike everything else in the file.
    ppqn_ = static_cast<std::uint16_t>((seq[8] << 8) | seq[9]);
    usPerQuarter_ = (static_cast<std::uint32_t>(seq[10]) << 16) |
                    (static_cast<std::uint32_t>(seq[11]) << 8) | seq[12];
    if (ppqn_ == 0)
    {
      ppqn_ = 48;
    }
    if (usPerQuarter_ == 0)
    {
      usPerQuarter_ = 500000;
    }

    events_.assign(seq.begin() + 15, seq.end());
    return true;
  }

  bool SequencePlayer::audible() const
  {
    if (playing_)
    {
      return true;
    }
    for (const SequenceVoice &voice : voices_)
    {
      if (voice.active)
      {
        return true;
      }
    }
    return false;
  }

  void SequencePlayer::reset()
  {
    playing_ = false;
    cursor_ = 0;
    runningStatus_ = 0;
    samplesUntilNextEvent_ = 0.0;
    deltaConsumed_ = false;
    loopCursor_ = 0;
    loopRunningStatus_ = 0;
    haveLoopPoint_ = false;
    loopsRemaining_ = 0;
    loopForever_ = false;
    loopsTaken_ = 0;
    reachedEndOfTrack_ = false;
    desynced_ = false;
    fader_ = kFaderFull;
    targetFader_ = kFaderFull;
    faderStep_ = 0.0;
    ramping_ = false;
    stopWhenRampEnds_ = false;
    channels_.fill(SequenceChannel{});
    for (SequenceVoice &voice : voices_)
    {
      voice = SequenceVoice{};
    }
  }

  float SequencePlayer::masterGain() const
  {
    // FUN_00206048: fader * base / 1000, where base is 0..127.
    const double scaled = fader_ * static_cast<double>(baseVolume_) / 1000.0;
    return static_cast<float>(std::clamp(scaled, 0.0, 127.0) / 127.0);
  }

  void SequencePlayer::FUN_00205d90_play(int fader)
  {
    const bool hadSequence = hasSequence();
    const std::uint8_t base = baseVolume_;
    std::vector<std::uint8_t> events = std::move(events_);
    reset();
    events_ = std::move(events);
    baseVolume_ = base;
    if (!hadSequence)
    {
      return;
    }
    fader_ = std::clamp(fader, 0, kFaderFull);
    targetFader_ = fader_;
    playing_ = true;
  }

  void SequencePlayer::stop()
  {
    playing_ = false;
    ramping_ = false;
    stopWhenRampEnds_ = false;
    for (SequenceVoice &voice : voices_)
    {
      voice.active = false;
      voice.held = false;
    }
  }

  void SequencePlayer::ramp(int targetFader, int speed, bool rampUp)
  {
    if (!playing_)
    {
      // FUN_002063c8:12 starts the slot first when it is loaded but idle.
      if (rampUp && hasSequence())
      {
        FUN_00205d90_play(0);
      }
      else
      {
        // FUN_00206260 returns here (its `state < 2` test), because on the real
        // machine the IOP owns the voices and has already silenced them. Here
        // the voices are ours, and a track that ran to its end left them in
        // release -- a slow release then rings on with nothing able to stop it.
        // A script asking for silence gets silence.
        if (!rampUp && targetFader == 0)
        {
          stop();
        }
        return;
      }
    }

    targetFader = std::clamp(targetFader, 0, kFaderFull);
    // The original works the frame count out from the 0..127 delta, not the
    // fader delta.
    const int from = static_cast<int>(fader_) * baseVolume_ / 1000;
    const int to = targetFader * baseVolume_ / 1000;
    const int delta = rampUp ? to - from : from - to;

    if (delta <= 0)
    {
      fader_ = targetFader;
      targetFader_ = targetFader;
      ramping_ = false;
      if (!rampUp && targetFader == 0)
      {
        stop();
      }
      return;
    }

    int scale = 4;
    if (speed >= 8)
    {
      scale = 1;
    }
    else if (speed >= 4)
    {
      scale = 2;
    }
    else if (speed >= 2)
    {
      scale = 3;
    }
    int frames = scale * delta;
    if (speed > 13)
    {
      frames /= 2;
    }
    frames += 10;

    // The original counts simulation frames; the sequencer runs on the audio
    // clock, so the same duration is expressed in output samples.
    const double samples = static_cast<double>(frames) *
                           (static_cast<double>(kSpuBaseSampleRate) / 60.0);
    targetFader_ = targetFader;
    faderStep_ = (targetFader_ - fader_) / samples;
    ramping_ = true;
    stopWhenRampEnds_ = !rampUp && targetFader == 0;
  }

  std::uint32_t SequencePlayer::readVariableLength()
  {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4 && cursor_ < events_.size(); ++byte)
    {
      const std::uint8_t next = events_[cursor_++];
      value = (value << 7) | static_cast<std::uint32_t>(next & 0x7F);
      if ((next & 0x80) == 0)
      {
        break;
      }
    }
    return value;
  }

  void SequencePlayer::allNotesOff()
  {
    for (SequenceVoice &voice : voices_)
    {
      if (voice.active)
      {
        voice.held = false;
        voice.envelope.keyOff();
      }
    }
  }

  void SequencePlayer::updateVoiceGain(SequenceVoice &voice)
  {
    const SequenceChannel &channel = channels_[voice.channel];
    if (voice.tone == nullptr)
    {
      return;
    }
    const VabProgram *program = bank_.program(channel.program);
    const float programGain = program != nullptr ? static_cast<float>(program->volume) / 127.0f : 1.0f;
    const float toneGain = static_cast<float>(voice.tone->volume) / 127.0f;
    const float bankGain = static_cast<float>(bank_.masterVolume()) / 127.0f;
    const float channelGain = (static_cast<float>(channel.volume) / 127.0f) *
                              (static_cast<float>(channel.expression) / 127.0f);

    // The tone's own pan and the channel's compose; both are 0..127 about 64.
    const float pan = std::clamp((static_cast<float>(channel.pan) - 64.0f) / 64.0f +
                                     (static_cast<float>(voice.tone->pan) - 64.0f) / 64.0f,
                                 -1.0f, 1.0f);
    // Constant-power, so a centred note is not louder than a panned one.
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265359f;
    // Velocity is not an SPU register; the IOP folds it into the voice volume,
    // so it belongs here rather than at key-on -- otherwise a later CC7 would
    // recompute the gain and quietly drop it.
    const float velocityGain = static_cast<float>(voice.velocity) / 127.0f;
    const float common = programGain * toneGain * bankGain * channelGain * velocityGain;
    voice.gainLeft = common * std::cos(angle);
    voice.gainRight = common * std::sin(angle);
  }

  void SequencePlayer::noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
  {
    if (channel >= kSequenceChannels)
    {
      return;
    }
    if (velocity == 0)
    {
      noteOff(channel, note);
      return;
    }

    const SequenceChannel &state = channels_[channel];
    // A program layers: every tone whose range covers the note sounds, each on
    // its own voice. See SoundBank::tones.
    const VabTone *matching[kMaxTonesPerProgram] = {};
    const std::size_t layers = bank_.tones(state.program, note, matching, kMaxTonesPerProgram);

    for (std::size_t layer = 0; layer < layers; ++layer)
    {
      const VabTone *tone = matching[layer];
      const WaveformPcm *pcm = bank_.waveform(tone->waveform);
      if (pcm == nullptr || pcm->empty())
      {
        continue;
      }

      // Take a free voice, else steal one that is already releasing.
      SequenceVoice *slot = nullptr;
      for (SequenceVoice &voice : voices_)
      {
        if (!voice.active)
        {
          slot = &voice;
          break;
        }
      }
      if (slot == nullptr)
      {
        for (SequenceVoice &voice : voices_)
        {
          if (!voice.held)
          {
            slot = &voice;
            break;
          }
        }
      }
      if (slot == nullptr)
      {
        return;
      }

      *slot = SequenceVoice{};
      slot->pcm = pcm;
      slot->tone = tone;
      slot->channel = channel;
      slot->note = note;
      slot->velocity = velocity;
      slot->position = 0.0;
      slot->step = SoundBank::sampleRateWithBend(*tone, note, state.bendCents) / kSpuBaseSampleRate;
      slot->active = true;
      slot->held = true;
      slot->envelope.keyOn(tone->adsr1, tone->adsr2);
      updateVoiceGain(*slot);
    }
  }

  void SequencePlayer::noteOff(std::uint8_t channel, std::uint8_t note)
  {
    if (channel >= kSequenceChannels)
    {
      return;
    }
    for (SequenceVoice &voice : voices_)
    {
      if (voice.active && voice.held && voice.channel == channel && voice.note == note)
      {
        voice.held = false;
        if (!channels_[channel].sustainPedal)
        {
          voice.envelope.keyOff();
        }
      }
    }
  }

  void SequencePlayer::runEvents()
  {
    // Runs every event whose delta has already elapsed. A zero-delta run of
    // events -- which is how a chord is written -- all fires in one pass.
    while (playing_)
    {
      if (cursor_ >= events_.size())
      {
        stop();
        return;
      }

      // The stream is delta-then-event, so the wait for the event about to be
      // read has to be taken before reading it.
      if (!deltaConsumed_)
      {
        const std::uint32_t delta = readVariableLength();
        const double samplesPerTick = static_cast<double>(kSpuBaseSampleRate) *
                                      static_cast<double>(usPerQuarter_) /
                                      (static_cast<double>(ppqn_) * 1000000.0);
        samplesUntilNextEvent_ += static_cast<double>(delta) * samplesPerTick;
        deltaConsumed_ = true;
      }
      if (samplesUntilNextEvent_ > 0.0)
      {
        return;
      }
      if (cursor_ >= events_.size())
      {
        stop();
        return;
      }
      deltaConsumed_ = false;

      std::uint8_t status = events_[cursor_];
      if ((status & 0x80) != 0)
      {
        ++cursor_;
        if (status < 0xF0)
        {
          runningStatus_ = status;
        }
      }
      else
      {
        status = runningStatus_;
        if (status == 0)
        {
          stop();
          return;
        }
      }

      const std::uint8_t channel = status & 0x0F;
      const std::uint8_t kind = status & 0xF0;

      const auto need = [&](std::size_t count) { return cursor_ + count <= events_.size(); };

      switch (kind)
      {
      case 0x80: // note off
        if (!need(2))
        {
          stop();
          return;
        }
        noteOff(channel, events_[cursor_]);
        cursor_ += 2;
        break;

      case 0x90: // note on
        if (!need(2))
        {
          stop();
          return;
        }
        noteOn(channel, events_[cursor_], events_[cursor_ + 1]);
        cursor_ += 2;
        break;

      case 0xA0: // polyphonic aftertouch -- no SPU equivalent
        if (!need(2))
        {
          stop();
          return;
        }
        cursor_ += 2;
        break;

      case 0xB0: // control change
      {
        if (!need(2))
        {
          stop();
          return;
        }
        const std::uint8_t controller = events_[cursor_];
        const std::uint8_t value = events_[cursor_ + 1];
        cursor_ += 2;
        SequenceChannel &state = channels_[channel];
        switch (controller)
        {
        case 7:
          state.volume = value;
          break;
        case 10:
          state.pan = value;
          break;
        case 11:
          state.expression = value;
          break;
        case 64:
          state.sustainPedal = value >= 64;
          if (!state.sustainPedal)
          {
            for (SequenceVoice &voice : voices_)
            {
              if (voice.active && !voice.held && voice.channel == channel)
              {
                voice.envelope.keyOff();
              }
            }
          }
          break;
        case kLoopCountController:
          // Data entry: how many times the loop below repeats.
          loopForever_ = value >= kLoopForever;
          loopsRemaining_ = loopForever_ ? 0 : static_cast<int>(value);
          break;
        case kLoopMarkerController:
          if (value == kLoopStartMarker)
          {
            loopCursor_ = cursor_;
            loopRunningStatus_ = runningStatus_;
            haveLoopPoint_ = true;
          }
          else if (value == kLoopEndMarker && haveLoopPoint_)
          {
            if (loopForever_ || loopsRemaining_ > 0)
            {
              if (!loopForever_)
              {
                --loopsRemaining_;
              }
              cursor_ = loopCursor_;
              runningStatus_ = loopRunningStatus_;
              ++loopsTaken_;
            }
          }
          break;
        case 120: // all sound off
        case 123: // all notes off
          allNotesOff();
          break;
        default:
          break;
        }
        // Volume, expression and pan take effect on notes already sounding.
        for (SequenceVoice &voice : voices_)
        {
          if (voice.active && voice.channel == channel)
          {
            updateVoiceGain(voice);
          }
        }
        break;
      }

      case 0xC0: // program change
        if (!need(1))
        {
          stop();
          return;
        }
        channels_[channel].program = events_[cursor_];
        cursor_ += 1;
        break;

      case 0xD0: // channel aftertouch
        if (!need(1))
        {
          stop();
          return;
        }
        cursor_ += 1;
        break;

      case 0xE0: // pitch bend
      {
        if (!need(2))
        {
          stop();
          return;
        }
        const int bend = (static_cast<int>(events_[cursor_ + 1]) << 7) |
                         static_cast<int>(events_[cursor_]);
        cursor_ += 2;
        // The default range is two semitones either way.
        channels_[channel].bendCents = static_cast<float>(bend - 8192) / 8192.0f * 200.0f;
        for (SequenceVoice &voice : voices_)
        {
          if (voice.active && voice.channel == channel && voice.tone != nullptr)
          {
            voice.step = SoundBank::sampleRateWithBend(*voice.tone, voice.note,
                                                       channels_[channel].bendCents) /
                         kSpuBaseSampleRate;
          }
        }
        break;
      }

      case 0xF0:
        if (status == 0xFF)
        {
          if (!need(1))
          {
            stop();
            return;
          }
          const std::uint8_t meta = events_[cursor_++];

          // **Sony's meta encoding is not standard MIDI's.** A tempo change is
          // `FF 51 <u24>` with *no* length byte in front of it, where a .mid
          // would write `FF 51 03 <u24>`. Reading the first tempo byte as a
          // length desynchronises the rest of the track: SND resource 117's
          // first tempo is 0x12AF29, so the parser skipped 18 bytes and spent
          // the remainder of the piece interpreting note data as status bytes.
          //
          // Only two meta types exist across all 283 sequences in the game --
          // FF 2F in 282 of them and FF 51 four times, all four in resource
          // 117 -- so anything else is a desync rather than a meta to skip.
          if (meta == 0x51)
          {
            if (!need(3))
            {
              stop();
              return;
            }
            usPerQuarter_ = (static_cast<std::uint32_t>(events_[cursor_]) << 16) |
                            (static_cast<std::uint32_t>(events_[cursor_ + 1]) << 8) |
                            events_[cursor_ + 2];
            cursor_ += 3;
            if (usPerQuarter_ == 0)
            {
              usPerQuarter_ = 500000;
            }
            break;
          }

          if (meta != 0x2F)
          {
            // Not a meta this format uses: the stream is out of step, and
            // guessing a length would only carry the damage further.
            desynced_ = true;
            stop();
            return;
          }

          // FF 2F 00 -- the trailing byte reads the same as a length.
          readVariableLength();

          // A sequence with a loop pair that reached the end without hitting
          // the end marker still repeats -- the ambient beds are written that
          // way. Otherwise it stops.
          if (haveLoopPoint_ && (loopForever_ || loopsRemaining_ > 0))
          {
            if (!loopForever_)
            {
              --loopsRemaining_;
            }
            cursor_ = loopCursor_;
            runningStatus_ = loopRunningStatus_;
            ++loopsTaken_;
            break;
          }
          reachedEndOfTrack_ = true;
          allNotesOff();
          playing_ = false;
          return;
        }
        // Any other system message: nothing in these files uses one.
        stop();
        return;

      default:
        stop();
        return;
      }

    }
  }

  void SequencePlayer::render(float *interleavedStereo, std::size_t frames)
  {
    std::size_t done = 0;
    while (done < frames)
    {
      if (playing_)
      {
        runEvents();
      }

      // Render up to the next event, so an event never lands mid-block.
      std::size_t block = frames - done;
      if (playing_ && samplesUntilNextEvent_ > 0.0 &&
          samplesUntilNextEvent_ < static_cast<double>(block))
      {
        block = static_cast<std::size_t>(samplesUntilNextEvent_) + 1;
        block = std::min(block, frames - done);
      }
      if (block == 0)
      {
        break;
      }

      // The fader moves per block rather than per sample: a ramp runs over
      // seconds, so a step every few hundred samples is inaudible and keeps the
      // inner loop free of it.
      const float blockGain = masterGain();

      bool anyVoice = false;
      for (SequenceVoice &voice : voices_)
      {
        if (!voice.active)
        {
          continue;
        }
        anyVoice = true;
        const std::vector<std::int16_t> &pcm = voice.pcm->samples;
        for (std::size_t frame = 0; frame < block; ++frame)
        {
          auto index = static_cast<std::size_t>(voice.position);
          if (index + 1 >= pcm.size())
          {
            if (voice.pcm->loops && voice.pcm->loopStart + 1 < pcm.size())
            {
              voice.position = static_cast<double>(voice.pcm->loopStart) +
                               (voice.position - static_cast<double>(index));
              index = static_cast<std::size_t>(voice.position);
              if (index + 1 >= pcm.size())
              {
                voice.active = false;
                break;
              }
            }
            else
            {
              voice.active = false;
              break;
            }
          }
          const float envelope = voice.envelope.step();
          if (voice.envelope.finished())
          {
            voice.active = false;
            break;
          }
          const auto fraction = static_cast<float>(voice.position - static_cast<double>(index));
          const float sample = (static_cast<float>(pcm[index]) * (1.0f - fraction) +
                                static_cast<float>(pcm[index + 1]) * fraction) /
                               32768.0f;
          const float gain = envelope * blockGain;
          const std::size_t out = (done + frame) * 2;
          interleavedStereo[out] += sample * voice.gainLeft * gain;
          interleavedStereo[out + 1] += sample * voice.gainRight * gain;
          voice.position += voice.step;
        }
      }

      if (ramping_)
      {
        fader_ += faderStep_ * static_cast<double>(block);
        const bool arrived = faderStep_ < 0.0 ? fader_ <= targetFader_ : fader_ >= targetFader_;
        if (arrived)
        {
          fader_ = targetFader_;
          ramping_ = false;
          if (stopWhenRampEnds_)
          {
            stop();
          }
        }
      }

      samplesUntilNextEvent_ -= static_cast<double>(block);
      done += block;

      if (!playing_ && !anyVoice)
      {
        break;
      }
    }
  }

} // namespace orphen::ported::sound
