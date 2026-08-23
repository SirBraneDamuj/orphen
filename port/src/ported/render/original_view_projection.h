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

    // DAT_00351FE8 / DAT_00351FEC / DAT_00351FF0, all 0.4. **This, not
    // kNearPlane, is where geometry is cut off.** kNearPlane only sets the
    // projection's depth mapping (FUN_0020bd58's z_screen = m22 + m32/z); it
    // clips nothing. The map's actual clip is FUN_0020a2c0's Sutherland-Hodgman
    // pass, which splits every edge crossing 0.4 and drops the vertices behind
    // it -- and it sees every primitive that could straddle a near plane at
    // all, because FUN_00209140 routes anything within
    // DAT_00351FCC + DAT_00351FD0 of the eye to FUN_00209ca0 and everything
    // else is wholly beyond 0.6.
    //
    // The 0.1 m between the two decides whole shots. In the s01_e012 "Big
    // ones!" cut the camera rests 0.31 m behind the cabin's near wall
    // (primitive 2672, the quad at x = -3.100): clipped at 0.4 the surviving
    // sliver is entirely off the left edge and the frame is clear, clipped at
    // 0.3 it covers three quarters of the screen. See README.
    inline constexpr float kGeometryNearClip = 0.4f;

    inline constexpr float kHalfPi = 1.57079601f;            // fGpffff8094 / -fGpffff8098
    inline constexpr float kEyeHeightOffset = 0.400000036f;  // fGpffff808c, 0x00351FFC

    // The GS geometry the projection lands in, measured from the repo's GS
    // dump: SCISSOR_1 = 640x224 and XYOFFSET_1 OFX=1728 OFY=1936, so the
    // visible half-extents about the 2048 px centre are 320 and 112.
    inline constexpr float kScreenHalfWidthPixels = 320.0f;
    inline constexpr float kScreenHalfHeightPixels = 112.0f;
    // The GS output 640x224 per field and it was displayed on a 4:3 screen, so
    // the pixels are not square and the display aspect is not 640/224. The 3D
    // viewport is letterboxed to this rather than filling the window.
    inline constexpr float kDisplayAspect = 4.0f / 3.0f;
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
  // **Both half-angles are the original's.** The projection is used exactly as
  // FUN_0020bec8 builds it, with no window-derived widening: horizontal comes
  // from projection.at(0,0) rather than from the vertical scaled by the window
  // aspect. The frustum is 320*16/scale by 112*16/(0.45*scale), a ratio of
  // exactly 9/7, and the caller is responsible for presenting it in a 4:3
  // viewport -- the shape the GS output was displayed at.
  //
  // This replaces an earlier "Hor+" adaptation that kept the vertical angle and
  // widened horizontally to fit the window. That showed more of the scene than
  // the game ever did, which is a fine thing to offer later but not while the
  // point is establishing what the original actually looked like.
  //
  // The game's own widescreen path (cGpffffb66e, a fixed 0.77 horizontal
  // squeeze, selected by FieldCameraView::widescreen) is still unused; it is a
  // 16:9 mode, not the shipped 4:3 framing.
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
