#pragma once

// Binds spawned entities to loaded models and textures.
//
// This is the port's stand-in for the original's resource residency: entity
// +0x15C / +0x160 / +0x9C (src/FUN_00229c40.c) become a lookup here, and
// FUN_00266118's "make sure this record's model and texture are loaded" becomes
// ensureLoaded below.
//
// Two resource sets are searched, in order:
//
//   the scene bundle   whatever --scene selected
//   the boot bundle    s00_e000
//
// The second one needs justifying. s01_e024's bundle carries grp_0001, 0003,
// 0006, 0008, 0009, 000a, 0091, 0094 and 0128 -- the lead player, the party and
// the enemies -- but not the chests' grp_0172 or its tex_0179. In the real game
// those come from GRP.BIN, loaded once at boot by FUN_00221fd8, and GRP.BIN is
// not present in this working copy. The s00_e000 bundle's records are
// byte-identical to what the EE dump shows resident: the PSC3 at 0x00DDB000
// matches out/target_all/s00_e000/grp_0172.psc3 exactly apart from the four
// pointers FUN_00221f60 relocates.
//
// So this reproduces the *observed memory* rather than the original's file
// path. If GRP.BIN turns up, this is the thing to revisit.

#include "harness/flat_bin_archive.h"
#include "harness/scene_resource_provider.h"
#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/map_prop_descriptor_table.h"
#include "ported/model/psc3_model.h"
#include "ported/resource/texture_slot_cache.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orphen::port
{

  // What the render pass needs to draw one entity.
  struct EntityModelBinding
  {
    const orphen::ported::model::Psc3Model *model = nullptr;
    std::uint16_t meshId = 0;
    std::uint16_t textureId = 0;
    // Slot in the texture cache, or kNoTextureSlot when the texture could not
    // be resolved. Not an error on its own -- a model can draw untextured.
    int textureSlot = orphen::ported::resource::kNoTextureSlot;
    // Why there is no model, when there is none.
    std::string diagnostic;
  };

  class EntityModelStore
  {
  public:
    // `bootScene` is loaded in addition to the scene provider. Passing a scene
    // that does not exist is not fatal; it just leaves the boot set empty.
    void initialize(const orphen::harness::SceneResourceProvider *sceneResources,
                    const std::filesystem::path &discRoot,
                    const orphen::ported::entity::EntityDescriptorTable *descriptors);
    void reset();

    // FUN_00221fd8's static half, minus the PSC3 loads: the seven hardcoded
    // binds plus every static record whose +0x06 is 'd'.
    void FUN_00221fd8_bind_boot_textures();

    // FUN_00266118. Idempotent; the binding is cached per model record address.
    const EntityModelBinding *ensureLoaded(std::uint32_t modelRecordAddress);
    // Builds a binding from an already-read record.
    const EntityModelBinding *bindRecord(std::uint32_t bindingKey,
                                         const orphen::ported::entity::EntityModelRecord &record);
    const EntityModelBinding *bindMapProp(std::uint32_t typeId);

    // Convenience: resolve a type id all the way through the descriptor table.
    const EntityModelBinding *bindingForTypeId(std::uint32_t typeId);

    // The map-streamed prop banks (FUN_00228e28) plus the scene's stage number,
    // which is the bank FUN_00229980 uses for the 0x272 range. Both come from
    // outside this class: the banks from SCR.BIN, the stage from the scene
    // selection.
    void setMapPropTable(const orphen::ported::entity::MapPropDescriptorTable *table, int stageBank);
    const orphen::ported::entity::MapPropDescriptorTable *mapPropTable() const { return mapProps_; }
    int stageBank() const { return stageBank_; }

    const orphen::ported::resource::TextureSlotCache &textureSlots() const { return textureSlots_; }
    // For FUN_0022a178, which loads the map's pages into slots 0..9 from
    // outside this class.
    orphen::ported::resource::TextureSlotCache &mutableTextureSlots() { return textureSlots_; }
    std::size_t loadedModelCount() const { return models_.size(); }
    bool bootBundleLoaded() const { return bootResources_.has_value(); }

  private:
    const orphen::harness::SceneResourceProvider *sceneResources_ = nullptr;
    std::optional<orphen::harness::SceneResourceProvider> bootResources_;
    const orphen::ported::entity::EntityDescriptorTable *descriptors_ = nullptr;
    const orphen::ported::entity::MapPropDescriptorTable *mapProps_ = nullptr;
    int stageBank_ = -1;

    orphen::ported::resource::TextureSlotCache textureSlots_;
    // ITM.BIN, archive index 4. The 0x1F1 item band's meshes are not in any
    // scene bundle: FUN_00221fd8 loads them through FUN_00221b78, which is
    // that archive's own table of contents. Empty when the file is absent,
    // which is not fatal -- the item just has no model.
    orphen::harness::FlatBinArchive itmArchive_;
    std::map<std::uint16_t, orphen::ported::model::Psc3Model> models_;
    std::map<std::uint32_t, EntityModelBinding> bindings_;
    std::map<std::uint32_t, std::uint32_t> modelRecordForTypeId_;

    std::vector<std::uint8_t> decodeResource(std::uint16_t category, std::uint16_t resourceId) const;
    const orphen::ported::model::Psc3Model *loadModel(std::uint16_t meshId);
  };

} // namespace orphen::port
