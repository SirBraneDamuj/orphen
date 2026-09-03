#include "ported/sound/original_voice_index.h"

#include "ported/original_frame_timing.h"

#include <cstdio>
#include <cstring>
#include <sstream>

namespace orphen::ported::sound
{
  namespace
  {
    // piGpffffbc30. gp is 0x00359F70 (scripts/gp_address_calculator.py) and the
    // Ghidra suffix is the signed displacement, so 0xFFFFBC30 is -0x43D0.
    constexpr std::uint64_t kPiGpffffbc30 = 0x00355BA0;

    // A voice table with more entries than this is not a voice table. The retail
    // disc has 3310.
    constexpr std::uint32_t kMaxPlausibleEntries = 65536;

    bool readWords(const std::filesystem::path &path,
                   std::uint64_t offset,
                   std::size_t words,
                   std::vector<std::uint32_t> &out)
    {
      std::FILE *file = nullptr;
#if defined(_MSC_VER)
      if (::fopen_s(&file, path.string().c_str(), "rb") != 0)
      {
        file = nullptr;
      }
#else
      file = std::fopen(path.string().c_str(), "rb");
#endif
      if (file == nullptr)
      {
        return false;
      }
      bool ok = false;
      if (std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0)
      {
        out.assign(words, 0);
        ok = std::fread(out.data(), 4, words, file) == words;
      }
      std::fclose(file);
      return ok;
    }
  } // namespace

  bool VoiceIndex::adopt(std::vector<std::uint32_t> table, std::string source)
  {
    if (table.empty() || table.front() == 0 || table.front() >= kMaxPlausibleEntries ||
        table.size() < static_cast<std::size_t>(table.front()) + 1)
    {
      return false;
    }

    // Check the packing rather than trust it: entries 1..count must ascend and
    // never overlap. Sector alignment leaves gaps, so only overlap is an error.
    std::uint64_t previousEnd = 0;
    std::size_t overlaps = 0;
    for (std::uint32_t id = 1; id <= table.front(); ++id)
    {
      const std::uint64_t offset = static_cast<std::uint64_t>(table[id] >> 15) * 2048u;
      const std::uint64_t size = static_cast<std::uint64_t>(table[id] & 0x7FFFu) * kAdpcmBlockBytes;
      if (offset < previousEnd)
      {
        ++overlaps;
      }
      previousEnd = offset + size;
    }

    entries_ = std::move(table);
    source_ = std::move(source);
    diagnostic_.clear();
    if (overlaps != 0)
    {
      std::ostringstream text;
      text << overlaps << " of " << entries_.front()
           << " entries overlap their predecessor -- the sector/size split looks wrong";
      diagnostic_ = text.str();
    }
    return true;
  }

  bool VoiceIndex::loadFromVoiceBin(const std::filesystem::path &path)
  {
    // FUN_00221b90's first read, which only needs the file's leading word.
    std::vector<std::uint32_t> head;
    if (!readWords(path, 0, 1, head))
    {
      return false;
    }
    const std::uint32_t count = head.front();
    if (count == 0 || count >= kMaxPlausibleEntries)
    {
      return false;
    }

    // ...and its second, sized the way the original sizes it.
    const std::size_t words = static_cast<std::size_t>(((count + 4) >> 2) * kAdpcmBlockBytes) / 4;
    std::vector<std::uint32_t> table;
    if (!readWords(path, 0, words, table))
    {
      return false;
    }
    if (!adopt(std::move(table), path.filename().string()))
    {
      return false;
    }
    audioPath_ = path;
    return true;
  }

  std::vector<std::uint8_t> VoiceIndex::readClipAdpcm(std::uint32_t voiceId) const
  {
    const std::uint32_t size = sizeBytes(voiceId);
    if (audioPath_.empty() || size == 0)
    {
      return {};
    }
    // FUN_00223698 reads the whole clip in one go and FUN_00206ae0 rejects
    // anything over 0x50000, which is the per-channel buffer.
    constexpr std::uint32_t kChannelBufferBytes = 0x50000;
    if (size > kChannelBufferBytes)
    {
      return {};
    }

    std::vector<std::uint32_t> words;
    if (!readWords(audioPath_, fileOffset(voiceId), size / 4, words))
    {
      return {};
    }
    std::vector<std::uint8_t> bytes(size);
    std::memcpy(bytes.data(), words.data(), size);
    return bytes;
  }

  bool VoiceIndex::loadFromEeDump(const std::filesystem::path &path)
  {
    std::vector<std::uint32_t> pointer;
    if (!readWords(path, kPiGpffffbc30, 1, pointer))
    {
      return false;
    }
    const std::uint32_t base = pointer.front();
    // EE RAM is 32 MiB and the heap this lands in sits well above the ELF.
    if (base < 0x00400000u || base >= 0x02000000u)
    {
      return false;
    }

    std::vector<std::uint32_t> head;
    if (!readWords(path, base, 1, head))
    {
      return false;
    }
    const std::uint32_t count = head.front();
    if (count == 0 || count >= kMaxPlausibleEntries)
    {
      return false;
    }

    std::vector<std::uint32_t> table;
    if (!readWords(path, base, static_cast<std::size_t>(count) + 1, table))
    {
      return false;
    }
    return adopt(std::move(table), path.filename().string() + " (boot-loaded table)");
  }

  std::uint32_t VoiceIndex::FUN_00221c40_entry(std::uint32_t voiceId) const
  {
    // The original's bound is `count < id`, so id == count is in range and id 0
    // addresses the count word itself -- which is exactly what the bootstrap
    // relies on. Nothing else ever asks for 0, and it is not a clip.
    if (entries_.empty() || voiceId == 0 || voiceId > FUN_00221c40_entryCount())
    {
      return 0;
    }
    return entries_[voiceId];
  }

  VoiceIndex::BankClip VoiceIndex::FUN_00206f08_bankClip(std::uint32_t voiceId,
                                                         std::uint32_t clipIndex) const
  {
    BankClip clip;
    const std::uint32_t size = sizeBytes(voiceId);
    if (size == 0)
    {
      return clip;
    }
    // The whole-entry answer, which is what FUN_00206d98 falls back to.
    const auto wholeEntry = [&] {
      BankClip whole;
      if (clipIndex == 0)
      {
        whole.offsetBytes = 0;
        whole.sizeBytes = size;
        whole.valid = true;
      }
      return whole;
    };

    if (audioPath_.empty() || size < 0x40)
    {
      return wholeEntry();
    }
    std::vector<std::uint32_t> header;
    if (!readWords(audioPath_, fileOffset(voiceId), 0x10, header))
    {
      return wholeEntry();
    }

    // FUN_00206d98's test, both halves. The second is what stops an ordinary
    // line whose first word happens to be small from being read as a directory.
    const std::uint32_t count = header[0];
    // DAT_00314BD6 is the halfword at +6, which little-endian is the *high*
    // half of word 1 -- clip 0's offset field, in 16-byte units.
    const std::uint32_t offsetField = (header[1] >> 16) & 0xFFFFu;
    if (count - 1u >= 0xFu || (offsetField << 4) != ((count * 4u + 0x13u) & 0xFFFFFFF0u))
    {
      return wholeEntry();
    }
    if (clipIndex >= count)
    {
      return clip;
    }
    const std::uint32_t entry = header[clipIndex + 1];
    clip.offsetBytes = ((entry >> 16) & 0xFFFFu) * 0x10u;
    clip.sizeBytes = (entry & 0xFFFFu) << 4;
    if (clip.offsetBytes + clip.sizeBytes > size)
    {
      return clip; // still invalid: the directory does not fit the entry
    }
    clip.valid = clip.sizeBytes != 0;
    return clip;
  }

  std::vector<std::uint8_t> VoiceIndex::readBankClipAdpcm(std::uint32_t voiceId,
                                                          std::uint32_t clipIndex) const
  {
    const BankClip clip = FUN_00206f08_bankClip(voiceId, clipIndex);
    if (!clip.valid || audioPath_.empty())
    {
      return {};
    }
    std::vector<std::uint32_t> words;
    if (!readWords(audioPath_, fileOffset(voiceId) + clip.offsetBytes, clip.sizeBytes / 4, words))
    {
      return {};
    }
    std::vector<std::uint8_t> bytes(clip.sizeBytes);
    std::memcpy(bytes.data(), words.data(), clip.sizeBytes);
    return bytes;
  }

  std::uint32_t VoiceIndex::bankClipHoldTicks(std::uint32_t voiceId, std::uint32_t clipIndex) const
  {
    const BankClip clip = FUN_00206f08_bankClip(voiceId, clipIndex);
    if (!clip.valid)
    {
      return 0;
    }
    const std::uint64_t samples =
        static_cast<std::uint64_t>(clip.sizeBytes) / kAdpcmBlockBytes * kAdpcmBlockSamples;
    const std::uint64_t ticksPerSecond =
        60ull * static_cast<std::uint64_t>(orphen::ported::kNominalFrameTicks);
    return static_cast<std::uint32_t>(samples * ticksPerSecond / kVoiceSampleRate);
  }

  std::uint32_t VoiceIndex::holdTicks(std::uint32_t voiceId) const
  {
    const std::uint64_t bytes = sizeBytes(voiceId);
    if (bytes == 0)
    {
      return 0;
    }
    const std::uint64_t samples = bytes / kAdpcmBlockBytes * kAdpcmBlockSamples;
    // samples / rate seconds, at 60 frames a second, at kNominalFrameTicks a
    // frame. 60 * 32 / 22125 reduces to 128 / 1475.
    const std::uint64_t ticksPerSecond =
        60ull * static_cast<std::uint64_t>(orphen::ported::kNominalFrameTicks);
    return static_cast<std::uint32_t>(samples * ticksPerSecond / kVoiceSampleRate);
  }

} // namespace orphen::ported::sound
