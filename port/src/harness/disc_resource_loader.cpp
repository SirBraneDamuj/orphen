#include "harness/disc_resource_loader.h"

#include "harness/scene_resource_provider.h"

#include "ported/resource/bmpa_texture_decoder.h"
#include "ported/resource/headerless_lz_decoder.h"
#include "ported/resource/mcb_table_loader.h"
#include "ported/resource/mcb_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>

namespace orphen::harness
{
  namespace
  {

    constexpr std::array<std::uint8_t, 4> kPsm2Magic{'P', 'S', 'M', '2'};
    constexpr std::array<std::uint8_t, 4> kBmpaMagic{'B', 'M', 'P', 'A'};

    std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path &path)
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
        throw std::runtime_error("failed to open disc resource file: " + path.string());
      }

      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::uint16_t parseFixedDecimal(std::string_view text, std::size_t offset, std::size_t digitCount, const char *label)
    {
      std::uint16_t value = 0;
      for (std::size_t digitIndex = 0; digitIndex < digitCount; ++digitIndex)
      {
        const char digit = text[offset + digitIndex];
        if (std::isdigit(static_cast<unsigned char>(digit)) == 0)
        {
          throw std::runtime_error(std::string("invalid ") + label + " in scene name: " + std::string(text));
        }
        value = static_cast<std::uint16_t>(value * 10 + static_cast<std::uint16_t>(digit - '0'));
      }
      return value;
    }

    bool hasPsm2Magic(const std::vector<std::uint8_t> &bytes)
    {
      return bytes.size() >= kPsm2Magic.size() &&
             std::equal(kPsm2Magic.begin(), kPsm2Magic.end(), bytes.begin());
    }

    bool hasBmpaMagic(const std::vector<std::uint8_t> &bytes)
    {
      return bytes.size() >= kBmpaMagic.size() &&
             std::equal(kBmpaMagic.begin(), kBmpaMagic.end(), bytes.begin());
    }

    std::string resourceIdHex(std::uint16_t resourceId)
    {
      std::ostringstream stream;
      stream << std::hex << std::setw(4) << std::setfill('0') << resourceId;
      return stream.str();
    }

    std::vector<LoadedDiscTexturePage> loadAdjacentBmpaTexturePages(const SceneResourceProvider &resources,
                                                                    const SceneResourceRecord &mapRecord)
    {
      std::vector<LoadedDiscTexturePage> texturePages;
      const SceneResourceRecord *record = resources.nextRecordAfter(mapRecord);
      while (record != nullptr)
      {
        if (record->category != kTextureCategory)
        {
          return texturePages;
        }

        std::vector<std::uint8_t> decoded = resources.decodeRecord(*record);
        if (!hasBmpaMagic(decoded))
        {
          return texturePages;
        }

        texturePages.push_back({orphen::ported::resource::decodeBmpaTexture(decoded),
                                record->resourceId,
                                resourceIdHex(record->resourceId),
                                record->bundleRecordOffset,
                                record->packedSize,
                                decoded.size()});
        record = resources.nextRecordAfter(*record);
      }

      return texturePages;
    }

  } // namespace

  McbSceneSelection parseSceneName(std::string_view text)
  {
    if (text.size() != 8 || text[0] != 's' || text[3] != '_' || text[4] != 'e')
    {
      throw std::runtime_error("scene must use form sNN_eMMM, for example s01_e012");
    }

    return {parseFixedDecimal(text, 1, 2, "section"), parseFixedDecimal(text, 5, 3, "entry")};
  }

  std::string sceneName(McbSceneSelection selection)
  {
    std::ostringstream stream;
    stream << 's' << std::dec << std::setw(2) << std::setfill('0') << selection.section
           << "_e" << std::setw(3) << std::setfill('0') << selection.entry;
    return stream.str();
  }

  std::vector<McbSceneSelection> listPopulatedMcbScenes(const std::filesystem::path &discRoot)
  {
    const std::vector<std::uint8_t> mcb0Bytes = readBinaryFile(discRoot / "MCB0.BIN");
    const auto table = orphen::ported::resource::loadMcb0Table(mcb0Bytes);
    std::vector<McbSceneSelection> scenes;

    for (std::uint16_t section = 0; section < orphen::ported::resource::McbTable::kSectionCount; ++section)
    {
      for (std::uint16_t entry = 0; entry < orphen::ported::resource::McbTable::kEntryCount; ++entry)
      {
        if (table.entryAt(section, entry).populated())
        {
          scenes.push_back({section, entry});
        }
      }
    }

    return scenes;
  }

  LoadedDiscMap loadFirstPsm2FromSceneResources(const SceneResourceProvider &resources)
  {
    for (const SceneResourceRecord &record : resources.records())
    {
      if (record.category == kMapCategory)
      {
        std::vector<std::uint8_t> decoded = resources.decodeRecord(record);
        if (hasPsm2Magic(decoded))
        {
          std::vector<LoadedDiscTexturePage> texturePages = loadAdjacentBmpaTexturePages(resources, record);
          return {std::move(decoded),
                  std::move(texturePages),
                  record.resourceId,
                  resourceIdHex(record.resourceId),
                  record.bundleRecordOffset,
                  record.packedSize,
                  decoded.size()};
        }
      }
    }

    throw std::runtime_error("no MAP record decoded to PSM2 in " + sceneName(resources.selection()));
  }

  LoadedDiscMap loadFirstPsm2FromDiscScene(const std::filesystem::path &discRoot, McbSceneSelection selection)
  {
    const SceneResourceProvider resources = SceneResourceProvider::loadFromDisc(discRoot, selection);
    return loadFirstPsm2FromSceneResources(resources);
  }

} // namespace orphen::harness
