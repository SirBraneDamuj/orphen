#pragma once

// Click-through ray probe for entity models.
//
// Reports *every* triangle along the ray in depth order, including the ones the
// renderer does not draw. That is the whole point: when a model has a hole, the
// question is not "what is in front" but "what should have been there and was
// not", and a probe that only reports drawn geometry cannot answer it.
//
// The probe works in viewer space and shares posedViewerVertex with the draw, so
// it tests the same triangles the renderer emits rather than a second opinion
// about where they are.

#include "ported/model/psc3_model.h"
#include "ported/model/psc3_skeleton.h"
#include "runtime/scene_object_view.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace orphen::harness
{

  // One posed vertex, in the viewer space drawObjectModel emits. Model and world
  // share the game's Z-up convention; the viewer is Y-up.
  inline orphen::ported::psm2::Vec3 posedViewerVertex(
      const orphen::ported::model::Psc3Model &model,
      const std::vector<orphen::ported::model::Matrix4> &palette,
      std::uint16_t vertexIndex)
  {
    const auto &vertex = model.vertices[vertexIndex];
    const std::size_t bone = vertex.boneIndex < palette.size() ? vertex.boneIndex : 0u;
    const auto world = orphen::ported::model::transformPoint(vertex.position, palette[bone]);
    return orphen::ported::psm2::Vec3{world.x, world.z, -world.y};
  }

  // The same bone transform applied to a normal, and left in the game's Z-up
  // space rather than folded to viewer space: VU1 0x01c2..0x01c5 rotates the
  // normal by the bone matrix and then dots it against the light directions,
  // which FUN_00200e38 uploads in game space. Rotation only -- the translation
  // column is skipped, matching the microprogram's 3x3 multiply.
  inline orphen::ported::psm2::Vec3 posedWorldNormal(
      const orphen::ported::model::Psc3Model &model,
      const std::vector<orphen::ported::model::Matrix4> &palette,
      std::uint16_t boneIndex,
      const orphen::ported::psm2::Vec3 &normal)
  {
    (void)model;
    const std::size_t bone = boneIndex < palette.size() ? boneIndex : 0u;
    const auto &m = palette[bone];
    orphen::ported::psm2::Vec3 rotated{
        normal.x * m[0] + normal.y * m[4] + normal.z * m[8],
        normal.x * m[1] + normal.y * m[5] + normal.z * m[9],
        normal.x * m[2] + normal.y * m[6] + normal.z * m[10]};

    // Renormalise. The palette matrices carry the entity's scale, so rotating a
    // unit normal by one does not give a unit normal back -- measured lengths
    // ran 0.35 on the type 0x62 enemies and 1.07 on the player, which
    // compressed the first group's shading almost flat and slightly over-lit
    // the second.
    //
    // This is not a fudge to make the numbers nicer: VU1 never sees a scaled
    // matrix. Micro-program 0x015 orthonormalises the whole bone palette before
    // any of it is used -- instructions 0x0033..0x0055 take ERLENG (reciprocal
    // length) of each row and scale the row by it, three rows per matrix, which
    // is exactly this normalisation done once per bone instead of once per
    // vertex. The port keeps the raw matrices because the same palette is used
    // for positions, where the scale is wanted, so the normalisation happens
    // here instead.
    const float lengthSquared =
        rotated.x * rotated.x + rotated.y * rotated.y + rotated.z * rotated.z;
    if (lengthSquared > 0.0f)
    {
      const float scale = 1.0f / std::sqrt(lengthSquared);
      rotated.x *= scale;
      rotated.y *= scale;
      rotated.z *= scale;
    }
    return rotated;
  }

  // Why a primitive the ray hit was not rasterised. kDrawn means it was.
  enum class ProbeSkipReason
  {
    kDrawn,
    kSkipFlag,        // primitive flags bit 0x20, FUN_00212058 line 84
    kVertexOutOfRange // an index past the parsed vertex table
  };

  struct ProbeHit
  {
    std::size_t viewIndex = 0;
    std::size_t slot = 0;
    std::int32_t typeId = 0;
    std::size_t primitiveIndex = 0;
    std::size_t cornerCount = 0;
    std::array<std::uint8_t, 4> bones{};
    std::uint16_t flags = 0;
    std::array<std::int16_t, 4> subdraws{};
    int chosenSubdraw = -1;
    int textureSlot = -1;
    std::uint16_t colourIndex = 0;
    // Along the ray, in viewer units from the camera.
    float distance = 0.0f;
    // True when the triangle faces the camera under the renderer's CCW winding.
    bool frontFacing = false;
    ProbeSkipReason skipReason = ProbeSkipReason::kDrawn;
    // Which of the fan's triangles was hit: 0 for corners 0-1-2, 1 for 0-2-3.
    int fanTriangle = 0;
  };

  // Every entity triangle the ray crosses, nearest first.
  std::vector<ProbeHit> probeEntityRay(const orphen::port::SceneObjectViewList &objects,
                                       const orphen::ported::psm2::Vec3 &origin,
                                       const orphen::ported::psm2::Vec3 &direction);

  void printProbeReport(std::ostream &output,
                        int pixelX,
                        int pixelY,
                        const orphen::ported::psm2::Vec3 &origin,
                        const orphen::ported::psm2::Vec3 &direction,
                        const std::vector<ProbeHit> &hits);

  // Screen pixel to a viewer-space ray, from the matrices the frame was drawn
  // with. Returns false when the projection is not invertible.
  bool unprojectPixel(const std::array<float, 16> &modelView,
                      const std::array<float, 16> &projection,
                      int viewportWidth,
                      int viewportHeight,
                      int pixelX,
                      int pixelY,
                      orphen::ported::psm2::Vec3 &origin,
                      orphen::ported::psm2::Vec3 &direction);

} // namespace orphen::harness
