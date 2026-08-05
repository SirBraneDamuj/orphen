#include "harness/flat_bin_archive.h"

#include "ported/resource/headerless_lz_decoder.h"

#include <cstring>
#include <fstream>

namespace orphen::harness
{
  namespace
  {
    constexpr std::size_t kSectorBytes = 2048;

    std::uint32_t readU32(const std::vector<std::uint8_t> &data, std::size_t offset)
    {
      std::uint32_t value = 0;
      std::memcpy(&value, data.data() + offset, sizeof(value));
      return value;
    }
  } // namespace

  bool FlatBinArchive::open(const std::filesystem::path &path)
  {
    data_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 8)
    {
      return false;
    }
    file.seekg(0, std::ios::beg);

    data_.resize(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char *>(data_.data()), size);
    if (!file)
    {
      data_.clear();
      return false;
    }
    return true;
  }

  std::uint32_t FlatBinArchive::entryCount() const
  {
    return data_.size() >= 4 ? readU32(data_, 0) : 0;
  }

  std::vector<std::uint8_t> FlatBinArchive::decode(std::uint32_t resourceId) const
  {
    // FUN_00221b30 / FUN_00221b48: the entry word sits at resourceId * 4 from
    // the file start, not from the entry array.
    const std::size_t entryOffset = static_cast<std::size_t>(resourceId) * 4u;
    if (data_.size() < entryOffset + 4 || resourceId > entryCount())
    {
      return {};
    }

    const std::uint32_t entry = readU32(data_, entryOffset);
    const std::size_t byteOffset = static_cast<std::size_t>(entry >> 17) * kSectorBytes;
    const std::size_t packedBytes = static_cast<std::size_t>(entry & 0x1FFFFu) * 4u;
    if (packedBytes == 0 || byteOffset + packedBytes > data_.size())
    {
      return {};
    }

    return orphen::ported::resource::decodeHeaderlessLzStream(
        std::span<const std::uint8_t>(data_.data() + byteOffset, packedBytes));
  }

} // namespace orphen::harness
