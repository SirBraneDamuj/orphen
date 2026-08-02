#include "ported/render/original_view_projection.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::render
{

  Matrix4 FUN_0020bc38_identity()
  {
    Matrix4 matrix;
    matrix.at(0, 0) = 1.0f;
    matrix.at(1, 1) = 1.0f;
    matrix.at(2, 2) = 1.0f;
    matrix.at(3, 3) = 1.0f;
    return matrix;
  }

  void FUN_0020bb48_setTranslation(Matrix4 &matrix, float x, float y, float z)
  {
    matrix.at(3, 0) = x;
    matrix.at(3, 1) = y;
    matrix.at(3, 2) = z;
  }

  void FUN_0020bae0_setRotationZ(Matrix4 &matrix, float angleRadians)
  {
    const float cosine = std::cos(angleRadians);
    const float sine = std::sin(angleRadians);
    matrix.at(0, 0) = cosine;
    matrix.at(0, 1) = -sine;
    matrix.at(1, 0) = sine;
    matrix.at(1, 1) = cosine;
  }

  void FUN_0020ba30_setRotationX(Matrix4 &matrix, float angleRadians)
  {
    const float cosine = std::cos(angleRadians);
    const float sine = std::sin(angleRadians);
    matrix.at(1, 1) = cosine;
    matrix.at(1, 2) = -sine;
    matrix.at(2, 1) = sine;
    matrix.at(2, 2) = cosine;
  }

  void FUN_0020bb38_setScale(Matrix4 &matrix, float x, float y, float z)
  {
    matrix.at(0, 0) = x;
    matrix.at(1, 1) = y;
    matrix.at(2, 2) = z;
  }

  // Row-vector order: the result applies `first` and then `second`, so each
  // row of `first` is transformed by `second`.
  Matrix4 FUN_0020bb58_multiply(const Matrix4 &first, const Matrix4 &second)
  {
    Matrix4 result;
    for (std::size_t row = 0; row < 4; ++row)
    {
      for (std::size_t column = 0; column < 4; ++column)
      {
        float sum = 0.0f;
        for (std::size_t index = 0; index < 4; ++index)
        {
          sum += first.at(row, index) * second.at(index, column);
        }
        result.at(row, column) = sum;
      }
    }
    return result;
  }

  Matrix4 FUN_0020bd58_projection(float scale,
                                  float horizontalRatio,
                                  float verticalRatio,
                                  float screenCentreX,
                                  float screenCentreY,
                                  float screenZAtNear,
                                  float screenZAtFar,
                                  float nearPlane,
                                  float farPlane)
  {
    Matrix4 matrix = FUN_0020bc38_identity();

    matrix.at(0, 0) = horizontalRatio * scale;
    matrix.at(1, 1) = verticalRatio * scale;
    matrix.at(2, 0) = screenCentreX;
    matrix.at(2, 1) = screenCentreY;

    // z_screen = m22 + m32 / z, mapping nearPlane to screenZAtNear and
    // farPlane to screenZAtFar.
    matrix.at(2, 2) = (farPlane * screenZAtFar - nearPlane * screenZAtNear) / (farPlane - nearPlane);
    matrix.at(3, 2) = (nearPlane * farPlane * (screenZAtFar - screenZAtNear)) / (nearPlane - farPlane);
    matrix.at(2, 3) = 1.0f;
    matrix.at(3, 3) = 0.0f;

    return matrix;
  }

  Vec3 ViewProjection::toViewSpace(const Vec3 &world) const
  {
    return {world.x * view.at(0, 0) + world.y * view.at(1, 0) + world.z * view.at(2, 0) + view.at(3, 0),
            world.x * view.at(0, 1) + world.y * view.at(1, 1) + world.z * view.at(2, 1) + view.at(3, 1),
            world.x * view.at(0, 2) + world.y * view.at(1, 2) + world.z * view.at(2, 2) + view.at(3, 2)};
  }

  float ViewProjection::horizontalHalfTangent() const
  {
    const float scale = projection.at(0, 0);
    if (scale == 0.0f)
    {
      return 1.0f;
    }
    return constants::kScreenHalfWidthPixels * 16.0f / scale;
  }

  float ViewProjection::verticalHalfTangent() const
  {
    const float scale = projection.at(1, 1);
    if (scale == 0.0f)
    {
      return 1.0f;
    }
    return constants::kScreenHalfHeightPixels * 16.0f / scale;
  }

  ViewProjection FUN_0020bec8_build(const FieldCameraView &camera)
  {
    // FUN_0020bec8:0x0020bf20-0x0020c078. Five component matrices, each
    // seeded to identity, then composed left to right.
    Matrix4 translation = FUN_0020bc38_identity();
    Matrix4 yaw = FUN_0020bc38_identity();
    Matrix4 pitch = FUN_0020bc38_identity();
    Matrix4 roll = FUN_0020bc38_identity();
    Matrix4 axisFlip = FUN_0020bc38_identity();

    const float eyeZ = camera.eye.z + constants::kEyeHeightOffset;
    FUN_0020bb48_setTranslation(translation, -camera.eye.x, -camera.eye.y, -eyeZ);
    FUN_0020bae0_setRotationZ(yaw, camera.yawRadians + constants::kHalfPi);
    FUN_0020ba30_setRotationX(pitch, -constants::kHalfPi - camera.pitchRadians);
    FUN_0020bae0_setRotationZ(roll, camera.rollRadians);
    FUN_0020bb38_setScale(axisFlip, -1.0f, 1.0f, -1.0f);

    ViewProjection result;
    result.view = FUN_0020bb58_multiply(translation, yaw);
    result.view = FUN_0020bb58_multiply(result.view, pitch);
    result.view = FUN_0020bb58_multiply(result.view, roll);
    result.view = FUN_0020bb58_multiply(result.view, axisFlip);

    // 0x0020c07c-0x0020c0c8: the shipped fast path is an exact 1.0 compare,
    // and everything else goes through powf.
    const float scale = camera.zoomLog2 == 1.0f
                            ? constants::kProjectionScaleBase * 2.0f
                            : std::pow(2.0f, camera.zoomLog2) * constants::kProjectionScaleBase;

    result.projection = FUN_0020bd58_projection(scale,
                                                camera.widescreen ? constants::kWidescreenHorizontal : 1.0f,
                                                constants::kVerticalRatio,
                                                constants::kScreenCentre,
                                                constants::kScreenCentre,
                                                constants::kScreenZAtNear,
                                                constants::kScreenZAtFar,
                                                constants::kNearPlane,
                                                constants::kFarPlane);

    result.DAT_003555a0_depthScale = result.projection.at(2, 2);
    result.DAT_003555a4_depthOffset = result.projection.at(3, 2);
    result.viewProjection = FUN_0020bb58_multiply(result.view, result.projection);

    return result;
  }

  GlCamera glCameraFor(const ViewProjection &viewProjection,
                       int framebufferWidth,
                       int framebufferHeight,
                       float nearPlane,
                       float farPlane)
  {
    GlCamera camera;

    const float width = static_cast<float>(std::max(framebufferWidth, 1));
    const float height = static_cast<float>(std::max(framebufferHeight, 1));
    const float windowAspect = width / height;

    camera.verticalHalfTangent = viewProjection.verticalHalfTangent();
    camera.horizontalHalfTangent = camera.verticalHalfTangent * windowAspect;

    // GL is column-vector and column-major, so this is the transpose of the
    // usual row layout. The view space coming in is y-down and +z forward,
    // hence the negated y and the +z (rather than -z) w term.
    auto &projection = camera.projection;
    projection.fill(0.0f);
    projection[0] = 1.0f / camera.horizontalHalfTangent;
    projection[5] = -1.0f / camera.verticalHalfTangent;
    projection[10] = (farPlane + nearPlane) / (farPlane - nearPlane);
    projection[11] = 1.0f;
    projection[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);

    // The viewer emits vertices in its own space, which maps game (x, y, z) to
    // (x, z, -y), while the original's view matrix expects game space. Fold
    // the inverse of that remap in ahead of the view so both paths can share
    // one set of drawing code. The basis change is orientation preserving, so
    // it does not disturb the winding backface culling depends on.
    Matrix4 viewerToGame = FUN_0020bc38_identity();
    viewerToGame.at(1, 1) = 0.0f;
    viewerToGame.at(1, 2) = 1.0f;
    viewerToGame.at(2, 1) = -1.0f;
    viewerToGame.at(2, 2) = 0.0f;

    // The original's view matrix is row-major row-vector; GL wants
    // column-major column-vector. Those are the same 16 floats in the same
    // order -- the two transposes cancel -- which is also why the original's
    // translation row lands in GL's translation column.
    camera.modelView = FUN_0020bb58_multiply(viewerToGame, viewProjection.view).element;

    return camera;
  }

} // namespace orphen::ported::render
