#include "ported/camera/original_camera_path.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::camera
{
  namespace
  {
    // FUN_00266a78:48-54 with param_4 == 0.
    std::array<float, kMaxSplinePoints> uniformKnots(std::size_t count)
    {
      std::array<float, kMaxSplinePoints> knots{};
      if (count < 2)
      {
        return knots;
      }
      for (std::size_t index = 0; index < count; ++index)
      {
        knots[index] = static_cast<float>(index) / static_cast<float>(count - 1);
      }
      return knots;
    }
  } // namespace

  std::size_t FUN_00266610_locate(std::span<const float> knots, float t)
  {
    // The original's binary search, kept in its own shape: `low` walks up past
    // every knot below t, `high` closes on it, and the result is backed off by
    // one so that index + 1 is a valid knot.
    std::size_t low = 0;
    std::size_t high = knots.empty() ? 0 : knots.size() - 1;
    std::size_t probe = high;
    while (low < high)
    {
      probe /= 2;
      if (knots[probe] < t)
      {
        low = probe + 1;
        probe = high;
      }
      high = probe;
      probe = low + high;
    }
    return low > 0 ? low - 1 : 0;
  }

  void CubicSpline::build(std::span<const float> knots, std::span<const float> values)
  {
    count_ = std::min({knots.size(), values.size(), kMaxSplinePoints});
    std::copy_n(knots.begin(), count_, knot_.begin());
    std::copy_n(values.begin(), count_, value_.begin());
    coefficient_.fill(0.0f);

    // FUN_00266460 is only called for three points or more; below that
    // FUN_00266ce8 never reads the coefficients.
    if (count_ < 3)
    {
      return;
    }

    // FUN_00266460. `interval` is the scratch h[], `slope` the divided
    // differences, and `diagonal` overwrites `slope` in place in the original.
    std::array<float, kMaxSplinePoints> interval{};
    std::array<float, kMaxSplinePoints> slope{};
    std::array<float, kMaxSplinePoints> diagonal{};
    for (std::size_t index = 0; index + 1 < count_; ++index)
    {
      interval[index] = knot_[index + 1] - knot_[index];
      slope[index] = (value_[index + 1] - value_[index]) / interval[index];
    }

    // Natural ends: the second derivative is zero at both.
    coefficient_[0] = 0.0f;
    coefficient_[count_ - 1] = 0.0f;

    coefficient_[1] = (slope[1] - slope[0]) - interval[0] * coefficient_[0];
    diagonal[1] = 2.0f * (knot_[2] - knot_[0]);

    const std::size_t last = count_ - 2;
    for (std::size_t index = 1; index < last; ++index)
    {
      const float factor = interval[index] / diagonal[index];
      coefficient_[index + 1] = (slope[index + 1] - slope[index]) - coefficient_[index] * factor;
      diagonal[index + 1] = 2.0f * (knot_[index + 2] - knot_[index]) - interval[index] * factor;
    }

    coefficient_[last] -= interval[last] * coefficient_[last + 1];
    for (std::size_t index = last; index >= 1; --index)
    {
      coefficient_[index] =
          (coefficient_[index] - interval[index] * coefficient_[index + 1]) / diagonal[index];
    }
  }

  float CubicSpline::evaluate(float t) const
  {
    if (count_ == 0)
    {
      return 0.0f;
    }
    if (count_ < 2)
    {
      return value_[0];
    }
    if (count_ == 2)
    {
      return value_[0] + (value_[1] - value_[0]) * t;
    }

    const std::size_t index = FUN_00266610_locate({knot_.data(), count_}, t);
    const float base = coefficient_[index];
    const float offset = t - knot_[index];
    const float width = knot_[index + 1] - knot_[index];

    // FUN_00266668, unchanged: cubic in Horner form, with the coefficients read
    // as second-derivative-over-six.
    return ((((coefficient_[index + 1] - base) * offset) / width + base * 3.0f) * offset +
            ((value_[index + 1] - value_[index]) / width -
             (base + base + coefficient_[index + 1]) * width)) *
               offset +
           value_[index];
  }

  float FUN_00218230_zoomLog2(float scale)
  {
    // log(2x) / log(2). FUN_00305670 is the libc log; the ratio is log2(2x).
    return std::log(scale + scale) / std::log(2.0f);
  }

  void CameraPath::clear()
  {
    for (auto &axis : eye_)
    {
      axis.clear();
    }
    for (auto &axis : lookAt_)
    {
      axis.clear();
    }
    for (auto &channel : rollZoom_)
    {
      channel.clear();
    }
  }

  void CameraPath::FUN_00217fe8_build(std::span<const Vec3> eyePoints,
                                      std::span<const float> rollValues,
                                      std::span<const float> zoomScales,
                                      std::span<const Vec3> lookAtPoints)
  {
    clear();

    const auto buildVec3 = [](std::array<CubicSpline, 3> &target, std::span<const Vec3> points) {
      const std::size_t count = std::min(points.size(), kMaxSplinePoints);
      if (count == 0)
      {
        return;
      }
      const auto knots = uniformKnots(count);
      std::array<float, kMaxSplinePoints> channel{};
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        for (std::size_t index = 0; index < count; ++index)
        {
          const Vec3 &point = points[index];
          channel[index] = axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
        }
        target[axis].build({knots.data(), count}, {channel.data(), count});
      }
    };

    buildVec3(eye_, eyePoints);
    buildVec3(lookAt_, lookAtPoints);

    const std::size_t count = std::min({rollValues.size(), zoomScales.size(), kMaxSplinePoints});
    if (count != 0)
    {
      const auto knots = uniformKnots(count);
      std::array<float, kMaxSplinePoints> converted{};
      std::copy_n(rollValues.begin(), count, converted.begin());
      rollZoom_[0].build({knots.data(), count}, {converted.data(), count});
      for (std::size_t index = 0; index < count; ++index)
      {
        converted[index] = FUN_00218230_zoomLog2(zoomScales[index]);
      }
      rollZoom_[1].build({knots.data(), count}, {converted.data(), count});
    }
  }

  CameraPathSample CameraPath::sample(float t) const
  {
    CameraPathSample result;
    result.eye = {eye_[0].evaluate(t), eye_[1].evaluate(t), eye_[2].evaluate(t)};
    result.lookAt = {lookAt_[0].evaluate(t), lookAt_[1].evaluate(t), lookAt_[2].evaluate(t)};
    if (rollZoom_[0].pointCount() != 0)
    {
      result.roll = rollZoom_[0].evaluate(t);
      result.zoomLog2 = rollZoom_[1].evaluate(t);
    }
    return result;
  }

} // namespace orphen::ported::camera
