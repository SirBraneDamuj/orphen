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
