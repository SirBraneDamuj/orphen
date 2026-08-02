#include "ported/resource/elf_data_reader.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace orphen::ported::resource
{
  namespace
  {

    constexpr std::size_t kElfIdentSize = 16;
    constexpr std::uint32_t kProgramHeaderTypeLoad = 1;

    // ELF32 header field offsets.
    constexpr std::size_t kElfPhoffOffset = 0x1C;
    constexpr std::size_t kElfPhentsizeOffset = 0x2A;
    constexpr std::size_t kElfPhnumOffset = 0x2C;

    // ELF32 program header field offsets.
    constexpr std::size_t kProgramTypeOffset = 0x00;
    constexpr std::size_t kProgramOffsetOffset = 0x04;
    constexpr std::size_t kProgramVaddrOffset = 0x08;
    constexpr std::size_t kProgramFileszOffset = 0x10;

    std::uint16_t readLittleU16(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    std::uint32_t readLittleU32(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

  } // namespace

  ElfDataReader::ElfDataReader(std::vector<std::uint8_t> fileBytes)
      : fileBytes_(std::move(fileBytes))
  {
    const std::span<const std::uint8_t> bytes{fileBytes_};
    if (bytes.size() < kElfIdentSize + 0x30)
    {
      throw std::runtime_error("ELF too small to contain a header");
    }
    if (bytes[0] != 0x7F || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F')
    {
      throw std::runtime_error("missing ELF magic");
    }
    if (bytes[4] != 1 || bytes[5] != 1)
    {
      throw std::runtime_error("expected a 32-bit little-endian ELF");
    }

    const std::uint32_t programHeaderOffset = readLittleU32(bytes, kElfPhoffOffset);
    const std::uint16_t programHeaderSize = readLittleU16(bytes, kElfPhentsizeOffset);
    const std::uint16_t programHeaderCount = readLittleU16(bytes, kElfPhnumOffset);

    if (programHeaderSize < 0x20)
    {
      throw std::runtime_error("program header entries are too small");
    }

    for (std::uint16_t index = 0; index < programHeaderCount; ++index)
    {
      const std::size_t entry =
          static_cast<std::size_t>(programHeaderOffset) + static_cast<std::size_t>(index) * programHeaderSize;
      if (entry + programHeaderSize > bytes.size())
      {
        break;
      }
      if (readLittleU32(bytes, entry + kProgramTypeOffset) != kProgramHeaderTypeLoad)
      {
        continue;
      }

      LoadedSegment segment;
      segment.fileOffset = readLittleU32(bytes, entry + kProgramOffsetOffset);
      segment.virtualAddress = readLittleU32(bytes, entry + kProgramVaddrOffset);
      segment.fileSize = readLittleU32(bytes, entry + kProgramFileszOffset);

      // Anything past p_filesz is .bss: zero at load time, with no bytes in the
      // file to read. Clamp rather than trusting the header against a truncated
      // file.
      if (segment.fileOffset >= bytes.size())
      {
        continue;
      }
      const std::uint32_t available = static_cast<std::uint32_t>(bytes.size()) - segment.fileOffset;
      if (segment.fileSize > available)
      {
        segment.fileSize = available;
      }
      if (segment.fileSize == 0)
      {
        continue;
      }

      segments_.push_back(segment);
    }

    if (segments_.empty())
    {
      throw std::runtime_error("ELF contains no loadable segment with file contents");
    }
  }

  std::optional<ElfDataReader> ElfDataReader::tryOpen(const std::string &path)
  {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
      return std::nullopt;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= 0)
    {
      return std::nullopt;
    }
    stream.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> fileBytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char *>(fileBytes.data()), size);
    if (!stream)
    {
      return std::nullopt;
    }

    try
    {
      return ElfDataReader(std::move(fileBytes));
    }
    catch (const std::exception &)
    {
      return std::nullopt;
    }
  }

  std::span<const std::uint8_t> ElfDataReader::bytesAt(std::uint32_t virtualAddress,
                                                      std::size_t byteCount) const
  {
    for (const LoadedSegment &segment : segments_)
    {
      if (virtualAddress < segment.virtualAddress)
      {
        continue;
      }
      const std::uint32_t offsetInSegment = virtualAddress - segment.virtualAddress;
      if (offsetInSegment >= segment.fileSize)
      {
        continue;
      }
      if (byteCount > segment.fileSize - offsetInSegment)
      {
        continue;
      }
      return std::span<const std::uint8_t>(fileBytes_).subspan(segment.fileOffset + offsetInSegment,
                                                               byteCount);
    }
    return {};
  }

  std::uint8_t ElfDataReader::readU8(std::uint32_t virtualAddress, std::uint8_t fallback) const
  {
    const auto bytes = bytesAt(virtualAddress, 1);
    return bytes.empty() ? fallback : bytes[0];
  }

  std::uint16_t ElfDataReader::readU16(std::uint32_t virtualAddress, std::uint16_t fallback) const
  {
    const auto bytes = bytesAt(virtualAddress, 2);
    return bytes.empty() ? fallback : readLittleU16(bytes, 0);
  }

  std::int16_t ElfDataReader::readS16(std::uint32_t virtualAddress, std::int16_t fallback) const
  {
    const auto bytes = bytesAt(virtualAddress, 2);
    return bytes.empty() ? fallback : static_cast<std::int16_t>(readLittleU16(bytes, 0));
  }

  std::uint32_t ElfDataReader::readU32(std::uint32_t virtualAddress, std::uint32_t fallback) const
  {
    const auto bytes = bytesAt(virtualAddress, 4);
    return bytes.empty() ? fallback : readLittleU32(bytes, 0);
  }

  float ElfDataReader::readF32(std::uint32_t virtualAddress, float fallback) const
  {
    const auto bytes = bytesAt(virtualAddress, 4);
    if (bytes.empty())
    {
      return fallback;
    }
    const std::uint32_t raw = readLittleU32(bytes, 0);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

} // namespace orphen::ported::resource
