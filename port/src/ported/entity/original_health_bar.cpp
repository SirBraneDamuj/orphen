#include "ported/entity/original_health_bar.h"

#include <algorithm>

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_002d5630's two banks, read out of the executable rather than guessed:
    // 0x0058C3FC / 0x0058C5D4 are the screen row each starts on, DAT_00354700
    // and DAT_003546FC the per-frame slide, and 0x0058C40C / 0x0058C5E4 the
    // animation base. The lower bar climbs, the upper one drops.
    inline constexpr float kDAT_0058c3fc_lowerStartRow = 456.0f;
    inline constexpr float kDAT_00354700_lowerStep = -2.6666667461395264f;
    inline constexpr std::uint16_t kDAT_0058c40c_lowerAnimationBase = 0;
    inline constexpr float kDAT_0058c5d4_upperStartRow = -8.0f;
    inline constexpr float kDAT_003546fc_upperStep = 2.6666667461395264f;
    inline constexpr std::uint16_t kDAT_0058c5e4_upperAnimationBase = 0x14;

    // The X the bar is pinned to and the depth word it draws at, both immediate
    // in FUN_002d5748's tail: 320.0 and DAT_00354704.
    inline constexpr float kFUN_002d5748_screenX = 320.0f;
    inline constexpr float kDAT_00354704_depth = 65534.0f;

    // The five phases +0x198 walks, by the constant each branch compares to.
    inline constexpr std::int16_t kSlideInFrames = 0x0F;
    inline constexpr std::int16_t kFirstDrainPhase = 0x0F;
    inline constexpr std::int16_t kDrainPhase = 0x10;
    inline constexpr std::int16_t kHoldEndPhase = 0x41;
    inline constexpr std::int16_t kSlideOutEndPhase = 0x50;

    // The bar walks one step per 32 ticks, the same unit every battle timer is
    // armed in.
    inline constexpr std::int32_t kTickStep = 0x20;

    // FUN_002d5630:41-45. Five pips, rounded up, of a value already clamped
    // into 0..max.
    std::int32_t pipsFor(std::int32_t value, std::int32_t maximum)
    {
      return (value * 5 + maximum - 1) / maximum;
    }
  } // namespace

  void FUN_0022a418_build_health_bars(EntityPool &pool, const EntityDescriptorTable &descriptors)
  {
    // FUN_00229c40(0x58c260, 0x68); DAT_0058c300 = 1; DAT_0058c268 |= 1 -- the
    // animation it rests on and +0x08 bit 0, which is what keeps it out of both
    // the behaviour and the draw until FUN_002d5630 clears it.
    for (const std::size_t slot : {kDAT_0058c260_lowerBarSlot, kDAT_0058c438_upperBarSlot})
    {
      pool.FUN_00229c40_initialize(slot, kHealthBarTypeId, descriptors);
      OriginalEntity &bar = pool.slot(slot);
      bar.animationA0 = 1;
      bar.halfword08 = static_cast<std::uint16_t>(bar.halfword08 | 1u);
    }
  }

  void FUN_002d5630_arm_health_bar(EntityPool &pool,
                                   bool upperBank,
                                   std::int32_t hitPoints,
                                   std::int32_t maxHitPoints,
                                   std::int32_t damage,
                                   bool DAT_003555d3_groupEScene)
  {
    // :10. The bar is a battle-section fixture; outside one the slots are not
    // even built. A zero maximum would divide by zero (the original traps on
    // exactly that a few lines down) and a hit that took nothing raises nothing.
    if (!DAT_003555d3_groupEScene || maxHitPoints <= 0 || damage <= 0)
    {
      return;
    }

    const std::size_t slot = upperBank ? kDAT_0058c438_upperBarSlot : kDAT_0058c260_lowerBarSlot;
    OriginalEntity &bar = pool.slot(slot);
    // The original has no such test -- it writes through a fixed address. The
    // port needs one because slot 2 is also where FUN_00254f60 builds the chest
    // item, and where the original overlays one union the port has two sets of
    // named fields. The two can never be live together (the chest is a field
    // cutscene and the bar only arms in a section-14 scene), so this changes no
    // behaviour; it just refuses to write bar fields into something else.
    if (bar.typeId00 != kHealthBarTypeId)
    {
      return;
    }

    bar.healthBarY19c = upperBank ? kDAT_0058c5d4_upperStartRow : kDAT_0058c3fc_lowerStartRow;
    bar.healthBarStep1a0 = upperBank ? kDAT_003546fc_upperStep : kDAT_00354700_lowerStep;
    bar.healthBarAnimationBase1ac =
        upperBank ? kDAT_0058c5e4_upperAnimationBase : kDAT_0058c40c_lowerAnimationBase;

    // :23-24. Restart the walk from the top and start drawing again, whatever
    // the bar was in the middle of.
    bar.healthBarPhase198 = 0;
    bar.halfword08 = static_cast<std::uint16_t>(bar.halfword08 & 0xFFFEu);

    // :25-40. Where the bar starts, and where this hit leaves it. The hit
    // points are still the pre-hit total -- the type wrapper drains +0x12A
    // afterwards -- so the two bands are genuinely before and after.
    const std::int32_t before =
        hitPoints < 0 ? 0 : std::min(hitPoints, maxHitPoints);
    const std::int32_t spent = std::min(damage, maxHitPoints);
    std::int32_t after = hitPoints - spent;
    after = after < 0 ? 0 : std::min(after, maxHitPoints);

    const std::int32_t segments = pipsFor(before, maxHitPoints);
    bar.healthBarSegments1a4 = segments;
    bar.healthBarTarget1a8 = pipsFor(after, maxHitPoints);

    // :46-51. The arming pose is the third bank, 0x15 above the base. Written
    // straight to +0xA0, not through FUN_00225BC8.
    bar.animationA0 = segments != 0
                          ? static_cast<std::uint16_t>(bar.healthBarAnimationBase1ac -
                                                       static_cast<std::uint16_t>(segments - 0x15))
                          : static_cast<std::uint16_t>(0);
  }

  void FUN_002d5748_health_bar(OriginalEntity &bar, std::uint32_t frameTicks)
  {
    // +0x08 bit 0: nothing has armed it, or it has already slid back off.
    if ((bar.halfword08 & 1u) != 0)
    {
      return;
    }

    std::int32_t remaining = static_cast<std::int32_t>(frameTicks);
    for (;;)
    {
      const std::int16_t phase = static_cast<std::int16_t>(bar.healthBarPhase198);
      if (phase < kSlideInFrames)
      {
        // Sliding on.
        bar.healthBarPhase198 = static_cast<std::uint16_t>(bar.healthBarPhase198 + 1);
        bar.healthBarY19c += bar.healthBarStep1a0;
      }
      else if (phase == kFirstDrainPhase)
      {
        // One frame, and it always advances: the first pip comes off here
        // whether or not the clip has looped.
        const std::int32_t segments = bar.healthBarSegments1a4;
        if (bar.healthBarTarget1a8 < segments)
        {
          bar.animationA0 = segments != 0
                                ? static_cast<std::uint16_t>(
                                      bar.healthBarAnimationBase1ac -
                                      static_cast<std::uint16_t>(segments - 6))
                                : static_cast<std::uint16_t>(0);
          bar.healthBarSegments1a4 = segments - 1;
        }
        else
        {
          bar.animationA0 = segments != 0
                                ? static_cast<std::uint16_t>(
                                      bar.healthBarAnimationBase1ac -
                                      static_cast<std::uint16_t>(segments - 11))
                                : static_cast<std::uint16_t>(0);
        }
        bar.healthBarPhase198 = static_cast<std::uint16_t>(bar.healthBarPhase198 + 1);
      }
      else if (phase == kDrainPhase)
      {
        // One pip per animation loop from here, and the phase only moves on
        // once the bar has reached the count the hit left it at.
        if ((bar.flags06 & 1u) != 0)
        {
          const std::int32_t segments = bar.healthBarSegments1a4;
          if (bar.healthBarTarget1a8 < segments)
          {
            bar.animationA0 = segments != 0
                                  ? static_cast<std::uint16_t>(
                                        bar.healthBarAnimationBase1ac -
                                        static_cast<std::uint16_t>(segments - 6))
                                  : static_cast<std::uint16_t>(0);
            bar.healthBarSegments1a4 = segments - 1;
          }
          else
          {
            bar.healthBarPhase198 = static_cast<std::uint16_t>(bar.healthBarPhase198 + 1);
            bar.animationA0 = segments != 0
                                  ? static_cast<std::uint16_t>(
                                        bar.healthBarAnimationBase1ac -
                                        static_cast<std::uint16_t>(segments - 11))
                                  : static_cast<std::uint16_t>(0);
          }
        }
      }
      else if (phase < kHoldEndPhase)
      {
        // Holding, forty-eight frames of it.
        bar.healthBarPhase198 = static_cast<std::uint16_t>(bar.healthBarPhase198 + 1);
      }
      else if (phase < kSlideOutEndPhase)
      {
        // Sliding back off, the same fifteen steps in reverse.
        bar.healthBarPhase198 = static_cast<std::uint16_t>(bar.healthBarPhase198 + 1);
        bar.healthBarY19c -= bar.healthBarStep1a0;
      }
      else
      {
        // Done. The original returns from here, so the position below is not
        // rewritten on the frame it stops drawing.
        bar.halfword08 = static_cast<std::uint16_t>(bar.halfword08 | 1u);
        return;
      }

      remaining -= kTickStep;
      if (remaining <= 0)
      {
        break;
      }
    }

    // FUN_002662e0(320.0, +0x19C, DAT_00354704, entity). The bar is pinned to
    // the middle of the screen and rides +0x19C up or down; +0x28 is the
    // screen-space branch's GS depth word rather than a world height, which the
    // port keeps beside it the same way the target cursor does.
    bar.positionX20 = kFUN_002d5748_screenX;
    bar.positionZ24 = bar.healthBarY19c;
    bar.positionY28 = kDAT_00354704_depth;
    bar.groundHeight4c = kDAT_00354704_depth;
    bar.cursorProjectedDepth28 = static_cast<std::int32_t>(kDAT_00354704_depth);
  }

} // namespace orphen::ported::entity
