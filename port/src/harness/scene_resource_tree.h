#pragma once

#include "harness/scene_resource_provider.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace orphen::harness
{

  struct SceneResourceTreeRecord
  {
    std::size_t recordIndex = 0;
    std::uint32_t packedId = 0;
    std::uint16_t category = 0;
    std::uint16_t resourceId = 0;
    std::size_t bundleRecordOffset = 0;
    std::size_t packedSize = 0;
    std::optional<std::size_t> decodedSize;
    std::string kind;
    std::string signature;
  };

  struct SceneResourceTreeCategory
  {
    std::uint16_t category = 0;
    std::string label;
    std::vector<SceneResourceTreeRecord> records;
  };

  struct SceneResourceTree
  {
    std::string sceneName;
    std::size_t bundleSize = 0;
    std::size_t recordCount = 0;
    std::vector<SceneResourceTreeCategory> categories;
  };

  SceneResourceTree buildSceneResourceTree(const SceneResourceProvider &resources);
  void printSceneResourceTree(const SceneResourceTree &tree, std::ostream &output);

} // namespace orphen::harness
