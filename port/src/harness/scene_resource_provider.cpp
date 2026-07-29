#include "harness/scene_resource_provider.h"

#include "ported/resource/headerless_lz_decoder.h"
#include "ported/resource/mcb_table_loader.h"
#include "ported/resource/mcb_runtime.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace orphen::harness
{
  namespace
  {

    std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path &path)
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
        throw std::runtime_error("failed to open disc resource file: " + path.string());
      }

      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::vector<std::uint8_t> readBinaryFileRange(const std::filesystem::path &path,
                                                  std::uint32_t byteOffset,
                                                  std::uint32_t byteSize)
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
        throw std::runtime_error("failed to open disc resource file: " + path.string());
      }

      file.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);
      if (!file)
      {
        throw std::runtime_error("failed to seek disc resource file: " + path.string());
      }

      std::vector<std::uint8_t> bytes(byteSize);
      file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      if (file.gcount() != static_cast<std::streamsize>(bytes.size()))
      {
        throw std::runtime_error("disc resource range extends past file end: " + path.string());
      }

      return bytes;
    }

  } // namespace

  SceneResourceProvider SceneResourceProvider::loadFromDisc(const std::filesystem::path &discRoot, McbSceneSelection selection)
  {
    const std::vector<std::uint8_t> mcb0Bytes = readBinaryFile(discRoot / "MCB0.BIN");
    const auto table = orphen::ported::resource::loadMcb0Table(mcb0Bytes);
    const auto &tableEntry = table.entryAt(selection.section, selection.entry);
    if (!tableEntry.populated())
    {
      throw std::runtime_error("selected MCB scene slot is empty: " + sceneName(selection));
    }

    SceneResourceProvider provider;
    provider.discRoot_ = discRoot;
    provider.selection_ = selection;
    provider.bundle_ = readBinaryFileRange(discRoot / "MCB1.BIN", tableEntry.byteOffset, tableEntry.byteSize);
    provider.indexRecords();
    return provider;
  }

  const SceneResourceRecord *SceneResourceProvider::findFirst(std::uint16_t category) const
  {
    const auto found = std::find_if(records_.begin(), records_.end(), [category](const SceneResourceRecord &record)
                                    { return record.category == category; });
    return found == records_.end() ? nullptr : &*found;
  }

  const SceneResourceRecord *SceneResourceProvider::find(std::uint16_t category, std::uint16_t resourceId) const
  {
    const auto found = std::find_if(records_.begin(), records_.end(), [category, resourceId](const SceneResourceRecord &record)
                                    { return record.category == category && record.resourceId == resourceId; });
    return found == records_.end() ? nullptr : &*found;
  }

  const SceneResourceRecord *SceneResourceProvider::nextRecordAfter(const SceneResourceRecord &record) const
  {
    const auto found = std::find_if(records_.begin(), records_.end(), [&record](const SceneResourceRecord &candidate)
                                    { return candidate.bundleRecordOffset == record.bundleRecordOffset; });
    if (found == records_.end() || found + 1 == records_.end())
    {
      return nullptr;
    }
    return &*(found + 1);
  }

  std::span<const std::uint8_t> SceneResourceProvider::payloadForRecord(const SceneResourceRecord &record) const
  {
    const std::size_t payloadOffset = record.bundleRecordOffset + 8;
    if (payloadOffset > bundle_.size() || record.packedSize > bundle_.size() - payloadOffset)
    {
      throw std::runtime_error("scene resource record payload extends past bundle end");
    }
    return std::span<const std::uint8_t>(bundle_.data() + payloadOffset, record.packedSize);
  }

  std::vector<std::uint8_t> SceneResourceProvider::decodeRecord(const SceneResourceRecord &record) const
  {
    return orphen::ported::resource::decodeHeaderlessLzStream(payloadForRecord(record));
  }

  void SceneResourceProvider::indexRecords()
  {
    records_.clear();

    std::size_t recordOffset = 0;
    while (true)
    {
      const std::optional<orphen::ported::resource::McbBundleRecord> record =
          orphen::ported::resource::readMcbBundleRecordAt(bundle_, recordOffset);
      if (!record.has_value())
      {
        return;
      }

      records_.push_back({record->packedId,
                          record->category,
                          record->resourceId,
                          record->offset,
                          record->payload.size()});
      recordOffset = record->offset + 8 + record->payload.size();
    }
  }

} // namespace orphen::harness
