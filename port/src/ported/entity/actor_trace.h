#pragma once

#include "ported/entity/actor_dispatch_table.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace orphen::ported::entity
{

  // Records what the actor tick actually dispatched, the same way ScriptTrace
  // records what the script VM actually executed. Actor behaviors that have not
  // been ported do nothing and are counted here rather than guessed at, so the
  // report names the next handler to port instead of the port inventing motion.
  struct ActorTypeStat
  {
    std::uint32_t tickCount = 0;      // times an entity of this type was dispatched
    std::uint32_t entityCount = 0;    // distinct slots seen holding this type
    std::size_t firstSlot = 0;        // lowest slot it was seen on
    ActorHandlerSource source = ActorHandlerSource::None;
    std::uint32_t handlerAddress = 0; // 0 when unresolved
    bool implemented = false;
  };

  class ActorTrace
  {
  public:
    void reset();

    // Called once per dispatched entity per frame.
    void recordDispatch(std::int16_t typeId,
                        std::size_t slot,
                        const ActorHandler &handler,
                        bool implemented);

    // The second dispatch: which per-type state handler the entity's +0x60
    // resolved to, and whether the port implements it. A type can be dispatched
    // and still do nothing because the *state* it is in has no port, and that
    // distinction is invisible without this.
    struct ActorStateStat
    {
      std::uint32_t handlerAddress = 0;
      std::uint32_t tickCount = 0;
      bool implemented = false;
    };
    void recordStateDispatch(std::int16_t typeId,
                             std::uint16_t state,
                             std::uint32_t handlerAddress,
                             bool implemented);
    const std::map<std::pair<std::int16_t, std::uint16_t>, ActorStateStat> &states() const { return states_; }
    std::uint32_t unimplementedStateCount() const;

    // Entities skipped before the type dispatch, by which guard stopped them.
    void recordHidden() { ++hiddenCount_; }
    void recordSuspended() { ++suspendedCount_; }
    void recordFading() { ++fadingCount_; }

    // True once the table could not be read at all, so the report can say the
    // executable was missing rather than listing 256 unresolved types.
    void noteTableUnavailable() { tableUnavailable_ = true; }
    bool tableUnavailable() const { return tableUnavailable_; }

    const std::map<std::int16_t, ActorTypeStat> &types() const { return types_; }
    std::uint32_t hiddenCount() const { return hiddenCount_; }
    std::uint32_t suspendedCount() const { return suspendedCount_; }
    std::uint32_t fadingCount() const { return fadingCount_; }

    // Distinct types with no ported behavior, and how many entities they cover.
    std::uint32_t unimplementedTypeCount() const;
    std::uint32_t unimplementedEntityCount() const;
    std::uint32_t tickedEntityCount() const;

  private:
    // Slots already counted toward entityCount, so a type ticked for 600 frames
    // reports the entities it covers rather than 600 of them. 256 slots, four
    // 64-bit words.
    using SlotBits = std::array<std::uint64_t, 4>;

    std::map<std::int16_t, ActorTypeStat> types_;
    std::map<std::int16_t, SlotBits> seenSlots_;
    std::map<std::pair<std::int16_t, std::uint16_t>, ActorStateStat> states_;
    std::uint32_t hiddenCount_ = 0;
    std::uint32_t suspendedCount_ = 0;
    std::uint32_t fadingCount_ = 0;
    bool tableUnavailable_ = false;
  };

} // namespace orphen::ported::entity
