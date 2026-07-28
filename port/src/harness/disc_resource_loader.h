#pragma once

#include "ported/resource/bmpa_texture_decoder.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace orphen::harness
{

  struct McbSceneSelection
  {
    std::uint16_t section = 0;
    std::uint16_t entry = 0;
  };

  struct LoadedDiscTexturePage
  {
    orphen::ported::resource::BmpaTexture texture;
    std::uint16_t resourceId = 0;
    std::string resourceIdHex;
    std::size_t bundleRecordOffset = 0;
    std::size_t packedSize = 0;
    std::size_t decodedSize = 0;
  };

  struct LoadedDiscMap
  {
    std::vector<std::uint8_t> decodedPsm2;
    std::vector<LoadedDiscTexturePage> texturePages;
    std::uint16_t resourceId = 0;
    std::string resourceIdHex;
    std::size_t bundleRecordOffset = 0;
    std::size_t packedSize = 0;
    std::size_t decodedSize = 0;
  };

  McbSceneSelection parseSceneName(std::string_view sceneName);
  std::string sceneName(McbSceneSelection selection);
  std::vector<McbSceneSelection> listPopulatedMcbScenes(const std::filesystem::path &discRoot);
  LoadedDiscMap loadFirstPsm2FromDiscScene(const std::filesystem::path &discRoot, McbSceneSelection selection);

} // namespace orphen::harness
