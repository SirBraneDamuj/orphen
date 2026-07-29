#pragma once

#include "harness/disc_resource_loader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace orphen::harness
{

  struct SceneResourceRecord
  {
    std::uint32_t packedId = 0;
    std::uint16_t category = 0;
    std::uint16_t resourceId = 0;
    std::size_t bundleRecordOffset = 0;
    std::size_t packedSize = 0;
  };

  class SceneResourceProvider
  {
  public:
    static SceneResourceProvider loadFromDisc(const std::filesystem::path &discRoot, McbSceneSelection selection);

    const std::filesystem::path &discRoot() const { return discRoot_; }
    McbSceneSelection selection() const { return selection_; }
    const std::vector<SceneResourceRecord> &records() const { return records_; }
    std::size_t bundleSize() const { return bundle_.size(); }

    const SceneResourceRecord *findFirst(std::uint16_t category) const;
    const SceneResourceRecord *find(std::uint16_t category, std::uint16_t resourceId) const;
    const SceneResourceRecord *nextRecordAfter(const SceneResourceRecord &record) const;
    std::span<const std::uint8_t> payloadForRecord(const SceneResourceRecord &record) const;
    std::vector<std::uint8_t> decodeRecord(const SceneResourceRecord &record) const;

  private:
    std::filesystem::path discRoot_;
    McbSceneSelection selection_;
    std::vector<std::uint8_t> bundle_;
    std::vector<SceneResourceRecord> records_;

    void indexRecords();
  };

} // namespace orphen::harness
