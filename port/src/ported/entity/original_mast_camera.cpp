#include "ported/entity/original_mast_camera.h"

#include "ported/camera/original_camera_path.h"
#include "ported/camera/original_field_camera.h"
#include "ported/render/original_view_projection.h"

#include <array>
#include <cmath>
#include <span>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    namespace render = orphen::ported::render;

    // Shot 1, over the player's shoulder toward the boss: half a turn off the
    // bearing, thirty degrees to one side, and five degrees of roll whose sign
    // is the only thing that separates the two sub-shots.
    inline constexpr float kDAT_003537dc_pi = 3.14159274101257f;
    inline constexpr float kDAT_003537e0_swing1 = 0.523598790168762f;
    inline constexpr float kDAT_003537e4_roll1 = -0.0872664973139763f;
    inline constexpr float kDAT_003537e8_pi = 3.14159274101257f;
    inline constexpr float kDAT_003537ec_swing2 = 0.523598790168762f;
    inline constexpr float kDAT_003537f0_roll2 = 0.0872664973139763f;
    // Shot 2, the same at five units with the boss's elevation folded in.
    inline constexpr float kDAT_003537f4_pitchBias = 1.57079637050629f;
    inline constexpr float kDAT_003537f8_pi = 3.14159274101257f;
    inline constexpr float kDAT_003537fc_swing1 = 0.523598790168762f;
    inline constexpr float kDAT_00353800_roll1 = -0.261799395084381f;
    inline constexpr float kDAT_00353804_swing2 = 0.523598790168762f;
    inline constexpr float kDAT_00353808_roll2 = 0.261799395084381f;
    // Shot 3, shot 2's geometry at four units, with two differences that
    // matter: the pitch goes straight on the eye's height instead of being
    // added to a fixed one, and the look-at is the boss itself rather than a
    // point pulled back along its facing.
    inline constexpr float kDAT_0035380c_pitchBias = 1.57079637050629f;
    inline constexpr float kDAT_00353810_pi = 3.14159274101257f;
    inline constexpr float kDAT_00353814_swing1 = 0.523598790168762f;
    inline constexpr float kDAT_00353818_roll1 = -0.261799395084381f;
    inline constexpr float kDAT_0035381c_swing2 = 0.523598790168762f;
    inline constexpr float kDAT_00353820_roll2 = 0.261799395084381f;
    // Shot 4's randomised orbit: a full turn, divided by 360 after the roll, so
    // the offset works out as fifteen to forty-four degrees in radians.
    inline constexpr float kDAT_00353824_turn = 6.28318405151367f;
    // Shot 5, the sweep that ping-pongs between two limits.
    inline constexpr float kDAT_00353828_base = 3.14159274101257f;
    inline constexpr float kDAT_0035382c_span1 = 0.698131680488586f;
    inline constexpr float kDAT_00353830_span2 = 0.698131680488586f;
    inline constexpr float kDAT_00353834_rateOut = 0.00174532680772245f;
    inline constexpr float kDAT_00353838_limitBase1 = 3.14159274101257f;
    inline constexpr float kDAT_0035383c_limitSpan1 = 0.698131680488586f;
    inline constexpr float kDAT_00353840_rateBack = 0.00174532680772245f;
    inline constexpr float kDAT_00353844_limitBase2 = 3.14159274101257f;
    inline constexpr float kDAT_00353848_limitSpan2 = 0.698131680488586f;
    // Shot 6's curve is authored in a bone's own space and turned by the boss's
    // facing less this, 97.93 degrees.
    inline constexpr float kDAT_0035384c_curveBias = 1.70915997028351f;
    // Shot 7's randomised orbit, the same full turn as shot 4's.
    inline constexpr float kDAT_00353850_turn = 6.28318405151367f;
    // Shot 9's three sub-shots are each a fixed swing off the *player's* facing
    // (DAT_0058BF0C), all three of them forty degrees: DAT_00353854,
    // DAT_00353858 and DAT_0035385C, read out of SLUS_200.11.
    inline constexpr float kDAT_00353854_swing1 = 0.698131680488586f;
    inline constexpr float kDAT_00353858_swing2 = 0.698131680488586f;
    inline constexpr float kDAT_0035385c_swing3 = 0.698131680488586f;
    // Shot 10: three fixed orbits round the boss and a fourth that sweeps past
    // the player. The roll differs per sub-shot, which is what tips the horizon
    // when the mast comes down.
    inline constexpr float kDAT_00353860_leadIn = 0.174532920122147f;
    inline constexpr float kDAT_00353864_swing1 = 0.523598790168762f;
    inline constexpr float kDAT_00353868_roll1 = -0.174532920122147f;
    inline constexpr float kDAT_0035386c_swing2 = 0.523598790168762f;
    inline constexpr float kDAT_00353870_roll2 = 0.261799395084381f;
    inline constexpr float kDAT_00353874_swing3 = 0.785398185253143f;
    inline constexpr float kDAT_00353878_roll3 = 0.174532920122147f;
    inline constexpr float kDAT_0035387c_sweepRate = 0.00174532680772245f;
    inline constexpr float kDAT_00353880_sweepSpan = 0.698131680488586f;
    // Shot 11, a one-shot pose five units either side of the boss.
    inline constexpr float kDAT_00353884_swing1 = 0.523598790168762f;
    inline constexpr float kDAT_00353888_swing2 = 0.523598790168762f;
    // Shot 12's eye is a fixed point on the deck -- DAT_0035388C/90/94 -- and
    // the only thing that moves is what it is pointed at.
    inline constexpr Vec3 kDAT_0035388c_deathEye{6.14900016784668f, 7.78000020980835f,
                                                 7.65399980545044f};

    // ---------------------------------------------- shot 6's spline, 0x00325DA8
    // Four points in bone 8's own space. DAT_00325DCC is the last of them,
    // which is what the shot holds once the 0xC80 ticks are up -- the same
    // array entry, not a separate constant.
    inline constexpr std::array<Vec3, 4> kDAT_00325da8_shot6Eye{{
        {-2.70399999618530f, 8.46899986267090f, 2.75800013542175f},
        {-0.319999992847443f, 8.16200065612793f, 0.629000008106232f},
        {3.31800007820129f, 5.90299987792969f, 0.629000008106232f},
        {5.95400018692017f, 2.45199990272522f, 0.629000008106232f},
    }};
    // DAT_00325D98, the one-point look-at curve FUN_00217E88 is handed. The
    // shot overwrites the look-at every frame, so this only ever decides where
    // FUN_00217D70 aims on the install frame.
    inline constexpr Vec3 kDAT_00325d98_shot6Look{0.0f, 0.0f, 0.0f};

    // ---------------------------------------------- shot 8's spline, 0x00325E00
    // Three points, and here they are a plain **offset** from the anchor rather
    // than a pose in a bone's space -- shot 8 adds them, where shot 6 rotates
    // first.
    inline constexpr std::array<Vec3, 3> kDAT_00325e00_shot8Eye{{
        {-2.25800004601479f, 21.6030006408691f, 1.37299997806549f},
        {-11.9210004806519f, 19.3449993133545f, 0.997000038623810f},
        {-18.8229999542236f, 12.1919998168945f, -0.118000000715256f},
    }};
    // DAT_00325DD8, indexed by the boss's +0x1BF: where the shot anchors for
    // each of the three swoops. The same table is handed to FUN_00217E88 as its
    // one-point look-at curve, which is why the two share a base address.
    inline constexpr std::array<Vec3, 3> kDAT_00325dd8_shot8Anchor{{
        {-6.5f, 5.0f, 2.0f},
        {6.5f, 5.0f, 2.0f},
        {0.0f, 5.0f, 2.0f},
    }};

    // The two curve durations, in the engine's ticks.
    inline constexpr int kShot6Duration = 0x0C80;
    inline constexpr int kShot8Duration = 0x1900;

    // DAT_00343880 and DAT_00355661, FUN_00201A38's screen smear. Shots 4 and
    // 12 arm the rotation; every shot from 4 up sets the alpha.
    inline constexpr std::int16_t kSmearRotation = 10;
    inline constexpr std::uint8_t kSmearAlpha = 0x50;

    // ----------------------------------------------------------- the globals
    struct DirectorState
    {
      std::int32_t DAT_00355310_installed = 0;
      std::int32_t DAT_00355314_shot = 0;
      std::int32_t DAT_00355318_priority = 0;
      std::int32_t DAT_0035531c_subShot = 0;
      std::int32_t DAT_00355320_builtFor = 0;
      // DAT_00355324 / DAT_00355328 / DAT_00355330: the running angle shots 4,
      // 5, 7 and 10 sweep, its accumulator, and the bearing the sweep started
      // from.
      float DAT_00355324_sweepAngle = 0.0f;
      float DAT_00355328_sweep = 0.0f;
      float DAT_00355330_sweepStart = 0.0f;
      // DAT_0035532E, the elapsed counter shots 6 and 8 walk their curve on. It
      // is a *halfword* in the original and the add is `+= (short)DAT_003555BC`,
      // so it wraps rather than saturating; both durations are far below the
      // wrap, so nothing depends on that.
      std::int16_t DAT_0035532e_pathElapsed = 0;
    };
    DirectorState &director()
    {
      static DirectorState value{};
      return value;
    }

    // DAT_0058B190/94/98. The **anchor**, not the published eye: shots 4, 5, 6,
    // 7, 8, 9, 10 and 11 stamp a point here on the frame their sub-shot changes
    // and build every later frame's pose off it. Shots 1, 2, 3 and 12 never
    // touch it.
    Vec3 &DAT_0058b190_anchor()
    {
      static Vec3 value{};
      return value;
    }

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    // FUN_0023A4B8(a, b): the bearing from a to b.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_0023A7D8(a, b): the *pitch* from a to b, and note the argument order
    // -- `atan2(horizontal, verticalDelta)`, not the other way round, so it is
    // measured from straight up rather than from the horizon.
    float FUN_0023a7d8_pitch(const OriginalEntity &from, const OriginalEntity &to)
    {
      const float dx = to.positionX20 - from.positionX20;
      const float dz = to.positionZ24 - from.positionZ24;
      return std::atan2(std::sqrt(dx * dx + dz * dz), to.positionY28 - from.positionY28);
    }

    // FUN_0029CC28(mode, point). It memsets its own three-float scratch to zero
    // and never fills it, so both arms measure against the **world origin**:
    // mode 0 is the bearing from the origin to the point and anything else is
    // the bearing back. Only mode 0 is reachable from this director.
    //
    // The FUN_00216690 wrap on the way out is a no-op over atan2's own range.
    float FUN_0029cc28_bearing_from_origin(const OriginalEntity &entity)
    {
      return std::atan2(entity.positionZ24, entity.positionX20);
    }

    // FUN_0020B810(point, matrix, out): the 3x4 transform, the same one
    // FUN_00218EB0 does a vertex at a time.
    Vec3 FUN_0020b810_transform(const Vec3 &point, const render::Matrix4 &matrix)
    {
      return {point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) + point.z * matrix.at(2, 0) +
                  matrix.at(3, 0),
              point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) + point.z * matrix.at(2, 1) +
                  matrix.at(3, 1),
              point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) + point.z * matrix.at(2, 2) +
                  matrix.at(3, 2)};
    }

    // `rotation` below zero means the shot writes DAT_00355661 and leaves
    // DAT_00343880 alone, which is what shots 7, 8 and 10 do.
    void set_smear(const ActorEnvironment &environment, int rotation, std::uint8_t alpha)
    {
      if (environment.DAT_00343878_frameFeedback == nullptr)
      {
        return;
      }
      if (rotation >= 0)
      {
        environment.DAT_00343878_frameFeedback->set_DAT_00343880_rotation(
            static_cast<std::int16_t>(rotation));
      }
      environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(alpha);
    }
  } // namespace

  std::uint8_t &DAT_0035532c_mastCameraSide()
  {
    static std::uint8_t value = 1;
    return value;
  }

  void FUN_00298160_mast_camera(std::int16_t shot,
                                std::int16_t subShot,
                                std::int16_t priority,
                                const OriginalEntity *target,
                                const ActorEnvironment &environment)
  {
    if (environment.camera == nullptr || environment.entityPool == nullptr || target == nullptr)
    {
      return;
    }
    orphen::ported::camera::OriginalFieldCamera &camera = *environment.camera;
    DirectorState &self = director();

    // :12-18. The release. Unlike the crab's director this one does not touch
    // the zoom on the way out; it only clears the latch and the three
    // selectors, so the manual camera stays installed at the pose the last shot
    // left it in and the next call re-installs from scratch.
    if (shot < 0)
    {
      self.DAT_00355310_installed = 0;
      self.DAT_00355314_shot = -1;
      self.DAT_00355318_priority = -1;
      self.DAT_00355320_builtFor = -1;
      return;
    }

    // :19-26. First call after a release installs a fresh manual camera at the
    // origin, which the shot below then moves the same frame.
    if (self.DAT_00355310_installed == 0)
    {
      camera.FUN_00217e18_release_manual_camera(true);
      camera.FUN_00217d70_set_manual_camera(Vec3{}, Vec3{});
      self.DAT_00355310_installed = 1;
      self.DAT_0035531c_subShot = 0;
    }
    else if (priority < self.DAT_00355318_priority)
    {
      // A lower-priority shot cannot take the camera off the one running.
      return;
    }

    if (subShot > 0)
    {
      self.DAT_0035531c_subShot = subShot;
    }
    self.DAT_00355314_shot = shot;
    self.DAT_00355318_priority = priority;

    // :33-34, both unconditional and ahead of the switch: every shot this
    // director runs is at the nominal zoom with the projection's own scale.
    // The line after it, `DAT_0035565C = 2.0`, has no consumer anywhere in
    // src/ and no field in the port, so it is recorded here and not stored.
    camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(1.0f));

    // Shots 1, 2, 3 and 5 write the **player's** facing, not just the camera's
    // -- DAT_0058BF0C is pool slot 0's +0x5C. The director turning the player
    // to look at the boss is the original's, not a side effect of the port.
    OriginalEntity &player = environment.entityPool->slot(0);
    const std::int32_t sub = self.DAT_0035531c_subShot;
    const auto ticks = static_cast<float>(static_cast<std::int32_t>(environment.frameTicks));
    const float groundZ = environment.DAT_003556fc_effectGroundZ;

    Vec3 &anchor = DAT_0058b190_anchor();
    // The original builds both of these on the stack and leaves them
    // uninitialised for a sub-shot a shot has no arm for -- shot 7 with sub 2,
    // for instance, publishes whatever was last left below the stack pointer.
    // Starting from the live pose holds instead, which is the port's choice and
    // the safer of the two.
    Vec3 eye = camera.DAT_0058c0a8_eye();
    Vec3 look = camera.DAT_0058be90_lookAt();
    // Every shot's swing starts at the function's own `fVar6 = 0.0`, which is
    // what a sub-shot with no arm of its own ends up using.
    float swung = 0.0f;

    switch (shot)
    {
    case 1:
    {
      // Over the player's shoulder toward the boss, three units out, looking at
      // the boss over the **water line** rather than at the boss's own height.
      player.facingRadians5c = FUN_0023a4b8_bearing(player, *target);
      if (sub == 1)
      {
        swung = (player.facingRadians5c - kDAT_003537dc_pi) - kDAT_003537e0_swing1;
        camera.setRoll(kDAT_003537e4_roll1);
      }
      else if (sub == 2)
      {
        swung = (player.facingRadians5c - kDAT_003537e8_pi) + kDAT_003537ec_swing2;
        camera.setRoll(kDAT_003537f0_roll2);
      }
      eye.x = player.positionX20 + cos_of(swung) * 3.0f;
      eye.y = player.positionZ24 + sin_of(swung) * 3.0f;
      eye.z = player.positionY28 + 3.0f;
      look.x = target->positionX20;
      look.y = target->positionZ24;
      look.z = groundZ;
      break;
    }

    case 2:
    {
      // The same at five units, with the boss's elevation folded into the eye's
      // height and the look-at pulled two units back along the boss's facing.
      player.facingRadians5c = FUN_0023a4b8_bearing(player, *target);
      const float pitch = FUN_0023a7d8_pitch(player, *target) - kDAT_003537f4_pitchBias;
      if (sub == 1)
      {
        swung = (player.facingRadians5c - kDAT_003537f8_pi) - kDAT_003537fc_swing1;
        camera.setRoll(kDAT_00353800_roll1);
      }
      else if (sub == 2)
      {
        swung = (player.facingRadians5c - kDAT_003537f8_pi) + kDAT_00353804_swing2;
        camera.setRoll(kDAT_00353808_roll2);
      }
      eye.x = player.positionX20 + cos_of(swung) * 5.0f;
      eye.y = player.positionZ24 + sin_of(swung) * 5.0f;
      eye.z = player.positionY28 + sin_of(pitch) * 3.0f + 1.0f;
      look.x = target->positionX20 - (cos_of(target->facingRadians5c) * 2.0f);
      look.y = target->positionZ24 - (sin_of(target->facingRadians5c) * 2.0f);
      look.z = target->positionY28 + 1.5f;
      break;
    }

    case 3:
    {
      // :0x002983B4. Shot 2's geometry pulled in to four units. The pitch goes
      // straight on the eye's height instead of being added to a fixed one, and
      // the look-at is the boss itself rather than a point two units behind it;
      // that is what makes it the tighter of the two orbit passes.
      player.facingRadians5c = FUN_0023a4b8_bearing(player, *target);
      const float pitch = FUN_0023a7d8_pitch(player, *target) - kDAT_0035380c_pitchBias;
      if (sub == 1)
      {
        swung = (player.facingRadians5c - kDAT_00353810_pi) - kDAT_00353814_swing1;
        camera.setRoll(kDAT_00353818_roll1);
      }
      else if (sub == 2)
      {
        swung = (player.facingRadians5c - kDAT_00353810_pi) + kDAT_0035381c_swing2;
        camera.setRoll(kDAT_00353820_roll2);
      }
      eye.x = player.positionX20 + cos_of(swung) * 4.0f;
      eye.y = player.positionZ24 + sin_of(swung) * 4.0f;
      eye.z = player.positionY28 + sin_of(pitch) * 4.0f;
      look.x = target->positionX20;
      look.y = target->positionZ24;
      look.z = target->positionY28;
      break;
    }

    case 4:
    {
      // :0x00298468. A randomised orbit round wherever the boss was when the
      // sub-shot changed. The angle is the bearing **from the world origin** to
      // the boss plus fifteen to forty-four degrees of roll, so two runs of the
      // same move are never framed identically.
      set_smear(environment, kSmearRotation, kSmearAlpha);
      if (sub != self.DAT_00355320_builtFor)
      {
        const float base = FUN_0029cc28_bearing_from_origin(*target);
        const std::uint32_t rolled = environment.random ? environment.random() : 0u;
        anchor = Vec3{target->positionX20, target->positionZ24, target->positionY28};
        self.DAT_00355320_builtFor = sub;
        self.DAT_00355324_sweepAngle =
            base + (static_cast<float>(rolled % 0x1E + 0x0F) * kDAT_00353824_turn) / 360.0f;
      }
      swung = self.DAT_00355324_sweepAngle;
      if (sub == 1)
      {
        // Ten units out and fifteen above the water, looking at the boss where
        // it is *now* -- the only one of the three that tracks.
        eye.x = target->positionX20 + cos_of(swung) * 10.0f;
        look.y = target->positionZ24;
        eye.y = look.y + sin_of(swung) * 10.0f;
        eye.z = groundZ + 15.0f;
        look.x = target->positionX20;
        look.z = target->positionY28 >= 0.0f ? target->positionY28 : groundZ;
        look.z += 1.0f;
      }
      if (sub == 2)
      {
        // Fifteen out and five up, off the **anchor** rather than the boss, so
        // the creature flies through a fixed frame.
        eye.x = anchor.x + cos_of(swung) * 15.0f;
        eye.y = anchor.y + sin_of(swung) * 15.0f;
        eye.z = groundZ + 5.0f;
        look.x = target->positionX20;
        look.y = target->positionZ24;
        look.z = groundZ + 2.0f;
      }
      if (sub == 3)
      {
        // The same ring closer to the water and aimed back at the anchor: the
        // boss leaves frame instead of being followed.
        eye.x = anchor.x + cos_of(swung) * 15.0f;
        eye.y = anchor.y + sin_of(swung) * 15.0f;
        eye.z = groundZ + 2.0f;
        look.x = anchor.x;
        look.y = anchor.y;
        look.z = target->positionY28 >= 0.0f ? target->positionY28 : groundZ;
        look.z += 4.0f;
      }
      break;
    }

    case 5:
    {
      // A sweep round the player that ping-pongs between two limits, three units
      // out. **The limit tests read DAT_00355324, which this shot never
      // writes** -- it is the running angle shots 4, 7 and 10 keep, so unless
      // one of those has run it is zero and the sweep flips as soon as the
      // limit goes non-positive. Reproduced, not corrected.
      if (sub != self.DAT_00355320_builtFor)
      {
        self.DAT_00355330_sweepStart = FUN_0023a4b8_bearing(player, *target);
        self.DAT_00355328_sweep = self.DAT_00355330_sweepStart - kDAT_00353828_base;
        if (sub == 1)
        {
          self.DAT_00355328_sweep -= kDAT_0035382c_span1;
        }
        else if (sub == 2)
        {
          self.DAT_00355328_sweep += kDAT_00353830_span2;
        }
        self.DAT_00355320_builtFor = sub;
        anchor = Vec3{target->positionX20, target->positionZ24, target->positionY28};
      }
      if (sub == 1)
      {
        self.DAT_00355328_sweep += ticks * kDAT_00353834_rateOut * 0.03125f;
        if ((self.DAT_00355330_sweepStart - kDAT_00353838_limitBase1) + kDAT_0035383c_limitSpan1 <=
            self.DAT_00355324_sweepAngle)
        {
          self.DAT_0035531c_subShot = 2;
        }
      }
      else if (sub == 2)
      {
        self.DAT_00355328_sweep -= ticks * kDAT_00353840_rateBack * 0.03125f;
        if (self.DAT_00355324_sweepAngle <=
            (self.DAT_00355330_sweepStart - kDAT_00353844_limitBase2) - kDAT_00353848_limitSpan2)
        {
          self.DAT_0035531c_subShot = 1;
        }
      }
      swung = self.DAT_00355328_sweep;
      eye.x = player.positionX20 + cos_of(swung) * 3.0f;
      eye.y = player.positionZ24 + sin_of(swung) * 3.0f;
      eye.z = player.positionY28 + 3.0f;
      look.x = player.positionX20;
      look.y = player.positionZ24;
      look.z = player.positionY28;
      break;
    }

    case 6:
    {
      // :0x002986F4, the transformation shot, and the only one that is a
      // *camera path* rather than a pose. Four control points authored in the
      // boss's bone 8 space are installed as a spline, walked over 0xC80 ticks,
      // then turned by the boss's facing and dropped on the point bone 8 was at
      // when the shot started. The look-at is bone 8 live, every frame, so the
      // curve orbits a head that is still moving.
      //
      // It has its own tail -- it never reaches the shared publish -- because
      // the eye it publishes is the transformed point, not the one
      // FUN_00217F38 has just written.
      //
      // The slot is the environment's current one. Every call the boss makes
      // passes its own entity as the target, and the boss is the entity the
      // actor loop is running, so the two are the same by construction.
      const std::size_t targetSlot = environment.currentSlot;
      if (sub != self.DAT_00355320_builtFor)
      {
        if (environment.FUN_0020dc88_bone_point)
        {
          anchor = environment.FUN_0020dc88_bone_point(targetSlot, 8, Vec3{});
        }
        camera.FUN_00217e18_release_manual_camera(true);
        camera.FUN_00217fe8_set_camera_path(
            std::span<const Vec3>{kDAT_00325da8_shot6Eye}, std::span<const float>{},
            std::span<const float>{}, std::span<const Vec3>{&kDAT_00325d98_shot6Look, 1});
        self.DAT_0035532e_pathElapsed = 0;
        self.DAT_00355320_builtFor = sub;
      }

      if (environment.FUN_0020dc88_bone_point)
      {
        look = environment.FUN_0020dc88_bone_point(targetSlot, 8, Vec3{});
      }

      // FUN_0020BAE0 writes four of the sixteen floats and leaves the rest of
      // DAT_00342828 alone; every other caller in the engine hands it a matrix
      // FUN_0020BC38 has just made identity, so identity is what the standing
      // contents are. Built fresh here rather than relying on that.
      render::Matrix4 turn = render::FUN_0020bc38_identity();
      render::FUN_0020bae0_setRotationZ(turn, target->facingRadians5c - kDAT_0035384c_curveBias);

      Vec3 local = kDAT_00325da8_shot6Eye.back();
      if (self.DAT_0035532e_pathElapsed < kShot6Duration + 1)
      {
        camera.FUN_00217f38_step_camera_path(self.DAT_0035532e_pathElapsed, kShot6Duration);
        local = camera.DAT_0058c0a8_eye();
        self.DAT_0035532e_pathElapsed = static_cast<std::int16_t>(
            self.DAT_0035532e_pathElapsed + static_cast<std::int32_t>(environment.frameTicks));
      }

      const Vec3 turned = FUN_0020b810_transform(local, turn);
      eye = Vec3{anchor.x + turned.x, anchor.y + turned.y, anchor.z + turned.z};
      camera.FUN_00217d10_set_look_at(look);
      camera.FUN_00217d40_set_eye(eye);
      return;
    }

    case 7:
    {
      // :0x00298890. Shot 4's randomised orbit aimed at the **player** instead:
      // ten units out and twenty-five above the water line, which is the high
      // wide shot the strafing run is watched from. Only sub-shot 1 has a pose;
      // 2 and 3 fall through holding whatever the last frame published, and the
      // boss only ever asks for 1.
      set_smear(environment, -1, kSmearAlpha);
      if (sub != self.DAT_00355320_builtFor)
      {
        const float base = FUN_0029cc28_bearing_from_origin(*target);
        const std::uint32_t rolled = environment.random ? environment.random() : 0u;
        anchor = Vec3{target->positionX20, target->positionZ24, target->positionY28};
        self.DAT_00355320_builtFor = sub;
        self.DAT_00355324_sweepAngle =
            base + (static_cast<float>(rolled % 0x1E + 0x0F) * kDAT_00353850_turn) / 360.0f;
      }
      if (sub == 1)
      {
        swung = self.DAT_00355324_sweepAngle;
        eye.x = player.positionX20 + cos_of(swung) * 10.0f;
        eye.y = player.positionZ24 + sin_of(swung) * 10.0f;
        eye.z = groundZ + 25.0f;
        look.x = player.positionX20;
        look.y = player.positionZ24;
        look.z = player.positionY28 + 1.0f;
      }
      break;
    }

    case 8:
    {
      // :0x00298940, the beam shot. A three-point spline walked over 0x1900
      // ticks, and unlike shot 6 the points are a plain **offset** from the
      // anchor -- no rotation -- so the move is the same shape every time. The
      // anchor comes out of DAT_00325DD8 indexed by the boss's +0x1BF, which is
      // how many of states 8 and 9 have run, so each swoop is framed from its
      // own side of the deck.
      set_smear(environment, -1, kSmearAlpha);
      if (sub != self.DAT_00355320_builtFor)
      {
        const std::size_t index =
            static_cast<std::size_t>(target->mastSwoopStage1bf) < kDAT_00325dd8_shot8Anchor.size()
                ? static_cast<std::size_t>(target->mastSwoopStage1bf)
                : kDAT_00325dd8_shot8Anchor.size() - 1;
        const Vec3 &row = kDAT_00325dd8_shot8Anchor[index];
        anchor = Vec3{row.x, row.y, row.z + groundZ};
        camera.FUN_00217e18_release_manual_camera(true);
        camera.FUN_00217fe8_set_camera_path(std::span<const Vec3>{kDAT_00325e00_shot8Eye},
                                            std::span<const float>{}, std::span<const float>{},
                                            std::span<const Vec3>{&row, 1});
        self.DAT_0035532e_pathElapsed = 0;
        self.DAT_00355320_builtFor = sub;
      }

      look.x = anchor.x;
      look.y = anchor.y;
      // **The water line goes on twice.** The anchor's height already has
      // DAT_003556FC in it from the one-shot above, and this adds it again.
      // That is what the original does; it is why the shot sits over the
      // creature rather than level with it.
      look.z = anchor.z + groundZ;

      Vec3 offset = kDAT_00325e00_shot8Eye.back();
      if (self.DAT_0035532e_pathElapsed < kShot8Duration + 1)
      {
        camera.FUN_00217f38_step_camera_path(self.DAT_0035532e_pathElapsed, kShot8Duration);
        offset = camera.DAT_0058c0a8_eye();
        self.DAT_0035532e_pathElapsed = static_cast<std::int16_t>(
            self.DAT_0035532e_pathElapsed + static_cast<std::int32_t>(environment.frameTicks));
      }
      eye = Vec3{anchor.x + offset.x, anchor.y + offset.y, anchor.z + offset.z};
      break;
    }

    case 9:
    {
      // :0x0029900C. A close shot one and a half units off the player, swung
      // forty degrees from where they are facing, looking at their chest.
      //
      // Note whose facing it is: DAT_0058BF0C is *pool slot 0's* +0x5C, not the
      // boss's -- and during the intro FUN_0029C198 writes that facing from the
      // spline tangent every frame, so the camera tracks the leap rather than
      // the creature.
      if (sub == 1)
      {
        swung = player.facingRadians5c + kDAT_00353854_swing1;
        anchor.x = player.positionX20 + cos_of(swung) * 1.5f;
        anchor.y = player.positionZ24 + sin_of(swung) * 1.5f;
        anchor.z = player.positionY28 + 1.5f;
      }
      if (sub == 2)
      {
        swung = player.facingRadians5c - kDAT_00353858_swing2;
        anchor.x = player.positionX20 + cos_of(swung) * 1.5f;
        anchor.y = player.positionZ24 + sin_of(swung) * 1.5f;
        anchor.z = player.positionY28 + 1.5f;
      }
      if (sub == 3)
      {
        // The third is the only one that subtracts: the camera sits on the far
        // side of the player and three units up rather than one and a half.
        swung = player.facingRadians5c + kDAT_0035385c_swing3;
        anchor.x = player.positionX20 - cos_of(swung) * 1.5f;
        anchor.y = player.positionZ24 - sin_of(swung) * 1.5f;
        anchor.z = player.positionY28 + 3.0f;
      }
      eye = anchor;
      look.x = player.positionX20;
      look.y = player.positionZ24;
      look.z = player.positionY28 + 1.0f;
      break;
    }

    case 10:
    {
      // :0x00298C48, the mast smash. Three fixed orbits round where the boss
      // was, each with its own roll, and a fourth that is not an orbit at all:
      // it drifts two units round the **player** at a fixed rate up to a limit,
      // which is the slow push-in the move ends on. The fourth is also the only
      // sub-shot that turns the screen smear back off.
      set_smear(environment, -1, kSmearAlpha);
      if (sub != self.DAT_00355320_builtFor)
      {
        self.DAT_00355324_sweepAngle = FUN_0029cc28_bearing_from_origin(*target);
        anchor = Vec3{target->positionX20, target->positionZ24, groundZ};
        if (sub == 4)
        {
          // Off the *player's* facing, not a bearing, and the start is kept so
          // the sweep below knows where it is allowed to stop.
          self.DAT_00355324_sweepAngle = player.facingRadians5c + kDAT_00353860_leadIn;
          self.DAT_00355330_sweepStart = self.DAT_00355324_sweepAngle;
        }
        self.DAT_00355320_builtFor = sub;
      }
      if (sub == 1)
      {
        swung = self.DAT_00355324_sweepAngle + kDAT_00353864_swing1;
        eye.x = anchor.x + cos_of(swung) * 7.0f;
        eye.y = anchor.y + sin_of(swung) * 7.0f;
        eye.z = anchor.z + 9.0f;
        camera.setRoll(kDAT_00353868_roll1);
      }
      if (sub == 2)
      {
        swung = self.DAT_00355324_sweepAngle - kDAT_0035386c_swing2;
        eye.x = anchor.x + cos_of(swung) * 7.0f;
        eye.y = anchor.y + sin_of(swung) * 7.0f;
        eye.z = anchor.z + 9.0f;
        camera.setRoll(kDAT_00353870_roll2);
      }
      if (sub == 3)
      {
        swung = self.DAT_00355324_sweepAngle + kDAT_00353874_swing3;
        eye.x = anchor.x + cos_of(swung) * 15.0f;
        eye.y = anchor.y + sin_of(swung) * 15.0f;
        eye.z = anchor.z + 7.0f;
        camera.setRoll(kDAT_00353878_roll3);
      }
      look.x = anchor.x;
      look.y = anchor.y;
      look.z = anchor.z + 1.0f;
      if (sub == 4)
      {
        set_smear(environment, -1, 0);
        const float limit = self.DAT_00355330_sweepStart + kDAT_00353880_sweepSpan;
        self.DAT_00355324_sweepAngle += ticks * kDAT_0035387c_sweepRate * 0.03125f;
        swung = self.DAT_00355324_sweepAngle < limit ? self.DAT_00355324_sweepAngle : limit;
        eye.x = player.positionX20 + cos_of(swung) * 2.0f;
        look.y = player.positionZ24;
        eye.z = player.positionY28 + 1.0f;
        eye.y = player.positionZ24 + sin_of(swung) * 2.0f;
        look.x = player.positionX20;
        // Eye height and look height are the same value -- dead level.
        look.z = eye.z;
      }
      break;
    }

    case 11:
    {
      // A one-shot pose five units either side of the boss, built once per
      // sub-shot change and then held while the boss flies through it.
      if (sub != self.DAT_00355320_builtFor)
      {
        if (sub == 1)
        {
          const float a = target->facingRadians5c + kDAT_00353884_swing1;
          anchor.x = target->positionX20 - cos_of(a) * 5.0f;
          anchor.y = target->positionZ24 - sin_of(a) * 5.0f;
          anchor.z = target->positionY28 + 3.0f;
        }
        if (sub == 2)
        {
          const float a = target->facingRadians5c - kDAT_00353888_swing2;
          anchor.x = target->positionX20 + cos_of(a) * 5.0f;
          anchor.y = target->positionZ24 + sin_of(a) * 5.0f;
          anchor.z = target->positionY28 - 3.0f;
        }
        self.DAT_00355320_builtFor = sub;
      }
      look.x = target->positionX20;
      look.y = target->positionZ24;
      look.z = target->positionY28;
      eye = anchor;
      break;
    }

    case 12:
    {
      // :0x00298E9C, the death. The eye never moves -- it is a point on the
      // deck the scene was authored around -- and the only thing that tracks is
      // what it is aimed at, clamped so the shot never dips below the water as
      // the creature sinks. Sub-shot 1 is the only one the fight asks for.
      //
      // Its own tail again, though it is the same pair in the same order as the
      // shared one.
      set_smear(environment, kSmearRotation, kSmearAlpha);
      if (sub == 1)
      {
        look.z = target->positionY28 + 5.0f;
        look.y = target->positionZ24;
        look.x = target->positionX20;
        eye = kDAT_0035388c_deathEye;
        if (look.z <= groundZ)
        {
          look.z = groundZ;
        }
      }
      camera.FUN_00217d10_set_look_at(look);
      camera.FUN_00217d40_set_eye(eye);
      return;
    }

    default:
      // The original writes DAT_0035565C = 2.0 and leaves. Nothing reads it.
      return;
    }

    // :LAB_002992A8, the shared tail. Look-at first, then the eye -- the order
    // matters because FUN_00217D10 and FUN_00217D40 both refuse unless the
    // script camera is installed, and the install above put one there.
    camera.FUN_00217d10_set_look_at(look);
    camera.FUN_00217d40_set_eye(eye);
  }

} // namespace orphen::ported::entity
