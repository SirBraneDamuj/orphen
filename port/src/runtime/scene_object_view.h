#pragma once

#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <vector>

namespace orphen::port
{

  // A script-spawned entity, flattened for rendering. Built from the entity pool
  // each time the scene reloads, the same way PlayerViewState is built from the
  // lead player.
  //
  // Each draws as its PSC3 model when one resolved, and always as a labelled
  // debug box sized from the type descriptor's collision radius and height --
  // the box is deliberately kept, not a fallback.
  struct SceneObjectView
  {
    std::size_t slot = 0;
    std::int32_t typeId = 0;
    std::int32_t modelIndex = -1; // -1 when the descriptor was not resolvable
    orphen::ported::psm2::Vec3 position{};
    // Where the entity actually *is*. Normally the same as `position`, but an
    // entity attached to another's bone (+0x192) keeps a bone-local offset in
    // +0x20..+0x28, so its world origin is the translation of the root matrix
    // FUN_0020cdc0 built from the bone. FUN_0020c810's attached branch copies
    // the parent's screen position into the child for exactly this reason: the
    // depth sort and the debug box both need a world point, and the entity's own
    // position fields are not one.
    orphen::ported::psm2::Vec3 worldOrigin{};
    float facingRadians = 0.0f;
    float radius = 0.0f;
    float height = 0.0f;
    float groundHeight = 0.0f;
    bool descriptorResolved = false;
    // Entity +0x134. FUN_0020c810:140 copies it into the draw header and
    // substitutes 0x80 when it is zero, so this is an alpha on the GS's
    // 0x80 = x1.0 scale and **zero means fully opaque**, not invisible. The
    // chest cutscene's cross-fade is the only thing that drives it so far.
    std::uint8_t fadeLevel = 0;

    // Null when the type has no static descriptor (the map-streamed ids from
    // 0x272) or the grp record is not in any open bundle.
    const orphen::ported::model::Psc3Model *model = nullptr;
    // Slot in the texture cache, or kNoTextureSlot.
    int textureSlot = -1;
    // Entity +0xAC, the pose column every bone's track is indexed by.
    std::uint16_t poseColumn = 0;
    // One world matrix per submesh, already through FUN_0020d188's filter.
    //
    // This is built during the simulation step rather than at draw time, which
    // it has to be: the filter carries state from frame to frame, so building
    // it from render() would advance the animation once per drawn frame instead
    // of once per simulation frame, and not at all when running headless. The
    // original builds it in FUN_0020c5a8, inside the frame function, for the
    // same reason. Empty when the entity has no model.
    std::vector<orphen::ported::model::Matrix4> bonePalette;
    // The rest of what FUN_0020cdc0 builds the root matrix from: +0x14C scales
    // x and y, +0x150 scales z, and +0x154 / +0x158 are pitch and roll on top
    // of the facing in +0x5C. All of them are 1/1/0/0 for every entity in
    // s01_e024, but the root matrix is theirs, not the facing's alone.
    float scale = 1.0f;      // +0x14C
    float scaleZ150 = 1.0f;  // +0x150
    float rotationX154 = 0.0f;
    float rotationY158 = 0.0f;

    // False for the lead player, which draws its own magenta box and ground
    // triangle through drawLeadPlayer. It is in this list only so its model
    // gets drawn with everything else.
    bool drawDebugBox = true;
  };

  using SceneObjectViewList = std::vector<SceneObjectView>;

} // namespace orphen::port
