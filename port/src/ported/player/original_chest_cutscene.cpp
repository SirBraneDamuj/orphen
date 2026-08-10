#include "ported/player/original_chest_cutscene.h"

#include <cmath>

namespace orphen::ported::player
{
  namespace
  {
    using orphen::ported::entity::OriginalEntity;
    using orphen::ported::render::ScreenFade;

    // FUN_00254db0's constants, at 0x00352958..0x00352964.
    constexpr float fGpffff89e8_halfTurn = 3.14159202575683594f;  // pi
    constexpr float fGpffff89ec_standOffDistance = 0.372f;
    constexpr float fGpffff89f0_cameraYawOffset = 0.785398006439209f; // pi/4
    constexpr float fGpffff89f4_cameraLookHeight = 0.6f;

    // Inline in FUN_00254db0: the camera trails 2.5 out and sits 0.5 up.
    constexpr float kCameraTrail = 2.5f;
    constexpr float kCameraHeight = 0.5f;

    // State 0x13's constants, at 0x00352968..0x00352974.
    constexpr float kExitHalfTurn = 3.14159202575683594f;
    constexpr float kExitGroundProbeLift = 0.3f;
    constexpr float kExitGroundClearance = 0.2f;
    constexpr float kExitNudge = 0.001f;

    // FUN_00254f60's item branch: chest +0x12A is the item's pool slot and
    // chest +0x62 seeds the cross-fade ramp.
    constexpr std::uint16_t kItemCrossFadeRamp = 0x0fe0;
    constexpr std::uint16_t kItemPoolSlot = 2;

    // Animation 0x57 is the twelve-keyframe chest open. Its keyframe 1 carries
    // 0x100 in the trailing word, which is the "the lid is up" event.
    constexpr std::uint16_t kAnimationOpenChest = 0x57;

    // Entity +0xAA, the trailing word the timeline entry carried, and the two
    // +0x06 bits FUN_00225c90 publishes.
    constexpr std::uint16_t kTimelineEventMarker = 0x0100;
    constexpr std::uint16_t kAnimationSteppedThisFrame = 0x0008;
    constexpr std::uint16_t kAnimationFinished = 0x0001;
    // +0x06 bit 0x80 holds the timeline cursor on the last keyframe rather
    // than restarting it, which is what leaves the player posed over the chest.
    constexpr std::uint16_t kHoldOnLastKeyframe = 0x0080;

    // FUN_00216690: fold an angle back into (-pi, pi].
    float FUN_00216690_wrapAngle(float angle)
    {
      constexpr float kTwoPi = 6.28318530717958648f;
      for (int guard = 0; guard < 16; ++guard)
      {
        if (angle > fGpffff89e8_halfTurn)
        {
          angle -= kTwoPi;
        }
        else if (angle < -fGpffff89e8_halfTurn)
        {
          angle += kTwoPi;
        }
        else
        {
          return angle;
        }
      }
      return angle;
    }

    // FUN_00225bf0.
    void FUN_00225bf0_set_state(OriginalEntity &entity, std::uint16_t state, std::uint16_t animation)
    {
      entity.state60 = state;
      entity.stateResetA4 = 999;
      entity.animationA0 = animation;
      entity.previousSubstateA2 = 0xffff;
      entity.flags06 &= 0xff38;
      entity.timelineCursorA8 = 0;
    }

    OriginalEntity *chestFor(const ChestCutsceneContext &context)
    {
      const std::int32_t slot = context.player->interactTarget198;
      if (slot < 0 || static_cast<std::size_t>(slot) >= orphen::ported::entity::kEntitySlotCount)
      {
        return nullptr;
      }
      return &context.pool->slot(static_cast<std::size_t>(slot));
    }

    // ---------------------------------------------------------------------
    // 0x0C -- FUN_00254d58
    // ---------------------------------------------------------------------
    void state0C_armFadeOut(const ChestCutsceneContext &context)
    {
      // FUN_00237b38(0) closes the item window. Nothing to close here.
      context.fade->FUN_0025d1c0_arm(true, ScreenFade::kCutsceneSpeed, 0x000000);
      context.player->state60 = 0x0D;
      // DAT_0031e190 / DAT_0031e194 are the item window's own two words.
    }

    // ---------------------------------------------------------------------
    // 0x0D -- FUN_00254db0
    // ---------------------------------------------------------------------
    void state0D_placeAndFrame(const ChestCutsceneContext &context)
    {
      if (!context.fade->FUN_0025d238_step_fade_out(context.frameTicks))
      {
        return;
      }

      OriginalEntity *chest = chestFor(context);
      if (chest == nullptr)
      {
        // Nothing to open. Bail rather than hang behind a black screen.
        context.fade->FUN_0025d1c0_arm(false, ScreenFade::kCutsceneSpeed, 0x000000);
        FUN_00225bf0_set_state(*context.player, 0, 1);
        return;
      }

      OriginalEntity &player = *context.player;

      // Stand the player fGpffff89ec in front of the chest, facing it.
      const float standAngle =
          FUN_00216690_wrapAngle(chest->facingRadians5c + fGpffff89e8_halfTurn);
      player.positionX20 = chest->positionX20 + std::cos(standAngle) * fGpffff89ec_standOffDistance;
      player.positionZ24 = chest->positionZ24 + std::sin(standAngle) * fGpffff89ec_standOffDistance;
      player.positionY28 = chest->positionY28;
      player.groundHeight4c = chest->groundHeight4c;

      // FUN_002342c0 here. Two of the things it does are what make the
      // cutscene look like a cutscene rather than a pose in a lit room.
      //
      // Its tail loop hides every entity from pool slot 2 up -- the other six
      // chests, the party, the enemies, and the player's own bandana, which is
      // slot 4. Slot 0 is not in the loop, and the chest is un-hidden a few
      // lines below, which is why those two are all that is left.
      for (std::size_t slot = 2; slot < orphen::ported::entity::kEntitySlotCount; ++slot)
      {
        OriginalEntity &other = context.pool->slot(slot);
        other.halfword04 = static_cast<std::uint16_t>(other.halfword04 | 0x4000);
        other.halfword08 = static_cast<std::uint16_t>(other.halfword08 | 0x0001);
      }

      // And its FUN_002340e0 spin leaves the map's global fade cap at 3
      // against a 0x80 = x1.0 scale, which is the black room.
      if (context.DAT_00355700_globalFadeCap != nullptr)
      {
        *context.DAT_00355700_globalFadeCap = kCutsceneFadeCap;
      }

      // The cap makes the map nearly transparent rather than nearly black, so
      // what shows through it has to go dark too: FUN_002342c0 sets the fog
      // colour to zero and drops both light colours.
      if (context.setItemSceneRenderState)
      {
        context.setItemSceneRenderState(true);
      }

      // The camera snapshot FUN_00233eb8 later restores is the third; the
      // FUN_00217d70 below saves what state 0x13 needs on its own.

      const float cameraAngle = FUN_00216690_wrapAngle(standAngle + fGpffff89f0_cameraYawOffset);
      context.camera->FUN_00217e18_release_manual_camera(false);
      context.camera->FUN_00217d70_set_manual_camera(
          {chest->positionX20 + std::cos(cameraAngle) * kCameraTrail,
           chest->positionZ24 + std::sin(cameraAngle) * kCameraTrail,
           chest->positionY28 + kCameraHeight},
          {chest->positionX20,
           chest->positionZ24,
           chest->positionY28 + fGpffff89f4_cameraLookHeight});

      chest->halfword08 &= 0xfffe;
      chest->halfword04 &= 0xbfff;
      player.facingRadians5c = chest->facingRadians5c;

      context.fade->FUN_0025d1c0_arm(false, ScreenFade::kCutsceneSpeed, 0x000000);
      player.state60 = 0x0E;
      *context.DAT_00354d2c_gameMode = kGameModeCutscene;
      player.halfword04 |= 0x0100;
    }

    // ---------------------------------------------------------------------
    // 0x0E -- FUN_00254f18
    // ---------------------------------------------------------------------
    void state0E_startAnimation(const ChestCutsceneContext &context)
    {
      if (context.fade->FUN_0025d2f8_step_fade_in(context.frameTicks))
      {
        FUN_00225bf0_set_state(*context.player, 0x0F, kAnimationOpenChest);
        context.player->flags06 |= kHoldOnLastKeyframe;
      }
      // FUN_00255ce8(0xff) reprograms the full-screen quad's vertex colour
      // every frame of the cutscene. The port's fade owns that surface.
    }

    // ---------------------------------------------------------------------
    // 0x0F -- FUN_00254f60
    // ---------------------------------------------------------------------
    void state0F_runAnimation(const ChestCutsceneContext &context)
    {
      OriginalEntity &player = *context.player;
      OriginalEntity *chest = chestFor(context);

      if ((player.flagsAa & kTimelineEventMarker) != 0 &&
          (player.flags06 & kAnimationSteppedThisFrame) != 0)
      {
        // The one line the whole cutscene exists for.
        if (chest != nullptr && context.FUN_002663a0_setEventFlag)
        {
          context.FUN_002663a0_setEventFlag(chest->eventFlagId198);
        }

        // FUN_00254f60's two branches. The original picks on the chest's id
        // byte; the port additionally falls back to the no-item path when the
        // item's own entity cannot be built, rather than cross-fading to
        // nothing.
        const bool built = chest != nullptr && chest->recordId130 >= 0 &&
                           context.buildItemEntity &&
                           context.buildItemEntity(static_cast<std::size_t>(player.interactTarget198),
                                                   chest->recordId130);
        if (built)
        {
          // DAT_003437B8[id]++ is the inventory count, and the original stages
          // the item-get stream here -- FUN_0025b9e8(0), then FUN_00237CA0 to
          // find its 0x14 and write the chest's id into the operand -- and
          // parks the *stream pointer* in player +0x19C. The port stages the
          // message by number instead and reads the id back off the chest at
          // 0x11, so +0x19C is only ever the "there is a stream" marker the
          // completion test below reads it as.
          player.itemEntity19c = 1;
          chest->fadeRamp62 = kItemCrossFadeRamp;
          chest->staggerTimer12a = kItemPoolSlot;
        }
        else
        {
          player.itemEntity19c = 0;
          if (chest != nullptr)
          {
            chest->staggerTimer12a = 0;
          }
        }
        return; // the original's goto past the completion test
      }

      if ((player.flags06 & kAnimationFinished) != 0)
      {
        if (player.itemEntity19c == 0)
        {
          // FUN_00237b38(FUN_0025b9e8(1)): "The chest is empty." The empty path
          // has a window too -- it just skips the reveal and the cross-fade,
          // and 0x12 waits on this one exactly as it does on the other.
          if (context.openItemWindow)
          {
            context.openItemWindow(kEmptyChestMessage, -1);
          }
          player.state60 = 0x12;
        }
        else
        {
          player.fadeRamp62 = 0;
          player.state60 = 0x10;
        }
      }
    }

    // ---------------------------------------------------------------------
    // 0x10 -- 0x002550F0. One frame: state 0x0F left player +0x62 at zero, so
    // the countdown is already spent and the item is revealed immediately.
    // ---------------------------------------------------------------------
    void state10_revealItem(const ChestCutsceneContext &context)
    {
      OriginalEntity &player = *context.player;
      if (static_cast<std::int16_t>(player.fadeRamp62) > 0)
      {
        player.fadeRamp62 = static_cast<std::uint16_t>(player.fadeRamp62 - context.frameTicks);
        return;
      }

      OriginalEntity *chest = chestFor(context);
      if (chest != nullptr)
      {
        context.pool->slot(chest->staggerTimer12a).halfword08 &= 0xfffe;
      }
      player.state60 = 0x11;
    }

    // ---------------------------------------------------------------------
    // 0x11 -- 0x00255148. The cross-fade: the chest and the player ramp out
    // while the item ramps in, then the window opens.
    // ---------------------------------------------------------------------
    void state11_crossFade(const ChestCutsceneContext &context)
    {
      OriginalEntity &player = *context.player;
      OriginalEntity *chest = chestFor(context);
      if (chest == nullptr)
      {
        player.state60 = 0x12;
        return;
      }
      OriginalEntity &item = context.pool->slot(chest->staggerTimer12a);

      // Both ramps are +0x62 >> 5 with the compiler's truncate-toward-zero
      // divide, stepping four ticks a frame.
      const auto level = [](std::int16_t ramp) {
        return static_cast<std::uint8_t>((ramp < 0 ? ramp + 31 : ramp) / 32);
      };
      const std::int32_t step = static_cast<std::int32_t>(context.frameTicks) * 4;

      const std::int16_t chestRamp = static_cast<std::int16_t>(chest->fadeRamp62);
      if (chestRamp >= 0x61)
      {
        player.fadeLevel134 = level(chestRamp);
        chest->fadeLevel134 = player.fadeLevel134;
        chest->fadeRamp62 = static_cast<std::uint16_t>(chestRamp - step);
      }
      else
      {
        chest->fadeLevel134 = 0;
        chest->halfword08 = static_cast<std::uint16_t>(chest->halfword08 | 1);
        player.fadeLevel134 = 0;
        player.halfword08 = static_cast<std::uint16_t>(player.halfword08 | 1);
      }

      if (item.fadeLevel134 != 0)
      {
        const std::int16_t itemRamp = static_cast<std::int16_t>(item.fadeRamp62);
        if (itemRamp < 0x0fe0)
        {
          item.fadeRamp62 = static_cast<std::uint16_t>(itemRamp + step);
          item.fadeLevel134 = level(static_cast<std::int16_t>(item.fadeRamp62));
        }
        else
        {
          // Zero is "not fading", so this is the item arriving fully opaque.
          item.fadeLevel134 = 0;
        }
      }
      else if ((chest->halfword08 & 1) != 0)
      {
        if (context.openItemWindow)
        {
          context.openItemWindow(kItemGetMessage, chest->recordId130);
        }
        player.state60 = 0x12;
        // FUN_00257b10(player) == FUN_00267d38(8, player). The only sound the
        // whole 0x0C..0x15 range plays; the chest's own opening cue comes from
        // FUN_002d1ea8.
        if (context.FUN_00267d38_playSound)
        {
          context.FUN_00267d38_playSound(kItemFanfareCue, player);
        }
      }
    }

    // ---------------------------------------------------------------------
    // 0x12 -- 0x00255260
    // ---------------------------------------------------------------------
    void state12_armWhiteFade(const ChestCutsceneContext &context)
    {
      // FUN_00237c60() reports the item window still being up.
      if (context.itemWindowOpen && context.itemWindowOpen())
      {
        return;
      }
      context.fade->FUN_0025d1c0_arm(true, ScreenFade::kCutsceneSpeed, 0xffffff);
      context.player->state60 = 0x13;
    }

    // ---------------------------------------------------------------------
    // 0x13 -- 0x002552b0
    // ---------------------------------------------------------------------
    void state13_restore(const ChestCutsceneContext &context)
    {
      if (!context.fade->FUN_0025d238_step_fade_out(context.frameTicks))
      {
        return;
      }

      OriginalEntity &player = *context.player;
      OriginalEntity *chest = chestFor(context);

      // FUN_00234400 here, undoing the three things state 0x0D did: it walks
      // slots 2..255 putting each one's +0x04 and +0x08 back from the
      // snapshot (with 0x10 set, so the first frame back snaps its pose rather
      // than blending), and FUN_00233eb8 restores the camera and the fade cap.
      for (std::size_t slot = 2; slot < orphen::ported::entity::kEntitySlotCount; ++slot)
      {
        OriginalEntity &other = context.pool->slot(slot);
        other.halfword04 = static_cast<std::uint16_t>(other.halfword04 & 0xbfff);
        other.halfword08 =
            static_cast<std::uint16_t>((other.halfword08 & 0xfffe) | 0x0010);
      }
      if (context.DAT_00355700_globalFadeCap != nullptr)
      {
        *context.DAT_00355700_globalFadeCap = 0;
      }
      if (context.setItemSceneRenderState)
      {
        context.setItemSceneRenderState(false);
      }
      context.camera->FUN_00217e18_release_manual_camera(true);
      // FUN_002241d8's `DAT_00355658 = 1.0`, which is what puts the projection
      // back after the chest's camera swing zoomed it to 3x.
      context.camera->FUN_002241d8_reset_zoom();

      if (chest != nullptr && chest->staggerTimer12a != 0)
      {
        // FUN_00265ec0 on the item entity.
        context.pool->releaseSlot(chest->staggerTimer12a);
        chest->staggerTimer12a = 0;
      }
      player.itemEntity19c = 0;
      player.fadeLevel134 = 0;

      if (chest != nullptr)
      {
        chest->fadeLevel134 = 0;
        // Clearing +0x94 makes FUN_002d1ea8 run its first-tick branch again,
        // which reads the event flag and settles on animation 6 -- open.
        chest->spawnParam94 = 0;
      }

      FUN_00225bf0_set_state(player, 0x14, 1);
      player.halfword08 = static_cast<std::uint16_t>((player.halfword08 & 0xfffe) | 0x0010);
      player.halfword04 &= 0xfeff;

      if (chest != nullptr)
      {
        // Push the player clear of the chest: one radius sum out along the
        // chest's back, plus a second chest radius when both axes are moving.
        const float angle = FUN_00216690_wrapAngle(chest->facingRadians5c + kExitHalfTurn);
        const float reach = chest->radius54 + player.radius54;
        float offsetX = reach * std::cos(angle);
        float offsetY = reach * std::sin(angle);
        if (static_cast<int>(offsetX * 1000.0f) != 0 && static_cast<int>(offsetY * 1000.0f) != 0)
        {
          offsetX += chest->radius54 * std::cos(angle);
          offsetY += chest->radius54 * std::sin(angle);
        }

        const float probeZ = player.positionY28 + kExitGroundProbeLift;
        player.positionX20 = chest->positionX20 + offsetX;
        player.positionZ24 = chest->positionZ24 + offsetY;

        float ground = player.groundHeight4c;
        if (context.FUN_00227798_groundHeight)
        {
          const auto sampled =
              context.FUN_00227798_groundHeight(player.positionX20, player.positionZ24, probeZ);
          if (sampled.has_value())
          {
            ground = *sampled;
          }
        }
        player.desiredDeltaX30 = kExitNudge;
        player.groundHeight4c = ground;
        player.positionY28 = ground + kExitGroundClearance;
      }

      // FUN_002241d8: back to the field frame loop. Its FUN_0023bae8 call is
      // the HUD's own restore and is not ported; its DAT_00355658 = 1.0 is the
      // FUN_002241d8_reset_zoom above.
      *context.DAT_00354d2c_gameMode = kGameModeField;
    }

    // ---------------------------------------------------------------------
    // 0x14 -- 0x00255448
    // ---------------------------------------------------------------------
    void state14_waitForGround(const ChestCutsceneContext &context)
    {
      context.fade->FUN_0025d238_step_fade_out(context.frameTicks);

      // +0x0C bit 0 is grounded: the player has fallen the clearance state
      // 0x13 gave them, so the screen can come back.
      if ((context.player->collisionFlags0c & 1) != 0)
      {
        context.fade->FUN_0025d1c0_arm(false, ScreenFade::kCutsceneSpeed, 0xffffff);
        context.player->state60 = 0x15;
      }
    }

    // ---------------------------------------------------------------------
    // 0x15 -- FUN_00255498
    // ---------------------------------------------------------------------
    void state15_returnToIdle(const ChestCutsceneContext &context)
    {
      if (context.fade->FUN_0025d2f8_step_fade_in(context.frameTicks))
      {
        // FUN_00252d88.
        FUN_00225bf0_set_state(*context.player, 0, 1);
        context.player->idleTimer1b6 = 0;
        context.player->interactParam1b8 = 0xa0;
      }
    }

  } // namespace

  void FUN_00251ed8_step_chest_cutscene(const ChestCutsceneContext &context)
  {
    switch (context.player->state60)
    {
    case 0x0C:
      state0C_armFadeOut(context);
      break;
    case 0x0D:
      state0D_placeAndFrame(context);
      break;
    case 0x0E:
      state0E_startAnimation(context);
      break;
    case 0x0F:
      state0F_runAnimation(context);
      break;
    case 0x10:
      state10_revealItem(context);
      break;
    case 0x11:
      state11_crossFade(context);
      break;
    case 0x12:
      state12_armWhiteFade(context);
      break;
    case 0x13:
      state13_restore(context);
      break;
    case 0x14:
      state14_waitForGround(context);
      break;
    case 0x15:
      state15_returnToIdle(context);
      break;
    default:
      break;
    }
  }

} // namespace orphen::ported::player
