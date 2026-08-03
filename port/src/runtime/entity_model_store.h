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

#include "harness/scene_resource_provider.h"
#include "ported/entity/entity_descriptor_table.h"
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

    // Convenience: resolve a type id all the way through the descriptor table.
    const EntityModelBinding *bindingForTypeId(std::uint32_t typeId);

    const orphen::ported::resource::TextureSlotCache &textureSlots() const { return textureSlots_; }
    std::size_t loadedModelCount() const { return models_.size(); }
    bool bootBundleLoaded() const { return bootResources_.has_value(); }

  private:
    const orphen::harness::SceneResourceProvider *sceneResources_ = nullptr;
    std::optional<orphen::harness::SceneResourceProvider> bootResources_;
    const orphen::ported::entity::EntityDescriptorTable *descriptors_ = nullptr;

    orphen::ported::resource::TextureSlotCache textureSlots_;
    std::map<std::uint16_t, orphen::ported::model::Psc3Model> models_;
    std::map<std::uint32_t, EntityModelBinding> bindings_;
    std::map<std::uint32_t, std::uint32_t> modelRecordForTypeId_;

    std::vector<std::uint8_t> decodeResource(std::uint16_t category, std::uint16_t resourceId) const;
    const orphen::ported::model::Psc3Model *loadModel(std::uint16_t meshId);
  };

} // namespace orphen::port
