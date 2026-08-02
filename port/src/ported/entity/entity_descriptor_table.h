#pragma once

#include "ported/resource/elf_data_reader.h"

#include <cstdint>
#include <optional>

namespace orphen::ported::entity
{

  // Native counterpart of src/FUN_00229980.c (0x00229980) and its guarded
  // wrapper src/FUN_00229be8.c (0x00229be8): resolves an entity type id to the
  // 0x1C-byte descriptor the spawn path initialises entities from.
  //
  // The descriptors are static tables in the executable's data segment, so this
  // reads them out of SLUS_200.11 through ElfDataReader rather than from disc
  // resources. See analyzed/entity_pool_and_descriptors.c.

  // Table bases, by the labels the decompiled sources use.
  constexpr std::uint32_t kDAT_00318b68_primaryDescriptors = 0x00318B68;
  constexpr std::uint32_t kDAT_0031ee48_primaryModels = 0x0031EE48;
  constexpr std::uint32_t kDAT_00319900_secondaryDescriptors = 0x00319900;
  constexpr std::uint32_t kDAT_003214f8_secondaryModels = 0x003214F8;
  constexpr std::uint32_t kDAT_0031a700_tertiaryDescriptors = 0x0031A700;
  constexpr std::uint32_t kPTR_DAT_003228c0_tertiaryModels = 0x003228C0;
  constexpr std::uint32_t kDAT_003198e0_sharedDescriptor = 0x003198E0;
  constexpr std::uint32_t kDAT_0031a95c_sharedModels = 0x0031A95C;

  constexpr std::uint32_t kDescriptorStride = 0x1C;
  constexpr std::uint32_t kModelRecordStride = 0x2C;

  // FUN_00229be8 rejects anything above this, and id 0x38, with
  // "ER_PARAM [get_pchr]".
  constexpr std::uint32_t kMaxTypeId = 0x574;
  constexpr std::uint32_t kIndirectTypeId = 0x38;

  // Ids at or above this come from descriptors streamed with the map rather than
  // from a static table. Not resolvable here.
  constexpr std::uint32_t kFirstStreamedTypeId = 0x272;

  // Which of FUN_00229980's ranges a type id fell into. Reported so the script
  // inventory can say *why* an id did not resolve.
  enum class DescriptorSource : std::uint8_t
  {
    Unresolved = 0,
    Primary,   // DAT_00318b68, indexed (id - 1)
    Secondary, // DAT_00319900, indexed (id - 0x7C)
    Tertiary,  // DAT_0031a700, indexed (id - 0xFC)
    Shared,    // the single descriptor at 0x3198E0, model 0x31A95C + id * 0x2C
    Streamed,  // id >= 0x272: loaded with the map, not in the executable
  };

  // The fields FUN_00229c40 copies out of the 0x1C-byte record. Offsets are the
  // record's own, and the trailing comment is where each lands in the entity.
  struct EntityDescriptor
  {
    DescriptorSource source = DescriptorSource::Unresolved;
    std::uint32_t typeId = 0;
    std::uint32_t recordAddress = 0; // PS2 virtual address it was read from

    std::int16_t modelIndex0x00 = 0;    // index into the 0x2C model record table
    std::uint8_t byte0x02 = 0;          // -> entity +0x133
    std::uint16_t flags0x04 = 0;        // -> entity +0x02
    float radius0x08 = 0.0f;            // -> entity +0x54, collision radius
    float height0x0c = 0.0f;            // -> entity +0x58
    std::uint32_t word0x10 = 0;         // -> entity +0x80
    std::int8_t byte0x14 = 0;           // -> entity +0x06
    std::uint16_t halfword0x16 = 0;     // -> entity +0x08 (OR 0x20 when id < 0x272)
    std::uint16_t halfword0x18 = 0;     // -> entity +0x04

    std::uint32_t modelRecordAddress = 0; // resolved 0x2C record, 0 when unknown
  };

  class EntityDescriptorTable
  {
  public:
    EntityDescriptorTable() = default;
    explicit EntityDescriptorTable(const orphen::ported::resource::ElfDataReader *elf) : elf_(elf) {}

    bool available() const { return elf_ != nullptr && elf_->valid(); }

    // FUN_00229980's range selection. Returns nullopt for ids the executable
    // cannot answer for -- out of range, streamed with the map, or with no ELF
    // loaded at all.
    //
    // The 0x38 indirection (which reads a halfword out of a context object) is
    // not reproduced; that id is rejected outright, matching FUN_00229be8.
    std::optional<EntityDescriptor> FUN_00229980_resolve(std::uint32_t typeId) const;

    // Classifies an id without needing the ELF, so the report can distinguish
    // "no executable loaded" from "this id was never static".
    static DescriptorSource sourceForTypeId(std::uint32_t typeId);

  private:
    const orphen::ported::resource::ElfDataReader *elf_ = nullptr;
  };

} // namespace orphen::ported::entity
