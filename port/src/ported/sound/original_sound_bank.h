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

  // SPU2 plays a waveform at its recorded rate when the pitch register reads
  // 0x1000, and the register is 48 kHz there (the PS1's SPU was 44.1).
  inline constexpr float kSpuBaseSampleRate = 48000.0f;

  // PS-ADPCM: 16-byte blocks, a shift/filter byte, a flag byte, then 28
  // nibbles. Bit 0 of the flags ends the waveform.
  std::vector<std::int16_t> decodePsAdpcm(std::span<const std::uint8_t> blocks);

  class SoundBank
  {
  public:
    // Parses a whole bank resource -- the 16-byte container header included.
    bool load(std::span<const std::uint8_t> resource);
    bool valid() const { return valid_; }

    // 128 slots always exist; this counts the ones with tones, which is the
    // VAB header's `ps` and what the header-length check keys off.
    std::size_t usedProgramCount() const;
    // The tone a note selects, or nullopt when the program has none covering it.
    const VabTone *tone(std::size_t program, std::uint8_t note) const;
    const VabProgram *program(std::size_t index) const;
    std::uint8_t masterVolume() const { return masterVolume_; }

    // Decoded PCM for a 1-based waveform index. Decoded on first use and kept,
    // because a bank is a few hundred kilobytes of ADPCM and the sound effects
    // reach only a handful of it.
    const std::vector<std::int16_t> *waveform(std::int16_t index) const;

    // The rate a tone plays a note at, in Hz.
    static float FUN_00205e50_sample_rate(const VabTone &tone, std::uint8_t note);

    const std::string &diagnostic() const { return diagnostic_; }

  private:
    bool valid_ = false;
    std::uint8_t masterVolume_ = 127;
    std::vector<std::uint8_t> body_;
    std::vector<VabProgram> programs_;
    std::vector<std::size_t> waveformOffset_;
    std::vector<std::size_t> waveformSize_;
    mutable std::vector<std::vector<std::int16_t>> decoded_;
    mutable std::vector<bool> decodedValid_;
    std::string diagnostic_;
  };

} // namespace orphen::ported::sound
