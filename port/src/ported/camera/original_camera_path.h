#pragma once

// The scripted camera path: a natural cubic spline for the eye, one for the
// look-at, and one for the roll/zoom pair.
//
//   src/FUN_00266460.c  the tridiagonal solve, Numerical Recipes' `spline`
//   src/FUN_00266610.c  the interval search, `locate`
//   src/FUN_00266668.c  the evaluation, `splint` in Horner form
//   src/FUN_00266a78.c  build a 3-component curve (the eye, the look-at)
//   src/FUN_00266738.c  build a 2-component curve (the roll/zoom pair)
//   src/FUN_00266ce8.c  sample the 3-component curve
//   src/FUN_00266988.c  sample the 2-component curve
//   src/FUN_00217fe8.c  install all three and hand the first point to
//                       FUN_00217d70
//   src/FUN_00218158.c  sample all three at elapsed/duration and publish
//
// The coefficient array FUN_00266460 produces is the second derivative over
// six: FUN_00266668 multiplies it by 3 for the quadratic term and divides the
// difference by h for the cubic one, which is M/2 and (M'-M)/(6h) once the six
// is put back. Reading it as "M/6" is what makes the two functions agree with
// the textbook pair.
//
// Only the uniform parameterisation is reachable from the chest cutscene:
// FUN_00266a78's fourth argument is 0 there, which spaces the knots i/(n-1).
// FUN_00266738 is called with -1 instead, meaning "keep the knots already in
// the struct" -- and FUN_00266708 has just copied the eye curve's knots into
// it, so the two curves share a parameterisation. The port passes the same
// uniform knots to both rather than reproducing the copy.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace orphen::ported::camera
{

  using orphen::ported::psm2::Vec3;

  // FUN_00266a78:25 reports ER_SPLINE and stops above this.
  inline constexpr std::size_t kMaxSplinePoints = 16;

  // FUN_00266610: the largest knot index at or below `t`, clamped so that
  // index + 1 is still addressable.
  std::size_t FUN_00266610_locate(std::span<const float> knots, float t);

  // One scalar channel of a curve. Empty until build() is called; a curve of
  // one point is that point, and a curve of two is a straight lerp -- both are
  // FUN_00266ce8's own short-circuits rather than degenerate spline maths.
  class CubicSpline
  {
  public:
    void build(std::span<const float> knots, std::span<const float> values);
    void clear() { count_ = 0; }

    std::size_t pointCount() const { return count_; }
    // FUN_00266668, with FUN_00266ce8's two short-circuits in front of it.
    float evaluate(float t) const;

  private:
    std::size_t count_ = 0;
    std::array<float, kMaxSplinePoints> knot_{};
    std::array<float, kMaxSplinePoints> value_{};
    // FUN_00266460's output, the second derivative over six.
    std::array<float, kMaxSplinePoints> coefficient_{};
  };

  struct CameraPathSample
  {
    Vec3 eye{};
    Vec3 lookAt{};
    float roll = 0.0f;     // uGpffffb6dc / DAT_0035564c
    float zoomLog2 = 1.0f; // fGpffffb6e8 / DAT_00355658
  };

  // FUN_00218230: log2(2x). The projection's x scale is 2^zoomLog2 * 3840, so
  // a curve value of 1.0 is the shipped 7680 and 3.0 is three times that.
  float FUN_00218230_zoomLog2(float scale);

  // The three curves FUN_00217fe8 installs, sampled together by FUN_00218158.
  class CameraPath
  {
  public:
    // `zoomScales` are pre-FUN_00218230 values -- what the caller writes into
    // the scratch block, before the log.
    void FUN_00217fe8_build(std::span<const Vec3> eyePoints,
                            std::span<const float> rollValues,
                            std::span<const float> zoomScales,
                            std::span<const Vec3> lookAtPoints);
    void clear();

    bool active() const { return eye_[0].pointCount() != 0; }
    CameraPathSample sample(float t) const;

  private:
    std::array<CubicSpline, 3> eye_{};
    std::array<CubicSpline, 3> lookAt_{};
    std::array<CubicSpline, 2> rollZoom_{};
  };

} // namespace orphen::ported::camera
