#include "ported/entity/entity_descriptor_table.h"

namespace orphen::ported::entity
{

  DescriptorSource EntityDescriptorTable::sourceForTypeId(std::uint32_t typeId)
  {
    // FUN_00229980's range tests are unsigned subtractions, so each one is a
    // half-open window. Reproduced in the original's order, because the windows
    // are not disjoint from the fallthrough: 0xFB, 0x1F0 and 0x271 all miss
    // every window and land on the primary table.
    if (typeId == kIndirectTypeId || typeId > kMaxTypeId)
    {
      return DescriptorSource::Unresolved;
    }

    if (typeId - 0x7Cu < 0x7Fu) // 0x7C .. 0xFA
    {
      return DescriptorSource::Secondary;
    }
    if (typeId - 0xFCu < 0xF4u) // 0xFC .. 0x1EF
    {
      return DescriptorSource::Tertiary;
    }
    if (typeId - 0x1F1u < 0x80u) // 0x1F1 .. 0x270
    {
      return DescriptorSource::Shared;
    }
    if (typeId - 0x272u < 0x100u || typeId - 0x373u < 0x100u || typeId - 0x474u < 0x100u)
    {
      return DescriptorSource::Streamed;
    }
    if (typeId == 0)
    {
      // The primary table is indexed (id - 1); the original bounds-checks the
      // result against 0 and iGpffffae04 and reports ER_BADNO.
      return DescriptorSource::Unresolved;
    }
    return DescriptorSource::Primary;
  }

  std::optional<EntityDescriptor> EntityDescriptorTable::FUN_00229980_resolve(std::uint32_t typeId) const
  {
    if (!available())
    {
      return std::nullopt;
    }

    const DescriptorSource source = sourceForTypeId(typeId);
    std::uint32_t recordAddress = 0;
    std::uint32_t modelTableBase = 0;
    bool modelIsDirectlyIndexed = false;

    switch (source)
    {
    case DescriptorSource::Primary:
      recordAddress = kDAT_00318b68_primaryDescriptors + (typeId - 1) * kDescriptorStride;
      modelTableBase = kDAT_0031ee48_primaryModels;
      break;
    case DescriptorSource::Secondary:
      recordAddress = kDAT_00319900_secondaryDescriptors + (typeId - 0x7Cu) * kDescriptorStride;
      modelTableBase = kDAT_003214f8_secondaryModels;
      break;
    case DescriptorSource::Tertiary:
      recordAddress = kDAT_0031a700_tertiaryDescriptors + (typeId - 0xFCu) * kDescriptorStride;
      modelTableBase = kPTR_DAT_003228c0_tertiaryModels;
      break;
    case DescriptorSource::Shared:
      // One descriptor shared by the whole band; the model record is indexed by
      // the raw type id instead of by descriptor[0].
      recordAddress = kDAT_003198e0_sharedDescriptor;
      modelTableBase = kDAT_0031a95c_sharedModels + typeId * kModelRecordStride;
      modelIsDirectlyIndexed = true;
      break;
    case DescriptorSource::Streamed:
    case DescriptorSource::Unresolved:
    default:
      return std::nullopt;
    }

    if (elf_->bytesAt(recordAddress, kDescriptorStride).empty())
    {
      return std::nullopt;
    }

    EntityDescriptor descriptor;
    descriptor.source = source;
    descriptor.typeId = typeId;
    descriptor.recordAddress = recordAddress;

    descriptor.modelIndex0x00 = elf_->readS16(recordAddress + 0x00);
    descriptor.byte0x02 = elf_->readU8(recordAddress + 0x02);
    descriptor.flags0x04 = elf_->readU16(recordAddress + 0x04);
    descriptor.radius0x08 = elf_->readF32(recordAddress + 0x08);
    descriptor.height0x0c = elf_->readF32(recordAddress + 0x0C);
    descriptor.word0x10 = elf_->readU32(recordAddress + 0x10);
    descriptor.byte0x14 = static_cast<std::int8_t>(elf_->readU8(recordAddress + 0x14));
    descriptor.halfword0x16 = elf_->readU16(recordAddress + 0x16);
    descriptor.halfword0x18 = elf_->readU16(recordAddress + 0x18);

    // ppuVar2 + *psVar4 * 0xb over a 4-byte pointer type, i.e. * 0x2C bytes.
    descriptor.modelRecordAddress =
        modelIsDirectlyIndexed
            ? modelTableBase
            : modelTableBase + static_cast<std::uint32_t>(descriptor.modelIndex0x00) * kModelRecordStride;

    return descriptor;
  }

} // namespace orphen::ported::entity
