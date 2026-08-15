#pragma once

// VOICE.BIN's table of contents, which is what makes a cutscene keep time.
//
//   src/FUN_00221b90.c   the boot-time bootstrap that loads the table
//   src/FUN_00221c40.c   the lookup: bounds-check, then piGpffffbc30[voiceId]
//   src/FUN_00223698.c   the read: seek (entry >> 15) * 2048, take
//                        (entry & 0x7FFF) * 16 bytes
//   src/FUN_00206ae0.c   the caller, which caches the id per channel
//
// **This is not the same packing as the flat archives.** `FlatBinArchive` splits
// its entries 15 bits of sector over 17 bits of size-in-words; VOICE.BIN splits
// them 17 bits of sector over 15 bits of size-in-16-byte-units, the other way
// round. Reading one with the other's shifts yields plausible-looking offsets,
// so the split here is checked rather than assumed: the entries tile the file
// with no overlaps and end exactly at its length.
//
// The bootstrap is worth spelling out, because it looks circular. FUN_00221b90
// writes a *fake* entry 0 of 1 into the empty buffer, which decodes to "sector
// 0, 16 bytes", and reads that -- landing the file's first word, the entry
// count, in the buffer. It then rewrites that as `(count + 4) >> 2`, which is
// the whole table's length in 16-byte units, and reads again. Two reads with no
// table needed to find the table.
//
// ## Why the port wants it
//
// A line of dialogue holds for exactly as long as its voice clip. The record
// arms a clip with text control 0x16, starts it with 0x18, and then blocks on
// 0x1A until `DAT_00356788` falls back to zero. So the clip's *length* sets the
// pace of every cutscene, and length comes from this table alone -- the audio
// itself is only needed to hear it.
//
// That matters here because VOICE.BIN is 142 MiB and is not in this repo's disc
// root, while the table is 13 KB and is already sitting in both EE dumps, having
// been loaded at boot. `loadFromEeDump` reads it from there so the timing can be
// exact without the audio.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace orphen::ported::sound
{

  // FUN_00207010 programs the streaming voice's pitch register with 0x760.
  // SPU2 plays at the recorded rate when the register reads 0x1000 == 48000 Hz,
  // so 0x760 is 22125 Hz -- 22050 to within a third of a percent, and an exact
  // integer, which keeps the tick maths reproducible.
  inline constexpr std::uint32_t kVoicePitchRegister = 0x760;
  inline constexpr std::uint32_t kVoiceSampleRate = kVoicePitchRegister * 48000 / 4096;

  // SPU ADPCM: a 16-byte block carries 28 samples.
  inline constexpr std::uint32_t kAdpcmBlockBytes = 16;
  inline constexpr std::uint32_t kAdpcmBlockSamples = 28;

  class VoiceIndex
  {
  public:
    // FUN_00221b90 against the real file. Only the first sectors are read.
    bool loadFromVoiceBin(const std::filesystem::path &path);

    // The same table as the running game holds it. piGpffffbc30 is gp - 0x43D0
    // == 0x00355BA0, and a PS2 address is a file offset in a raw EE dump.
    bool loadFromEeDump(const std::filesystem::path &path);

    bool valid() const { return !entries_.empty(); }

    // Word 0 of the table, which FUN_00221c40 uses as its bound.
    std::uint32_t FUN_00221c40_entryCount() const { return entries_.empty() ? 0 : entries_.front(); }

    // FUN_00221c40. Returns 0 for an id the table cannot answer for; the
    // original calls FUN_0026bfc0("voice index over.") and dies.
    std::uint32_t FUN_00221c40_entry(std::uint32_t voiceId) const;

    std::uint32_t sizeBytes(std::uint32_t voiceId) const
    {
      return (FUN_00221c40_entry(voiceId) & 0x7FFFu) * kAdpcmBlockBytes;
    }
    std::uint64_t fileOffset(std::uint32_t voiceId) const
    {
      return static_cast<std::uint64_t>(FUN_00221c40_entry(voiceId) >> 15) * 2048u;
    }

    // How long 0x1A blocks for, in the frame ticks the rest of the port counts
    // in. Integer throughout so `--frames N` stays reproducible.
    std::uint32_t holdTicks(std::uint32_t voiceId) const;

    // Where the table came from, for the report.
    const std::string &source() const { return source_; }

    // True when the table came from VOICE.BIN itself, so the clips can be read
    // as well as measured. An EE dump gives lengths but no audio.
    bool hasAudio() const { return !audioPath_.empty(); }

    // The clip's stored bytes: raw SPU ADPCM, 16-byte blocks, no VAG header.
    // Empty when the id is unknown or no audio file backs the table.
    std::vector<std::uint8_t> readClipAdpcm(std::uint32_t voiceId) const;

    // Set when the entries do not tile a file cleanly. Non-fatal -- the table is
    // still used -- but it means the packing assumption is off and the holds
    // should not be trusted.
    const std::string &diagnostic() const { return diagnostic_; }

  private:
    bool adopt(std::vector<std::uint32_t> table, std::string source);

    std::vector<std::uint32_t> entries_;
    std::string source_;
    std::string diagnostic_;
    std::filesystem::path audioPath_;
  };

} // namespace orphen::ported::sound
