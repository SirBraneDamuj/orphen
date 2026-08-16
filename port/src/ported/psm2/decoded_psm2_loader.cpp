#include "ported/psm2/decoded_psm2_loader.h"

#include "ported/psm2/psm2_geometry_builder.h"
#include "ported/psm2/psm2_material_expansion.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace orphen::ported::psm2
{
  namespace
  {

    constexpr std::uint32_t kPsm2Magic = 0x324d5350;

    void requireRange(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t byteCount, const char *label)
    {
      if (offset > bytes.size() || byteCount > bytes.size() - offset)
      {
        throw std::runtime_error(std::string("PSM2 read outside buffer while reading ") + label);
      }
    }

    std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      requireRange(bytes, offset, sizeof(std::uint16_t), "u16");
      return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    std::int16_t readS16(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int16_t>(readU16(bytes, offset));
    }

    std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      requireRange(bytes, offset, sizeof(std::uint32_t), "u32");
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    float readF32(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      const std::uint32_t rawValue = readU32(bytes, offset);
      float value = 0.0f;
      std::memcpy(&value, &rawValue, sizeof(value));
      return value;
    }

    std::size_t positiveCount(std::int16_t count)
    {
      return count > 0 ? static_cast<std::size_t>(count) : 0;
    }

    void loadSectionB(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::uint32_t rawCount = readU32(decodedPsm2, sectionBase) & 0xffff;
      const std::size_t recordCount = rawCount >= 0x8000 ? 0 : static_cast<std::size_t>(rawCount);
      std::size_t recordOffset = sectionBase + 4;

      state.DAT_003556a4_sectionBRecords.reserve(recordCount);
      for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
      {
        requireRange(decodedPsm2, recordOffset, 12, "PSM2 section B record");
        SectionBRecord record;
        record.normal = {readF32(decodedPsm2, recordOffset),
                         readF32(decodedPsm2, recordOffset + 4),
                         readF32(decodedPsm2, recordOffset + 8)};
        state.DAT_003556a4_sectionBRecords.push_back(record);
        recordOffset += 12;
      }
    }

    void loadSectionC(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::size_t recordCount = positiveCount(readS16(decodedPsm2, sectionBase));
      std::size_t recordOffset = sectionBase + 2;

      state.DAT_0035569c_sectionCRecords.reserve(recordCount);
      for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
      {
        requireRange(decodedPsm2, recordOffset, 16, "PSM2 section C record");
        SectionCRecord record;
        record.position = {readF32(decodedPsm2, recordOffset),
                           readF32(decodedPsm2, recordOffset + 4),
                           readF32(decodedPsm2, recordOffset + 8)};
        record.sectionBIndex = readU16(decodedPsm2, recordOffset + 12);
        record.styleFlags = decodedPsm2[recordOffset + 14];
        state.DAT_0035569c_sectionCRecords.push_back(record);
        recordOffset += 16;
      }
    }

    void loadSectionD(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::size_t recordCount = positiveCount(readS16(decodedPsm2, sectionBase));
      std::size_t recordOffset = sectionBase + 4;

      state.DAT_003556ac_dRecords80.reserve(recordCount);
      state.DAT_003556b0_dRecords78.reserve(recordCount);

      for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
      {
        requireRange(decodedPsm2, recordOffset, 32, "PSM2 section D record");
        std::array<std::uint16_t, 16> words{};
        for (std::size_t wordIndex = 0; wordIndex < words.size(); ++wordIndex)
        {
          words[wordIndex] = readU16(decodedPsm2, recordOffset + wordIndex * 2);
        }

        // Field assignment follows FUN_0022b5a8:191-244 word for word. w4 is
        // the flag word on its own -- the original zero-extends it into both
        // 0x78 +0x00 and 0x80 +0x70 and leaves the high half for runtime bits.
        // w5 is a colour index, not the top half of the flags.
        DRecord78 record78;
        record78.vertexIndices = {words[0], words[1], words[2], words[3]};
        record78.leadingWord = words[4];
        record78.terrainFlags = words[10] | (static_cast<std::uint32_t>(words[11]) << 16);
        record78.selector = words[12];
        record78.byte12 = static_cast<std::uint8_t>(words[13] & 0xff);
        record78.byte13 = static_cast<std::uint8_t>((words[13] >> 8) & 0xff);

        DRecord80 record80;
        record80.vertexIndices = record78.vertexIndices;
        record80.colourIndex = words[5];
        record80.blendParam = static_cast<std::uint8_t>(words[14] & 0xff);
        record80.staticAlpha = static_cast<std::uint8_t>((words[14] >> 8) & 0xff);
        record80.normalIndex = words[15];
        record80.primitiveFlags = record78.leadingWord;
        if (static_cast<std::size_t>(record80.normalIndex) < state.DAT_003556a4_sectionBRecords.size())
        {
          record80.normal = state.DAT_003556a4_sectionBRecords[record80.normalIndex].normal;
        }

        // w6..w9 land in the first halfword of each material slot, which
        // FUN_0022c3d8 then expands in place. They are kept alongside the
        // expanded slot rather than inside it because the expansion
        // overwrites those bytes.
        record80.slotSelectors = {static_cast<std::int16_t>(words[6]),
                                  static_cast<std::int16_t>(words[7]),
                                  static_cast<std::int16_t>(words[8]),
                                  static_cast<std::int16_t>(words[9])};

        state.DAT_003556b0_dRecords78.push_back(record78);
        state.DAT_003556ac_dRecords80.push_back(record80);
        recordOffset += 32;
      }
    }

    // FUN_0022b5a8's tail: PSM2 header word 13 points at a count dword followed
    // by count 16-byte records, which the original stores at DAT_003556e8 with
    // the count in DAT_003556e4. This is the map's object placement table --
    // where scene objects stand -- and script opcode 0x51 is what turns entries
    // into entities. Note the section is only 2-byte aligned in practice, which
    // is why the original reads it a dword at a time through FUN_0022b4e0
    // rather than casting.
    // FUN_0022b5a8:305-325. PSM2 header word 6: 0x1000 int16 cell heads copied
    // straight into DAT_00343a18, then a length at +0x2000, then that many
    // int16 primitive indices copied to DAT_003556f0. The two shorts at +0x2002
    // are skipped -- the original advances its cursor by 3 after the grid.
    //
    // Verified byte for byte against eeMemory.bin: out/mapbin/0001.psm2 gives
    // 4096/4096 grid entries and 1022/1022 list entries identical to the live
    // arrays in the dump.
    void loadCollisionGrid(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t gridBytes = kCollisionGridCells * 2;
      requireRange(decodedPsm2, sectionOffset, gridBytes + 4, "PSM2 collision grid");

      state.DAT_00343a18_collisionGrid.resize(kCollisionGridCells);
      for (std::size_t cellIndex = 0; cellIndex < kCollisionGridCells; ++cellIndex)
      {
        state.DAT_00343a18_collisionGrid[cellIndex] = readS16(decodedPsm2, sectionOffset + cellIndex * 2);
      }

      const std::size_t listLength = positiveCount(readS16(decodedPsm2, sectionOffset + gridBytes));
      const std::size_t listBase = sectionOffset + gridBytes + 4;
      requireRange(decodedPsm2, listBase, listLength * 2, "PSM2 collision cell list");

      state.DAT_003556f0_collisionCellList.resize(listLength);
      for (std::size_t entryIndex = 0; entryIndex < listLength; ++entryIndex)
      {
        state.DAT_003556f0_collisionCellList[entryIndex] = readS16(decodedPsm2, listBase + entryIndex * 2);
      }

      state.stats.collisionCellListLength = listLength;
      state.stats.occupiedCollisionCells = static_cast<std::size_t>(
          std::count_if(state.DAT_00343a18_collisionGrid.begin(),
                        state.DAT_00343a18_collisionGrid.end(),
                        [](std::int16_t head) { return head >= 0; }));
    }

    // FUN_0022b5a8:56-89. Header word 1. The file packs 24 bytes per record; the
    // original blits them into a 0x20-stride slot and zeroes the tail, so only
    // the first 24 bytes are file data. `4 + count * 24` lands exactly on the
    // next section, which is how the stride is confirmed.
    void loadCollisionDescriptors(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }
      requireRange(decodedPsm2, sectionOffset, 4, "PSM2 collision descriptor header");
      const std::size_t count = positiveCount(readS16(decodedPsm2, sectionOffset));
      const std::size_t base = sectionOffset + 4;
      requireRange(decodedPsm2, base, count * 24, "PSM2 collision descriptors");

      state.DAT_003556d8_collisionDescriptors.resize(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        CollisionDescriptor descriptor;
        descriptor.firstPrimitive = readU16(decodedPsm2, base + index * 24 + 4);
        descriptor.primitiveCount = readU16(decodedPsm2, base + index * 24 + 6);
        state.DAT_003556d8_collisionDescriptors[index] = descriptor;
      }
    }

    // FUN_0022b5a8:443-517. Header word 7: an int16 count then 14 int16 per
    // group. Only the first two matter to the terrain scan; the rest is the
    // group's pivot and extents, which drive the render-side update the port
    // does not have.
    void loadCollisionGroups(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }
      requireRange(decodedPsm2, sectionOffset, 2, "PSM2 collision group header");
      const std::size_t count = positiveCount(readS16(decodedPsm2, sectionOffset));
      const std::size_t base = sectionOffset + 2;
      requireRange(decodedPsm2, base, count * 28, "PSM2 collision groups");

      state.DAT_003556e0_collisionGroups.resize(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        CollisionGroup group;
        group.descriptorIndex = readU16(decodedPsm2, base + index * 28);
        group.type = readS16(decodedPsm2, base + index * 28 + 2);
        state.DAT_003556e0_collisionGroups[index] = group;
      }
    }

    // FUN_00227840 rejects a group on the XY box the original recomputes every
    // frame. For a group that never moves that box is the union of its own
    // primitives' bounds, so it can be resolved once at load. Must run after the
    // geometry pass, which is what fills those bounds.
    void resolveCollisionGroupBounds(Psm2RuntimeState &state)
    {
      for (auto &group : state.DAT_003556e0_collisionGroups)
      {
        if (group.descriptorIndex >= state.DAT_003556d8_collisionDescriptors.size())
        {
          continue;
        }
        const auto &descriptor = state.DAT_003556d8_collisionDescriptors[group.descriptorIndex];
        group.firstPrimitive = descriptor.firstPrimitive;
        group.primitiveCount = descriptor.primitiveCount;

        for (std::size_t offset = 0; offset < group.primitiveCount; ++offset)
        {
          const std::size_t primitiveIndex = static_cast<std::size_t>(group.firstPrimitive) + offset;
          if (primitiveIndex >= state.DAT_003556b0_dRecords78.size())
          {
            break;
          }
          const auto &bounds = state.DAT_003556b0_dRecords78[primitiveIndex].bounds;
          if (!bounds.valid)
          {
            continue;
          }
          if (!group.boundsValid)
          {
            group.minX = bounds.min.x;
            group.maxX = bounds.max.x;
            group.minY = bounds.min.y;
            group.maxY = bounds.max.y;
            group.boundsValid = true;
            continue;
          }
          group.minX = std::min(group.minX, bounds.min.x);
          group.maxX = std::max(group.maxX, bounds.max.x);
          group.minY = std::min(group.minY, bounds.min.y);
          group.maxY = std::max(group.maxY, bounds.max.y);
        }
      }
    }

    void loadObjectPlacements(std::span<const std::uint8_t> decodedPsm2,
                              std::uint32_t sectionOffset,
                              Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::int16_t rawCount = static_cast<std::int16_t>(readU32(decodedPsm2, sectionBase) & 0xffff);
      const std::size_t count = positiveCount(rawCount);
      state.DAT_003556e8_objectPlacements.reserve(count);

      for (std::size_t index = 0; index < count; ++index)
      {
        const std::size_t recordBase = sectionBase + 4 + index * 0x10;
        ObjectPlacementRecord record;
        record.position.x = readF32(decodedPsm2, recordBase + 0x00);
        record.position.y = readF32(decodedPsm2, recordBase + 0x04);
        record.position.z = readF32(decodedPsm2, recordBase + 0x08);
        record.angle = static_cast<std::int8_t>(decodedPsm2[recordBase + 0x0C]);
        record.group = static_cast<std::int8_t>(decodedPsm2[recordBase + 0x0D]);
        record.id = static_cast<std::int8_t>(decodedPsm2[recordBase + 0x0E]);
        record.param = decodedPsm2[recordBase + 0x0F];
        state.DAT_003556e8_objectPlacements.push_back(record);
      }
    }

    // FUN_0022b5a8:535-563: an s16 count followed by count RGB triples, staged
    // at DAT_00355bdc with the count in DAT_00355be0. FUN_0022c3d8 turns
    // 0x80-record indices into per-vertex colours out of this.
    void loadPalette(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::size_t colourCount = positiveCount(readS16(decodedPsm2, sectionBase));
      requireRange(decodedPsm2, sectionBase + 2, colourCount * 3, "PSM2 colour palette");

      state.DAT_00355bdc_palette.reserve(colourCount);
      for (std::size_t colourIndex = 0; colourIndex < colourCount; ++colourIndex)
      {
        const std::size_t base = sectionBase + 2 + colourIndex * 3;
        state.DAT_00355bdc_palette.push_back({decodedPsm2[base], decodedPsm2[base + 1], decodedPsm2[base + 2]});
      }
    }

    void loadSectionE(std::span<const std::uint8_t> decodedPsm2, std::uint32_t sectionOffset, Psm2RuntimeState &state)
    {
      if (sectionOffset == 0)
      {
        return;
      }

      const std::size_t sectionBase = sectionOffset;
      const std::uint16_t rawCount = readU16(decodedPsm2, sectionBase);
      const std::size_t recordCount = rawCount >= 0x8000 ? 0 : static_cast<std::size_t>(rawCount);
      std::size_t recordOffset = sectionBase + 2;

      state.DAT_003556b4_sectionERecords.reserve(recordCount);
      for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
      {
        requireRange(decodedPsm2, recordOffset, 12, "PSM2 section E record");
        SectionERecord record;
        std::memcpy(record.bytes.data(), decodedPsm2.data() + recordOffset, record.bytes.size());
        state.DAT_003556b4_sectionERecords.push_back(record);
        recordOffset += 12;
      }
    }

  } // namespace

  Psm2RuntimeState loadDecodedPsm2(std::span<const std::uint8_t> decodedPsm2)
  {
    requireRange(decodedPsm2, 0, 0x3c, "PSM2 header");
    if (readU32(decodedPsm2, 0) != kPsm2Magic)
    {
      throw std::runtime_error("decoded payload is not a PSM2 chunk");
    }

    Psm2RuntimeState state;

    // Section B before section D: FUN_0022b5a8 resolves each primitive's face
    // normal out of B as it parses D.
    loadSectionB(decodedPsm2, readU32(decodedPsm2, 0x30), state);
    loadSectionC(decodedPsm2, readU32(decodedPsm2, 0x08), state);
    loadSectionD(decodedPsm2, readU32(decodedPsm2, 0x0c), state);
    loadSectionE(decodedPsm2, readU32(decodedPsm2, 0x14), state);
    loadPalette(decodedPsm2, readU32(decodedPsm2, 0x10), state);
    loadCollisionGrid(decodedPsm2, readU32(decodedPsm2, 0x18), state);
    loadCollisionDescriptors(decodedPsm2, readU32(decodedPsm2, 0x04), state);
    loadCollisionGroups(decodedPsm2, readU32(decodedPsm2, 0x1c), state);
    loadObjectPlacements(decodedPsm2, readU32(decodedPsm2, 0x34), state);

    // FUN_0022b5a8's tail order: FUN_0022c3d8 then FUN_0022c6e8.
    expandPsm2Materials(state);
    buildPsm2DerivedGeometry(state);

    // Needs the bounds the geometry pass just filled in.
    resolveCollisionGroupBounds(state);

    state.stats.positionRecordCount = state.DAT_0035569c_sectionCRecords.size();
    state.stats.sectionBRecordCount = state.DAT_003556a4_sectionBRecords.size();
    state.stats.primitiveRecordCount = state.DAT_003556ac_dRecords80.size();
    state.stats.paletteColourCount = state.DAT_00355bdc_palette.size();
    state.stats.triangleCount = state.derivedTriangles.size();
    state.stats.objectPlacementCount = state.DAT_003556e8_objectPlacements.size();

    return state;
  }

} // namespace orphen::ported::psm2
