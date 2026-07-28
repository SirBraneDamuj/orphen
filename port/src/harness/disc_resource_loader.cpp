#include "harness/disc_resource_loader.h"

#include "ported/resource/fun_00222638.h"
#include "ported/resource/fun_00222898.h"
#include "ported/resource/fun_002f3118.h"
#include "ported/resource/mcb_runtime.h"

#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace orphen::harness
{
  namespace
  {

    constexpr std::uint16_t kMapCategory = 2;
    constexpr std::array<std::uint8_t, 4> kPsm2Magic{'P', 'S', 'M', '2'};

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

    std::string resourceIdHex(std::uint16_t resourceId)
    {
      std::ostringstream stream;
      stream << std::hex << std::setw(4) << std::setfill('0') << resourceId;
      return stream.str();
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

  LoadedDiscMap loadFirstPsm2FromDiscScene(const std::filesystem::path &discRoot, McbSceneSelection selection)
  {
    const std::vector<std::uint8_t> mcb0Bytes = readBinaryFile(discRoot / "MCB0.BIN");
    const std::vector<std::uint8_t> mcb1Bytes = readBinaryFile(discRoot / "MCB1.BIN");

    const auto table = orphen::ported::resource::FUN_00222638(mcb0Bytes);
    const std::vector<std::uint8_t> bundle = orphen::ported::resource::FUN_00222898(
        table, mcb1Bytes, selection.section, selection.entry);

    std::size_t recordOffset = 0;
    while (true)
    {
      const std::optional<orphen::ported::resource::McbBundleRecord> record =
          orphen::ported::resource::readMcbBundleRecordAt(bundle, recordOffset);
      if (!record.has_value())
      {
        break;
      }

      if (record->category == kMapCategory)
      {
        std::vector<std::uint8_t> decoded = orphen::ported::resource::FUN_002f3118(record->payload);
        if (hasPsm2Magic(decoded))
        {
          return {std::move(decoded),
                  record->resourceId,
                  resourceIdHex(record->resourceId),
                  record->offset,
                  record->payload.size(),
                  decoded.size()};
        }
      }

      recordOffset = record->offset + 8 + record->payload.size();
    }

    throw std::runtime_error("no MAP record decoded to PSM2 in " + sceneName(selection));
  }

} // namespace orphen::harness