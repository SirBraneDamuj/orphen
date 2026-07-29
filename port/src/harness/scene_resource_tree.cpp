#include "harness/scene_resource_tree.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

namespace orphen::harness
{
  namespace
  {

    constexpr std::array<std::uint8_t, 4> kPsm2Magic{'P', 'S', 'M', '2'};
    constexpr std::array<std::uint8_t, 4> kBmpaMagic{'B', 'M', 'P', 'A'};
    constexpr std::array<std::uint8_t, 4> kPsc3Magic{'P', 'S', 'C', '3'};
    constexpr std::array<std::uint8_t, 4> kPsb4Magic{'P', 'S', 'B', '4'};

    std::string categoryLabel(std::uint16_t category)
    {
      switch (category)
      {
      case 1:
        return "scene/script data";
      case 2:
        return "map/model geometry";
      case 3:
        return "texture pages";
      default:
        return "unknown resource category";
      }
    }

    bool startsWith(const std::vector<std::uint8_t> &bytes, const std::array<std::uint8_t, 4> &magic)
    {
      return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
    }

    std::string hex16(std::uint16_t value)
    {
      std::ostringstream stream;
      stream << "0x" << std::hex << std::setw(4) << std::setfill('0') << value;
      return stream.str();
    }

    std::string hex32(std::uint32_t value)
    {
      std::ostringstream stream;
      stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
      return stream.str();
    }

    std::string byteSignature(const std::vector<std::uint8_t> &bytes)
    {
      if (bytes.empty())
      {
        return "empty";
      }

      const std::size_t count = std::min<std::size_t>(bytes.size(), 4);
      bool printable = count == 4;
      for (std::size_t index = 0; index < count; ++index)
      {
        printable = printable && std::isprint(static_cast<unsigned char>(bytes[index])) != 0;
      }
      if (printable)
      {
        return std::string(reinterpret_cast<const char *>(bytes.data()), count);
      }

      std::ostringstream stream;
      stream << std::hex << std::setfill('0');
      for (std::size_t index = 0; index < count; ++index)
      {
        if (index != 0)
        {
          stream << ' ';
        }
        stream << std::setw(2) << static_cast<int>(bytes[index]);
      }
      return stream.str();
    }

    std::string resourceKind(std::uint16_t category, const std::vector<std::uint8_t> &decoded)
    {
      if (startsWith(decoded, kPsm2Magic))
      {
        return "PSM2 map";
      }
      if (startsWith(decoded, kBmpaMagic))
      {
        return "BMPA texture";
      }
      if (startsWith(decoded, kPsc3Magic))
      {
        return "PSC3 model";
      }
      if (startsWith(decoded, kPsb4Magic))
      {
        return "PSB4 model/resource";
      }

      switch (category)
      {
      case 1:
        return "scene/script blob";
      case 2:
        return "geometry blob";
      case 3:
        return "texture blob";
      default:
        return "decoded blob";
      }
    }

    SceneResourceTreeCategory &categoryNodeFor(SceneResourceTree &tree, std::uint16_t category)
    {
      const auto found = std::find_if(tree.categories.begin(), tree.categories.end(), [category](const SceneResourceTreeCategory &node)
                                      { return node.category == category; });
      if (found != tree.categories.end())
      {
        return *found;
      }

      tree.categories.push_back({category, categoryLabel(category), {}});
      return tree.categories.back();
    }

  } // namespace

  SceneResourceTree buildSceneResourceTree(const SceneResourceProvider &resources)
  {
    SceneResourceTree tree;
    tree.sceneName = sceneName(resources.selection());
    tree.bundleSize = resources.bundleSize();
    tree.recordCount = resources.records().size();

    for (std::size_t recordIndex = 0; recordIndex < resources.records().size(); ++recordIndex)
    {
      const SceneResourceRecord &record = resources.records()[recordIndex];
      SceneResourceTreeRecord treeRecord{recordIndex,
                                         record.packedId,
                                         record.category,
                                         record.resourceId,
                                         record.bundleRecordOffset,
                                         record.packedSize,
                                         std::nullopt,
                                         "undecoded blob",
                                         ""};

      try
      {
        const std::vector<std::uint8_t> decoded = resources.decodeRecord(record);
        treeRecord.decodedSize = decoded.size();
        treeRecord.kind = resourceKind(record.category, decoded);
        treeRecord.signature = byteSignature(decoded);
      }
      catch (const std::exception &)
      {
        treeRecord.kind = "decode failed";
      }

      categoryNodeFor(tree, record.category).records.push_back(std::move(treeRecord));
    }

    return tree;
  }

  void printSceneResourceTree(const SceneResourceTree &tree, std::ostream &output)
  {
    output << "[scene-tree] " << tree.sceneName
           << " records=" << tree.recordCount
           << " bundleBytes=" << tree.bundleSize << '\n';

    for (const SceneResourceTreeCategory &category : tree.categories)
    {
      output << "  category " << hex16(category.category)
             << " " << category.label
             << " records=" << category.records.size() << '\n';

      for (const SceneResourceTreeRecord &record : category.records)
      {
        output << "    #" << std::dec << std::setw(2) << std::setfill('0') << record.recordIndex
               << " id=" << hex16(record.resourceId)
               << " kind=" << record.kind;
        if (!record.signature.empty())
        {
          output << " sig=" << record.signature;
        }
        output << '\n';

        output << "       packedId=" << hex32(record.packedId)
               << " offset=0x" << std::hex << std::setw(6) << std::setfill('0') << record.bundleRecordOffset
               << std::dec << std::setfill(' ')
               << " packedBytes=" << record.packedSize;
        if (record.decodedSize.has_value())
        {
          output << " decodedBytes=" << *record.decodedSize;
        }
        output << '\n';
      }
    }
  }

} // namespace orphen::harness
