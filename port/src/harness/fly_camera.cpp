#include "harness/fly_camera.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace orphen::harness
{

  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    constexpr float kMouseRadiansPerPixel = 0.0035f;
    constexpr float kPitchLimit = 1.55f;
    constexpr float kFastMultiplier = 4.0f;
    constexpr float kSlowMultiplier = 0.25f;
    constexpr float kSpeedStep = 1.25f;
    constexpr float kMinimumSpeed = 0.25f;
    constexpr float kMaximumSpeed = 200.0f;
    constexpr float kVerticalFovRadians = 60.0f * 3.14159265f / 180.0f;

    Vec3 sum(const Vec3 &left, const Vec3 &right) { return {left.x + right.x, left.y + right.y, left.z + right.z}; }
    Vec3 scaled(const Vec3 &value, float factor) { return {value.x * factor, value.y * factor, value.z * factor}; }

    // map_viewer.cpp's toViewerSpace: game (x, y, z) -> viewer (x, z, -y).
    void emitViewer(const Vec3 &game) { glVertex3f(game.x, game.z, -game.y); }

    Vec3 normalized(const Vec3 &value)
    {
      const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
      return length > 0.0f ? scaled(value, 1.0f / length) : value;
    }

    // Row-vector: v * M.
    Vec3 transformPoint(const Vec3 &point, const Matrix4 &matrix)
    {
      return {point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) + point.z * matrix.at(2, 0) + matrix.at(3, 0),
              point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) + point.z * matrix.at(2, 1) + matrix.at(3, 1),
              point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) + point.z * matrix.at(2, 2) + matrix.at(3, 2)};
    }

    // The inverse of an affine row-vector matrix. Not a transpose: the area
    // map's FUN_00214300 folds its zoom into the view as a uniform scale, so
    // the game's view is only rigid on the field camera.
    Matrix4 affineInverse(const Matrix4 &matrix)
    {
      const auto m = [&](std::size_t row, std::size_t column) { return matrix.at(row, column); };
      const float determinant = m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
                                m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
                                m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
      Matrix4 inverse = orphen::ported::render::FUN_0020bc38_identity();
      if (determinant == 0.0f)
      {
        return inverse;
      }
      const float reciprocal = 1.0f / determinant;
      inverse.at(0, 0) = (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) * reciprocal;
      inverse.at(0, 1) = (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * reciprocal;
      inverse.at(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * reciprocal;
      inverse.at(1, 0) = (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) * reciprocal;
      inverse.at(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * reciprocal;
      inverse.at(1, 2) = (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) * reciprocal;
      inverse.at(2, 0) = (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0)) * reciprocal;
      inverse.at(2, 1) = (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) * reciprocal;
      inverse.at(2, 2) = (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * reciprocal;
      // The translation row, -t * A^-1. Row 3 of `inverse` is still zero here,
      // so transformPoint applies the 3x3 alone.
      const Vec3 origin = transformPoint({-m(3, 0), -m(3, 1), -m(3, 2)}, inverse);
      inverse.at(3, 0) = origin.x;
      inverse.at(3, 1) = origin.y;
      inverse.at(3, 2) = origin.z;
      return inverse;
    }

    // A camera taken apart: where the eye is, and the world directions of
    // view-space +x, +y and +z, unit length.
    struct CameraFrame
    {
      Vec3 eye;
      Vec3 right;
      Vec3 down;
      Vec3 forward;
    };

    CameraFrame frameOf(const Matrix4 &view)
    {
      const Matrix4 inverse = affineInverse(view);
      CameraFrame frame;
      frame.eye = {inverse.at(3, 0), inverse.at(3, 1), inverse.at(3, 2)};
      frame.right = normalized({inverse.at(0, 0), inverse.at(0, 1), inverse.at(0, 2)});
      frame.down = normalized({inverse.at(1, 0), inverse.at(1, 1), inverse.at(1, 2)});
      frame.forward = normalized({inverse.at(2, 0), inverse.at(2, 1), inverse.at(2, 2)});
      return frame;
    }

    // The axes FUN_0020bec8's composition produces for a heading and pitch
    // with no roll, worked out of its Rz(yaw + pi/2) Rx(-pi/2 - pitch) diag(-1,
    // 1, -1) product. Using the same parameterisation means snapTo round-trips.
    CameraFrame frameFor(const Vec3 &eye, float yaw, float pitch)
    {
      const float cy = std::cos(yaw);
      const float sy = std::sin(yaw);
      const float cp = std::cos(pitch);
      const float sp = std::sin(pitch);
      return {eye, {sy, -cy, 0.0f}, {cy * sp, sy * sp, -cp}, {cy * cp, sy * cp, sp}};
    }
  } // namespace

  void FlyCamera::snapTo(const orphen::ported::render::ViewProjection &gameCamera)
  {
    const CameraFrame frame = frameOf(gameCamera.view);
    placeAt(frame.eye, std::atan2(frame.forward.y, frame.forward.x),
            std::asin(std::clamp(frame.forward.z, -1.0f, 1.0f)));
  }

  void FlyCamera::placeAt(const Vec3 &eye, float yawRadians, float pitchRadians)
  {
    eye_ = eye;
    yawRadians_ = yawRadians;
    pitchRadians_ = std::clamp(pitchRadians, -kPitchLimit, kPitchLimit);
  }

  void FlyCamera::frame(const Vec3 &centre, float radius)
  {
    // A sphere touches the top and bottom of the picture at radius / sin(fov /
    // 2); the margin leaves room around it.
    constexpr float kMargin = 1.6f;
    constexpr float kMinimumDistance = 1.5f;
    const float distance =
        std::max(kMinimumDistance, kMargin * radius / std::sin(kVerticalFovRadians * 0.5f));
    const CameraFrame view = frameFor(eye_, yawRadians_, pitchRadians_);
    eye_ = sum(centre, scaled(view.forward, -distance));
  }

  void FlyCamera::update(float deltaSeconds, const orphen::port::InputSnapshot &input)
  {
    if (input.flySpeedSteps != 0)
    {
      speed_ = std::clamp(speed_ * std::pow(kSpeedStep, static_cast<float>(input.flySpeedSteps)),
                          kMinimumSpeed, kMaximumSpeed);
    }

    // Mouse right turns right, which is a falling heading; mouse down looks
    // down. SDL's relative y grows downward.
    yawRadians_ -= input.flyLookX * kMouseRadiansPerPixel;
    pitchRadians_ = std::clamp(pitchRadians_ - input.flyLookY * kMouseRadiansPerPixel,
                               -kPitchLimit, kPitchLimit);

    float speed = speed_;
    if (input.flyFastHeld)
    {
      speed *= kFastMultiplier;
    }
    if (input.flySlowHeld)
    {
      speed *= kSlowMultiplier;
    }

    // Forward flies where the camera points; up is the world's, so Q/E behave
    // the same whatever the pitch.
    const CameraFrame frame = frameFor(eye_, yawRadians_, pitchRadians_);
    Vec3 motion = sum(scaled(frame.right, input.flyMoveRight), scaled(frame.forward, input.flyMoveForward));
    motion.z += input.flyMoveUp;
    const float length = std::sqrt(motion.x * motion.x + motion.y * motion.y + motion.z * motion.z);
    if (length > 1.0f)
    {
      motion = scaled(motion, 1.0f / length);
    }
    eye_ = sum(eye_, scaled(motion, speed * deltaSeconds));
  }

  Matrix4 FlyCamera::view() const
  {
    const CameraFrame frame = frameFor(eye_, yawRadians_, pitchRadians_);
    const std::array<Vec3, 3> axes = {frame.right, frame.down, frame.forward};

    Matrix4 view = orphen::ported::render::FUN_0020bc38_identity();
    for (std::size_t column = 0; column < 3; ++column)
    {
      view.at(0, column) = axes[column].x;
      view.at(1, column) = axes[column].y;
      view.at(2, column) = axes[column].z;
      view.at(3, column) = -(eye_.x * axes[column].x + eye_.y * axes[column].y + eye_.z * axes[column].z);
    }
    return view;
  }

  Matrix4 FlyCamera::viewFrom(const Matrix4 &otherView) const
  {
    return orphen::ported::render::FUN_0020bb58_multiply(affineInverse(otherView), view());
  }

  orphen::ported::render::GlCamera FlyCamera::glCamera(int framebufferWidth,
                                                       int framebufferHeight,
                                                       float nearPlane,
                                                       float farPlane) const
  {
    orphen::ported::render::GlCamera camera;
    const float aspect = static_cast<float>(std::max(framebufferWidth, 1)) /
                         static_cast<float>(std::max(framebufferHeight, 1));
    camera.verticalHalfTangent = std::tan(kVerticalFovRadians * 0.5f);
    camera.horizontalHalfTangent = camera.verticalHalfTangent * aspect;

    // glCameraFor's projection: y-down, +z forward view space in, GL clip out.
    auto &projection = camera.projection;
    projection.fill(0.0f);
    projection[0] = 1.0f / camera.horizontalHalfTangent;
    projection[5] = -1.0f / camera.verticalHalfTangent;
    projection[10] = (farPlane + nearPlane) / (farPlane - nearPlane);
    projection[11] = 1.0f;
    projection[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);

    // And its viewer-to-game fold, so viewer-space vertices go in.
    Matrix4 viewerToGame = orphen::ported::render::FUN_0020bc38_identity();
    viewerToGame.at(1, 1) = 0.0f;
    viewerToGame.at(1, 2) = 1.0f;
    viewerToGame.at(2, 1) = -1.0f;
    viewerToGame.at(2, 2) = 0.0f;
    camera.modelView = orphen::ported::render::FUN_0020bb58_multiply(viewerToGame, view()).element;
    return camera;
  }

  void drawCameraFrustumGizmo(const orphen::ported::render::ViewProjection &camera, float farDistance)
  {
    // Everything is placed in the game camera's own view space and taken back
    // to the world through its inverse, so the depths are the ones the game
    // clips at -- the near clip and DAT_00355628 are both view-space distances.
    const Matrix4 viewToWorld = affineInverse(camera.view);
    const float horizontal = camera.horizontalHalfTangent();
    const float vertical = camera.verticalHalfTangent();
    const float nearDistance = orphen::ported::render::constants::kGeometryNearClip;
    const auto world = [&](float x, float y, float z) { return transformPoint({x, y, z}, viewToWorld); };
    const Vec3 eye = world(0.0f, 0.0f, 0.0f);

    // Corners in the order top-left, top-right, bottom-right, bottom-left, as
    // the screen sees them: view-space -y is up.
    const auto corners = [&](float depth) {
      std::array<Vec3, 4> result;
      const float signX[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
      const float signY[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
      for (std::size_t index = 0; index < 4; ++index)
      {
        result[index] = world(signX[index] * horizontal * depth, signY[index] * vertical * depth, depth);
      }
      return result;
    };
    const auto nearCorners = corners(nearDistance);
    const auto farCorners = corners(farDistance);

    // The up marker stands just off the near plane's top edge, half as tall as
    // the plane is wide.
    const float markerHalfWidth = horizontal * nearDistance * 0.5f;
    const float markerBase = -vertical * nearDistance * 1.15f;
    const Vec3 markerLeft = world(-markerHalfWidth, markerBase, nearDistance);
    const Vec3 markerRight = world(markerHalfWidth, markerBase, nearDistance);
    const Vec3 markerTip = world(0.0f, markerBase - markerHalfWidth, nearDistance);
    const Vec3 lookEnd = world(0.0f, 0.0f, farDistance);

    const auto drawGizmo = [&](float alpha) {
      // The body: eye to the near plane.
      glLineWidth(2.0f);
      glColor4f(1.0f, 0.85f, 0.2f, alpha);
      glBegin(GL_LINES);
      for (std::size_t index = 0; index < 4; ++index)
      {
        emitViewer(eye);
        emitViewer(nearCorners[index]);
        emitViewer(nearCorners[index]);
        emitViewer(nearCorners[(index + 1) % 4]);
      }
      glEnd();

      glBegin(GL_TRIANGLES);
      emitViewer(markerLeft);
      emitViewer(markerRight);
      emitViewer(markerTip);
      glEnd();

      // The frustum proper: near plane out to the far one.
      glLineWidth(1.0f);
      glColor4f(1.0f, 0.85f, 0.2f, alpha * 0.6f);
      glBegin(GL_LINES);
      for (std::size_t index = 0; index < 4; ++index)
      {
        emitViewer(nearCorners[index]);
        emitViewer(farCorners[index]);
        emitViewer(farCorners[index]);
        emitViewer(farCorners[(index + 1) % 4]);
      }
      glEnd();

      // The view axis, so where it is looking reads even when the far plane is
      // off the screen.
      glColor4f(1.0f, 0.4f, 0.2f, alpha * 0.6f);
      glBegin(GL_LINES);
      emitViewer(eye);
      emitViewer(lookEnd);
      glEnd();
    };

    const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean lightingWasEnabled = glIsEnabled(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Twice: faint through the world, then solid where nothing is in front.
    glDisable(GL_DEPTH_TEST);
    drawGizmo(0.3f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    drawGizmo(1.0f);
    glDepthFunc(GL_LESS);

    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (textureWasEnabled == GL_TRUE)
    {
      glEnable(GL_TEXTURE_2D);
    }
    if (lightingWasEnabled == GL_TRUE)
    {
      glEnable(GL_LIGHTING);
    }
  }

} // namespace orphen::harness
