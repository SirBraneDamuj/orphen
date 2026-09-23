#include "ported/scene/area_map.h"

#include "ported/model/psc3_skeleton.h"

#include <cmath>

namespace orphen::ported::scene
{
  namespace render = orphen::ported::render;

  namespace
  {
    // The tuning block at 0x003520F8..0x00352174, dumped from SLUS_200.11. The
    // names are what FUN_00214300 does with each, not anything the binary says.
    constexpr float kDAT_003520f8_initialPitch = -1.22173023f;  // -70 degrees
    constexpr float kDAT_003520fc_initialZoom = 0.300000012f;
    constexpr float kDAT_00352100_panRate = 0.000500000024f;
    constexpr float kDAT_00352104_lightZ = -0.800000012f;
    constexpr float kDAT_00352108_lightStep = 0.0174532887f; // one degree
    constexpr float kDAT_0035210c_panAngleBias = 1.57079601f;
    constexpr float kDAT_00352110_zoomInLow = -0.610865116f;  // -35 degrees
    constexpr float kDAT_00352114_zoomInHigh = 0.610865116f;
    constexpr float kDAT_00352118_zoomInStep = 1.03999996f;
    constexpr float kDAT_0035211c_zoomOutHigh = 2.53072691f;  // 145 degrees
    constexpr float kDAT_00352120_zoomOutLow = -2.53072691f;
    constexpr float kDAT_00352124_zoomOutStep = 1.03999996f;
    constexpr float kDAT_00352128_zoomFloor = 0.0399999991f;
    constexpr float kDAT_0035212c_tiltUpLow = 1.13446379f;    // 65 degrees
    constexpr float kDAT_00352130_tiltUpHigh = 2.18166113f;   // 125 degrees
    constexpr float kDAT_00352134_tiltDownHigh = -1.13446379f;
    constexpr float kDAT_00352138_tiltDownLow = -2.18166113f;
    constexpr float kDAT_0035213c_tiltRate = 0.000500000024f;
    constexpr float kDAT_00352140_pitchFloor = -1.57079601f;  // straight down
    constexpr float kDAT_00352144_pitchCeiling = -0.0872664452f; // 5 degrees off level
    constexpr float kDAT_00352148_turnRateR1 = 0.00156250002f;
    constexpr float kDAT_0035214c_turnRateL1 = 0.00156250002f;
    constexpr float kDAT_0035216c_yawBias = 1.57079601f;
    constexpr float kDAT_00352170_pitchBias = -1.57079601f;

    // The two `lui at, 0x4000` / `lui at, 0x3d00` immediates FUN_00214300 keeps
    // in f23 and f22: the zoom ceiling, which doubles as the marker's scale
    // numerator, and the per-tick divisor every rate is multiplied by.
    constexpr float kZoomCeiling = 2.0f;
    constexpr float kTickScale = 0.03125f; // 1/32, the nominal frame's tick count

    // `lui at, 0x40a0` at 0x002147C0: the push down the view axis that the
    // scale leaves room for.
    constexpr float kEyeDistance = 5.0f;

    // FUN_00214300's own copies of the projection arguments, at 0x00352150..68.
    // Every one is equal to the field camera's -- the two calls to FUN_0020BD58
    // differ only in which block of constants they read -- but they are their
    // own addresses, so they are spelled out here rather than borrowed.
    constexpr float kProjectionScale = 7680.0f;          // `lui at, 0x45f0`
    constexpr float kDAT_00352150_widescreenRatio = 0.769999981f;
    constexpr float kDAT_00352160_verticalRatio = 0.449999988f;
    constexpr float kScreenCentre = 32768.0f;            // `lui at, 0x4700`
    constexpr float kDAT_00352164_screenZAtNear = 65534.0f;
    constexpr float kScreenZAtFar = 1.0f;                // `lui at, 0x3f80`
    constexpr float kDAT_00352168_nearPlane = 0.300000012f;
    constexpr float kFarPlane = 128.0f;                  // `lui at, 0x4300`, the stack argument

    // DAT_003555F4's shoulder bits. The low byte of the pad word is the PS2
    // buffer's second button byte, so L2 is 0x01 and R1 is 0x08.
    constexpr std::uint16_t kPadL1 = 0x0004;
    constexpr std::uint16_t kPadR1 = 0x0008;

    // FUN_00216510: normalise in place, but only if the vector is non-zero.
    Vec3 FUN_00216510_normalise(Vec3 vector)
    {
      if (vector.x == 0.0f && vector.y == 0.0f && vector.z == 0.0f)
      {
        return vector;
      }
      const float length =
          std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
      return {vector.x / length, vector.y / length, vector.z / length};
    }
  } // namespace

  void AreaMap::FUN_00213ef0_open(const Vec3 &DAT_0058bed0_leadPosition, float fGpffffb6d4_fieldYaw)
  {
    // FUN_00213EF0:11-19. The three-word copy of slot 0's position runs before
    // the slot is respawned, which is the whole reason the map opens centred on
    // the player rather than on the origin.
    DAT_0055f090_focus_ = DAT_0058bed0_leadPosition;

    // FUN_00213EF0:81-83, the four gp-relative seeds. The yaw comes from the
    // live field camera, the other two from constants.
    DAT_00355a50_markerReset_ = 1;
    DAT_00355a54_pitch_ = kDAT_003520f8_initialPitch;
    DAT_00355a58_yaw_ = fGpffffb6d4_fieldYaw;
    DAT_00355a5c_zoom_ = kDAT_003520fc_initialZoom;
    open_ = true;
  }

  void AreaMap::FUN_002141d8_close()
  {
    open_ = false;
  }

  AreaMapStep AreaMap::FUN_00214300_step(const AreaMapPad &pad,
                                         std::uint32_t DAT_003555bc_frameTicks,
                                         bool cGpffffb66e_widescreen)
  {
    AreaMapStep step;
    const float frameTicks = static_cast<float>(static_cast<std::int32_t>(DAT_003555bc_frameTicks));

    // 0x0021437C-0x002143A4. The marker is rescaled before anything moves, so
    // it uses the zoom the *previous* frame ended on.
    step.markerScale = kZoomCeiling / DAT_00355a5c_zoom_;
    if (DAT_00355a50_markerReset_ != 0)
    {
      DAT_00355a50_markerReset_ = 0;
      step.markerStateReset = true;
    }

    // 0x002143A8-0x00214414. The key light, then its bearing advanced one
    // degree. The advance is per *frame*, not per tick, so it runs at whatever
    // rate the frame does.
    step.DAT_003439c8_lightDirection =
        FUN_00216510_normalise({std::cos(DAT_00354c60_lightBearing_),
                                std::sin(DAT_00354c60_lightBearing_),
                                kDAT_00352104_lightZ});
    DAT_00354c60_lightBearing_ = orphen::ported::model::FUN_00216690_wrap_angle(
        DAT_00354c60_lightBearing_ + kDAT_00352108_lightStep);

    // 0x00214400-0x002144A4. Pan the focus along the movement stick, turned
    // into the view's frame by the current yaw. Both the yaw and the zoom read
    // here are this frame's *input* values -- the stick updates below happen
    // afterwards.
    const float panHeading =
        (pad.DAT_003555e4_moveAngle + DAT_00355a58_yaw_) - kDAT_0035210c_panAngleBias;
    const float panRate = kDAT_00352100_panRate * frameTicks * kTickScale;
    DAT_0055f090_focus_.x += ((pad.DAT_003555e8_moveMagnitude * std::cos(panHeading)) /
                              DAT_00355a5c_zoom_) *
                             panRate;
    DAT_0055f090_focus_.y += ((pad.DAT_003555e8_moveMagnitude * std::sin(panHeading)) /
                              DAT_00355a5c_zoom_) *
                             panRate;

    // 0x002144A8-0x002145F0. The camera stick, sector by sector. Nothing here
    // runs while it is inside FUN_0023B3F0's deadzone, because that leaves the
    // magnitude at exactly 0.
    if (pad.DAT_003555f0_cameraMagnitude > 0.0f)
    {
      const float angle = pad.DAT_003555ec_cameraAngle;
      if (kDAT_00352110_zoomInLow < angle && angle < kDAT_00352114_zoomInHigh)
      {
        // Held right: multiply in, clamp at the ceiling. Not tick-scaled, so a
        // dropped frame really does zoom less.
        const float zoomed = DAT_00355a5c_zoom_ * kDAT_00352118_zoomInStep;
        DAT_00355a5c_zoom_ = zoomed > kZoomCeiling ? kZoomCeiling : zoomed;
      }
      else if (kDAT_0035211c_zoomOutHigh < angle || angle < kDAT_00352120_zoomOutLow)
      {
        // Held left: divide out, clamp at the floor.
        const float zoomed = DAT_00355a5c_zoom_ / kDAT_00352124_zoomOutStep;
        DAT_00355a5c_zoom_ = zoomed < kDAT_00352128_zoomFloor ? kDAT_00352128_zoomFloor : zoomed;
      }

      // The tilt test is separate, not an else: the zoom-in branch falls into
      // it at 0x002144E8. The two sectors never overlap, so that only matters
      // for how the code reads.
      if ((kDAT_0035212c_tiltUpLow < angle && angle < kDAT_00352130_tiltUpHigh) ||
          (angle < kDAT_00352134_tiltDownHigh && kDAT_00352138_tiltDownLow < angle))
      {
        DAT_00355a54_pitch_ -= pad.DAT_003555f0_cameraMagnitude * std::sin(angle) *
                               kDAT_0035213c_tiltRate * frameTicks * kTickScale;
        if (DAT_00355a54_pitch_ < kDAT_00352140_pitchFloor)
        {
          DAT_00355a54_pitch_ = kDAT_00352140_pitchFloor;
        }
        if (kDAT_00352144_pitchCeiling < DAT_00355a54_pitch_)
        {
          DAT_00355a54_pitch_ = kDAT_00352144_pitchCeiling;
        }
      }
    }

    // 0x002145F4-0x00214648. R1 wins over L1 when both are down.
    if ((pad.DAT_003555f4_held & kPadR1) != 0)
    {
      DAT_00355a58_yaw_ = orphen::ported::model::FUN_00216690_wrap_angle(
          DAT_00355a58_yaw_ - frameTicks * kDAT_00352148_turnRateR1);
    }
    else if ((pad.DAT_003555f4_held & kPadL1) != 0)
    {
      DAT_00355a58_yaw_ = orphen::ported::model::FUN_00216690_wrap_angle(
          DAT_00355a58_yaw_ + frameTicks * kDAT_0035214c_turnRateL1);
    }

    // 0x00214674-0x00214708. The projection, which is the field camera's down
    // to the last constant -- the zoom is in the view matrix, not here.
    const render::Matrix4 projection = render::FUN_0020bd58_projection(
        kProjectionScale,
        cGpffffb66e_widescreen ? kDAT_00352150_widescreenRatio : 1.0f,
        kDAT_00352160_verticalRatio,
        kScreenCentre,
        kScreenCentre,
        kDAT_00352164_screenZAtNear,
        kScreenZAtFar,
        kDAT_00352168_nearPlane,
        kFarPlane);

    // 0x00214740-0x0021484C. Five component matrices, composed left to right
    // exactly as FUN_0020BEC8 composes its own five.
    render::Matrix4 translation = render::FUN_0020bc38_identity();
    render::Matrix4 yaw = render::FUN_0020bc38_identity();
    render::Matrix4 pitch = render::FUN_0020bc38_identity();
    render::Matrix4 scale = render::FUN_0020bc38_identity();
    render::Matrix4 eyeOffset = render::FUN_0020bc38_identity();

    render::FUN_0020bb48_setTranslation(translation,
                                        -DAT_0055f090_focus_.x,
                                        -DAT_0055f090_focus_.y,
                                        -DAT_0055f090_focus_.z);
    render::FUN_0020bae0_setRotationZ(yaw, DAT_00355a58_yaw_ + kDAT_0035216c_yawBias);
    render::FUN_0020ba30_setRotationX(pitch, kDAT_00352170_pitchBias - DAT_00355a54_pitch_);
    // The x and z flips FUN_0020BEC8 spends a plain scale(-1, 1, -1) on, with
    // the zoom folded into all three.
    render::FUN_0020bb38_setScale(scale,
                                  -DAT_00355a5c_zoom_,
                                  DAT_00355a5c_zoom_,
                                  -DAT_00355a5c_zoom_);
    render::FUN_0020bb48_setTranslation(eyeOffset, 0.0f, 0.0f, kEyeDistance);

    step.camera.view = render::FUN_0020bb58_multiply(translation, yaw);
    step.camera.view = render::FUN_0020bb58_multiply(step.camera.view, pitch);
    step.camera.view = render::FUN_0020bb58_multiply(step.camera.view, scale);
    step.camera.view = render::FUN_0020bb58_multiply(step.camera.view, eyeOffset);

    step.camera.projection = projection;
    step.camera.DAT_003555a0_depthScale = projection.at(2, 2);
    step.camera.DAT_003555a4_depthOffset = projection.at(3, 2);
    step.camera.viewProjection =
        render::FUN_0020bb58_multiply(step.camera.view, step.camera.projection);

    return step;
  }

} // namespace orphen::ported::scene
