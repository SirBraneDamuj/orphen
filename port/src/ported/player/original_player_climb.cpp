// Climbing: FUN_00252DE0 and the three states it leads to.
//
//   src/FUN_00252DE0.c   Cross at a climbable face -- grab it (from states 0/1)
//   src/FUN_002537A0.c   PTR_FUN_0031E0E8[3], hanging: read the stick, pick a face
//   src/FUN_00253BE8.c   PTR_FUN_0031E0E8[4], moving onto that face
//   src/FUN_002540D0.c   PTR_FUN_0031E0E8[5], climbing over the lip at the top
//
// The faces are map primitives -- see original_climb_graph.h. The character
// is held a fixed distance off the face's centre, facing into it, and moves
// face to face: state 3 picks the neighbour the stick points at, state 4 walks
// the body onto it at +0x1B0 per tick (0.0003, a hundredth of a unit a frame),
// and hands back to state 3 on arrival. Up from a face whose terrain nibble is
// 0xE climbs over the top instead. Cross again lets go.
//
// All of it holds a manual camera: a point 2.5 out from the wall, looking at
// the body, which the move states nudge as the body goes.

#include "ported/player/original_player_controller.h"

#include "ported/entity/actor_frame_update.h"
#include "ported/entity/original_climb_graph.h"
#include "ported/entity/original_entity_sound.h"
#include "ported/model/psc3_skeleton.h"

#include <cmath>

namespace orphen::ported::player
{

  namespace
  {
    using orphen::ported::model::FUN_002166e8_angle_delta;
    using orphen::ported::psm2::Vec3;

    // FUN_00252DE0.
    constexpr float kuGpffff88f4_climbSpeed = 0.0003000000142492354f;
    constexpr float kfGpffff88f8_standOff = 0.15000000596046448f;
    constexpr float kfGpffff88fc_faceTheWall = 3.141592025756836f;

    // FUN_002537A0's stick sectors, on DAT_003555E4 = atan2(up, right).
    constexpr float kDAT_0035288c_upLow = 1.1344637870788574f;
    constexpr float kDAT_00352890_upHigh = 2.0071282386779785f;
    constexpr float kDAT_00352894_climbOverSpeed = 0.0003000000142492354f;
    constexpr float kDAT_00352898_downLow = -2.1816611289978027f;
    constexpr float kDAT_0035289c_downHigh = -1.1344637870788574f;
    constexpr float kDAT_003528a0_rightLow = -0.43633222579956055f;
    constexpr float kDAT_003528a4_rightHigh = 0.43633222579956055f;
    constexpr float kDAT_003528a8_pi = 3.141592025756836f;
    constexpr float kDAT_003528ac_leftLow = 2.7052597999572754f;
    constexpr float kDAT_003528b0_negPi = -3.141592025756836f;
    constexpr float kDAT_003528b4_leftHigh = -2.7052597999572754f;
    // The turn rate a corner gets: under 20 degrees, under 60, and beyond.
    constexpr float kDAT_003528b8_gentleLow = -0.34906578063964844f;
    constexpr float kDAT_003528bc_gentleHigh = 0.34906578063964844f;
    constexpr float kDAT_003528c0_gentleRate = 0.004000000189989805f;
    constexpr float kDAT_003528c4_cornerLow = -1.0471973419189453f;
    constexpr float kDAT_003528c8_cornerHigh = 1.0471973419189453f;
    constexpr float kDAT_003528cc_cornerRate = 0.019999999552965164f;
    constexpr float kDAT_003528d0_sharpRate = 0.05999999865889549f;
    constexpr float kDAT_003528d4_moveSpeed = 0.0003000000142492354f;

    // FUN_00253BE8.
    constexpr float kfGpffff8968_standOff = 0.15000000596046448f;
    constexpr float kuGpffff896c_pitchStep = 0.004999999888241291f;
    constexpr float kfGpffff8970_faceTheWall = 3.141592025756836f;
    constexpr float kfGpffff8974_cameraRise = 0.0015625000232830644f;

    // FUN_002540D0.
    constexpr float kDAT_003528e8_hopVelocity = 0.05299999937415123f;
    constexpr float kDAT_003528ec_hopForward = 0.0009374999790452421f;

    // +0x0C bit 0, and FUN_00225C90's "a new timeline entry was taken" on +0x06.
    constexpr std::uint32_t kPhysicsFlagGrounded = 0x0001;
    constexpr std::uint16_t kAnimationStepped06 = 0x0008;

    constexpr float kCameraDistance = 2.5f;
    constexpr float kNoGround = 128.0f;
    constexpr float kStickDeadZone = 80.0f;
    constexpr std::uint16_t kCameraSettleFrames = 0x60;

    constexpr std::uint16_t kAnimationClimbHang = 0x19;
    constexpr std::uint16_t kAnimationClimbVertical = 0x18;
    constexpr std::uint16_t kAnimationClimbLeft = 0x1a;
    constexpr std::uint16_t kAnimationClimbRight = 0x1b;
    constexpr std::uint16_t kAnimationClimbOver = 0x2e;
    constexpr std::uint16_t kAnimationClimbLand = 0x10;

    // FUN_002540D0's two cursor tests: rise until the timeline reaches entry 7,
    // hop off on the frame it enters entry 8.
    constexpr std::uint16_t kClimbOverRiseEnds = 7;
    constexpr std::uint16_t kClimbOverHopEntry = 8;

    // FUN_00251C80 indices: the two climbing scrapes and the hop.
    constexpr int kCueClimbVertical = 0x10;
    constexpr int kCueClimbSideways = 0x11;
    constexpr int kCueClimbOverHop = 8;

    float normalHeading(const orphen::ported::psm2::Psm2RuntimeState &map, std::int16_t primitive)
    {
      const Vec3 &normal = map.DAT_003556ac_dRecords80[static_cast<std::size_t>(primitive)].normal;
      return std::atan2(normal.y, normal.x);
    }

    bool validFace(const orphen::ported::psm2::Psm2RuntimeState &map, std::int16_t primitive)
    {
      return primitive >= 0 &&
             static_cast<std::size_t>(primitive) < map.DAT_003556ac_dRecords80.size() &&
             static_cast<std::size_t>(primitive) < map.DAT_003556b0_dRecords78.size();
    }

    float probeGround(const orphen::ported::entity::ActorEnvironment &environment, float x, float y, float z)
    {
      return environment.FUN_00227798_probe ? environment.FUN_00227798_probe(x, y, z).height : kNoGround;
    }
  } // namespace

  // FUN_00252DE0. Returns true when the climb started.
  bool OriginalPlayerController::FUN_00252de0_start_climb(std::uint32_t frameTicks,
                                                           const OriginalPlayerFrameInput &input)
  {
    // `uGpffffb68a & 0x40`: Cross, pressed this frame.
    if (!input.interactPressed)
    {
      return false;
    }
    if (entity().slopeSlide1ba != 0 || entity().verticalVelocity44 != 0.0f)
    {
      return false;
    }
    if (!actionEffect_.actorEnvironment)
    {
      return false;
    }
    const auto environment = actionEffect_.actorEnvironment(frameTicks);
    if (environment.psm2Map == nullptr || environment.DAT_00355020_climbGraph == nullptr)
    {
      return false;
    }
    const auto &map = *environment.psm2Map;

    // +0x1B4 takes the answer whether or not there is one.
    const std::int16_t face =
        orphen::ported::entity::FUN_00257160_find_climb_face(entity(), *environment.DAT_00355020_climbGraph, map);
    entity().climbFace1b4 = face;
    if (face < 0 || !validFace(map, face))
    {
      return false;
    }

    entity().collisionFlags0c &= 0xFFFFFFFEu;
    entity().fadeRamp62 = 0;
    entity().halfword04 = static_cast<std::uint16_t>(entity().halfword04 | 0x0008u); // gravity off
    entity().groundPrimitive0a = -1;
    entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 & 0xFFFBu);
    entity().playerSpeed1b0 = kuGpffff88f4_climbSpeed;
    entity().climbStartGround1ac = entity().groundHeight4c;
    FUN_00225bf0_set_entity_state(kStateClimbHold, kAnimationClimbHang);

    // Onto the face: its centre, pushed out along the normal by the body's
    // radius and a little more, facing in, the middle of the body level with
    // the middle of the face.
    const Vec3 &centre = map.DAT_003556ac_dRecords80[static_cast<std::size_t>(face)].center;
    const float standOff = entity().radius54 + kfGpffff88f8_standOff;
    const float heading = normalHeading(map, face);
    entity().climbHeading1a4 = heading;
    entity().facingRadians5c = heading + kfGpffff88fc_faceTheWall;
    entity().positionX20 = centre.x + standOff * std::cos(heading);
    entity().positionZ24 = centre.y + standOff * std::sin(heading);
    entity().positionY28 = centre.z - entity().height58 * 0.5f;

    if (environment.camera != nullptr)
    {
      const float eyeX = entity().positionX20 + std::cos(heading) * kCameraDistance;
      const float eyeY = entity().positionZ24 + std::sin(heading) * kCameraDistance;
      const Vec3 lookAt{entity().positionX20, entity().positionZ24,
                        entity().positionY28 + entity().height58 * 0.5f};
      if (orphen::ported::entity::FUN_002298d0_character_class(entity().typeId00) == 3)
      {
        // Class 3 looks down on the climb from four body-heights up.
        environment.camera->FUN_00217d70_set_manual_camera(
            {eyeX, eyeY, entity().positionY28 + entity().height58 * 4.0f}, lookAt);
      }
      else
      {
        // Everyone else from level with the feet, or the ground out there if
        // that is higher.
        float eyeZ = entity().positionY28;
        const float ground = probeGround(environment, eyeX, eyeY, eyeZ);
        if (ground < kNoGround && eyeZ < ground)
        {
          eyeZ = ground;
        }
        environment.camera->FUN_00217d70_set_manual_camera({eyeX, eyeY, eyeZ}, lookAt);
      }
    }

    // DAT_003555D1: no embedded-corner push-out while anyone is on a wall.
    orphen::ported::entity::DAT_003555d1_suspendPushOut() = true;
    return true;
  }

  // FUN_002537A0, state 3: hanging on +0x1B4.
  void OriginalPlayerController::FUN_002537a0_update_climb_hold(std::uint32_t frameTicks,
                                                                const OriginalPlayerFrameInput &input)
  {
    // `DAT_003555FA & 0x40`: Cross lets go.
    if (input.interactPressed)
    {
      FUN_00252d88_return_to_idle_state();
      FUN_002536a8_break_out_of_state();
      // FUN_00217E18(1). FUN_002536A8 has already released the camera without
      // restoring it, so this one finds nothing installed and does nothing.
      if (actionEffect_.actorEnvironment)
      {
        const auto environment = actionEffect_.actorEnvironment(frameTicks);
        if (environment.camera != nullptr)
        {
          environment.camera->FUN_00217e18_release_manual_camera(true);
        }
      }
      return;
    }
    if (!actionEffect_.actorEnvironment)
    {
      return;
    }
    const auto environment = actionEffect_.actorEnvironment(frameTicks);
    if (environment.psm2Map == nullptr || environment.DAT_00355020_climbGraph == nullptr ||
        !validFace(*environment.psm2Map, entity().climbFace1b4))
    {
      return;
    }
    const auto &map = *environment.psm2Map;
    const auto &graph = *environment.DAT_00355020_climbGraph;
    const std::int16_t face = entity().climbFace1b4;

    std::int16_t next = -1;
    bool idle = input.stickMagnitude < kStickDeadZone;
    if (!idle)
    {
      const float angle = input.stickAngle;
      const bool up = kDAT_0035288c_upLow < angle && angle < kDAT_00352890_upHigh;
      const bool down = kDAT_00352898_downLow < angle && angle < kDAT_0035289c_downHigh;
      const bool right = kDAT_003528a0_rightLow < angle && angle < kDAT_003528a4_rightHigh;
      const bool left = (kDAT_003528ac_leftLow < angle && angle <= kDAT_003528a8_pi) ||
                        (kDAT_003528b0_negPi < angle && angle < kDAT_003528b4_leftHigh);
      if (up)
      {
        const auto terrain = map.DAT_003556b0_dRecords78[static_cast<std::size_t>(face)].terrainFlags;
        if (orphen::ported::entity::isClimbLipTerrain(terrain))
        {
          // Over the top.
          entity().playerSpeed1b0 = kDAT_00352894_climbOverSpeed;
          entity().climbSavedStepDown1a8 = entity().maxStepDown7c;
          entity().maxStepDown7c = kNoGround;
          FUN_00225bf0_set_entity_state(kStateClimbOver, kAnimationClimbOver);
          if (environment.camera != nullptr)
          {
            environment.camera->FUN_00217e18_release_manual_camera(false);
          }
          return;
        }
        next = orphen::ported::entity::FUN_002573d8_find_climb_neighbour(entity(), graph, map, face, 1);
        if (next >= 0)
        {
          entity().animationA0 = kAnimationClimbVertical;
        }
      }
      else if (down)
      {
        next = orphen::ported::entity::FUN_002573d8_find_climb_neighbour(entity(), graph, map, face, 3);
        if (next >= 0)
        {
          entity().animationA0 = kAnimationClimbVertical;
        }
      }
      else if (right)
      {
        next = orphen::ported::entity::FUN_002573d8_find_climb_neighbour(entity(), graph, map, face, 2);
        if (next >= 0)
        {
          entity().animationA0 = kAnimationClimbRight;
        }
      }
      else if (left)
      {
        next = orphen::ported::entity::FUN_002573d8_find_climb_neighbour(entity(), graph, map, face, 0);
        if (next >= 0)
        {
          entity().animationA0 = kAnimationClimbLeft;
        }
      }
      else
      {
        // Between the sectors counts as no stick at all.
        idle = true;
      }
    }

    if (idle)
    {
      // Class 3 keeps the camera FUN_00252DE0 gave it. Anyone else, after 0x60
      // frames still, has the eye drift towards the middle of the body --
      // provided there is ground under the eye and it is below the eye.
      if (orphen::ported::entity::FUN_002298d0_character_class(entity().typeId00) != 3)
      {
        entity().idleTimer1b6 = static_cast<std::uint16_t>(entity().idleTimer1b6 + 1);
        if (static_cast<std::int16_t>(entity().idleTimer1b6) > static_cast<std::int16_t>(kCameraSettleFrames) &&
            environment.camera != nullptr)
        {
          const float heading = normalHeading(map, face);
          const float eyeX = entity().positionX20 + std::cos(heading) * kCameraDistance;
          const float eyeY = entity().positionZ24 + std::sin(heading) * kCameraDistance;
          const float eyeZ = environment.camera->DAT_0058c0a8_eye().z;
          const float ground = probeGround(environment, eyeX, eyeY, eyeZ);
          if (ground < kNoGround && ground < eyeZ)
          {
            float drift = entity().playerSpeed1b0;
            if ((entity().positionY28 + entity().height58 * 0.5f) - eyeZ <= 0.0f)
            {
              drift = -drift;
            }
            // `(float)(DAT_003555BC / 2)` -- an integer halving.
            environment.camera->FUN_00217d40_set_eye(
                {eyeX, eyeY, eyeZ + drift * static_cast<float>(frameTicks / 2)});
          }
        }
      }
    }

    if (next < 0)
    {
      entity().animationA0 = kAnimationClimbHang;
      return;
    }

    // How hard the camera swings to follow depends on how sharply the next
    // face turns away from this one.
    const float turn = FUN_002166e8_angle_delta(normalHeading(map, face), normalHeading(map, next));
    if (turn <= kDAT_003528b8_gentleLow || kDAT_003528bc_gentleHigh <= turn)
    {
      entity().climbTurnRate1a0 = (turn <= kDAT_003528c4_cornerLow || kDAT_003528c8_cornerHigh <= turn)
                                      ? kDAT_003528d0_sharpRate
                                      : kDAT_003528cc_cornerRate;
    }
    else
    {
      entity().climbTurnRate1a0 = kDAT_003528c0_gentleRate;
    }
    entity().climbFace1b4 = next;
    entity().playerSpeed1b0 = kDAT_003528d4_moveSpeed;
    // A bare +0x60 write: the animation just chosen is not reset.
    entity().state60 = kStateClimbMove;
    playCharacterCue(entity().animationA0 == kAnimationClimbVertical ? kCueClimbVertical : kCueClimbSideways);
  }

  // FUN_00253BE8, state 4: carry the body onto +0x1B4.
  void OriginalPlayerController::FUN_00253be8_update_climb_move(std::uint32_t frameTicks,
                                                                const OriginalPlayerFrameInput &input)
  {
    if (!actionEffect_.actorEnvironment)
    {
      return;
    }
    const auto environment = actionEffect_.actorEnvironment(frameTicks);
    if (environment.psm2Map == nullptr || !validFace(*environment.psm2Map, entity().climbFace1b4))
    {
      return;
    }
    const auto &map = *environment.psm2Map;
    auto *camera = environment.camera;
    const auto eyeZ = [camera] { return camera != nullptr ? camera->DAT_0058c0a8_eye().z : 0.0f; };
    const auto setEye = [camera](float x, float y, float z)
    {
      if (camera != nullptr)
      {
        camera->FUN_00217d40_set_eye({x, y, z});
      }
    };

    const auto &record80 = map.DAT_003556ac_dRecords80[static_cast<std::size_t>(entity().climbFace1b4)];
    const float step = entity().playerSpeed1b0 * static_cast<float>(frameTicks);
    const float heading = std::atan2(record80.normal.y, record80.normal.x);
    const float reach = entity().radius54 + kfGpffff8968_standOff;

    // Lean the body to the face's pitch, and face into it.
    const float pitch = std::atan2(record80.normal.z,
                                   std::sqrt(record80.normal.x * record80.normal.x +
                                             record80.normal.y * record80.normal.y));
    entity().rotationX154 += orphen::ported::entity::FUN_0023a320_approach_angle(
        entity().rotationX154, pitch, kuGpffff896c_pitchStep);
    entity().facingRadians5c = heading + kfGpffff8970_faceTheWall;

    const float offsetX = (record80.center.x + reach * std::cos(heading)) - entity().positionX20;
    const float offsetY = (record80.center.y + reach * std::sin(heading)) - entity().positionZ24;

    // +0x1A4 turns towards the face's heading at +0x1A0 a frame; it is only
    // the camera's, the body faces the wall outright.
    const float swing = orphen::ported::entity::FUN_0023a320_approach_angle(
        entity().climbHeading1a4, heading, entity().climbTurnRate1a0);
    const float cameraHeading = swing != 0.0f ? swing + entity().climbHeading1a4 : heading;
    entity().climbHeading1a4 = cameraHeading;

    bool arrivedAcross = false;
    if (step <= std::sqrt(offsetX * offsetX + offsetY * offsetY))
    {
      const float direction = std::atan2(offsetY, offsetX);
      entity().positionX20 += step * std::cos(direction);
      entity().positionZ24 += step * std::sin(direction);
      const float ground = probeGround(environment, entity().positionX20, entity().positionZ24, entity().positionY28);
      if (ground < entity().positionY28)
      {
        entity().groundHeight4c = ground;
      }

      // Once the ground has risen a unit above where the climb started and
      // past the eye, lift the eye too -- while the eye is still below the
      // middle of the body.
      float rise = 0.0f;
      if (1.0f < entity().groundHeight4c - entity().climbStartGround1ac && eyeZ() < entity().groundHeight4c &&
          eyeZ() < entity().positionY28 + entity().height58 * 0.5f)
      {
        rise = static_cast<float>(frameTicks) * kfGpffff8974_cameraRise;
      }
      if (entity().state60 == kStateClimbMove)
      {
        setEye(entity().positionX20 + std::cos(cameraHeading) * kCameraDistance,
               entity().positionZ24 + std::sin(cameraHeading) * kCameraDistance, eyeZ() + rise);
      }
    }
    else
    {
      arrivedAcross = true;
    }

    const float bodyBase = entity().positionY28;
    const float toGo = record80.center.z - (bodyBase + entity().height58 * 0.5f);
    float climb = 0.0f;
    bool moveVertically = false;
    if (0.0f < toGo)
    {
      if (!(toGo < step))
      {
        climb = -step;
        moveVertically = true;
      }
    }
    else if (step <= -toGo && entity().groundHeight4c < bodyBase)
    {
      climb = step;
      moveVertically = true;
    }

    if (moveVertically)
    {
      entity().positionY28 = bodyBase - climb;
      if (entity().state60 != kStateClimbMove)
      {
        return;
      }
      // The eye follows only once the body is more than 2.5 below it or 3.0
      // above it.
      const float lead = (bodyBase - climb) - eyeZ();
      if (!(lead <= 3.0f && -2.5f <= lead))
      {
        setEye(entity().positionX20 + std::cos(cameraHeading) * kCameraDistance,
               entity().positionZ24 + std::sin(cameraHeading) * kCameraDistance, eyeZ() - climb);
      }
    }
    else if (arrivedAcross)
    {
      if (entity().state60 != kStateClimbMove)
      {
        return;
      }
      if (FUN_002166e8_angle_delta(entity().climbHeading1a4, heading) != 0.0f)
      {
        setEye(entity().positionX20 + std::cos(heading) * kCameraDistance,
               entity().positionZ24 + std::sin(heading) * kCameraDistance, eyeZ());
      }
      entity().rotationX154 = pitch;
      entity().idleTimer1b6 = 0;
      entity().state60 = kStateClimbHold;
      FUN_002537a0_update_climb_hold(frameTicks, input);
    }

    if (entity().state60 != kStateClimbMove)
    {
      return;
    }
    if (camera != nullptr)
    {
      camera->FUN_00217d10_set_look_at(
          {entity().positionX20, entity().positionZ24, entity().positionY28 + entity().height58 * 0.5f});
    }
  }

  // FUN_002540D0, state 5: up and over the lip.
  void OriginalPlayerController::FUN_002540d0_update_climb_over(std::uint32_t frameTicks)
  {
    if (entity().animationA0 != kAnimationClimbOver)
    {
      // The landing animation below has taken over; idle when it ends.
      FUN_002560e8_end_on_animation_complete();
      return;
    }

    if (entity().timelineCursorA8 < kClimbOverRiseEnds)
    {
      entity().positionY28 += entity().playerSpeed1b0 * static_cast<float>(frameTicks);
      return;
    }

    if ((entity().flags06 & kAnimationStepped06) != 0 && entity().timelineCursorA8 == kClimbOverHopEntry)
    {
      // The hop off the top: gravity back on, a little upward speed.
      entity().halfword04 = static_cast<std::uint16_t>(entity().halfword04 & 0xFFF7u);
      entity().halfword08 = static_cast<std::uint16_t>(entity().halfword08 | 0x0004u);
      entity().collisionFlags0c &= 0xFFFFFFFEu;
      entity().verticalVelocity44 = kDAT_003528e8_hopVelocity;
      if (actionEffect_.actorEnvironment)
      {
        const auto environment = actionEffect_.actorEnvironment(frameTicks);
        const float ground =
            probeGround(environment, entity().positionX20, entity().positionZ24, entity().positionY28);
        if (ground < entity().positionY28)
        {
          entity().groundHeight4c = ground;
        }
      }
      playCharacterCue(kCueClimbOverHop);
      return;
    }

    if ((entity().collisionFlags0c & kPhysicsFlagGrounded) != 0)
    {
      orphen::ported::entity::FUN_00225bc8_set_animation(entity(), kAnimationClimbLand);
      entity().maxStepDown7c = entity().climbSavedStepDown1a8;
      if (FUN_00267d38_playSound_)
      {
        FUN_00267d38_playSound_(orphen::ported::entity::FUN_00255d88_surface_cue(
                                    entity().typeId00, currentSurfaceTerrainFlags(),
                                    entity().interactTarget68 >= 0,
                                    orphen::ported::entity::SurfaceSoundKind::Extra),
                                entity());
      }
      return;
    }

    // In the air: carry on forwards.
    const float forward = static_cast<float>(frameTicks) * kDAT_003528ec_hopForward;
    entity().desiredDeltaX30 += forward * std::cos(entity().facingRadians5c);
    entity().desiredDeltaZ34 += forward * std::sin(entity().facingRadians5c);
  }

} // namespace orphen::ported::player
