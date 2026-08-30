#include "ported/render/original_frame_feedback.h"

#include <cmath>

namespace orphen::ported::render
{
  namespace
  {
    // FUN_00201a38's base quad, in the order the packet emits its four
    // (UV, XYZ) pairs. Coordinates are GS 12.4 fixed point relative to the
    // screen centre; UVs are the same format over the source frame.
    //
    //   0x5C/0x60  (-5104, -1776)  UV (0x0010, 0x0010)
    //   0x64/0x68  (-5104, +1776)  UV (0x0010, 0x0DF0)
    //   0x6C/0x70  (+5104, +1776)  UV (0x27F0, 0x0DF0)
    //   0x74/0x78  (+5104, -1776)  UV (0x27F0, 0x0010)
    //
    // 5104/16 is 319 and 1776/16 is 111, and the origin constants the function
    // adds last (0x7FF8 and 0x7FFE) are the screen centre: dropping them and
    // dividing by 16 puts the untransformed quad on exactly the 1..639 x
    // 1..223 rectangle its own UVs name, which is the 1:1 blit it has to be.
    // So the port never needs the GS XYOFFSET -- centre plus X/16 is the same
    // answer with one fewer constant to be wrong about.
    constexpr int kHalfWidthFixed = 5104;
    constexpr int kHalfHeightFixed = 1776;
    constexpr float kCentreX = kFeedbackScreenWidth * 0.5f;
    constexpr float kCentreY = kFeedbackScreenHeight * 0.5f;

    struct FixedVertex
    {
      int x = 0;
      int y = 0;
      float u = 0.0f;
      float v = 0.0f;
    };

    // `iVar3 * (scale + 0x400)` then `(negative ? + 0x3FF : +0) >> 10`, which
    // is a shift biased into truncating toward zero.
    int scaleTowardZero(int value, int scale)
    {
      const int product = value * (scale + 0x400);
      return (product < 0 ? product + 0x3FF : product) >> 10;
    }

    // FUN_0030bd20: float to int, truncating toward zero.
    int FUN_0030bd20_toInt(float value) { return static_cast<int>(value); }
  } // namespace

  void FrameFeedback::FUN_00264470_set_alpha_and_transform(std::uint8_t alpha,
                                                           std::int16_t offsetX,
                                                           std::int16_t offsetY,
                                                           std::int16_t scaleX,
                                                           std::int16_t scaleY,
                                                           std::int16_t rotationTenthDegrees)
  {
    DAT_00355661_targetAlpha_ = alpha;
    DAT_00343878_offsetX_ = offsetX;
    DAT_0034387a_offsetY_ = offsetY;
    DAT_0034387c_scaleX_ = scaleX;
    DAT_0034387e_scaleY_ = scaleY;
    DAT_00343880_rotation_ = rotationTenthDegrees;
  }

  void FrameFeedback::reset()
  {
    DAT_00355661_targetAlpha_ = 0;
    DAT_00354b88_currentAlpha_ = 0;
    DAT_00343878_offsetX_ = 0;
    DAT_0034387a_offsetY_ = 0;
    DAT_0034387c_scaleX_ = 0;
    DAT_0034387e_scaleY_ = 0;
    DAT_00343880_rotation_ = 0;
  }

  std::optional<int> FrameFeedback::FUN_00201a38_step()
  {
    if (!FUN_002000c0_armed())
    {
      return std::nullopt;
    }

    const int target = static_cast<int>(DAT_00355661_targetAlpha_);

    // `if (lVar12 == 0) { alpha = target; goto draw; }` -- no ramp, no cutoff.
    if (DAT_00354b88_currentAlpha_ == 0)
    {
      return target;
    }

    int current = static_cast<int>(DAT_00354b88_currentAlpha_);
    if (current < target)
    {
      ++current;
      if (target < current)
      {
        current = target;
      }
    }
    else if (current > target)
    {
      --current;
      if (current < target)
      {
        current = target;
      }
      // The floor is 1, not 0: a ramp that reaches zero would fall back into
      // the branch above and start taking the target verbatim again.
      if (current < 1)
      {
        current = 1;
      }
    }
    DAT_00354b88_currentAlpha_ = static_cast<std::int16_t>(current);

    return current < 2 ? std::nullopt : std::optional<int>{current};
  }

  bool FUN_00201a38_is_identity(const FrameFeedback &state)
  {
    return state.DAT_00343878_offsetX() == 0 && state.DAT_0034387a_offsetY() == 0 &&
           state.DAT_0034387c_scaleX() == 0 && state.DAT_0034387e_scaleY() == 0 &&
           state.DAT_00343880_rotation() == 0;
  }

  FeedbackQuad FUN_00201a38_build_quad(const FrameFeedback &state, int alpha)
  {
    FixedVertex vertices[4] = {
        {-kHalfWidthFixed, -kHalfHeightFixed, 1.0f, 1.0f},
        {-kHalfWidthFixed, +kHalfHeightFixed, 1.0f, 223.0f},
        {+kHalfWidthFixed, +kHalfHeightFixed, 639.0f, 223.0f},
        {+kHalfWidthFixed, -kHalfHeightFixed, 639.0f, 1.0f},
    };

    const int scaleX = state.DAT_0034387c_scaleX();
    const int scaleY = state.DAT_0034387e_scaleY();
    if (scaleX != 0 || scaleY != 0)
    {
      for (FixedVertex &vertex : vertices)
      {
        vertex.x = scaleTowardZero(vertex.x, scaleX);
        vertex.y = scaleTowardZero(vertex.y, scaleY);
      }
    }

    if (const int rotation = state.DAT_00343880_rotation(); rotation != 0)
    {
      // fGpffff8010 is 0.017453289 -- pi/180 -- so the stored value is tenths
      // of a degree.
      const float radians = (static_cast<float>(rotation) / 10.0f) *
                            0.017453288659453392f;
      const float sine = std::sin(radians);
      const float cosine = std::cos(radians);
      for (FixedVertex &vertex : vertices)
      {
        // y is doubled going in and halved coming out. The quad lives in a
        // 224-line field over a 640-pixel width, so a line is worth two
        // pixels; without the correction a 90 degree spin would come out
        // squashed to the field's own aspect.
        const float x = static_cast<float>(vertex.x);
        const float y = static_cast<float>(vertex.y) + static_cast<float>(vertex.y);
        vertex.x = FUN_0030bd20_toInt(x * cosine - y * sine);
        vertex.y = FUN_0030bd20_toInt(x * sine + y * cosine) / 2;
      }
    }

    const int offsetX = state.DAT_00343878_offsetX();
    const int offsetY = state.DAT_0034387a_offsetY();
    if (offsetX != 0 || offsetY != 0)
    {
      // Same field correction, so an offset is in pixels on both axes.
      const int offsetYHalved = (offsetY - (offsetY >> 31)) >> 1;
      for (FixedVertex &vertex : vertices)
      {
        vertex.x += offsetX;
        vertex.y += offsetYHalved;
      }
    }

    FeedbackQuad quad;
    quad.blendFactor = static_cast<float>(alpha) / 128.0f;
    if (quad.blendFactor > 1.0f)
    {
      quad.blendFactor = 1.0f;
    }
    for (std::size_t index = 0; index < quad.vertices.size(); ++index)
    {
      quad.vertices[index].x = static_cast<float>(vertices[index].x) / 16.0f + kCentreX;
      quad.vertices[index].y = static_cast<float>(vertices[index].y) / 16.0f + kCentreY;
      quad.vertices[index].u = vertices[index].u;
      quad.vertices[index].v = vertices[index].v;
    }
    return quad;
  }

} // namespace orphen::ported::render
