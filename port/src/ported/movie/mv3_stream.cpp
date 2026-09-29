#include "ported/movie/mv3_stream.h"

#include <cstring>

namespace orphen::ported::movie
{

  namespace
  {
    std::uint32_t readLe32(const std::uint8_t *at)
    {
      return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) |
             (static_cast<std::uint32_t>(at[2]) << 16) | (static_cast<std::uint32_t>(at[3]) << 24);
    }
  } // namespace

  bool Mv3Stream::open(const std::filesystem::path &path)
  {
    file_.close();
    header_ = {};
    blocksRead_ = 0;
    blockCount_ = 0;
    diagnostic_.clear();

    file_.open(path, std::ios::binary);
    if (!file_)
    {
      diagnostic_ = "cannot open " + path.string();
      return false;
    }

    std::uint8_t sector[kMv3SectorBytes] = {};
    file_.read(reinterpret_cast<char *>(sector), sizeof(sector));
    if (file_.gcount() != static_cast<std::streamsize>(sizeof(sector)) || readLe32(sector) != kMv3Magic)
    {
      diagnostic_ = "no MV30 header";
      file_.close();
      return false;
    }

    header_.blk_byte = readLe32(sector + 0x04);
    header_.pcm_ofs = readLe32(sector + 0x08);
    header_.pcm_1sz = readLe32(sector + 0x0C);
    header_.mpg_ofs = readLe32(sector + 0x10);
    header_.mpg_1sz = readLe32(sector + 0x14);

    // FUN_002F1A70's two checks, plus the one it gets for free from the rings
    // being carved out of a single block: both slices have to lie inside it.
    if (static_cast<std::uint64_t>(header_.mpg_1sz) * kMpegRingChunks > kMpegRingBytes ||
        static_cast<std::uint64_t>(header_.pcm_1sz) * kPcmRingChunks > kPcmRingBytes ||
        header_.blk_byte == 0 ||
        static_cast<std::uint64_t>(header_.pcm_ofs) + header_.pcm_1sz > header_.blk_byte ||
        static_cast<std::uint64_t>(header_.mpg_ofs) + header_.mpg_1sz > header_.blk_byte)
    {
      diagnostic_ = "mpg_block size over.";
      file_.close();
      return false;
    }

    file_.seekg(0, std::ios::end);
    const std::uint64_t fileBytes = static_cast<std::uint64_t>(file_.tellg());
    file_.seekg(kMv3SectorBytes, std::ios::beg);
    blockCount_ = fileBytes > kMv3SectorBytes
                      ? static_cast<std::uint32_t>((fileBytes - kMv3SectorBytes) / header_.blk_byte)
                      : 0;
    block_.resize(header_.blk_byte);
    return true;
  }

  bool Mv3Stream::readBlock(std::vector<std::uint8_t> &mpeg, std::vector<std::int16_t> &pcm)
  {
    if (!file_.is_open() || blocksRead_ >= blockCount_)
    {
      return false;
    }
    file_.read(reinterpret_cast<char *>(block_.data()), static_cast<std::streamsize>(block_.size()));
    if (file_.gcount() != static_cast<std::streamsize>(block_.size()))
    {
      return false;
    }
    ++blocksRead_;

    mpeg.insert(mpeg.end(), block_.begin() + header_.mpg_ofs,
                block_.begin() + header_.mpg_ofs + header_.mpg_1sz);
    unstripePcm(block_.data() + header_.pcm_ofs, header_.pcm_1sz, pcm);
    return true;
  }

  void unstripePcm(const std::uint8_t *slice, std::size_t bytes, std::vector<std::int16_t> &out)
  {
    constexpr std::size_t kSamplesPerStripe = kPcmStripeBytes / 2;
    const std::size_t pairs = bytes / (kPcmStripeBytes * 2);
    const std::size_t base = out.size();
    out.resize(base + pairs * kSamplesPerStripe * 2);
    std::int16_t *write = out.data() + base;
    for (std::size_t pair = 0; pair < pairs; ++pair)
    {
      const std::uint8_t *left = slice + pair * kPcmStripeBytes * 2;
      const std::uint8_t *right = left + kPcmStripeBytes;
      for (std::size_t sample = 0; sample < kSamplesPerStripe; ++sample)
      {
        *write++ = static_cast<std::int16_t>(left[sample * 2] | (left[sample * 2 + 1] << 8));
        *write++ = static_cast<std::int16_t>(right[sample * 2] | (right[sample * 2 + 1] << 8));
      }
    }
  }

} // namespace orphen::ported::movie
