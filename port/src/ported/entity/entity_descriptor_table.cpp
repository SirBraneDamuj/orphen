#include "ported/entity/entity_descriptor_table.h"

#include "ported/entity/map_prop_descriptor_table.h"

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
    const DescriptorSource source = sourceForTypeId(typeId);

    // Checked before `available()`: the streamed branch reads the map's prop
    // banks, not the executable, and FUN_00229980 reaches it without touching a
    // static table.
    if (source == DescriptorSource::Streamed)
    {
      return mapProps_ != nullptr ? mapProps_->FUN_00229980_synthesizeDescriptor(typeId, stageBank_)
                                  : std::nullopt;
    }

    if (!available())
    {
      return std::nullopt;
    }

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

  std::optional<EntityModelRecord> EntityDescriptorTable::readModelRecord(std::uint32_t recordAddress) const
  {
    if (!available() || recordAddress == 0)
    {
      return std::nullopt;
    }
    if (elf_->bytesAt(recordAddress, kModelRecordStride).empty())
    {
      return std::nullopt;
    }

    EntityModelRecord record;
    record.recordAddress = recordAddress;
    record.meshId0x00 = elf_->readU16(recordAddress + 0x00);
    record.texId0x02 = elf_->readU16(recordAddress + 0x02);
    record.flags0x04 = elf_->readU8(recordAddress + 0x04);
    record.loadState0x05 = elf_->readU8(recordAddress + 0x05);
    record.textureBind0x06 = elf_->readU8(recordAddress + 0x06);
    record.staticSlot0x07 = elf_->readU8(recordAddress + 0x07);
    return record;
  }

  std::vector<EntityModelRecord> EntityDescriptorTable::FUN_00221fd8_staticTextureBinds() const
  {
    std::vector<EntityModelRecord> binds;
    if (!available())
    {
      return binds;
    }

    // FUN_00221fd8 walks all three model tables here, not two. The original
    // bounds the primary walk with iGpffffb270 and the other two with "the
    // next record's mesh id is zero"; all three terminate on a zero mesh id at
    // exactly the record counts the offline extractor found (95, 114 and 226),
    // so the terminator alone is enough and the port does not have to resolve a
    // gp-relative count.
    //
    // PTR_DAT_003228c0 is a table, not a pointer -- the original takes its
    // *address* as the base and strides 0x2C from there -- and it is in the
    // executable's LOAD segment like the other two, so its records read
    // straight out of the ELF. Its 17 static binds are the effect models'
    // sheets, and slot 37 (tex_0197) is reachable from nowhere else: it is what
    // grp_00a8, the battle ground ring, draws with, and leaving the table out
    // is why the ring came out as a white drum.
    constexpr std::size_t kRecordCap = 256;
    for (const std::uint32_t base : {kDAT_0031ee48_primaryModels, kDAT_003214f8_secondaryModels,
                                     kPTR_DAT_003228c0_tertiaryModels})
    {
      for (std::size_t index = 0; index < kRecordCap; ++index)
      {
        const std::optional<EntityModelRecord> record =
            readModelRecord(base + static_cast<std::uint32_t>(index) * kModelRecordStride);
        if (!record.has_value() || record->meshId0x00 == 0)
        {
          break;
        }
        if (record->bindsTextureStatically() && record->texId0x02 != 0)
        {
          binds.push_back(*record);
        }
      }
    }

    return binds;
  }

} // namespace orphen::ported::entity
