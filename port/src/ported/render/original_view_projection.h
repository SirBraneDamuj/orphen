#pragma once

// Native counterpart of the camera matrix builders:
//
//   src/FUN_0020bec8.c (0x0020bec8)  the field view + projection composer
//   src/FUN_0020bd58.c (0x0020bd58)  the projection matrix itself
//   src/FUN_0020bc38.c               identity
//   src/FUN_0020bb48.c               translation
//   src/FUN_0020bae0.c               rotation about Z
//   src/FUN_0020ba30.c               rotation about X
//   src/FUN_0020bb38.c               diagonal scale
//   src/FUN_0020bb58.c               multiply (VU0 microprogram 0x60)
//
// The original stores three results: the view matrix at 0x0058BC80, the
// projection at 0x0058BD00, and their product at 0x0058BCC0. It also caches
// the projection's two depth terms in DAT_003555A0 / DAT_003555A4, which are
// what FUN_00209140 uses for its depth sort key.
//
// Conventions
// -----------
// Matrices are row-major float[16] in *row-vector* order: v' = v * M, so the
// translation lives in row 3 (elements 12..14). That is the PS2 VU0 layout the
// original uses, where vf10..vf13 hold the rows and the macro-mode sequence is
// ACC = vf10*x; ACC += vf11*y; ACC += vf12*z; result = ACC + vf13.
//
// The view space this produces is left-handed and y-down, matching the GS:
// +x right, +y down, +z into the screen. glCameraFor() converts it for GL.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>

namespace orphen::ported::render
{

  using orphen::ported::psm2::Vec3;

  struct Matrix4
  {
    std::array<float, 16> element{};

    float &at(std::size_t row, std::size_t column) { return element[row * 4 + column]; }
    float at(std::size_t row, std::size_t column) const { return element[row * 4 + column]; }
  };

  // --- the primitives FUN_0020bec8 composes -------------------------------
  Matrix4 FUN_0020bc38_identity();
  void FUN_0020bb48_setTranslation(Matrix4 &matrix, float x, float y, float z);
  void FUN_0020bae0_setRotationZ(Matrix4 &matrix, float angleRadians);
  void FUN_0020ba30_setRotationX(Matrix4 &matrix, float angleRadians);
  void FUN_0020bb38_setScale(Matrix4 &matrix, float x, float y, float z);
  Matrix4 FUN_0020bb58_multiply(const Matrix4 &first, const Matrix4 &second);

  // FUN_0020bd58. Argument names follow what the values turn out to mean:
  // the disassembly passes the output pointer in a0 and nine floats in
  // f12..f19 plus one stack slot, which Ghidra renders as a shifted argument
  // list. See docs/rendering_pipeline_analysis.md.
  Matrix4 FUN_0020bd58_projection(float scale,
                                  float horizontalRatio,
                                  float verticalRatio,
                                  float screenCentreX,
                                  float screenCentreY,
                                  float screenZAtNear,
                                  float screenZAtFar,
                                  float nearPlane,
                                  float farPlane);

  // --- the shipped constants ----------------------------------------------
  // All read out of eeMemory.bin at the gp-relative addresses FUN_0020bec8
  // uses (gp = 0x00359F70).
  namespace constants
  {
    inline constexpr float kProjectionScaleBase = 3840.0f;   // 7680.0 * 0.5, FUN_0020bec8:0x0020c0b0
    inline constexpr float kWidescreenHorizontal = 0.77f;    // uGpffff809c, 0x0035200C
    inline constexpr float kVerticalRatio = 0.45f;           // uGpffff80ac, 0x0035201C
    inline constexpr float kScreenCentre = 32768.0f;         // GS 12.4 -> 2048 px
    inline constexpr float kScreenZAtNear = 65534.0f;        // uGpffff80b0, 0x00352020
    inline constexpr float kScreenZAtFar = 1.0f;
    inline constexpr float kNearPlane = 0.3f;                // uGpffff80b4, 0x00352024
    inline constexpr float kFarPlane = 128.0f;               // 0x43000000, the stack argument

    inline constexpr float kHalfPi = 1.57079601f;            // fGpffff8094 / -fGpffff8098
    inline constexpr float kEyeHeightOffset = 0.400000036f;  // fGpffff808c, 0x00351FFC

    // The GS geometry the projection lands in, measured from the repo's GS
    // dump: SCISSOR_1 = 640x224 and XYOFFSET_1 OFX=1728 OFY=1936, so the
    // visible half-extents about the 2048 px centre are 320 and 112.
    inline constexpr float kScreenHalfWidthPixels = 320.0f;
    inline constexpr float kScreenHalfHeightPixels = 112.0f;
  } // namespace constants

  // What FUN_0020bec8 reads out of the camera globals each frame.
  struct FieldCameraView
  {
    Vec3 eye{};              // DAT_0058C0A8/AC/B0, before the +0.4 height offset
    float yawRadians = 0.0f;   // fGpffffb6d4
    float pitchRadians = 0.0f; // fGpffffb6d8
    float rollRadians = 0.0f;  // uGpffffb6dc
    // fGpffffb6e8: a log2 zoom. The projection x scale is
    // powf(2, zoomLog2) * 3840, so 1.0 is the shipped default of 7680.
    float zoomLog2 = 1.0f;
    // cGpffffb66e. The port does not use this -- see glCameraFor() -- but it
    // is carried so the shipped value stays visible.
    bool widescreen = false;
  };

  struct ViewProjection
  {
    Matrix4 view;            // 0x0058BC80
    Matrix4 projection;      // 0x0058BD00
    Matrix4 viewProjection;  // 0x0058BCC0

    float DAT_003555a0_depthScale = 0.0f;   // projection row 2 column 2
    float DAT_003555a4_depthOffset = 0.0f;  // projection row 3 column 2

    // Transform a world point into the original's view space.
    Vec3 toViewSpace(const Vec3 &world) const;

    // FUN_00209140:361. The depth sort key before the >> 4 and the clamp.
    float screenDepth(float viewZ) const { return DAT_003555a0_depthScale + DAT_003555a4_depthOffset / viewZ; }

    // Half-angle tangents implied by the projection and the GS screen
    // geometry: tan = halfExtentPixels * 16 / scale.
    float horizontalHalfTangent() const;
    float verticalHalfTangent() const;
  };

  ViewProjection FUN_0020bec8_build(const FieldCameraView &camera);

  // --- host adaptation -----------------------------------------------------

  // Matrices ready for glLoadMatrixf (column-major, column-vector order).
  //
  // The projection preserves the original's vertical half-angle exactly and
  // widens horizontally to the window -- "Hor+". At 4:3 that reproduces the
  // shipped framing; wider windows reveal more to the sides and never crop.
  // This deliberately does not use the game's own widescreen path
  // (cGpffffb66e, a fixed 0.77 horizontal squeeze), which is fixed at roughly
  // 16:9 rather than following the window.
  struct GlCamera
  {
    std::array<float, 16> projection{};
    std::array<float, 16> modelView{};

    // The horizontal half-tangent actually used, after the window widened it.
    float horizontalHalfTangent = 0.0f;
    float verticalHalfTangent = 0.0f;
  };

  GlCamera glCameraFor(const ViewProjection &viewProjection,
                       int framebufferWidth,
                       int framebufferHeight,
                       float nearPlane,
                       float farPlane);

} // namespace orphen::ported::render
