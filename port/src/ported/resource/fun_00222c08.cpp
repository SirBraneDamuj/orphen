#include "ported/resource/fun_00222c08.h"

namespace orphen::ported::resource
{

  std::optional<McbBundleRecord> FUN_00222c08(std::span<const std::uint8_t> bundle, std::uint32_t packedId)
  {
    std::size_t recordOffset = 0;
    while (true)
    {
      std::optional<McbBundleRecord> record = readMcbBundleRecordAt(bundle, recordOffset);
      if (!record.has_value())
      {
        return std::nullopt;
      }
      if (record->packedId == packedId)
      {
        return record;
      }
      recordOffset = record->offset + 8 + record->payload.size();
    }
  }

} // namespace orphen::ported::resource
