#include "ported/entity/original_crab_boss.h"

#include "ported/entity/original_boss_camera.h"
#include "ported/entity/original_bubble_effect.h"
#include "ported/entity/original_dust_pool.h"
#include "ported/entity/original_water_splash.h"

#include "ported/entity/actor_dispatch_table.h"
#include "ported/battle/battle_tables.h"
#include "ported/entity/original_hit_test.h"

#include <array>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // ------------------------------------------------------------- constants

    // fGpffff91ac at 0x0035311C and fGpffff9210 at 0x00353180: the crab turns
    // ten degrees a frame while it charges and two while it carries, both
    // multiplied by the frame tick and by 0.03125 the way every turn rate is.
    inline constexpr float kFGpffff91ac_chargeTurnRate = 0.174532890319824f;
    inline constexpr float kFGpffff9210_carryTurnRate = 0.0349065996706486f;
    // DAT_003531b4, the swipe's own rate. Ten degrees again, and kept apart for
    // the same reason the other four in this game are: it is its own word.
    inline constexpr float kDAT_003531b4_swipeTurnRate = 0.174532890319824f;
    // DAT_003531b8 at 0x003531B8: the magnitude FUN_0022dcf0 shakes with when a
    // swipe connects, over 0x32 ticks.
    inline constexpr float kDAT_003531b8_swipeShake = 0.0500000007450581f;
    inline constexpr std::int16_t kFUN_0027c458_shakeTicks = 0x32;

    // DAT_00353118 at 0x00353118: the fraction of maximum hit points the crab
    // has to lose before it sheds a leg -- three tenths, so two thresholds.
    inline constexpr float kDAT_00353118_damageThreshold = 0.300000011920929f;
    // fGpffff9260 / fGpffff9264 at 0x003531D0 / 0x003531D4: and the two health
    // fractions FUN_0027ccd0 blows the legs off at, six tenths and three.
    inline constexpr float kFGpffff9260_firstLegHealth = 0.600000023841858f;
    inline constexpr float kFGpffff9264_secondLegHealth = 0.300000011920929f;

    // The crab's own three cues. 0x11A is the hit, 0xAF and 0xAE the two legs
    // coming off, and 0x114..0x119 the animation cues FUN_0027ef40 keys.
    inline constexpr std::uint16_t kFUN_00279298_hitCue = 0x11A;
    inline constexpr std::uint16_t kFUN_0027ccd0_firstLegCue = 0xAF;
    inline constexpr std::uint16_t kFUN_0027ccd0_secondLegCue = 0xAE;

    // FUN_0027ce48's splash: 0x140 ticks between them, and it only runs while
    // the crab is below -2.0 in Y -- in the water.
    inline constexpr std::uint16_t kFUN_0027ce48_splashPeriod = 0x140;
    inline constexpr float kFUN_0027ce48_waterLine = -2.0f;
    inline constexpr std::int32_t kFUN_0027ce48_splashCount = 10;

    // FUN_0027c8a0's body box: 1.5 either side of the crab, and it only sweeps
    // while the crab is below -4.5 -- deep enough that it is the *body* doing
    // the damage rather than a limb.
    inline constexpr float kFUN_0027c8a0_bodyReach = 1.5f;
    inline constexpr float kFUN_0027c8a0_bodyDepth = -4.5f;
    inline constexpr std::uint32_t kFUN_0027c8a0_contactMask = 0xE0u;

    // FUN_00279298's flash: 0x1900 ticks of invulnerability after a hit, and
    // 0x14C8 in the fade word while it runs.
    inline constexpr std::uint16_t kFUN_00279298_flinchTicks = 0x1900;
    inline constexpr std::uint32_t kFUN_00279298_flashColour = 0x14C8u;

    // FUN_00279298's own gate: the crab does nothing at all while script work
    // word 0 has passed 3000, which is the beat the fight is over on.
    inline constexpr std::uint32_t kFUN_00279298_beatCeiling = 3000;

    // State 13's own constants, all gp-relative words in 0x00353180..0x003531B0.
    // fGpffff9214 / 9224 / 9228 / 9238 are all 1.5708 -- the ninety degrees the
    // crab turns through at each step of the carry -- and are kept apart because
    // they are four separate words.
    inline constexpr float kFGpffff9214_quarterTurn = 1.57079637050629f;
    inline constexpr float kFGpffff9224_quarterTurn = 1.57079637050629f;
    inline constexpr float kFGpffff9228_quarterTurn = 1.57079637050629f;
    inline constexpr float kFGpffff9238_quarterTurn = 1.57079637050629f;
    // uGpffff9218 / 921c / 9220: where the victim sits once it is picked up --
    // (0, -0.65, -0.1) in the crab's own frame, rolled a quarter turn.
    inline constexpr float kUGpffff9218_holdY = -0.649999976158142f;
    inline constexpr float kUGpffff921c_holdZ = -0.100000001490116f;
    inline constexpr float kUGpffff9220_holdRoll = -1.57079637050629f;
    // uGpffff922c / 9230 / 9234: and where the crab plants itself for the hurl.
    inline constexpr float kUGpffff922c_hurlX = 1.33500003814697f;
    inline constexpr float kUGpffff9230_hurlY = -6.40000009536743f;
    inline constexpr float kUGpffff9234_hurlZ = -0.800000011920929f;
    // uGpffff923c / 9240: and where the three cinematic entities are parked.
    inline constexpr float kUGpffff923c_watchX = 1.20000004768372f;
    inline constexpr float kUGpffff9240_watchY = -3.20000004768372f;

    // The crab's two carry speeds and the four holds state 13 counts through.
    inline constexpr float kFUN_0027bbe0_walkSpeed = 50.0f;
    inline constexpr float kFUN_0027bbe0_carrySpeed = 20.0f;
    inline constexpr std::uint16_t kFUN_0027bbe0_pickUpHold = 0x0640;
    inline constexpr std::uint16_t kFUN_0027bbe0_turnHold = 0x0280;
    inline constexpr std::uint16_t kFUN_0027bbe0_hurlHold = 0x1900;
    inline constexpr std::uint16_t kFUN_0027bbe0_watchHold = 0x12C0;
    inline constexpr std::uint16_t kFUN_0027bbe0_beatHold = 0x0780;
    // The bone the victim rides while it is held, and the cue for the hurl.
    inline constexpr std::uint8_t kFUN_0027bbe0_carryBone = 0x12;
    inline constexpr std::uint16_t kFUN_0027bbe0_hurlCue = 0x114;
    // The three actor tags the cinematic borrows: the two watchers and the one
    // whose clip is started as each beat lands.
    inline constexpr std::int16_t kFUN_0027bbe0_watcherA = 0x2E;
    inline constexpr std::int16_t kFUN_0027bbe0_watcherB = 0x2F;
    inline constexpr std::int16_t kFUN_0027bbe0_watcherC = 0x30;

    // State 5's stamp and state 8's station. DAT_003258B0 / B4 is (0, -5), the
    // deep water at the top of the arena; fGpffff91d4 and fGpffff91dc are both
    // pi, the half turn between the facing and the heading when it backs there.
    inline constexpr float kDAT_003258b0_stationX = 0.0f;
    inline constexpr float kDAT_003258b4_stationZ = -5.0f;
    inline constexpr float kFGpffff91d4_halfTurn = 3.14159274101257f;
    inline constexpr float kFGpffff91dc_halfTurn = 3.14159274101257f;
    inline constexpr float kFGpffff91d8_stationTurnRate = 0.174532890319824f;
    inline constexpr float kDAT_0035312c_stampTurnRate = 0.174532890319824f;
    inline constexpr std::uint16_t kFUN_0027a440_stampHold = 0x0C80;

    // FUN_00279d60's slam: half a unit of shake over 500 ticks.
    inline constexpr float kFUN_00279d60_slamShake = 0.5f;
    inline constexpr std::int16_t kFUN_00279d60_slamShakeTicks = 500;

    // FUN_0027c7b8's three move rotations, at DAT_00325910, DAT_00325920 and
    // DAT_003552A0. The crab walks one of them depending on how many legs it
    // has left, and each entry is a *state* id -- the animation argument is
    // always 5, which is the neutral clip every state tests for on its first
    // frame.
    inline constexpr std::array<std::int16_t, 7> kDAT_00325910_openingMoves{
        {2, 3, 8, 5, 2, 8, 3}};
    inline constexpr std::array<std::int16_t, 5> kDAT_00325920_middleMoves{{7, 4, 7, 4, 7}};
    inline constexpr std::array<std::int16_t, 3> kDAT_003552a0_lateMoves{{9, 10, 11}};
    inline constexpr std::uint16_t kFUN_0027c7b8_neutralAnimation = 5;

    // DAT_00355248: the pairs of actor tags the swipe knocks over, two per
    // threshold. FUN_0027c458 reads (stage - 1) * 2 and + 1.
    inline constexpr std::array<std::uint8_t, 8> kDAT_00355248_swipeTargets{
        {0x0E, 0x11, 0x0F, 0x10, 0x0D, 0x12, 0x00, 0x00}};

    // The two globals the swipe ladder counts on. DAT_0035526A counts the
    // swipes; DAT_0035526B latches once the last one has landed. The third,
    // DAT_0035528C -- which of FUN_00277d30's camera sides to use -- lives with
    // the camera director, which is the only other thing that writes it.
    std::uint8_t &DAT_0035526a_swipeCount()
    {
      static std::uint8_t value = 0;
      return value;
    }
    std::uint8_t &DAT_0035526b_swipeDone()
    {
      static std::uint8_t value = 0;
      return value;
    }

    // FUN_0027d230's own tables and state.
    //
    // DAT_00325888: where the player lands after each swipe, two floats per
    // entry indexed by how many swipes have gone. Entry 0 is the origin, which
    // is never used -- the counter has already been raised before the claw can
    // connect.
    inline constexpr std::array<std::array<float, 2>, 4> kDAT_00325888_landings{
        {{{0.0f, 0.0f}},
         {{-1.55200004577637f, -3.08699989318848f}},
         {{0.107999999821186f, -2.06800007820129f}},
         {{-0.0820000022649765f, -0.254000008106232f}}}};
    // DAT_003258E0 / DAT_003258F0: the two scripted runs, each a mid control
    // point and an end point.
    inline constexpr std::array<float, 4> kDAT_003258e0_runA{
        {0.500999987125397f, 3.63899993896484f, -5.0f, 4.0f}};
    inline constexpr std::array<float, 4> kDAT_003258f0_runB{{-14.0f, 4.0f, -15.0f, 4.0f}};
    // DAT_003531F8: the rate the player turns back toward the crab afterwards,
    // 0.5585 -- thirty-two degrees, and already per tick.
    inline constexpr float kDAT_003531f8_recoverTurnRate = 0.558505237102509f;
    inline constexpr std::int16_t kFUN_0027d230_flightTicks = 0x0640;
    inline constexpr std::int16_t kFUN_0027d230_runTicks = 0x1900;
    inline constexpr std::uint16_t kFUN_0027d230_launchCue = 0x57;
    inline constexpr std::uint16_t kFUN_0027d230_landCue = 0x4B;

    std::uint8_t &DAT_0035526c_dodgePhase()
    {
      static std::uint8_t value = 0;
      return value;
    }
    std::uint8_t &DAT_0035526d_runSpot()
    {
      static std::uint8_t value = 0;
      return value;
    }
    // DAT_005737C0 / CC / D8: the three-point arc the player is thrown along,
    // one array per axis -- the same shape every Bezier in the game uses.
    std::array<float, 3> &DAT_005737c0_arcX()
    {
      static std::array<float, 3> value{};
      return value;
    }
    std::array<float, 3> &DAT_005737cc_arcZ()
    {
      static std::array<float, 3> value{};
      return value;
    }
    std::array<float, 3> &DAT_005737d8_arcY()
    {
      static std::array<float, 3> value{};
      return value;
    }
    // DAT_00573798 / A4 / B0: the same shape again, for the entity the crab
    // throws rather than the one it swipes at.
    std::array<float, 3> &DAT_00573798_throwX()
    {
      static std::array<float, 3> value{};
      return value;
    }
    std::array<float, 3> &DAT_005737a4_throwZ()
    {
      static std::array<float, 3> value{};
      return value;
    }
    std::array<float, 3> &DAT_005737b0_throwY()
    {
      static std::array<float, 3> value{};
      return value;
    }

    // `(&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C]` -- the *pending* action byte
    // of the last party control block. 0x0B parks the player's own state machine
    // while the dodge drives them; 6 hands it back. With no battle built yet the
    // blocks are all zero and this is inert, which is exactly what the original
    // does in the same situation.
    void write_control_pending(const ActorEnvironment &environment, std::uint8_t action)
    {
      if (!environment.DAT_0031d3c8_battleTableWord || !environment.DAT_0031d3c8_setBattleTableWord)
      {
        return;
      }
      const std::uint32_t at = orphen::ported::battle::kDAT_0031d7b0_controlBlocks +
                               orphen::ported::battle::control::kPendingAction0e;
      const std::uint32_t aligned = at & ~3u;
      const std::uint32_t shift = (at & 3u) * 8u;
      const std::uint32_t word = environment.DAT_0031d3c8_battleTableWord(aligned);
      environment.DAT_0031d3c8_setBattleTableWord(
          aligned, (word & ~(0xFFu << shift)) | (static_cast<std::uint32_t>(action) << shift));
    }
    void FUN_00249388_hold_player(const ActorEnvironment &environment)
    {
      write_control_pending(environment, 0x0B);
    }
    void FUN_00249388_release_player(const ActorEnvironment &environment)
    {
      write_control_pending(environment, 0x06);
    }

    // ------------------------------------------------------------- shorthands

    // FUN_00305130 / FUN_00305218 are cos and sin.
    float cos_of(float radians) { return std::cos(radians); }
    float sin_of(float radians) { return std::sin(radians); }

    // FUN_0023a4b8: the bearing from one entity to another.
    float FUN_0023a4b8_bearing(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_0023a6d0(speed, entity, xz): how many ticks the crab needs to cover
    // the distance at that speed. The original's `(v << 0x15) >> 0x10` is a
    // truncation to 11 bits followed by a shift up by 5 -- the same 32-ticks-a
    // -frame scale +0x62 counts in.
    std::int16_t FUN_0023a6d0_travel_ticks(float speed,
                                           const OriginalEntity &entity,
                                           float x,
                                           float z)
    {
      const float distance =
          std::sqrt((x - entity.positionX20) * (x - entity.positionX20) +
                    (z - entity.positionZ24) * (z - entity.positionZ24));
      const std::int32_t raw = static_cast<std::int32_t>(distance / (speed / 1000.0f));
      return static_cast<std::int16_t>((raw << 21) >> 16);
    }

    // FUN_0023a9d0: the index of the lowest set bit in the low sixteen, or 0
    // when bit 0 is set or nothing is.
    std::int16_t FUN_0023a9d0_lowest_bit(std::uint32_t mask)
    {
      std::uint32_t index = 0;
      if ((mask & 1u) == 0)
      {
        for (index = 1; index < 0x10 && ((mask & 0xFFFFu & (1u << (index & 0x1Fu))) == 0); ++index)
        {
        }
      }
      return static_cast<std::int16_t>(index < 0x10 ? index : 0);
    }

    // FUN_00215e48: clear the eight words at +0xCC and drop +0x06 bit 0x40.
    // The port models that block as the contact scratch the ground query fills,
    // so the equivalent is to clear the flag; there is nothing else there to
    // zero that anything reads back.
    void FUN_00215e48_clear_contacts(OriginalEntity &entity)
    {
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFBFu);
    }

    // A tick countdown on a 16-bit field, the way every one of these is spelled:
    // subtract, store, and test the *signed* result.
    bool countdown(std::uint16_t &timer, std::uint32_t frameTicks)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(timer) - static_cast<std::int32_t>(frameTicks & 0xFFFFu);
      timer = static_cast<std::uint16_t>(remaining);
      return (remaining * 0x10000) < 0;
    }

    std::array<float, 6> body_box(const OriginalEntity &entity)
    {
      const float reach = kFUN_0027c8a0_bodyReach;
      return {{entity.positionX20 - reach, entity.positionX20 + reach,
               entity.positionZ24 - reach, entity.positionZ24 + reach, entity.positionY28,
               entity.positionY28 + entity.height58}};
    }

    // ----------------------------------------------------- the script contract

    // FUN_0027cef8. Two lines: script work word 1 is 1 or 0. It is the only
    // thing in the game that writes that word, and s14_e001's beats 110 and 150
    // are `while (work[1] == 0)`.
    void FUN_0027cef8_set_script_cue(const ActorEnvironment &environment, bool raised)
    {
      if (environment.DAT_00355060_setScriptWork)
      {
        environment.DAT_00355060_setScriptWork(1, raised ? 1u : 0u);
      }
    }

    // ------------------------------------------------------- the move rotation

    // FUN_0027c7b8: take the next entry of whichever rotation the crab's damage
    // stage selects, fall back to 5 when it is hurt enough that 3 is off the
    // menu, fall back to 1 outright when the player is dead, and roll a fresh
    // camera side every time the rotation wraps.
    void FUN_0027c7b8_pick_next_move(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const std::uint8_t cursor = entity.crabIdleCursor1c4;
      std::int16_t move = 0;
      std::uint8_t last = 0;
      if (entity.crabPhase1c5 == 0)
      {
        move = kDAT_00325910_openingMoves[cursor % kDAT_00325910_openingMoves.size()];
        last = 6;
      }
      else if (entity.crabPhase1c5 == 1)
      {
        move = kDAT_00325920_middleMoves[cursor % kDAT_00325920_middleMoves.size()];
        last = 4;
      }
      else
      {
        move = kDAT_003552a0_lateMoves[cursor % kDAT_003552a0_lateMoves.size()];
        last = 2;
      }

      if ((move == 3 || move == 5) && entity.crabDamageStage1bd > 1)
      {
        move = 5;
      }

      // DAT_0058BFDA is pool slot 0's +0x12A -- the player's hit points. With
      // the player down the crab stops choosing moves and just idles.
      const EntityPool &pool = *environment.entityPool;
      if (static_cast<std::int16_t>(pool.slot(0).staggerTimer12a) < 1)
      {
        entity.crabPhase1c5 = 0;
        move = 1;
      }

      FUN_00225bf0_set_state_and_animation(entity, static_cast<std::uint16_t>(move),
                                           kFUN_0027c7b8_neutralAnimation);

      const std::uint8_t next = static_cast<std::uint8_t>(entity.crabIdleCursor1c4 + 1);
      entity.crabIdleCursor1c4 = next;
      if (next > last)
      {
        const std::uint32_t roll = environment.random ? environment.random() : 0;
        DAT_0035528c_cameraSide() = static_cast<std::uint8_t>((roll & 1u) + 1u);
        entity.crabIdleCursor1c4 = 0;
      }
    }

    // FUN_0027c3e8: the swipe ladder. Drop the script's cue, and either take
    // another swipe -- animation 14, counting -- or, once three have landed,
    // settle on animation 1 and raise the cue so the animatic moves on.
    void FUN_0027c3e8_swipe_ladder(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      FUN_0027cef8_set_script_cue(environment, false);
      if (DAT_0035526a_swipeCount() > 2)
      {
        FUN_00225bf0_set_state_and_animation(entity, 1, 5);
        FUN_0027cef8_set_script_cue(environment, true);
        return;
      }
      FUN_00225bf0_set_state_and_animation(entity, 0x0E, 5);
      DAT_0035526a_swipeCount() = static_cast<std::uint8_t>(DAT_0035526a_swipeCount() + 1);
    }

    // ------------------------------------------------------ the per-frame work

    // FUN_00279180: pick this frame's camera shot, and open or close the two map
    // primitive groups that go with the crab's damage stage.
    //
    // A hidden crab (+0x08 bit 0) gets shot 3, the slow orbit, and nothing else.
    // Otherwise the damage stage picks: stage 0 is shot 1 with the arena open,
    // stage 1 is shot 2 with it closed again, and any other stage is shot 2 on
    // its own. The side each shot comes from is DAT_0035528C, which shot 3 is
    // what rolls over.
    void FUN_00279180_stage_camera(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const auto side = static_cast<std::int16_t>(DAT_0035528c_cameraSide());
      if (((entity.halfword08 ^ 1u) & 1u) == 0)
      {
        FUN_00277d30_boss_camera(3, side, 0, &entity, environment);
        return;
      }
      const std::int8_t phase = entity.crabPhase1c5;
      if (phase != 0 && phase != 1)
      {
        FUN_00277d30_boss_camera(2, side, 0, &entity, environment);
        return;
      }

      const bool opening = (phase == 0);
      FUN_00277d30_boss_camera(opening ? 1 : 2, side, 0, &entity, environment);
      if (environment.FUN_0022dbc8_show_map_primitives)
      {
        environment.FUN_0022dbc8_show_map_primitives(0x20u, opening);
        environment.FUN_0022dbc8_show_map_primitives(0x08u, opening);
      }
      // FUN_0023ab68(0x13) is FUN_00248f18 plus the pool base: the entity tagged
      // 19, whose +0x08 bit 0 this clears on the way in and sets on the way out.
      if (environment.FUN_00248f18_find_by_tag && environment.entityPool != nullptr)
      {
        const std::int32_t found = environment.FUN_00248f18_find_by_tag(0x13);
        if (found > 0 && static_cast<std::size_t>(found) < environment.entityPool->slotCount())
        {
          OriginalEntity &tagged = environment.entityPool->slot(static_cast<std::size_t>(found));
          tagged.halfword08 = static_cast<std::uint16_t>(opening ? (tagged.halfword08 & 0xFFFEu)
                                                                 : (tagged.halfword08 | 1u));
        }
      }
    }

    // FUN_0027CF20's two dust rings. Every constant is a gp word in
    // 0x003531D8..0x003531E8.
    inline constexpr float kFGpffff9268_lowerDrop = 0.300000011920929f;
    inline constexpr float kFGpffff926c_size = 0.800000011920929f;
    inline constexpr float kFGpffff9270_jitterX = 1.60000002384186f;
    inline constexpr float kFGpffff9274_jitterY = 0.100000001490116f;
    inline constexpr float kFGpffff9278_radius = 0.699999988079071f;

    // FUN_0027A440's stamp. DAT_0034E5F0 is the local offset the bone-6 point
    // is taken at, (0, -0.1, -0.2); fGpffff9130 is a full turn, used to place
    // the three ring bubbles at 0, 120 and 240 degrees half a unit out.
    inline constexpr float kDAT_0034e5f0_stampOffsetX = 0.0f;
    inline constexpr float kDAT_0034e5f4_stampOffsetY = -0.100000001490116f;
    inline constexpr float kDAT_0034e5f8_stampOffsetZ = -0.200000002980232f;
    inline constexpr float kDAT_00353130_turn = 6.28318548202515f;
    inline constexpr float kFUN_0027a440_ringRadius = 0.5f;
    inline constexpr std::size_t kFUN_0027a440_bubbleBone = 6;

    // FUN_0027EF40's cue table. Six sounds, all keyed on the animation, the
    // timeline cursor and one of the two contact bits the clip raises.
    inline constexpr std::uint16_t kFUN_0027ef40_slamA = 0x114;
    inline constexpr std::uint16_t kFUN_0027ef40_slamB = 0x115;
    inline constexpr std::uint16_t kFUN_0027ef40_slamC = 0x116;
    inline constexpr std::uint16_t kFUN_0027ef40_stamp = 0x117;
    inline constexpr std::uint16_t kFUN_0027ef40_stepShallow = 0x118;
    inline constexpr std::uint16_t kFUN_0027ef40_stepDeep = 0x119;
    // The depth each clip decides "shallow" at. Walking uses -2, the charge and
    // the backing walk use -4; they are separate immediates in the original.
    inline constexpr float kFUN_0027ef40_walkWaterLine = -2.0f;
    inline constexpr float kFUN_0027ef40_chargeWaterLine = -4.0f;

    // FUN_0027CFE0's own block. fGpffff927c is the fifteen degrees off the
    // crab's facing the victim is thrown along, fGpffff9280 the fifteen degrees
    // it is turned by on the way, and uGpffff9284 the radius of the small ring
    // of splashes it makes when it lands.
    inline constexpr float kFGpffff927c_throwSwing = 0.261799395084381f;
    inline constexpr float kFGpffff9280_victimTurn = 0.261799395084381f;
    inline constexpr float kUGpffff9284_splashRadius = 0.300000011920929f;
    inline constexpr float kFUN_0027cfe0_throwReach = 5.0f;
    inline constexpr float kFUN_0027cfe0_arcApex = 2.0f;
    inline constexpr std::int16_t kFUN_0027cfe0_flightTicks = 0x0C80;
    inline constexpr float kFUN_0027cfe0_flightScale = 3200.0f;
    inline constexpr std::uint16_t kFUN_0027cfe0_splashCue = 0x7C;
    inline constexpr std::uint32_t kFUN_0027cfe0_landedMask = 4u;

    // FUN_0027ce48: the splash, keyed every 0x140 ticks while the crab is in the
    // water and playing one of the six clips that move it through the water.
    void FUN_0027ce48_splash(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      const std::uint16_t animation = entity.animationA0;
      const bool inWaterClip = (static_cast<std::uint16_t>(animation - 2) < 2) || animation == 0x10 ||
                               animation == 0x11 || animation == 0x12 || animation == 0x13 ||
                               animation == 0x0B || animation == 0x0E;
      if (!inWaterClip)
      {
        return;
      }
      if (entity.positionZ24 >= kFUN_0027ce48_waterLine)
      {
        return;
      }
      if (!countdown(entity.crabSplashTimer1ba, environment.frameTicks))
      {
        return;
      }
      entity.crabSplashTimer1ba = kFUN_0027ce48_splashPeriod;
      FUN_002eb500_wade_spray(entity, kFUN_0027ce48_splashCount, environment);
    }

    // FUN_0027CFE0, the thrown pair's flight -- the crab's wrapper runs it every
    // frame its mode byte is 12, which is the whole of the throw.
    //
    // The subject is the entity at +0x1C0, and the gate is that it is no longer
    // riding a bone (+0x192 below 1) and its own state is non-zero: state 13
    // clears the state on entry and sets it to 1 the frame the claw lets go, so
    // this does nothing at all until the hurl.
    //
    // Then four steps, counted in the *victim's* +0x94:
    //
    //   0  build the arc -- from where it is to five units along the crab's
    //      facing plus fifteen degrees, with the control point at the far end
    //      and an apex two units up, landing at zero. Raise +0x04 bit 3.
    //   1  turn the victim to the crab's facing plus fifteen degrees.
    //   2  walk the arc over 0xC80 ticks, writing the three deltas. On the last
    //      tick drop +0x04 bit 3, which hands it back to the physics.
    //   3  wait for the physics to answer "landed" in +0x0C, then two rings of
    //      splashes, the splash cue, and **destroy the victim**.
    //
    // That last step is the only thing that takes the thrown pair off the
    // screen. Without it they hang in the water for the rest of the scene.
    void FUN_0027cfe0_thrown_pair(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      if (entity.crabPartner1c0 < 0 ||
          static_cast<std::size_t>(entity.crabPartner1c0) >= pool.slotCount())
      {
        return;
      }
      const std::size_t victimSlot = static_cast<std::size_t>(entity.crabPartner1c0);
      OriginalEntity &victim = pool.slot(victimSlot);
      if (victim.parentSlot192 >= 1 || victim.state60 == 0)
      {
        return;
      }

      const std::uint8_t phase = victim.spawnParam94;
      if (phase == 0)
      {
        const float heading = entity.facingRadians5c + kFGpffff927c_throwSwing;
        const float endX = victim.positionX20 + cos_of(heading) * kFUN_0027cfe0_throwReach;
        const float endZ = victim.positionZ24 + sin_of(heading) * kFUN_0027cfe0_throwReach;
        victim.fadeRamp62 = 0;
        // DAT_00573798 / A4 / B0, three axes of a quadratic Bezier. The control
        // point is the end point on both horizontal axes, so the victim leaves
        // fast and coasts in; only the height has a real apex.
        DAT_00573798_throwX() = {{victim.positionX20, endX, endX}};
        DAT_005737a4_throwZ() = {{victim.positionZ24, endZ, endZ}};
        DAT_005737b0_throwY() = {{victim.positionY28, kFUN_0027cfe0_arcApex, 0.0f}};
        victim.halfword04 = static_cast<std::uint16_t>(victim.halfword04 | 8u);
        victim.spawnParam94 = static_cast<std::uint8_t>(phase + 1);
        return;
      }

      if (phase == 1)
      {
        victim.facingRadians5c = entity.facingRadians5c + kFGpffff9280_victimTurn;
        victim.spawnParam94 = static_cast<std::uint8_t>(phase + 1);
        return;
      }

      if (phase == 2)
      {
        const auto stepped = static_cast<std::int16_t>(
            static_cast<std::int16_t>(victim.fadeRamp62) +
            static_cast<std::int16_t>(environment.frameTicks & 0xFFFFu));
        victim.fadeRamp62 = static_cast<std::uint16_t>(stepped);
        if (stepped <= kFUN_0027cfe0_flightTicks)
        {
          const float t = static_cast<float>(stepped) / kFUN_0027cfe0_flightScale;
          victim.desiredDeltaX30 =
              FUN_0023a990_bezier(t, DAT_00573798_throwX()) - victim.positionX20;
          victim.desiredDeltaZ34 =
              FUN_0023a990_bezier(t, DAT_005737a4_throwZ()) - victim.positionZ24;
          victim.desiredDeltaY38 =
              FUN_0023a990_bezier(t, DAT_005737b0_throwY()) - victim.positionY28;
        }
        else
        {
          victim.spawnParam94 = static_cast<std::uint8_t>(phase + 1);
          victim.halfword04 = static_cast<std::uint16_t>(victim.halfword04 & 0xFFF7u);
        }
        return;
      }

      if ((victim.collisionFlags0c & kFUN_0027cfe0_landedMask) != 0)
      {
        FUN_002eb398_splash_ring(0.0f, 3.0f, 5.0f, victim, 1, 100, environment);
        FUN_002eb398_splash_ring(kUGpffff9284_splashRadius, 2.0f, 3.0f, victim, 10, 0x32,
                                 environment);
        if (environment.FUN_00267d38_playSound)
        {
          // FUN_00267D88(0x7C, victim, -1). The port's sound path takes no
          // attenuation argument, so the -1 the original passes -- against
          // FUN_00267D38's 100 -- has nowhere to go.
          environment.FUN_00267d38_playSound(kFUN_0027cfe0_splashCue, victim);
        }
        FUN_00265ec0_destroy_entity(victimSlot, environment);
        entity.crabPartner1c0 = -1;
      }
    }

    // FUN_0027CF20, the impact dust: two rings of a hundred puffs each, both
    // centred on the point the claw or the foot came down on. The first is wide
    // and low -- five rings of twenty at 0.8 size, dropped 0.3 below the point;
    // the second is bigger and slower, five rings of ten at 1.5 size dropped
    // half a unit, and lives four times as long.
    //
    // `lit` is the caller's own argument: it picks white over the CLUT-banked
    // grey, and every crab call passes 0.
    void FUN_0027cf20_impact_dust(const orphen::ported::psm2::Vec3 &at,
                                  bool lit,
                                  const ActorEnvironment &environment)
    {
      if (environment.DAT_00355a9c_dust == nullptr)
      {
        return;
      }
      const auto &random = environment.random;
      environment.DAT_00355a9c_dust->FUN_00219af0_spawn_impact(
          at.x, at.y, at.z - kFGpffff9268_lowerDrop, kFGpffff926c_size, kFGpffff9270_jitterX,
          kFGpffff9274_jitterY, 0.5f, 0x50, 5, 0x14, 0, lit, random);
      environment.DAT_00355a9c_dust->FUN_00219af0_spawn_impact(
          at.x, at.y, at.z - 0.5f, 1.5f, kFGpffff926c_size, kFGpffff926c_size,
          kFGpffff9278_radius, 200, 5, 10, 0, lit, random);
    }

    // ------------------------------------------------- the three debris pools
    //
    // FUN_00279940 clears all three at crab init: thirty entries at
    // DAT_005737E8, fifty at DAT_00573860 and thirty at DAT_00573928. Each
    // holds one pool slot; the original holds a pointer and treats null as
    // free, and -1 is that here.
    //
    //   DAT_005737E8  the wreckage FUN_0027E118 drops into the water every
    //                 sixteen thousand ticks, stepped by FUN_0027E5D8
    //   DAT_00573860  the arena's own destructible props, bound at crab init by
    //                 FUN_002797D0 and stepped by FUN_0027E770
    //   DAT_00573928  the chunks a broken prop throws, spawned by FUN_0027E370
    //                 and stepped by FUN_0027EBE0
    //
    // Everything in all three is **retyped to 400**, whose handler is
    // FUN_00239E78, the no-op. That is the point: the crab's wrapper is what
    // steps them, so the actor loop must not.
    inline constexpr std::int32_t kDebrisTypeId = 400;

    std::array<std::int32_t, 30> &DAT_005737e8_wreckPool()
    {
      static std::array<std::int32_t, 30> value{};
      return value;
    }
    std::array<std::int32_t, 50> &DAT_00573860_propPool()
    {
      static std::array<std::int32_t, 50> value{};
      return value;
    }
    std::array<std::int32_t, 30> &DAT_00573928_chunkPool()
    {
      static std::array<std::int32_t, 30> value{};
      return value;
    }

    // FUN_0027E048: the first free entry, or -1. Note it tests entry zero
    // outside the loop and starts the scan at one, which is the same answer by
    // a longer road.
    template <std::size_t N>
    std::int32_t FUN_0027e048_free_entry(const std::array<std::int32_t, N> &pool)
    {
      if (pool[0] < 0)
      {
        return 0;
      }
      for (std::size_t index = 1; index < N; ++index)
      {
        if (pool[index] < 0)
        {
          return static_cast<std::int32_t>(index);
        }
      }
      return -1;
    }

    template <std::size_t N>
    void clear_pool(std::array<std::int32_t, N> &pool)
    {
      pool.fill(-1);
    }

    // FUN_0027E0A0(from, to, speed): how many ticks to cover the gap at that
    // speed. The SQRT of a square is the original's own way of writing an
    // absolute value.
    std::int32_t FUN_0027e0a0_fall_ticks(float from, float to, float speed)
    {
      const float distance = std::sqrt((from - to) * (from - to));
      return static_cast<std::int32_t>(distance / (speed / 1000.0f)) << 5;
    }

    // FUN_0027E118's own countdown, sixteen thousand ticks -- five hundred
    // frames -- between one piece of wreckage and the next.
    inline constexpr std::int16_t kFUN_0027e118_period = 16000;
    inline constexpr float kDAT_00353204_wreckFacing = 3.14159274101257f;
    inline constexpr float kDAT_00353208_wreckHeight = -0.699999988079071f;
    inline constexpr float kFUN_0027e118_spawnX = 7.0f;
    inline constexpr float kFUN_0027e118_fallFrom = 14.0f;
    inline constexpr float kFUN_0027e118_body = 0.25f;

    std::int16_t &DAT_003552a8_wreckTimer()
    {
      static std::int16_t value = 0;
      return value;
    }

    // FUN_0027E118. Drops one piece of wreckage into the water at x = 7, five
    // to nine units up the arena, facing straight back down it. The entity is
    // allocated as a 0x10C-band effect for its model and then retyped, so the
    // only thing that ever moves it is FUN_0027E5D8.
    void FUN_0027e118_drop_wreck(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if ((entity.halfword08 & 1u) != 0)
      {
        return;
      }
      const auto stepped = static_cast<std::int16_t>(
          DAT_003552a8_wreckTimer() - static_cast<std::int16_t>(environment.frameTicks & 0xFFFFu));
      DAT_003552a8_wreckTimer() = stepped;
      if (stepped >= 1)
      {
        return;
      }
      DAT_003552a8_wreckTimer() = kFUN_0027e118_period;

      const std::int32_t entry = FUN_0027e048_free_entry(DAT_005737e8_wreckPool());
      if (entry < 0 || environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const std::size_t slot =
          pool.FUN_00265e28_allocate_and_initialize(0x10B, *environment.descriptors);
      if (slot >= pool.slotCount())
      {
        return;
      }
      OriginalEntity &wreck = pool.slot(slot);

      wreck.animationA0 = 1;
      wreck.facingRadians5c = kDAT_00353204_wreckFacing;
      wreck.positionX20 = kFUN_0027e118_spawnX;
      const float alongZ =
          -5.0f - static_cast<float>((environment.random ? environment.random() : 0) % 5u);
      wreck.scale14c = 1.0f;
      wreck.positionY28 = kDAT_00353208_wreckHeight;
      wreck.positionZ24 = alongZ;
      wreck.scaleZ150 = 1.0f;

      float ground = wreck.positionY28;
      if (environment.terrainSurface)
      {
        const auto surface =
            environment.terrainSurface(wreck.positionX20, alongZ, wreck.positionY28,
                                       wreck.height58, wreck.radius54, wreck.halfword04, 0);
        if (surface.has_value())
        {
          ground = surface->height;
        }
      }
      wreck.groundHeight4c = ground;
      wreck.previousGroundHeight50 = ground;

      wreck.flags06 = static_cast<std::uint16_t>(wreck.flags06 & 0xFFEFu);
      wreck.halfword08 = static_cast<std::uint16_t>(wreck.halfword08 | 0x4000u);
      wreck.halfword04 = 8;
      wreck.descriptorFlags02 = 0x3000;
      wreck.typeId00 = kDebrisTypeId;
      wreck.battleFlags96 = static_cast<std::uint8_t>(wreck.battleFlags96 | 1u);

      // FUN_0025BA98(0x10B, ...) is an out-of-range read: it indexes the object
      // stat groups at `type - 0x272`, which is **negative** for 0x10B, so the
      // original picks up whatever sits before the group and copies one byte of
      // it into +0x12C. There is nothing faithful to reproduce -- the value is
      // whatever the blob happens to hold -- so +0x12C keeps its default and
      // the two DAT_00355D70 globals the same call fills are left alone.
      wreck.staggerTimer12a = 1;
      wreck.height58 = kFUN_0027e118_body;
      wreck.maxHitPoints128 = 1;
      wreck.hitVolumeRadius11c = kFUN_0027e118_body;
      wreck.radius54 = kFUN_0027e118_body;
      wreck.hitVolumeHeight120 = kFUN_0027e118_body;

      const auto speed = static_cast<std::int16_t>(
          static_cast<std::int32_t>(((environment.random ? environment.random() : 0) % 10u) + 5u));
      wreck.debrisSpeed198 = speed;
      wreck.debrisTimer19c = FUN_0027e0a0_fall_ticks(
          wreck.positionY28, kFUN_0027e118_fallFrom, static_cast<float>(speed));
      wreck.debrisKind1d4 = 8;
      DAT_005737e8_wreckPool()[static_cast<std::size_t>(entry)] = static_cast<std::int32_t>(slot);
    }

    // FUN_0027E370(lift, at, count): the chunks a broken prop throws. Each is a
    // shared-band prop -- DAT_003552B0 holds two type ids, 0x49 and 0x4A above
    // 0x272 -- retyped to 400 and given a random spin, a random lifetime of one
    // to three times 0xC80, and a fall from -56 that its own step never lets it
    // reach.
    inline constexpr std::array<std::uint8_t, 2> kDAT_003552b0_chunkTypes{{0x49, 0x4A}};
    inline constexpr float kDAT_0035320c_chunkTurn = 6.28318548202515f;
    inline constexpr float kDAT_00353210_chunkGravity = 0.000250000011874363f;
    inline constexpr float kFUN_0027e370_chunkFall = -56.0f;

    void FUN_0027e370_throw_chunks(float lift,
                                   const orphen::ported::psm2::Vec3 &at,
                                   std::int16_t count,
                                   const ActorEnvironment &environment)
    {
      if (count <= 0 || environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      for (std::int16_t index = 0; index < count; ++index)
      {
        const std::uint32_t pick = environment.random ? environment.random() : 0;
        const std::uint8_t kind = kDAT_003552b0_chunkTypes[pick & 1u];
        const std::int32_t entry = FUN_0027e048_free_entry(DAT_00573928_chunkPool());
        if (entry < 0)
        {
          return;
        }
        const std::size_t slot = pool.FUN_00265e28_allocate_and_initialize(
            static_cast<std::int32_t>(kind) + 0x272, *environment.descriptors);
        if (slot >= pool.slotCount())
        {
          continue;
        }
        OriginalEntity &chunk = pool.slot(slot);

        chunk.animationA0 = 0;
        const auto spin = static_cast<std::uint32_t>(
            ((environment.random ? environment.random() : 0) % 0x24u) * 10u);
        chunk.facingRadians5c = (static_cast<float>(spin) * kDAT_0035320c_chunkTurn) / 360.0f;
        chunk.positionX20 = at.x;
        chunk.flags06 = static_cast<std::uint16_t>(chunk.flags06 & 0xFFEFu);
        chunk.positionZ24 = at.y;
        chunk.halfword04 = 0x11;
        chunk.scaleZ150 = 0.5f;
        chunk.scale14c = 0.5f;
        chunk.previousGroundHeight50 = kFUN_0027e370_chunkFall;
        chunk.groundHeight4c = kFUN_0027e370_chunkFall;
        chunk.positionY28 = at.z;
        chunk.typeId00 = kDebrisTypeId;
        chunk.desiredDeltaY38 = kDAT_00353210_chunkGravity;
        chunk.fadeRamp62 = static_cast<std::uint16_t>(
            (static_cast<std::int16_t>((environment.random ? environment.random() : 0) % 3u) + 1) *
            0x0C80);
        chunk.debrisSpeed198 = static_cast<std::int16_t>(
            static_cast<std::int32_t>(((environment.random ? environment.random() : 0) % 3u) + 1u));
        // The original stores this one as a float hundredth at +0x44 and reads
        // it back nowhere the port can see; the speed at +0x198 is what
        // FUN_0027EBE0 actually uses.
        DAT_00573928_chunkPool()[static_cast<std::size_t>(entry)] =
            static_cast<std::int32_t>(slot);
        (void)lift;
      }
    }

    // FUN_0027EBE0: one chunk. It slides along its own facing until its timer
    // runs out or it hits anything, trailing one dust puff a frame, and then it
    // is gone.
    inline constexpr float kFGpffff92b0_chunkDust = 0.100000001490116f;
    inline constexpr std::uint32_t kFUN_0027ebe0_stopMask = 0x4006u;
    inline constexpr std::uint32_t kFUN_0027ebe0_entityMask = 0xE0u;

    void FUN_0027ebe0_step_chunk(std::int32_t &entry, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &chunk = pool.slot(static_cast<std::size_t>(entry));
      const auto remaining = static_cast<std::int16_t>(
          static_cast<std::int16_t>(chunk.fadeRamp62) -
          static_cast<std::int16_t>(environment.frameTicks & 0xFFFFu));
      chunk.fadeRamp62 = static_cast<std::uint16_t>(remaining);

      if (remaining >= 0 && (chunk.collisionFlags0c & kFUN_0027ebe0_stopMask) == 0 &&
          (chunk.collisionFlags0c & kFUN_0027ebe0_entityMask) == 0)
      {
        const float travel = (static_cast<float>(chunk.debrisSpeed198) *
                              static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
                             32000.0f;
        chunk.desiredDeltaX30 = travel * cos_of(chunk.facingRadians5c);
        chunk.desiredDeltaZ34 = travel * sin_of(chunk.facingRadians5c);
        if (environment.DAT_00355a9c_dust != nullptr)
        {
          const auto size = static_cast<float>(
                                ((environment.random ? environment.random() : 0) % 10u) + 0x1Eu) /
                            100.0f;
          environment.DAT_00355a9c_dust->FUN_0021a170_spawn_one(
              chunk.positionX20, chunk.positionZ24, chunk.positionY28, size, 0.0f,
              kFGpffff92b0_chunkDust, 0x28, 1, 1, true, environment.random);
        }
        return;
      }
      FUN_00265ec0_destroy_entity(static_cast<std::size_t>(entry), environment);
      entry = -1;
    }

    // FUN_0027E5D8: one piece of wreckage. While its kind byte has bit 3 it
    // runs a timer down and slides; when that expires it becomes a hazard --
    // one hit point, a fresh clip, cue 0xC0, a burst of chunks and two rings of
    // splash -- and is then released either way.
    inline constexpr std::uint16_t kFUN_0027e5d8_dropCue = 0xC0;

    void FUN_0027e5d8_step_wreck(std::int32_t &entry, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      const auto slot = static_cast<std::size_t>(entry);
      OriginalEntity &wreck = pool.slot(slot);

      if ((wreck.debrisKind1d4 & 8u) == 0)
      {
        // Not a live piece any more: drop it from the pool once it has no hit
        // points left.
        if (static_cast<std::int16_t>(wreck.staggerTimer12a) < 1)
        {
          entry = -1;
        }
        return;
      }

      const std::int32_t remaining =
          wreck.debrisTimer19c - static_cast<std::int32_t>(environment.frameTicks);
      wreck.debrisTimer19c = remaining;
      if (remaining >= 0)
      {
        if (wreck.pendingDamageBe == 0)
        {
          const float travel = (static_cast<float>(wreck.debrisSpeed198) *
                                static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
                               32000.0f;
          wreck.desiredDeltaX30 = travel * cos_of(wreck.facingRadians5c);
          wreck.desiredDeltaZ34 = travel * sin_of(wreck.facingRadians5c);
          return;
        }
        // Struck. It stops being an actor, plays its cue, throws chunks and
        // splashes twice.
        wreck.freezeTimerBd = 0;
        wreck.attackPower12c = 0x32; // +0x12C
        wreck.debrisFlagged19a = 1;
        wreck.debrisSpeed198 = 0x10;
        wreck.debrisFlagged19b = 0;
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0027e5d8_dropCue, wreck);
        }
        const auto count = static_cast<std::int16_t>(
            (((environment.random ? environment.random() : 0) % 3u) * 2u) + 5u);
        FUN_0027e370_throw_chunks(
            2.0f, orphen::ported::psm2::Vec3{wreck.positionX20, wreck.positionZ24, wreck.positionY28},
            count, environment);
        FUN_002eb398_splash_ring(0.0f, 7.0f, 8.0f, wreck, 2, 100, environment);
        FUN_002eb398_splash_ring(0.5f, 5.0f, 6.0f, wreck, 10, 0x32, environment);
      }
      // FUN_00248040 unbinds it from the battle actor table; the port's battle
      // path owns that table, and a piece of wreckage never bound into it.
      FUN_00265ec0_destroy_entity(slot, environment);
      entry = -1;
    }

    // FUN_002EF688(lift, at, lit, kind): the burst a breaking thing makes.
    // Four kinds, and only the first two are dust -- kinds 2 and 3 go to
    // FUN_0021F6E8, a fifth particle system the port does not have. Both of
    // those are reached only from FUN_0027E770, and both are noted where they
    // occur rather than approximated with the wrong pool.
    inline constexpr float kDAT_00354b24_size0 = 0.600000023841858f;
    inline constexpr float kDAT_00354b28_jitter0 = 0.800000011920929f;
    inline constexpr float kDAT_00354b2c_lift0 = 0.100000001490116f;
    inline constexpr float kDAT_00354b30_size0b = 1.20000004768372f;
    inline constexpr float kDAT_00354b34_jitter1 = 0.100000001490116f;
    inline constexpr float kDAT_00354b38_size1 = 0.300000011920929f;
    inline constexpr float kDAT_00354b3c_jitter1b = 0.800000011920929f;
    inline constexpr float kDAT_00354b40_size1b = 0.600000023841858f;

    void FUN_002ef688_break_burst(float lift,
                                  const orphen::ported::psm2::Vec3 &at,
                                  bool lit,
                                  int kind,
                                  const ActorEnvironment &environment)
    {
      if (environment.DAT_00355a9c_dust == nullptr)
      {
        return;
      }
      auto &dust = *environment.DAT_00355a9c_dust;
      const auto &random = environment.random;
      const float z = at.z + lift;
      if (kind == 0)
      {
        dust.FUN_00219af0_spawn_impact(at.x, at.y, z, kDAT_00354b24_size0, kDAT_00354b28_jitter0,
                                       kDAT_00354b2c_lift0, 0.5f, 0x50, 5, 10, 0, lit, random);
        dust.FUN_00219af0_spawn_impact(at.x, at.y, z, kDAT_00354b30_size0b, kDAT_00354b28_jitter0,
                                       kDAT_00354b28_jitter0, kDAT_00354b24_size0, 100, 5, 10, 0,
                                       lit, random);
        return;
      }
      if (kind == 1)
      {
        dust.FUN_00219af0_spawn_impact(at.x, at.y, z, kDAT_00354b38_size1, kDAT_00354b3c_jitter1b,
                                       kDAT_00354b34_jitter1, 0.5f, 0x50, 5, 10, 0, lit, random);
        dust.FUN_00219af0_spawn_impact(at.x, at.y, z, kDAT_00354b40_size1b, kDAT_00354b40_size1b,
                                       kDAT_00354b40_size1b, kDAT_00354b34_jitter1, 100, 5, 10, 0,
                                       lit, random);
        return;
      }
      // Kinds 2 and 3: FUN_0021F6E8 with (0.01, 1.5) and (0.01, 0.7). Not
      // ported -- see the note above.
    }

    // FUN_002797D0(tags, animation, flags, count): bind the arena's own
    // destructible props into the prop pool at crab init. Each tag names a
    // placement the script has already put down; this gives it one hit point,
    // clears the bits that make it solid furniture, stamps the kind byte the
    // stepper switches on, and -- unless the kind has bit 4 -- retypes it to
    // 400 so the actor loop leaves it alone.
    //
    // The four calls, straight out of FUN_00279940:
    //
    //   tags 20..23  animation -1  kind 0x01   the lamps, which carry a light
    //   tags 30..34  animation  0  kind 0x02   the crates
    //   tags 35..39  animation  0  kind 0x04   the barrels
    //   tags 41..42  animation  0  kind 0x10   the two that keep their own type
    inline constexpr float kFUN_002797d0_lightRadius = 3.0f;

    void FUN_002797d0_bind_props(const std::uint8_t *tags,
                                 std::int16_t animation,
                                 std::uint16_t kind,
                                 std::int16_t count,
                                 const ActorEnvironment &environment)
    {
      if (!environment.FUN_00248f18_find_by_tag || environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      for (std::int16_t index = 0; index < count; ++index)
      {
        const std::int32_t entry = FUN_0027e048_free_entry(DAT_00573860_propPool());
        if (entry < 0)
        {
          return;
        }
        const std::int32_t found =
            environment.FUN_00248f18_find_by_tag(static_cast<std::int16_t>(tags[index]));
        if (found <= 0 || static_cast<std::size_t>(found) >= pool.slotCount())
        {
          continue;
        }
        OriginalEntity &prop = pool.slot(static_cast<std::size_t>(found));
        prop.maxHitPoints128 = 1;
        prop.flags06 = static_cast<std::uint16_t>(prop.flags06 & 0xFFEFu);
        prop.halfword04 = static_cast<std::uint16_t>(prop.halfword04 & 0xFFEEu);
        prop.staggerTimer12a = 1;
        prop.descriptorFlags02 = 0x3000;
        prop.debrisKind1d4 = kind;
        if (animation >= 0)
        {
          FUN_00225bc8_set_animation(prop, static_cast<std::uint16_t>(animation));
        }
        if ((kind & 0x10u) == 0)
        {
          prop.typeId00 = kDebrisTypeId;
        }
        if ((kind & 1u) != 0 && environment.DAT_00343888_lights != nullptr)
        {
          // FUN_0023AA18 then FUN_0023AAB0: take a light slot from three up,
          // give it the prop's colour, and put it where the prop is.
          const std::int32_t light =
              environment.DAT_00343888_lights->FUN_00266008_allocateFromThree();
          if (light > 0)
          {
            prop.lightSlot195 = static_cast<std::int8_t>(light);
            auto &lamp = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(light));
            lamp.red = 0xDC;
            lamp.green = 0xF0;
            lamp.blue = 0x00;
            // FUN_0023AAB0 with a non-negative bone takes the entity's own
            // position rather than a bone point, which is what the bind does.
            lamp.x = prop.positionX20;
            lamp.y = prop.positionZ24;
            lamp.z = prop.positionY28;
            environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(light),
                                                        kFUN_002797d0_lightRadius);
          }
        }
        DAT_00573860_propPool()[static_cast<std::size_t>(entry)] = found;
      }
    }

    // FUN_0027E770: one destructible prop. Which of three shapes it takes is
    // the kind byte at +0x1D4, and every one of them is driven by +0xBE -- the
    // pending damage the hit test writes.
    //
    //   bit 0  a lamp. Its light follows bone 1 while it stands; a hit plays
    //          cue 0xBF and drops it to animation 4, and when that clip ends it
    //          scatters ten sparks of type 0x47 (retyped 0x1A3) and is gone.
    //   bit 1  a crate. A hit plays cue 0x11D, throws chunks, and bursts; the
    //          entity stays in the world on animation 2 and only leaves the
    //          pool.
    //   bit 2  a barrel. A hit plays cue 0xC3 and bursts, and the clip ending
    //          destroys it.
    inline constexpr std::uint16_t kFUN_0027e770_lampCue = 0xBF;
    inline constexpr std::uint16_t kFUN_0027e770_crateCue = 0x11D;
    inline constexpr std::uint16_t kFUN_0027e770_barrelCue = 0xC3;
    inline constexpr float kDAT_00353214_sparkTurn = 6.28318548202515f;
    inline constexpr float kDAT_00353218_sparkBody = 0.100000001490116f;
    inline constexpr float kDAT_0035321c_crateLift = 0.699999988079071f;
    inline constexpr std::int32_t kFUN_0027e770_sparkType = 0x1A3;

    void FUN_0027e770_step_prop(std::int32_t &entry, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      const auto slot = static_cast<std::size_t>(entry);
      OriginalEntity &prop = pool.slot(slot);
      const std::uint16_t kind = prop.debrisKind1d4;

      const auto randomCount = [&]() {
        return static_cast<std::int16_t>(
            (((environment.random ? environment.random() : 0) % 3u) * 2u) + 5u);
      };
      const orphen::ported::psm2::Vec3 at{prop.positionX20, prop.positionZ24, prop.positionY28};

      if ((kind & 1u) != 0)
      {
        if (prop.animationA0 == 2)
        {
          // FUN_0023AAB0(prop, 1): the light rides bone 1 while the lamp stands.
          if (prop.lightSlot195 > 0 && environment.DAT_00343888_lights != nullptr)
          {
            // FUN_0023AAB0(prop, 1). The bone argument is non-negative, so it
            // takes the entity's own +0x20 rather than a bone point.
            auto &lamp = environment.DAT_00343888_lights->slot(
                static_cast<std::uint32_t>(prop.lightSlot195));
            lamp.x = prop.positionX20;
            lamp.y = prop.positionZ24;
            lamp.z = prop.positionY28;
          }
          if (prop.pendingDamageBe == 0)
          {
            return;
          }
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027e770_lampCue, prop);
          }
          prop.staggerTimer12a = 0;
          prop.hitFlagsC2 = 0;
          prop.pendingDamageBe = 0;
          FUN_00225bc8_set_animation(prop, 4);
          return;
        }
        if ((prop.flags06 & 1u) == 0)
        {
          return;
        }
        // The clip has come round: ten sparks around bone 1, each on its own
        // heading within thirty degrees of the lamp's facing and up to a unit
        // out.
        if (environment.FUN_0020dc88_bone_point && environment.descriptors != nullptr)
        {
          const auto origin =
              environment.FUN_0020dc88_bone_point(slot, 1, orphen::ported::psm2::Vec3{});
          for (int index = 0; index < 10; ++index)
          {
            const std::uint32_t sign = environment.random ? environment.random() : 0;
            const auto swing = static_cast<float>(
                (environment.random ? environment.random() : 0) % 0x1Eu);
            const float heading =
                (sign & 1u) == 0
                    ? prop.facingRadians5c - (swing * kDAT_00353214_sparkTurn) / 360.0f
                    : prop.facingRadians5c + (swing * kDAT_00353214_sparkTurn) / 360.0f;
            const auto reach =
                static_cast<float>((environment.random ? environment.random() : 0) % 100u) / 100.0f;
            const std::size_t sparkSlot = pool.FUN_00265e28_allocate_and_initialize(
                0x47, *environment.descriptors);
            if (sparkSlot >= pool.slotCount())
            {
              continue;
            }
            OriginalEntity &spark = pool.slot(sparkSlot);
            spark.typeId00 = kFUN_0027e770_sparkType;
            spark.positionX20 = origin.x + reach * cos_of(heading);
            spark.positionZ24 = origin.y + reach * sin_of(heading);
            spark.positionY28 = origin.z;
            spark.radius54 = kDAT_00353218_sparkBody;
            spark.height58 = kDAT_00353218_sparkBody;
            spark.facingRadians5c = heading;
            spark.groundHeight4c = prop.groundHeight4c;
          }
        }
        FUN_00265ec0_destroy_entity(slot, environment);
        entry = -1;
        return;
      }

      if ((kind & 2u) != 0)
      {
        if (prop.animationA0 == 0)
        {
          if (prop.pendingDamageBe == 0)
          {
            return;
          }
          prop.staggerTimer12a = 0;
          FUN_00225bc8_set_animation(prop, 1);
          prop.hitFlagsC2 = 0;
          prop.pendingDamageBe = 0;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027e770_crateCue, prop);
          }
          const float lift = prop.height58 * kDAT_0035321c_crateLift;
          FUN_0027e370_throw_chunks(lift, at, randomCount(), environment);
          // FUN_002EF688(lift, at, 1, 3) -- kind 3 is FUN_0021F6E8's pool,
          // which the port does not have.
          FUN_002ef688_break_burst(lift, at, true, 3, environment);
          return;
        }
        if (prop.animationA0 != 1 || (prop.flags06 & 1u) == 0)
        {
          return;
        }
        // The crate stays in the world on animation 2; only the pool lets go.
        FUN_00225bc8_set_animation(prop, 2);
        entry = -1;
        return;
      }

      if ((kind & 4u) == 0)
      {
        return;
      }
      if (prop.animationA0 == 0)
      {
        if (prop.pendingDamageBe == 0)
        {
          return;
        }
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0027e770_barrelCue, prop);
        }
        prop.staggerTimer12a = 0;
        prop.hitFlagsC2 = 0;
        prop.pendingDamageBe = 0;
        FUN_00225bc8_set_animation(prop, 1);
        FUN_0027e370_throw_chunks(0.0f, at, randomCount(), environment);
        // FUN_002EF688(0, at, 1, 2) -- kind 2, also FUN_0021F6E8's pool.
        FUN_002ef688_break_burst(0.0f, at, true, 2, environment);
        return;
      }
      if ((prop.flags06 & 1u) == 0)
      {
        return;
      }
      FUN_00265ec0_destroy_entity(slot, environment);
      entry = -1;
    }

    // FUN_0027ED58: the walk. Which of the first two pools gets stepped depends
    // on whether the crab is hidden -- the props only tick while it is, the
    // wreckage only while it is not -- and the chunks tick either way. Then the
    // three entities the crab is holding at +0x1AC, which is where the drop
    // beat lives.
    inline constexpr std::uint16_t kFUN_0027ed58_dropCue = 0xC0;

    void FUN_0027ed58_step_debris(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr)
      {
        return;
      }
      EntityPool &pool = *environment.entityPool;
      const bool crabHidden = ((entity.halfword08 ^ 1u) & 1u) == 0;

      if (crabHidden)
      {
        for (std::int32_t &slot : DAT_00573860_propPool())
        {
          if (slot >= 0 && static_cast<std::size_t>(slot) < pool.slotCount() &&
              pool.slot(static_cast<std::size_t>(slot)).typeId00 == kDebrisTypeId)
          {
            FUN_0027e770_step_prop(slot, environment);
          }
        }
      }
      else
      {
        for (std::int32_t &slot : DAT_005737e8_wreckPool())
        {
          if (slot >= 0 && static_cast<std::size_t>(slot) < pool.slotCount())
          {
            FUN_0027e5d8_step_wreck(slot, environment);
          }
        }
      }
      for (std::int32_t &slot : DAT_00573928_chunkPool())
      {
        if (slot >= 0 && static_cast<std::size_t>(slot) < pool.slotCount())
        {
          FUN_0027ebe0_step_chunk(slot, environment);
        }
      }

      // The three held slots. Each raises the smear while it is occupied, puts
      // anything that has just been hit back on its walk clip, and -- on the
      // fourth frame of the walk with the claw bit up -- is let go: cue 0xC0, a
      // burst of chunks, a dust ring, and the slot cleared.
      for (std::size_t index = 0; index < entity.crabHeld1ac.size(); ++index)
      {
        const std::int32_t held = entity.crabHeld1ac[index];
        if (held < 0 || static_cast<std::size_t>(held) >= pool.slotCount())
        {
          continue;
        }
        OriginalEntity &carried = pool.slot(static_cast<std::size_t>(held));
        if (environment.DAT_00343878_frameFeedback != nullptr)
        {
          environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
        }
        if (carried.pendingDamageBe != 0)
        {
          carried.hitFlagsC2 = 0;
          carried.pendingDamageBe = 0;
          carried.freezeTimerBd = 0;
          carried.halfword04 = static_cast<std::uint16_t>(carried.halfword04 | 0x10u);
          FUN_00225bc8_set_animation(carried, 2);
        }
        if (carried.animationA0 == 2 && carried.timelineCursorA8 == 0 &&
            (carried.flags06 & 8u) != 0)
        {
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027ed58_dropCue, entity);
          }
          const auto count = static_cast<std::int16_t>(
              (((environment.random ? environment.random() : 0) % 3u) * 2u) + 5u);
          const orphen::ported::psm2::Vec3 at{carried.positionX20, carried.positionZ24,
                                              carried.positionY28};
          FUN_0027e370_throw_chunks(2.0f, at, count, environment);
          FUN_002ef688_break_burst(0.0f, at, true, 1, environment);
          entity.crabHeld1ac[index] = -1;
          return;
        }
      }
    }

    // FUN_0027EF40, the crab's whole sound layer. The wrapper runs it every
    // frame, outside the mode gate and outside the beat ceiling, so it is the one
    // piece of the crab that is audible from the moment the scene loads.
    //
    // Every cue is a clip keyframe: the animation says which move, the timeline
    // cursor says which frame of it, and a contact bit says the frame is the one
    // the foot or the claw actually lands on. Bit 8 is the claw, bit 4 the legs.
    //
    //   animation 8   the slam       cursor 0 / 6 / 8 -> 0x114 / 0x115 / 0x116
    //   animation 9   the stamp      cursor 0         -> 0x117
    //   animation 2   the walk       cursor 0         -> 0x118 above -2, else 0x119
    //   animation 3   the charge     cursor 4         -> 0x118 above -4, else 0x119
    //   animation 0x19 backing up    cursor 4         -> 0x118 above -4, else 0x119
    //
    // Not ported: FUN_0027F1A8, which animations 0, 1 and 3 also reach. It puts
    // two puffs on the crab's bones 2 and 3 through FUN_0021F6E8, the same
    // particle spawner the status aura wants and the port has no equivalent for.
    void FUN_0027ef40_crab_sounds(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (!environment.FUN_00267d38_playSound)
      {
        return;
      }
      const auto play = [&](std::uint16_t cue) { environment.FUN_00267d38_playSound(cue, entity); };
      const std::uint16_t animation = entity.animationA0;
      const std::uint16_t cursor = entity.timelineCursorA8;
      const bool clawContact = (entity.flags06 & 8u) != 0;
      const bool legContact = (entity.flags06 & 4u) != 0;

      if (animation == 8)
      {
        if (cursor == 0 && clawContact)
        {
          play(kFUN_0027ef40_slamA);
        }
        if (cursor == 6 && clawContact)
        {
          play(kFUN_0027ef40_slamB);
        }
        if (cursor == 8 && clawContact)
        {
          play(kFUN_0027ef40_slamC);
        }
      }
      if (animation == 9 && cursor == 0 && clawContact)
      {
        play(kFUN_0027ef40_stamp);
      }
      if (animation == 2 && cursor == 0 && legContact)
      {
        play(entity.positionZ24 > kFUN_0027ef40_walkWaterLine ? kFUN_0027ef40_stepShallow
                                                              : kFUN_0027ef40_stepDeep);
      }
      if ((animation == 3 || animation == 0x19) && cursor == 4 && legContact)
      {
        play(entity.positionZ24 > kFUN_0027ef40_chargeWaterLine ? kFUN_0027ef40_stepShallow
                                                                : kFUN_0027ef40_stepDeep);
      }
      // FUN_0027F1A8 on animations 0 and 1, and on 3 with the leg bit -- the
      // bone dust, left out with the rest of FUN_0021F6E8's pool.
    }

    // FUN_0027c8a0: the body sweep. Only while the crab is deep -- below -4.5 --
    // and only on a frame the animation raised one of the three contact bits.
    void FUN_0027c8a0_body_sweep(OriginalEntity &entity,
                                 std::size_t slot,
                                 const ActorEnvironment &environment)
    {
      if (entity.positionZ24 >= kFUN_0027c8a0_bodyDepth)
      {
        return;
      }
      if ((entity.collisionFlags0c & kFUN_0027c8a0_contactMask) == 0)
      {
        return;
      }
      if (environment.hitTest == nullptr || !DAT_00573788_crabAttacks().filled)
      {
        return;
      }
      FUN_00215ac8_box_hit_test(entity, slot, body_box(entity),
                                DAT_00573788_crabAttacks().record[0], *environment.hitTest);
    }

    // FUN_0027ccd0: the legs. Two thresholds, six tenths and three tenths of
    // maximum hit points; each hides two bones, keys a cue at the crab's origin
    // and raises the damage stage, which is what moves it onto the next move
    // rotation.
    void FUN_0027ccd0_shed_leg(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (entity.crabDamageStage1bd >= 2)
      {
        return;
      }
      const float fraction = static_cast<float>(static_cast<std::int16_t>(entity.staggerTimer12a)) /
                             static_cast<float>(static_cast<std::int16_t>(entity.maxHitPoints128));
      if (fraction < kFGpffff9260_firstLegHealth && entity.crabDamageStage1bd == 0)
      {
        // FUN_0020d8c0(entity, 0x0B/0x0C, ...) hides the two bones of the first
        // leg. The port's bone override table is per-slot state the draw walk
        // owns; hiding is not modelled here yet.
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0027ccd0_firstLegCue, entity);
        }
        entity.crabDamageStage1bd = 1;
      }
      if (fraction < kFGpffff9264_secondLegHealth && entity.crabDamageStage1bd == 1)
      {
        if (environment.FUN_00267d38_playSound)
        {
          environment.FUN_00267d38_playSound(kFUN_0027ccd0_secondLegCue, entity);
        }
        entity.crabDamageStage1bd = 2;
      }
    }

    // -------------------------------------------------------- the action path

    // FUN_002796c8, the action map. Only seven of the fourteen actions mean
    // anything to the crab, and three of them are the ones s14_e001's animatic
    // asks for.
    void FUN_002796c8_action_map(OriginalEntity &entity,
                                 const ActorEnvironment &environment,
                                 ActorEnvironment::BattleActorView &view,
                                 std::int16_t action)
    {
      switch (action)
      {
      case 0x0C:
        // The throw. +0x94 is the crab's mode byte, and the wrapper reads it
        // back to decide which of its per-frame helpers run.
        entity.spawnParam94 = static_cast<std::uint8_t>(action);
        FUN_00225bf0_set_state_and_animation(entity, 13, 5);
        return;
      case 0x0D:
        entity.spawnParam94 = static_cast<std::uint8_t>(action);
        FUN_0027c3e8_swipe_ladder(entity, environment);
        return;
      case 0x0E:
        // The fight proper: release whatever shot the animatic left running and
        // let FUN_0027c7b8 pick the crab's first move.
        entity.spawnParam94 = static_cast<std::uint8_t>(action);
        FUN_00277d30_boss_camera(-1, 0, 0, &entity, environment);
        FUN_0027c7b8_pick_next_move(entity, environment);
        return;
      case 6:
        FUN_00225bf0_set_state_and_animation(entity, 1, 5);
        view.currentAction0f = static_cast<std::uint8_t>(action);
        return;
      case 2:
      case 3:
      case 7:
        view.currentAction0f = static_cast<std::uint8_t>(action);
        return;
      default:
        return;
      }
    }

    // FUN_00279600, the action check. Returns true for "this crab is out of the
    // fight, skip the damage reaction" -- which is also what action 10 means.
    bool FUN_00279600_action_check(OriginalEntity &entity,
                                   const ActorEnvironment &environment,
                                   ActorEnvironment::BattleActorView &view,
                                   bool haveRecord)
    {
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        return true;
      }
      if (!haveRecord || view.pendingAction0e == 0)
      {
        return false;
      }

      const std::int8_t action = static_cast<std::int8_t>(view.pendingAction0e);
      switch (action)
      {
      case 0x0A:
        // Park: animation 1 with the state left alone, publish the order, and
        // report busy *without* clearing the pending byte.
        FUN_00225bf0_set_state_and_animation(entity, 1, 0);
        view.currentAction0f = 10;
        return true;
      case 0x0B:
        // Latch only. The animatic's first order is this one, and all it does is
        // make the record say 11.
        view.currentAction0f = 0x0B;
        return false;
      case 1:
      case 2:
      case 3:
      case 5:
      case 6:
      case 7:
      case 8:
      case 0x0C:
      case 0x0D:
      case 0x0E:
        FUN_002796c8_action_map(entity, environment, view, action);
        view.pendingAction0e = 0;
        return false;
      default:
        view.pendingAction0e = 0;
        return false;
      }
    }

    // ------------------------------------------------------------- the states

    // FUN_00279940, state 0. The usual enemy init plus two things only the crab
    // does: three attack records into its own bank, and the pair it throws.
    void crab_state0(OriginalEntity &entity, std::size_t slot, const ActorEnvironment &environment)
    {
      entity.scale14c = 1.0f;
      entity.scaleZ150 = 1.0f;

      // FUN_0025bae8(0, type): group 0 of SCR.BIN 0xBF, indexed by type - 0x7C.
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
      entity.crabPartner1c0 = -1;
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x80u);
      entity.battleFlags96 = static_cast<std::uint8_t>(entity.battleFlags96 | 0x21u);

      FUN_00225bf0_set_state_and_animation(entity, 1, 5);

      FUN_00216078_fill_crab_records(static_cast<std::int16_t>(entity.typeId00), environment);

      if (environment.FUN_0023f8b8_bind_battle_actor)
      {
        entity.battleActorRecord198 = environment.FUN_0023f8b8_bind_battle_actor(slot);
      }

      entity.crabHeld1ac = {{-1, -1, -1}};
      entity.crabHeldCursor1b8 = 0;
      entity.crabDamageStage1bd = 0;
      entity.crabIdleCursor1c4 = 0;
      entity.crabPhase1c5 = 0;
      entity.crabDamageAccum1c8 = 0;
      entity.crabByte1ce = 0;
      entity.crabSubPhase1d0 = 0;

      // The five bytes at 0x0035526A..0x0035528C. They are globals, so a second
      // run of the scene inherits whatever the first left; the original clears
      // them here and so does this.
      DAT_0035526a_swipeCount() = 0;
      DAT_0035526b_swipeDone() = 0;
      DAT_0035526c_dodgePhase() = 0;
      DAT_0035526d_runSpot() = 0;
      DAT_0035528c_cameraSide() = 1;
      DAT_00325900_cinematicSlots() = {{-1, -1, -1}};
      clear_pool(DAT_005737e8_wreckPool());
      clear_pool(DAT_00573860_propPool());
      clear_pool(DAT_00573928_chunkPool());
      DAT_003552a8_wreckTimer() = 0;
      FUN_00277d30_boss_camera(-1, 0, 0, &entity, environment);

      // The four FUN_002797D0 calls, in the order FUN_00279940 makes them. The
      // tag lists are four byte runs at 0x00355250, 0x00355258, 0x00355260 and
      // 0x00355268.
      {
        static constexpr std::array<std::uint8_t, 4> kLamps{{20, 21, 22, 23}};
        static constexpr std::array<std::uint8_t, 5> kCrates{{30, 31, 32, 33, 34}};
        static constexpr std::array<std::uint8_t, 5> kBarrels{{35, 36, 37, 38, 39}};
        static constexpr std::array<std::uint8_t, 2> kOther{{41, 42}};
        FUN_002797d0_bind_props(kLamps.data(), -1, 0x01, 4, environment);
        FUN_002797d0_bind_props(kCrates.data(), 0, 0x02, 5, environment);
        FUN_002797d0_bind_props(kBarrels.data(), 0, 0x04, 5, environment);
        FUN_002797d0_bind_props(kOther.data(), 0, 0x10, 2, environment);
      }

      // FUN_00248f18(0x0C) and (0x31): the pair state 13 picks up. The first
      // lands at +0x1C0 and the second inside *its* +0x198, which is how state
      // 13 reaches both from one link.
      if (environment.FUN_00248f18_find_by_tag && environment.entityPool != nullptr)
      {
        const std::int32_t partner = environment.FUN_00248f18_find_by_tag(0x0C);
        if (partner > 0 && static_cast<std::size_t>(partner) < environment.entityPool->slotCount())
        {
          entity.crabPartner1c0 = partner;
          const std::int32_t other = environment.FUN_00248f18_find_by_tag(0x31);
          if (other > 0)
          {
            environment.entityPool->slot(static_cast<std::size_t>(partner)).eventFlagId198 =
                static_cast<std::uint16_t>(other);
          }
        }
      }
    }

    // FUN_00279bf0, state 1. Animation 5 is the entry sentinel every crab state
    // opens on; from there it sits on 0 and flips to 1 one time in ten when the
    // clip comes round.
    void FUN_00279bf0_state1_idle(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 5)
      {
        FUN_00225bc8_set_animation(entity, 0);
      }
      if (entity.animationA0 == 0)
      {
        if ((entity.flags06 & 1u) != 0)
        {
          const std::uint32_t roll = environment.random ? environment.random() : 0;
          if (static_cast<std::int32_t>(roll) % 10 == 0)
          {
            FUN_00225bc8_set_animation(entity, 1);
          }
        }
        return;
      }
      if (entity.animationA0 == 1 && (entity.flags06 & 1u) != 0)
      {
        FUN_00225bc8_set_animation(entity, 0);
      }
    }

    // FUN_00279ca8, state 2. Roll a clip and a 100..280 tick hold, then hand
    // over to the move rotation.
    void FUN_00279ca8_state2_hold(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 5)
      {
        const std::uint32_t pick = environment.random ? environment.random() : 0;
        FUN_00225bc8_set_animation(entity, static_cast<std::uint16_t>((pick & 1u) == 0 ? 1 : 0));
        const std::int32_t roll =
            environment.random ? static_cast<std::int32_t>(environment.random()) : 0;
        entity.fadeRamp62 =
            static_cast<std::uint16_t>((static_cast<std::int16_t>(roll % 10) * 0x14 + 100) * 0x20);
      }
      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_0027c7b8_pick_next_move(entity, environment);
      }
    }

    // FUN_00279d60, state 3 -- the charge and slam, the first move of the fight
    // proper and the one FUN_0027c7b8's opening rotation asks for twice.
    //
    // Aim one unit past the player, cost the run, walk it, then swing: animation
    // 8 sweeps the crab's animated hit volume through FUN_002148a8 every frame,
    // and clip frame 6 is the one that shakes the camera.
    void FUN_00279d60_state3_charge(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);

      if (entity.animationA0 == 5)
      {
        entity.crabSpeed1a0 = kFUN_0027bbe0_walkSpeed;
        entity.crabTarget1a8 = 0;
        const float bearing = FUN_0023a4b8_bearing(entity, player);
        entity.battleDesiredFacing19c = bearing;
        const float aimX = entity.positionX20 + cos_of(bearing);
        const float aimZ = entity.positionZ24 + sin_of(bearing);
        // FUN_0023a6d0's second argument is the *target*, so the hold is how
        // long the crab needs to cover the gap between them.
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a6d0_travel_ticks(entity.crabSpeed1a0, player, aimX, aimZ));
        FUN_00215e48_clear_contacts(entity);
        FUN_00225bc8_set_animation(entity, 2);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kFGpffff91ac_chargeTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      if (entity.animationA0 == 2)
      {
        if (countdown(entity.fadeRamp62, environment.frameTicks))
        {
          FUN_00225bc8_set_animation(entity, 8);
          return;
        }
        const float travel =
            (entity.crabSpeed1a0 *
             static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
            32000.0f;
        entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
        entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
        return;
      }

      if (entity.animationA0 != 8)
      {
        return;
      }

      // uGpffffb6f1: the smear's target alpha, up for the slam.
      if (environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264448_set_alpha(0x50);
      }
      if (environment.hitTest != nullptr && DAT_00573788_crabAttacks().filled)
      {
        FUN_002148a8_swept_hit_test(entity, slot, DAT_00573788_crabAttacks().record[0],
                                    *environment.hitTest);
      }
      if (entity.timelineCursorA8 == 6 && environment.FUN_0022dcf0_shake_camera)
      {
        environment.FUN_0022dcf0_shake_camera(kFUN_00279d60_slamShake,
                                              kFUN_00279d60_slamShakeTicks);
      }
      if ((entity.flags06 & 1u) != 0)
      {
        FUN_0027c7b8_pick_next_move(entity, environment);
      }
    }

    // FUN_0027a440, state 5 -- the crab closes to three units and then stamps
    // three times, each stamp throwing a ring of bubbles up out of the water.
    //
    // The stamps themselves are FUN_002eac48, an effect spawner the port has no
    // pool for yet; what is here is the movement, the three-beat count and the
    // 0xC80-tick hold between them, which is what decides how long the move
    // occupies the crab.
    void FUN_0027a440_state5_stamp(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);

      if (entity.animationA0 == 5)
      {
        entity.crabSpeed1a0 = kFUN_0027bbe0_walkSpeed;
        entity.crabTarget1a8 = 0;
        const float bearing = FUN_0023a4b8_bearing(entity, player);
        entity.battleDesiredFacing19c = bearing;
        const float aimX = entity.positionX20 + cos_of(bearing) * 3.0f;
        const float aimZ = entity.positionZ24 + sin_of(bearing) * 3.0f;
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a6d0_travel_ticks(entity.crabSpeed1a0, player, aimX, aimZ));
        FUN_00225bc8_set_animation(entity, 2);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kDAT_0035312c_stampTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      if (entity.animationA0 == 2)
      {
        if (countdown(entity.fadeRamp62, environment.frameTicks))
        {
          entity.crabStampCount1bc = 0;
          FUN_00225bc8_set_animation(entity, 9);
          return;
        }
        const float travel =
            (entity.crabSpeed1a0 *
             static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
            32000.0f;
        entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
        entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
        return;
      }

      if (entity.animationA0 != 9)
      {
        return;
      }

      if (entity.crabStampCount1bc < 3)
      {
        if ((entity.flags06 & 1u) != 0)
        {
          // Ten bubbles off bone 6, fifteen once one leg is gone and twenty
          // once two are -- and the last of those doubles the spread as well.
          // Then three more of mode 4 in a ring half a unit out, each lifted by
          // a random hundredth or two.
          orphen::ported::psm2::Vec3 stampPoint{entity.positionX20, entity.positionZ24,
                                                entity.positionY28};
          if (environment.FUN_0020dc88_bone_point)
          {
            stampPoint = environment.FUN_0020dc88_bone_point(
                environment.currentSlot, kFUN_0027a440_bubbleBone,
                orphen::ported::psm2::Vec3{kDAT_0034e5f0_stampOffsetX,
                                           kDAT_0034e5f4_stampOffsetY,
                                           kDAT_0034e5f8_stampOffsetZ});
          }
          const orphen::ported::resource::HitParameters *attack =
              DAT_00573788_crabAttacks().filled ? &DAT_00573788_crabAttacks().record[1] : nullptr;
          std::int32_t count = 10;
          std::uint32_t spread = 1;
          if (entity.crabDamageStage1bd == 1)
          {
            count = 15;
          }
          else if (entity.crabDamageStage1bd == 2)
          {
            count = 20;
            spread = 2;
          }
          for (std::int32_t index = 0; index < count; ++index)
          {
            FUN_002eac48_spawn_bubble(entity, 0, stampPoint, spread, attack, environment);
          }
          // Three of mode 4, at 0, 120 and 240 degrees. The original's loop
          // runs its body three times -- the counter starts at 2 and the test
          // is `-1 < n` after the decrement.
          std::int32_t degrees = 0;
          for (std::int32_t index = 0; index < 3; ++index)
          {
            const float angle = (static_cast<float>(degrees) * kDAT_00353130_turn) / 360.0f;
            const std::uint32_t lift = environment.random ? environment.random() : 0;
            const orphen::ported::psm2::Vec3 ringPoint{
                stampPoint.x + cos_of(angle) * kFUN_0027a440_ringRadius,
                stampPoint.y + sin_of(angle) * kFUN_0027a440_ringRadius,
                stampPoint.z + static_cast<float>(static_cast<std::int32_t>(lift) % 0x0F) / 100.0f};
            FUN_002eac48_spawn_bubble(entity, 4, ringPoint, 0, attack, environment);
            degrees += 0x78;
          }
          entity.fadeRamp62 = kFUN_0027a440_stampHold;
          entity.crabStampCount1bc = static_cast<std::uint8_t>(entity.crabStampCount1bc + 1);
        }
        return;
      }
      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_0027c7b8_pick_next_move(entity, environment);
      }
    }

    // FUN_0027aaf8, state 8 -- the crab walks back to (0, -5), which is the deep
    // water at the top of the arena. If it is already past that line it turns
    // around and *backs* there instead, on animation 0x19, with the heading it
    // moves along a half turn off the one it faces.
    void FUN_0027aaf8_state8_reposition(OriginalEntity &entity,
                                        const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 5)
      {
        entity.crabBackwards1cf = 0;
        entity.battleDesiredFacing19c =
            std::atan2(kDAT_003258b4_stationZ - entity.positionZ24,
                       kDAT_003258b0_stationX - entity.positionX20);
        FUN_00225bc8_set_animation(entity, 2);
        if (entity.positionZ24 >= kDAT_003258b4_stationZ)
        {
          entity.crabBackwards1cf = 1;
          entity.battleDesiredFacing19c += kFGpffff91d4_halfTurn;
          FUN_00225bc8_set_animation(entity, 0x19);
        }
        entity.crabSpeed1a0 = kFUN_0027bbe0_walkSpeed;
        entity.fadeRamp62 = static_cast<std::uint16_t>(FUN_0023a6d0_travel_ticks(
            kFUN_0027bbe0_walkSpeed, entity, kDAT_003258b0_stationX, kDAT_003258b4_stationZ));
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kFGpffff91d8_stationTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      if (countdown(entity.fadeRamp62, environment.frameTicks))
      {
        FUN_0027c7b8_pick_next_move(entity, environment);
        return;
      }
      const float travel =
          (entity.crabSpeed1a0 *
           static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
          32000.0f;
      const float heading = entity.crabBackwards1cf != 0
                                ? entity.facingRadians5c + kFGpffff91dc_halfTurn
                                : entity.facingRadians5c;
      entity.desiredDeltaX30 += travel * cos_of(heading);
      entity.desiredDeltaZ34 += travel * sin_of(heading);
    }

    // FUN_0027b7c8, state 12 -- the hit reaction, and the phase change.
    //
    // The wrapper drops the crab in here on every hit that gets past its
    // invulnerability. Which flinch it plays is +0x1C6, the element index
    // FUN_0023a9d0 pulled out of the hit record: zero rolls between animations 6
    // and 7, anything else plays 0x14. When the clip comes round it either goes
    // back to a move -- state 8 while it still has all its legs, state 7 once it
    // does not -- or, if the hit took the last of its hit points, opens the two
    // map groups, moves onto the late rotation and picks from that instead.
    void FUN_0027b7c8_state12_hit(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      if (entity.animationA0 == 5)
      {
        if (entity.crabHitReaction1c6 == 0)
        {
          const std::uint32_t roll = environment.random ? environment.random() : 0;
          FUN_00225bc8_set_animation(entity, static_cast<std::uint16_t>((roll & 1u) == 0 ? 6 : 7));
        }
        else
        {
          FUN_00225bc8_set_animation(entity, 0x14);
        }
      }

      if ((entity.flags06 & 1u) == 0)
      {
        return;
      }
      entity.fadeColor138 = 0;

      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        // Out of hit points: the arena opens up and the crab moves onto the
        // three-move late rotation. This is not the death -- state 15 is, and
        // nothing here goes there.
        if (environment.FUN_0022dbc8_show_map_primitives)
        {
          environment.FUN_0022dbc8_show_map_primitives(0x20u, true);
          environment.FUN_0022dbc8_show_map_primitives(0x08u, true);
        }
        if (environment.FUN_00248f18_find_by_tag && environment.entityPool != nullptr)
        {
          const std::int32_t found = environment.FUN_00248f18_find_by_tag(0x13);
          if (found > 0 && static_cast<std::size_t>(found) < environment.entityPool->slotCount())
          {
            OriginalEntity &tagged = environment.entityPool->slot(static_cast<std::size_t>(found));
            tagged.halfword08 = static_cast<std::uint16_t>(tagged.halfword08 & 0xFFFEu);
          }
        }
        entity.crabPhase1c5 = 2;
        entity.crabIdleCursor1c4 = 0;
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
        if (environment.entityPool != nullptr)
        {
          OriginalEntity &player = environment.entityPool->slot(0);
          player.halfword04 = static_cast<std::uint16_t>(player.halfword04 | 0x10u);
        }
        FUN_0027c7b8_pick_next_move(entity, environment);
        return;
      }

      FUN_00225bf0_set_state_and_animation(entity, entity.crabPhase1c5 == 0 ? 8 : 7,
                                           kFUN_0027c7b8_neutralAnimation);
    }

    // FUN_0027d230 -- **Orphen's half of the fight**, and the reason the swipe
    // ladder can run at all.
    //
    // The wrapper calls this every frame the crab's mode byte is non-zero, and
    // it is a little state machine of its own on DAT_0035526B: the swipe sets it
    // to 1 when the claw connects, this walks the player along a Bezier to a
    // landing spot over 0x640 ticks, and then sets 9, which hands control back
    // and clears it to 0. Only then does FUN_0027c458 ask for the next swipe.
    // Leaving it out is what made the crab take one swing and stop.
    //
    // Every field it touches is on pool slot 0 -- the DAT_0058BExx spellings are
    // that entity's own offsets.
    void FUN_0027d230_player_dodge(OriginalEntity &entity, const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      OriginalEntity &player = pool.slot(0);
      const std::uint32_t ticks = environment.frameTicks;

      if (DAT_0035526b_swipeDone() != 0)
      {
        // Whatever the player was doing, they are not taking damage this frame.
        player.flags06 = static_cast<std::uint16_t>(player.flags06 & 0xFFEFu);
        player.freezeTimerBd = 0;
        player.pendingDamageBe = 0;
        player.hitFlagsC2 = 0;
      }

      if (DAT_0035526b_swipeDone() == 9)
      {
        // Release the boss camera. The party control block's current action goes
        // back to 6 and the player's own state machine takes over again.
        FUN_00277d30_boss_camera(-1, 0, 0, &entity, environment);
        if (entity.spawnParam94 == 0x0E && environment.DAT_0031d3c8_setBattleTableWord)
        {
          // (&DAT_0031D7BE)[(DAT_00354EBE - 1) * 0x3C] = 6 -- the control block's
          // +0x0F. Routed through the battle table the same way every other
          // control-block write in the port is.
          FUN_00249388_release_player(environment);
        }
        player.halfword04 = static_cast<std::uint16_t>(player.halfword04 & 0xFFE7u);
        FUN_00225bc8_set_animation(player, 2);
        DAT_0035526b_swipeDone() = 0;
        return;
      }

      if (DAT_0035526b_swipeDone() == 1)
      {
        if (DAT_0035526c_dodgePhase() == 0)
        {
          // The landing spot for this swipe, out of DAT_00325888 indexed by how
          // many swipes have landed. The arc's first control point is where the
          // player is standing, the second the same again -- so the curve leaves
          // straight and lands on the table entry.
          const std::uint32_t index = DAT_0035526a_swipeCount() % kDAT_00325888_landings.size();
          const float endX = kDAT_00325888_landings[index][0];
          const float endZ = kDAT_00325888_landings[index][1];
          DAT_005737c0_arcX() = {{player.positionX20, player.positionX20, endX}};
          DAT_005737cc_arcZ() = {{player.positionZ24, player.positionZ24, endZ}};
          float landingY = player.positionY28;
          if (environment.terrainSurface)
          {
            const auto surface = environment.terrainSurface(endX, endZ, player.positionY28,
                                                            player.height58, player.radius54,
                                                            player.halfword04, 0);
            if (surface.has_value())
            {
              landingY = surface->height;
            }
          }
          DAT_005737d8_arcY() = {{player.positionY28, player.positionY28 + 1.0f, landingY}};
          player.halfword04 = static_cast<std::uint16_t>(player.halfword04 | 8u);
          player.fadeRamp62 = 0;
          player.groundHeight4c = landingY;
          FUN_00225bc8_set_animation(player, 0x0C);
          FUN_00249388_hold_player(environment);
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027d230_launchCue, player);
          }
          DAT_0035526c_dodgePhase() = 1;
        }
        if (DAT_0035526c_dodgePhase() != 1)
        {
          return;
        }
        player.fadeRamp62 =
            static_cast<std::uint16_t>(player.fadeRamp62 + static_cast<std::uint16_t>(ticks));
        if (static_cast<std::int16_t>(player.fadeRamp62) > kFUN_0027d230_flightTicks)
        {
          player.positionY28 = DAT_005737d8_arcY()[2];
          player.positionX20 = DAT_005737c0_arcX()[2];
          player.positionZ24 = DAT_005737cc_arcZ()[2];
          DAT_0035526b_swipeDone() = 9;
          DAT_0035526c_dodgePhase() = 0;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027d230_landCue, player);
          }
          return;
        }
        player.facingRadians5c = FUN_0023a4b8_bearing(player, entity);
        const float t = static_cast<float>(static_cast<std::int16_t>(player.fadeRamp62)) / 1600.0f;
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
        // The camera follows the tumble. The sub-shot is picked off the swipe
        // counter: the first swipe frames it from one side, the second from the
        // other, and anything past that takes the fixed pose.
        const std::int16_t dodgeShot = DAT_0035526a_swipeCount() == 1   ? 2
                                       : DAT_0035526a_swipeCount() == 2 ? 1
                                                                        : 3;
        FUN_00277d30_boss_camera(6, dodgeShot, 1, &entity, environment);
        player.positionX20 = FUN_0023a990_bezier(t, DAT_005737c0_arcX());
        player.positionZ24 = FUN_0023a990_bezier(t, DAT_005737cc_arcZ());
        player.positionY28 = FUN_0023a990_bezier(t, DAT_005737d8_arcY());
        return;
      }

      if (DAT_0035526b_swipeDone() == 2)
      {
        if (player.animationA0 != 0x14)
        {
          FUN_00225bc8_set_animation(player, 0x14);
        }
        return;
      }

      if (DAT_0035526b_swipeDone() == 3)
      {
        FUN_00249388_hold_player(environment);
        player.facingRadians5c = FUN_0023a4b8_bearing(player, entity);
        FUN_00225bc8_set_animation(player, 2);
        return;
      }

      if (DAT_0035526b_swipeDone() == 4)
      {
        return;
      }

      if (DAT_0035526b_swipeDone() == 6)
      {
        // Turn back toward the crab at 32 degrees a frame and stop.
        const float bearing = FUN_0023a4b8_bearing(player, entity);
        const float step = FUN_0023a320_approach_angle(player.facingRadians5c, bearing,
                                                       kDAT_003531f8_recoverTurnRate);
        if (step != 0.0f)
        {
          player.facingRadians5c += step;
        }
        return;
      }

      if (DAT_0035526b_swipeDone() != 5)
      {
        return;
      }

      // Mode 5: the long run to one of two scripted spots, 0x1900 ticks of it,
      // with the facing taken from the curve's own tangent.
      if (DAT_0035526c_dodgePhase() == 0)
      {
        const auto &spot = (DAT_0035526d_runSpot() == 0) ? kDAT_003258e0_runA : kDAT_003258f0_runB;
        DAT_005737c0_arcX() = {{player.positionX20, spot[0], spot[2]}};
        DAT_005737cc_arcZ() = {{player.positionZ24, spot[1], spot[3]}};
        FUN_00225bc8_set_animation(player, 0x81);
        DAT_0035526c_dodgePhase() = 1;
        player.fadeRamp62 = 0;
        FUN_00249388_hold_player(environment);
      }
      player.fadeRamp62 =
          static_cast<std::uint16_t>(player.fadeRamp62 + static_cast<std::uint16_t>(ticks));
      const std::int32_t elapsed = static_cast<std::int16_t>(player.fadeRamp62);
      if (elapsed > kFUN_0027d230_runTicks)
      {
        player.positionZ24 = DAT_005737cc_arcZ()[2];
        player.positionX20 = DAT_005737c0_arcX()[2];
        FUN_00225bc8_set_animation(player, 2);
        DAT_0035526b_swipeDone() = 6;
        DAT_0035526c_dodgePhase() = 0;
        return;
      }
      const float t = static_cast<float>(elapsed) / 6400.0f;
      const float tNext = static_cast<float>(elapsed + static_cast<std::int32_t>(ticks)) / 6400.0f;
      player.positionX20 = FUN_0023a990_bezier(t, DAT_005737c0_arcX());
      player.positionZ24 = FUN_0023a990_bezier(t, DAT_005737cc_arcZ());
      const float aheadX = FUN_0023a990_bezier(tNext, DAT_005737c0_arcX());
      const float aheadZ = FUN_0023a990_bezier(tNext, DAT_005737cc_arcZ());
      player.facingRadians5c = std::atan2(aheadZ - player.positionZ24, aheadX - player.positionX20);
    }

    // FUN_0027bbe0, state 13 -- the throw, and the first half of the animatic.
    //
    // The crab walks to the pair, picks one of them up onto bone 18, turns
    // twice, plants itself at (1.335, -6.4, -0.8), hurls, and then holds four
    // beats while the camera watches. The last of those beats is what raises the
    // script's cue, so nothing in s14_e001 gets past its own beat 110 until this
    // has run all the way through.
    //
    // The four watching beats also stand up the close-up rig in the player's
    // place -- DAT_00325900/04/08, found by actor tag -- and shots 12 and 13
    // frame it. FUN_0027C458 is the only thing that takes it away again.
    void FUN_0027bbe0_state13_throw(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      (void)slot;

      const std::size_t partnerSlot =
          (entity.crabPartner1c0 >= 0 &&
           static_cast<std::size_t>(entity.crabPartner1c0) < pool.slotCount())
              ? static_cast<std::size_t>(entity.crabPartner1c0)
              : pool.slotCount();
      const bool havePartner = partnerSlot < pool.slotCount();

      if (entity.animationA0 == 5)
      {
        if (havePartner)
        {
          OriginalEntity &partner = pool.slot(partnerSlot);
          partner.spawnParam94 = 0;
          partner.state60 = 0;
          partner.halfword04 = static_cast<std::uint16_t>(partner.halfword04 | 1u);
          const float bearing = FUN_0023a4b8_bearing(entity, partner);
          entity.crabSubPhase1d0 = 0;
          entity.crabSpeed1a0 = kFUN_0027bbe0_walkSpeed;
          entity.battleDesiredFacing19c = bearing;
          // The aim is two units past the crab along the bearing, and the travel
          // is costed from the *partner* -- FUN_0023a6d0's second argument is
          // +0x1C0, not the crab.
          const float aimX = entity.positionX20 + cos_of(bearing) * 2.0f;
          const float aimZ = entity.positionZ24 + sin_of(bearing) * 2.0f;
          entity.fadeRamp62 = static_cast<std::uint16_t>(
              FUN_0023a6d0_travel_ticks(entity.crabSpeed1a0, partner, aimX, aimZ));
        }
        FUN_00225bc8_set_animation(entity, 2);
        FUN_0027cef8_set_script_cue(environment, false);
      }

      // The pair's own two-step: the one about to be carried walks its clip on
      // and drags the other along with it.
      if (havePartner)
      {
        OriginalEntity &partner = pool.slot(partnerSlot);
        const std::size_t otherSlot = static_cast<std::size_t>(partner.eventFlagId198);
        OriginalEntity *other = otherSlot < pool.slotCount() ? &pool.slot(otherSlot) : nullptr;
        if (partner.animationA0 == 0x38)
        {
          FUN_00225bc8_set_animation(partner, 0x39);
          if (other != nullptr)
          {
            FUN_00225bc8_set_animation(*other, 2);
          }
        }
        else if (partner.animationA0 == 0x39 && (partner.flags06 & 1u) != 0)
        {
          FUN_00225bc8_set_animation(partner, 0x3A);
          if (other != nullptr)
          {
            FUN_00225bc8_set_animation(*other, 3);
          }
        }
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kFGpffff9210_carryTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        // Once the throw has reached its watching beats the turn no longer
        // blocks the rest of the state -- that is the +0x1D0 test in the
        // original's early return.
        if (entity.crabSubPhase1d0 == 0)
        {
          return;
        }
      }

      const auto advance = [&](float heading)
      {
        const float travel =
            (entity.crabSpeed1a0 *
             static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
            32000.0f;
        entity.desiredDeltaX30 += travel * cos_of(heading);
        entity.desiredDeltaZ34 += travel * sin_of(heading);
      };

      switch (entity.animationA0)
      {
      case 2:
        // The approach.
        FUN_00277d30_boss_camera(7, 1, 1, &entity, environment);
        if (countdown(entity.fadeRamp62, environment.frameTicks))
        {
          const float turned = entity.facingRadians5c - kFGpffff9214_quarterTurn;
          entity.battleDesiredFacing19c = turned;
          entity.facingRadians5c = turned;
          FUN_00225bc8_set_animation(entity, 0x1A);
          return;
        }
        advance(entity.facingRadians5c);
        break;

      case 0x1A:
        // The pick-up. On clip frame 4 with the contact bit raised the victim is
        // moved onto bone 18 and both halves change clip; when the clip ends the
        // crab settles and the script's *other* word, work 3, goes up.
        if (entity.timelineCursorA8 == 4 && (entity.flags06 & 4u) != 0 && havePartner)
        {
          OriginalEntity &partner = pool.slot(partnerSlot);
          partner.positionX20 = 0.0f;
          partner.positionZ24 = kUGpffff9218_holdY;
          partner.positionY28 = kUGpffff921c_holdZ;
          partner.rotationX154 = kUGpffff9220_holdRoll;
          partner.parentSlot192 = static_cast<std::int16_t>(environment.currentSlot);
          partner.attachBone194 = static_cast<std::int8_t>(kFUN_0027bbe0_carryBone);
          FUN_00225bc8_set_animation(partner, 0x3B);
          const std::size_t otherSlot = static_cast<std::size_t>(partner.eventFlagId198);
          if (otherSlot < pool.slotCount())
          {
            FUN_00225bc8_set_animation(pool.slot(otherSlot), 0);
          }
        }
        if ((entity.flags06 & 1u) != 0)
        {
          FUN_00225bc8_set_animation(entity, 0);
          entity.fadeRamp62 = kFUN_0027bbe0_pickUpHold;
          if (environment.DAT_00355060_setScriptWork)
          {
            environment.DAT_00355060_setScriptWork(3, 1);
          }
        }
        break;

      case 0:
        // The first turn with the victim in hand.
        FUN_00277d30_boss_camera(7, 2, 1, &entity, environment);
        if (countdown(entity.fadeRamp62, environment.frameTicks))
        {
          entity.crabSpeed1a0 = kFUN_0027bbe0_carrySpeed;
          entity.battleDesiredFacing19c = entity.facingRadians5c - kFGpffff9224_quarterTurn;
          entity.crabTimer1cc = kFUN_0027bbe0_turnHold;
          entity.fadeRamp62 = kFUN_0027bbe0_turnHold;
          FUN_00225bc8_set_animation(entity, 0x18);
        }
        break;

      case 0x18:
        // The carry, walked sideways -- the heading it moves along is a quarter
        // turn off the one it faces.
        FUN_00277d30_boss_camera(7, 2, 1, &entity, environment);
        if (countdown(entity.fadeRamp62, environment.frameTicks))
        {
          const float turned = entity.facingRadians5c - kFGpffff9228_quarterTurn;
          entity.battleDesiredFacing19c = turned;
          entity.facingRadians5c = turned;
          FUN_00225bc8_set_animation(entity, 0x0F);
          entity.positionX20 = kUGpffff922c_hurlX;
          entity.positionZ24 = kUGpffff9230_hurlY;
          entity.positionY28 = kUGpffff9234_hurlZ;
          // The hurl's own camera.
          FUN_00277d30_boss_camera(9, 1, 1, &entity, environment);
          return;
        }
        advance(entity.facingRadians5c - kFGpffff9238_quarterTurn);
        break;

      case 0x0F:
        FUN_00277d30_boss_camera(9, 1, 1, &entity, environment);
        // The hurl. Clip frame 6 with the contact bit lets the victim go: it
        // stops riding the bone, takes the bone's world point as its own
        // position, and drops into its own state 1.
        if (entity.timelineCursorA8 == 6 && (entity.flags06 & 4u) != 0 && havePartner)
        {
          OriginalEntity &partner = pool.slot(partnerSlot);
          partner.attachBone194 = -1;
          partner.parentSlot192 = -1;
          if (environment.FUN_0020dc88_bone_point)
          {
            const auto point = environment.FUN_0020dc88_bone_point(
                environment.currentSlot, kFUN_0027bbe0_carryBone,
                orphen::ported::psm2::Vec3{0.0f, 0.0f, 0.0f});
            partner.positionX20 = point.x;
            partner.positionZ24 = point.y;
            partner.positionY28 = point.z;
          }
          partner.state60 = 1;
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_0027bbe0_hurlCue, entity);
          }
        }
        if ((entity.flags06 & 1u) != 0)
        {
          entity.fadeRamp62 = kFUN_0027bbe0_hurlHold;
          FUN_00225bc8_set_animation(entity, 0x1B);
          entity.crabSubPhase1d0 = 0;
        }
        break;

      default:
        // Animation 0x1B and everything after it: four holds, and the last one
        // hands the script back its cue.
        switch (entity.crabSubPhase1d0)
        {
        case 0:
          FUN_00277d30_boss_camera(9, 1, 1, &entity, environment);
          if (countdown(entity.fadeRamp62, environment.frameTicks))
          {
            // The three watchers, found by actor tag. The original parks pool
            // slot 0 -- the player -- and the first of them at the same spot,
            // shows that one, hides the second behind bit 7, and starts the
            // third's clip.
            if (environment.FUN_00248f18_find_by_tag)
            {
              const std::int32_t watcherA =
                  environment.FUN_00248f18_find_by_tag(kFUN_0027bbe0_watcherA);
              const std::int32_t watcherB =
                  environment.FUN_00248f18_find_by_tag(kFUN_0027bbe0_watcherB);
              const std::int32_t watcherC =
                  environment.FUN_00248f18_find_by_tag(kFUN_0027bbe0_watcherC);
              OriginalEntity &player = pool.slot(0);
              player.positionX20 = kUGpffff923c_watchX;
              player.positionZ24 = kUGpffff9240_watchY;
              player.positionY28 = 0.0f;
              player.halfword04 = static_cast<std::uint16_t>(player.halfword04 | 1u);
              player.halfword08 = static_cast<std::uint16_t>(player.halfword08 | 1u);
              if (watcherA > 0 && static_cast<std::size_t>(watcherA) < pool.slotCount())
              {
                OriginalEntity &a = pool.slot(static_cast<std::size_t>(watcherA));
                a.positionX20 = kUGpffff923c_watchX;
                a.positionZ24 = kUGpffff9240_watchY;
                a.positionY28 = 0.0f;
                a.halfword08 = static_cast<std::uint16_t>(a.halfword08 & 0xFFFEu);
                a.groundHeight4c = player.groundHeight4c;
                a.facingRadians5c = player.facingRadians5c;
              }
              if (watcherB > 0 && static_cast<std::size_t>(watcherB) < pool.slotCount())
              {
                OriginalEntity &b = pool.slot(static_cast<std::size_t>(watcherB));
                b.halfword08 = static_cast<std::uint16_t>(b.halfword08 | 0x80u);
              }
              if (watcherC > 0 && static_cast<std::size_t>(watcherC) < pool.slotCount())
              {
                OriginalEntity &c = pool.slot(static_cast<std::size_t>(watcherC));
                c.halfword08 = static_cast<std::uint16_t>(c.halfword08 | 0x80u);
                FUN_00225bc8_set_animation(c, 1);
              }
              // DAT_00325900/04/08. Shots 12 and 13 frame the first of the
              // three, and state 14 is what gives all of them back -- without
              // that the mount stands in the arena for the rest of the fight
              // and the player never comes back out of hiding.
              DAT_00325900_cinematicSlots() = {{watcherA, watcherB, watcherC}};
            }
            entity.battleDesiredFacing19c = FUN_0023a4b8_bearing(entity, pool.slot(0));
            entity.fadeRamp62 = kFUN_0027bbe0_watchHold;
            entity.crabSubPhase1d0 = 1;
            // Release, then the watch -- the shot that frames the close-up rig
            // this beat just stood up in the player's place.
            FUN_00277d30_boss_camera(-1, 0, 0, &entity, environment);
            FUN_00277d30_boss_camera(10, 1, 1, &entity, environment);
          }
          break;

        case 1:
          FUN_00277d30_boss_camera(10, 1, 1, &entity, environment);
          if (countdown(entity.fadeRamp62, environment.frameTicks))
          {
            entity.crabSubPhase1d0 = 2;
            entity.fadeRamp62 = kFUN_0027bbe0_beatHold;
          }
          if ((entity.flags06 & 1u) != 0)
          {
            entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10u);
          }
          break;

        case 2:
          FUN_00277d30_boss_camera(10, 1, 1, &entity, environment);
          if (countdown(entity.fadeRamp62, environment.frameTicks))
          {
            FUN_00277d30_boss_camera(-1, 0, 0, &entity, environment);
            entity.crabSubPhase1d0 = 3;
            entity.fadeRamp62 = kFUN_0027bbe0_beatHold;
            if (environment.FUN_00248f18_find_by_tag)
            {
              const std::int32_t watcherB =
                  environment.FUN_00248f18_find_by_tag(kFUN_0027bbe0_watcherB);
              if (watcherB > 0 && static_cast<std::size_t>(watcherB) < pool.slotCount())
              {
                FUN_00225bc8_set_animation(pool.slot(static_cast<std::size_t>(watcherB)), 1);
              }
            }
          }
          break;

        case 3:
          // The close-up itself.
          FUN_00277d30_boss_camera(12, 1, 1, &entity, environment);
          if (countdown(entity.fadeRamp62, environment.frameTicks))
          {
            entity.fadeRamp62 = kFUN_0027bbe0_beatHold;
            entity.crabSubPhase1d0 = 4;
            entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
          }
          break;

        default:
          if (countdown(entity.fadeRamp62, environment.frameTicks))
          {
            // **The cue.** s14_e001's beat 110 has been waiting on this since
            // the order landed.
            FUN_0027cef8_set_script_cue(environment, true);
            entity.state60 = 1;
          }
          break;
        }
        break;
      }

      // LAB_0027C38C. Once the throw is past its first watching beat every
      // frame sets the smear to 0x6E with an identity transform, which is what
      // gives the whole close-up sequence its soft trail.
      if (entity.crabSubPhase1d0 > 1 && environment.DAT_00343878_frameFeedback != nullptr)
      {
        environment.DAT_00343878_frameFeedback->FUN_00264470_set_alpha_and_transform(0x6E, 0, 0,
                                                                                     10, 10, 0);
      }
    }


    // FUN_0027c458, state 14 -- the swipe, and the second half of the animatic.
    //
    // Animation 5 opens it: aim one and a half units past the player, cost the
    // travel, clear the contacts and drop into animation 2, the wind-up. Turn
    // toward that aim until it is reached; then animation 2 walks the crab in
    // and animation 8 is the swipe itself, whose fourth clip frame with contact
    // bit 4 raised is the moment the claw lands.
    void FUN_0027c458_state14_swipe(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      EntityPool &pool = *environment.entityPool;
      (void)slot;

      if (entity.animationA0 == 5)
      {
        // The three cinematic entities the throw stood up, released here. This
        // is the *only* thing that takes the close-up rig back off the screen
        // and the only thing that unhides the player, so a swipe that never
        // runs leaves both wrong for the rest of the scene.
        if (DAT_00325900_cinematicSlots()[0] >= 0)
        {
          for (const std::int32_t cinematic : DAT_00325900_cinematicSlots())
          {
            if (cinematic >= 0 && static_cast<std::size_t>(cinematic) < pool.slotCount())
            {
              FUN_00265ec0_destroy_entity(static_cast<std::size_t>(cinematic), environment);
            }
          }
          OriginalEntity &lead = pool.slot(0);
          lead.halfword08 = static_cast<std::uint16_t>(lead.halfword08 & 0xFFFEu);
          lead.halfword04 = static_cast<std::uint16_t>(lead.halfword04 & 0xFFFEu);
          DAT_00325900_cinematicSlots() = {{-1, -1, -1}};
        }
        const OriginalEntity &player = pool.slot(0);
        const float bearing = FUN_0023a4b8_bearing(entity, player);
        entity.battleDesiredFacing19c = bearing;
        entity.crabSpeed1a0 = 50.0f;
        const float aimX = entity.positionX20 + cos_of(bearing) * kFUN_0027c8a0_bodyReach;
        const float aimZ = entity.positionZ24 + sin_of(bearing) * kFUN_0027c8a0_bodyReach;
        // FUN_0023a6d0's second argument is pool slot 0, not the crab: the
        // hold is the gap between them, not the crab's own step.
        entity.fadeRamp62 = static_cast<std::uint16_t>(
            FUN_0023a6d0_travel_ticks(entity.crabSpeed1a0, player, aimX, aimZ));
        FUN_00225bc8_set_animation(entity, 2);
        FUN_0027cef8_set_script_cue(environment, false);
      }

      const float step = FUN_0023a320_approach_angle(
          entity.facingRadians5c, entity.battleDesiredFacing19c,
          static_cast<float>(static_cast<std::int32_t>(environment.frameTicks)) *
              kDAT_003531b4_swipeTurnRate * 0.03125f);
      if (step != 0.0f)
      {
        entity.facingRadians5c += step;
        return;
      }

      if (entity.animationA0 == 2)
      {
        if (countdown(entity.fadeRamp62, environment.frameTicks) ||
            (entity.collisionFlags0c & 0x62u) != 0)
        {
          FUN_00225bc8_set_animation(entity, 8);
          return;
        }
        const float travel = (entity.crabSpeed1a0 *
                              static_cast<float>(static_cast<std::int32_t>(environment.frameTicks))) /
                             32000.0f;
        entity.desiredDeltaX30 += travel * cos_of(entity.facingRadians5c);
        entity.desiredDeltaZ34 += travel * sin_of(entity.facingRadians5c);
        return;
      }

      if (entity.animationA0 != 8)
      {
        if (DAT_0035526b_swipeDone() == 0)
        {
          FUN_0027c3e8_swipe_ladder(entity, environment);
        }
        return;
      }

      // The claw's own frame. +0xA8 is the clip cursor and bit 2 of +0x06 the
      // frame flag the timeline raises with it.
      if (entity.timelineCursorA8 == 4 && (entity.flags06 & 4u) != 0)
      {
        DAT_0035526b_swipeDone() = 1;
        // The dust, thrown a unit ahead of the crab along its facing -- the
        // original builds the point on the stack and hands it straight to
        // FUN_0027CF20.
        const orphen::ported::psm2::Vec3 impact{
            entity.positionX20 + cos_of(entity.facingRadians5c),
            entity.positionZ24 + sin_of(entity.facingRadians5c), entity.positionY28};
        FUN_0027cf20_impact_dust(impact, false, environment);
        if (environment.FUN_0022dcf0_shake_camera)
        {
          environment.FUN_0022dcf0_shake_camera(kDAT_003531b8_swipeShake,
                                                kFUN_0027c458_shakeTicks);
        }
        // FUN_0023BBD8, the pad rumble, also fires here. The port has no motor.
      }

      if ((entity.flags06 & 1u) == 0)
      {
        return;
      }

      // The clip has come round. Whoever the two tags for this swipe name gets
      // knocked out of its hold and put back on animation 1, the map group for
      // this swipe stops being terrain, and the crab drops to state 0.
      const std::uint32_t index =
          (static_cast<std::uint32_t>(DAT_0035526a_swipeCount()) - 1u) & 0xFFu;
      if (environment.FUN_00248f18_find_by_tag && index * 2u + 1u < kDAT_00355248_swipeTargets.size())
      {
        for (std::uint32_t half = 0; half < 2; ++half)
        {
          const std::int32_t found = environment.FUN_00248f18_find_by_tag(
              static_cast<std::int16_t>(kDAT_00355248_swipeTargets[index * 2u + half]));
          if (found > 0 && static_cast<std::size_t>(found) < pool.slotCount())
          {
            OriginalEntity &victim = pool.slot(static_cast<std::size_t>(found));
            victim.flags06 = static_cast<std::uint16_t>(victim.flags06 & 0xFFEFu);
            FUN_00225bc8_set_animation(victim, 1);
          }
        }
      }
      // FUN_0022dc68(1 << (count - 1), 0, 0x800): the group this swipe knocked
      // over stops being terrain. Routed through the same callback opcode 0xA6
      // uses once the actor environment carries it.
      FUN_00225bc8_set_animation(entity, 0);
    }
  } // namespace

  CrabAttackRecords &DAT_00573788_crabAttacks()
  {
    static CrabAttackRecords records;
    return records;
  }

  void FUN_00279298_crab_boss(OriginalEntity &entity,
                              std::size_t slot,
                              const ActorEnvironment &environment,
                              ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    // DAT_0058BFDA is pool slot 0's +0x12A. The crab does not run a frame while
    // the player is down -- not the state, not the helpers, not even the cues.
    if (static_cast<std::int16_t>(environment.entityPool->slot(0).staggerTimer12a) < 1)
    {
      return;
    }

    ActorEnvironment::BattleActorView view;
    const bool haveRecord = static_cast<bool>(environment.DAT_00354eb4_battleActor) &&
                            environment.DAT_00354eb4_battleActor(entity.battleActorRecord198, view);
    const auto publish = [&]()
    {
      if (haveRecord && environment.DAT_00354eb4_setBattleActor)
      {
        environment.DAT_00354eb4_setBattleActor(entity.battleActorRecord198, view);
      }
    };

    // The mode byte. 12 while a throw is in flight, 14 once the fight proper has
    // started, 0 before either.
    const std::uint8_t mode = entity.spawnParam94;
    // :22-24. The thrown pair's own flight, and the only thing that takes them
    // off the screen when they hit the water.
    if (mode == 0x0C)
    {
      FUN_0027cfe0_thrown_pair(entity, environment);
    }
    // :24-31. FUN_0027cfe0 carries the thrown pair; FUN_0027d230 is the player's
    // own half of the fight and runs for every non-zero mode. Without the second
    // one nothing ever clears DAT_0035526B and the crab takes one swing.
    if (mode != 0)
    {
      FUN_0027d230_player_dodge(entity, environment);
    }
    if (mode == 0x0E)
    {
      const std::uint32_t beat =
          environment.DAT_00355060_scriptWork ? environment.DAT_00355060_scriptWork(0) : 0;
      if (beat < kFUN_00279298_beatCeiling)
      {
        // The whole 0xC9 block plus the camera roll, cleared every frame before
        // FUN_00279180 gets a say. Any shot that wants the smear re-raises it.
        if (environment.DAT_00343878_frameFeedback != nullptr)
        {
          environment.DAT_00343878_frameFeedback->FUN_00264470_set_alpha_and_transform(0, 0, 0, 0,
                                                                                       0, 0);
        }
        if (environment.camera != nullptr)
        {
          environment.camera->setRoll(0.0f);
        }
        FUN_00279180_stage_camera(entity, environment);
        // FUN_0027DC38 sits between these two in the original. It is the
        // fight's own bookkeeping -- the live-enemy count, the target list, the
        // music step-downs and the beat that ends the battle -- and belongs
        // with the battle module rather than here; it is the one helper of the
        // six that is still out.
        FUN_0027e118_drop_wreck(entity, environment);
        FUN_0027c8a0_body_sweep(entity, slot, environment);
        if (entity.crabFlinchTimer1ca == 0)
        {
          entity.fadeColor138 = 0;
        }
        else if (countdown(entity.crabFlinchTimer1ca, environment.frameTicks))
        {
          entity.crabFlinchTimer1ca = 0;
          entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFEFu);
          entity.fadeColor138 = kFUN_00279298_flashColour;
        }
        else
        {
          entity.fadeColor138 = kFUN_00279298_flashColour;
        }
      }
      // :63. Outside the beat ceiling but inside the mode gate: the debris
      // keeps moving after the fight's own beat has passed.
      FUN_0027ed58_step_debris(entity, environment);
    }

    // :65. Outside the mode gate and outside the beat ceiling: the crab's
    // footsteps and claw slams are audible from the first frame of the scene.
    FUN_0027ef40_crab_sounds(entity, environment);

    // FUN_0023a068 inlined: +0xBD parks the action check for a few frames, and
    // the last parked frame still runs it.
    const std::int8_t freeze = entity.freezeTimerBd;
    bool runActionCheck = (freeze == 0);
    if (freeze != 0)
    {
      entity.freezeTimerBd = static_cast<std::int8_t>(freeze - 1);
      runActionCheck = (freeze == 1);
    }

    if (runActionCheck && !FUN_00279600_action_check(entity, environment, view, haveRecord))
    {
      if (entity.pendingDamageBe != 0)
      {
        if (mode == 0x0E)
        {
          entity.crabFlinchTimer1ca = kFUN_00279298_flinchTicks;
          if (static_cast<std::uint8_t>(entity.hitReactionBc - 0x19) > 1)
          {
            entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
          }
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_00279298_hitCue, entity);
          }
          if (environment.FUN_002d5630_damage_bar)
          {
            environment.FUN_002d5630_damage_bar(
                true, static_cast<std::int16_t>(entity.staggerTimer12a),
                static_cast<std::int16_t>(entity.maxHitPoints128),
                static_cast<std::int16_t>(entity.pendingDamageBe));
          }
          const float threshold =
              static_cast<float>(static_cast<std::int16_t>(entity.maxHitPoints128)) *
              kDAT_00353118_damageThreshold;
          entity.staggerTimer12a = static_cast<std::uint16_t>(
              static_cast<std::int16_t>(entity.staggerTimer12a) -
              static_cast<std::int16_t>(entity.pendingDamageBe));
          entity.crabDamageAccum1c8 = static_cast<std::int16_t>(
              entity.crabDamageAccum1c8 + static_cast<std::int16_t>(entity.pendingDamageBe));
          if (static_cast<std::int32_t>(threshold) <= entity.crabDamageAccum1c8)
          {
            entity.crabDamageAccum1c8 = 0;
            entity.crabIdleCursor1c4 = 0;
            entity.crabPhase1c5 = static_cast<std::int8_t>(entity.crabPhase1c5 ^ 1);
            entity.crabByte1ce = 0;
          }
          FUN_0027ccd0_shed_leg(entity, environment);
          // The three held victims are dropped here with a burst each. Nothing
          // in the port fills +0x1AC yet, so the loop has nothing to do.
          entity.rotationX154 = 0.0f;
          entity.crabHitReaction1c6 = FUN_0023a9d0_lowest_bit(entity.hitFlagsC2);
          FUN_00225bf0_set_state_and_animation(entity, 12, 5);
          entity.hitFlagsC2 = 0;
        }
        else
        {
          entity.hitFlagsC2 = 0;
        }
        entity.pendingDamageBe = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00325930_crabStates, kCrabStateCount, entity.state60);
    const bool implemented = entity.state60 == 0 || entity.state60 == 1 || entity.state60 == 2 ||
                             entity.state60 == 3 || entity.state60 == 5 || entity.state60 == 8 ||
                             entity.state60 == 12 || entity.state60 == 13 || entity.state60 == 14;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);

    switch (entity.state60)
    {
    case 0:
      crab_state0(entity, slot, environment);
      break;
    case 1:
      FUN_00279bf0_state1_idle(entity, environment);
      break;
    case 2:
      FUN_00279ca8_state2_hold(entity, environment);
      break;
    case 3:
      FUN_00279d60_state3_charge(entity, slot, environment);
      break;
    case 5:
      FUN_0027a440_state5_stamp(entity, environment);
      break;
    case 8:
      FUN_0027aaf8_state8_reposition(entity, environment);
      break;
    case 12:
      FUN_0027b7c8_state12_hit(entity, environment);
      break;
    case 13:
      FUN_0027bbe0_state13_throw(entity, slot, environment);
      break;
    case 14:
      FUN_0027c458_state14_swipe(entity, slot, environment);
      break;
    default:
      break;
    }

    FUN_0027ce48_splash(entity, environment);
    publish();
  }

  void FUN_00216078_fill_crab_records(std::int16_t typeId, const ActorEnvironment &environment)
  {
    if (environment.DAT_00354d6c_hitParameters == nullptr)
    {
      return;
    }
    CrabAttackRecords &records = DAT_00573788_crabAttacks();
    for (std::uint32_t index = 0; index < records.record.size(); ++index)
    {
      const auto parameters =
          environment.DAT_00354d6c_hitParameters->FUN_00216078_record(typeId, index);
      if (parameters.has_value())
      {
        records.record[index] = *parameters;
        records.filled = true;
      }
    }
  }

} // namespace orphen::ported::entity
