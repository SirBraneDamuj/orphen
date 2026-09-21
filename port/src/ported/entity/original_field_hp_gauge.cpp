#include "ported/entity/original_field_hp_gauge.h"

#include <algorithm>

namespace orphen::ported::entity
{
  namespace
  {
    // 0x002D0F64 / 0x002D0FA4 / 0x002D0FDC. The saturated value of +0x98, which
    // is 0xF00 ticks -- 120 frames at the 0x20 every frame advances by.
    inline constexpr std::int32_t kFUN_002d0ea8_holdFull = 0x0F00;

    // 0x002D1008. Five pips are the battle bar's business; this one is banded
    // into 31 animation frames.
    inline constexpr std::int32_t kGaugeAnimationSteps = 0x1E;

    // 0x002D0F08. A lead type at or above this is not a player character, so
    // the gauge has nothing to report.
    inline constexpr std::int16_t kMaxLeadTypeId = 0x3A;

    // FUN_00266368's argument at 0x002D0F18. SFLG 11 -- see the flag banks in
    // docs; nothing in the shipped scripts sets it.
    inline constexpr std::uint32_t kGaugeSuppressFlag = 0x50B;

    // 0x002D0F54-0x002D0F5C. The lead states FUN_00251ED8 puts it in for the
    // three hit reactions, which are what force the gauge up.
    inline constexpr std::int16_t kFirstHitReactionState = 0x16;
    inline constexpr std::int16_t kLastGaugeReactionState = 0x18;

    void hide(OriginalEntity &gauge)
    {
      // 0x002D0F28-0x002D0F3C, the one block every rejected gate falls into.
      gauge.fadeRamp62 = 1;
      gauge.gaugeHold98 = 0;
      gauge.halfword08 = static_cast<std::uint16_t>(gauge.halfword08 | 1u);
    }

    void show(OriginalEntity &gauge)
    {
      gauge.halfword08 = static_cast<std::uint16_t>(gauge.halfword08 & 0xFFFEu);
    }
  } // namespace

  void FUN_0022a418_build_field_hp_gauge(EntityPool &pool,
                                         const EntityDescriptorTable &descriptors,
                                         bool DAT_003555d3_groupEScene,
                                         bool DAT_003555c6_arenaMode,
                                         int DAT_003551f4_stage)
  {
    if (DAT_003555d3_groupEScene || DAT_003555c6_arenaMode || DAT_003551f4_stage == 0x0C)
    {
      return;
    }

    pool.FUN_00229c40_initialize(kDAT_0058cd70_fieldHpGaugeSlot, kFieldHpGaugeTypeId, descriptors);
    OriginalEntity &gauge = pool.slot(kDAT_0058cd70_fieldHpGaugeSlot);

    // FUN_002662e0(48.0, 32.0, DAT_003524E0, gauge): +0x20, +0x24, +0x28 and
    // +0x28 again into +0x4C. On the bit-0x1000 branch +0x28 is a GS depth
    // word rather than a world height, and the port keeps the integer form of
    // it beside the float the same way the battle bar and the target cursor do.
    gauge.positionX20 = kFUN_0022a418_gaugeScreenX;
    gauge.positionZ24 = kFUN_0022a418_gaugeScreenY;
    gauge.positionY28 = kDAT_003524e0_gaugeDepth;
    gauge.groundHeight4c = kDAT_003524e0_gaugeDepth;
    gauge.cursorProjectedDepth28 = static_cast<std::int32_t>(kDAT_003524e0_gaugeDepth);
  }

  void FUN_002d0ea8_field_hp_gauge(OriginalEntity &gauge,
                                   const OriginalEntity &lead,
                                   const FieldHpGaugeEnvironment &environment)
  {
    // 0x002D0EBC-0x002D0F1C, the gate. Every failure lands on the same hide.
    if (environment.DAT_00354d2c_gameMode != 0 ||
        environment.DAT_00355054_letterboxMode != 0 ||
        environment.DAT_003551ec_sceneRequest != 0 || lead.typeId00 == 0 ||
        ((lead.halfword08 & 1u) != 0 && !environment.DAT_00355654) ||
        lead.typeId00 >= kMaxLeadTypeId || environment.DAT_0034ab70_flag50b)
    {
      hide(gauge);
      return;
    }

    const std::int16_t leadState = static_cast<std::int16_t>(lead.state60);
    const std::int32_t ticks = static_cast<std::int32_t>(environment.frameTicks);
    bool skipToAnimation = false;

    if (leadState == 0)
    {
      // 0x002D0F80-0x002D0FC8. +0x62 is a short lockout the reaction branch
      // arms; while it is running the gauge only restamps its animation.
      if (gauge.fadeRamp62 != 0)
      {
        gauge.fadeRamp62 = static_cast<std::uint16_t>(gauge.fadeRamp62 - 1);
        if (static_cast<std::int16_t>(gauge.fadeRamp62) > 0)
        {
          skipToAnimation = true;
        }
        else
        {
          gauge.fadeRamp62 = 0;
          gauge.gaugeHold98 = 0;
        }
      }
      if (!skipToAnimation)
      {
        if (gauge.gaugeHold98 < kFUN_002d0ea8_holdFull)
        {
          gauge.gaugeHold98 += ticks;
        }
        else
        {
          show(gauge);
        }
      }
    }
    else if (leadState >= kFirstHitReactionState && leadState <= kLastGaugeReactionState)
    {
      // 0x002D0F60-0x002D0F7C. A hit reaction: saturate the hold, arm the
      // one-frame lockout and put the bar up.
      gauge.gaugeHold98 = kFUN_002d0ea8_holdFull;
      gauge.fadeRamp62 = 1;
      show(gauge);
    }
    else
    {
      // 0x002D0FCC-0x002D1000. Any other state -- a negative one, 0x19 and
      // above, or below 0x16 -- runs the hold *down*. The two ways in
      // (0x002D0F58's `bnel` for a state below 0x16, and 0x002D0FCC for the
      // rest) differ only in which instruction loads +0x62, so they are one
      // block.
      if (gauge.fadeRamp62 == 0)
      {
        gauge.fadeRamp62 = 3;
        gauge.gaugeHold98 = kFUN_002d0ea8_holdFull;
      }

      if (gauge.gaugeHold98 > 0)
      {
        gauge.gaugeHold98 -= ticks;
        if (gauge.gaugeHold98 <= 0)
        {
          gauge.gaugeHold98 = 0;
        }
      }
    }

    // 0x002D1004-0x002D1034. The original traps on a zero maximum; the port
    // leaves the animation alone instead, because a port scene can reach this
    // with the lead's stats never loaded and the original cannot.
    const std::int32_t maximum = static_cast<std::int16_t>(lead.maxHitPoints128);
    if (maximum == 0)
    {
      return;
    }
    const std::int32_t hitPoints = static_cast<std::int16_t>(lead.staggerTimer12a);
    const std::int32_t banded =
        std::min(hitPoints * kGaugeAnimationSteps / maximum, kGaugeAnimationSteps);
    gauge.animationA0 = static_cast<std::uint16_t>(kGaugeAnimationSteps - banded);
  }

} // namespace orphen::ported::entity
