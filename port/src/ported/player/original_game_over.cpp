#include "ported/player/original_game_over.h"

#include <algorithm>

namespace orphen::ported::player
{
  namespace
  {
    // 0x00255820's own constants.
    constexpr float kGameOverLightRadius = 2.5f;   // 0x40200000
    constexpr float kGameOverLightLift = 0.5f;     // above the body's +0x28
    constexpr std::uint16_t kFadeCapRamp = 0x0FE0; // +0x1B6
    constexpr float kHoldTicks = 19200.0f;         // 0x46960000, ten seconds
    constexpr std::uint8_t kDeathFadeLevel = 0x7C; // +0x134
    constexpr std::uint16_t kStateGameOverHold = 0x1B;

    // FUN_00255820:5-9. The scene's lighting is replaced wholesale, the same way
    // FUN_002342C0 does it for the item close-up, and the only difference is
    // that the ambient is 0x202020 rather than 0x404040.
    constexpr std::uint32_t kGameOverAmbient = 0x00202020;
    constexpr std::uint32_t kGameOverLightColour = 0x00808080;

    // 0x002559E8's. +0x62 climbs four ticks a tick to 0x2000 and the level is
    // it over 32, so the ramp is 64 frames and the level tops out at 255.
    constexpr std::int32_t kOverlayFull = 0x2000;
    constexpr int kOverlayTickScale = 4;
    constexpr int kFadeCapTickScale = 2;
    // 0x00255B08. Each of the other lights loses this off every channel.
    constexpr int kLightDecayPerFrame = 4;
    // 0x00255BC0. The walk starts at pool slot 10 and runs 0xF5 entries, so it
    // stops one short of the end of the 256-slot pool.
    constexpr std::size_t kFirstFadedSlot = 10;
    constexpr std::size_t kFadedSlotCount = 0xF5;
    // The one value the cap is allowed to rest at. FUN_00209140 draws the map
    // for `cap == 0 || cap > 3`, so 3 is "off" without being the 0 that means
    // "no cap at all".
    constexpr std::uint8_t kFadeCapFloor = 3;

    // The arithmetic shift the original spells out with `addiu 0x1f; movn; srl`
    // -- a divide by 32 that rounds toward zero, then truncated to a byte.
    std::uint8_t levelFromRamp(std::int32_t ramp)
    {
      const std::int32_t divided = (ramp < 0 ? ramp + 31 : ramp) >> 5;
      return static_cast<std::uint8_t>(divided);
    }
  } // namespace

  void FUN_00255820_enter_game_over(orphen::ported::entity::OriginalEntity &lead,
                                    const GameOverHooks &hooks)
  {
    // 0x00255828. The body's own light, out of the high half of the table. It
    // starts black -- +0x0C is written as the word 0x01000000, so rgb 0 and
    // alpha 1 -- and FUN_002559E8 is what raises it.
    std::int32_t lightSlot = -1;
    if (hooks.DAT_00343888_lights != nullptr)
    {
      lightSlot = hooks.DAT_00343888_lights->FUN_00266008_allocateFromThree();
    }
    lead.lightSlot195 = static_cast<std::int8_t>(lightSlot);
    if (lightSlot >= 0 && hooks.DAT_00343888_lights != nullptr)
    {
      auto &light = hooks.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
      light.radius = kGameOverLightRadius;
      light.red = 0;
      light.green = 0;
      light.blue = 0;
      light.alpha = 1;
      light.x = lead.positionX20;
      light.y = lead.positionZ24;
      light.z = lead.positionY28 + kGameOverLightLift;
      hooks.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(lightSlot),
                                            kGameOverLightRadius);
    }

    // 0x0025589C. The state itself. +0x08 bit 2 off is what stops the body
    // being a collision target; +0x134 at 0x7C is a *full* fade level, so the
    // body is drawn solid for the whole sequence and never fades with the room.
    lead.state60 = kStateGameOverHold;
    lead.fadeRamp62 = 0;
    lead.idleTimer1b6 = kFadeCapRamp;
    lead.playerSavedScaleZ1a4 = kHoldTicks;
    lead.halfword08 = static_cast<std::uint16_t>(lead.halfword08 & 0xFFFBu);
    lead.fadeLevel134 = kDeathFadeLevel;

    // 0x002558C4. Slots 4 and 7 go with the player, children and all.
    if (hooks.FUN_00265ec0_release)
    {
      hooks.FUN_00265ec0_release(4);
      hooks.FUN_00265ec0_release(7);
    }

    if (hooks.FUN_002d36f8_install_dust)
    {
      hooks.FUN_002d36f8_install_dust();
    }
    if (hooks.stopEffectPools)
    {
      hooks.stopEffectPools();
    }

    // 0x002558E0. The scene's lighting, replaced. The direction is written
    // straight down and then normalised by FUN_00216510; (0, 0, -1) already is.
    if (hooks.DAT_0035566c_ambient != nullptr)
    {
      *hooks.DAT_0035566c_ambient = kGameOverAmbient;
    }
    if (hooks.DAT_00355670_lightColour != nullptr)
    {
      *hooks.DAT_00355670_lightColour = kGameOverLightColour;
    }
    if (hooks.DAT_003439c8_lightDirection != nullptr)
    {
      hooks.DAT_003439c8_lightDirection[0] = 0.0f;
      hooks.DAT_003439c8_lightDirection[1] = 0.0f;
      hooks.DAT_003439c8_lightDirection[2] = -1.0f;
    }
    if (hooks.applySceneEnvironment)
    {
      hooks.applySceneEnvironment();
    }

    if (hooks.FUN_00255820_stage_camera)
    {
      hooks.FUN_00255820_stage_camera();
    }
    // 0x00255964. Free-look hides the body; dying out of it has to put it back.
    if (hooks.clearFreeLook && hooks.clearFreeLook())
    {
      lead.halfword08 = static_cast<std::uint16_t>(lead.halfword08 & 0xFFFEu);
    }

    if (hooks.FUN_00255820_stage_sound)
    {
      hooks.FUN_00255820_stage_sound();
    }
  }

  void FUN_002559e8_update_game_over(orphen::ported::entity::OriginalEntity &lead,
                                     std::uint32_t frameTicks,
                                     const GameOverHooks &hooks)
  {
    const std::int32_t ticks = static_cast<std::int32_t>(frameTicks);

    // `s2` in the original: non-zero while either ramp is still running, which
    // is what holds the ten-second timer off until the picture has settled.
    int ramping = 0;

    // ---- the black quad, the body's light and the fog -----------------------
    const std::int32_t overlayRamp = static_cast<std::int16_t>(lead.fadeRamp62);
    if (overlayRamp < kOverlayFull)
    {
      const std::uint8_t level = levelFromRamp(overlayRamp);
      if (lead.lightSlot195 >= 0 && hooks.DAT_00343888_lights != nullptr)
      {
        auto &light =
            hooks.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lead.lightSlot195));
        light.red = level;
        light.green = level;
        light.blue = level;
      }
      if (hooks.FUN_00255ce8_black_quad)
      {
        hooks.FUN_00255ce8_black_quad(level);
      }
      ramping = 1;

      // 0x00255A60. The fog is multiplied by the *inverse* of the same level,
      // per channel, so it reaches black on the frame the quad reaches opaque.
      if (hooks.DAT_00355674_fogColour != nullptr)
      {
        const std::uint32_t inverse = static_cast<std::uint8_t>(~level);
        const std::uint32_t fog = *hooks.DAT_00355674_fogColour;
        const std::uint32_t red = (((fog >> 16) & 0xFFu) * inverse) >> 8;
        const std::uint32_t green = (((fog >> 8) & 0xFFu) * inverse) >> 8;
        const std::uint32_t blue = ((fog & 0xFFu) * inverse) >> 8;
        *hooks.DAT_00355674_fogColour = (red << 16) | (green << 8) | blue;
      }

      lead.fadeRamp62 =
          static_cast<std::uint16_t>(overlayRamp + ticks * kOverlayTickScale);
    }
    else if (hooks.FUN_00255ce8_black_quad)
    {
      hooks.FUN_00255ce8_black_quad(0xFF);
    }

    // ---- every other dynamic light dims out ---------------------------------
    if (hooks.DAT_00343888_lights != nullptr)
    {
      for (std::uint32_t index = 0;
           index < orphen::ported::render::LightTable::kSlotCount; ++index)
      {
        if (static_cast<std::int32_t>(index) == static_cast<std::int32_t>(lead.lightSlot195))
        {
          continue;
        }
        auto &light = hooks.DAT_00343888_lights->slot(index);
        if (light.radius == 0.0f)
        {
          continue;
        }
        const int red = std::max(0, static_cast<int>(light.red) - kLightDecayPerFrame);
        const int green = std::max(0, static_cast<int>(light.green) - kLightDecayPerFrame);
        const int blue = std::max(0, static_cast<int>(light.blue) - kLightDecayPerFrame);
        light.red = static_cast<std::uint8_t>(red);
        light.green = static_cast<std::uint8_t>(green);
        light.blue = static_cast<std::uint8_t>(blue);
        // Only when all three have bottomed out is the slot handed back, and
        // the radius is what hands it back.
        if ((red + green + blue) == 0)
        {
          light.radius = 0.0f;
        }
      }
    }

    // ---- the map's draw gate, and every entity from slot 10 up --------------
    if (static_cast<std::int16_t>(lead.idleTimer1b6) >= 0x81)
    {
      lead.idleTimer1b6 = static_cast<std::uint16_t>(
          lead.idleTimer1b6 - static_cast<std::uint16_t>(ticks * kFadeCapTickScale));
      const std::int16_t remaining = static_cast<std::int16_t>(lead.idleTimer1b6);
      std::uint8_t fade = 0;
      if (remaining >= 0x80)
      {
        fade = levelFromRamp(remaining);
      }
      if (hooks.DAT_00355700_globalFadeCap != nullptr)
      {
        *hooks.DAT_00355700_globalFadeCap = fade != 0 ? fade : kFadeCapFloor;
      }

      if (hooks.DAT_0058beb0_pool != nullptr)
      {
        auto &pool = *hooks.DAT_0058beb0_pool;
        // 0x00255BD4: 0xF5 iterations from slot 10, so 10..254. Slot 255 is
        // genuinely outside the walk.
        for (std::size_t index = kFirstFadedSlot; index < kFirstFadedSlot + kFadedSlotCount;
             ++index)
        {
          if (pool.status(index) == orphen::ported::entity::SlotStatus::Free)
          {
            continue;
          }
          auto &entity = pool.slot(index);
          if (entity.typeId00 <= 0)
          {
            continue;
          }
          entity.fadeLevel134 = fade;
          if (fade == 0)
          {
            entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
          }
          // +0x06 = 0x10 and +0x04 |= 0x4000 together take the entity out of
          // the animation and physics passes: it is frozen where it stands for
          // whatever is left of the fade.
          entity.flags06 = 0x0010;
          entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x4000u);
        }
      }
      ramping += 1;
    }

    if (ramping == 0)
    {
      // 0x00255C40. The store is in the branch delay slot, so +0x1A4 is always
      // decremented whether or not this is the frame it runs out on.
      lead.playerSavedScaleZ1a4 -= static_cast<float>(ticks);
      const bool expired = lead.playerSavedScaleZ1a4 <= 0.0f;
      const bool skipped = hooks.skipRequested && hooks.skipRequested();
      if (expired || skipped)
      {
        // FUN_00237A08 puts the player back in the hands of whatever comes
        // next, and the state it leaves behind is the script-driven one.
        lead.state60 = 10;
        if (hooks.onHandOff)
        {
          hooks.onHandOff();
        }
      }
    }

    if (hooks.FUN_002559e8_hold_camera)
    {
      hooks.FUN_002559e8_hold_camera();
    }
    if (hooks.applySceneEnvironment)
    {
      hooks.applySceneEnvironment();
    }
  }

} // namespace orphen::ported::player
