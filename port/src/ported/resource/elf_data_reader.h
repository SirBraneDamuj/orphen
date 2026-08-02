#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::resource
{

  // Reads the game executable's data segment so that static tables compiled into
  // SLUS_200.11 can be looked up by their PS2 virtual address -- the same
  // addresses the decompiled sources use as DAT_* labels.
  //
  // This has no original counterpart: the PS2 has the ELF already loaded, so the
  // engine simply dereferences these addresses. It is the port's substitute for
  // that, and it reads the retail executable unmodified.
  //
  // SLUS_200.11 is a normal 32-bit little-endian MIPS ELF with a single PT_LOAD:
  //
  //   vaddr  0x00200000
  //   offset 0x00001000
  //   filesz 0x00155990   (memsz is larger; the tail is .bss and has no bytes)
  //
  // so the mapping is fileOffset = vaddr - 0x001FF000, valid up to 0x00355990.
  // Everything above that is zero-initialised at load and cannot be read here.
  class ElfDataReader
  {
  public:
    ElfDataReader() = default;

    // Parses the ELF headers and keeps the file contents. Throws on a malformed
    // or unexpected file; callers that want the port to degrade instead of fail
    // should use tryOpen.
    explicit ElfDataReader(std::vector<std::uint8_t> fileBytes);

    // Returns an empty optional rather than throwing when the file is missing or
    // is not the executable we expect.
    static std::optional<ElfDataReader> tryOpen(const std::string &path);

    bool valid() const { return !segments_.empty(); }

    // Bytes at a PS2 virtual address, or an empty span when the range is not
    // backed by file contents (out of range, or in .bss).
    std::span<const std::uint8_t> bytesAt(std::uint32_t virtualAddress, std::size_t byteCount) const;

    // Typed little-endian reads. Return the fallback when the address is not
    // readable, so callers can stay branch-light.
    std::uint8_t readU8(std::uint32_t virtualAddress, std::uint8_t fallback = 0) const;
    std::uint16_t readU16(std::uint32_t virtualAddress, std::uint16_t fallback = 0) const;
    std::int16_t readS16(std::uint32_t virtualAddress, std::int16_t fallback = 0) const;
    std::uint32_t readU32(std::uint32_t virtualAddress, std::uint32_t fallback = 0) const;
    float readF32(std::uint32_t virtualAddress, float fallback = 0.0f) const;

  private:
    struct LoadedSegment
    {
      std::uint32_t virtualAddress = 0;
      std::uint32_t fileOffset = 0;
      std::uint32_t fileSize = 0; // p_filesz only; the .bss tail is not readable
    };

    std::vector<std::uint8_t> fileBytes_;
    std::vector<LoadedSegment> segments_;
  };

} // namespace orphen::ported::resource
