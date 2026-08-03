#pragma once

#include "ported/resource/elf_data_reader.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // Native counterpart of the type dispatch inside src/FUN_00239ce0.c
  // (0x00239ce0): resolves an entity type id to the address of the native
  // behavior function that runs for it every frame.
  //
  // Actor behavior in this engine is not script. FUN_00239ce0 walks the entity
  // pool and calls a function selected by the entity's type id through four
  // function-pointer tables compiled into SLUS_200.11. Those tables are static
  // data, so they are read out of the executable through ElfDataReader rather
  // than transcribed. See analyzed/actor_frame_dispatch.c.

  // The four tables, by the labels the decompiled sources use. They are
  // contiguous -- 0x31C6C0 + 0x7B*4 == 0x31C8AC and 0x31C8B0 + 0x7F*4 ==
  // 0x31CAAC -- so this is one array the compiler split into four base pointers
  // with biased indices.
  constexpr std::uint32_t kPTR_FUN_0031c6c0_primaryHandlers = 0x0031C6C0;
  constexpr std::uint32_t kPTR_FUN_0031c8b0_secondaryHandlers = 0x0031C8B0;
  constexpr std::uint32_t kPTR_LAB_0031cab0_tertiaryHandlers = 0x0031CAB0;
  constexpr std::uint32_t kPTR_LAB_0031ce90_sharedHandlers = 0x0031CE90;

  // The do-nothing handler most table entries point at. It has no body in src/
  // because it is a bare `jr ra`; it is implemented, not unknown.
  constexpr std::uint32_t kFUN_00239e78_noOp = 0x00239E78;

  // Entities in the map-streamed id bands do not go through a table at all;
  // FUN_00239ce0 calls this directly.
  constexpr std::uint32_t kFUN_002cfe08_streamedProp = 0x002CFE08;

  // Taken instead of the type handler when entity +0x04 has bit 0x800.
  constexpr std::uint32_t kFUN_0023a568_fade = 0x0023A568;

  // Which of FUN_00239ce0's branches a type id fell into. Reported so the actor
  // inventory can say why an id resolved the way it did.
  enum class ActorHandlerSource : std::uint8_t
  {
    None = 0,  // type <= 0, or no executable loaded
    Streamed,  // ids 0x272.., 0x373.., 0x474.. -> FUN_002cfe08, no table
    Shared,    // PTR_LAB_0031ce90, indexed (id - 0x1F1)
    Secondary, // PTR_FUN_0031c8b0, indexed (id - 0x7C)
    Tertiary,  // PTR_LAB_0031cab0, indexed (id - 0xFC)
    Primary,   // PTR_FUN_0031c6c0, indexed (id - 1)
  };

  struct ActorHandler
  {
    ActorHandlerSource source = ActorHandlerSource::None;
    // PS2 address of the behavior function, or 0 when it could not be resolved.
    std::uint32_t address = 0;
    // Where the pointer was read from; 0 for the table-free streamed path.
    std::uint32_t slotAddress = 0;
  };

  // The *second* dispatch. Most type handlers are a thin shell: open the freeze
  // gate, then index a per-type-family table with the entity's state at +0x60.
  // These tables are static data in the executable too, so they are read rather
  // than transcribed. Contents confirmed by reading SLUS_200.11:
  //
  //   PTR_LAB_0031e1d0  12 entries, FUN_0025ab68, party members (types 3..7).
  //                     States 0 and 6 both point at 0x0025ABB8, which is a bare
  //                     `jr ra; nop` -- a real no-op, not a stub. State 5 is a
  //                     null pointer and is never entered.
  //   PTR_FUN_00326660  20 entries, FUN_002cd0a0, the type 0x62 enemy.
  //                     State 7 is null.
  constexpr std::uint32_t kPTR_LAB_0031e1d0_partyStates = 0x0031E1D0;
  constexpr std::size_t kPartyStateCount = 12;
  constexpr std::uint32_t kPTR_FUN_00326660_enemy62States = 0x00326660;
  constexpr std::size_t kEnemy62StateCount = 20;

  // 0x0025ABB8: `jr ra; nop`, verified in the executable.
  constexpr std::uint32_t kLAB_0025abb8_noOp = 0x0025ABB8;

  class ActorDispatchTable
  {
  public:
    ActorDispatchTable() = default;
    explicit ActorDispatchTable(const orphen::ported::resource::ElfDataReader *elf) : elf_(elf) {}

    bool available() const { return elf_ != nullptr && elf_->valid(); }

    // FUN_00239ce0's range selection. The streamed bands resolve without an
    // executable, because that path is a direct call rather than a table read.
    ActorHandler FUN_00239ce0_resolve(std::int16_t typeId) const;

    // Classifies an id without needing the ELF, so the report can distinguish
    // "no executable loaded" from "this type never had a handler".
    static ActorHandlerSource sourceForTypeId(std::int16_t typeId);

    // One entry of a state table. Returns 0 when there is no executable or the
    // state is out of range; a null entry in the table reads back as 0 too, and
    // in both tables that means the state is never entered.
    std::uint32_t stateHandler(std::uint32_t tableAddress, std::size_t stateCount, std::uint16_t state) const;

  private:
    const orphen::ported::resource::ElfDataReader *elf_ = nullptr;
  };

} // namespace orphen::ported::entity
