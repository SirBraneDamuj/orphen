#pragma once

// Native counterpart of the per-frame map visibility pass:
//
//   src/FUN_00209140.c (0x00209140)  the loop: reject, fade, depth sort, emit
//   src/FUN_002099d8.c (0x002099d8)  screen-space overlap against the player
//   src/FUN_00209928.c (0x00209928)  one edge of that overlap test
//
// The original walks every 0x80-record, decides whether it is visible, ramps
// its fade byte, and inserts a DMA `call` tag to the primitive's prebuilt
// packet into one of 4096 depth buckets. The packets themselves are built once
// at map load by FUN_00211230 and are not modelled here -- the port rasterises
// with GL, so this pass produces an ordered draw list instead.
//
// The fade is what makes walls see-through. A primitive fades only when it
// stands in front of the player, covers the player on screen, and is not
// already a blended material; the byte then walks down to kFadeFloor at one
// step per frame and back up to kFadeCeiling when the condition clears. At the
// ceiling the original emits 0, so 0 means "no fade at all".
//
// Because FUN_00209928's sign convention assumes a consistent screen-space
// winding, a back-facing primitive passes every edge test trivially and always
// reads as covering the player. That is not an accident of the port: it is why
// the near wall of a room goes translucent when the camera is outside it.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_view_projection.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orphen::ported::render
{

  // Constants read out of eeMemory.bin at the addresses FUN_00209140 uses.
  namespace visibility
  {
    inline constexpr float kNearReject = 0.4f;         // DAT_00351FC8
    inline constexpr float kNearClipMargin = 0.6f;     // DAT_00351FCC + DAT_00351FD0
    inline constexpr float kPlayerProbeOffset = 0.2f;  // DAT_00351FC4
    inline constexpr float kScreenBandRight = 0.1f;    // DAT_00351FD4
    inline constexpr float kScreenBandLeft = -0.1f;    // DAT_00351FD8
    inline constexpr float kFadeHeightBias = 0.38f;    // DAT_00351FDC / DAT_00351FF4
    inline constexpr float kMinimumSortDepth = 0.1f;   // DAT_00351FE0

    inline constexpr std::uint32_t kHiddenBit = 0x20;        // skip entirely
    inline constexpr std::uint32_t kTriangleBit = 0x4000;    // 3 corners rather than 4
    inline constexpr std::uint32_t kNeverFadeBit = 0x40;     // blended materials
    inline constexpr std::uint32_t kBattleHideBit = 0x400000;  // on 0x78 +0x04, mode 0xC only

    inline constexpr int kDepthBucketCount = 0x1000;
  } // namespace visibility

  // What the pass needs from the rest of the frame. The original reads these
  // out of globals; naming them here keeps the mapping checkable.
  struct MapVisibilityInput
  {
    Vec3 DAT_0058bed0_playerPosition{};  // the lead player's world position
    float DAT_0058bf08_playerHeadOffset = 0.0f;

    // DAT_00355628, set at map load from DAT_0032538c. Primitives beyond this
    // are not drawn at all.
    float drawDistance = 32.0f;

    // DAT_00355641. Non-zero disables the fade entirely.
    bool fadeDisabled = false;

    // DAT_00354d2c == 0xC. Enables the 0x400000 hide bit.
    bool battleMode = false;

    // DAT_00355700. Non-zero caps every primitive's fade at this value.
    std::uint8_t globalFadeCap = 0;

    // The cull frustum's half-tangent. The original tests |x| <= z, a fixed
    // 90 degrees, which is wider than the 67.4 it actually draws. A window
    // wider than about 17:9 needs more than that, so the caller passes
    // max(1.0, windowHalfTangent) and nothing is ever culled while visible.
    float horizontalCullHalfTangent = 1.0f;
    float verticalCullHalfTangent = 1.0f;
  };

  struct MapDrawItem
  {
    std::size_t primitiveIndex = 0;
    // 0 means fully opaque. Otherwise the raw fade byte, kFadeFloor..kFadeCeiling.
    std::uint8_t fade = 0;
    int depthBucket = 0;
    // True when the primitive straddles the near plane and the original would
    // have taken the FUN_00209ca0 clip path.
    bool nearClipped = false;
  };

  struct MapVisibilityReport
  {
    std::size_t primitiveCount = 0;
    std::size_t hiddenSkipped = 0;
    std::size_t nearRejected = 0;
    std::size_t sideRejected = 0;
    std::size_t drawDistanceRejected = 0;
    std::size_t nearClipped = 0;
    std::size_t faded = 0;
    std::size_t drawn = 0;

    // How far each candidate got through the fade gate, so a frame with no
    // fading says which condition stopped it rather than just "none".
    std::size_t fadeCandidates = 0;      // reached the gate at all
    std::size_t fadeBlockedByBlend = 0;  // flag 0x40
    std::size_t fadeBlockedByHeight = 0; // below the player's waist
    std::size_t fadeBlockedByBand = 0;   // outside the screen band or behind the player
    std::size_t fadeBlockedByOverlap = 0;// FUN_002099d8 said no

    // Of the primitives that survived, how many face the camera by their own
    // plane normal. The original culls the rest in VU1, so a healthy frame is
    // mostly front-facing; a mostly back-facing frame means GL's front-face
    // winding is inverted relative to FUN_0022c6e8's corner order.
    std::size_t drawnFrontFacing = 0;
    std::size_t drawnBackFacing = 0;
  };

  // Runs one frame of FUN_00209140. Mutates each record's dynamicFade, which
  // is per-frame state exactly as the original's +0x2E is.
  std::vector<MapDrawItem> FUN_00209140_buildDrawList(orphen::ported::psm2::Psm2RuntimeState &map,
                                                      const ViewProjection &viewProjection,
                                                      const MapVisibilityInput &input,
                                                      MapVisibilityReport *report = nullptr);

} // namespace orphen::ported::render
