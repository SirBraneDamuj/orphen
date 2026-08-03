#include "ported/entity/actor_dispatch_table.h"

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_00239ce0 widens the type through `short` then `int`, so a negative
    // type id becomes a very large unsigned value and misses every window.
    std::uint32_t widen(std::int16_t typeId)
    {
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(typeId));
    }
  } // namespace

  ActorHandlerSource ActorDispatchTable::sourceForTypeId(std::int16_t typeId)
  {
    const std::uint32_t type = widen(typeId);

    // Reproduced in FUN_00239ce0's branch order. The windows happen to be
    // disjoint, so the order does not change the answer -- but the *bounds* do,
    // and one of them is load-bearing. Every comparison is unsigned:
    //
    //   type 0xFB is 0x7F past 0x7C, so it fails `< 0x7F` and does not reach the
    //   secondary table; it then underflows the 0xFC test; so it falls through
    //   to the primary table at index 0xFA. That is a seam bug in the original,
    //   and reproducing it is the point of writing the tests this way rather
    //   than as sorted ranges.
    if (type - 0x272u < 0x100u || type - 0x373u < 0x100u || type - 0x474u < 0x100u)
    {
      return ActorHandlerSource::Streamed;
    }
    if (type - 0x1F1u < 0x80u) // 0x1F1 .. 0x270
    {
      return ActorHandlerSource::Shared;
    }
    if (type - 0x7Cu < 0x7Fu) // 0x7C .. 0xFA
    {
      return ActorHandlerSource::Secondary;
    }
    if (type - 0xFCu <= 0xF3u) // 0xFC .. 0x1EF
    {
      return ActorHandlerSource::Tertiary;
    }
    if (typeId > 0) // 0x01 .. 0x7B, plus 0xFB through the seam above
    {
      return ActorHandlerSource::Primary;
    }
    return ActorHandlerSource::None;
  }

  ActorHandler ActorDispatchTable::FUN_00239ce0_resolve(std::int16_t typeId) const
  {
    ActorHandler handler;
    handler.source = sourceForTypeId(typeId);

    if (handler.source == ActorHandlerSource::Streamed)
    {
      // Not a table read: FUN_00239ce0 calls FUN_002cfe08 directly, so this
      // resolves even with no executable loaded.
      handler.address = kFUN_002cfe08_streamedProp;
      return handler;
    }

    if (handler.source == ActorHandlerSource::None || !available())
    {
      return handler;
    }

    const std::uint32_t type = widen(typeId);
    switch (handler.source)
    {
    case ActorHandlerSource::Shared:
      handler.slotAddress = kPTR_LAB_0031ce90_sharedHandlers + (type - 0x1F1u) * 4u;
      break;
    case ActorHandlerSource::Secondary:
      handler.slotAddress = kPTR_FUN_0031c8b0_secondaryHandlers + (type - 0x7Cu) * 4u;
      break;
    case ActorHandlerSource::Tertiary:
      handler.slotAddress = kPTR_LAB_0031cab0_tertiaryHandlers + (type - 0xFCu) * 4u;
      break;
    case ActorHandlerSource::Primary:
      handler.slotAddress = kPTR_FUN_0031c6c0_primaryHandlers + (type - 1u) * 4u;
      break;
    case ActorHandlerSource::Streamed:
    case ActorHandlerSource::None:
    default:
      return handler;
    }

    if (elf_->bytesAt(handler.slotAddress, 4).empty())
    {
      handler.slotAddress = 0;
      return handler;
    }
    handler.address = elf_->readU32(handler.slotAddress);
    return handler;
  }

  std::uint32_t ActorDispatchTable::stateHandler(std::uint32_t tableAddress,
                                                std::size_t stateCount,
                                                std::uint16_t state) const
  {
    if (!available() || state >= stateCount)
    {
      return 0;
    }
    return elf_->readU32(tableAddress + static_cast<std::uint32_t>(state) * 4u, 0u);
  }

} // namespace orphen::ported::entity
