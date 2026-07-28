#include "ported/psm2/fun_0022b5a8.h"

#include "ported/psm2/fun_0022c6e8.h"

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

        DRecord78 record78;
        record78.vertexIndices = {words[0], words[1], words[2], words[3]};
        record78.leadingWord = words[4] | (static_cast<std::uint32_t>(words[5]) << 16);
        record78.terrainFlags = words[10] | (static_cast<std::uint32_t>(words[11]) << 16);
        record78.selector = words[12];
        record78.byte12 = static_cast<std::uint8_t>(words[13] & 0xff);
        record78.byte13 = static_cast<std::uint8_t>((words[13] >> 8) & 0xff);

        DRecord80 record80;
        record80.vertexIndices = record78.vertexIndices;
        record80.sectionEIndex = words[6];
        record80.terrainFlags = record78.terrainFlags;

        state.DAT_003556b0_dRecords78.push_back(record78);
        state.DAT_003556ac_dRecords80.push_back(record80);
        recordOffset += 32;
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

  Psm2RuntimeState FUN_0022b5a8(std::span<const std::uint8_t> decodedPsm2)
  {
    requireRange(decodedPsm2, 0, 0x3c, "PSM2 header");
    if (readU32(decodedPsm2, 0) != kPsm2Magic)
    {
      throw std::runtime_error("decoded payload is not a PSM2 chunk");
    }

    Psm2RuntimeState state;

    loadSectionB(decodedPsm2, readU32(decodedPsm2, 0x30), state);
    loadSectionC(decodedPsm2, readU32(decodedPsm2, 0x08), state);
    loadSectionD(decodedPsm2, readU32(decodedPsm2, 0x0c), state);
    loadSectionE(decodedPsm2, readU32(decodedPsm2, 0x14), state);

    FUN_0022c6e8(state);

    state.stats.positionRecordCount = state.DAT_0035569c_sectionCRecords.size();
  state.stats.sectionBRecordCount = state.DAT_003556a4_sectionBRecords.size();
    state.stats.primitiveRecordCount = state.DAT_003556ac_dRecords80.size();
    state.stats.triangleCount = state.derivedTriangles.size();

    return state;
  }

} // namespace orphen::ported::psm2