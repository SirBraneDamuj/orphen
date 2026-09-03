#include "runtime/entity_model_store.h"

#include "harness/disc_resource_loader.h"

#include <algorithm>
#include <array>
#include <utility>

namespace orphen::port
{
  namespace
  {
    // src/FUN_00221fd8.c lines 35-47: seven textures bound to fixed slots before
    // the table walks. These are not in any model record's +0x06 'd' set, which
    // is why the chest's tex_0179 reaches slot 0x30 even though its record says
    // it binds nothing.
    struct BootTextureBind
    {
      int slot;
      std::uint16_t textureId;
    };

    constexpr std::array<BootTextureBind, 9> kFUN_00221fd8_fixedBinds{{
        {0x30, 0x179},
        {0x2A, 0x178},
        {0x2B, 0x177},
        {0x2C, 0x176},
        {0x20, 0x19C},
        {0x21, 0x19B},
        {0x28, 0x171},
        // The dialogue font. The original takes these two through
        // FUN_00210218 + FUN_00238c90 + FUN_002102d0 rather than FUN_00210280,
        // because FUN_00238c90 measures the decoded sheet on the way past to
        // build the proportional width table. The port measures out of the slot
        // afterwards instead, so the load itself can go the ordinary way.
        {0x2E, 0x173},
        {0x2F, 0x172},
    }};

  } // namespace

  void EntityModelStore::initialize(const orphen::harness::SceneResourceProvider *sceneResources,
                                    const std::filesystem::path &discRoot,
                                    const orphen::ported::entity::EntityDescriptorTable *descriptors)
  {
    reset();
    sceneResources_ = sceneResources;
    descriptors_ = descriptors;

    if (!discRoot.empty())
    {
      itmArchive_.open(discRoot / "ITM.BIN");
      try
      {
        bootResources_ = orphen::harness::SceneResourceProvider::loadFromDisc(
            discRoot, orphen::harness::parseSceneName("s00_e000"));
      }
      catch (const std::exception &)
      {
        // No boot bundle just means the statically-descriptored models are
        // unavailable. Reported by whoever asks for one.
        bootResources_.reset();
      }
    }

    textureSlots_.setLoader([this](std::uint16_t textureId) -> std::optional<orphen::ported::resource::BmpaTexture> {
      const std::vector<std::uint8_t> bytes =
          decodeResource(orphen::harness::kTextureCategory, textureId);
      if (bytes.size() < 4 || bytes[0] != 'B' || bytes[1] != 'M' || bytes[2] != 'P' || bytes[3] != 'A')
      {
        return std::nullopt;
      }
      try
      {
        return orphen::ported::resource::decodeBmpaTexture(bytes);
      }
      catch (const std::exception &)
      {
        return std::nullopt;
      }
    });
  }

  void EntityModelStore::reset()
  {
    sceneResources_ = nullptr;
    bootResources_.reset();
    descriptors_ = nullptr;
    textureSlots_.reset();
    models_.clear();
    bindings_.clear();
    modelRecordForTypeId_.clear();
  }

  // Scene bundle first, then boot. Ids are scene-private, so a collision is
  // possible in principle; in practice the boot bundle carries the 0x01xx range
  // and scene bundles carry their own, and every id s01_e024 needs resolves in
  // exactly one of the two.
  std::vector<std::uint8_t> EntityModelStore::decodeResource(std::uint16_t category,
                                                             std::uint16_t resourceId) const
  {
    for (const orphen::harness::SceneResourceProvider *provider :
         {sceneResources_, bootResources_.has_value() ? &*bootResources_ : nullptr})
    {
      if (provider == nullptr)
      {
        continue;
      }
      const orphen::harness::SceneResourceRecord *record = provider->find(category, resourceId);
      if (record == nullptr)
      {
        continue;
      }
      std::vector<std::uint8_t> decoded = provider->decodeRecord(*record);
      if (!decoded.empty())
      {
        return decoded;
      }
    }
    return {};
  }

  const orphen::ported::model::Psc3Model *EntityModelStore::loadModel(std::uint16_t meshId)
  {
    const auto existing = models_.find(meshId);
    if (existing != models_.end())
    {
      return existing->second.valid ? &existing->second : nullptr;
    }

    // Category 0 is where entity models normally live, but a map-streamed prop
    // ships in category 2 alongside the map geometry instead -- s01_e024 carries
    // its type 0x272 chest as category 2 id 0x009F, and nothing in category 0.
    // Both decode to a plain PSC3, so the only difference is where to look.
    //
    // **The bundle has to be exhausted before the category.** This used to ask
    // for category 0 across both bundles and only then try category 2, on the
    // reasoning that ids are scene-private in practice. They are not: s01_e012
    // wants six models that live in its own category 2 -- 0x00A6, 0x00A8,
    // 0x00A9, 0x00AC, 0x00AD, 0x00AE -- and the boot bundle answers for those
    // ids first out of category 0. Two of those answers are some other model,
    // which parsed and drew as a plausible but wrong mesh; two are not models at
    // all and failed the magic check. `eeMemory.bin` settles which is right:
    // entity +0x15C is the model base FUN_00229c40:28 stored, and its bone count
    // matches the scene bundle's copy, not the boot bundle's.
    std::vector<std::uint8_t> bytes;
    const std::array<const orphen::harness::SceneResourceProvider *, 2> providers{
        sceneResources_, bootResources_.has_value() ? &*bootResources_ : nullptr};
    const std::array<std::uint16_t, 2> categories{orphen::harness::kGrpCategory,
                                                  orphen::harness::kMapCategory};
    for (const orphen::harness::SceneResourceProvider *provider : providers)
    {
      if (provider == nullptr)
      {
        continue;
      }
      for (const std::uint16_t category : categories)
      {
        const orphen::harness::SceneResourceRecord *record = provider->find(category, meshId);
        if (record == nullptr)
        {
          continue;
        }
        bytes = provider->decodeRecord(*record);
        if (!bytes.empty())
        {
          break;
        }
      }
      if (!bytes.empty())
      {
        break;
      }
    }
    if (bytes.empty() && itmArchive_.valid())
    {
      // FUN_00221fd8's own model pass: the records at PTR_DAT_0031FEC8 pull
      // their meshes from ITM.BIN by the record's +0x00 id rather than from a
      // bundle. That is where the 0x1F1 band's item models live.
      bytes = itmArchive_.decode(meshId);
    }
    orphen::ported::model::Psc3Model model = orphen::ported::model::loadPsc3Model(bytes);
    const auto inserted = models_.emplace(meshId, std::move(model)).first;
    return inserted->second.valid ? &inserted->second : nullptr;
  }

  void EntityModelStore::FUN_00221fd8_bind_boot_textures()
  {
    for (const BootTextureBind &bind : kFUN_00221fd8_fixedBinds)
    {
      textureSlots_.FUN_00210280_load_into_slot(bind.slot, bind.textureId);
    }

    if (descriptors_ == nullptr)
    {
      return;
    }
    // Second pass. The original skips a record whose slot is already occupied
    // (FUN_00210348 test at line 155), which matters because several records
    // share a slot -- eight of them point at slot 34.
    for (const orphen::ported::entity::EntityModelRecord &record :
         descriptors_->FUN_00221fd8_staticTextureBinds())
    {
      if (textureSlots_.slot(record.staticSlot0x07).occupied())
      {
        continue;
      }
      textureSlots_.FUN_00210280_load_into_slot(record.staticSlot0x07, record.texId0x02);
    }
  }

  EntityModelBinding *EntityModelStore::ensureLoaded(std::uint32_t modelRecordAddress)
  {
    const auto existing = bindings_.find(modelRecordAddress);
    if (existing != bindings_.end())
    {
      return &existing->second;
    }
    if (descriptors_ == nullptr)
    {
      return nullptr;
    }

    const std::optional<orphen::ported::entity::EntityModelRecord> record =
        descriptors_->readModelRecord(modelRecordAddress);
    if (!record.has_value())
    {
      return nullptr;
    }

    return bindRecord(modelRecordAddress, *record);
  }

  // Split out from ensureLoaded so a record from somewhere other than the ELF
  // can reuse it once that source is identified.
  EntityModelBinding *EntityModelStore::bindRecord(
      std::uint32_t bindingKey, const orphen::ported::entity::EntityModelRecord &record)
  {
    EntityModelBinding binding;
    binding.meshId = record.meshId0x00;
    binding.textureId = record.texId0x02;
    binding.model = loadModel(record.meshId0x00);
    if (binding.model != nullptr && !binding.model->uvAnimationScript.empty())
    {
      // FUN_00221E70's loop: four copies of the same script, each seeded by
      // FUN_002256F0 with the frame cursor at 0xFF and the running bit set.
      for (auto &state : binding.uvAnimation)
      {
        state = binding.model->uvAnimationScript;
      }
    }
    if (binding.model == nullptr)
    {
      const auto stored = models_.find(record.meshId0x00);
      binding.diagnostic = stored != models_.end() && !stored->second.diagnostic.empty()
                               ? stored->second.diagnostic
                               : "grp record not in any open bundle";
    }

    if (record.texId0x02 != 0)
    {
      if (record.bindsTextureStatically())
      {
        // Already bound by FUN_00221fd8's second pass, at the record's own slot.
        binding.textureSlot = record.staticSlot0x07;
      }
      else if (textureSlots_.slot(record.staticSlot0x07).DAT_003429a8_residentId == record.texId0x02)
      {
        // One of the seven fixed boot binds. The record does not advertise
        // itself as static, but its slot already holds exactly its texture --
        // which is how the chests get theirs.
        binding.textureSlot = record.staticSlot0x07;
      }
      else
      {
        binding.textureSlot = textureSlots_.FUN_00266118_bind_texture(
            record.texId0x02, record.usesAltTextureBank(), record.storesNegatedTextureId());
      }
    }

    return &bindings_.emplace(bindingKey, std::move(binding)).first->second;
  }

  void EntityModelStore::setMapPropTable(const orphen::ported::entity::MapPropDescriptorTable *table,
                                         int stageBank)
  {
    mapProps_ = table;
    stageBank_ = stageBank;
  }

  // FUN_00229980's map-streamed branch. The record is real data out of SCR.BIN,
  // so this is the same binding path as the ELF one -- only the lookup differs.
  EntityModelBinding *EntityModelStore::bindMapProp(std::uint32_t typeId)
  {
    if (mapProps_ == nullptr || !mapProps_->valid())
    {
      return nullptr;
    }
    const auto record = mapProps_->FUN_00229980_resolve(typeId, stageBank_);
    if (!record.has_value())
    {
      return nullptr;
    }

    // These records have no address of their own -- the original allocates them
    // on the heap and identifies them by that pointer. The port needs *some*
    // stable identity for the binding cache, so it uses the type id in a range
    // no ELF record address can occupy. This is bookkeeping, not data.
    constexpr std::uint32_t kMapPropKeyBase = 0xF0000000u;
    return bindRecord(kMapPropKeyBase | typeId, *record);
  }

  EntityModelBinding *EntityModelStore::bindingForTypeId(std::uint32_t typeId)
  {
    if (descriptors_ == nullptr)
    {
      return nullptr;
    }

    const auto cached = modelRecordForTypeId_.find(typeId);
    if (cached != modelRecordForTypeId_.end())
    {
      return ensureLoaded(cached->second);
    }

    const auto descriptor = descriptors_->FUN_00229980_resolve(typeId);
    if (!descriptor.has_value() || descriptor->modelRecordAddress == 0)
    {
      return bindMapProp(typeId);
    }
    modelRecordForTypeId_.emplace(typeId, descriptor->modelRecordAddress);
    return ensureLoaded(descriptor->modelRecordAddress);
  }

} // namespace orphen::port
