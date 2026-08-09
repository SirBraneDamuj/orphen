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

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::resource
{

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

  private:
    std::vector<std::uint8_t> blob_;
    std::uint32_t nameTableOffset_ = 0;
    std::uint32_t descriptionTableOffset_ = 0;
    std::uint32_t messageTableOffset_ = 0;

    std::string stringAt(std::uint32_t tableOffset, std::int32_t itemId) const;
  };

} // namespace orphen::ported::resource
