#include "ported/entity/original_mast_boss.h"

#include "ported/camera/original_camera_path.h"
#include "ported/camera/original_field_camera.h"
#include "ported/entity/entity_pool.h"
#include "ported/model/psc3_skeleton.h"
#include "ported/entity/original_enemy_attack.h"
#include "ported/battle/battle_tables.h"
#include "ported/battle/battle_target_markers.h"
#include "ported/entity/original_dust_pool.h"
#include "ported/entity/original_hit_test.h"
#include "ported/entity/original_mast_camera.h"
#include "ported/render/original_frame_feedback.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::camera::CubicSpline;
    using orphen::ported::psm2::Vec3;

    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to);

    // ---------------------------------------------------------------- the data
    //
    // DAT_0034EB60..0x0034EC4C: six blocks of three points, read out of
    // SLUS_200.11. FUN_0029DAC0 takes them in pairs, and the pair is chosen by
    // the work block's own cursor, so three carries in a row use three different
    // routes through the rigging before it comes back round.
    //
    // **The first point of an odd-numbered block is never read.** FUN_0029DAC0
    // copies from that block at +0x0C, not +0x00, because the leg it builds
    // starts at wherever the player is standing. Block 0's first point is zero
    // in the executable for exactly that reason; blocks 2 and 4's are too.
    using SplineBlock = std::array<Vec3, 3>;
    inline constexpr std::array<SplineBlock, 6> kDAT_0034eb60_carryPoints{{
        // 0x0034EB60, the leg up: point 0 unused, then two rungs of the shroud.
        {{{0.0f, 0.0f, 0.0f},
          {-6.123000f, -2.760000f, 12.072000f},
          {-7.190000f, -2.384000f, 11.068000f}}},
        // 0x0034EB88, the leg across onto the crow's nest.
        {{{-7.190000f, -2.384000f, 11.068000f},
          {-6.657000f, -1.213000f, 14.145000f},
          {-6.175000f, -0.894000f, 12.599000f}}},
        // 0x0034EBB0
        {{{0.0f, 0.0f, 0.0f},
          {-2.737000f, -1.999000f, 14.041000f},
          {-0.662000f, -2.927000f, 12.136000f}}},
        // 0x0034EBD8
        {{{-0.662000f, -2.927000f, 12.136000f},
          {3.932000f, -1.410000f, 14.767000f},
          {5.780000f, -0.682000f, 12.599000f}}},
        // 0x0034EC00
        {{{0.0f, 0.0f, 0.0f},
          {1.965000f, 1.952000f, 19.540001f},
          {-0.620000f, 3.132000f, 17.950001f}}},
        // 0x0034EC28
        {{{-0.620000f, 3.132000f, 17.950001f},
          {0.694000f, 2.222000f, 18.424000f},
          {0.732000f, -0.190000f, 15.099000f}}},
    }};

    // DAT_00353950 / DAT_00353954 for FUN_0029C198 and DAT_003539D0 /
    // DAT_003539D4 for FUN_0029D658. Both pairs are 0.4 and 0.8: the fractions
    // of the carry at which the camera moves from sub-shot 1 to 2 and 2 to 3.
    inline constexpr float kDAT_00353950_shotSplitA = 0.4f;
    inline constexpr float kDAT_00353954_shotSplitB = 0.8f;
    inline constexpr float kDAT_003539d0_carryShotSplitA = 0.4f;
    inline constexpr float kDAT_003539d4_carryShotSplitB = 0.8f;

    // The carry's length, in the same ticks DAT_003555BC counts. `0x12C0 < t`
    // is the test and 4800.0 the divisor, so a leg is 150 nominal frames and the
    // curve parameter reaches exactly 1.0 on the frame it ends.
    inline constexpr std::int32_t kCarryTicks = 0x12C0;
    inline constexpr float kCarryTicksFloat = 4800.0f;

    // The two cues FUN_0029C198 keys, both on the player: 0x57 as each leg
    // starts and 0x4B as the carry ends.
    inline constexpr std::uint16_t kCarryLaunchCue = 0x57;
    inline constexpr std::uint16_t kCarryLandCue = 0x4B;
    // FUN_00295A60(0x131) -- the boss's own hit cue, through FUN_00267D88.
    inline constexpr std::uint16_t kMastHitCue = 0x131;
    // The flash the hit sets on +0x1BC, and the smear alpha the fight runs at.
    inline constexpr std::uint16_t kMastFlashTicks = 0xC80;
    inline constexpr std::uint8_t kMastSmearAlpha = 0x3C;

    // Segment dimensions, written over type 0xC0's descriptor by state 0.
    inline constexpr float kSegmentRadius = 0.75f;
    inline constexpr float kSegmentHeight = 1.5f;
    inline constexpr float kSegmentGround = -56.0f;
    // The scale state 0 gives the boss itself, twice: +0x14C and +0x150.
    inline constexpr float kMastScale = 22.0f;
    // FUN_00299390's "the player is down" arm parks the player's ground height
    // here before it returns.
    inline constexpr float kDownedGroundHeight = 2.0f;

    // -------------------------------------------------------- the work block
    //
    // DAT_00355DB8, the 0x5B8 bytes state 0 takes off the script arena. Only
    // the fields the ported states touch are modelled; the rest of the block is
    // scratch for states this pass does not have.
    struct MastWork
    {
      std::uint8_t byte00_routeCursor = 0; // +0x00, picks the pair of blocks
      std::uint8_t byte01_carryMode = 0;   // +0x01, 0/1/2/3/9
      std::uint8_t byte02_carryPhase = 0;  // +0x02, which leg is running
      SplineBlock legA{};                  // +0x04..+0x24
      SplineBlock legB{};                  // +0x28..+0x48
      std::array<CubicSpline, 3> curve{};  // +0x4C, FUN_00266A78's output
      // +0x250/+0x254/+0x258, the three FUN_00216078 records.
      EnemyAttackRecords attacks{};
      // +0x264..+0x284 and +0x288: the fight's *own* curve, a second one
      // entirely. The intro's lives at +0x04/+0x4C and the two are never in
      // flight at the same time, but they are separate storage in the original
      // and separate storage here.
      SplineBlock divePoints{};
      std::array<CubicSpline, 3> diveCurve{};
      // +0x4B0, nine pool slots.
      std::array<std::int32_t, kMastSegmentCount> segments{{-1, -1, -1, -1, -1, -1, -1, -1, -1}};
      // +0x25C/+0x260: the scene's own fog band, kept while the transformation
      // borrows it.
      float fogNearSave25c = 0.0f;
      float fogFarSave260 = 0.0f;
      // +0x4D4, five: the beam's links, grown one at a time. +0x4E8 is the
      // blast at the end of a chain and +0x4EC the head the second chain runs
      // off. All six are pool slots biased by one, because zero is "empty".
      std::array<std::int32_t, 5> beamChain{};
      std::int32_t blast4e8 = 0;
      std::int32_t beamHead4ec = 0;
      // Not in the original's block: the twelve accumulated break angles
      // FUN_0029D498 keeps in the collision-group records themselves. See its
      // note.
      std::array<float, 12> breakAngles{};
      // +0x48C, nine: the type 0x1C5 limbs the death throws off the segment
      // bones. They sit immediately below the segments and nothing but state 12
      // touches them.
      std::array<std::int32_t, kMastSegmentCount> deathLimbs{};
      // +0x4F0, thirty: the debris a smash throws. FUN_0029E078 fills them and
      // FUN_0029DFB8 steps them every frame the mode byte is 14.
      std::array<std::int32_t, 30> projectiles{};
      // +0x568, ten: the type 0x1B0 trails a shed streak drags and the burst it
      // leaves on the player. Stepped by FUN_0029EC60.
      std::array<std::int32_t, 10> trails{};
      // +0x590, ten more: the parts a pass sheds. The last thing in the block.
      std::array<std::int32_t, 10> shedParts{{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
      bool allocated = false;
    };

    MastWork &DAT_00355db8_work()
    {
      static MastWork value{};
      return value;
    }

    // How many frames the boss has spent in a move the rotation picked and this
    // port has no handler for. Reported, because a boss standing still is
    // otherwise indistinguishable from one that is waiting on purpose.
    std::uint32_t &unportedMoveFrames()
    {
      static std::uint32_t value = 0;
      return value;
    }
    std::uint16_t &unportedMoveState()
    {
      static std::uint16_t value = 0;
      return value;
    }

    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    // FUN_00266A78(curve, points, 3, 0): three points, uniform knots i/(n-1).
    void FUN_00266a78_build(std::array<CubicSpline, 3> &curve, const SplineBlock &points)
    {
      static constexpr std::array<float, 3> knots{{0.0f, 0.5f, 1.0f}};
      const std::array<float, 3> x{{points[0].x, points[1].x, points[2].x}};
      const std::array<float, 3> z{{points[0].y, points[1].y, points[2].y}};
      const std::array<float, 3> y{{points[0].z, points[1].z, points[2].z}};
      curve[0].build(knots, x);
      curve[1].build(knots, z);
      curve[2].build(knots, y);
    }

    // FUN_00266CE8(t, curve, out): the three channels together.
    Vec3 FUN_00266ce8_sample(const std::array<CubicSpline, 3> &curve, float t)
    {
      return Vec3{curve[0].evaluate(t), curve[1].evaluate(t), curve[2].evaluate(t)};
    }

    // FUN_0029C510: the scene script's work word 1, and the only writer of it in
    // the game. The object script's beat 30 is waiting on this; without it the
    // animatic never leaves the intro even once the boss has finished.
    void FUN_0029c510_set_script_cue(bool done, const ActorEnvironment &environment)
    {
      if (environment.DAT_00355060_setScriptWork)
      {
        environment.DAT_00355060_setScriptWork(1, done ? 1u : 0u);
      }
    }

    // FUN_0023DB50, the wrapper's tail. A shake counter in sGpffffAF1E that
    // pumps the frame smear while it drains: scale 0x32 on both axes and an
    // alpha of 0x5A plus however many frames are left. Nothing in the intro
    // arms it -- the counter is written by the hit paths -- but the call is at
    // the bottom of every frame the boss runs, so it is here.
    std::int16_t &sGpffffaf1e_shakeFrames()
    {
      static std::int16_t value = 0;
      return value;
    }

    void FUN_0023db50_pump_shake(const ActorEnvironment &environment)
    {
      std::int16_t &frames = sGpffffaf1e_shakeFrames();
      if (frames == 0)
      {
        return;
      }
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(
            static_cast<std::uint8_t>(static_cast<std::uint8_t>(frames) + 0x5Au));
        environment.DAT_00343878_frameFeedback->set_DAT_0034387c_scale(0x32, 0x32);
      }
      frames = static_cast<std::int16_t>(frames - 1);
    }

    // FUN_0029DAC0. Stage the next pair of blocks into the work block, with the
    // player's own position as the first leg's first control point, and advance
    // the route cursor. The cursor is a plain byte the original never bounds --
    // a fourth carry in one scene takes the first pair again, because the
    // if-chain simply falls through and leaves the initial selection standing.
    void FUN_0029dac0_stage_route(const OriginalEntity &player)
    {
      MastWork &work = DAT_00355db8_work();
      std::size_t first = 0;
      std::size_t second = 1;
      if (work.byte00_routeCursor == 1)
      {
        first = 2;
        second = 3;
      }
      else if (work.byte00_routeCursor == 2)
      {
        first = 4;
        second = 5;
      }

      work.legA[0] = Vec3{player.positionX20, player.positionZ24, player.positionY28};
      work.legA[1] = kDAT_0034eb60_carryPoints[first][1];
      work.legA[2] = kDAT_0034eb60_carryPoints[first][2];
      work.legB = kDAT_0034eb60_carryPoints[second];
      work.byte00_routeCursor = static_cast<std::uint8_t>(work.byte00_routeCursor + 1);
    }

    // The body FUN_0029C198 and FUN_0029D658 share: step the carry timer, swap
    // the player's animation at the halfway mark, pick the camera sub-shot, and
    // write the player's position and facing off the curve.
    //
    // It is spelled twice in the original with different camera priorities and
    // different threshold globals, so it is spelled once here with both passed
    // in.
    void run_carry_frame(OriginalEntity &boss,
                         OriginalEntity &player,
                         std::int32_t timer,
                         float splitA,
                         float splitB,
                         std::int16_t cameraPriority,
                         const ActorEnvironment &environment)
    {
      const MastWork &work = DAT_00355db8_work();
      const auto ticks = static_cast<std::int32_t>(environment.frameTicks);
      const float t = static_cast<float>(timer) / kCarryTicksFloat;
      const float tNext = static_cast<float>(timer + ticks) / kCarryTicksFloat;

      // Animation 12 is the leap, 13 the fall onto the far end. The test is
      // against the *current* animation, so the setter runs once per swap and
      // the clip is not restarted every frame.
      if (t < 0.5f)
      {
        if (player.animationA0 != 0x0C)
        {
          FUN_00225bc8_set_animation(player, 0x0C);
        }
      }
      else if (player.animationA0 != 0x0D)
      {
        FUN_00225bc8_set_animation(player, 0x0D);
      }

      if (t < splitA)
      {
        FUN_00298160_mast_camera(9, 1, cameraPriority, &boss, environment);
      }
      else if (splitB <= t)
      {
        FUN_00298160_mast_camera(9, 3, cameraPriority, &boss, environment);
      }
      else
      {
        FUN_00298160_mast_camera(9, 2, cameraPriority, &boss, environment);
      }

      const Vec3 here = FUN_00266ce8_sample(work.curve, t);
      const Vec3 ahead = FUN_00266ce8_sample(work.curve, tNext);
      player.positionX20 = here.x;
      player.positionZ24 = here.y;
      player.positionY28 = here.z;
      // The tangent, taken one frame ahead rather than from the derivative.
      player.facingRadians5c = std::atan2(ahead.y - player.positionZ24, ahead.x - player.positionX20);
      // DAT_0058BEBA = 0xFFFF: the player is on no map primitive at all while
      // the curve owns them, which is what stops the ground scan arguing.
      player.groundPrimitive0a = -1;
    }

    // FUN_00249388(record, 0x4000, DAT_003253C2). The "record" it is handed is
    // the control block's +0x08, which is the entity, and the store lands at
    // DAT_0031D7A0 + entity[0x95] * 0x3C -- the control block's own +0x2C, the
    // target field. DAT_003253C2 is target-marker **row 0**'s pool slot, and in
    // this fight that is the boss's first body segment, because FUN_0029DE10 is
    // the only thing that ever writes that row.
    //
    // So the handback does not clear the player's target the way the crab's
    // does; it points him back at the creature.
    void FUN_00249388_retarget(const ActorEnvironment &environment)
    {
      if (!environment.DAT_0031d7b0_writePlayerControl || environment.DAT_003253c0_markers == nullptr)
      {
        return;
      }
      const std::int16_t target = environment.DAT_003253c0_markers->entry(0).slot02;
      environment.DAT_0031d7b0_writePlayerControl(
          orphen::ported::battle::control::kTarget2c, 2,
          static_cast<std::uint32_t>(static_cast<std::uint16_t>(target)));
    }

    // FUN_00245978(entity, control): re-record where the player is standing,
    // in tenths, into both copies the control block keeps.
    //
    // **This is what stops him hopping.** Battle state 120, the idle, arms a
    // timer and on expiry measures the character against +0x14/+0x16; more than
    // three tenths of drift and it hands off to state 108, the walk home, which
    // walks back and returns to 120. Every one of this boss's moves teleports
    // the player -- state 8 to DAT_003538DC, state 9 to uGpffff9978, and every
    // carry along a spline -- so without the re-record the recorded mark is
    // wherever FUN_00243F80 first saw him and the pair bounces
    // 120 -> 108 -> 120 for ever, on the spot, with the pad locked out for the
    // three frames of 108 each time round.
    //
    // The same omission produced the same symptom in s14_e001; see
    // FUN_00245978_record_home in original_crab_boss.cpp.
    void FUN_00245978_record_home(const OriginalEntity &player,
                                  const ActorEnvironment &environment)
    {
      if (!environment.DAT_0031d7b0_writePlayerControl)
      {
        return;
      }
      // FUN_0030BD20 truncates toward zero; it is not a round.
      const auto tenths = [](float value) {
        return static_cast<std::uint32_t>(
            static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int32_t>(value * 10.0f))));
      };
      namespace control = orphen::ported::battle::control;
      environment.DAT_0031d7b0_writePlayerControl(control::kPosX14, 2, tenths(player.positionX20));
      environment.DAT_0031d7b0_writePlayerControl(control::kPosY16, 2, tenths(player.positionZ24));
      environment.DAT_0031d7b0_writePlayerControl(control::kPosZ18, 2, tenths(player.positionY28));
      environment.DAT_0031d7b0_writePlayerControl(control::kPosX26, 2, tenths(player.positionX20));
      environment.DAT_0031d7b0_writePlayerControl(control::kPosY28, 2, tenths(player.positionZ24));
      environment.DAT_0031d7b0_writePlayerControl(control::kPosZ2a, 2, tenths(player.positionY28));
    }

    // FUN_0029D658. The in-fight version of the carry, driven by the work
    // block's mode byte rather than by the boss's state, plus the mode-9 tail
    // that hands the player back -- which is the only way either carry ends.
    void FUN_0029d658_player_carry(OriginalEntity &boss, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (!work.allocated)
      {
        return;
      }
      OriginalEntity &player = environment.entityPool->slot(0);
      const std::uint8_t mode = work.byte01_carryMode;

      if (mode != 0)
      {
        // Nothing lands on the player while the curve owns them.
        player.flags06 = static_cast<std::uint16_t>(player.flags06 & 0xFFEFu);
        player.freezeTimerBd = 0;
        player.hitFlagsC2 = 0;
        player.pendingDamageBe = 0;
      }

      if (mode == 9)
      {
        FUN_00298160_mast_camera(-1, 0, 0, &boss, environment);
        if (boss.spawnParam94 == 0x0E)
        {
          // The fight's own release, and all three lines matter: the pending
          // action goes back to 6, FUN_00249388 points the player's record back
          // at marker row 0, and FUN_00245978 re-records his home spot at
          // wherever the carry left him.
          if (environment.DAT_0031d7b0_writePlayerControl)
          {
            environment.DAT_0031d7b0_writePlayerControl(
                orphen::ported::battle::control::kPendingAction0e, 1, 6);
          }
          FUN_00249388_retarget(environment);
          FUN_00245978_record_home(player, environment);
        }
        else
        {
          player.halfword04 = static_cast<std::uint16_t>(player.halfword04 & 0xFFF7u);
        }
        FUN_00225bc8_set_animation(player, 2);
        work.byte01_carryMode = 0;
        work.byte02_carryPhase = 0;
        return;
      }

      if (mode != 1)
      {
        // Mode 2 does nothing at all. Mode 3 parks the player's state machine,
        // turns him to face the boss and drops him back on animation 2.
        if (mode == 3)
        {
          if (environment.DAT_0031d7b0_writePlayerControl)
          {
            environment.DAT_0031d7b0_writePlayerControl(
                orphen::ported::battle::control::kPendingAction0e, 1, 0x0B);
          }
          player.facingRadians5c = FUN_0023a4b8_bearing(player, boss);
          FUN_00225bc8_set_animation(player, 2);
        }
        return;
      }

      // Both are at the top of the mode-1 arm, ahead of the phase check, so the
      // smear is up for the whole carry and not only for the frames that reach
      // the curve. 0x50 alpha, 1.0 degrees of rotation.
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
        environment.DAT_00343878_frameFeedback->set_DAT_00343880_rotation(10);
      }

      if (work.byte02_carryPhase == 0)
      {
        if (work.byte00_routeCursor > 2)
        {
          work.byte01_carryMode = 9;
          work.byte02_carryPhase = 0;
          return;
        }
        // **This write is load-bearing.** It parks the player's own state
        // machine for the length of the carry; without it he keeps running it,
        // and the first thing that does is reset the +0x62 the carry is using
        // as its timer -- so the curve never advances and the boss waits on it
        // for ever.
        if (environment.DAT_0031d7b0_writePlayerControl)
        {
          environment.DAT_0031d7b0_writePlayerControl(
              orphen::ported::battle::control::kPendingAction0e, 1, 0x0B);
        }
        FUN_0029dac0_stage_route(player);
        FUN_00266a78_build(work.curve, work.legA);
        player.fadeRamp62 = 0;
        player.halfword04 = static_cast<std::uint16_t>(player.halfword04 | 0x0008u);
        FUN_00225bc8_set_animation(player, 0x0C);
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kCarryLaunchCue, player);
        }
        work.byte02_carryPhase = 1;
      }

      player.fadeRamp62 = static_cast<std::uint16_t>(player.fadeRamp62 + environment.frameTicks);
      const auto timer = static_cast<std::int32_t>(player.fadeRamp62);
      if (kCarryTicks < timer)
      {
        if (work.byte02_carryPhase == 1)
        {
          player.fadeRamp62 = 0;
          FUN_00266a78_build(work.curve, work.legB);
          work.byte02_carryPhase = 2;
          player.positionX20 = work.legA[2].x;
          player.positionZ24 = work.legA[2].y;
          player.positionY28 = work.legA[2].z;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kCarryLaunchCue, player);
          }
          return;
        }
        if (work.byte02_carryPhase != 2)
        {
          return;
        }
        work.byte01_carryMode = 9;
        work.byte02_carryPhase = 0;
        player.positionX20 = work.legB[2].x;
        player.positionZ24 = work.legB[2].y;
        float landing = player.positionY28;
        if (environment.terrainSurface)
        {
          const auto surface =
              environment.terrainSurface(player.positionX20, player.positionZ24, player.positionY28,
                                         player.height58, player.radius54, player.halfword04,
                                         player.rejectTerrainMask74);
          if (surface.has_value())
          {
            landing = surface->height;
          }
        }
        player.positionY28 = landing;
        player.groundHeight4c = landing;
        player.previousGroundHeight50 = landing;
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kCarryLandCue, player);
        }
        return;
      }

      run_carry_frame(boss, player, timer, kDAT_003539d0_carryShotSplitA,
                      kDAT_003539d4_carryShotSplitB, 1, environment);
    }

    // FUN_0029C198, state 13. Same move, run once, and the one the scene opens
    // with. It is also the only thing that writes script work word 1.
    void FUN_0029c198_state13_intro(OriginalEntity &boss, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      OriginalEntity &player = environment.entityPool->slot(0);

      if (work.byte02_carryPhase == 0)
      {
        FUN_0029dac0_stage_route(player);
        FUN_00266a78_build(work.curve, work.legA);
        player.fadeRamp62 = 0;
        // +0x04 bit 8. The player is off the floor for the whole carry.
        player.halfword04 = static_cast<std::uint16_t>(player.halfword04 | 0x0008u);
        FUN_00225bc8_set_animation(player, 0x0C);
        work.byte02_carryPhase = 1;
        DAT_003555d1_suspendPushOut() = true;
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kCarryLaunchCue, player);
        }
        FUN_0029c510_set_script_cue(false, environment);
      }

      player.fadeRamp62 = static_cast<std::uint16_t>(player.fadeRamp62 + environment.frameTicks);
      const auto timer = static_cast<std::int32_t>(player.fadeRamp62);
      if (kCarryTicks < timer)
      {
        if (work.byte02_carryPhase == 1)
        {
          // Leg one done: rebuild on leg two and snap the player onto leg one's
          // last control point, which is leg two's first.
          player.fadeRamp62 = 0;
          FUN_00266a78_build(work.curve, work.legB);
          work.byte02_carryPhase = 2;
          player.positionX20 = work.legA[2].x;
          player.positionZ24 = work.legA[2].y;
          player.positionY28 = work.legA[2].z;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kCarryLaunchCue, player);
          }
          return;
        }
        if (work.byte02_carryPhase != 2)
        {
          return;
        }

        // The landing. The boss hands itself back to state 1, the player is put
        // down on whatever the ground query answers at the curve's last point,
        // and the script's beat 30 is released.
        work.byte01_carryMode = 9;
        player.positionX20 = work.legB[2].x;
        player.positionZ24 = work.legB[2].y;
        float landing = player.positionY28;
        if (environment.terrainSurface)
        {
          const auto surface =
              environment.terrainSurface(player.positionX20, player.positionZ24, player.positionY28,
                                         player.height58, player.radius54, player.halfword04,
                                         player.rejectTerrainMask74);
          if (surface.has_value())
          {
            landing = surface->height;
          }
        }
        player.positionY28 = landing;
        // FUN_0029C198 writes +0x4C and not +0x50 here; FUN_0029D658's copy
        // writes both. Kept different because they are different.
        player.groundHeight4c = landing;
        FUN_00225bf0_set_state_and_animation(boss, 1, 0);
        DAT_003555d1_suspendPushOut() = false;
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kCarryLandCue, player);
        }
        FUN_0029c510_set_script_cue(true, environment);
        return;
      }

      run_carry_frame(boss, player, timer, kDAT_00353950_shotSplitA, kDAT_00353954_shotSplitB, 0,
                      environment);
    }

    // FUN_00299590, the action map. Three actions and nothing else; anything the
    // battle system sends that is not 12, 13 or 14 is swallowed.
    // FUN_00299590 is called from FUN_002994E0, which the original reaches with
    // only the entity -- the environment it needs for action 14 is a global
    // there. Parked for the length of one wrapper call rather than threaded
    // through two signatures that do not have it in the original.
    const ActorEnvironment *&action14Environment()
    {
      static const ActorEnvironment *value = nullptr;
      return value;
    }

    // Defined with the fight below; action 14 is the one thing in the action
    // path that reaches forward into it.
    void FUN_0029c468_next_move(OriginalEntity &entity, const ActorEnvironment &environment);

    void FUN_00299590_action_map(OriginalEntity &entity, std::uint8_t action)
    {
      if (action == 0x0C)
      {
        entity.spawnParam94 = action;
        entity.state60 = 13;
        return;
      }
      if (action == 0x0D)
      {
        entity.spawnParam94 = action;
        return;
      }
      if (action == 0x0E)
      {
        entity.spawnParam94 = action;
        FUN_0029c468_next_move(entity, *action14Environment());
        return;
      }
    }

    // FUN_002994E0. Returns true for "the state handler still runs but the
    // damage path does not", which is the original's non-zero return.
    bool FUN_002994e0_action_check(OriginalEntity &entity,
                                   ActorEnvironment::BattleActorView &view,
                                   bool haveRecord,
                                   bool &publishView)
    {
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        return true;
      }
      if (!haveRecord)
      {
        return false;
      }
      const std::uint8_t pending = view.pendingAction0e;
      if (pending == 0)
      {
        return false;
      }
      switch (pending)
      {
      case 0x0A:
        view.currentAction0f = 0x0A;
        publishView = true;
        return true;
      case 0x0B:
        view.currentAction0f = 0x0B;
        publishView = true;
        return false;
      case 0x01:
      case 0x02:
      case 0x03:
      case 0x05:
      case 0x06:
      case 0x07:
      case 0x08:
      case 0x0C:
      case 0x0D:
      case 0x0E:
        FUN_00299590_action_map(entity, pending);
        [[fallthrough]];
      default:
        view.pendingAction0e = 0;
        publishView = true;
        return false;
      }
    }

    // FUN_002995E0, state 0.
    void mast_state0(OriginalEntity &entity, std::size_t slot, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();

      entity.scale14c = kMastScale;
      entity.scaleZ150 = kMastScale;

      // FUN_0025BAE8(0, type): group 0 of SCR.BIN 0xBF at type - 0x7C.
      if (environment.uGpffffadf8_stats != nullptr)
      {
        const auto record = environment.uGpffffadf8_stats->FUN_00229688_record(
            0, static_cast<std::int32_t>(entity.typeId00) - 0x7C);
        if (record.has_value())
        {
          entity.radius54 = record->radius0c;
          entity.hitVolumeRadius11c = record->radius0c;
          entity.height58 = record->height10;
          entity.hitVolumeHeight120 = record->height10;
          const auto hitPoints =
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte06));
          entity.staggerTimer12a = static_cast<std::uint16_t>(hitPoints);
          entity.maxHitPoints128 = static_cast<std::uint16_t>(hitPoints);
          entity.attackPower12c = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte07)));
          entity.defence12e = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(static_cast<std::int8_t>(record->byte08)));
        }
      }

      entity.spawnParam94 = 0;
      entity.mastSpawnFacing19c = entity.facingRadians5c;
      // +0x04 |= 0x99, not +0x02. Confirmed against the save state: the slot
      // reads 0x0020 from the descriptor before state 0 and 0x00B9 after.
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x0099u);
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 | 0x21u);
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0080u);

      // State 1 is the no-op, which is where the scene's beat 10 is waiting.
      FUN_00225bf0_set_state_and_animation(entity, 1, 10);

      // FUN_00267E78(work, 0x5B8): the block comes off the arena zeroed.
      work = MastWork{};
      work.allocated = true;
      unportedMoveFrames() = 0;
      unportedMoveState() = 0;

      FUN_00216078_fill_attack_records(static_cast<std::int16_t>(entity.typeId00), work.attacks,
                                       environment);

      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(slot);
      }

      // The nine body segments. FUN_00265E28(0xC0) allocates and initialises
      // from *type 0xC0's* descriptor and state 0 then writes 0xBF over +0x00,
      // so they draw 0xC0's model and dispatch to 0xBF -- which is the shared
      // no-op. Nothing moves them until FUN_0029CCB8 runs, and that needs the
      // mode byte at 14.
      if (environment.entityPool != nullptr && environment.descriptors != nullptr)
      {
        EntityPool &pool = *environment.entityPool;
        for (std::size_t index = 0; index < kMastSegmentCount; ++index)
        {
          const std::size_t segmentSlot =
              pool.FUN_00265e28_allocate_and_initialize(kMastSegmentSpawnTypeId,
                                                        *environment.descriptors);
          if (segmentSlot >= pool.slotCount())
          {
            work.segments[index] = -1;
            continue;
          }
          work.segments[index] = static_cast<std::int32_t>(segmentSlot);
          OriginalEntity &segment = pool.slot(segmentSlot);
          segment.animationA0 = 0;
          segment.battleFlags96 = static_cast<std::uint8_t>(segment.battleFlags96 | 0x21u);
          segment.descriptorFlags02 = static_cast<std::uint16_t>(segment.descriptorFlags02 | 0x0008u);
          segment.maxHitPoints128 = 1;
          segment.staggerTimer12a = 1;
          segment.halfword04 = 9;
          segment.typeId00 = static_cast<std::int16_t>(kMastSegmentTypeId);
          segment.hitVolumeRadius11c = kSegmentRadius;
          segment.radius54 = kSegmentRadius;
          segment.defence12e = entity.defence12e;
          segment.hitVolumeHeight120 = kSegmentHeight;
          segment.height58 = kSegmentHeight;
          segment.previousGroundHeight50 = kSegmentGround;
          segment.groundHeight4c = kSegmentGround;
        }
      }

      FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
      work.byte00_routeCursor = 0;
      work.byte01_carryMode = 0;
      work.byte02_carryPhase = 0;
      DAT_003555d1_suspendPushOut() = false;
      entity.mastByte1af = 0;
      entity.mastFlag1b2 = 0;
    }

    // ======================================================================
    // The fight
    // ======================================================================

    // DAT_00325E28, the move rotation FUN_0029C468 walks: eighteen entries,
    // cycling. Every one of them is state 3, 4, 5, 6, 8 or 9.
    inline constexpr std::array<std::uint16_t, 18> kDAT_00325e28_moveRotation{
        {4, 5, 6, 5, 3, 5, 6, 8, 6, 4, 5, 8, 5, 8, 3, 5, 6, 9}};

    // DAT_0034EB40: the nine body bones the whole tail bends through. The tenth
    // byte in the executable (0x21) is copied onto the stack with them and never
    // read -- the loop runs nine times.
    inline constexpr std::array<std::uint8_t, 9> kDAT_0034eb40_bodyBones{
        {0x13, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20}};

    // DAT_0034EB30: which bone each of the nine segments rides.
    inline constexpr std::array<std::uint8_t, kMastSegmentCount> kDAT_0034eb30_segmentBones{
        {0x08, 0x03, 0x01, 0x12, 0x18, 0x1A, 0x1C, 0x1E, 0x20}};

    // DAT_00325E88: how much of a segment's damage reaches the boss. The head
    // counts for all of it and the tail for three tenths.
    inline constexpr std::array<float, kMastSegmentCount> kDAT_00325e88_segmentDamage{
        {1.0f, 0.8f, 0.7f, 0.6f, 0.5f, 0.3f, 0.3f, 0.3f, 0.3f}};

    // DAT_0034EB20: three groups of three bones, the parts state 5 sheds when a
    // pass connects.
    inline constexpr std::array<std::array<std::uint8_t, 3>, 3> kDAT_0034eb20_shedBones{
        {{{0x01, 0x02, 0x03}}, {{0x02, 0x04, 0x13}}, {{0x04, 0x05, 0x13}}}};

    // DAT_00325D38: three flight lanes, each a pair of (x, z) ends plus a spare
    // the code never reads. FUN_0029C538 rolls a lane and the caller picks an
    // end.
    struct FlightLane
    {
      std::array<float, 2> end[3];
    };
    inline constexpr std::array<FlightLane, 3> kDAT_00325d38_flightLanes{
        {{{{{-3.0f, 20.0f}}, {{-3.0f, -20.0f}}, {{0.0f, 0.0f}}}},
         {{{{3.0f, 20.0f}}, {{3.0f, -20.0f}}, {{0.0f, 0.0f}}}},
         {{{{11.0f, 20.0f}}, {{11.0f, -20.0f}}, {{0.0f, 0.0f}}}}}};

    // Where FUN_0029C538 stands the player, DAT_00353958..0x0035397C: a pair
    // per lane, picked by which way the pass is running.
    inline constexpr float kDAT_00353958_lane1Back = -6.062f;
    inline constexpr float kDAT_0035395c_lane1Fwd = -6.062f;
    inline constexpr float kDAT_00353960_lane2aFwd = 7.189f;
    inline constexpr float kDAT_00353964_lane2aBack = 7.189f;
    inline constexpr float kDAT_00353968_lane2bFwd = 5.869f;
    inline constexpr float kDAT_0035396c_lane2bBack = 5.869f;
    inline constexpr float kDAT_00353970_lane3aBack = -0.742f;
    inline constexpr float kDAT_00353974_lane3aFwd = -0.742f;
    inline constexpr float kDAT_00353978_lane3bBack = 0.777f;
    inline constexpr float kDAT_0035397c_lane3bFwd = 0.777f;

    // The flight states' tuning. States 3 and 4 have separate copies of the same
    // six numbers at 0x0035389C and 0x003538B4; they are kept apart here because
    // the executable keeps them apart.
    inline constexpr float kDAT_0035389c_state3Swing = 1.57079637050629f;
    inline constexpr float kDAT_003538a0_state3SplitA = 0.400000005960464f;
    inline constexpr float kDAT_003538a4_state3SplitB = 0.899999976158142f;
    inline constexpr float kDAT_003538a8_state3Rise = 0.00200000009499490f;
    inline constexpr float kDAT_003538ac_state3Drift = 0.00100000004749745f;
    inline constexpr float kDAT_003538b0_state3Size = 0.0500000007450581f;
    inline constexpr float kDAT_003538b4_state4Swing = 1.57079637050629f;
    inline constexpr float kDAT_003538b8_state4SplitA = 0.400000005960464f;
    inline constexpr float kDAT_003538bc_state4SplitB = 0.899999976158142f;
    inline constexpr float kDAT_003538c0_state4Rise = 0.00200000009499490f;
    inline constexpr float kDAT_003538c4_state4Drift = 0.00100000004749745f;
    inline constexpr float kDAT_003538c8_state4Size = 0.0500000007450581f;
    inline constexpr float kDAT_003538cc_state5SplitA = 0.310000002384186f;
    inline constexpr float kDAT_003538d0_state5SplitB = 0.600000023841858f;
    inline constexpr float kDAT_003538d4_state6SwingOut = 1.57079637050629f;
    inline constexpr float kDAT_003538d8_state6SwingBack = 1.57079637050629f;
    inline constexpr float kDAT_00353898_state2Lead = 1.57079637050629f;
    inline constexpr float kDAT_00353980_bonePitchBias = 1.57079637050629f;
    inline constexpr float kDAT_00353984_pathPitchBias = 1.57079637050629f;

    // The orbit every flight state opens on: twelve of turn per 32000 ticks,
    // twenty units out, five below the water, for 0x1900 ticks.
    inline constexpr float kMastOrbitRate = 12.0f;
    inline constexpr float kMastOrbitRadius = 20.0f;
    inline constexpr float kMastOrbitHeight = -5.0f;
    inline constexpr std::uint16_t kMastOrbitTicks = 0x1900;
    // The map primitives states 3 and 4 uncover while they pass.
    inline constexpr std::uint32_t kMastPassPrimitiveGroup = 0x100;
    // The tint FUN_0029CCB8 holds on the boss while a hit is still flashing.
    inline constexpr std::uint32_t kMastFlashColour = 0x14C8;

    // Cues, all through FUN_00295A60 -> FUN_00267D88.
    inline constexpr std::uint16_t kMastPassCue = 0x12D;
    inline constexpr std::uint16_t kMastDiveCue = 0x130;
    inline constexpr std::uint16_t kMastShedCue = 0x13C;
    inline constexpr std::uint16_t kMastSplashInCue = 0x12F;
    inline constexpr std::uint16_t kMastSplashOutCue = 0x12E;
    inline constexpr std::uint16_t kMastSplashNoOwnerCue = 0x13A;
    inline constexpr std::uint16_t kMastWakeCue = 0x13B;

    // FUN_0029CEE8's and FUN_0029EAE8's own numbers, 0x0035399C..0x003539A8 and
    // 0x003539F0..0x003539F8.
    inline constexpr float kDAT_0035399c_splashShake = 0.200000002980232f;
    inline constexpr float kDAT_003539a0_sprayRise = 0.100000001490116f;
    inline constexpr float kDAT_003539a4_sprayDrift = 0.0299999993294477f;
    inline constexpr float kDAT_003539a8_spraySize = 0.0500000007450581f;
    inline constexpr float kDAT_003539f0_wakeScale = 0.800000011920929f;
    inline constexpr float kDAT_003539f4_wakeZ = 0.100000001490116f;
    inline constexpr float kDAT_003539f8_wakePitch = 1.57079637050629f;

    // The three effect types the fight allocates: the splash ring, the wake and
    // the shed parts.
    inline constexpr std::int32_t kMastSplashTypeId = 0x1AA;
    inline constexpr std::int32_t kMastWakeTypeId = 0x1B1;
    inline constexpr std::int32_t kMastShedTypeId = 0x1E6;
    // FUN_0029D0B0's wingtip, FUN_0029D168's wash and the trail FUN_0029E9B0
    // and FUN_0029E878 share.
    inline constexpr std::int32_t kMastWingtipTypeId = 0x1AD;
    inline constexpr std::int32_t kMastWashTypeId = 0x1AE;
    inline constexpr std::int32_t kMastTrailTypeId = 0x1B0;

    inline constexpr std::uint16_t kMastStrafeCue = 0x133;
    inline constexpr std::uint16_t kMastStrafeEndCue = 0x134;

    // 0x003539AC..0x003539BC, FUN_0029D0B0's five in order.
    inline constexpr float kDAT_003539ac_wingtipScale = 0.100000001490116f;
    inline constexpr float kDAT_003539b0_wingtipFacing = -1.57079637050629f;
    inline constexpr float kDAT_003539b4_wingtipRoll = 1.57079637050629f;
    inline constexpr float kDAT_003539b8_wingtipZ = -0.0399999991059303f;
    inline constexpr float kDAT_003539bc_wingtipY = 0.00999999977648258f;

    // 0x003539E0/E4, the dust one piece of debris leaves; 0x003539E8, how far
    // above the player a shed streak aims; 0x003539EC, the same for the burst.
    inline constexpr float kDAT_003539e0_shardDustSize = 0.600000023841858f;
    inline constexpr float kDAT_003539e4_shardDustJitter = 0.100000001490116f;
    inline constexpr float kDAT_003539e8_shedAimRise = 0.699999988079071f;
    inline constexpr float kFGpffff9a7c_impactRise = 0.699999988079071f;

    // 0x003538DC..0x003538E4 keyed on the route cursor, and the Z that goes
    // with all three: where a strafing run stands the player.
    inline constexpr std::array<float, 3> kDAT_003538dc_strafePlayerX{
        {-6.06200003623962f, 7.18900012969971f, -0.777000010013580f}};
    inline constexpr float kStrafePlayerZ = -0.5f;

    // DAT_00325CF0: three smash runs, each a start and an end. One per mast
    // section, taken in order by +0x1BE.
    inline constexpr std::array<std::array<float, 6>, 3> kDAT_00325cf0_smashRuns{
        {{{-12.3460001945496f, -14.7330000400543f, -5.0f, -8.32599997520447f,
           -2.82200002670288f, 2.25f}},
         {{0.400000005960464f, -18.0f, -5.0f, 0.467000007629395f, -4.24599981307983f, 1.0f}},
         {{0.400000005960464f, 18.0f, -5.0f, 1.93799996376038f, 3.67999982833862f, 1.0f}}}};

    // DAT_00355340: the placement tag of the plank each run breaks. DAT_00355348:
    // the collision-group *bit* that goes with it, which is what comes out of
    // the draw and out of the ground scan.
    inline constexpr std::array<std::uint8_t, 3> kDAT_00355340_smashTags{{0x33, 0x34, 0x35}};
    inline constexpr std::array<std::uint8_t, 3> kDAT_00355348_smashGroups{{0x02, 0x03, 0x04}};

    inline constexpr float kDAT_0035393c_smashFireRise = 0.0199999995529652f;
    inline constexpr float kDAT_00353940_smashCloseShot = 0.699999988079071f;
    inline constexpr float kDAT_00353944_smashRumble = 0.600000023841858f;
    inline constexpr float kDAT_00353948_smashShake = 0.200000002980232f;

    // DAT_00355358: the two debris kinds, biased by 0x272 at the allocate.
    inline constexpr std::array<std::uint8_t, 2> kDAT_00355358_debrisKinds{{0x49, 0x4A}};
    inline constexpr float kDAT_003539d8_debrisTurn = 6.28318405151367f;
    inline constexpr float kDAT_003539dc_debrisGravity = 0.000250000011874363f;

    // The two shakes FUN_0023ABD0's phases ask for, both 0.1.
    inline constexpr float kUGpffff8688_whiteOutShake = 0.100000001490116f;
    inline constexpr float kUGpffff868c_whiteOutShake = 0.100000001490116f;

    // Type 0x1C5, the nine limbs the death throws, at 0x003539DC's neighbour.
    inline constexpr std::int32_t kMastLimbTypeId = 0x1C5;
    inline constexpr float kUGpffff99dc_limbScale = 0.100000001490116f;
    inline constexpr std::uint16_t kMastDeathSinkCue = 0x132;

    // ------------------------------------------------- state 9's own constants
    inline constexpr std::int32_t kMastBlastTypeId = 0x1AF;
    inline constexpr std::int32_t kMastBlastRingTypeId = 0x1AB;

    inline constexpr std::uint16_t kMastTransformCue = 0x135;
    inline constexpr std::uint16_t kMastBeamHitCue = 0x136;
    inline constexpr std::uint16_t kMastBeamLinkCue = 0x137;
    inline constexpr std::uint16_t kMastBlastCue = 0x138;
    inline constexpr std::uint16_t kMastBeamCue = 0x139;

    // 0x003538E8..0x003538F0 keyed on the route cursor -- the *transformation's*
    // spots, which are not the strafing run's: the third is +0.777, not -0.777.
    inline constexpr std::array<float, 3> kUGpffff9978_transformPlayerX{
        {-6.06200003623962f, 7.18900012969971f, 0.777000010013580f}};

    inline constexpr float kUGpffff9984_glowFacing = -1.57079637050629f;
    inline constexpr float kUGpffff9988_glowRise = 0.00999999977648258f;
    inline constexpr float kUGpffff998c_glowPitch = 3.14159250259399f;
    inline constexpr float kFGpffff9990_minScale = 0.300000011920929f;
    inline constexpr float kUGpffff9994_linkShake = 0.100000001490116f;
    inline constexpr float kUGpffff9998_blastPitch = 3.14159250259399f;
    inline constexpr float kFGpffff999c_linkTurn = 6.28318405151367f;
    inline constexpr float kUGpffff99a0_linkEndShake = 0.200000002980232f;
    inline constexpr float kUGpffff99a4_blastShake = 0.100000001490116f;
    inline constexpr float kUGpffff99a8_headShake = 0.200000002980232f;
    inline constexpr float kUGpffff99ac_headFacing = -1.57079637050629f;
    inline constexpr float kUGpffff99b0_headHoldShake = 0.300000011920929f;
    inline constexpr float kUGpffff99b4_beamShake = 0.300000011920929f;
    inline constexpr float kUGpffff99b8_beamStepShake = 0.300000011920929f;
    inline constexpr float kFGpffff99bc_beamTurn = 6.28318405151367f;
    inline constexpr float kUGpffff99c0_beamFireRise = 0.0399999991059303f;
    inline constexpr float kUGpffff99c4_beamEndShake = 0.300000011920929f;
    inline constexpr float kUGpffff99c8_columnRise = 0.0399999991059303f;

    inline constexpr float kDAT_003539c0_blastRingTurn = 6.28318405151367f;
    inline constexpr float kDAT_003539c4_breakRate = 0.00349099002778530f;
    inline constexpr float kDAT_003539c8_breakDone = -1.57079637050629f;
    inline constexpr float kDAT_003539cc_breakShake = 0.300000011920929f;

    // DAT_0034EB50: the four map collision groups each mast section is made of.
    inline constexpr std::array<std::uint8_t, 12> kDAT_0034eb50_breakGroups{
        {0x01, 0x07, 0x08, 0x09, 0x02, 0x0A, 0x0B, 0x0C, 0x03, 0x04, 0x05, 0x06}};
    // DAT_00325D80: where the beam head is planted, as (x, height).
    inline constexpr std::array<std::array<float, 2>, 3> kDAT_00325d80_beamHeads{
        {{{-6.5f, 6.0f}}, {{6.5f, 8.0f}}, {{0.0f, 6.0f}}}};
    // DAT_00355338: the group bit phase 12 takes out of the draw.
    inline constexpr std::array<std::uint8_t, 3> kDAT_00355338_sectionGroups{{0x05, 0x06, 0x07}};

    // DAT_0035534C / DAT_00355350, both file-initialised: the interval between
    // the splashes a breaking section makes, and how high each one sits.
    std::uint16_t &DAT_0035534c_breakTimer()
    {
      static std::uint16_t value = 0x2BC0;
      return value;
    }
    float &DAT_00355350_breakSplashZ()
    {
      static float value = 7.0f;
      return value;
    }

    // FUN_00216690, and FUN_002166E8(a, b) which is the same over `b - a`.
    float wrap_angle(float radians)
    {
      return orphen::ported::model::FUN_00216690_wrap_angle(radians);
    }
    float angle_difference(float from, float to) { return wrap_angle(to - from); }

    // FUN_0029CC28. The bearing between a point and **the mast**, which is the
    // world origin -- the scratch it measures against is a zeroed triple, not an
    // entity. Mode 0 is origin to point, mode 1 is point to origin.
    float FUN_0029cc28_mast_bearing(int mode, float x, float z)
    {
      if (mode == 0)
      {
        return wrap_angle(std::atan2(z, x));
      }
      return wrap_angle(std::atan2(-z, -x));
    }

    orphen::ported::model::EntityBoneOverrides *bone_overrides(const ActorEnvironment &environment,
                                                               std::size_t slot)
    {
      return slot < environment.boneOverrides.size() ? &environment.boneOverrides[slot] : nullptr;
    }

    // FUN_0029CA28. Put the nine body bones back on their animated pose with the
    // driver's two fields zeroed, then raise +0x08 bit 4. Every flight state
    // opens with it.
    void FUN_0029ca28_clear_body_bones(OriginalEntity &entity,
                                       std::size_t slot,
                                       const ActorEnvironment &environment)
    {
      auto *overrides = bone_overrides(environment, slot);
      if (overrides != nullptr && environment.FUN_0020da68_sample_bone_pose)
      {
        for (const std::uint8_t bone : kDAT_0034eb40_bodyBones)
        {
          const auto pose =
              environment.FUN_0020da68_sample_bone_pose(slot, bone, entity.animationA0);
          std::array<float, orphen::ported::model::kPoseFieldCount> fields{};
          if (pose.has_value())
          {
            fields = *pose;
          }
          fields[0] = 0.0f;
          fields[2] = 0.0f;
          orphen::ported::model::FUN_0020d8c0_set_bone_override(*overrides, bone, fields, 1);
        }
      }
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0010u);
    }

    // FUN_0029C7A8, the body bend, and the tail call every flight state ends on.
    //
    // **All nine bones get the same pair of angles.** The rotation compounds
    // down the chain, which is what makes the body arc rather than kink. The
    // pair is the lag between where the head is actually pointing and where the
    // entity says it is facing -- yaw times eight, pitch times ten (times eight
    // in state 10) -- and its sign is flipped by +0x1C1, the same mirror flag
    // that tells states 3 and 4 apart.
    void FUN_0029c7a8_drive_body_bones(OriginalEntity &entity,
                                       std::size_t slot,
                                       const ActorEnvironment &environment)
    {
      if (entity.state60 == 12 || entity.state60 == 8)
      {
        return;
      }
      auto *overrides = bone_overrides(environment, slot);
      if (overrides == nullptr || !environment.FUN_0020da68_sample_bone_pose)
      {
        return;
      }

      float pitchTerm = 0.0f;
      float yawTerm = 0.0f;
      // State 9 skips the measurement and drives the bones with zero: it is
      // mid-transformation and something else owns the body.
      if (static_cast<std::uint16_t>(entity.state60 - 8) > 1 && environment.FUN_0020dc88_bone_point)
      {
        const Vec3 head = environment.FUN_0020dc88_bone_point(slot, 0x26, Vec3{});
        const Vec3 root = environment.FUN_0020dc88_bone_point(slot, 1, Vec3{});
        const float dx = head.x - root.x;
        const float dz = head.y - root.y;
        const float horizontal = std::sqrt(dx * dx + dz * dz);

        const float heading = wrap_angle(std::atan2(dz, dx));
        const float yaw = angle_difference(heading, entity.facingRadians5c);
        entity.mastBoneYaw1b8 = yaw;
        const bool flip = entity.mastMirror1c1 == 0 ? (yaw > 0.0f) : (yaw < 0.0f);
        if (flip)
        {
          entity.mastBoneYaw1b8 = -yaw;
        }

        const float elevation = std::atan2(horizontal, head.z - root.z);
        const float pitchGoal = wrap_angle(elevation - kDAT_00353980_bonePitchBias);
        const float pitch = angle_difference(pitchGoal, entity.rotationX154);
        entity.mastBonePitch1b4 = pitch;
        if (pitch > 0.0f)
        {
          entity.mastBonePitch1b4 = -pitch;
        }

        yawTerm = entity.mastBoneYaw1b8 * 8.0f;
        pitchTerm =
            entity.state60 == 10 ? entity.mastBonePitch1b4 * 8.0f : entity.mastBonePitch1b4 * 10.0f;
      }

      for (const std::uint8_t bone : kDAT_0034eb40_bodyBones)
      {
        const auto pose = environment.FUN_0020da68_sample_bone_pose(slot, bone, entity.animationA0);
        std::array<float, orphen::ported::model::kPoseFieldCount> fields{};
        if (pose.has_value())
        {
          fields = *pose;
        }
        fields[0] = pitchTerm;
        fields[2] = yawTerm;
        orphen::ported::model::FUN_0020d8c0_set_bone_override(*overrides, bone, fields, 1);
      }
    }

    // FUN_0029CCB8, the head of the wrapper's mode-14 block: the nine segments
    // ride the boss's bones, and whatever damage one of them took is folded back
    // onto the boss scaled by which one it was.
    void FUN_0029ccb8_carry_segments(OriginalEntity &entity,
                                     std::size_t slot,
                                     const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();

      if (entity.mastFlashTimer1bc == 0)
      {
        entity.fadeColor138 = 0;
      }
      else
      {
        const std::int32_t remaining = static_cast<std::int32_t>(entity.mastFlashTimer1bc) -
                                       static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
        entity.mastFlashTimer1bc = static_cast<std::uint16_t>(remaining);
        if (static_cast<std::int16_t>(remaining) < 0)
        {
          entity.mastFlashTimer1bc = 0;
        }
        entity.fadeColor138 = kMastFlashColour;
      }

      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;

      for (std::size_t index = 0; index < kMastSegmentCount; ++index)
      {
        const std::int32_t segment = work.segments[index];
        if (segment < 0 || !environment.FUN_0020dc88_bone_point)
        {
          continue;
        }
        const Vec3 point =
            environment.FUN_0020dc88_bone_point(slot, kDAT_0034eb30_segmentBones[index], Vec3{});
        OriginalEntity &body = pool.slot(static_cast<std::size_t>(segment));
        body.positionX20 = point.x;
        body.positionZ24 = point.y;
        body.positionY28 = point.z;
      }

      // The damage fold. The loop stops at the first segment that is both out of
      // its freeze and carrying a hit, and only that one is folded.
      for (std::size_t index = 0; index < kMastSegmentCount; ++index)
      {
        const std::int32_t segment = work.segments[index];
        if (segment < 0)
        {
          continue;
        }
        OriginalEntity &body = pool.slot(static_cast<std::size_t>(segment));
        const std::int8_t freeze = body.freezeTimerBd;
        bool ready = freeze == 0;
        if (freeze != 0)
        {
          body.freezeTimerBd = static_cast<std::int8_t>(freeze - 1);
          ready = freeze == 1;
        }
        if (!ready || body.pendingDamageBe == 0)
        {
          continue;
        }
        entity.freezeTimerBd = body.freezeTimerBd;
        const float scaled = static_cast<float>(static_cast<std::int16_t>(body.pendingDamageBe)) *
                             kDAT_00325e88_segmentDamage[index];
        entity.pendingDamageBe = static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled));
        entity.hitFlagsC2 = body.hitFlagsC2;
        body.pendingDamageBe = 0;
        body.hitFlagsC2 = 0;
        break;
      }
    }

    // FUN_0029C468, the move selector. Action 14 lands here, and so does every
    // move that runs out: read the next entry of the rotation, enter it, and
    // roll the side the next camera shot comes in on.
    void FUN_0029c468_next_move(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      std::uint16_t next = kDAT_00325e28_moveRotation[entity.mastMoveCursor1ae %
                                                      kDAT_00325e28_moveRotation.size()];
      if (environment.entityPool != nullptr &&
          static_cast<std::int16_t>(environment.entityPool->slot(0).staggerTimer12a) < 1)
      {
        // The player is down: circle and wait rather than take a turn.
        next = 2;
      }
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        next = 12;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x0010u);
      }
      FUN_00225bf0_set_state_and_animation(entity, next, 0);
      const std::uint8_t cursor = static_cast<std::uint8_t>(entity.mastMoveCursor1ae + 1);
      entity.mastMoveCursor1ae = cursor > 0x11 ? 0 : cursor;
      // DAT_0035532C, the side the next shot comes in on: 1 or 2.
      DAT_0035532c_mastCameraSide() =
          static_cast<std::uint8_t>((environment.random ? (environment.random() & 1u) : 0u) + 1u);
    }

    // The orbit opening states 2, 3, 4 and 6 share, straight out of their four
    // identical first blocks.
    void mast_begin_orbit(OriginalEntity &entity,
                          std::size_t slot,
                          std::uint16_t animation,
                          const ActorEnvironment &environment)
    {
      const float bearing = FUN_0029cc28_mast_bearing(0, entity.positionX20, entity.positionZ24);
      entity.rotationX154 = 0.0f;
      entity.mastTurnRate1a0 = kMastOrbitRate;
      entity.mastOrbitRadius1a8 = kMastOrbitRadius;
      entity.mastOrbitAngle1a4 = bearing;
      entity.mastMoveTicks1ac = kMastOrbitTicks;
      entity.fadeRamp62 = kMastOrbitTicks;
      entity.positionX20 = entity.mastOrbitRadius1a8 * std::cos(bearing);
      entity.positionZ24 = entity.mastOrbitRadius1a8 * std::sin(bearing);
      entity.positionY28 = kMastOrbitHeight;
      FUN_00225bc8_set_animation(entity, animation);
      FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
      FUN_0029ca28_clear_body_bones(entity, slot, environment);
    }

    // DAT_00354CA8 and DAT_00354CB8. Two of the eight per-frame ambient emitter
    // gates FUN_00219368 walks, all of them keyed on the player. States 3 and 4
    // drop both for the close half of a pass and put them back either side of
    // it. The port has no emitter behind either gate, so they are kept and
    // reported rather than acted on -- writing them into the rain or haze pools
    // would be a guess about which of the eight they are.
    std::uint8_t &DAT_00354ca8_ambientGateA()
    {
      static std::uint8_t value = 0;
      return value;
    }
    std::uint8_t &DAT_00354cb8_ambientGateB()
    {
      static std::uint8_t value = 0;
      return value;
    }

    // FUN_00295A60(cue, entity) -> FUN_00267D88(cue, entity, -1). A null entity
    // means the player.
    //
    // **The -1 is the whole point of this wrapper.** FUN_00267A80 reads a
    // negative volume as "use fGpffff8D9C for the distance instead of the real
    // one", which is 0.3 -- so every cue the creature makes keys on at 125 of
    // 128 wherever it is, and the fourteen-unit cutoff never applies. The fight
    // orbits at twenty units out, so routing these through FUN_00267D38's
    // fixed 100 instead leaves the roars faint at best and silent at worst.
    void FUN_00295a60_cue(std::uint16_t cue,
                          const OriginalEntity *at,
                          const ActorEnvironment &environment)
    {
      if (!environment.FUN_00267d88_playSoundScaled || environment.entityPool == nullptr)
      {
        return;
      }
      environment.FUN_00267d88_playSoundScaled(
          cue, at != nullptr ? *at : environment.entityPool->slot(0), -1);
    }

    // FUN_0029CEE8, the splash: two rings on the water line, a shake, a cue and
    // -- when something owns it -- a burst of spray. `owner` null is the
    // player's own splash, which gets the two rings marked camera-relative and
    // no spray at all.
    void FUN_0029cee8_splash(float scale,
                             OriginalEntity *owner,
                             float x,
                             float z,
                             bool entering,
                             const ActorEnvironment &environment)
    {
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
        environment.DAT_00343878_frameFeedback->set_DAT_00343880_rotation(0x1E);
      }
      if (environment.FUN_0022dcf0_shake_camera)
      {
        environment.FUN_0022dcf0_shake_camera(kDAT_0035399c_splashShake, 200);
      }
      if (owner == nullptr)
      {
        if (scale > 4.0f)
        {
          FUN_00295a60_cue(kMastSplashNoOwnerCue, nullptr, environment);
        }
      }
      else
      {
        FUN_00295a60_cue(entering ? kMastSplashInCue : kMastSplashOutCue, owner, environment);
      }

      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const float water = environment.DAT_003556fc_effectGroundZ;
      for (std::uint16_t ring = 0; ring < 2; ++ring)
      {
        const std::size_t slot =
            pool.FUN_00265e28_allocate_and_initialize(kMastSplashTypeId, *environment.descriptors);
        if (slot >= pool.slotCount())
        {
          continue;
        }
        OriginalEntity &effect = pool.slot(slot);
        FUN_00225bc8_set_animation(effect, ring);
        effect.scale14c = scale;
        effect.scaleZ150 = scale;
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 0x0080u);
        effect.positionX20 = x;
        effect.positionZ24 = z;
        effect.positionY28 = water;
        effect.groundHeight4c = water;
        effect.previousGroundHeight50 = water;
        if (owner == nullptr)
        {
          // The `| 0xC0` is applied to the *pre-OR* value both times, so the
          // 0x80 above is not carried into it -- transcribed, not simplified.
          effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 0x00C0u);
        }
      }

      if (owner == nullptr)
      {
        return;
      }
      if (environment.FUN_0021ed50_spawn_fountain)
      {
        environment.FUN_0021ed50_spawn_fountain(kDAT_003539a0_sprayRise, kDAT_003539a0_sprayRise,
                                                kDAT_003539a4_sprayDrift, 4.0f, 10.0f,
                                                kDAT_003539a8_spraySize, x, z, water, 200, 400, 0,
                                                0, 0xFFFFFFu);
      }
    }

    // FUN_0029EAE8, the wake: one type 0x1B1 riding bone 0x26 of the boss.
    void FUN_0029eae8_spawn_wake(OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      if (entity.mastChild1c4 >= 0)
      {
        pool.releaseSlot(static_cast<std::size_t>(entity.mastChild1c4));
        entity.mastChild1c4 = -1;
      }
      const std::size_t child =
          pool.FUN_00265e28_allocate_and_initialize(kMastWakeTypeId, *environment.descriptors);
      if (child >= pool.slotCount())
      {
        return;
      }
      entity.mastChild1c4 = static_cast<std::int32_t>(child);
      OriginalEntity &wake = pool.slot(child);
      FUN_00225bc8_set_animation(wake, 2);
      wake.halfword04 = 0x0100;
      wake.rotationX154 = kDAT_003539f8_wakePitch;
      wake.halfword08 = static_cast<std::uint16_t>(wake.halfword08 | 0x0080u);
      wake.groundHeight4c = -56.0f;
      wake.previousGroundHeight50 = -56.0f;
      wake.positionZ24 = kDAT_003539f4_wakeZ;
      wake.scale14c = kDAT_003539f0_wakeScale;
      wake.scaleZ150 = kDAT_003539f0_wakeScale;
      wake.parentSlot192 = static_cast<std::int16_t>(slot);
      wake.attachBone194 = 0x26;
      wake.positionX20 = 0.0f;
      wake.positionY28 = 0.0f;
    }

    // FUN_0029E4F8, one shed part. Ten of them live in the work block at
    // +0x590, which is the last thing in its 0x5B8 bytes.
    void FUN_0029e4f8_shed_part(OriginalEntity &entity,
                                std::size_t slot,
                                std::uint8_t bone,
                                std::uint16_t state,
                                const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      for (std::size_t index = 0; index < work.shedParts.size(); ++index)
      {
        if (work.shedParts[index] >= 0)
        {
          continue;
        }
        const std::size_t child =
            pool.FUN_00265e28_allocate_and_initialize(kMastShedTypeId, *environment.descriptors);
        if (child >= pool.slotCount())
        {
          return;
        }
        work.shedParts[index] = static_cast<std::int32_t>(child);
        OriginalEntity &part = pool.slot(child);
        part.mastShedBone198 = bone;
        part.enemyTargetSlot1a0 = static_cast<std::int32_t>(slot);
        const std::uint32_t roll = environment.random ? environment.random() : 0u;
        part.battleDesiredFacing19c = static_cast<float>(roll % 5u) / 100.0f;
        const std::uint32_t clip = environment.random ? environment.random() : 0u;
        FUN_00225bc8_set_animation(part, static_cast<std::uint16_t>(clip % 3u));
        part.halfword04 = 0x0019;
        part.halfword08 = static_cast<std::uint16_t>(part.halfword08 | 0x00C0u);
        part.groundHeight4c = entity.groundHeight4c;
        part.previousGroundHeight50 = entity.groundHeight4c;
        // :36 -- 300 decimal is +0x12C, the *attack power*, which is what the
        // burst FUN_0029E878 leaves on the player carries into its hit test.
        part.attackPower12c = entity.attackPower12c;
        part.state60 = static_cast<std::uint16_t>(state & 0xFFu);
        part.fadeRamp62 = 0x0780;
        return;
      }
    }

    // FUN_0029C538. Stage the dive's three control points out of a randomly
    // chosen lane, and stand the player on the spot that lane and direction
    // call for. The spot table is keyed on the *route cursor*, the same byte
    // FUN_0029DAC0 uses to pick the intro's splines.
    void FUN_0029c538_stage_dive(OriginalEntity &entity,
                                 std::uint32_t direction,
                                 const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      OriginalEntity &player = environment.entityPool->slot(0);

      const std::uint32_t lane = (environment.random ? environment.random() : 0u) % 3u;
      const auto &ends = kDAT_00325d38_flightLanes[lane].end;
      const std::uint32_t pick = direction & 0xFFu;

      work.divePoints[0] = Vec3{ends[pick][0], ends[pick][1], entity.positionY28};
      const std::uint32_t rise = (environment.random ? environment.random() : 0u) % 7u;
      work.divePoints[1] =
          Vec3{ends[0][0], 0.0f, player.positionY28 + static_cast<float>(rise) + 1.0f};
      work.divePoints[2] = Vec3{ends[pick ^ 1u][0], ends[pick ^ 1u][1] * 2.0f, -30.0f};

      // Which side of the mast the pass ends on decides which of the pair the
      // player is stood on; the sense of the test is different per cursor, and
      // it is different in the executable too.
      const float endZ = work.divePoints[2].y;
      const std::uint8_t cursor = work.byte00_routeCursor;
      bool placed = false;
      if (cursor == 1)
      {
        player.positionX20 = kDAT_0035395c_lane1Fwd;
        if (endZ < 0.0f)
        {
          player.positionZ24 = 0.5f;
          player.positionX20 = kDAT_00353958_lane1Back;
          placed = true;
        }
      }
      else if (cursor == 2)
      {
        if (lane == 2)
        {
          player.positionX20 = kDAT_00353960_lane2aFwd;
          if (endZ >= 0.0f)
          {
            player.positionZ24 = 0.5f;
            player.positionX20 = kDAT_00353964_lane2aBack;
            placed = true;
          }
        }
        else
        {
          player.positionX20 = kDAT_00353968_lane2bFwd;
          if (endZ >= 0.0f)
          {
            player.positionZ24 = 0.5f;
            player.positionX20 = kDAT_0035396c_lane2bBack;
            placed = true;
          }
        }
      }
      else if (cursor == 3)
      {
        if (lane == 0)
        {
          player.positionX20 = kDAT_00353974_lane3aFwd;
          if (endZ < 0.0f)
          {
            player.positionZ24 = 0.5f;
            player.positionX20 = kDAT_00353970_lane3aBack;
            placed = true;
          }
        }
        else
        {
          player.positionX20 = kDAT_0035397c_lane3bFwd;
          if (endZ < 0.0f)
          {
            player.positionZ24 = 0.5f;
            player.positionX20 = kDAT_00353978_lane3bBack;
            placed = true;
          }
        }
      }
      else
      {
        // A cursor outside 1..3 leaves the player exactly where they are, and
        // still re-samples their ground. That is the original's fall-through.
        placed = true;
      }
      if (!placed)
      {
        player.positionZ24 = -0.5f;
      }

      if (environment.terrainSurface)
      {
        const auto surface =
            environment.terrainSurface(player.positionX20, player.positionZ24, player.positionY28,
                                       player.height58, player.radius54, player.halfword04,
                                       player.rejectTerrainMask74);
        if (surface.has_value())
        {
          player.groundHeight4c = surface->height;
        }
      }
    }

    // FUN_0029CAF8. Put the boss on its dive curve and aim it down the tangent,
    // and hand the caller back how far through the move it is.
    float FUN_0029caf8_follow_dive(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const MastWork &work = DAT_00355db8_work();
      const float span = static_cast<float>(static_cast<std::int16_t>(entity.mastMoveTicks1ac));
      const float now = static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) / span;
      const float next =
          (static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) +
           static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
          span;

      const Vec3 ahead = FUN_00266ce8_sample(work.diveCurve, next);
      const Vec3 here = FUN_00266ce8_sample(work.diveCurve, now);
      entity.positionX20 = here.x;
      entity.positionZ24 = here.y;
      entity.positionY28 = here.z;

      const float dx = ahead.x - entity.positionX20;
      const float dz = ahead.y - entity.positionZ24;
      const float dy = ahead.z - entity.positionY28;
      entity.facingRadians5c = std::atan2(dz, dx);
      entity.rotationX154 =
          std::atan2(std::sqrt(dx * dx + dz * dz), dy) - kDAT_00353984_pathPitchBias;
      return now;
    }

    // FUN_00299870, state 2. **The hold while the player is down**, and the only
    // state with no exit -- nothing calls FUN_0029C468 from it, so once the
    // fight has stopped the boss simply circles the mast for good.
    void mast_state2_hold(OriginalEntity &entity,
                          std::size_t slot,
                          const ActorEnvironment &environment)
    {
      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;
      if (entity.animationA0 == 0)
      {
        const float bearing = FUN_0029cc28_mast_bearing(0, entity.positionX20, entity.positionZ24);
        entity.rotationX154 = 0.0f;
        entity.mastTurnRate1a0 = kMastOrbitRate;
        entity.mastOrbitRadius1a8 = kMastOrbitRadius;
        entity.mastMirror1c1 = 1;
        entity.mastOrbitAngle1a4 = bearing;
        entity.positionX20 = entity.mastOrbitRadius1a8 * std::cos(bearing);
        entity.positionY28 = kMastOrbitHeight;
        entity.positionZ24 = entity.mastOrbitRadius1a8 * std::sin(bearing);
        FUN_00225bc8_set_animation(entity, 10);
        // FUN_00217E18(1) as well as the release: this state drops the manual
        // camera outright and lets the field camera have it back.
        if (environment.camera != nullptr)
        {
          environment.camera->FUN_00217e18_release_manual_camera(true);
        }
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
      }

      const auto ticks = static_cast<float>(static_cast<std::int32_t>(environment.frameTicks));
      entity.facingRadians5c = entity.mastOrbitAngle1a4 - kDAT_00353898_state2Lead;
      const float stepped =
          entity.mastOrbitAngle1a4 - (entity.mastTurnRate1a0 * ticks) / 32000.0f;
      entity.mastOrbitAngle1a4 = wrap_angle(stepped);
      entity.positionX20 = entity.mastOrbitRadius1a8 * std::cos(entity.mastOrbitAngle1a4);
      entity.positionZ24 = entity.mastOrbitRadius1a8 * std::sin(entity.mastOrbitAngle1a4);
    }

    // FUN_002999B0 (state 3) and FUN_00299C98 (state 4), which are **the same
    // function mirrored**. State 4 has no `src/` file; it was read out of
    // SLUS_200.11 at 0x00299C98 and it differs from state 3 in exactly three
    // places: +0x1C1 is 1 rather than 0, the orbit turns the other way, and the
    // close shot comes in on camera sub-shot 1 rather than 2. Its five tuning
    // constants at 0x003538B4 hold the same values as state 3's at 0x0035389C,
    // and they are kept apart here because the executable keeps them apart.
    //
    // The move: swing round the mast for 0x1900 ticks at twenty units, pulling
    // in by up to six as the pass comes abeam, and drop the two ambient emitter
    // gates for the middle of it.
    void mast_orbit_pass(OriginalEntity &entity,
                         std::size_t slot,
                         bool mirrored,
                         const ActorEnvironment &environment)
    {
      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;
      if (entity.animationA0 == 0)
      {
        entity.mastMirror1c1 = mirrored ? 1 : 0;
        mast_begin_orbit(entity, slot, 10, environment);
        entity.mastCueLatch1c2 = 0;
        if (environment.FUN_0022dbc8_show_map_primitives)
        {
          environment.FUN_0022dbc8_show_map_primitives(kMastPassPrimitiveGroup, true);
        }
      }

      const auto ticksInt = static_cast<std::int32_t>(environment.frameTicks);
      const std::int32_t remaining = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(remaining) < 0)
      {
        if (environment.FUN_0022dbc8_show_map_primitives)
        {
          environment.FUN_0022dbc8_show_map_primitives(kMastPassPrimitiveGroup, false);
        }
        FUN_0029c468_next_move(entity, environment);
        return;
      }

      const float swing = mirrored ? kDAT_003538b4_state4Swing : kDAT_0035389c_state3Swing;
      const float splitA = mirrored ? kDAT_003538b8_state4SplitA : kDAT_003538a0_state3SplitA;
      const float splitB = mirrored ? kDAT_003538bc_state4SplitB : kDAT_003538a4_state3SplitB;
      const float rise = mirrored ? kDAT_003538c0_state4Rise : kDAT_003538a8_state3Rise;
      const float drift = mirrored ? kDAT_003538c4_state4Drift : kDAT_003538ac_state3Drift;
      const float size = mirrored ? kDAT_003538c8_state4Size : kDAT_003538b0_state3Size;
      const float sign = mirrored ? -1.0f : 1.0f;
      const auto ticks = static_cast<float>(ticksInt);

      entity.facingRadians5c = entity.mastOrbitAngle1a4 + sign * swing;
      const float stepped =
          entity.mastOrbitAngle1a4 + sign * ((entity.mastTurnRate1a0 * ticks) / 32000.0f);
      entity.mastOrbitAngle1a4 = wrap_angle(stepped);

      // **Both directions add the swing here**, even though the facing above
      // subtracts it in the mirrored case. The radius pull-in is symmetric; the
      // direction of travel is not.
      float pull = std::sin(entity.mastOrbitAngle1a4 + swing);
      if (pull <= 0.0f)
      {
        pull = -pull;
      }
      const float radius = entity.mastOrbitRadius1a8 - pull * 6.0f;
      entity.positionX20 = radius * std::cos(entity.mastOrbitAngle1a4);
      entity.positionZ24 = radius * std::sin(entity.mastOrbitAngle1a4);

      const float through = static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) /
                            static_cast<float>(static_cast<std::int16_t>(entity.mastMoveTicks1ac));
      if (through < splitA || splitB <= through)
      {
        DAT_00354cb8_ambientGateB() = 1;
        DAT_00354ca8_ambientGateA() = 1;
        FUN_00298160_mast_camera(1, static_cast<std::int16_t>(DAT_0035532c_mastCameraSide()), 0,
                                 &entity, environment);
      }
      else
      {
        DAT_00354ca8_ambientGateA() = 0;
        DAT_00354cb8_ambientGateB() = 0;
        if (entity.mastCueLatch1c2 == 0)
        {
          FUN_00295a60_cue(kMastPassCue, &entity, environment);
          entity.mastCueLatch1c2 = 1;
        }
        if (environment.DAT_00343878_frameFeedback != nullptr)
        {
          environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
        }
        FUN_00298160_mast_camera(11, mirrored ? 1 : 2, 0, &entity, environment);
        if (environment.FUN_0021ed50_spawn_fountain)
        {
          environment.FUN_0021ed50_spawn_fountain(rise, rise, drift, 5.0f, 3.0f, size,
                                                  entity.positionX20, entity.positionZ24,
                                                  entity.positionY28, 3, 100, 0, 0, 0xFF46u);
        }
      }

      FUN_0029c7a8_drive_body_bones(entity, slot, environment);
    }

    // FUN_0029A2C0, state 6. The same orbit with the pass's camera and cue taken
    // out and a steady descent added: it is how the boss gets back down to the
    // water between moves, and the direction it turns is whatever the last
    // mirrored pass left in +0x1C1.
    void mast_state6_descend(OriginalEntity &entity,
                             std::size_t slot,
                             const ActorEnvironment &environment)
    {
      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;
      if (entity.animationA0 == 0)
      {
        mast_begin_orbit(entity, slot, 10, environment);
      }

      const auto ticksInt = static_cast<std::int32_t>(environment.frameTicks);
      const std::int32_t remaining = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
      entity.fadeRamp62 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(remaining) < 0)
      {
        FUN_0029c468_next_move(entity, environment);
        return;
      }

      const auto ticks = static_cast<float>(ticksInt);
      const bool mirrored = entity.mastMirror1c1 != 0;
      const float swing = mirrored ? kDAT_003538d8_state6SwingBack : kDAT_003538d4_state6SwingOut;
      const float sign = mirrored ? -1.0f : 1.0f;

      entity.facingRadians5c = entity.mastOrbitAngle1a4 + sign * swing;
      const float stepped =
          entity.mastOrbitAngle1a4 + sign * ((entity.mastTurnRate1a0 * ticks) / 32000.0f);
      entity.mastOrbitAngle1a4 = wrap_angle(stepped);

      float pull = std::sin(entity.mastOrbitAngle1a4 + swing);
      if (pull <= 0.0f)
      {
        pull = -pull;
      }
      const float radius = entity.mastOrbitRadius1a8 - pull * 6.0f;
      entity.positionX20 = radius * std::cos(entity.mastOrbitAngle1a4);
      entity.positionZ24 = radius * std::sin(entity.mastOrbitAngle1a4);
      entity.positionY28 -= (entity.mastTurnRate1a0 * ticks) / 32000.0f;

      FUN_00298160_mast_camera(5, static_cast<std::int16_t>(DAT_0035532c_mastCameraSide()), 0,
                               &entity, environment);
      FUN_0029c7a8_drive_body_bones(entity, slot, environment);
    }

    // FUN_00299F80, state 5 -- **the pass**, and seven of the rotation's
    // eighteen entries. A three-point curve down one of three lanes, the player
    // dropped on a spot that lane picks, a splash each time the body crosses the
    // water line, and a wake that sheds three parts when its clip ends.
    void mast_state5_dive(OriginalEntity &entity,
                          std::size_t slot,
                          const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      entity.mastFlag1b1 = 1;
      entity.mastFlag1b2 = 1;

      if (entity.animationA0 == 0)
      {
        entity.mastPhase1b0 = 0;
        entity.mastInWater1c0 = 0;
        entity.fadeRamp62 = 0;
        entity.mastMoveTicks1ac = 16000;
        entity.mastOrbitRadius1a8 = kMastOrbitRadius;
        FUN_00225bc8_set_animation(entity, 0x0E);
        const std::uint32_t direction = (environment.random ? environment.random() : 0u) & 1u;
        FUN_0029c538_stage_dive(entity, direction, environment);
        FUN_00266a78_build(work.diveCurve, work.divePoints);
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        entity.mastCueLatch1c2 = 0;
        const std::uint32_t jitter = (environment.random ? environment.random() : 0u) % 0x78u;
        entity.mastSubTimer1c8 = static_cast<std::uint16_t>(jitter << 5);
      }

      const auto ticksInt = static_cast<std::int32_t>(environment.frameTicks);
      const std::int32_t elapsed = static_cast<std::int32_t>(entity.fadeRamp62) + ticksInt;
      entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
      if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < static_cast<std::int16_t>(elapsed))
      {
        if (entity.mastChild1c4 >= 0 && environment.entityPool != nullptr)
        {
          environment.entityPool->releaseSlot(static_cast<std::size_t>(entity.mastChild1c4));
          entity.mastChild1c4 = -1;
        }
        FUN_0029c468_next_move(entity, environment);
        return;
      }

      const float through = FUN_0029caf8_follow_dive(entity, environment);

      if (through > 0.5f && entity.mastCueLatch1c2 == 0)
      {
        if (environment.random && (environment.random() & 1u) != 0u)
        {
          FUN_00295a60_cue(kMastDiveCue, &entity, environment);
        }
        entity.mastCueLatch1c2 = 1;
      }

      // The wake only starts inside the middle band of the pass, and only once
      // its own jittered countdown runs out -- which is what staggers it against
      // the splash rather than firing both on the same frame.
      if (through > kDAT_003538cc_state5SplitA && through < kDAT_003538d0_state5SplitB)
      {
        const std::int32_t sub = static_cast<std::int32_t>(entity.mastSubTimer1c8) - ticksInt;
        entity.mastSubTimer1c8 = static_cast<std::uint16_t>(sub);
        if (static_cast<std::int16_t>(sub) < 0)
        {
          FUN_0029eae8_spawn_wake(entity, slot, environment);
          FUN_00295a60_cue(kMastWakeCue, &entity, environment);
          entity.mastSubTimer1c8 = 0x4B00;
        }
      }

      if (entity.mastChild1c4 >= 0 && environment.entityPool != nullptr)
      {
        EntityPool &pool = *environment.entityPool;
        const OriginalEntity &wake = pool.slot(static_cast<std::size_t>(entity.mastChild1c4));
        if ((wake.flags06 & 0x0001u) != 0)
        {
          const std::uint32_t group = (environment.random ? environment.random() : 0u) % 3u;
          const auto &bones = kDAT_0034eb20_shedBones[group];
          FUN_0029e4f8_shed_part(entity, slot, bones[0], 1, environment);
          FUN_0029e4f8_shed_part(entity, slot, bones[1], 0, environment);
          FUN_0029e4f8_shed_part(entity, slot, bones[2], 0, environment);
          FUN_00295a60_cue(kMastShedCue, &entity, environment);
          if (entity.mastChild1c4 >= 0)
          {
            pool.releaseSlot(static_cast<std::size_t>(entity.mastChild1c4));
            entity.mastChild1c4 = -1;
          }
        }
      }

      const float water = environment.DAT_003556fc_effectGroundZ;
      if (entity.mastInWater1c0 == 0)
      {
        if (entity.positionY28 >= water)
        {
          FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, true,
                              environment);
          entity.mastInWater1c0 = 1;
        }
      }
      else if (entity.positionY28 <= water)
      {
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, false,
                            environment);
        entity.mastInWater1c0 = 0;
      }

      // Which side the pass ends on decides which way the camera watches it.
      FUN_00298160_mast_camera(2, work.divePoints[2].y > 0.0f ? 2 : 1, 0, &entity, environment);
      FUN_0029c7a8_drive_body_bones(entity, slot, environment);
    }
    // ------------------------------------------------------- the mode-14 pools
    //
    // Three of the four helpers the wrapper runs once the mode byte reaches 14.
    // None of them is about the boss's own movement: between them they are what
    // makes it targetable, what carries the debris a smash throws, and what
    // steps the streaks a pass sheds. The fourth, FUN_0029CCB8, is the segment
    // carry and was already here.

    // FUN_0023A4B8 with pool slot 0 as the second argument: the plain bearing
    // from one entity to another, no wrap. Its own file is two lines.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_0029D0B0. The wingtip spray a strafing run drags: a type 0x1AD parked
    // on the boss's bone 9 through +0x192/+0x194, which is why nothing ever
    // moves it afterwards. Returns the pool slot, or -1.
    std::int32_t FUN_0029d0b0_spawn_wingtip(const OriginalEntity &entity,
                                            std::size_t slot,
                                            const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return -1;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t child =
          pool.FUN_00265e28_allocate_and_initialize(kMastWingtipTypeId, *environment.descriptors);
      if (child >= pool.slotCount())
      {
        return -1;
      }
      OriginalEntity &tip = pool.slot(child);
      FUN_00225bc8_set_animation(tip, 0);
      tip.scale14c = kDAT_003539ac_wingtipScale;
      tip.scaleZ150 = kDAT_003539ac_wingtipScale;
      tip.halfword08 = static_cast<std::uint16_t>(tip.halfword08 | 0x0080u);
      tip.facingRadians5c = kDAT_003539b0_wingtipFacing;
      tip.rotationY158 = kDAT_003539b4_wingtipRoll;
      tip.positionX20 = 0.0f;
      tip.positionZ24 = kDAT_003539b8_wingtipZ;
      tip.positionY28 = kDAT_003539bc_wingtipY;
      tip.parentSlot192 = static_cast<std::int16_t>(slot);
      tip.attachBone194 = 9;
      (void)entity;
      return static_cast<std::int32_t>(child);
    }

    // FUN_0029D168. The wash the run leaves behind it: a type 0x1AE stood once
    // on bone 9 and then left alone, carrying attack record 0 out of the work
    // block so that swimming through it hurts.
    void FUN_0029d168_spawn_wash(const OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t child =
          pool.FUN_00265e28_allocate_and_initialize(kMastWashTypeId, *environment.descriptors);
      if (child >= pool.slotCount())
      {
        return;
      }
      OriginalEntity &wash = pool.slot(child);
      FUN_00225bc8_set_animation(wash, 1);
      wash.state60 = 0;
      wash.scale14c = 1.5f;
      wash.scaleZ150 = 1.5f;
      wash.halfword08 = static_cast<std::uint16_t>(wash.halfword08 | 0x0080u);
      wash.groundHeight4c = entity.groundHeight4c;
      wash.height58 = 1.0f;
      wash.attackPower12c = entity.attackPower12c;
      wash.hitVolumeRadius11c = 1.0f;
      wash.radius54 = 1.0f;
      wash.hitVolumeHeight120 = 1.0f;
      if (environment.FUN_0020dc88_bone_point)
      {
        const orphen::ported::psm2::Vec3 point =
            environment.FUN_0020dc88_bone_point(slot, 9, orphen::ported::psm2::Vec3{});
        wash.positionX20 = point.x;
        wash.positionZ24 = point.y;
        wash.positionY28 = point.z;
      }
      // +0x198 is a *pointer* to work +0x250 in the original. The port's hit
      // path takes its parameters by value, so the record travels packed.
      wash.hitParameters198 = work.attacks.record[0].packed();
    }

    // FUN_0029E9B0. The trail a shed streak drags: a type 0x1B0 parked at the
    // streak's own position with its ground forced to -56 so nothing lands it,
    // and a scale rolled 1..3. Returns the pool slot, or -1 when the ten-entry
    // array at work +0x568 is full.
    std::int32_t FUN_0029e9b0_spawn_trail(const OriginalEntity &at,
                                          const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return -1;
      }
      EntityPool &pool = *environment.entityPool;
      for (std::size_t index = 0; index < work.trails.size(); ++index)
      {
        if (work.trails[index] != 0)
        {
          continue;
        }
        const std::size_t child =
            pool.FUN_00265e28_allocate_and_initialize(kMastTrailTypeId, *environment.descriptors);
        if (child >= pool.slotCount())
        {
          return -1;
        }
        work.trails[index] = static_cast<std::int32_t>(child) + 1;
        OriginalEntity &trail = pool.slot(child);
        FUN_00225bc8_set_animation(trail, 0);
        trail.groundHeight4c = kSegmentGround;
        trail.previousGroundHeight50 = kSegmentGround;
        trail.halfword04 = 0x0100;
        trail.positionX20 = at.positionX20;
        trail.positionZ24 = at.positionZ24;
        trail.positionY28 = at.positionY28;
        trail.halfword08 = static_cast<std::uint16_t>(trail.halfword08 | 0x00C0u);
        const std::uint32_t roll = environment.random ? environment.random() : 0u;
        const float scale = static_cast<float>((roll % 3u) + 1u);
        trail.scale14c = scale;
        trail.scaleZ150 = scale;
        trail.state60 = 0;
        return static_cast<std::int32_t>(child);
      }
      return -1;
    }

    // FUN_0029E878. The burst a streak leaves *on the player* when its timer
    // runs out -- the same type 0x1B0 pool, but stood on slot 0 rather than on
    // the streak, carrying the streak's own state byte across so that only the
    // one FUN_0029E4F8 stamped with state 1 goes on to hit anything.
    void FUN_0029e878_spawn_impact(const OriginalEntity &part, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      for (std::size_t index = 0; index < work.trails.size(); ++index)
      {
        if (work.trails[index] != 0)
        {
          continue;
        }
        const std::size_t child =
            pool.FUN_00265e28_allocate_and_initialize(kMastTrailTypeId, *environment.descriptors);
        if (child >= pool.slotCount())
        {
          return;
        }
        work.trails[index] = static_cast<std::int32_t>(child) + 1;
        OriginalEntity &burst = pool.slot(child);
        const OriginalEntity &player = pool.slot(0);
        FUN_00225bc8_set_animation(burst, 0);
        burst.groundHeight4c = part.groundHeight4c;
        burst.previousGroundHeight50 = part.groundHeight4c;
        burst.halfword04 = 0x0019;
        burst.halfword08 = static_cast<std::uint16_t>(burst.halfword08 | 0x0080u);
        burst.descriptorFlags02 = 0x2000;
        burst.height58 = player.height58 + player.height58;
        burst.attackPower12c = part.attackPower12c;
        burst.state60 = part.state60;
        burst.positionX20 = player.positionX20;
        burst.positionZ24 = player.positionZ24;
        burst.positionY28 = player.positionY28 + player.height58 * kFGpffff9a7c_impactRise;
        burst.scale14c = 1.0f;
        burst.scaleZ150 = 1.0f;
        return;
      }
    }

    // FUN_0029E668, one shed streak. It never integrates a position: it rides
    // the bone FUN_0029E4F8 gave it, and everything else it writes is the aim
    // at the player -- +0x5C the bearing, +0x154 the pitch, +0x150 the range
    // and +0x14C half of it. That is what stretches the streak from the boss's
    // body to where Orphen is standing.
    void FUN_0029e668_step_shed_part(std::size_t index, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      std::int32_t &entry = work.shedParts[index];
      if (entry < 0 || static_cast<std::size_t>(entry) >= pool.slotCount())
      {
        return;
      }
      OriginalEntity &part = pool.slot(static_cast<std::size_t>(entry));

      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      const std::int32_t left = static_cast<std::int32_t>(part.fadeRamp62) - ticksInt;
      part.fadeRamp62 = static_cast<std::uint16_t>(left);
      if (static_cast<std::int16_t>(left) < 0)
      {
        FUN_0029e878_spawn_impact(part, environment);
        pool.releaseSlot(static_cast<std::size_t>(entry));
        entry = -1;
        return;
      }

      // :36-38. The local offset is (0, 0, +0x19C) -- the per-streak jitter
      // FUN_0029E4F8 rolled, and the only thing that keeps the three of a shed
      // from sitting exactly on their bones.
      if (environment.FUN_0020dc88_bone_point && part.enemyTargetSlot1a0 >= 0)
      {
        const orphen::ported::psm2::Vec3 offset{0.0f, 0.0f, part.battleDesiredFacing19c};
        const orphen::ported::psm2::Vec3 point = environment.FUN_0020dc88_bone_point(
            static_cast<std::size_t>(part.enemyTargetSlot1a0),
            static_cast<std::size_t>(part.mastShedBone198), offset);
        part.positionX20 = point.x;
        part.positionZ24 = point.y;
        part.positionY28 = point.z;
      }

      // :41-48. +0x94 is the "I have my trail" latch, so the spawn is one-shot.
      if (part.spawnParam94 == 0)
      {
        part.mastShedTrail1a4 = FUN_0029e9b0_spawn_trail(part, environment);
        part.spawnParam94 = 1;
      }
      if (part.mastShedTrail1a4 >= 0 &&
          static_cast<std::size_t>(part.mastShedTrail1a4) < pool.slotCount())
      {
        OriginalEntity &trail = pool.slot(static_cast<std::size_t>(part.mastShedTrail1a4));
        trail.positionX20 = part.positionX20;
        trail.positionZ24 = part.positionZ24;
        trail.positionY28 = part.positionY28;
        if ((trail.flags06 & 0x0001u) != 0)
        {
          // The original releases *nothing* here -- it drops the reference and
          // leaves the entity to FUN_0029EBB8, which is what frees it.
          part.mastShedTrail1a4 = -1;
        }
      }

      const OriginalEntity &player = pool.slot(0);
      const float dx = player.positionX20 - part.positionX20;
      const float dz = player.positionZ24 - part.positionZ24;
      const float flat = dx * dx + dz * dz;
      const float dy =
          (player.positionY28 + player.height58 * kDAT_003539e8_shedAimRise) - part.positionY28;
      part.facingRadians5c = std::atan2(dz, dx);
      part.rotationX154 = wrap_angle(std::atan2(std::sqrt(flat), dy));
      const float range = std::sqrt(flat + dy * dy);
      part.scaleZ150 = range;
      part.scale14c = (range / 10.0f) * 5.0f;
    }

    // FUN_0029EBB8, one trail. Nothing moves it -- FUN_0029E668 does that while
    // its streak is alive -- but a trail whose state byte is non-zero runs the
    // swept hit test against attack record 2 every frame, which is how a shed
    // actually hurts.
    void FUN_0029ebb8_step_trail(std::size_t index, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      std::int32_t &entry = work.trails[index];
      if (entry == 0)
      {
        return;
      }
      const std::size_t slot = static_cast<std::size_t>(entry - 1);
      if (slot >= pool.slotCount())
      {
        entry = 0;
        return;
      }
      OriginalEntity &trail = pool.slot(slot);
      if ((trail.flags06 & 0x0001u) != 0)
      {
        pool.releaseSlot(slot);
        entry = 0;
        return;
      }
      if (trail.state60 != 0 && environment.hitTest != nullptr && work.attacks.filled)
      {
        FUN_002148a8_swept_hit_test(trail, slot, work.attacks.record[2], *environment.hitTest);
      }
    }

    // FUN_0029E4D8 = FUN_0029EC18 + FUN_0029EC60, and both loops run ten
    // entries, not nine: `i = 9; do { --i; ... } while (-1 < i)`.
    void FUN_0029e4d8_step_pools(const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      for (std::size_t index = 0; index < work.shedParts.size(); ++index)
      {
        FUN_0029e668_step_shed_part(index, environment);
      }
      for (std::size_t index = 0; index < work.trails.size(); ++index)
      {
        FUN_0029ebb8_step_trail(index, environment);
      }
    }

    // FUN_0029E3B8, one piece of debris. It flies on its own heading at a speed
    // its +0x198 carries, and dies the moment the physics says it hit anything:
    // the two masks are the contact bits and the VU0 clip reject.
    void FUN_0029e3b8_step_projectile(std::size_t index, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      std::int32_t &entry = work.projectiles[index];
      if (entry == 0)
      {
        return;
      }
      const std::size_t slot = static_cast<std::size_t>(entry - 1);
      if (slot >= pool.slotCount())
      {
        entry = 0;
        return;
      }
      OriginalEntity &shard = pool.slot(slot);
      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      const std::int32_t left = static_cast<std::int32_t>(shard.fadeRamp62) - ticksInt;
      shard.fadeRamp62 = static_cast<std::uint16_t>(left);
      if (static_cast<std::int16_t>(left) < 0 || (shard.collisionFlags0c & 0x4006u) != 0 ||
          (shard.collisionFlags0c & 0x00E0u) != 0)
      {
        pool.releaseSlot(slot);
        entry = 0;
        return;
      }

      const float step = (static_cast<float>(shard.mastShardSpeed198) *
                          static_cast<float>(environment.frameTicks)) /
                         32000.0f;
      shard.desiredDeltaX30 = step * std::cos(shard.facingRadians5c);
      shard.desiredDeltaZ34 = step * std::sin(shard.facingRadians5c);
      shard.desiredDeltaY38 = shard.desiredDeltaY38 + step;
      if (environment.DAT_00355a9c_dust != nullptr)
      {
        environment.DAT_00355a9c_dust->FUN_0021a170_spawn_one(
            shard.positionX20, shard.positionZ24, shard.positionY28, kDAT_003539e0_shardDustSize,
            0.0f, kDAT_003539e4_shardDustJitter, 0x1E, 2, 1, true, environment.random);
      }
    }

    // FUN_0029DFB8: thirty entries at work +0x4F0, every frame.
    void FUN_0029dfb8_step_projectiles(const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      for (std::size_t index = 0; index < work.projectiles.size(); ++index)
      {
        FUN_0029e3b8_step_projectile(index, environment);
      }
    }

    // ---------------------------------------------------- the target markers
    //
    // FUN_0029DDA0: hand every row of DAT_003253C0 back, occupied or not. Row 0
    // of an empty table names pool slot 0, and unmarking the player is harmless
    // because FUN_00248040 only acts on a row that names him with a kind above
    // 1 -- which is why the original can afford the unconditional walk.
    void FUN_0029dda0_unmark_all(const ActorEnvironment &environment)
    {
      if (environment.DAT_003253c0_markers == nullptr || environment.entityPool == nullptr)
      {
        return;
      }
      auto &markers = *environment.DAT_003253c0_markers;
      for (std::size_t index = 0; index < orphen::ported::battle::kMarkerCount; ++index)
      {
        const std::int32_t slot = markers.entry(index).slot02;
        markers.FUN_00248040_unmark(*environment.entityPool, slot);
      }
    }

    // FUN_0029DE10: put one entity in the table, if it has hit points left, is
    // not already in it, and there is a free row. A type 0xBF body segment gets
    // its cursor offsets zeroed first, so the marker sits on the segment rather
    // than wherever the previous tenant of the row wanted it.
    void FUN_0029de10_mark(std::size_t slot, const ActorEnvironment &environment)
    {
      if (environment.DAT_003253c0_markers == nullptr || environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &entity = pool.slot(slot);
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        return;
      }
      auto &markers = *environment.DAT_003253c0_markers;
      for (std::size_t index = 0; index < orphen::ported::battle::kMarkerCount; ++index)
      {
        if (static_cast<std::size_t>(markers.entry(index).slot02) == slot)
        {
          return;
        }
      }
      for (std::size_t index = 0; index < orphen::ported::battle::kMarkerCount; ++index)
      {
        if (markers.entry(index).kind00 != 0)
        {
          continue;
        }
        if (entity.typeId00 == kMastSegmentTypeId)
        {
          markers.entry(index).offsetX08 = 0.0f;
          markers.entry(index).offsetZ10 = 0.0f;
        }
        markers.FUN_00247f28_mark(pool, static_cast<std::int32_t>(slot),
                                  static_cast<std::int16_t>(index), 2, 0);
        return;
      }
    }

    // FUN_0029DED8. Two halves: drop every row whose entity has run out of hit
    // points, then -- and only while +0x1B1 says the boss is holding still
    // enough to aim at -- put the *first body segment* in the table. That one
    // segment is the whole of the fight's targeting; the other eight are never
    // registered, and with +0x1B1 clear the table is emptied outright.
    void FUN_0029ded8_target_markers(const OriginalEntity &entity,
                                     const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.DAT_003253c0_markers == nullptr || environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      auto &markers = *environment.DAT_003253c0_markers;
      for (std::size_t index = 0; index < orphen::ported::battle::kMarkerCount; ++index)
      {
        const std::int32_t slot = markers.entry(index).slot02;
        if (slot == 0 || static_cast<std::size_t>(slot) >= pool.slotCount())
        {
          continue;
        }
        if (static_cast<std::int16_t>(pool.slot(static_cast<std::size_t>(slot)).staggerTimer12a) < 1)
        {
          markers.FUN_00248040_unmark(pool, slot);
        }
      }

      if (entity.mastFlag1b1 == 0)
      {
        FUN_0029dda0_unmark_all(environment);
        return;
      }
      if (work.segments[0] < 0)
      {
        return;
      }
      FUN_0029de10_mark(static_cast<std::size_t>(work.segments[0]), environment);
    }

    // ----------------------------------------------------- state 8, the strafe
    //
    // FUN_0029A4E8. Unlike the orbit states this one flies no curve at all: it
    // picks one of the three lanes at DAT_00325D38 by +0x1BF, drops itself on
    // the water line at that lane's near end with the Z jittered by 0..2, turns
    // to face the player once, and then plays animation 11 out. What makes it a
    // *pass* is that it also stands the player on the matching spot, so the run
    // always comes down the length of whatever plank Orphen is on.
    void mast_state8_strafe(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);

      if (entity.animationA0 == 0)
      {
        const std::size_t lane = entity.mastSwoopStage1bf % kDAT_00325d38_flightLanes.size();
        const auto &row = kDAT_00325d38_flightLanes[lane];
        const std::uint32_t jitter = (environment.random ? environment.random() : 0u) % 3u;
        entity.positionX20 = row.end[0][0];
        entity.positionZ24 = row.end[0][1] + static_cast<float>(jitter);
        entity.positionY28 = environment.DAT_003556fc_effectGroundZ;
        entity.rotationX154 = 0.0f;
        entity.facingRadians5c = FUN_0023a4b8_bearing(entity, player);
        FUN_00225bc8_set_animation(entity, 11);
        FUN_00295a60_cue(kMastStrafeCue, &entity, environment);
        entity.mastChild1c4 = FUN_0029d0b0_spawn_wingtip(entity, slot, environment);
        entity.mastPhase1b0 = 0;
        entity.mastFlag1b1 = 0;
        entity.mastFlag1b2 = 0;

        // :77-84. Keyed on the *route cursor*, work byte 0 -- not on +0x1BF --
        // so the player is put back on the plank the intro left him on, half a
        // unit inboard of the rail. Cursor 0 moves nobody.
        const std::uint8_t route = work.byte00_routeCursor;
        if (route >= 1 && route <= 3)
        {
          player.positionZ24 = kStrafePlayerZ;
          player.positionX20 = kDAT_003538dc_strafePlayerX[route - 1];
        }
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, true,
                            environment);
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        return;
      }

      // Which side of the mast the previous dive ended on decides which way the
      // recovery is watched from -- work +0x280 is the last dive's third control
      // point, and it survives into this state untouched.
      const std::int16_t side = work.divePoints[2].y > 0.0f ? 2 : 1;

      if (entity.mastPhase1b0 == 0)
      {
        if (entity.animationA0 == 11)
        {
          if (environment.DAT_00343878_frameFeedback != nullptr)
          {
            environment.DAT_00343878_frameFeedback->set_DAT_00343880_rotation(10);
            environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
          }
          FUN_00298160_mast_camera(6, 1, 0, &entity, environment);
          if ((entity.flags06 & 0x0001u) == 0)
          {
            // The run is still playing. The *wingtip's* own clip is what is
            // tested here, and it is restarted on clip 1 once it has finished.
            if (entity.mastChild1c4 >= 0 &&
                static_cast<std::size_t>(entity.mastChild1c4) < pool.slotCount())
            {
              OriginalEntity &tip = pool.slot(static_cast<std::size_t>(entity.mastChild1c4));
              if (tip.animationA0 == 0 && (tip.flags06 & 0x0001u) != 0)
              {
                FUN_00225bc8_set_animation(tip, 1);
              }
            }
          }
          else
          {
            if (entity.mastChild1c4 >= 0 &&
                static_cast<std::size_t>(entity.mastChild1c4) < pool.slotCount())
            {
              pool.releaseSlot(static_cast<std::size_t>(entity.mastChild1c4));
            }
            entity.mastChild1c4 = -1;
            entity.fadeRamp62 = 0x1900;
            FUN_00225bc8_set_animation(entity, 12);
            entity.mastFlag1b2 = 1;
            entity.mastFlag1b1 = 1;
          }
        }
        else if (entity.animationA0 == 12)
        {
          FUN_00298160_mast_camera(2, side, 0, &entity, environment);
          if ((entity.flags06 & 0x0001u) != 0)
          {
            FUN_00295a60_cue(kMastStrafeEndCue, &entity, environment);
            FUN_0029d168_spawn_wash(entity, slot, environment);
            entity.mastPhase1b0 = 1;
            entity.fadeRamp62 = 0x0F00;
            FUN_00225bc8_set_animation(entity, 0x11);
          }
        }
        return;
      }

      // :55-68. The tail: hold animation 0x11 for 0x0F00 ticks with the camera
      // still on the boss, then splash back out and take the next move.
      FUN_00298160_mast_camera(2, side, 0, &entity, environment);
      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
      entity.fadeRamp62 = static_cast<std::uint16_t>(left);
      if (static_cast<std::int16_t>(left) < 0)
      {
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, true,
                            environment);
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
        FUN_0029c468_next_move(entity, environment);
      }
    }

    // ------------------------------------------------- the debris a smash throws
    //
    // FUN_0029E020: the first free entry of a pointer array, or -1. Used only
    // for work +0x4F0, and the original's first-slot test is unrolled.
    std::int32_t FUN_0029e020_free_entry(const std::array<std::int32_t, 30> &entries)
    {
      for (std::size_t index = 0; index < entries.size(); ++index)
      {
        if (entries[index] == 0)
        {
          return static_cast<std::int32_t>(index);
        }
      }
      return -1;
    }

    // FUN_0029E078(zBias, at, count). Thirty pieces of mast, thrown in a disc
    // around a point: each one is allocated as type 0x2BB or 0x2BC and then
    // **retyped to 400** before anything else looks at it, which is what keeps
    // the model it spawned with (see the retype note in the header) while
    // giving it an empty behaviour record. The two flag clears matter more than
    // they look: `+0x06 & ~0x10` lets it be drawn, and `+0x04 & ~0x08` turns
    // gravity back *on*, so the disc falls as it spreads.
    void FUN_0029e078_throw_debris(float zBias,
                                   const Vec3 &at,
                                   std::int16_t count,
                                   const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const auto roll = [&environment]() -> std::uint32_t
      { return environment.random ? environment.random() : 0u; };

      for (std::int16_t index = 0; index < count; ++index)
      {
        const std::uint8_t kind = kDAT_00355358_debrisKinds[roll() & 1u];
        const std::int32_t free = FUN_0029e020_free_entry(work.projectiles);
        if (free < 0)
        {
          return;
        }
        const std::size_t child = pool.FUN_00265e28_allocate_and_initialize(
            static_cast<std::int32_t>(kind) + 0x272, *environment.descriptors);
        if (child >= pool.slotCount())
        {
          continue;
        }
        OriginalEntity &shard = pool.slot(child);
        shard.animationA0 = 0;
        const float bearing =
            (static_cast<float>((roll() % 0x24u) * 10u) * kDAT_003539d8_debrisTurn) / 360.0f;
        shard.facingRadians5c = bearing;
        shard.fadeRamp62 = static_cast<std::uint16_t>(((roll() % 3u) + 1u) * 0x0C80u);
        const float radius = static_cast<float>((roll() % 0x14u) * 10u) / 100.0f;
        const float rise = static_cast<float>((roll() % 5u) * 10u) / 100.0f + zBias;
        shard.positionX20 = at.x + radius * std::cos(bearing);
        shard.flags06 = static_cast<std::uint16_t>(shard.flags06 & 0xFFEFu);
        shard.positionZ24 = at.y + radius * std::sin(bearing);
        shard.halfword04 = static_cast<std::uint16_t>(shard.halfword04 & 0xFFF7u);
        shard.scale14c = 1.0f;
        shard.groundHeight4c = kSegmentGround;
        shard.typeId00 = 400;
        shard.positionY28 = at.z + rise;
        shard.scaleZ150 = 1.0f;
        shard.previousGroundHeight50 = kSegmentGround;
        shard.verticalAcceleration48 = kDAT_003539dc_debrisGravity;
        shard.mastShardSpeed198 = static_cast<std::int16_t>((roll() % 0x14u) + 0x14u);
        work.projectiles[static_cast<std::size_t>(free)] = static_cast<std::int32_t>(child) + 1;
        shard.verticalVelocity44 = static_cast<float>((roll() % 6u) + 1u) / 100.0f;
      }
    }

    // ----------------------------------------------------------- the white-out
    //
    // FUN_0023ABB0 arms it and FUN_0023ABD0 steps it: six phases keyed on
    // gp-0xAEDC, ramping the overlay's alpha up to full, down, back to half,
    // down again, and reporting done on the sixth. The port has the overlay
    // sink (FUN_0025D0E0) but no pad rumble, so FUN_0023BBD8 is a comment here
    // the same way it is everywhere else in the port.
    struct WhiteOut
    {
      std::uint32_t colour = 0; // iGpffffbd04
      std::uint8_t level = 0;   // iGpffffbd08
      std::uint16_t now = 0;    // uGpffffaed8
      std::uint16_t span = 0;   // uGpffffaeda
      std::int16_t phase = 0;   // sGpffffaedc
    };
    WhiteOut &DAT_00355c74_whiteOut()
    {
      static WhiteOut value{};
      return value;
    }

    // **FUN_0025D0E0 is per-frame in the original and sticky here.** There it
    // pushes one screen-sized sprite into *this* frame's draw list, so a caller
    // that stops calling it stops covering the screen. The port's ScreenFade
    // holds the last value it was given and the runtime pushes that to the
    // renderer every frame, so anything that paints the overlay has to take it
    // back down when it is finished. Both of the boss's users do, here.
    void mast_release_overlay(const ActorEnvironment &environment)
    {
      if (environment.DAT_0025d0e0_screenFade != nullptr)
      {
        environment.DAT_0025d0e0_screenFade->FUN_0025d0e0_set_overlay(0, 0);
      }
    }

    void FUN_0023abb0_arm_white_out(std::uint32_t colour)
    {
      WhiteOut &flash = DAT_00355c74_whiteOut();
      flash.colour = colour;
      flash.span = 0x1900;
      flash.now = 0;
      flash.phase = 0;
      flash.level = 0;
    }

    // Returns true once, on the frame the last phase runs out.
    bool FUN_0023abd0_step_white_out(const ActorEnvironment &environment)
    {
      WhiteOut &flash = DAT_00355c74_whiteOut();
      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      const std::uint16_t was = flash.now;
      const float span = static_cast<float>(static_cast<std::int16_t>(flash.span));
      const auto level = [&flash, span](float scale)
      {
        const float through = static_cast<float>(static_cast<std::int16_t>(flash.now)) / span;
        flash.level = static_cast<std::uint8_t>(through * scale);
      };

      bool done = false;
      bool shake = false;
      if (flash.phase == 0 || flash.phase == 2 || flash.phase == 4)
      {
        // The rising arms. Phase 0 climbs to full, the other two to half.
        level(flash.phase == 0 ? 255.0f : 128.0f);
        flash.now = static_cast<std::uint16_t>(was + ticksInt);
        if (static_cast<std::int16_t>(flash.span) < static_cast<std::int16_t>(flash.now))
        {
          flash.now = flash.phase == 0 ? 0x03C0 : 0x01E0;
          flash.phase = static_cast<std::int16_t>(flash.phase + 1);
          flash.span = flash.now;
        }
        shake = true;
      }
      else if (flash.phase == 1 || flash.phase == 3)
      {
        level(flash.phase == 1 ? 255.0f : 128.0f);
        flash.now = static_cast<std::uint16_t>(was - ticksInt);
        if (static_cast<std::int16_t>(flash.now) < 0)
        {
          flash.phase = static_cast<std::int16_t>(flash.phase + 1);
          flash.span = 0x01E0;
          flash.now = 0;
        }
        shake = true;
      }
      else if (flash.phase == 5)
      {
        const float through = static_cast<float>(static_cast<std::int16_t>(flash.now)) /
                              static_cast<float>(static_cast<std::int16_t>(flash.span));
        flash.level = static_cast<std::uint8_t>(through * 128.0f);
        flash.now = static_cast<std::uint16_t>(was - ticksInt);
        if (static_cast<std::int16_t>(flash.now) < 0)
        {
          flash.phase = 6;
          mast_release_overlay(environment);
          return true;
        }
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kUGpffff868c_whiteOutShake, 100);
        }
        // FUN_0023BBD8((through * 10) / 1000, 1): the rumble. No rumble path.
      }

      if (shake && environment.FUN_0022dcf0_shake_camera)
      {
        environment.FUN_0022dcf0_shake_camera(kUGpffff8688_whiteOutShake, 100);
      }
      if (environment.DAT_0025d0e0_screenFade != nullptr)
      {
        environment.DAT_0025d0e0_screenFade->FUN_0025d0e0_set_overlay(flash.colour, flash.level);
      }
      return done;
    }

    // ------------------------------------------- state 10, breaking a mast section
    //
    // FUN_0029B628. Two halves separated by +0x1B0: the swoop in along a curve
    // built out of DAT_00325CF0, and then, once the timer runs out at the far
    // end of it, the impact -- the plank is hidden and taken out of the ground
    // scan, thirty pieces of it are thrown, three fires are lit on the water,
    // and the boss dives away on a second curve straight down. The one lasting
    // consequence is at the end: **the water line goes up by 1.5**, and the
    // map's collision group 0 is moved with it.
    void mast_state10_smash(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      entity.mastFlag1b2 = 1;
      entity.mastFlag1b1 = 1;

      if (entity.animationA0 == 0)
      {
        entity.mastPhase1b0 = 0;
        entity.mastInWater1c0 = 0;
        entity.fadeRamp62 = 0;
        entity.mastMoveTicks1ac = 0x2580;
        entity.mastOrbitRadius1a8 = kMastOrbitRadius;
        FUN_00225bc8_set_animation(entity, 10);
        if (entity.mastSmashStage1be > 2)
        {
          // All three planks are already down: give the turn back.
          FUN_0029c468_next_move(entity, environment);
          return;
        }
        const auto &row = kDAT_00325cf0_smashRuns[entity.mastSmashStage1be];
        work.divePoints[0] = Vec3{row[0], row[1], row[2]};
        work.divePoints[2] = Vec3{row[3], row[4], row[5]};
        const float dx = row[3] - row[0];
        const float dz = row[4] - row[1];
        const float length = std::sqrt(dx * dx + dz * dz);
        const float bearing = std::atan2(dz, dx);
        work.divePoints[1] = Vec3{work.divePoints[0].x + length * 0.5f * std::cos(bearing),
                                  work.divePoints[0].y + length * 0.5f * std::sin(bearing), 25.0f};
        FUN_00266a78_build(work.diveCurve, work.divePoints);
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
      }

      const float water = environment.DAT_003556fc_effectGroundZ;
      if (entity.mastInWater1c0 == 0)
      {
        if (entity.positionY28 >= water)
        {
          FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, true,
                              environment);
          entity.mastInWater1c0 = 1;
        }
      }
      else if (entity.positionY28 <= water)
      {
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, false,
                            environment);
        entity.mastInWater1c0 = 0;
      }

      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);

      if (entity.mastPhase1b0 == 0)
      {
        const std::int32_t elapsed = static_cast<std::int32_t>(entity.fadeRamp62) + ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
        if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < static_cast<std::int16_t>(elapsed))
        {
          // The impact. The tag names the plank's own entity, which is hidden
          // and put on animation 1 -- the broken pose.
          const std::size_t stage = entity.mastSmashStage1be;
          if (environment.FUN_00248f18_find_by_tag)
          {
            const std::int32_t victim = environment.FUN_00248f18_find_by_tag(
                static_cast<std::int16_t>(kDAT_00355340_smashTags[stage]));
            if (victim > 0 && static_cast<std::size_t>(victim) < pool.slotCount())
            {
              OriginalEntity &plank = pool.slot(static_cast<std::size_t>(victim));
              plank.halfword08 = static_cast<std::uint16_t>(plank.halfword08 & 0xFFFEu);
              FUN_00225bc8_set_animation(plank, 1);
            }
          }

          const Vec3 burst{entity.positionX20, entity.positionZ24, water + 3.0f};
          FUN_0029e078_throw_debris(0.0f, burst, 0x1E, environment);

          if (environment.DAT_00355b6c_plumes != nullptr)
          {
            PlumeEmitterSpawn fire{};
            fire.riseSpeed = kDAT_0035393c_smashFireRise;
            fire.size = 2.0f;
            fire.x = entity.positionX20;
            fire.burstCount = 3;
            fire.lifeUnits = 200;
            fire.colour = 0;
            fire.y = entity.positionZ24 + 2.0f;
            fire.z = water;
            fire.cycles = 1;
            fire.mode = 1;
            environment.DAT_00355b6c_plumes->FUN_0021f6e8_open_emitter(fire);
            fire.z = water + 1.5f;
            environment.DAT_00355b6c_plumes->FUN_0021f6e8_open_emitter(fire);
            fire.y = entity.positionZ24;
            fire.z = water + 3.0f;
            fire.cycles = 5;
            fire.mode = 0;
            environment.DAT_00355b6c_plumes->FUN_0021f6e8_open_emitter(fire);
          }

          const std::uint32_t groupMask = 1u << (kDAT_00355348_smashGroups[stage] & 0x1Fu);
          if (environment.FUN_0022dbc8_show_map_primitives)
          {
            environment.FUN_0022dbc8_show_map_primitives(groupMask, false);
          }
          if (environment.FUN_0022dc68_enable_map_terrain)
          {
            environment.FUN_0022dc68_enable_map_terrain(groupMask, false);
          }
          if (environment.FUN_0022dcf0_shake_camera)
          {
            environment.FUN_0022dcf0_shake_camera(0.5f, 500);
          }
          // FUN_0023BBD8(0, 0): the rumble. No rumble path in the port.

          entity.mastPhase1b0 = 1;
          entity.fadeRamp62 = 0;
          entity.mastMoveTicks1ac = 0x12C0;
          work.divePoints[0] = Vec3{entity.positionX20, entity.positionZ24, entity.positionY28};
          work.divePoints[1] = Vec3{entity.positionX20, entity.positionZ24, -15.0f};
          work.divePoints[2] = Vec3{entity.positionX20, entity.positionZ24, -30.0f};
          FUN_00266a78_build(work.diveCurve, work.divePoints);
        }

        const float through = FUN_0029caf8_follow_dive(entity, environment);
        if (through < kDAT_00353940_smashCloseShot)
        {
          FUN_00298160_mast_camera(3, static_cast<std::int16_t>(DAT_0035532c_mastCameraSide()), 0,
                                   &entity, environment);
        }
        else
        {
          entity.mastFlag1b1 = 0;
          entity.mastFlag1b2 = 0;
          FUN_00298160_mast_camera(4, 1, 0, &entity, environment);
        }
      }

      if (entity.mastPhase1b0 != 1)
      {
        return;
      }

      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;
      const std::int32_t elapsed = static_cast<std::int32_t>(entity.fadeRamp62) + ticksInt;
      entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
      if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < static_cast<std::int16_t>(elapsed))
      {
        // **The water rises.** DAT_003556FC is what every effect in the scene
        // sits on, and map collision group 0 -- the sea itself -- is moved with
        // it through its translation channel.
        if (environment.set_DAT_003556fc_effectGroundZ)
        {
          environment.set_DAT_003556fc_effectGroundZ(water + 1.5f);
        }
        if (environment.FUN_00260738_move_collision_group)
        {
          environment.FUN_00260738_move_collision_group(0, 2, water + 1.5f, false);
        }
        FUN_0029c468_next_move(entity, environment);
        entity.mastSmashStage1be = static_cast<std::uint8_t>(entity.mastSmashStage1be + 1);
        return;
      }

      const float through = FUN_0029caf8_follow_dive(entity, environment);
      if (entity.mastInWater1c0 != 0 && entity.positionY28 <= water)
      {
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, false,
                            environment);
        entity.mastInWater1c0 = 0;
      }
      FUN_00298160_mast_camera(4, 1, 0, &entity, environment);
      if (through < kDAT_00353944_smashRumble && environment.FUN_0022dcf0_shake_camera)
      {
        environment.FUN_0022dcf0_shake_camera(kDAT_00353948_smashShake, 200);
        // FUN_0023BBD8(0, 0): the rumble.
      }
    }

    // --------------------------------------------------------- state 12, the death
    //
    // FUN_0029BC10. Five phases. It opens by clearing the battle-entry gate and
    // flying the boss to the middle of the map ten below the water; it rises
    // along a curve, throws nine type 0x1C5 limbs off the segment bones, flashes
    // the screen white, sinks six under, fades out, and on the last phase writes
    // **3000 into script work word 0** -- which is what the scene's own beat
    // machine has been waiting for -- gives the player gravity back and frees
    // itself. A splash every 0x0F00 ticks runs underneath all five.
    void mast_state12_death(OriginalEntity &entity,
                            std::size_t slot,
                            const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);
      const float water = environment.DAT_003556fc_effectGroundZ;
      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);

      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;

      if (entity.animationA0 == 0)
      {
        // DAT_00354CC4 = 0 and FUN_00246290(0, 0): the battle module is told to
        // stop, which is what takes the target cursors off the screen.
        if (environment.DAT_0031d7b0_readPlayerControl &&
            environment.DAT_0031d7b0_writePlayerControl)
        {
          const std::uint32_t flags = environment.DAT_0031d7b0_readPlayerControl(
              orphen::ported::battle::control::kFlags38, 4);
          environment.DAT_0031d7b0_writePlayerControl(
              orphen::ported::battle::control::kFlags38, 4, flags | 4u);
        }
        FUN_00225bc8_set_animation(entity, 15);
        entity.positionX20 = 0.0f;
        entity.rotationX154 = 0.0f;
        entity.positionZ24 = -15.0f;
        entity.positionY28 = water - 10.0f;
        entity.facingRadians5c =
            FUN_0029cc28_mast_bearing(0, entity.positionX20, entity.positionZ24);
        entity.mastPhase1b0 = 0;
        entity.mastInWater1c0 = 0;
        entity.fadeRamp62 = 0;
        entity.mastSubTimer1c8 = 0;
        entity.mastMoveTicks1ac = 0x1900;
        work.divePoints[0] = Vec3{entity.positionX20, entity.positionZ24, entity.positionY28};
        work.divePoints[1] = Vec3{entity.positionX20, entity.positionZ24, water};
        work.divePoints[2] = Vec3{entity.positionX20, entity.positionZ24, water + 1.0f};
        FUN_00266a78_build(work.diveCurve, work.divePoints);
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, false,
                            environment);
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
      }

      switch (entity.mastPhase1b0)
      {
      case 0:
      {
        FUN_00298160_mast_camera(12, 1, 0, &entity, environment);
        const std::int32_t elapsed = static_cast<std::int32_t>(
            static_cast<std::int16_t>(entity.fadeRamp62) + ticksInt);
        entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
        if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < elapsed)
        {
          entity.mastPhase1b0 = 1;
          entity.fadeRamp62 = 0x1680;
          work.divePoints[0] = Vec3{entity.positionX20, entity.positionZ24, entity.positionY28};
          work.divePoints[1] = Vec3{entity.positionX20, entity.positionZ24, water};
          work.divePoints[2] = Vec3{entity.positionX20, entity.positionZ24, water - 6.0f};
          FUN_00266a78_build(work.diveCurve, work.divePoints);
          // Nine type 0x1C5 limbs, one per segment bone, each parented to the
          // boss. They are the only users of work +0x48C.
          for (std::size_t index = 0; index < kMastSegmentCount; ++index)
          {
            if (environment.descriptors == nullptr)
            {
              break;
            }
            const std::size_t child = pool.FUN_00265e28_allocate_and_initialize(
                kMastLimbTypeId, *environment.descriptors);
            if (child >= pool.slotCount())
            {
              break;
            }
            OriginalEntity &limb = pool.slot(child);
            limb.attachBone194 = static_cast<std::int8_t>(kDAT_0034eb30_segmentBones[index]);
            limb.positionX20 = 0.0f;
            limb.positionZ24 = 0.0f;
            limb.positionY28 = 0.0f;
            limb.halfword04 = 0x0019;
            limb.scaleZ150 = kUGpffff99dc_limbScale;
            limb.scale14c = kUGpffff99dc_limbScale;
            limb.parentSlot192 = static_cast<std::int16_t>(slot);
            // FUN_0023A620(limb, 0, 9), inlined -- it is three writes: the
            // state timer cleared, animation 0 selected without going through
            // FUN_00225BC8, and the timeline cursor started at a random even
            // column, so the nine limbs do not fall in step.
            limb.stateResetA4 = 0;
            limb.animationA0 = 0;
            limb.timelineCursorA8 = static_cast<std::uint16_t>(
                ((environment.random ? environment.random() : 0u) % 9u) * 2u);
            work.deathLimbs[index] = static_cast<std::int32_t>(child) + 1;
          }
          return;
        }
        const Vec3 here = FUN_00266ce8_sample(
            work.diveCurve, static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) /
                                static_cast<float>(
                                    static_cast<std::int16_t>(entity.mastMoveTicks1ac)));
        entity.positionX20 = here.x;
        entity.positionZ24 = here.y;
        entity.positionY28 = here.z;
        break;
      }
      case 1:
      {
        FUN_00298160_mast_camera(12, 1, 0, &entity, environment);
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) < 0)
        {
          FUN_0023abb0_arm_white_out(0x00FFFFFF);
          entity.mastPhase1b0 = 2;
        }
        // 0x0029BF50: `work[0x48C] != 0` jumps straight to the sub-timer. The
        // other arm loads `+0x06` **off that null pointer** and gates cue 0x13D
        // on bit 0 of whatever the EE has at address 6. It is a real defect in
        // the original and it is unreachable in practice, because phase 0 only
        // hands over after it has filled work +0x48C. Left as the no-op the
        // reachable path is, rather than invented.
        break;
      }
      case 2:
        FUN_00298160_mast_camera(12, 1, 0, &entity, environment);
        if (FUN_0023abd0_step_white_out(environment))
        {
          entity.fadeRamp62 = 0;
          entity.mastMoveTicks1ac = 0x12C0;
          entity.mastPhase1b0 = 3;
          FUN_00295a60_cue(kMastDeathSinkCue, &entity, environment);
        }
        break;
      case 3:
      {
        FUN_00298160_mast_camera(12, 1, 0, &entity, environment);
        const std::int32_t elapsed = static_cast<std::int32_t>(
            static_cast<std::int16_t>(entity.fadeRamp62) + ticksInt);
        entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
        if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < elapsed)
        {
          entity.fadeRamp62 = 0;
          entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 0x0001u);
          entity.mastMoveTicks1ac = 0x0C80;
          entity.mastPhase1b0 = 4;
          FUN_0029cee8_splash(10.0f, &entity, entity.positionX20, entity.positionZ24, false,
                              environment);
          return;
        }
        const float through =
            static_cast<float>(static_cast<std::int16_t>(entity.fadeRamp62)) /
            static_cast<float>(static_cast<std::int16_t>(entity.mastMoveTicks1ac));
        entity.fadeLevel134 = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(124.0f - through * 124.0f) + 3u);
        const Vec3 here = FUN_00266ce8_sample(work.diveCurve, through);
        entity.positionX20 = here.x;
        entity.positionZ24 = here.y;
        entity.positionY28 = here.z;
        break;
      }
      case 4:
      {
        FUN_00298160_mast_camera(12, 1, 0, &entity, environment);
        const std::int32_t elapsed = static_cast<std::int32_t>(
            static_cast<std::int16_t>(entity.fadeRamp62) + ticksInt);
        entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
        if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < elapsed)
        {
          DAT_003555d1_suspendPushOut() = false;
          // **The handshake.** Script work word 0 goes to 3000, which is the
          // beat the scene's object script has been polling since beat 30.
          if (environment.DAT_00355060_setScriptWork)
          {
            environment.DAT_00355060_setScriptWork(0, 3000);
          }
          // The player gets gravity back: +0x04 bit 3 is what the carry set.
          player.halfword04 = static_cast<std::uint16_t>(player.halfword04 & 0xFFF7u);
          pool.releaseSlot(slot);
          return;
        }
        break;
      }
      default:
        break;
      }

      const std::int32_t sub = static_cast<std::int32_t>(entity.mastSubTimer1c8) - ticksInt;
      entity.mastSubTimer1c8 = static_cast<std::uint16_t>(sub);
      if (static_cast<std::int16_t>(sub) < 0)
      {
        FUN_0029cee8_splash(4.0f, &entity, entity.positionX20, entity.positionZ24, true,
                            environment);
        entity.mastSubTimer1c8 = 0x0F00;
      }
    }

    // ------------------------------------------------ state 9's three spawners
    //
    // FUN_0029D228(at). One link of the beam: a type 0x1B0 on top of whatever
    // it was handed, scale 20. The chain grows by watching each link's own
    // animation cursor reach entry 2 and spawning the next one there.
    std::int32_t FUN_0029d228_spawn_link(const OriginalEntity &at,
                                         const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return 0;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t child =
          pool.FUN_00265e28_allocate_and_initialize(kMastTrailTypeId, *environment.descriptors);
      if (child >= pool.slotCount())
      {
        return 0;
      }
      OriginalEntity &link = pool.slot(child);
      FUN_00295a60_cue(kMastBeamLinkCue, &link, environment);
      FUN_00225bc8_set_animation(link, 0);
      link.scale14c = 20.0f;
      link.scaleZ150 = 20.0f;
      link.halfword08 = static_cast<std::uint16_t>(link.halfword08 | 0x0080u);
      link.groundHeight4c = at.groundHeight4c;
      link.positionX20 = at.positionX20;
      link.positionZ24 = at.positionZ24;
      link.positionY28 = at.positionY28;
      return static_cast<std::int32_t>(child) + 1;
    }

    // FUN_0029D2C0(at). The blast at the end of the chain: one type 0x1AF, plus
    // **five satellites** ringed six units out at 72 degrees apart, each of
    // which is allocated as another 0x1AF and then retyped to 0x1AB before
    // anything reads it. The satellites are not tracked anywhere -- they are
    // left to their own behaviour and to the pool.
    std::int32_t FUN_0029d2c0_spawn_blast(const OriginalEntity &at,
                                          const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return 0;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t core =
          pool.FUN_00265e28_allocate_and_initialize(kMastBlastTypeId, *environment.descriptors);
      if (core >= pool.slotCount())
      {
        return 0;
      }
      {
        OriginalEntity &blast = pool.slot(core);
        FUN_00225bc8_set_animation(blast, 0);
        blast.scaleZ150 = 8.0f;
        blast.scale14c = 8.0f;
        blast.halfword08 = static_cast<std::uint16_t>(blast.halfword08 | 0x00C0u);
        blast.groundHeight4c = at.groundHeight4c;
        blast.positionX20 = at.positionX20;
        blast.positionZ24 = at.positionZ24;
        blast.positionY28 = at.positionY28;
      }

      std::uint32_t degrees = 0;
      for (int index = 0; index < 5; ++index)
      {
        const std::size_t satellite =
            pool.FUN_00265e28_allocate_and_initialize(kMastBlastTypeId, *environment.descriptors);
        if (satellite < pool.slotCount())
        {
          OriginalEntity &ring = pool.slot(satellite);
          OriginalEntity &blast = pool.slot(core);
          FUN_00225bc8_set_animation(ring, 0);
          // Both writes are to the *core*'s +0x150 in the original; the
          // satellite only gets +0x14C. Reproduced rather than tidied.
          blast.scaleZ150 = 8.0f;
          ring.scale14c = 8.0f;
          ring.halfword08 = static_cast<std::uint16_t>(ring.halfword08 | 0x00C0u);
          const float bearing =
              (static_cast<float>(degrees) * kDAT_003539c0_blastRingTurn) / 360.0f;
          ring.facingRadians5c = bearing;
          ring.positionX20 = blast.positionX20 + std::cos(bearing) * 6.0f;
          ring.positionZ24 = blast.positionZ24 + std::sin(bearing) * 6.0f;
          const std::uint32_t roll = environment.random ? environment.random() : 0u;
          ring.typeId00 = kMastBlastRingTypeId;
          ring.positionY28 = blast.positionY28 + static_cast<float>(roll % 3u);
          ring.groundHeight4c = blast.positionY28;
        }
        degrees += 0x48;
      }
      return static_cast<std::int32_t>(core) + 1;
    }

    // FUN_0029D498(stage). The mast section coming apart: four map collision
    // groups turned a little further every frame off an angle the group record
    // keeps, and the whole thing is over once all four have passed -90 degrees.
    // A splash every 0x05A0 ticks marks the section settling, and the water it
    // makes goes up by 3 each time.
    //
    // The original keeps the accumulated angle in the group record's own
    // scratch at +0x74-record offset 0x68, which the port's CollisionGroup does
    // not model. It lives in the work block here instead, indexed by the same
    // (stage, i) pair -- the twelve group indices in DAT_0034EB50 are distinct,
    // so the two are the same storage by another name.
    bool FUN_0029d498_break_section(std::uint8_t stage, const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      const std::size_t base = static_cast<std::size_t>(stage & 0xFFu) * 4u;
      const float step = kDAT_003539c4_breakRate *
                         static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) /
                         32.0f;
      std::uint8_t settled = 0;
      for (std::size_t index = 0; index < 4; ++index)
      {
        const std::size_t group = kDAT_0034eb50_breakGroups[base + index];
        float &angle = work.breakAngles[base + index];
        const float wrapped = wrap_angle(angle);
        if (environment.FUN_00260738_move_collision_group)
        {
          environment.FUN_00260738_move_collision_group(static_cast<std::uint32_t>(group), 0,
                                                        wrapped, true);
        }
        angle -= step;
        if (wrapped < kDAT_003539c8_breakDone)
        {
          settled = static_cast<std::uint8_t>(settled + 1);
        }
      }

      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);
      const std::int32_t left = static_cast<std::int32_t>(DAT_0035534c_breakTimer()) - ticksInt;
      DAT_0035534c_breakTimer() = static_cast<std::uint16_t>(left);
      if (static_cast<std::int16_t>(left) < 0)
      {
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kDAT_003539cc_breakShake, 300);
        }
        // FUN_0023BBD8(0, 0): the rumble.
        const std::size_t lane = static_cast<std::size_t>(stage & 0xFFu);
        FUN_0029cee8_splash(7.0f, nullptr, kDAT_00325d80_beamHeads[lane][0],
                            DAT_00355350_breakSplashZ(), false, environment);
        DAT_0035534c_breakTimer() = 0x05A0;
        DAT_00355350_breakSplashZ() = DAT_00355350_breakSplashZ() + 3.0f;
      }

      if (settled > 3)
      {
        DAT_0035534c_breakTimer() = 0x2BC0;
        DAT_00355350_breakSplashZ() = 7.0f;
        return true;
      }
      return false;
    }

    // ----------------------------------------- state 9, the transformation
    //
    // FUN_0029A838, thirteen phases in +0x1B0. It rises out of the water
    // shrinking as it goes, fades itself and its glow out, grows a five-link
    // chain of beam segments, blasts, whites the screen out, plants a beam head
    // at one of three fixed spots, grows a *second* chain off the blast that
    // one makes, and then turns four map collision groups until the mast
    // section they belong to has come apart. While that runs the player is
    // carried (work byte 1 drives FUN_0029D658), and the last phase hides the
    // section, puts the boss back to full size and takes the next move.
    //
    // It only runs twice: +0x1BF counts the sections and the guard at the top
    // gives the turn straight back from the third.
    void mast_state9_transform(OriginalEntity &entity,
                               std::size_t slot,
                               const ActorEnvironment &environment)
    {
      MastWork &work = DAT_00355db8_work();
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);
      const float water = environment.DAT_003556fc_effectGroundZ;
      const std::int32_t ticksInt = static_cast<std::int32_t>(environment.frameTicks & 0xFFFFu);

      entity.mastFlag1b1 = 0;
      entity.mastFlag1b2 = 0;
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
      }
      if (entity.mastSwoopStage1bf > 1)
      {
        FUN_0029c468_next_move(entity, environment);
        return;
      }

      const auto slotOf = [&pool](std::int32_t entry) -> OriginalEntity *
      {
        if (entry <= 0 || static_cast<std::size_t>(entry - 1) >= pool.slotCount())
        {
          return nullptr;
        }
        return &pool.slot(static_cast<std::size_t>(entry - 1));
      };
      const auto release = [&pool](std::int32_t &entry)
      {
        if (entry > 0 && static_cast<std::size_t>(entry - 1) < pool.slotCount())
        {
          pool.releaseSlot(static_cast<std::size_t>(entry - 1));
        }
        entry = 0;
      };

      if (entity.animationA0 == 0)
      {
        const std::uint8_t route = work.byte00_routeCursor;
        if (route >= 1 && route <= 3)
        {
          player.positionZ24 = 0.5f;
          player.positionX20 = kUGpffff9978_transformPlayerX[route - 1];
        }
        entity.positionX20 = 0.0f;
        entity.positionZ24 = -18.0f;
        entity.positionY28 = -5.0f;
        entity.facingRadians5c =
            FUN_0029cc28_mast_bearing(1, entity.positionX20, entity.positionZ24);
        entity.fadeRamp62 = 0;
        FUN_00225bc8_set_animation(entity, 5);
        FUN_00295a60_cue(kMastTransformCue, &entity, environment);
        entity.mastPhase1b0 = 0;
        entity.mastInWater1c0 = 0;
        entity.mastMoveTicks1ac = 0x1900;
        work.divePoints[0] = Vec3{entity.positionX20, entity.positionZ24, entity.positionY28};
        work.divePoints[1] = Vec3{entity.positionX20, entity.positionZ24, 15.0f};
        work.divePoints[2] = Vec3{entity.positionX20, entity.positionZ24, 30.0f};
        FUN_00266a78_build(work.diveCurve, work.divePoints);

        entity.mastChild1c4 = -1;
        if (environment.descriptors != nullptr)
        {
          const std::size_t child = pool.FUN_00265e28_allocate_and_initialize(
              kMastWingtipTypeId, *environment.descriptors);
          if (child < pool.slotCount())
          {
            OriginalEntity &glow = pool.slot(child);
            FUN_00225bc8_set_animation(glow, 0);
            entity.mastChild1c4 = static_cast<std::int32_t>(child);
            glow.positionX20 = 0.0f;
            glow.positionZ24 = 0.0f;
            glow.scale14c = 10.0f;
            glow.halfword08 = static_cast<std::uint16_t>(glow.halfword08 | 0x0080u);
            glow.facingRadians5c = kUGpffff9984_glowFacing;
            glow.positionY28 = kUGpffff9988_glowRise;
            glow.rotationX154 = kUGpffff998c_glowPitch;
            glow.scaleZ150 = 10.0f;
          }
        }
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
        // The fog is pulled in to a 63..64 band for the whole of the sequence
        // and put back in phase 8.
        work.fogNearSave25c = environment.DAT_0035567c_fogNear;
        work.fogFarSave260 = environment.DAT_00355680_fogFar;
        if (environment.set_DAT_0035567c_fogBand)
        {
          environment.set_DAT_0035567c_fogBand(63.0f, 64.0f);
        }
      }

      switch (entity.mastPhase1b0)
      {
      case 0:
      {
        FUN_00298160_mast_camera(2, work.divePoints[2].y > 0.0f ? 2 : 1, 0, &entity, environment);
        const std::int32_t elapsed =
            static_cast<std::int32_t>(static_cast<std::int16_t>(entity.fadeRamp62)) + ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(elapsed);
        if (static_cast<std::int16_t>(entity.mastMoveTicks1ac) < elapsed)
        {
          entity.mastPhase1b0 = 1;
          entity.fadeRamp62 = 0x0C80;
          return;
        }
        const Vec3 here = FUN_00266ce8_sample(
            work.diveCurve, static_cast<float>(elapsed) /
                                static_cast<float>(
                                    static_cast<std::int16_t>(entity.mastMoveTicks1ac)));
        entity.positionX20 = here.x;
        entity.positionZ24 = here.y;
        entity.positionY28 = here.z;
        if (entity.positionY28 >= 10.0f)
        {
          float scale = kMastScale - ((entity.positionY28 - 10.0f) / 20.0f) * kMastScale;
          if (scale < kFGpffff9990_minScale)
          {
            scale = kFGpffff9990_minScale;
          }
          entity.scale14c = scale;
          entity.scaleZ150 = scale;
        }
        if (OriginalEntity *glow = entity.mastChild1c4 >= 0 &&
                                           static_cast<std::size_t>(entity.mastChild1c4) <
                                               pool.slotCount()
                                       ? &pool.slot(static_cast<std::size_t>(entity.mastChild1c4))
                                       : nullptr)
        {
          if (environment.FUN_0020dc88_bone_point)
          {
            const Vec3 point =
                environment.FUN_0020dc88_bone_point(slot, 9, orphen::ported::psm2::Vec3{});
            glow->positionX20 = point.x;
            glow->positionZ24 = point.y;
            glow->positionY28 = point.z;
          }
          if (glow->animationA0 == 0 && (glow->flags06 & 0x0001u) != 0)
          {
            FUN_00225bc8_set_animation(*glow, 1);
          }
        }
        if (entity.mastInWater1c0 == 0)
        {
          if (entity.positionY28 >= water)
          {
            FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, true,
                                environment);
            entity.mastInWater1c0 = 1;
          }
        }
        else if (entity.positionY28 <= water)
        {
          FUN_0029cee8_splash(7.0f, &entity, entity.positionX20, entity.positionZ24, false,
                              environment);
          entity.mastInWater1c0 = 0;
        }
        return;
      }
      case 1:
      {
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) < 0)
        {
          if (entity.mastChild1c4 >= 0 &&
              static_cast<std::size_t>(entity.mastChild1c4) < pool.slotCount())
          {
            pool.releaseSlot(static_cast<std::size_t>(entity.mastChild1c4));
          }
          entity.mastChild1c4 = -1;
          entity.mastPhase1b0 = 2;
          entity.fadeRamp62 = 0x0640;
          return;
        }
        const auto level = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>((static_cast<float>(static_cast<std::int16_t>(left)) /
                                       3200.0f) *
                                      124.0f) +
            3u);
        entity.fadeLevel134 = level;
        if (entity.mastChild1c4 >= 0 &&
            static_cast<std::size_t>(entity.mastChild1c4) < pool.slotCount())
        {
          pool.slot(static_cast<std::size_t>(entity.mastChild1c4)).fadeLevel134 = level;
        }
        return;
      }
      case 2:
      {
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) >= 0)
        {
          return;
        }
        work.beamChain[0] = FUN_0029d228_spawn_link(entity, environment);
        entity.mastPhase1b0 = 3;
        return;
      }
      case 3:
      {
        for (std::size_t index = 0; index < work.beamChain.size(); ++index)
        {
          OriginalEntity *link = slotOf(work.beamChain[index]);
          if (link == nullptr)
          {
            continue;
          }
          if (link->timelineCursorA8 == 2 && (link->flags06 & 0x0004u) != 0)
          {
            if (environment.FUN_0022dcf0_shake_camera)
            {
              environment.FUN_0022dcf0_shake_camera(kUGpffff9994_linkShake, 100);
            }
            // FUN_0023BBD8(0, 1): the rumble.
            if (index == 4)
            {
              work.blast4e8 = FUN_0029d2c0_spawn_blast(*link, environment);
              if (OriginalEntity *blast = slotOf(work.blast4e8))
              {
                FUN_00295a60_cue(kMastBlastCue, blast, environment);
                blast->positionX20 = link->positionX20;
                blast->positionZ24 = link->positionZ24;
                blast->positionY28 = link->positionY28;
                blast->rotationX154 = kUGpffff9998_blastPitch;
              }
            }
            else
            {
              work.beamChain[index + 1] = FUN_0029d228_spawn_link(entity, environment);
              if (index < 3)
              {
                if (OriginalEntity *next = slotOf(work.beamChain[index + 1]))
                {
                  const std::uint32_t roll = environment.random ? environment.random() : 0u;
                  const float bearing =
                      (static_cast<float>((roll % 5u) * 0x2Du) * kFGpffff999c_linkTurn) / 360.0f;
                  next->positionX20 = entity.positionX20 + std::cos(bearing) * 10.0f;
                  next->positionZ24 = entity.positionZ24 + std::sin(bearing) * 10.0f;
                }
              }
            }
          }
          OriginalEntity *alive = slotOf(work.beamChain[index]);
          if (alive != nullptr && (alive->flags06 & 0x0001u) != 0)
          {
            if (environment.FUN_0022dcf0_shake_camera)
            {
              environment.FUN_0022dcf0_shake_camera(kUGpffff99a0_linkEndShake, 200);
            }
            release(work.beamChain[index]);
          }
        }
        if (work.blast4e8 != 0)
        {
          if (environment.FUN_0022dcf0_shake_camera)
          {
            environment.FUN_0022dcf0_shake_camera(kUGpffff99a4_blastShake, 100);
          }
          if (OriginalEntity *blast = slotOf(work.blast4e8))
          {
            if ((blast->flags06 & 0x0001u) != 0)
            {
              release(work.blast4e8);
            }
          }
        }
        for (const std::int32_t entry : work.beamChain)
        {
          if (entry != 0)
          {
            return;
          }
        }
        if (work.blast4e8 != 0)
        {
          return;
        }
        entity.fadeRamp62 = 0x03C0;
        entity.mastPhase1b0 = 4;
        return;
      }
      case 4:
      {
        FUN_00298160_mast_camera(7, 1, 0, &entity, environment);
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) >= 0)
        {
          return;
        }
        entity.mastPhase1b0 = 5;
        entity.fadeRamp62 = 0x0140;
        DAT_00354ca8_ambientGateA() = 0;
        DAT_00354cb8_ambientGateB() = 0;
        return;
      }
      case 5:
      {
        FUN_00298160_mast_camera(7, 1, 0, &entity, environment);
        if (environment.DAT_0025d0e0_screenFade != nullptr)
        {
          environment.DAT_0025d0e0_screenFade->FUN_0025d0e0_set_overlay(0x00FFFFFF, 0xFF);
        }
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) >= 0)
        {
          return;
        }
        entity.fadeRamp62 = 0x0640;
        entity.mastPhase1b0 = 6;
        mast_release_overlay(environment);
        return;
      }
      case 6:
      {
        FUN_00298160_mast_camera(7, 1, 0, &entity, environment);
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kUGpffff99a8_headShake, 200);
        }
        const std::int32_t left = static_cast<std::int32_t>(entity.fadeRamp62) - ticksInt;
        entity.fadeRamp62 = static_cast<std::uint16_t>(left);
        if (static_cast<std::int16_t>(left) >= 0)
        {
          return;
        }
        if (environment.descriptors != nullptr)
        {
          const std::size_t child = pool.FUN_00265e28_allocate_and_initialize(
              kMastWingtipTypeId, *environment.descriptors);
          if (child < pool.slotCount())
          {
            OriginalEntity &head = pool.slot(child);
            FUN_00225bc8_set_animation(head, 0);
            head.scale14c = 10.0f;
            head.facingRadians5c = kUGpffff99ac_headFacing;
            head.scaleZ150 = 10.0f;
            head.halfword08 = static_cast<std::uint16_t>(head.halfword08 | 0x0080u);
            const std::size_t lane = entity.mastSwoopStage1bf % kDAT_00325d80_beamHeads.size();
            head.positionZ24 = 0.0f;
            head.positionX20 = kDAT_00325d80_beamHeads[lane][0];
            work.beamHead4ec = static_cast<std::int32_t>(child) + 1;
            head.rotationX154 = 0.0f;
            head.positionY28 = kDAT_00325d80_beamHeads[lane][1];
          }
        }
        entity.mastPhase1b0 = 7;
        return;
      }
      case 7:
      {
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kUGpffff99b0_headHoldShake, 300);
        }
        OriginalEntity *head = slotOf(work.beamHead4ec);
        if (head == nullptr || (head->flags06 & 0x0001u) == 0)
        {
          return;
        }
        // FUN_0023BBD8(0, 0): the rumble.
        work.blast4e8 = FUN_0029d2c0_spawn_blast(entity, environment);
        if (OriginalEntity *blast = slotOf(work.blast4e8))
        {
          FUN_00295a60_cue(kMastBeamCue, blast, environment);
          blast->positionX20 = head->positionX20;
          blast->positionZ24 = head->positionZ24;
          blast->positionY28 = head->positionY28;
          release(work.beamHead4ec);
          work.beamChain[0] = FUN_0029d228_spawn_link(*blast, environment);
        }
        else
        {
          release(work.beamHead4ec);
        }
        entity.mastPhase1b0 = 8;
        return;
      }
      case 8:
      {
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kUGpffff99b4_beamShake, 300);
        }
        for (std::size_t index = 0; index < work.beamChain.size(); ++index)
        {
          OriginalEntity *link = slotOf(work.beamChain[index]);
          if (link == nullptr)
          {
            continue;
          }
          if (link->timelineCursorA8 == 2 && (link->flags06 & 0x0004u) != 0)
          {
            if (environment.FUN_0022dcf0_shake_camera)
            {
              environment.FUN_0022dcf0_shake_camera(kUGpffff99b8_beamStepShake, 300);
            }
            // FUN_0023BBD8(0, 1): the rumble.
            if (index < 4)
            {
              if (OriginalEntity *source = slotOf(work.blast4e8))
              {
                work.beamChain[index + 1] = FUN_0029d228_spawn_link(*source, environment);
                if (OriginalEntity *next = slotOf(work.beamChain[index + 1]))
                {
                  const std::uint32_t roll = environment.random ? environment.random() : 0u;
                  const float bearing =
                      (static_cast<float>((roll % 5u) * 0x2Du) * kFGpffff99bc_beamTurn) / 360.0f;
                  next->positionX20 = source->positionX20 + std::cos(bearing) * 3.0f;
                  next->positionZ24 = source->positionZ24 + std::sin(bearing) * 3.0f;
                }
              }
            }
          }
          OriginalEntity *alive = slotOf(work.beamChain[index]);
          if (alive != nullptr && (alive->flags06 & 0x0001u) != 0)
          {
            if (environment.DAT_00355b6c_plumes != nullptr)
            {
              PlumeEmitterSpawn fire{};
              fire.riseSpeed = kUGpffff99c0_beamFireRise;
              fire.size = 2.0f;
              fire.x = alive->positionX20;
              fire.y = alive->positionZ24;
              fire.z = 0.0f;
              fire.burstCount = 1;
              fire.lifeUnits = 300;
              fire.cycles = 3;
              fire.mode = 1;
              environment.DAT_00355b6c_plumes->FUN_0021f6e8_open_emitter(fire);
            }
            FUN_00295a60_cue(kMastBeamHitCue, alive, environment);
            FUN_0029e078_throw_debris(
                0.0f, Vec3{alive->positionX20, alive->positionZ24, alive->positionY28}, 5,
                environment);
            release(work.beamChain[index]);
          }
        }
        for (const std::int32_t entry : work.beamChain)
        {
          if (entry != 0)
          {
            return;
          }
        }
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kUGpffff99c4_beamEndShake, 300);
        }
        // FUN_0023BBD8(0, 0): the rumble.
        if (environment.set_DAT_0035567c_fogBand)
        {
          environment.set_DAT_0035567c_fogBand(work.fogNearSave25c, work.fogFarSave260);
        }
        if (environment.DAT_00355b6c_plumes != nullptr)
        {
          PlumeEmitterSpawn column{};
          column.riseSpeed = kUGpffff99c8_columnRise;
          column.size = 2.0f;
          if (OriginalEntity *blast = slotOf(work.blast4e8))
          {
            column.x = blast->positionX20;
          }
          column.y = 0.0f;
          column.z = 0.0f;
          column.burstCount = 3;
          column.lifeUnits = 400;
          column.cycles = 99;
          column.mode = 1;
          environment.DAT_00355b6c_plumes->FUN_0021f6e8_open_emitter(column);
        }
        entity.mastPhase1b0 = 9;
        FUN_00298160_mast_camera(-1, 0, 0, &entity, environment);
        if (work.byte01_carryMode == 0)
        {
          // The carry: work byte 1 is what FUN_0029D658 runs off, and the two
          // ambient gates come back up with it.
          work.byte01_carryMode = 1;
          DAT_003555d1_suspendPushOut() = true;
          DAT_00354cb8_ambientGateB() = 1;
          DAT_00354ca8_ambientGateA() = 1;
        }
        release(work.blast4e8);
        return;
      }
      case 9:
        entity.mastPhase1b0 = 10;
        return;
      case 10:
        FUN_00298160_mast_camera(8, 1, 0, &entity, environment);
        if (FUN_0029d498_break_section(entity.mastSwoopStage1bf, environment))
        {
          entity.mastPhase1b0 = 11;
        }
        return;
      case 11:
        if (work.byte01_carryMode != 0)
        {
          return;
        }
        entity.mastPhase1b0 = 12;
        DAT_003555d1_suspendPushOut() = false;
        FUN_0029ca28_clear_body_bones(entity, slot, environment);
        return;
      case 12:
      {
        FUN_00298160_mast_camera(7, 1, 0, &entity, environment);
        const std::size_t lane = entity.mastSwoopStage1bf % kDAT_00355338_sectionGroups.size();
        const std::uint8_t bit = kDAT_00355338_sectionGroups[lane];
        entity.fadeLevel134 = 0;
        entity.scale14c = kMastScale;
        entity.scaleZ150 = kMastScale;
        if (environment.FUN_0022dbc8_show_map_primitives)
        {
          environment.FUN_0022dbc8_show_map_primitives(1u << (bit & 0x1Fu), false);
        }
        const std::uint8_t stage = static_cast<std::uint8_t>(entity.mastSwoopStage1bf + 1);
        entity.mastSwoopStage1bf = stage;
        if (stage > 2)
        {
          // **Dead code in the retail build.** The guard at the top of this
          // function gives the turn back from +0x1BF == 2, so the counter never
          // reaches 3 with this block in reach. Ported as written: it stands
          // the player at the origin five up, re-samples his floor, hands his
          // gravity back and puts 999 in his +0xBE.
          player.positionX20 = 0.0f;
          player.positionY28 = 5.0f;
          player.positionZ24 = 0.0f;
          if (environment.terrainSurface)
          {
            const auto surface =
                environment.terrainSurface(0.0f, 0.0f, player.positionY28, player.height58,
                                           player.radius54, player.halfword04, 0);
            if (surface.has_value())
            {
              player.groundHeight4c = surface->height;
            }
          }
          player.halfword04 = static_cast<std::uint16_t>(player.halfword04 & 0xFFF7u);
          player.pendingDamageBe = 999;
          player.previousGroundHeight50 = player.groundHeight4c;
        }
        FUN_0029c468_next_move(entity, environment);
        return;
      }
      default:
        return;
      }
    }

  } // namespace

  std::uint32_t FUN_0029c468_unported_move_frames() { return unportedMoveFrames(); }
  std::uint16_t FUN_0029c468_unported_move_state() { return unportedMoveState(); }

  void FUN_00299390_mast_boss(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment,
                              ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    // :7-11. DAT_0058BFDA is pool slot 0's +0x12A. With the player down the
    // boss does not run at all -- and on its way out it parks the player's
    // ground height at 2.0, which is the one thing it does to the world here.
    OriginalEntity &player = environment.entityPool->slot(0);
    if (static_cast<std::int16_t>(player.staggerTimer12a) < 1)
    {
      player.groundHeight4c = kDownedGroundHeight;
      // DAT_00354CC4 = 0 -- no consumer anywhere in src/ and no field here.
      return;
    }

    // :13-25. The mode byte gate. 12 is a carry in flight, 14 the fight proper.
    if (entity.spawnParam94 == 0x0E)
    {
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        // DAT_00355661 = 0x3C with the whole 0xC9 transform cleared.
        environment.DAT_00343878_frameFeedback->FUN_00264470_set_alpha_and_transform(
            kMastSmearAlpha, 0, 0, 0, 0, 0);
      }
      if (environment.camera != nullptr)
      {
        environment.camera->setRoll(0.0f);
      }
      FUN_0029ccb8_carry_segments(entity, slot, environment);
      FUN_0029ded8_target_markers(entity, environment);
      FUN_0029dfb8_step_projectiles(environment);
      FUN_0029e4d8_step_pools(environment);
    }

    FUN_0029d658_player_carry(entity, environment);

    action14Environment() = &environment;
    ActorEnvironment::BattleActorView view;
    const bool haveRecord = static_cast<bool>(environment.DAT_00354eb4_battleActor) &&
                            environment.DAT_00354eb4_battleActor(entity.battleActorRecord198, view);
    bool publishView = false;
    const bool skipDamage = FUN_002994e0_action_check(entity, view, haveRecord, publishView);
    if (publishView && haveRecord && environment.DAT_00354eb4_setBattleActor)
    {
      environment.DAT_00354eb4_setBattleActor(entity.battleActorRecord198, view);
    }

    if (!skipDamage)
    {
      // FUN_0023A068 inlined, and **only on this arm**: the freeze countdown is
      // inside the `action check said no` branch, not above it.
      const std::int8_t freeze = entity.freezeTimerBd;
      if (freeze != 0)
      {
        entity.freezeTimerBd = static_cast<std::int8_t>(freeze - 1);
        if (freeze != 1)
        {
          return;
        }
      }
      if (entity.pendingDamageBe != 0)
      {
        if (entity.mastFlag1b2 != 0)
        {
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kMastHitCue, entity);
          }
          entity.mastFlashTimer1bc = kMastFlashTicks;
          if (environment.FUN_002d5630_damage_bar)
          {
            environment.FUN_002d5630_damage_bar(true,
                                                static_cast<std::int16_t>(entity.staggerTimer12a),
                                                static_cast<std::int16_t>(entity.maxHitPoints128),
                                                static_cast<std::int16_t>(entity.pendingDamageBe));
          }
          entity.staggerTimer12a = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(entity.staggerTimer12a) -
              static_cast<std::int16_t>(entity.pendingDamageBe));
        }
        entity.hitFlagsC2 = 0;
        entity.pendingDamageBe = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325E50_mastBossStates, kMastBossStateCount, entity.state60);
    // States 1, 7 and 11 are bare `jr ra` in the executable, so they count as
    // ported; 0 and 13 are here. Everything else is still missing.
    // Every one of the fourteen is here now: the three bare `jr ra` entries
    // count as ported, and nothing falls through to the report any more.
    const bool implemented = true;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);

    switch (entity.state60)
    {
    case 0:
      mast_state0(entity, slot, environment);
      break;
    case 1:
    case 7:
    case 11:
      // 0x00299868 / 0x0029A4E0 / 0x0029C190: `jr ra; nop`.
      break;
    case 2:
      mast_state2_hold(entity, slot, environment);
      break;
    case 3:
      mast_orbit_pass(entity, slot, false, environment);
      break;
    case 4:
      mast_orbit_pass(entity, slot, true, environment);
      break;
    case 5:
      mast_state5_dive(entity, slot, environment);
      break;
    case 6:
      mast_state6_descend(entity, slot, environment);
      break;
    case 8:
      mast_state8_strafe(entity, slot, environment);
      break;
    case 9:
      mast_state9_transform(entity, slot, environment);
      break;
    case 10:
      mast_state10_smash(entity, slot, environment);
      break;
    case 12:
      mast_state12_death(entity, slot, environment);
      break;
    case 13:
      FUN_0029c198_state13_intro(entity, environment);
      break;
    default:
      // Unreachable: the table has fourteen entries and all fourteen are above.
      // The counter is kept so that a +0x60 out of range -- which would mean
      // something outside this file wrote it -- says so in the actor report
      // rather than silently doing nothing.
      unportedMoveFrames() = unportedMoveFrames() + 1;
      unportedMoveState() = entity.state60;
      break;
    }

    FUN_0023db50_pump_shake(environment);
  }

} // namespace orphen::ported::entity
