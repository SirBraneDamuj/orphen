#include "ported/debug/original_player_param_menu.h"

#include <cstdint>

namespace orphen::ported::debug
{

  void FUN_0026bc50_step_player_param(orphen::ported::entity::OriginalEntity &lead,
                                      PlayerParam param,
                                      PlayerParamStep step)
  {
    std::uint16_t *row = nullptr;
    switch (param)
    {
    case PlayerParam::HitPoints: row = &lead.staggerTimer12a; break;
    case PlayerParam::Strength: row = &lead.attackPower12c; break;
    case PlayerParam::Defence: row = &lead.defence12e; break;
    }
    const std::uint16_t old = *row;

    switch (step)
    {
    case PlayerParamStep::Up1:
      // :50-54. The sum is compared as a signed halfword.
      *row = static_cast<std::uint16_t>(old + 1);
      if (static_cast<std::int16_t>(*row) > 999)
      {
        *row = 0;
      }
      break;
    case PlayerParamStep::Down1:
      // :76-80. Signed below zero is the wrap.
      *row = static_cast<std::uint16_t>(old - 1);
      if (static_cast<std::int16_t>(*row) < 0)
      {
        *row = 999;
      }
      break;
    case PlayerParamStep::Up10:
      // :60-65. Not a modulo: 995 + 10 lands on 6, not 5.
      *row = static_cast<std::uint16_t>(old + 10);
      if (static_cast<std::int16_t>(*row) >= 1000)
      {
        *row = static_cast<std::uint16_t>(old - 0x3DD);
      }
      break;
    case PlayerParamStep::Down10:
      // :68-73.
      *row = static_cast<std::uint16_t>(old - 10);
      if (static_cast<std::int16_t>(*row) < 0)
      {
        *row = static_cast<std::uint16_t>(old + 0x3DD);
      }
      break;
    }

    // :101-104, DAT_0058bfd8 = DAT_0058bfda, floored at 1.
    lead.maxHitPoints128 = lead.staggerTimer12a;
    if (static_cast<std::int16_t>(lead.staggerTimer12a) < 1)
    {
      lead.maxHitPoints128 = 1;
    }
  }

} // namespace orphen::ported::debug
