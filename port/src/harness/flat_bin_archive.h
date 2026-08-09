#pragma once

// The flat table-of-contents archives: GRP / SCR / MAP / TEX / ITM / SND /
// VOICE.BIN. Distinct from MCB0/MCB1, which pair up into the per-scene bundles
// SceneResourceProvider reads.
//
//   src/FUN_00223268.c   the generic loader, switching on the archive index to
//                        pick a per-archive TOC lookup
//   src/FUN_00221b30.c   SCR.BIN's:  *(u32 *)(id * 4 + iGpffffbc1c)
//   src/FUN_00221b48.c   MAP.BIN's:  *(u32 *)(id * 4 + iGpffffbc28)
//
// Those gp variables hold the address the archive's first sector was loaded to,
// so the lookup indexes from the *start of the file* -- which is why the word at
// offset 0 is entry 0 and resource ids are effectively 1-based against the entry
// array proper. Getting this off by one silently returns a neighbouring
// resource, and neighbouring props share a topology, so it does not look wrong.
//
// Each entry packs an offset and a size:
//
//   bits [17:32]  sector index, sector = 2048 bytes
//   bits [ 0:17]  size in 32-bit words
//
// FUN_00223268:85 seeks to `(entry >> 17) << 11` and the payload is headerless
// LZ (FUN_002f3118).
//
// Verified against SCR.BIN id 0xBD, which decodes to 31292 bytes byte-identical
// to the entity descriptor blob at 0x00F3E760 in s01_e24.bin.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace orphen::harness
{

  class FlatBinArchive
  {
  public:
    // Reads the whole archive into memory. Returns false if the file is absent
    // or too small to hold a table; not fatal, callers report it.
    bool open(const std::filesystem::path &path);
    bool valid() const { return !data_.empty(); }

    // Word 0 of the file, which is also entry 0. The real game never indexes
    // past this, so it doubles as a bound.
    std::uint32_t entryCount() const;

    // Decompressed payload, or empty when the id is out of range, the entry is
    // empty, or the extent falls outside the file.
    std::vector<std::uint8_t> decode(std::uint32_t resourceId) const;

    // The stored bytes, undecompressed. FUN_00223268 only ever DMAs sectors --
    // every *caller* that wants a compressed resource runs FUN_002f3118 over
    // the result itself, and FUN_00205548, which reads SND.BIN, does not. So
    // sound banks are stored raw and need this rather than decode().
    std::vector<std::uint8_t> raw(std::uint32_t resourceId) const;

  private:
    std::vector<std::uint8_t> data_;
  };

} // namespace orphen::harness
