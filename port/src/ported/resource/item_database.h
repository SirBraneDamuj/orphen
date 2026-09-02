#pragma once

// The item database: names, descriptions and stat records, keyed by item id.
//
//   src/FUN_00228e28.c:119  loads it -- SCR.BIN resource 1, decompressed into
//                           the handle uGpffffade0, with its first eleven
//                           dwords relocated by the load address
//   src/FUN_00229688.c      the lookup
//   src/FUN_00229820.c      the wrapper the text system calls
//   src/FUN_002397f0.c      text control code 0x14, "insert the name of the
//                           item whose id is the next byte"
//
// The blob's dword 8 points at a table of per-group triples. Group 0 is the
// only one anything reaches:
//
//   [0]  stat records, 0x28 bytes each, indexed by item id
//   [1]  u32 per item id -> a u16 -> the name string
//   [2]  the same again for the description
//
// Both string layers are offsets from the blob's base, and the strings
// themselves are plain NUL-terminated ASCII -- item 64 reads "Blue Lantern".
// That is not true of dialogue text, which is a bytecode stream; the item
// tables are the simple case.
//
// The same blob also carries the *message streams*. FUN_0025b9e8 is
//
//     return *(int *)(index * 4 + *(int *)(blob + 0x14)) + blob;
//
// so dword 5 points at a table of stream offsets. The chest cutscene uses two
// of them: index 0 is the item-get line and index 1 is "The chest is empty."

#include "harness/flat_bin_archive.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::resource
{

  // FUN_00229688 param_4: the 0x28-byte stat record, unpacked exactly as that
  // function unpacks it -- the three ints at +0x0C/+0x10/+0x14 divided by 1000
  // into floats, everything else copied straight across at the same offsets.
  //
  // The battle module reads four things out of this. `kindByte` (+0x27) picks
  // which of FUN_002432d8 four button-mask fields the spell binds to, and the
  // first non-zero entry of `elementTable` (+0x18..+0x27) gives the element
  // index and its power: index 0 physical, 1 lightning, 2 wind, 4 fire, 5 dark,
  // 10 ice, matching the pentagon vertex order in FUN_0022ec30.
  //
  // Note that +0x27 is *both* the kind byte and the sixteenth element entry.
  // That overlap is what the original does -- FUN_002432d8 declares
  // `char acStack_e8[15]` immediately followed by `char cStack_d9` -- so the
  // two are not separated here either.
  struct ItemRecord
  {
    std::array<std::uint16_t, 3> ids{};      // +0x00, +0x02, +0x04
    std::uint8_t byte06 = 0;                 // +0x06
    std::uint8_t byte07 = 0;                 // +0x07, party record +0x14 + slot
    std::uint8_t byte08 = 0;                 // +0x08, party record +0x18 + slot*4 + 3
    std::uint8_t byte09 = 0;                 // +0x09
    float value0c = 0.0f;                    // +0x0C, raw / 1000
    float value10 = 0.0f;                    // +0x10, raw / 1000
    float value14 = 0.0f;                    // +0x14, raw / 1000
    std::array<std::uint8_t, 16> elementTable{}; // +0x18..+0x27

    std::int8_t kindByte() const { return static_cast<std::int8_t>(elementTable[15]); }
  };

  class ItemDatabase
  {
  public:
    // FUN_00228e28's SCR.BIN load. False when the archive is missing or the
    // blob does not look like the table; not fatal, callers report it.
    bool load(const std::filesystem::path &discRoot);
    bool loaded() const { return !blob_.empty(); }

    // FUN_00229688 with param_5 set: the item's name, empty when the id has
    // none. Ids run 0..0x7F; the table is sparse and most entries are empty.
    std::string FUN_00229688_name(std::int32_t itemId) const;
    std::string FUN_00229688_description(std::int32_t itemId) const;

    // FUN_0025b9e8: one message stream, or an empty span when the index is out
    // of range. The span runs to the end of the blob -- the stream terminates
    // itself with control code 0x01, which is the reader's job to spot.
    std::span<const std::uint8_t> FUN_0025b9e8_message(std::size_t index) const;

    // FUN_00229688 with param_4 set, which is how FUN_002432d8 reaches it
    // through the FUN_00229820 wrapper. Empty when the id is out of range.
    std::optional<ItemRecord> FUN_00229688_record(std::int32_t itemId) const;

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t recordTableOffset_ = 0;
    std::uint32_t nameTableOffset_ = 0;
    std::uint32_t descriptionTableOffset_ = 0;
    std::uint32_t messageTableOffset_ = 0;

    std::string stringAt(std::uint32_t tableOffset, std::int32_t itemId) const;
  };

} // namespace orphen::ported::resource
