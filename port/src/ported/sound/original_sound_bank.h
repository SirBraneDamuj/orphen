#pragma once

// A sound bank: the container SND.BIN stores, the Sony VAB inside it, and the
// PS-ADPCM waveforms the VAB indexes.
//
//   src/FUN_00205548.c  split a bank resource into its three sections
//   src/FUN_00205310.c  hand the sections to the IOP and open the VAB
//   src/FUN_00205118.c  boot: SND resources 1, 2 and 3 become banks 0, 1 and 2
//
// == The container ==
//
// A bank resource is a 16-byte header followed by three sections:
//
//   +0x00  u32  section count, always 3
//   +0x04  u32  section 0: (word >> 16) * 16 = offset, (word & 0xFFFF) * 16 = size
//   +0x08  u32  section 1
//   +0x0C  u32  section 2
//
// Ghidra prints the offset term as `&DAT_01849a00 + n * 4`, which is pointer
// arithmetic on a 4-byte element -- the same function's other read of the same
// value spells it `(int)&DAT_01849a00 + n * 0x10 + 3`, so the scale is 16. The
// three sections then tile the resource exactly, which is how this was checked:
// for SND 0x85 they run 0x10..0x6B40, 0x6B40..0x8160 and 0x8160..0x8980, and
// 0x8980 is the resource's length.
//
// Section 0 is the VAB body (the waveforms), section 1 the VAB header, section
// 2 the sequence data. FUN_00205548 sniffs section 1 for "NV" and section 2 for
// "NSEQ", both of which mark a section as a reference rather than data; neither
// appears in the banks this port loads.
//
// **SND resources are stored uncompressed.** FUN_00223268 DMAs raw sectors and
// leaves decompression to the caller; every other caller runs FUN_002f3118 over
// the result and FUN_00205548 does not.
//
// == The VAB ==
//
// Standard Sony VAB, version 7:
//
//   +0x000  VabHdr, 32 bytes: 'VABp', ver, id, fsize, ps, ts, vs, mvol, pan
//   +0x020  ProgAtr[128], 16 bytes each
//   +0x820  VagAtr[16] per *used* program, 32 bytes each
//   ...     u16 vagSizes[256], each an eighth of a waveform's byte length
//
// so the header's length is 32 + 2048 + programs * 512 + 512. That is how the
// port cross-checks the parse: the EE dump records bank 0's header length as
// 9760, which is exactly 14 programs, and the sound-effect table never asks
// bank 0 for a program above 13.
//
// Bank 0's programs are **key split**: every tone has min == max, so the note
// in a sound-effect record selects the waveform rather than transposing one.
// The pitch still comes out of the note, against the tone's centre note, which
// is how a waveform recorded at 22 kHz is stored in a 48 kHz bank. See
// FUN_00205e50_sample_rate for the sign on the tone's `shift`, which is not
// what the field name suggests.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::sound
{

  // One VagAtr, reduced to the fields a one-shot needs.
  struct VabTone
  {
    std::uint8_t volume = 127;
    std::uint8_t pan = 64;
    std::uint8_t centreNote = 60;
    std::uint8_t fineShift = 0;
    std::uint8_t noteLow = 0;
    std::uint8_t noteHigh = 127;
    std::uint16_t adsr1 = 0;
    std::uint16_t adsr2 = 0;
    std::int16_t waveform = 0; // 1-based index into the VAG table
  };

  struct VabProgram
  {
    std::uint8_t toneCount = 0;
    std::uint8_t volume = 127;
    std::uint8_t pan = 64;
    std::vector<VabTone> tones;
  };

  // == The ADSR envelope ==
  //
  // A one-shot cue never needed one: it plays a waveform to its end block and
  // stops, and the port's mixer did exactly that. A held sequencer note has no
  // end block to reach, so the envelope is the only thing that ever ends it, and
  // the release phase is what stops a chord ringing through the next bar.
  //
  // VagAtr stores the two SPU registers verbatim. Fields are the hardware's:
  //
  //   adsr1  bits 0-3   sustain level          bits 4-7   decay rate (4 bits)
  //          bits 8-14  attack rate (7 bits)   bit 15     attack exponential
  //   adsr2  bits 0-4   release rate (5 bits)  bit 5      release exponential
  //          bits 6-12  sustain rate (7 bits)  bit 14     sustain decreasing
  //          bit 15     sustain exponential
  //
  // The envelope is a 15-bit counter stepped once per output sample. For a rate
  // R the hardware waits `1 << max(0, (R >> 2) - 11)` samples and then adds
  // `step << max(0, 11 - (R >> 2))`, where step is `7 - (R & 3)` rising and
  // `-8 + (R & 3)` falling. Exponential rise slows by 4x above 0x6000;
  // exponential fall scales the step by the current level.
  enum class EnvelopePhase
  {
    Attack,
    Decay,
    Sustain,
    Release,
    Off,
  };

  class AdsrEnvelope
  {
  public:
    void keyOn(std::uint16_t adsr1, std::uint16_t adsr2);
    void keyOff();
    // One output sample. Returns the level as 0..1.
    float step();
    bool finished() const { return phase_ == EnvelopePhase::Off; }
    EnvelopePhase phase() const { return phase_; }

  private:
    EnvelopePhase phase_ = EnvelopePhase::Off;
    std::int32_t level_ = 0; // 0..0x7FFF
    std::int32_t counter_ = 0;
    std::uint8_t attackRate_ = 0;
    std::uint8_t decayRate_ = 0;
    std::uint8_t sustainRate_ = 0;
    std::uint8_t releaseRate_ = 0;
    std::int32_t sustainLevel_ = 0x7FFF;
    bool attackExponential_ = false;
    bool sustainExponential_ = false;
    bool sustainDecreasing_ = true;
    bool releaseExponential_ = false;
  };

  // SPU2 plays a waveform at its recorded rate when the pitch register reads
  // 0x1000, and the register is 48 kHz there (the PS1's SPU was 44.1).
  inline constexpr float kSpuBaseSampleRate = 48000.0f;

  // VagAtr[16] per program, so a note can layer at most this many tones.
  inline constexpr std::size_t kMaxTonesPerProgram = 16;

  // A decoded waveform and the loop the flag bytes describe.
  //
  // The one-shot path never needed this: a sound effect runs to the end block
  // and stops. A *sequence* holds notes, so the sustained part of a waveform has
  // to repeat, and the ambient beds in this game are single held notes over a
  // one-second loop. Without it the wind blows once and stops.
  struct WaveformPcm
  {
    std::vector<std::int16_t> samples;
    std::size_t loopStart = 0;
    bool loops = false;
    bool empty() const { return samples.empty(); }
  };

  // PS-ADPCM: 16-byte blocks, a shift/filter byte, a flag byte, then 28
  // nibbles. Of the flag byte, bit 0 ends the waveform, bit 1 says the end block
  // repeats rather than stopping, and bit 2 marks the block the repeat returns
  // to. A one-shot's last block is 0x07 -- all three at once, which is the
  // degenerate "loop back to my own start" the hardware treats as a stop.
  WaveformPcm decodePsAdpcm(std::span<const std::uint8_t> blocks);

  class SoundBank
  {
  public:
    // Parses a whole bank resource -- the 16-byte container header included.
    bool load(std::span<const std::uint8_t> resource);
    bool valid() const { return valid_; }

    // 128 slots always exist; this counts the ones with tones, which is the
    // VAB header's `ps` and what the header-length check keys off.
    std::size_t usedProgramCount() const;
    // == Tones layer ==
    //
    // A VAB program is a *set* of tones, and keying a note sounds **every** tone
    // whose note range covers it, not the first one. Returning only the first is
    // silently wrong on any program that layers, and this game layers a lot:
    // SND resource 112's wind is two tones over the same 0..120 range at pan 0
    // and pan 127 -- a stereo pair. Take only the first and the ambient bed
    // plays mono, hard left, which does not sound like wind at all. Boot bank 1
    // (resource 2) has 631 overlapping pairs and bank 2 (resource 3) has 8, so
    // the sound-effect path needs this as much as the sequencer does.
    //
    // Fills `out` with up to `max` matching tones and returns how many. 16 is
    // the most a program can hold.
    std::size_t tones(std::size_t program, std::uint8_t note, const VabTone **out,
                      std::size_t max) const;
    // The first tone covering a note. Reporting only -- playback must layer.
    const VabTone *tone(std::size_t program, std::uint8_t note) const;
    const VabProgram *program(std::size_t index) const;
    std::uint8_t masterVolume() const { return masterVolume_; }

    // Decoded PCM for a 1-based waveform index. Decoded on first use and kept,
    // because a bank is a few hundred kilobytes of ADPCM and the sound effects
    // reach only a handful of it.
    const WaveformPcm *waveform(std::int16_t index) const;

    // The rate a tone plays a note at, in Hz.
    static float FUN_00205e50_sample_rate(const VabTone &tone, std::uint8_t note);

    // The rate a tone plays a note at including a pitch-bend offset in cents.
    // Only the sequencer needs this; a sound-effect record carries a whole note
    // and nothing else.
    static float sampleRateWithBend(const VabTone &tone, std::uint8_t note, float cents);

    const std::string &diagnostic() const { return diagnostic_; }

  private:
    bool valid_ = false;
    std::uint8_t masterVolume_ = 127;
    std::vector<std::uint8_t> body_;
    std::vector<VabProgram> programs_;
    std::vector<std::size_t> waveformOffset_;
    std::vector<std::size_t> waveformSize_;
    mutable std::vector<WaveformPcm> decoded_;
    mutable std::vector<bool> decodedValid_;
    std::string diagnostic_;
  };

} // namespace orphen::ported::sound
