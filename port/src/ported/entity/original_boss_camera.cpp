#include "ported/entity/original_boss_camera.h"

#include "ported/camera/original_field_camera.h"
#include "ported/render/original_frame_feedback.h"

#include <array>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    // ------------------------------------------------------------- constants
    //
    // Every one of these is a gp-relative word in .sdata, read out of
    // SLUS_200.11 at gp - (0x10000 - offset) with gp = 0x00359F70. The names
    // keep Ghidra's spelling so the transcription stays checkable line by line.

    // Shot 1: half a turn off the bearing, then forty degrees to one side.
    inline constexpr float kFGpffff9078_pi = 3.14159274101257f;
    inline constexpr float kFGpffff907c_swing = 0.698131680488586f;
    inline constexpr float kFGpffff9080_pi = 3.14159274101257f;
    inline constexpr float kFGpffff9084_swing = 0.698131680488586f;
    // Shot 2: the same, at forty-five degrees and two and a half units out.
    inline constexpr float kFGpffff9088_pi = 3.14159274101257f;
    inline constexpr float kFGpffff908c_swing = 0.785398185253143f;
    inline constexpr float kFGpffff9090_pi = 3.14159274101257f;
    inline constexpr float kFGpffff9094_swing = 0.785398185253143f;
    // Shot 3, the orbit: a start angle per sub-shot, a rate, and the limit that
    // flips DAT_0035528C for whatever runs next.
    inline constexpr float kFGpffff9098_startDefault = -3.14159274101257f;
    inline constexpr float kFGpffff909c_start2 = -2.70526027679443f;
    inline constexpr float kFGpffff90a0_start3 = -3.66519165039063f;
    inline constexpr float kFGpffff90a4_start4 = -2.61799383163452f;
    inline constexpr float kFGpffff90a8_start1and5 = -3.57792472839355f;
    inline constexpr float kFGpffff90ac_rate1 = 0.000872664619237185f;
    inline constexpr float kFGpffff90b0_limitBase1 = 3.14159274101257f;
    inline constexpr float kFGpffff90b4_limitSpan1 = 0.436332315206528f;
    inline constexpr float kFGpffff90b8_rate2 = 0.000872664619237185f;
    inline constexpr float kFGpffff90bc_limitBase2 = 3.14159274101257f;
    inline constexpr float kFGpffff90c0_limitSpan2 = 0.436332315206528f;
    inline constexpr float kFGpffff90c4_rate3 = 0.000872664619237185f;
    inline constexpr float kFGpffff90c8_limitBase3 = 3.14159274101257f;
    inline constexpr float kFGpffff90cc_limitSpan3 = 0.523598790168762f;
    inline constexpr float kFGpffff90d0_rate4 = 0.000872664619237185f;
    inline constexpr float kFGpffff90d4_limitBase4 = 3.14159274101257f;
    inline constexpr float kFGpffff90d8_limitSpan4 = 0.523598790168762f;
    inline constexpr float kFGpffff90dc_orbitHeight = 1.70000004768372f;
    // Shot 6, the dodge chase.
    inline constexpr float kFGpffff90e0_pi = 3.14159274101257f;
    inline constexpr float kFGpffff90e4_swing = 0.523598790168762f;
    inline constexpr float kFGpffff90e8_pi = 3.14159274101257f;
    inline constexpr float kFGpffff90ec_swing = 0.610865175724030f;
    inline constexpr Vec3 kFGpffff90f0_look3{0.0410000011324883f, -1.54699993133545f,
                                             -0.119999997317791f};
    inline constexpr Vec3 kFGpffff90fc_eye3{-0.967999994754791f, 1.96499991416931f,
                                            -0.340000003576279f};
    // Shot 7, the approach and the carry.
    inline constexpr Vec3 kFGpffff9108_eye1{0.075000002980232f, -3.79399991035461f,
                                            1.47399997711182f};
    inline constexpr Vec3 kFGpffff9114_eye2{-0.0309999994933605f, 1.00300002098083f, -0.375f};
    inline constexpr Vec3 kFGpffff911c_eye3{-0.940999984741211f, -4.89400005340576f,
                                            -0.175000004470348f};
    inline constexpr float kFGpffff9128_carrySwing = 0.785398185253143f;
    inline constexpr Vec3 kFGpffff912c_eye5{2.19599986076355f, -2.57200002670288f, 0.0f};
    inline constexpr Vec3 kFGpffff9134_look1{-1.00300002098083f, -1.23399996757507f,
                                             -0.456999987363815f};
    inline constexpr float kFGpffff9140_look2x = -1.27899992465973f;
    inline constexpr float kFGpffff9144_look2y = -3.13699984550476f;
    inline constexpr Vec3 kFGpffff9148_look3{1.00300002098083f, -6.96399974822998f,
                                             0.662000000476837f};
    inline constexpr float kFGpffff9154_look5x = -0.0619999989867210f;
    inline constexpr float kFGpffff9158_look5y = 1.19200003147125f;
    // Shot 11, the late-fight moves.
    inline constexpr float kFGpffff915c_eye2x = -1.89999997615814f;
    inline constexpr float kFGpffff9160_eye2y = 5.40000009536743f;
    inline constexpr float kFGpffff9164_swing3 = 0.523598790168762f;
    inline constexpr float kFGpffff9168_roll3 = 0.174532890319824f;
    inline constexpr float kFGpffff916c_lift4 = 0.300000011920929f;
    inline constexpr float kFGpffff9170_lift7 = 0.800000011920929f;
    inline constexpr float kFGpffff9174_swing9 = 0.698131680488586f;
    // Shots 12 and 13, off the close-up mount's role-9 bone.
    inline constexpr float kFGpffff9178_boneLift12 = 0.150000005960464f;
    inline constexpr float kFGpffff917c_swing12 = 0.174532890319824f;
    inline constexpr Vec3 kFGpffff9180_look12b{1.54699993133545f, -6.94299983978271f,
                                               0.243000000715256f};
    inline constexpr float kFGpffff918c_eye12by = -3.95199990272522f;
    inline constexpr float kFGpffff9190_eye12bz = -0.166999995708466f;
    inline constexpr float kFGpffff9194_boneLift13 = 0.150000005960464f;
    inline constexpr float kFGpffff9198_swing13 = 0.174532890319824f;
    // Shot 14's second look-at point.
    inline constexpr Vec3 kFGpffff919c_lookEnd{0.0799999982118607f, 4.83800020217896f,
                                               -0.200000002980232f};

    // The three spline shots. DAT_0034E4C0 onward is a plain run of Vec3s and
    // (roll, zoom) pairs; the original copies the run onto its stack and hands
    // the copies to FUN_00217fe8, which reads points at a stride of twelve --
    // which is what makes the copies come out fully initialised, and is how the
    // stride was pinned down.
    //
    //   shot  9  0x0034E4C0  1 look-at, 2 eye, 2 roll/zoom, over 0x1680 ticks
    //   shot 10  0x0034E4F8  2 look-at, 3 eye, 3 roll/zoom, over 0x12C0 ticks
    //   shot 14  0x0034E550  2 look-at (the first taken live), 3 eye, 3 pairs
    inline constexpr std::array<Vec3, 1> kDAT_0034e4c0_look9{
        {{1.80999994277954f, -7.42600011825562f, 0.769999980926514f}}};
    inline constexpr std::array<Vec3, 2> kDAT_0034e4d0_eye9{
        {{0.111000001430511f, -4.32999992370605f, -0.340000003576279f},
         {-0.949999988079071f, -5.58500003814697f, 1.07500004768372f}}};
    inline constexpr std::array<float, 2> kDAT_0034e4e8_roll9{{0.0f, 0.0f}};
    inline constexpr std::array<float, 2> kDAT_0034e4e8_zoom9{{1.0f, 2.0f}};

    inline constexpr std::array<Vec3, 2> kDAT_0034e4f8_look10{
        {{1.80999994277954f, -7.42600011825562f, 0.769999980926514f},
         {1.54699993133545f, -6.94299983978271f, 0.243000000715256f}}};
    inline constexpr std::array<Vec3, 3> kDAT_0034e510_eye10{
        {{-0.949000000953674f, -5.58500003814697f, 1.07500004768372f},
         {0.0500000007450581f, -4.32800006866455f, -0.0250000003725290f},
         {1.07000005245209f, -3.84200000762939f, -0.166999995708466f}}};
    inline constexpr std::array<float, 3> kDAT_0034e538_roll10{{0.0f, 0.0f, 0.0f}};
    inline constexpr std::array<float, 3> kDAT_0034e538_zoom10{{2.0f, 6.0f, 8.0f}};

    inline constexpr std::array<Vec3, 3> kDAT_0034e550_eye14{
        {{-2.0f, 2.0f, 1.5f},
         {-3.84899997711182f, 2.78000009059906f, 0.0f},
         {-4.92300033569336f, 4.01000022888184f, -0.379999995231628f}}};
    inline constexpr std::array<float, 3> kDAT_0034e578_roll14{{0.0f, 0.0f, 0.0f}};
    inline constexpr std::array<float, 3> kDAT_0034e578_zoom14{{2.0f, 4.0f, 5.0f}};

    inline constexpr int kShot9Duration = 0x1680;
    inline constexpr int kShot10Duration = 0x12C0;
    inline constexpr int kShot13Duration = 0x12C0;
    inline constexpr int kShot14Duration = 0x12C0;

    // ----------------------------------------------------------- the globals
    //
    // 0x00355274..0x0035529B. `DAT_0035528C` sits inside the block at the
    // uGpffffb31c slot, and is the one field anything outside this file reads.
    struct DirectorState
    {
      std::int32_t iGpffffb304_installed = 0;
      std::int32_t iGpffffb308_shot = 0;
      std::int32_t iGpffffb30c_priority = 0;
      std::int32_t iGpffffb310_subShot = 0;
      std::int32_t iGpffffb314_builtFor = 0;
      float uGpffffb318_savedZoom = 0.0f;
      float fGpffffb320_orbitAngle = 0.0f;
      std::int16_t sGpffffb324_shot9Elapsed = 0;
      std::int16_t sGpffffb326_shot10Elapsed = 0;
      std::int16_t sGpffffb328_shot13Elapsed = 0;
      std::int16_t sGpffffb32a_shot14Elapsed = 0;
    };
    DirectorState &director()
    {
      static DirectorState value{};
      return value;
    }

    // DAT_005739A0: the eye shots 7, 11 and 13 park once and then hold, so a
    // sub-shot that only moves the look-at leaves the camera where it was.
    Vec3 &DAT_005739a0_heldEye()
    {
      static Vec3 value{};
      return value;
    }

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    // FUN_0023a4b8(a, b): the bearing from a to b.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    Vec3 position_of(const OriginalEntity &entity)
    {
      return Vec3{entity.positionX20, entity.positionZ24, entity.positionY28};
    }

    // DAT_00343880 on its own. The original writes the halfword and nothing
    // else in the 0xC9 block.
    void set_smear_rotation(const ActorEnvironment &environment, std::int16_t rotation)
    {
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->set_DAT_00343880_rotation(rotation);
      }
    }

    // uGpffffb6f1 is DAT_00355661, the smear's target alpha.
    void set_smear_alpha(const ActorEnvironment &environment, std::uint8_t alpha)
    {
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(alpha);
      }
    }
  } // namespace

  std::array<std::int32_t, 3> &DAT_00325900_cinematicSlots()
  {
    static std::array<std::int32_t, 3> value{{-1, -1, -1}};
    return value;
  }

  std::uint8_t &DAT_0035528c_cameraSide()
  {
    static std::uint8_t value = 1;
    return value;
  }

  void FUN_00277d30_boss_camera(std::int16_t mode,
                                std::int16_t subMode,
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

    // :19-27. A negative shot releases the director. Note what it does *not*
    // do: FUN_00217e18 is never called here, so the manual camera stays
    // installed at the pose the last shot left it in. Only the latch is
    // cleared, so the next call re-installs from scratch.
    if (mode < 0)
    {
      self.iGpffffb30c_priority = -1;
      self.iGpffffb304_installed = 0;
      self.iGpffffb314_builtFor = -1;
      self.iGpffffb308_shot = -1;
      camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(1.0f));
      // uGpffffb6ec = 0 -- unmodelled, see the header.
      return;
    }

    if (self.iGpffffb304_installed == 0)
    {
      camera.FUN_00217e18_release_manual_camera(true);
      camera.FUN_00217d70_set_manual_camera(Vec3{}, Vec3{});
      self.iGpffffb308_shot = mode;
      self.iGpffffb304_installed = 1;
      self.iGpffffb30c_priority = priority;
      self.iGpffffb310_subShot = 0;
    }
    else if (priority < self.iGpffffb30c_priority)
    {
      // A lower-priority shot cannot take the camera off the one running.
      return;
    }
    else
    {
      self.iGpffffb308_shot = mode;
      self.iGpffffb30c_priority = priority;
    }
    if (subMode > 0)
    {
      self.iGpffffb310_subShot = subMode;
    }

    const OriginalEntity &player = environment.entityPool->slot(0);
    const std::int32_t sub = self.iGpffffb310_subShot;
    const auto ticks = static_cast<float>(static_cast<std::int32_t>(environment.frameTicks));

    // The two triples every non-spline shot fills in and the tail publishes.
    // The original leaves them uninitialised for a sub-shot it has no pose for
    // and publishes the stack garbage; zero is the port's stand-in.
    Vec3 eye{};
    Vec3 look{};
    bool publish = true;

    switch (mode)
    {
    case 1:
    {
      // Over the player's shoulder, two units out, looking one unit past them
      // toward the boss.
      const float bearing = FUN_0023a4b8_bearing(player, *target);
      float swung = 0.0f;
      if (sub == 1)
      {
        eye.z = player.positionY28;
        swung = (bearing - kFGpffff9078_pi) - kFGpffff907c_swing;
      }
      else if (sub == 2)
      {
        eye.z = player.positionY28;
        swung = (bearing - kFGpffff9080_pi) + kFGpffff9084_swing;
      }
      eye.x = player.positionX20 + cos_of(swung) * 2.0f;
      eye.y = player.positionZ24 + sin_of(swung) * 2.0f;
      look.x = player.positionX20 + cos_of(bearing);
      look.y = player.positionZ24 + sin_of(bearing);
      look.z = player.positionY28 + 1.0f;
      break;
    }

    case 2:
    {
      // The same shot wider: two and a half units out, a unit and a half up,
      // looking three units past.
      const float bearing = FUN_0023a4b8_bearing(player, *target);
      float swung = 0.0f;
      if (sub == 1)
      {
        eye.z = player.positionY28 + 1.5f;
        swung = (bearing - kFGpffff9088_pi) - kFGpffff908c_swing;
      }
      else if (sub == 2)
      {
        eye.z = player.positionY28 + 1.5f;
        swung = (bearing - kFGpffff9090_pi) + kFGpffff9094_swing;
      }
      eye.x = player.positionX20 + cos_of(swung) * 2.5f;
      eye.y = player.positionZ24 + sin_of(swung) * 2.5f;
      look.x = player.positionX20 + cos_of(bearing) * 3.0f;
      look.y = player.positionZ24 + sin_of(bearing) * 3.0f;
      look.z = player.positionY28;
      break;
    }

    case 3:
    {
      // The orbit. The camera creeps around the player at a twentieth of a
      // degree a tick until it passes the sub-shot's limit, and passing the
      // limit is what writes DAT_0035528C -- so the *next* shot the crab asks
      // for comes from the other side. Sub-shots 1 and 2 orbit three units out
      // and one point seven up; 3 and 4 orbit two units out and half a unit up.
      // Sub-shot 5 holds the angle where it is.
      //
      // The look-at angle is a literal zero, not a bearing: the register the
      // original hands to cos and sin here is the one it cleared on entry. So
      // the shot always looks at a point three units along +X of the player.
      const float zeroAngle = 0.0f;
      if (sub != self.iGpffffb314_builtFor)
      {
        self.fGpffffb320_orbitAngle = kFGpffff9098_startDefault;
        if (sub == 1 || sub == 5)
        {
          self.fGpffffb320_orbitAngle = kFGpffff90a8_start1and5;
        }
        else if (sub == 2)
        {
          self.fGpffffb320_orbitAngle = kFGpffff909c_start2;
        }
        else if (sub == 3)
        {
          self.fGpffffb320_orbitAngle = kFGpffff90a0_start3;
        }
        else if (sub == 4)
        {
          self.fGpffffb320_orbitAngle = kFGpffff90a4_start4;
        }
        self.iGpffffb314_builtFor = sub;
      }

      float angle = 0.0f;
      if (sub == 1)
      {
        angle = self.fGpffffb320_orbitAngle + ticks * kFGpffff90ac_rate1 * 0.03125f;
        self.fGpffffb320_orbitAngle = angle;
        if ((zeroAngle - kFGpffff90b0_limitBase1) + kFGpffff90b4_limitSpan1 <= angle)
        {
          DAT_0035528c_cameraSide() = 4;
        }
      }
      else if (sub == 2)
      {
        angle = self.fGpffffb320_orbitAngle - ticks * kFGpffff90b8_rate2 * 0.03125f;
        self.fGpffffb320_orbitAngle = angle;
        if (angle <= (zeroAngle - kFGpffff90bc_limitBase2) - kFGpffff90c0_limitSpan2)
        {
          DAT_0035528c_cameraSide() = 3;
        }
      }
      else if (sub == 3)
      {
        angle = self.fGpffffb320_orbitAngle + ticks * kFGpffff90c4_rate3 * 0.03125f;
        self.fGpffffb320_orbitAngle = angle;
        if ((zeroAngle - kFGpffff90c8_limitBase3) + kFGpffff90cc_limitSpan3 <= angle)
        {
          DAT_0035528c_cameraSide() = 2;
        }
      }
      else if (sub == 4)
      {
        angle = self.fGpffffb320_orbitAngle - ticks * kFGpffff90d0_rate4 * 0.03125f;
        self.fGpffffb320_orbitAngle = angle;
        if (angle <= (zeroAngle - kFGpffff90d4_limitBase4) - kFGpffff90d8_limitSpan4)
        {
          DAT_0035528c_cameraSide() = 1;
        }
      }
      else if (sub == 5)
      {
        angle = self.fGpffffb320_orbitAngle;
      }

      if (static_cast<std::uint32_t>(sub - 1) < 2u || sub == 5)
      {
        eye.x = player.positionX20 + cos_of(angle) * 3.0f;
        eye.y = player.positionZ24 + sin_of(angle) * 3.0f;
        eye.z = player.positionY28 + kFGpffff90dc_orbitHeight;
        look.x = player.positionX20 + cos_of(zeroAngle) * 3.0f;
        look.y = player.positionZ24 + sin_of(zeroAngle) * 3.0f;
        look.z = player.positionY28;
      }
      else if (static_cast<std::uint32_t>(sub - 3) < 2u)
      {
        eye.x = player.positionX20 + cos_of(angle) * 2.0f;
        eye.y = player.positionZ24 + sin_of(angle) * 2.0f;
        eye.z = player.positionY28 + 0.5f;
        look.x = player.positionX20 + cos_of(zeroAngle) * 3.0f;
        look.y = player.positionZ24 + sin_of(zeroAngle) * 3.0f;
        look.z = player.positionY28 + 0.5f;
      }
      break;
    }

    case 4:
    case 5:
      // Two shot numbers with no body. They still take the priority slot.
      return;

    case 6:
    {
      // The dodge chase, the shot FUN_0027D230 asks for while the player
      // tumbles. It turns the smear on for the whole move.
      set_smear_rotation(environment, 0x14);
      set_smear_alpha(environment, 0x50);
      if (sub == 1)
      {
        const float swung = (target->facingRadians5c + kFGpffff90e0_pi) + kFGpffff90e4_swing;
        eye.x = target->positionX20 + cos_of(swung) * 2.0f;
        eye.y = target->positionZ24 + sin_of(swung) * 2.0f;
        eye.z = target->positionY28 + 1.5f;
        look.x = player.positionX20;
        look.y = player.positionZ24;
        look.z = player.positionY28 + 1.0f;
      }
      if (sub == 2)
      {
        // The player's own facing this time, not the boss's.
        const float swung = (player.facingRadians5c + kFGpffff90e8_pi) - kFGpffff90ec_swing;
        eye.x = player.positionX20 + cos_of(swung) * 2.5f;
        eye.y = player.positionZ24 + sin_of(swung) * 2.5f;
        eye.z = player.positionY28 + 1.0f;
        look = position_of(*target);
      }
      if (sub == 3)
      {
        look = kFGpffff90f0_look3;
        eye = kFGpffff90fc_eye3;
      }
      break;
    }

    case 7:
    {
      // The throw's approach and carry. The subject is the *partner* -- the
      // entity the boss has in its claw, at +0x1C0 -- and the eye is parked
      // once per sub-shot and then held.
      const std::int32_t partnerSlot = target->crabPartner1c0;
      if (partnerSlot < 0 ||
          static_cast<std::size_t>(partnerSlot) >= environment.entityPool->slotCount())
      {
        return;
      }
      const OriginalEntity &partner =
          environment.entityPool->slot(static_cast<std::size_t>(partnerSlot));

      if (sub != self.iGpffffb314_builtFor)
      {
        if (sub == 1)
        {
          // FUN_0023a4b8(partner, target) -- the original throws the result
          // away. Kept because it is what the code does, and because it is the
          // only hint that this sub-shot once aimed itself.
          (void)FUN_0023a4b8_bearing(partner, *target);
          DAT_005739a0_heldEye() = kFGpffff9108_eye1;
        }
        if (sub == 2)
        {
          DAT_005739a0_heldEye() = kFGpffff9114_eye2;
        }
        if (sub == 3)
        {
          DAT_005739a0_heldEye() = kFGpffff911c_eye3;
        }
        if (sub == 4)
        {
          const float swung = target->facingRadians5c + kFGpffff9128_carrySwing;
          DAT_005739a0_heldEye().x = target->positionX20 - cos_of(swung) * 3.0f;
          DAT_005739a0_heldEye().y = target->positionZ24 - sin_of(swung) * 3.0f;
          DAT_005739a0_heldEye().z = target->positionY28 + 2.0f;
        }
        if (sub == 5)
        {
          DAT_005739a0_heldEye() = kFGpffff912c_eye5;
        }
        self.iGpffffb314_builtFor = sub;
      }

      eye = DAT_005739a0_heldEye();
      if (sub == 1)
      {
        look = kFGpffff9134_look1;
        camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(2.0f));
      }
      if (sub == 2)
      {
        look.x = kFGpffff9140_look2x;
        look.y = kFGpffff9144_look2y;
        look.z = target->positionY28 + 1.0f;
      }
      if (sub == 3)
      {
        look = kFGpffff9148_look3;
      }
      if (sub == 4)
      {
        look = position_of(partner);
      }
      if (sub == 5)
      {
        look.x = kFGpffff9154_look5x;
        look.y = kFGpffff9158_look5y;
        look.z = 0.0f;
      }
      break;
    }

    case 8:
      // Nothing but the smear rotation, and out.
      set_smear_rotation(environment, 0x14);
      return;

    case 9:
    {
      // The hurl. A two-point eye spline over 0x1680 ticks toward one fixed
      // look-at; once it runs out the eye holds the last point.
      if (sub != self.iGpffffb314_builtFor)
      {
        camera.FUN_00217e18_release_manual_camera(true);
        camera.FUN_00217fe8_set_camera_path(kDAT_0034e4d0_eye9, kDAT_0034e4e8_roll9,
                                            kDAT_0034e4e8_zoom9, kDAT_0034e4c0_look9);
        self.sGpffffb324_shot9Elapsed = 0;
        self.iGpffffb314_builtFor = sub;
      }
      look = kDAT_0034e4c0_look9[0];
      if (self.sGpffffb324_shot9Elapsed <= kShot9Duration)
      {
        camera.FUN_00218158_step_camera_path(self.sGpffffb324_shot9Elapsed, kShot9Duration);
        self.sGpffffb324_shot9Elapsed = static_cast<std::int16_t>(
            self.sGpffffb324_shot9Elapsed + static_cast<std::int32_t>(environment.frameTicks));
        eye = camera.DAT_0058c0a8_eye();
      }
      else
      {
        eye = kDAT_0034e4d0_eye9[1];
      }
      camera.FUN_00217d10_set_look_at(look);
      camera.FUN_00217d40_set_eye(eye);
      return;
    }

    case 10:
    {
      // The watch. Three eye points and two look-at points over 0x12C0 ticks;
      // when it runs out the last pair is published by hand, and the zoom it
      // finished on is saved for shot 12 to pick back up.
      if (sub != self.iGpffffb314_builtFor)
      {
        camera.FUN_00217e18_release_manual_camera(true);
        camera.FUN_00217fe8_set_camera_path(kDAT_0034e510_eye10, kDAT_0034e538_roll10,
                                            kDAT_0034e538_zoom10, kDAT_0034e4f8_look10);
        self.sGpffffb326_shot10Elapsed = 0;
        self.iGpffffb314_builtFor = sub;
      }
      if (self.sGpffffb326_shot10Elapsed <= kShot10Duration)
      {
        camera.FUN_00218158_step_camera_path(self.sGpffffb326_shot10Elapsed, kShot10Duration);
        self.sGpffffb326_shot10Elapsed = static_cast<std::int16_t>(
            self.sGpffffb326_shot10Elapsed + static_cast<std::int32_t>(environment.frameTicks));
        eye = camera.DAT_0058c0a8_eye();
      }
      else
      {
        eye = kDAT_0034e510_eye10[2];
        camera.FUN_00217d10_set_look_at(kDAT_0034e4f8_look10[1]);
      }
      camera.FUN_00217d40_set_eye(eye);
      self.uGpffffb318_savedZoom = camera.fGpffffb6e8_zoomLog2();
      return;
    }

    case 11:
    {
      // The late-fight moves. Nine sub-shot numbers, six of them used, each
      // parking an eye once and then holding it.
      set_smear_alpha(environment, 0x50);
      const float bearing = FUN_0023a4b8_bearing(player, *target);
      if (sub != self.iGpffffb314_builtFor)
      {
        if (sub == 1)
        {
          DAT_005739a0_heldEye() = Vec3{-1.5f, 1.5f, player.positionY28};
        }
        if (sub == 2)
        {
          DAT_005739a0_heldEye() =
              Vec3{kFGpffff915c_eye2x, kFGpffff9160_eye2y, player.positionY28};
        }
        if (sub == 4)
        {
          DAT_005739a0_heldEye() = Vec3{-2.0f, 2.0f, 1.5f};
        }
        if (sub == 7)
        {
          DAT_005739a0_heldEye() = Vec3{-11.0f, 5.0f, player.positionY28};
        }
        self.iGpffffb314_builtFor = sub;
      }
      eye = DAT_005739a0_heldEye();
      if (sub == 1 || sub == 2)
      {
        look.x = target->positionX20;
        look.y = target->positionZ24;
        look.z = target->positionY28 + 1.5f;
      }
      if (sub == 3)
      {
        const float swung = bearing + kFGpffff9164_swing3;
        set_smear_rotation(environment, 0x14);
        eye.x = player.positionX20 - cos_of(swung) * 2.0f;
        eye.y = player.positionZ24 - sin_of(swung) * 2.0f;
        eye.z = player.positionY28;
        look.x = target->positionX20;
        look.y = target->positionZ24;
        look.z = target->positionY28 + 1.0f;
        camera.setRoll(kFGpffff9168_roll3);
      }
      if (sub == 4)
      {
        look.x = 0.0f;
        look.y = 5.0f;
        look.z = target->positionY28 + kFGpffff916c_lift4;
        camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(2.0f));
      }
      if (sub == 7)
      {
        look.x = player.positionX20;
        look.y = player.positionZ24;
        look.z = player.positionY28 + kFGpffff9170_lift7;
        camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(2.0f));
      }
      if (sub == 9)
      {
        const float swung = target->facingRadians5c + kFGpffff9174_swing9;
        eye.x = target->positionX20 - cos_of(swung) * 2.5f;
        eye.y = target->positionZ24 - sin_of(swung) * 2.5f;
        eye.z = target->positionY28 + 1.0f;
        look.x = target->positionX20 + cos_of(target->facingRadians5c);
        look.y = target->positionZ24 + sin_of(target->facingRadians5c);
        look.z = target->positionY28 + 0.5f;
      }
      break;
    }

    case 12:
    {
      // The close-up. Sub-shot 1 looks at the role-9 bone of the mount the
      // throw stood up -- the close-up rig -- from two units off its facing,
      // zoomed to six; sub-shot 2 is a fixed pair that puts the zoom back where
      // shot 10 left it.
      const std::int32_t mountSlot = DAT_00325900_cinematicSlots()[0];
      if (sub == 1 && mountSlot >= 0 &&
          static_cast<std::size_t>(mountSlot) < environment.entityPool->slotCount())
      {
        const OriginalEntity &mount =
            environment.entityPool->slot(static_cast<std::size_t>(mountSlot));
        if (environment.FUN_0020dc88_bone_point)
        {
          look = environment.FUN_0020dc88_bone_point(static_cast<std::size_t>(mountSlot), 9,
                                                     Vec3{0.0f, 0.0f, kFGpffff9178_boneLift12});
        }
        const float swung = mount.facingRadians5c + kFGpffff917c_swing12;
        eye.x = mount.positionX20 + cos_of(swung) * 2.0f;
        eye.y = mount.positionZ24 + sin_of(swung) * 2.0f;
        eye.z = mount.positionY28 + 0.5f;
        camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2(6.0f));
      }
      if (sub == 2)
      {
        look = kFGpffff9180_look12b;
        eye = Vec3{0.75f, kFGpffff918c_eye12by, kFGpffff9190_eye12bz};
        camera.setZoomLog2(self.uGpffffb318_savedZoom);
      }
      break;
    }

    case 13:
    {
      // The same close-up pulling back: the look-at is sampled off the bone
      // once and held while the zoom eases from 10 down to 4.5 over 0x12C0.
      const std::int32_t mountSlot = DAT_00325900_cinematicSlots()[0];
      if (mountSlot < 0 ||
          static_cast<std::size_t>(mountSlot) >= environment.entityPool->slotCount())
      {
        return;
      }
      const OriginalEntity &mount =
          environment.entityPool->slot(static_cast<std::size_t>(mountSlot));
      if (sub != self.iGpffffb314_builtFor)
      {
        if (environment.FUN_0020dc88_bone_point)
        {
          DAT_005739a0_heldEye() = environment.FUN_0020dc88_bone_point(
              static_cast<std::size_t>(mountSlot), 9, Vec3{0.0f, 0.0f, kFGpffff9194_boneLift13});
        }
        self.iGpffffb314_builtFor = sub;
        self.sGpffffb328_shot13Elapsed = 0;
      }
      if (self.sGpffffb328_shot13Elapsed <= kShot13Duration)
      {
        self.sGpffffb328_shot13Elapsed = static_cast<std::int16_t>(
            self.sGpffffb328_shot13Elapsed + static_cast<std::int32_t>(environment.frameTicks));
      }
      const float progress =
          static_cast<float>(self.sGpffffb328_shot13Elapsed) / static_cast<float>(kShot13Duration);
      camera.setZoomLog2(orphen::ported::camera::FUN_00218230_zoomLog2((5.5f - progress * 5.5f) + 4.5f));

      const float swung = mount.facingRadians5c + kFGpffff9198_swing13;
      eye.x = mount.positionX20 + cos_of(swung) * 2.0f;
      eye.y = mount.positionZ24 + sin_of(swung) * 2.0f;
      eye.z = mount.positionY28 + 0.5f;
      look = DAT_005739a0_heldEye();
      break;
    }

    case 14:
    {
      // The spin-around that ends the animatic. The look-at spline starts from
      // wherever the camera is looking *now* and ends on a fixed point; the eye
      // walks three points, zooming 2 -> 4 -> 5, over 0x12C0 ticks. Nothing is
      // published by hand -- FUN_00218158 does all of it.
      if (sub != self.iGpffffb314_builtFor)
      {
        const std::array<Vec3, 2> lookPoints{
            {camera.DAT_0058be90_lookAt(), kFGpffff919c_lookEnd}};
        camera.FUN_00217e18_release_manual_camera(true);
        camera.FUN_00217fe8_set_camera_path(kDAT_0034e550_eye14, kDAT_0034e578_roll14,
                                            kDAT_0034e578_zoom14, lookPoints);
        self.sGpffffb32a_shot14Elapsed = 0;
        self.iGpffffb314_builtFor = sub;
      }
      self.sGpffffb32a_shot14Elapsed = static_cast<std::int16_t>(
          self.sGpffffb32a_shot14Elapsed + static_cast<std::int32_t>(environment.frameTicks));
      if (self.sGpffffb32a_shot14Elapsed > kShot14Duration)
      {
        return;
      }
      camera.FUN_00218158_step_camera_path(self.sGpffffb32a_shot14Elapsed, kShot14Duration);
      return;
    }

    default:
      // Every shot number above 14 falls out without touching the camera.
      publish = false;
      break;
    }

    if (publish)
    {
      camera.FUN_00217d10_set_look_at(look);
      camera.FUN_00217d40_set_eye(eye);
    }
  }

} // namespace orphen::ported::entity
