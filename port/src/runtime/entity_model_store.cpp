#include "runtime/entity_model_store.h"

#include "harness/disc_resource_loader.h"

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

    constexpr std::array<BootTextureBind, 7> kFUN_00221fd8_fixedBinds{{
        {0x30, 0x179},
        {0x2A, 0x178},
        {0x2B, 0x177},
        {0x2C, 0x176},
        {0x20, 0x19C},
        {0x21, 0x19B},
        {0x28, 0x171},
    }};

    // The two FUN_002102d0 calls at 0x2E and 0x2F take an already-decoded buffer
    // through a different path and are not reproduced.
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

    const std::vector<std::uint8_t> bytes = decodeResource(orphen::harness::kGrpCategory, meshId);
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

  const EntityModelBinding *EntityModelStore::ensureLoaded(std::uint32_t modelRecordAddress)
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

    EntityModelBinding binding;
    binding.meshId = record->meshId0x00;
    binding.textureId = record->texId0x02;
    binding.model = loadModel(record->meshId0x00);
    if (binding.model == nullptr)
    {
      const auto stored = models_.find(record->meshId0x00);
      binding.diagnostic = stored != models_.end() && !stored->second.diagnostic.empty()
                               ? stored->second.diagnostic
                               : "grp record not in any open bundle";
    }

    if (record->texId0x02 != 0)
    {
      if (record->bindsTextureStatically())
      {
        // Already bound by FUN_00221fd8's second pass, at the record's own slot.
        binding.textureSlot = record->staticSlot0x07;
      }
      else if (textureSlots_.slot(record->staticSlot0x07).DAT_003429a8_residentId == record->texId0x02)
      {
        // One of the seven fixed boot binds. The record does not advertise
        // itself as static, but its slot already holds exactly its texture --
        // which is how the chests get theirs.
        binding.textureSlot = record->staticSlot0x07;
      }
      else
      {
        binding.textureSlot = textureSlots_.FUN_00266118_bind_texture(
            record->texId0x02, record->usesAltTextureBank(), record->storesNegatedTextureId());
      }
    }

    return &bindings_.emplace(modelRecordAddress, std::move(binding)).first->second;
  }

  const EntityModelBinding *EntityModelStore::bindingForTypeId(std::uint32_t typeId)
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
      return nullptr;
    }
    modelRecordForTypeId_.emplace(typeId, descriptor->modelRecordAddress);
    return ensureLoaded(descriptor->modelRecordAddress);
  }

} // namespace orphen::port
