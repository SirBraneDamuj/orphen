#pragma once

// The screen smear: the previous frame drawn back over the current one.
//
//   src/FUN_00201a38.c  build and submit the feedback quad
//   src/FUN_00264448.c  opcode 0xC8, alpha only
//   src/FUN_00264470.c  opcode 0xC9, alpha plus the five transform shorts
//
// FUN_002000c0:214 calls FUN_00201a38 whenever either alpha global is set:
//
//   if ((DAT_00355661 != '\0') || (DAT_00354b88 != 0)) FUN_00201a38();
//
// It binds **the other framebuffer** as a texture -- FUN_002f9620 with
// `TBP0 = DAT_00354c2c * 0x8C0`, TBW 10, PSMCT24, and DAT_00354c2c is the
// parity of the buffer *not* being drawn into, so it is the frame the player
// is looking at. The two 640x224 field buffers sit at 0x00000 and 0x8C000.
// One alpha-blended textured tri-fan then covers the whole screen:
//
//   PRIM      0x155     TRI_FAN | TME | ABE | FST
//   ALPHA_1   0x44      (Cs - Cd) * As + Cd
//   RGBAQ     A<<24 | 0x808080, and TFX is MODULATE, where 0x80 is unity --
//             so the RGB passes the texture through and A is the whole effect
//   TEX0      TCC cleared, so the alpha really is RGBAQ's and not the
//             framebuffer's
//
// Each frame re-samples a frame that already contains the previous blend, so
// it compounds into an exponential trail rather than a single ghost. That is
// the smear.
//
// The quad lands in sort bucket 0x1006 (FUN_00201a38's tail links through
// `DAT_7000000c + 0x10064`, and FUN_00207de8's own tail shows the bucket
// stride is 0x10 with the head pointer at +4). The letterbox bars and the
// fullscreen fade are bucket 0x1007 and every text overlay is 0x1009, so the
// smear goes under all three: it covers the world, and the fade tints it.
//
// Not ported: the GS packet, and the 0.5-line offset FUN_00201a38 applies
// when `DAT_00354c34 != 0` (0x7FF6 instead of 0x7FFE) -- that is the interlace
// field parity, and the port draws progressively.

#include <array>
#include <cstdint>
#include <optional>

namespace orphen::ported::render
{

  // The virtual screen the original's coordinates are in. The frame is one
  // 224-line field, not 448 lines: FUN_00201a38's quad is +/-111 lines tall
  // and the buffers are 0x8C000 apart, which is exactly 640*224*4.
  inline constexpr float kFeedbackScreenWidth = 640.0f;
  inline constexpr float kFeedbackScreenHeight = 224.0f;

  class FrameFeedback
  {
  public:
    // FUN_00264448 (0xC8) and FUN_00264470 (0xC9). The five shorts are written
    // in stream order, and 0xC8 leaves them alone -- a script that wants a
    // plain ghost sets them once with 0xC9 and then drives alpha with 0xC8.
    void FUN_00264448_set_alpha(std::uint8_t alpha) { DAT_00355661_targetAlpha_ = alpha; }
    void FUN_00264470_set_alpha_and_transform(std::uint8_t alpha,
                                              std::int16_t offsetX,
                                              std::int16_t offsetY,
                                              std::int16_t scaleX,
                                              std::int16_t scaleY,
                                              std::int16_t rotationTenthDegrees);

    // FUN_002000c0:214's test.
    bool FUN_002000c0_armed() const
    {
      return DAT_00355661_targetAlpha_ != 0 || DAT_00354b88_currentAlpha_ != 0;
    }

    // FUN_00201a38's head, which is the only thing in it that has state: the
    // ramp toward the target, and the "< 2 draws nothing" cutoff. Returns the
    // RGBAQ alpha to draw with, or nullopt for a frame with no quad.
    //
    // Note the branch order. While `DAT_00354b88` is zero -- and only opcode
    // 0xBE's table entry 12 (`sh a0,-0x53e8(gp)`) ever makes it otherwise --
    // the function takes the target verbatim every frame and never ramps. So a
    // script driving 0xC8 per frame gets exactly the alpha it wrote, and its
    // own 0x90/0x91/0x92 parameter ramp is the fade. The ramp here is for the
    // callers that seed the current level instead.
    std::optional<int> FUN_00201a38_step();

    // Not an original entry point; FUN_0022a418:294 clears both alphas on a
    // scene load and the transform is left where it lay.
    void reset();

    std::uint8_t DAT_00355661_targetAlpha() const { return DAT_00355661_targetAlpha_; }
    std::int16_t DAT_00354b88_currentAlpha() const { return DAT_00354b88_currentAlpha_; }
    std::int16_t DAT_00343878_offsetX() const { return DAT_00343878_offsetX_; }
    std::int16_t DAT_0034387a_offsetY() const { return DAT_0034387a_offsetY_; }
    std::int16_t DAT_0034387c_scaleX() const { return DAT_0034387c_scaleX_; }
    std::int16_t DAT_0034387e_scaleY() const { return DAT_0034387e_scaleY_; }
    std::int16_t DAT_00343880_rotation() const { return DAT_00343880_rotation_; }

  private:
    std::uint8_t DAT_00355661_targetAlpha_ = 0;
    std::int16_t DAT_00354b88_currentAlpha_ = 0;
    std::int16_t DAT_00343878_offsetX_ = 0;
    std::int16_t DAT_0034387a_offsetY_ = 0;
    std::int16_t DAT_0034387c_scaleX_ = 0;
    std::int16_t DAT_0034387e_scaleY_ = 0;
    std::int16_t DAT_00343880_rotation_ = 0;
  };

  // What the renderer needs: four vertices in 640x224 screen pixels and the
  // 640x224 source rectangle they sample, plus the blend factor.
  struct FeedbackQuad
  {
    struct Vertex
    {
      float x = 0.0f;
      float y = 0.0f;
      float u = 0.0f; // source pixel, 0..640
      float v = 0.0f; // source pixel, 0..224
    };

    // GS alpha blending divides by 128, not 255: `As = A / 128`. 0x80 is
    // opaque and the 0x7E scripts favour is 98% of the previous frame.
    float blendFactor = 0.0f;
    std::array<Vertex, 4> vertices{};
  };

  // FUN_00201a38's body from LAB_00201B60 down, minus the packet. `alpha` is
  // what FUN_00201a38_step returned.
  //
  // The source rectangle never moves -- only the destination quad is scaled,
  // rotated and offset, which is why the effect stretches the picture instead
  // of panning across it.
  FeedbackQuad FUN_00201a38_build_quad(const FrameFeedback &state, int alpha);

  // Whether the two are close enough that the quad is a 1:1 copy, which is
  // worth knowing because that case does not need filtering.
  bool FUN_00201a38_is_identity(const FrameFeedback &state);

} // namespace orphen::ported::render
