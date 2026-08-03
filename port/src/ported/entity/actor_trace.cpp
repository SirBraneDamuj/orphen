#include "ported/entity/actor_trace.h"

namespace orphen::ported::entity
{

  void ActorTrace::reset()
  {
    types_.clear();
    seenSlots_.clear();
    states_.clear();
    hiddenCount_ = 0;
    suspendedCount_ = 0;
    fadingCount_ = 0;
    tableUnavailable_ = false;
  }

  void ActorTrace::recordDispatch(std::int16_t typeId,
                                  std::size_t slot,
                                  const ActorHandler &handler,
                                  bool implemented)
  {
    auto [entry, inserted] = types_.try_emplace(typeId);
    ActorTypeStat &stat = entry->second;
    if (inserted)
    {
      stat.firstSlot = slot;
    }
    ++stat.tickCount;
    stat.source = handler.source;
    stat.handlerAddress = handler.address;
    stat.implemented = implemented;
    if (slot < stat.firstSlot)
    {
      stat.firstSlot = slot;
    }

    SlotBits &bits = seenSlots_[typeId];
    const std::size_t word = (slot >> 6) & 3u;
    const std::uint64_t mask = std::uint64_t{1} << (slot & 63u);
    if ((bits[word] & mask) == 0)
    {
      bits[word] |= mask;
      ++stat.entityCount;
    }
  }

  std::uint32_t ActorTrace::unimplementedTypeCount() const
  {
    std::uint32_t count = 0;
    for (const auto &[typeId, stat] : types_)
    {
      (void)typeId;
      if (!stat.implemented)
      {
        ++count;
      }
    }
    return count;
  }

  std::uint32_t ActorTrace::unimplementedEntityCount() const
  {
    std::uint32_t count = 0;
    for (const auto &[typeId, stat] : types_)
    {
      (void)typeId;
      if (!stat.implemented)
      {
        count += stat.entityCount;
      }
    }
    return count;
  }

  std::uint32_t ActorTrace::tickedEntityCount() const
  {
    std::uint32_t count = 0;
    for (const auto &[typeId, stat] : types_)
    {
      (void)typeId;
      count += stat.entityCount;
    }
    return count;
  }

  void ActorTrace::recordStateDispatch(std::int16_t typeId,
                                       std::uint16_t state,
                                       std::uint32_t handlerAddress,
                                       bool implemented)
  {
    auto &stat = states_[{typeId, state}];
    stat.handlerAddress = handlerAddress;
    stat.implemented = implemented;
    ++stat.tickCount;
  }

  std::uint32_t ActorTrace::unimplementedStateCount() const
  {
    std::uint32_t count = 0;
    for (const auto &entry : states_)
    {
      if (!entry.second.implemented)
      {
        ++count;
      }
    }
    return count;
  }

} // namespace orphen::ported::entity
