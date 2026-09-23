// Game mode 3, the Equip screen. See ported/scene/equip_screen.h for what the
// original does and why the field is snapshotted rather than hidden.

#include "runtime/port_runtime.h"

#include "ported/model/psc3_skeleton.h"
#include "ported/scene/title_screen.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace orphen::port
{
  namespace
  {
    namespace scene = orphen::ported::scene;
    namespace entity = orphen::ported::entity;
    using orphen::ported::model::FUN_00216690_wrap_angle;
    using orphen::ported::psm2::Vec3;

    // The gp constants the states place things with. All but the camera's
    // four live in .sdata; the camera's are the words at 0x00354E00, which
    // nothing writes -- hardware reads the ELF's values there.
    constexpr float kPi = 3.141592025756836f;          // fGpffff859C, 85A8, 8608, 8610, DAT_0035251C
    constexpr float kFGpffff8598_ringReach = 0.4f;
    constexpr float kFGpffff85a0_ringHeight = 0.9f;
    constexpr float kUGpffff85a4_ringTilt154 = 0.26179933547973633f;
    constexpr float kUGpffff860c_ringTilt158 = 0.26179933547973633f;
    constexpr float kFGpffff8614_lookSwing = 0.34906578063964844f;
    constexpr float kFGpffffae90_eyeReach = 3.5f;
    constexpr float kFGpffffae94_lookReach = 0.8f;
    constexpr float kFGpffffae98_eyeHeight = 0.3f;
    constexpr float kFGpffffae9c_lookHeight = 0.0f;
    // FUN_0022F620's DAT_00352520/24/28: the two swap curves bow out a
    // quarter turn either side, and the outgoing icon's crests 0.4 of the way up.
    constexpr float kDAT_00352520_quarterTurn = 1.570796012878418f;
    constexpr float kDAT_00352524_quarterTurn = 1.570796012878418f;
    constexpr float kDAT_00352528_swapRise = 0.4f;
    // fGpffff85C0 (FUN_0022FA18) and fGpffff865C (FUN_002378E0): the icons face
    // back at the camera.
    constexpr float kFGpffff85c0_iconTurn = 3.141592025756836f;
    constexpr float kFGpffff865c_iconTurn = 3.141592025756836f;
    // uGpffff85D0, the lead's +0x30 on the way out.
    constexpr float kUGpffff85d0_leadDrift = 0.001f;

    // FUN_00233B28:98-106, the fog the screen runs under.
    constexpr float kEquipFogNear = 8.0f;
    constexpr float kEquipFogFar = 32.0f;
    constexpr std::uint32_t kEquipFogColour2 = 0x505050;

    // FUN_002298D0.
    int FUN_002298d0_roster(std::int16_t type)
    {
      switch (type)
      {
      case 1: return 0;
      case 3: return 1;
      case 4: return 2;
      case 5: return 3;
      case 6: return 4;
      case 7: return 5;
      case 0x16: return 6;
      default: return 7;
      }
    }

    // The entity +0x02 bit FUN_0022F020:57 clears before each release: the
    // `& 0x8000` script hook FUN_00265EC0 would otherwise run.
    constexpr std::uint16_t kScriptHook02 = 0x8000;
    // +0x06 bit 0x10, cleared on the lead in a battle scene.
    constexpr std::uint16_t kLeadBattleFlag06 = 0x0010;
  } // namespace

  std::uint16_t PortRuntime::FUN_00231a98_availability() const
  {
    // FUN_00231A98:20-29. Every PTR_FUN_0031C3C0 entry is non-null, so the only
    // bit that can drop is row 6's, for the two leads whose roster index is 1
    // or 2 -- they have no spells to equip.
    std::uint16_t mask = 0xFFFF;
    const int roster = FUN_002298d0_roster(entityPool_.leadPlayer().typeId00);
    if (static_cast<unsigned>(roster - 1) < 2u)
    {
      mask = static_cast<std::uint16_t>(mask & 0xFFBF);
    }
    return mask;
  }

  // FUN_0022E910.
  void PortRuntime::FUN_0022e910_equip_screen(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    // :10-28. Triangle is read before the dispatch, so a state that leaves on
    // it runs its handler in the same frame.
    if ((input.rawPressedPad & scene::kEquipPadTriangle) != 0)
    {
      if (DAT_00354da0_equipState_ == scene::kEquipStateEnter)
      {
        // :11-14. Not yet built: FUN_002241D8 and back.
        DAT_00354d2c_gameMode_ = orphen::ported::player::kGameModeField;
        DAT_00342a70_mappedActions_.reset();
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueCancel);
        equipDrawn_ = false;
        std::cout << "[equip] backed out before the screen was built\n";
        return;
      }
      if (DAT_00354da0_equipState_ == scene::kEquipStateRingOpen)
      {
        // :15-20. The ring closes: its icons shrink and it fades back down.
        DAT_00354da0_equipState_ = scene::kEquipStateRingClose;
        auto &ring = entityPool_.slot(scene::kEquipRingSlot);
        ring.state60 = scene::kRingFadeOut;
        equipRing1c4_ = scene::kRingStepShrink;
        ring.fadeRamp62 = scene::kRingFadeFull;
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueCancel);
      }
      else if (DAT_00354da0_equipState_ > scene::kEquipStateFadeIn &&
               DAT_00354da0_equipState_ != scene::kEquipStateLeaveByReload)
      {
        // :22-31. A battle scene cannot have its field put back, so it is
        // reloaded behind a fade instead.
        if (!DAT_003555d3_groupEScene_)
        {
          DAT_00354da0_equipState_ = scene::kEquipStateLeave;
        }
        else
        {
          DAT_00571dc0_screenFade_.FUN_0025d1c0_arm(true, scene::kEquipReloadFadeTicks, 0);
          DAT_00354da0_equipState_ = scene::kEquipStateLeaveByReload;
        }
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueCancel);
      }
    }

    // :33, `PTR_LAB_0031C2F0[uGpffffae30]()`.
    bool pickSlotRan = false;
    equipNoSpellsDrawn_ = false;
    equipItemLine_.clear();
    switch (DAT_00354da0_equipState_)
    {
    case scene::kEquipStateArm:
      // 0x0022F010.
      DAT_00354da0_equipState_ = scene::kEquipStateEnter;
      break;
    case scene::kEquipStateEnter:
      FUN_0022f020_enter_equip_screen();
      break;
    case scene::kEquipStateFadeIn:
      FUN_0022f2d8_equip_fade_in(frameTicks);
      break;
    case scene::kEquipStateFirstSlot:
    {
      // 0x0022F3E8: slot 0's icon plays animation 1, the highlight.
      const int icon = DAT_00570da0_slotIcon_[0];
      if (icon >= 0)
      {
        entityPool_.slot(static_cast<std::size_t>(icon)).animationA0 = 1;
      }
      DAT_00354da0_equipState_ = scene::kEquipStatePickSlot;
      break;
    }
    case scene::kEquipStatePickSlot:
      FUN_0022f408_pick_slot(input, frameTicks);
      pickSlotRan = true;
      break;
    case scene::kEquipStateRingClose:
      FUN_0022f588_close_ring(input, frameTicks);
      break;
    case scene::kEquipStateRingOpen:
      FUN_0022f620_ring_open(input, frameTicks);
      break;
    case scene::kEquipStateSwap:
      FUN_0022fa18_swap(frameTicks);
      break;
    case scene::kEquipStateNoSpells:
      // FUN_0022FBD0. Its draw is in buildEquipScreenSprites.
      equipNoSpellsDrawn_ = true;
      equipNoItemsCaption_ = scene::kEquipNoSpellsCaptionMessage;
      if ((input.rawPressedPad & scene::kEquipPadCross) != 0)
      {
        DAT_00354da0_equipState_ = scene::kEquipStatePickSlot;
      }
      break;
    case scene::kEquipStateItemList:
      FUN_0022fca8_build_item_ring();
      break;
    case scene::kEquipStateItemBrowse:
      FUN_0022fd38_browse_items(input, frameTicks);
      break;
    case scene::kEquipStateNoItems:
      // FUN_0022FDE8: the same box as state 8, but nothing but Triangle
      // (handled above) leaves it.
      equipNoSpellsDrawn_ = true;
      equipNoItemsCaption_ = scene::kItemNoItemsCaptionMessage;
      break;
    case scene::kEquipStateLeaveByReload:
      FUN_0022fea8_leave_by_reload(frameTicks);
      break;
    case scene::kEquipStateLeave:
      FUN_0022ff20_leave_equip_screen();
      break;
    default:
      break;
    }

    // :34-52, the draw. Nothing below runs until the screen is built.
    equipDrawn_ = DAT_00354da0_equipState_ > scene::kEquipStateEnter;
    equipDescriptionTwice_ = pickSlotRan;
    if (!equipDrawn_)
    {
      return;
    }
    // FUN_002311E8's projection, taken now: it reads the camera and the lead,
    // and nothing after this in the frame moves either.
    const auto &lead = entityPool_.leadPlayer();
    const Vec3 leadPosition{lead.positionX20, lead.positionZ24, lead.positionY28};
    for (int row = 0; row < scene::kEquipSlotCount; ++row)
    {
      equipRowX_[static_cast<std::size_t>(row)] = scene::FUN_002311e8_row_x(
          scene::FUN_00230128_slot_position(row, leadPosition, lead.facingRadians5c),
          fieldCamera_.DAT_0058c0a8_eye(), fieldCamera_.yawRadians(), fieldCamera_.pitchRadians());
    }
    // FUN_00255CE8(0xFF): the black quad under the map, every frame.
    DAT_00255ce8_underlayAlpha_ = 0xFF;
    // :42-44, FUN_002333E8 on the ring while it has a type.
    if (entityPool_.status(scene::kEquipRingSlot) != entity::SlotStatus::Free &&
        entityPool_.slot(scene::kEquipRingSlot).typeId00 > 0)
    {
      FUN_002333e8_ring_fade(frameTicks);
    }
  }

  // FUN_0022F020, state 1.
  void PortRuntime::FUN_0022f020_enter_equip_screen()
  {
    auto &lead = entityPool_.leadPlayer();
    // :19. Nothing happens until the lead is standing on something -- a jump
    // pressed into the menu finishes first. A battle scene does not wait.
    if (!DAT_003555d3_groupEScene_ && (lead.collisionFlags0c & 1u) == 0)
    {
      return;
    }
    // :20 and :90-92, `(uGpffffb668 & 0x40) == 0` -- a debug bit clear on
    // retail, so the other arm (straight back to the field) is not taken.

    // :21-23, FUN_00267E78(0x570DE8, 0x40C) and the name.
    DAT_00570dec_spellName_.clear();
    DAT_00570df4_description_ = {};
    DAT_00570de8_characterName_ = FUN_0025b9e8_text(
        static_cast<std::size_t>(FUN_002298d0_roster(lead.typeId00) + scene::kEquipCharacterNameMessage));

    if (DAT_003555d3_groupEScene_)
    {
      // :24-31. Every bone override off.
      lead.flags06 = static_cast<std::uint16_t>(lead.flags06 & ~kLeadBattleFlag06);
      for (std::size_t bone = 0; bone < 0x2A; ++bone)
      {
        orphen::ported::model::FUN_0020d9c8_clear_bone_override(DAT_004a7e00_boneOverrides_[0], bone);
      }
    }

    // :33, FUN_00252D88: standing, idle animation, the fidget timer cleared.
    // Its other two writes -- +0x1B8 raised to 1 if zero, and a negative
    // camera sub-mode clamped to 0 -- have no port state behind them: the
    // port does not model +0x1B8, and its sub-mode is unsigned.
    entity::FUN_00225bf0_set_state_and_animation(lead, 0, 1);
    lead.idleTimer1b6 = 0;
    DAT_00354da0_equipState_ = scene::kEquipStateFadeIn;
    lead.halfword08 = static_cast<std::uint16_t>((lead.halfword08 & ~0x40u) | 0x10u);

    // :36-38. The companions' slots and whatever was in slot 6, released
    // *before* the snapshot -- so they do not come back.
    auto &lights = sceneScript_.state().DAT_00343888_lights;
    entity::FUN_00265ec0_destroy_entity(scene::kEquipReleasedSlotA, entityPool_, &lights);
    entity::FUN_00265ec0_destroy_entity(scene::kEquipReleasedSlotB, entityPool_, &lights);
    entity::FUN_00265ec0_destroy_entity(scene::kEquipRingSlot, entityPool_, &lights);
    FUN_002338f0_spawn_ring();

    // :40-47. The ring stands 0.4 behind the lead, as the old camera saw it,
    // and 0.9 up.
    const float away = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
    auto &ring = entityPool_.slot(scene::kEquipRingSlot);
    ring.positionX20 = lead.positionX20 + std::cos(away) * kFGpffff8598_ringReach;
    ring.positionZ24 = lead.positionZ24 + std::sin(away) * kFGpffff8598_ringReach;
    ring.positionY28 = lead.positionY28 + kFGpffff85a0_ringHeight;
    ring.groundHeight4c = ring.positionY28;
    ring.state60 = 0;
    ring.rotationX154 = kUGpffff85a4_ringTilt154;

    FUN_00233b28_save_field();
    FUN_00233a10_place_camera();

    // :51-52, uGpffffb6f1 (the smear) and uGpffffb654.
    DAT_00343878_frameFeedback_.FUN_00264448_set_alpha(0);
    // :53-55.
    if (lead.parentSlot192 >= 0)
    {
      lead.parentSlot192 = -1;
    }
    // :56-66. Everything from slot 10 up goes, hook bit cleared first.
    std::size_t released = 0;
    for (std::size_t slot = entity::kFirstScriptSlot; slot < entityPool_.slotCount(); ++slot)
    {
      if (entityPool_.status(slot) == entity::SlotStatus::Free)
      {
        continue;
      }
      auto &doomed = entityPool_.slot(slot);
      doomed.descriptorFlags02 = static_cast<std::uint16_t>(doomed.descriptorFlags02 & ~kScriptHook02);
      entity::FUN_00265ec0_destroy_entity(slot, entityPool_, &lights);
      ++released;
    }

    // :67. The lead turns to face the camera FUN_00233A10 just placed.
    lead.facingRadians5c = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
    // :68-70. Zoom 1.0, and the pitch and roll FUN_00217D70 worked out are
    // thrown away: the view is level.
    fieldCamera_.setZoomLog2(1.0f);
    fieldCamera_.setPitch(0.0f);
    fieldCamera_.setRoll(0.0f);
    // :71-81 zero nine effect gates, uGpffffb6ec and uGpffffacec. Mode 3 steps
    // no effect pool and FUN_00233EB8 puts every one back, so nothing can see
    // them at zero. FUN_00203298 clears 32 records at 0x00356180 the port has
    // no counterpart for.

    // :82-84.
    if (fieldMenu_.uGpffffae34_selected() == scene::kFieldMenuEquipItem)
    {
      FUN_002302f0_spawn_slot_icons();
    }
    // :85-89.
    DAT_00354da0_equipState_ = scene::kEquipStateFadeIn;
    equipRing1c8_ = -1;
    equipRing1c0_ = 0xFE0;
    equipRing1c4_ = 0;
    lead.halfword08 = static_cast<std::uint16_t>(lead.halfword08 & ~1u);
    lead.halfword04 = static_cast<std::uint16_t>(lead.halfword04 | 0x100u);

    std::cout << "[equip] built at frame " << frameCount_ << ": " << released
              << " slot(s) from 10 up released, icons in slots " << DAT_00570da0_slotIcon_[0] << ' '
              << DAT_00570da0_slotIcon_[1] << ' ' << DAT_00570da0_slotIcon_[2] << '\n';
  }

  // FUN_002338F0.
  void PortRuntime::FUN_002338f0_spawn_ring()
  {
    entityPool_.FUN_00229c40_initialize(scene::kEquipRingSlot, scene::kEquipRingType, descriptorTable_);
    entityPool_.setStatus(scene::kEquipRingSlot, entity::SlotStatus::ScriptSpawned);
    auto &ring = entityPool_.slot(scene::kEquipRingSlot);
    // :6-7 put it at the camera; FUN_0022F020 moves it before anything reads
    // that, so the first placement is left out.
    entity::FUN_00229ef0_set_scale(ring, scene::kEquipRingScale, &descriptorTable_);
    ring.halfword08 = 0x90;
    ring.halfword04 = static_cast<std::uint16_t>(ring.halfword04 | 0x100u);
    ring.rotationY158 = kUGpffff860c_ringTilt158;
    ring.facingRadians5c = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
    ring.fadeRamp62 = 0x60;
    ring.state60 = 10;
    // +0x1B8, FUN_00268010(0x54A8): the ring's work block, which states 5..11
    // fill.
  }

  // FUN_00233B28.
  void PortRuntime::FUN_00233b28_save_field()
  {
    // :13-50, the types FUN_00233B28 releases before it copies anything:
    // 0x44, 0x4F, 0x52 and 0x56, hook bit cleared.
    auto &lights = sceneScript_.state().DAT_00343888_lights;
    for (std::size_t slot = entity::kFirstScriptSlot; slot < entityPool_.slotCount(); ++slot)
    {
      auto &candidate = entityPool_.slot(slot);
      const std::int16_t type = candidate.typeId00;
      if (type == 0x44 || type == 0x4F || type == 0x52 || type == 0x56)
      {
        candidate.descriptorFlags02 = static_cast<std::uint16_t>(candidate.descriptorFlags02 & ~kScriptHook02);
        entity::FUN_00265ec0_destroy_entity(slot, entityPool_, &lights);
      }
    }

    auto &state = sceneScript_.state();
    equipSaved_.DAT_0058beb0_pool.resize(entityPool_.slotCount());
    equipSaved_.mode168.resize(entityPool_.slotCount());
    for (std::size_t slot = 0; slot < entityPool_.slotCount(); ++slot)
    {
      equipSaved_.DAT_0058beb0_pool[slot] = entityPool_.slot(slot);
      equipSaved_.DAT_005a96b0_slotStatus[slot] = entityPool_.status(slot);
      equipSaved_.mode168[slot] = DAT_004a7e00_boneOverrides_[slot].mode168;
    }
    equipSaved_.camera = fieldCamera_;
    equipSaved_.uGpffffb6fc_globalRgb = state.uGpffffb6fc_globalRgb;
    equipSaved_.uGpffffb700_vectorRgb = state.uGpffffb700_vectorRgb;
    for (int axis = 0; axis < 3; ++axis)
    {
      equipSaved_.DAT_003439c8_lightDirection[axis] = state.DAT_003439c8_vector[axis];
    }
    equipSaved_.uGpffffb704_color1 = state.uGpffffb704_color1;
    equipSaved_.uGpffffb708_color2 = state.uGpffffb708_color2;
    equipSaved_.fGpffffb70c_fadeNear = state.fGpffffb70c_fadeNear;
    equipSaved_.fGpffffb710_fadeFar = state.fGpffffb710_fadeFar;
    equipSaved_.DAT_00343888_lights = state.DAT_00343888_lights;
    equipSaved_.DAT_0058bf0c_leadFacing = entityPool_.leadPlayer().facingRadians5c;
    // :89-91.
    equipSaved_.fade = scene::EquipFade{};

    // :80-86, after the copy: everything live from slot 10 up, except the
    // ring, is hidden. FUN_0022F020 releases all of it on the next line, so
    // only the snapshot ever sees the difference -- and it was taken first.
    // :92-105. The fog closes in, every light goes out and the smear is armed
    // at 0x7F -- FUN_0022F020 zeroes it again before the frame draws.
    state.fGpffffb70c_fadeNear = kEquipFogNear;
    state.fGpffffb710_fadeFar = kEquipFogFar;
    state.uGpffffb708_color2 = kEquipFogColour2;
    DAT_00343878_frameFeedback_.FUN_00264448_set_alpha(0x7F);
    for (std::uint32_t light = 0; light < orphen::ported::render::LightTable::kSlotCount; ++light)
    {
      state.DAT_00343888_lights.slot(light).radius = 0.0f;
    }
    // :106-111 clear DAT_00570BA0..0x00570D9C, the battle target display's
    // table, which the port keeps inside BattleParty and only a battle fills.
    entityPool_.slot(scene::kEquipRingSlot).state60 = 0;

    // :112, FUN_002579F0: the impact-dust pool reset, type 0x44 released and
    // type 0x37 parked.
    DAT_00355620_particles_.FUN_002d3290_reset([this] { return FUN_00216868_random(); });
    for (std::size_t slot = entity::kFirstScriptSlot; slot < entityPool_.slotCount(); ++slot)
    {
      if (entityPool_.status(slot) == entity::SlotStatus::Free)
      {
        continue;
      }
      auto &candidate = entityPool_.slot(slot);
      if (candidate.typeId00 == 0x44)
      {
        entity::FUN_00265ec0_destroy_entity(slot, entityPool_, &lights);
      }
      else if (candidate.typeId00 == 0x37)
      {
        candidate.animationA0 = 0;
        candidate.state60 = 1;
        candidate.fadeRamp62 = 0;
      }
    }
    // :113, DAT_0058BF0C = the ring's facing.
    entityPool_.leadPlayer().facingRadians5c = entityPool_.slot(scene::kEquipRingSlot).facingRadians5c;
  }

  // FUN_00233A10: the camera, 3.5 out in front of the lead and looking back at
  // a point just beside him.
  void PortRuntime::FUN_00233a10_place_camera()
  {
    const auto &lead = entityPool_.leadPlayer();
    const float out = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
    fieldCamera_.FUN_00217e18_release_manual_camera(false);
    const float look = FUN_00216690_wrap_angle(out + kFGpffff8614_lookSwing);
    const Vec3 eye{lead.positionX20 + kFGpffffae90_eyeReach * std::cos(out),
                   lead.positionZ24 + kFGpffffae90_eyeReach * std::sin(out),
                   lead.positionY28 + kFGpffffae98_eyeHeight};
    const Vec3 lookAt{lead.positionX20 + kFGpffffae94_lookReach * std::cos(look),
                      lead.positionZ24 + kFGpffffae94_lookReach * std::sin(look),
                      lead.positionY28 + kFGpffffae9c_lookHeight};
    fieldCamera_.FUN_00217d70_set_manual_camera(eye, lookAt);
  }

  // FUN_002302F0: an icon per loadout slot, at rest in its row.
  void PortRuntime::FUN_002302f0_spawn_slot_icons()
  {
    const auto &lead = entityPool_.leadPlayer();
    const int roster = FUN_002298d0_roster(lead.typeId00);
    const auto loadout = battleParty_.DAT_003437a0_loadout();
    const Vec3 leadPosition{lead.positionX20, lead.positionZ24, lead.positionY28};
    for (int slot = 0; slot < scene::kEquipSlotCount; ++slot)
    {
      const std::size_t s = static_cast<std::size_t>(slot);
      DAT_00570da0_slotIcon_[s] = -1;
      DAT_00570dd8_slotName_[s].clear();
      const std::size_t at = static_cast<std::size_t>(roster * 3 + slot);
      if (at >= loadout.size())
      {
        continue;
      }
      const std::uint8_t item = loadout[at];
      const std::size_t index = entityPool_.FUN_00265e28_allocate_and_initialize(
          item + scene::kEquipIconTypeBase, descriptorTable_);
      if (index >= entityPool_.slotCount())
      {
        continue;
      }
      entityPool_.setStatus(index, entity::SlotStatus::ScriptSpawned);
      auto &icon = entityPool_.slot(index);
      const Vec3 rest = scene::FUN_00230128_slot_position(slot, leadPosition, lead.facingRadians5c);
      icon.positionX20 = rest.x;
      icon.positionZ24 = rest.y;
      icon.positionY28 = rest.z;
      icon.groundHeight4c = icon.positionY28;
      icon.facingRadians5c = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
      icon.halfword08 = static_cast<std::uint16_t>(icon.halfword08 | 0x4040u);
      icon.halfword04 = static_cast<std::uint16_t>(icon.halfword04 | 0x100u);
      DAT_00570da0_slotIcon_[s] = static_cast<int>(index);
      // FUN_00229820(item, 0, +0x19C, +0x1A0).
      iconName19c_[s] = itemDatabase_.FUN_00229688_name(item);
      iconDescription1a0_[s] = itemDatabase_.FUN_00229688_description(item);
      DAT_00570dd8_slotName_[s] = iconName19c_[s];
    }
    // :36-39, for the slot the ring's +0x1C0 names -- 0, the ring being fresh.
    selectEquipSlot(equipRing1c0_);
  }

  void PortRuntime::selectEquipSlot(int slot)
  {
    if (slot < 0 || slot >= scene::kEquipSlotCount)
    {
      return;
    }
    const std::size_t s = static_cast<std::size_t>(slot);
    DAT_00570df4_description_ = scene::FUN_00230ce0_split(iconDescription1a0_[s]);
    DAT_00570dec_spellName_ = iconName19c_[s];
    // FUN_00229820(type - 0x1F1, 0x570DB0, 0, 0): the stat record the
    // pentagon draws.
    const int icon = DAT_00570da0_slotIcon_[s];
    if (icon >= 0)
    {
      FUN_00229820_equip_record(entityPool_.slot(static_cast<std::size_t>(icon)).typeId00);
    }
  }

  void PortRuntime::FUN_00229820_equip_record(std::int16_t iconType)
  {
    // FUN_00229688 copies nothing when the id has no row; the port's lookup
    // reports that as empty, and the buffer keeps what it had.
    if (const auto record = itemDatabase_.FUN_00229688_record(iconType - scene::kEquipIconTypeBase))
    {
      DAT_00570db0_record_ = *record;
    }
  }

  std::vector<orphen::ported::render::HudQuad> PortRuntime::buildEquipPentagonQuads() const
  {
    std::vector<orphen::ported::render::HudQuad> quads;
    if (equipDrawn_ && scene::FUN_00230e50_spell_shown(DAT_00354da0_equipState_, DAT_00570dec_spellName_))
    {
      orphen::ported::battle::FUN_0022ec30_pentagon(scene::kEquipPentagonX, scene::kEquipPentagonY,
                                                    DAT_00570db0_record_.elementTable, false, quads);
    }
    return quads;
  }

  // FUN_0022F2D8, state 2.
  void PortRuntime::FUN_0022f2d8_equip_fade_in(std::uint32_t frameTicks)
  {
    // :11-12. Twice a frame, so the whole fade runs at double the rate.
    FUN_002340e0_equip_fade(frameTicks);
    FUN_002340e0_equip_fade(frameTicks);
    if (equipSaved_.fade.done1da8c != 0)
    {
      if (fieldMenu_.uGpffffae34_selected() == scene::kFieldMenuEquipItem)
      {
        // :15-17. Icon 0's description; the name was already picked.
        DAT_00570df4_description_ = scene::FUN_00230ce0_split(iconDescription1a0_[0]);
        DAT_00354da0_equipState_ = scene::kEquipStateFirstSlot;
      }
      else if (fieldMenu_.uGpffffae34_selected() == scene::kFieldMenuItemItem)
      {
        DAT_00354da0_equipState_ = scene::kEquipStateItemList;
      }
      else
      {
        DAT_00354da0_equipState_ = scene::kEquipStateLeave;
      }
      // :24-35. The screen's own light: straight down, white, over a dim
      // ambient.
      auto &state = sceneScript_.state();
      state.DAT_003439c8_vector[0] = 0.0f;
      state.DAT_003439c8_vector[1] = 0.0f;
      state.DAT_003439c8_vector[2] = -1.0f;
      equipRing1c4_ = 1;
      equipRing1c6_ = 3;
      equipRing1c0_ = 0;
      state.uGpffffb6fc_globalRgb = scene::kEquipAmbientRgb;
      state.uGpffffb700_vectorRgb = scene::kEquipLightRgb;
      std::cout << "[equip] faded in at frame " << frameCount_ << '\n';
    }
    // :37-47. Every frame, the three icons turn to the camera.
    const float facing = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kPi);
    for (const int icon : DAT_00570da0_slotIcon_)
    {
      if (icon >= 0)
      {
        entityPool_.slot(static_cast<std::size_t>(icon)).facingRadians5c = facing;
      }
    }
  }

  // FUN_002340E0, with its writes landed.
  void PortRuntime::FUN_002340e0_equip_fade(std::uint32_t frameTicks)
  {
    const bool titleSection = !DAT_003555d3_groupEScene_ &&
                              DAT_003551f4_sceneSection_ == orphen::ported::scene::kTitleSceneSection;
    const bool entry2a = DAT_003551f0_sceneEntry_ == 0x2A;
    const scene::EquipFadeStep step =
        scene::FUN_002340e0_step(equipSaved_.fade, titleSection, entry2a, frameTicks);
    if (step.fadeCapWritten)
    {
      DAT_00355700_globalFadeCap_ = step.fadeCap;
    }
    if (step.smearWritten)
    {
      DAT_00343878_frameFeedback_.FUN_00264448_set_alpha(step.smear);
    }
    if (step.colourMixLevel >= 0)
    {
      // FUN_0022EF30: from the colours the snapshot kept, toward the screen's.
      auto &state = sceneScript_.state();
      state.uGpffffb6fc_globalRgb =
          scene::FUN_0022ef30_mix(equipSaved_.uGpffffb6fc_globalRgb, step.colourMixLevel, 0x20);
      state.uGpffffb700_vectorRgb =
          scene::FUN_0022ef30_mix(equipSaved_.uGpffffb700_vectorRgb, step.colourMixLevel, 0xFF);
    }
    DAT_00255ce8_underlayAlpha_ = step.underlayAlpha;
    // DAT_003555C5 = 0xFF: the frame-pacing byte FUN_002000C0 reads. The port
    // paces frames its own way.
  }

  // FUN_0022F408, state 4.
  void PortRuntime::FUN_0022f408_pick_slot(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    orphen::ported::scene::FieldMenuPad pad;
    pad.uGpffffb684_held = input.rawHeldPad;
    pad.uGpffffb686_pressed = input.rawPressedPad;
    pad.uGpffffb68e_stickDirection = input.rawStickDirection;
    // :11, FUN_0023B9F8(0x5000, 1) -- the same helper, and the same globals,
    // the field menu steps with.
    if (fieldMenu_.FUN_0023b9f8_autoRepeat(0x5000, pad, frameTicks))
    {
      soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueMove);
      const int before = DAT_00570da0_slotIcon_[static_cast<std::size_t>(equipRing1c0_)];
      if (before >= 0)
      {
        entityPool_.slot(static_cast<std::size_t>(before)).animationA0 = 0;
      }
      // :15-19. The direction is read off the *pressed* word, so a held
      // repeat -- which has no fresh press -- always steps down.
      const int step = (input.rawPressedPad & 0x1000) != 0 ? -1 : 1;
      equipRing1c0_ = static_cast<std::int16_t>(equipRing1c0_ + step);
      if (equipRing1c0_ < 0)
      {
        equipRing1c0_ = 2;
      }
      else if (equipRing1c0_ > 2)
      {
        equipRing1c0_ = 0;
      }
      const int after = DAT_00570da0_slotIcon_[static_cast<std::size_t>(equipRing1c0_)];
      if (after >= 0)
      {
        entityPool_.slot(static_cast<std::size_t>(after)).animationA0 = 1;
      }
      selectEquipSlot(equipRing1c0_);
    }
    if ((input.rawPressedPad & scene::kEquipPadCross) != 0)
    {
      // :36-49. The ring for this slot, or the "no spells" box.
      if (FUN_00230910_build_ring(equipRing1c0_, -1) == 0)
      {
        DAT_00570df0_message_ = FUN_0025b9e8_text(scene::kEquipNoSpellsMessage);
        DAT_00354da0_equipState_ = scene::kEquipStateNoSpells;
      }
      else
      {
        auto &ring = entityPool_.slot(scene::kEquipRingSlot);
        ring.fadeRamp62 = scene::kRingFadeOpenFrom;
        DAT_00354da0_equipState_ = scene::kEquipStateRingOpen;
        ring.state60 = scene::kRingFadeIn;
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueRingOpen);
      }
    }
    // :50, FUN_00230DB0 -- drawn from the draw list, see equipDescriptionTwice_.
  }

  // FUN_0022FEA8, state 12: a battle scene's way out.
  void PortRuntime::FUN_0022fea8_leave_by_reload(std::uint32_t frameTicks)
  {
    if (!DAT_00571dc0_screenFade_.FUN_0025d238_step_fade_out(frameTicks))
    {
      return;
    }
    // :12-13, FUN_00267E78(0x342BD4, 0x1C) and flag 800.
    auto &state = sceneScript_.state();
    std::fill_n(state.DAT_00342b70_flags + scene::kEquipReloadFlagBytesFirst,
                scene::kEquipReloadFlagByteCount, std::uint8_t{0});
    state.FUN_002663a0_setEventFlag(scene::kEquipReloadFlag);
    // :14-16. Not FUN_002241D8: the action ring is left alone.
    DAT_00354d2c_gameMode_ = orphen::ported::player::kGameModeField;
    DAT_00354da0_equipState_ = scene::kEquipStateArm;
    DAT_003551ec_sceneRequest_ = scene::kEquipReloadRequest;
    std::cout << "[equip] left by reloading the scene at frame " << frameCount_ << '\n';
  }

  // FUN_0022FF20, state 13.
  void PortRuntime::FUN_0022ff20_leave_equip_screen()
  {
    // :13. The item an Item-screen exit is about to use; -1 on this path.
    const std::int16_t usedItemType = equipRing1c8_;

    // :15-16. Slots 2..255 and their status bytes, wholesale.
    for (std::size_t slot = 2; slot < entityPool_.slotCount(); ++slot)
    {
      entityPool_.slot(slot) = equipSaved_.DAT_0058beb0_pool[slot];
      entityPool_.setStatus(slot, equipSaved_.DAT_005a96b0_slotStatus[slot]);
      DAT_004a7e00_boneOverrides_[slot].mode168 = equipSaved_.mode168[slot];
    }
    FUN_00233eb8_restore_field();
    // :18-23 clear DAT_00570BA0 again; see FUN_00233B28.
    DAT_00570da0_slotIcon_ = {-1, -1, -1};
    DAT_00354da0_equipState_ = scene::kEquipStateArm;
    // FUN_002241D8.
    DAT_00354d2c_gameMode_ = orphen::ported::player::kGameModeField;
    DAT_00342a70_mappedActions_.reset();
    if (usedItemType > 0x1F0)
    {
      // :28-50, using the item: it is spawned over the lead's head, the lead
      // plays animation 0x45 in state 10, cue FUN_00237AA8, and its count
      // goes down one. None of that is ported yet -- the count included.
      const int item = usedItemType - scene::kEquipIconTypeBase;
      std::cout << "[item] use item 0x" << std::hex << item << std::dec << " \""
                << itemDatabase_.FUN_00229688_name(item)
                << "\" at frame " << frameCount_ << " -- its effect (FUN_0022FF20:28-50) is not ported\n";
    }

    auto &lead = entityPool_.leadPlayer();
    // :51-55. DAT_0058C0EA is slot 1's +0x62.
    entityPool_.slot(entity::kCameraSlot).fadeRamp62 = 0;
    if (fieldCamera_.freeLookActive())
    {
      lead.halfword08 = static_cast<std::uint16_t>(lead.halfword08 | 1u);
    }
    lead.desiredDeltaX30 = kUGpffff85d0_leadDrift;
    lead.halfword04 = static_cast<std::uint16_t>(lead.halfword04 & ~0x100u);
    equipDrawn_ = false;
    std::cout << "[equip] closed at frame " << frameCount_ << '\n';
  }

  // FUN_00233EB8.
  void PortRuntime::FUN_00233eb8_restore_field()
  {
    auto &state = sceneScript_.state();
    // :13-19. The camera, whole: FUN_00217E18(0) drops the screen's manual
    // camera and the saved eye, angles, look-at and sub-mode go back.
    fieldCamera_ = equipSaved_.camera;
    fieldCamera_.FUN_00217e18_release_manual_camera(false);
    state.uGpffffb6fc_globalRgb = equipSaved_.uGpffffb6fc_globalRgb;
    state.uGpffffb700_vectorRgb = equipSaved_.uGpffffb700_vectorRgb;
    for (int axis = 0; axis < 3; ++axis)
    {
      state.DAT_003439c8_vector[axis] = equipSaved_.DAT_003439c8_lightDirection[axis];
    }
    state.uGpffffb704_color1 = equipSaved_.uGpffffb704_color1;
    // :24, uGpffffb790 = DAT_00355700 from +0x1DA65, which FUN_00233B28 never
    // writes. Hardware reads 0 after the screen closes.
    DAT_00355700_globalFadeCap_ = 0;
    entityPool_.leadPlayer().facingRadians5c = equipSaved_.DAT_0058bf0c_leadFacing;
    state.fGpffffb70c_fadeNear = equipSaved_.fGpffffb70c_fadeNear;
    state.uGpffffb708_color2 = equipSaved_.uGpffffb708_color2;
    state.fGpffffb710_fadeFar = equipSaved_.fGpffffb710_fadeFar;
    state.DAT_00343888_lights = equipSaved_.DAT_00343888_lights;
    // :44-54. Everything live from slot 10 up gets its saved fade level and
    // its +0x08 with bit 0x10 raised -- which, the pool having just been
    // copied back, is the same as raising the bit.
    for (std::size_t slot = entity::kFirstScriptSlot; slot < entityPool_.slotCount(); ++slot)
    {
      if (entityPool_.status(slot) == entity::SlotStatus::Free || slot == scene::kEquipRingSlot)
      {
        continue;
      }
      auto &restored = entityPool_.slot(slot);
      restored.halfword08 = static_cast<std::uint16_t>(restored.halfword08 | 0x10u);
    }
    // :55-59. The ring, which came back with the copy.
    entity::FUN_00265ec0_destroy_entity(scene::kEquipRingSlot, entityPool_, &state.DAT_00343888_lights);
  }

  // 0x0022FCA8, state 9: the Item screen's ring.
  void PortRuntime::FUN_0022fca8_build_item_ring()
  {
    if (FUN_00230910_build_ring(scene::kItemScreenSlot, -1) == 0)
    {
      DAT_00570df0_message_ = FUN_0025b9e8_text(scene::kEquipNoSpellsMessage);
      DAT_00354da0_equipState_ = scene::kEquipStateNoItems;
      return;
    }
    // 0x22FCC8-0x22FD08: icon 0's description and name, the ring fading in.
    const auto &first = DAT_00570ba0_ringIcons_[0];
    DAT_00570df4_description_ = scene::FUN_00230ce0_split(first.description1a0);
    DAT_00570dec_spellName_ = first.name19c;
    auto &ring = entityPool_.slot(scene::kEquipRingSlot);
    ring.state60 = scene::kRingFadeIn;
    ring.fadeRamp62 = scene::kRingFadeOpenFrom;
    DAT_00354da0_equipState_ = scene::kEquipStateItemBrowse;
    soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueRingOpen);
  }

  // FUN_0022FD38, state 10.
  void PortRuntime::FUN_0022fd38_browse_items(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    int picked = -1;
    if (FUN_002313b8_ring_step(&input, frameTicks, &picked) < 0)
    {
      // :13-21. `sprintf(buf, "%s*%d", name, +0x1C0)` for the front icon.
      if (equipRing198_ >= 0)
      {
        const auto &front = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(equipRing198_)];
        equipItemLine_ = front.name19c + "*" + std::to_string(front.count1c0);
      }
      return;
    }
    // :23-26. Leave, remembering what to use on the way out.
    DAT_00354da0_equipState_ = scene::kEquipStateLeave;
    const auto &chosen = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(picked)];
    equipRing1c8_ = chosen.slot >= 0 ? entityPool_.slot(static_cast<std::size_t>(chosen.slot)).typeId00
                                     : static_cast<std::int16_t>(-1);
  }

  // FUN_0022F588, state 5: wait for the icons to be gone and the ring to have
  // faded down, then back to the slot cursor.
  void PortRuntime::FUN_0022f588_close_ring(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    if (FUN_002313b8_ring_step(&input, frameTicks, nullptr) == 0)
    {
      return;
    }
    if (entityPool_.slot(scene::kEquipRingSlot).state60 == 0)
    {
      DAT_00354da0_equipState_ = scene::kEquipStatePickSlot;
    }
    // :13-17, the highlighted slot's own name and description again.
    selectEquipSlot(equipRing1c0_);
  }

  // FUN_0022F620, state 6: the ring is open.
  void PortRuntime::FUN_0022f620_ring_open(const InputSnapshot &input, std::uint32_t frameTicks)
  {
    int picked = -1;
    if (FUN_002313b8_ring_step(&input, frameTicks, &picked) < 0)
    {
      // :30-47. Up/Down still move the slot cursor, on a fresh press only.
      if ((input.rawPressedPad & 0x5000) != 0)
      {
        const int before = DAT_00570da0_slotIcon_[static_cast<std::size_t>(equipRing1c0_)];
        if (before >= 0)
        {
          entityPool_.slot(static_cast<std::size_t>(before)).animationA0 = 0;
        }
        equipRing1c0_ = static_cast<std::int16_t>(equipRing1c0_ + ((input.rawPressedPad & 0x1000) != 0 ? -1 : 1));
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueMove);
        if (equipRing1c0_ < 0)
        {
          equipRing1c0_ = 2;
        }
        else if (equipRing1c0_ > 2)
        {
          equipRing1c0_ = 0;
        }
        const int after = DAT_00570da0_slotIcon_[static_cast<std::size_t>(equipRing1c0_)];
        if (after >= 0)
        {
          entityPool_.slot(static_cast<std::size_t>(after)).animationA0 = 1;
        }
      }
      return;
    }

    // :48-101, the swap. The front ring icon goes to the slot and the slot's
    // icon goes to where the ring icon was.
    const int slot = equipRing1c0_;
    const int outgoingSlot = DAT_00570da0_slotIcon_[static_cast<std::size_t>(slot)];
    if (picked < 0 || outgoingSlot < 0 || DAT_00570ba0_ringIcons_[static_cast<std::size_t>(picked)].slot < 0)
    {
      // The original dereferences both without a check.
      std::cout << "[equip] swap on slot " << slot << " with no icon -- skipped\n";
      return;
    }
    auto &incoming =
        entityPool_.slot(static_cast<std::size_t>(DAT_00570ba0_ringIcons_[static_cast<std::size_t>(picked)].slot));
    auto &outgoing = entityPool_.slot(static_cast<std::size_t>(outgoingSlot));
    const std::int16_t incomingType = incoming.typeId00;
    const std::int16_t outgoingType = outgoing.typeId00;

    // :52-57. DAT_003435C7[type] is DAT_003437B8[type - 0x1F1].
    auto &counts = sceneScript_.state().DAT_003437b8_itemCounts;
    const auto countAt = [&](std::int16_t type) -> std::uint8_t *
    {
      const int item = type - scene::kEquipIconTypeBase;
      return item >= 0 && item < static_cast<int>(std::size(counts)) ? &counts[item] : nullptr;
    };
    if (auto *count = countAt(incomingType))
    {
      *count = static_cast<std::uint8_t>(*count - 1);
    }
    const int roster = FUN_002298d0_roster(entityPool_.leadPlayer().typeId00);
    battleParty_.FUN_0022f620_set_loadout(static_cast<std::size_t>(slot + roster * 3),
                                         static_cast<std::uint8_t>(incomingType + 0xF));
    if (auto *count = countAt(outgoingType))
    {
      *count = static_cast<std::uint8_t>(*count + 1);
    }
    DAT_00570dd8_slotName_[static_cast<std::size_t>(slot)].clear();

    // :60-89. Two three-point curves, each bowed out sideways by a random
    // 0.25..0.74 and crossing part way up.
    const Vec3 in{incoming.positionX20, incoming.positionZ24, incoming.positionY28};
    const Vec3 out{outgoing.positionX20, outgoing.positionZ24, outgoing.positionY28};
    const float yaw = fieldCamera_.yawRadians();
    {
      const float reach =
          static_cast<float>(static_cast<std::int32_t>(FUN_00216868_random()) % 0x32 + 0x19) / 100.0f;
      const float angle = FUN_00216690_wrap_angle(yaw - kDAT_00352520_quarterTurn);
      const std::array<Vec3, 3> points{
          in, Vec3{in.x + reach * std::cos(angle), in.y + reach * std::sin(angle), in.z + (out.z - in.z) * 0.5f},
          out};
      equipRing1b0_.FUN_00266a78_build(points, true);
    }
    {
      const float reach =
          static_cast<float>(static_cast<std::int32_t>(FUN_00216868_random()) % 0x32 + 0x19) / 100.0f;
      const float angle = FUN_00216690_wrap_angle(yaw + kDAT_00352524_quarterTurn);
      const std::array<Vec3, 3> points{
          out,
          Vec3{out.x + reach * std::cos(angle), out.y + reach * std::sin(angle),
               out.z + (in.z - out.z) * kDAT_00352528_swapRise},
          in};
      equipRing1ac_.FUN_00266a78_build(points, true);
    }
    // :90-101.
    const float facing = FUN_00216690_wrap_angle(yaw + kPi);
    outgoing.facingRadians5c = facing;
    incoming.halfword08 = static_cast<std::uint16_t>(incoming.halfword08 | 0x40u);
    equipRing1c4_ = scene::kRingStepSwap;
    incoming.facingRadians5c = facing;
    DAT_00570da0_slotIcon_[static_cast<std::size_t>(slot)] = -1;
    DAT_00354da0_equipState_ = scene::kEquipStateSwap;
    soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueSwap);
    equipRing19c_ = outgoingSlot;
    equipRing1a4_ = 0.0f;
    equipRing1a0_ = picked;
    std::cout << "[equip] slot " << slot << ": item " << (outgoingType - scene::kEquipIconTypeBase) << " -> "
              << (incomingType - scene::kEquipIconTypeBase) << " at frame " << frameCount_ << '\n';
  }

  // FUN_0022FA18, state 7: the two icons fly, then the slot gets a fresh one.
  void PortRuntime::FUN_0022fa18_swap(std::uint32_t frameTicks)
  {
    const auto &picked = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(equipRing1a0_)];
    auto &incoming = entityPool_.slot(static_cast<std::size_t>(picked.slot));
    auto &outgoing = entityPool_.slot(static_cast<std::size_t>(equipRing19c_));
    const float t = equipRing1a4_ / scene::kEquipSwapTicks;
    const Vec3 a = equipRing1b0_.FUN_00266ce8_sample(t);
    incoming.positionX20 = a.x;
    incoming.positionZ24 = a.y;
    incoming.positionY28 = a.z;
    const Vec3 b = equipRing1ac_.FUN_00266ce8_sample(t);
    outgoing.positionX20 = b.x;
    outgoing.positionZ24 = b.y;
    outgoing.positionY28 = b.z;
    outgoing.groundHeight4c = outgoing.positionY28;
    incoming.groundHeight4c = incoming.positionY28;
    equipRing1a4_ += static_cast<float>(frameTicks);
    if (equipRing1a4_ <= scene::kEquipSwapTicks)
    {
      return;
    }

    // :22-44. A new icon of the incoming type, at rest in the slot's row.
    const int slot = equipRing1c0_;
    const std::size_t s = static_cast<std::size_t>(slot);
    const std::int16_t outgoingType = outgoing.typeId00;
    const std::size_t index =
        entityPool_.FUN_00265e28_allocate_and_initialize(incoming.typeId00, descriptorTable_);
    if (index < entityPool_.slotCount())
    {
      entityPool_.setStatus(index, entity::SlotStatus::ScriptSpawned);
      auto &icon = entityPool_.slot(index);
      const auto &lead = entityPool_.leadPlayer();
      const Vec3 rest = scene::FUN_00230128_slot_position(
          slot, Vec3{lead.positionX20, lead.positionZ24, lead.positionY28}, lead.facingRadians5c);
      icon.positionX20 = rest.x;
      icon.positionZ24 = rest.y;
      icon.positionY28 = rest.z;
      icon.groundHeight4c = icon.positionY28;
      icon.facingRadians5c = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kFGpffff85c0_iconTurn);
      DAT_00570da0_slotIcon_[s] = static_cast<int>(index);
      // Only 0x40: unlike FUN_002302F0's icons, this one is not flat-shaded
      // and its physics is not switched off.
      icon.halfword08 = static_cast<std::uint16_t>(icon.halfword08 | 0x40u);
      icon.animationA0 = 1;
    }
    else
    {
      std::cout << "[equip] no free slot for the swapped-in icon\n";
    }
    iconName19c_[s] = picked.name19c;
    iconDescription1a0_[s] = picked.description1a0;
    DAT_00570df4_description_ = scene::FUN_00230ce0_split(iconDescription1a0_[s]);
    equipRing1c4_ = scene::kRingStepBrowse;
    DAT_00570dd8_slotName_[s] = iconName19c_[s];
    DAT_00354da0_equipState_ = scene::kEquipStateRingOpen;
    // :47-48. The ring again, with the spell just taken off at the front.
    const int outgoingSlot = equipRing19c_;
    FUN_00230910_build_ring(slot, outgoingType - scene::kEquipIconTypeBase);
    entity::FUN_00265ec0_destroy_entity(static_cast<std::size_t>(outgoingSlot), entityPool_,
                                        &sceneScript_.state().DAT_00343888_lights);
  }

  Vec3 PortRuntime::equipRingBonePoint(int bone) const
  {
    const auto &ring = entityPool_.slot(scene::kEquipRingSlot);
    const Vec3 fallback{ring.positionX20, ring.positionZ24, ring.positionY28 + ring.height58 * 0.5f};
    if (scene::kEquipRingSlot >= DAT_00357e00_bonePalettes_.size())
    {
      return fallback;
    }
    return orphen::ported::model::FUN_0020dc88_bone_point(DAT_00357e00_bonePalettes_[scene::kEquipRingSlot],
                                                          static_cast<std::size_t>(bone), Vec3{}, fallback);
  }

  // FUN_00230910.
  int PortRuntime::FUN_00230910_build_ring(int slot, int front)
  {
    auto &lights = sceneScript_.state().DAT_00343888_lights;
    // :17-31. The last ring's icons go first.
    for (int i = 0; i < equipRing1c6_; ++i)
    {
      auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)];
      if (icon.slot >= 0)
      {
        entity::FUN_00265ec0_destroy_entity(static_cast<std::size_t>(icon.slot), entityPool_, &lights);
        icon.slot = -1;
      }
    }
    equipRing1c6_ = 0;

    // :32-56. Every item held that this character can put in this slot.
    const int roster = FUN_002298d0_roster(entityPool_.leadPlayer().typeId00);
    std::uint16_t mask = static_cast<std::uint16_t>((1u << (roster & 0x1F)) & 0xFFFFu);
    mask = static_cast<std::uint16_t>(mask | (slot == 3 ? scene::kItemFlagSlot3 : scene::kItemFlagSpellSlot));
    const auto &counts = sceneScript_.state().DAT_003437b8_itemCounts;
    std::vector<int> items;
    for (int item = 1; item < 0x80 && static_cast<int>(items.size()) < scene::kEquipRingMaxIcons; ++item)
    {
      if (counts[item] == 0)
      {
        continue;
      }
      const auto record = itemDatabase_.FUN_00229688_record(item);
      if (record && (record->ids[0] & mask) == mask)
      {
        items.push_back(item);
      }
    }
    equipRing1c6_ = static_cast<std::int16_t>(items.size());
    if (items.empty())
    {
      return 0;
    }
    // :60-77. `front` goes first, the rest keep their order round the ring.
    if (front >= 0)
    {
      const auto at = std::find(items.begin(), items.end(), front);
      if (at != items.end())
      {
        std::rotate(items.begin(), at, items.end());
      }
    }

    // :78-99. The loop from the ring's bones, then one icon per item.
    DAT_005711f8_ringPath_ =
        scene::FUN_00230450_ring_path([this](int bone) { return equipRingBonePoint(bone); });
    float angle = 0.0f;
    const float step = 80.0f / static_cast<float>(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
      const int item = items[i];
      auto &icon = DAT_00570ba0_ringIcons_[i];
      FUN_002378e0_spawn_ring_icon(step, static_cast<int>(i),
                                   static_cast<std::int16_t>(item + scene::kEquipIconTypeBase), angle);
      icon.count1c0 = counts[item];
      icon.name19c = itemDatabase_.FUN_00229688_name(item);
      icon.description1a0 = itemDatabase_.FUN_00229688_description(item);
    }
    // :100-106.
    if (DAT_00570ba0_ringIcons_[0].slot >= 0)
    {
      entityPool_.slot(static_cast<std::size_t>(DAT_00570ba0_ringIcons_[0].slot)).animationA0 = 1;
    }
    equipRing198_ = 0;
    equipRing1c4_ = scene::kRingStepSlide;
    equipRing1a4_ = 0.0f;
    std::cout << "[equip] ring built for slot " << slot << " with " << items.size() << " icon(s) at frame "
              << frameCount_ << '\n';
    return static_cast<int>(items.size());
  }

  // FUN_002378E0: one ring icon, at loop point 0 and heading for `angle`.
  void PortRuntime::FUN_002378e0_spawn_ring_icon(float step, int index, std::int16_t type, float &angle)
  {
    auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(index)];
    icon.slot = -1;
    const std::size_t slot = entityPool_.FUN_00265e28_allocate_and_initialize(type, descriptorTable_);
    if (slot >= entityPool_.slotCount())
    {
      // :13, FUN_0026BFC0 -- a fatal error on hardware.
      std::cout << "[equip] no free slot for ring icon type 0x" << std::hex << type << std::dec << '\n';
      angle += step;
      return;
    }
    entityPool_.setStatus(slot, entity::SlotStatus::ScriptSpawned);
    auto &entity = entityPool_.slot(slot);
    entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x100u);
    icon.index1be = static_cast<std::int16_t>(index);
    icon.position1bc = static_cast<std::int16_t>(index);
    entity.facingRadians5c = FUN_00216690_wrap_angle(fieldCamera_.yawRadians() + kFGpffff865c_iconTurn);
    const Vec3 start = equipRingBonePoint(0);
    entity.positionX20 = start.x;
    entity.positionZ24 = start.y;
    entity.positionY28 = start.z;
    entity.groundHeight4c = entity.positionY28;
    // :27-33. FUN_0030BD20, a truncating convert.
    const int target = static_cast<int>(angle);
    icon.angle1c5 = static_cast<std::uint8_t>(target);
    icon.curve198 = scene::FUN_00230608_path_curve(DAT_005711f8_ringPath_, 0, target, 1);
    icon.slot = static_cast<int>(slot);
    angle += step;
  }

  // PTR_LAB_0031C370[+0x1C4].
  int PortRuntime::FUN_002313b8_ring_step(const InputSnapshot *input, std::uint32_t frameTicks, int *picked)
  {
    const int count = equipRing1c6_;
    const auto entityOf = [this](const EquipRingIcon &icon) -> entity::OriginalEntity *
    {
      return icon.slot >= 0 ? &entityPool_.slot(static_cast<std::size_t>(icon.slot)) : nullptr;
    };
    switch (equipRing1c4_)
    {
    case scene::kRingStepBrowse:
    {
      // 0x002313F0.
      if (input != nullptr && (input->rawPressedPad & scene::kEquipPadCross) != 0)
      {
        // 0x231420-0x2314B4. The icon at position 0 is the pick. Its +0x1C2
        // is copied to the ring's; nothing reads either.
        equipRing198_ = -1;
        for (int i = 0; i < count; ++i)
        {
          if (DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)].position1bc == 0)
          {
            equipRing198_ = i;
            break;
          }
        }
        if (equipRing198_ < 0)
        {
          return -1;
        }
        if (picked != nullptr)
        {
          *picked = equipRing198_;
        }
        soundEngine_.FUN_00267d38_play_flat(scene::kEquipCuePick);
        return DAT_00570ba0_ringIcons_[static_cast<std::size_t>(equipRing198_)].index1be;
      }
      if (count < 2 || input == nullptr)
      {
        return -1;
      }
      orphen::ported::scene::FieldMenuPad pad;
      pad.uGpffffb684_held = input->rawHeldPad;
      pad.uGpffffb686_pressed = input->rawPressedPad;
      pad.uGpffffb68e_stickDirection = input->rawStickDirection;
      if (!fieldMenu_.FUN_0023b9f8_autoRepeat(0xA000, pad, frameTicks))
      {
        return -1;
      }
      // 0x2314E0-0x2314FC. Left turns the ring one way, anything else the
      // other -- read off uGpffffb684 *after* FUN_0023B9F8 has ORed the
      // stick's direction bits into it, which is what makes the stick turn
      // the ring both ways.
      const int turn = (pad.uGpffffb684_held & 0x8000) != 0 ? 1 : -1;
      for (int i = 0; i < count; ++i)
      {
        const auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)];
        DAT_00570b10_positionAngle_[static_cast<std::size_t>(icon.position1bc)] = icon.angle1c5;
      }
      for (int i = 0; i < count; ++i)
      {
        auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)];
        if (auto *e = entityOf(icon))
        {
          e->animationA0 = 0;
        }
        int position = icon.position1bc + turn;
        if (position < 0)
        {
          position = count - 1;
        }
        else if (position == 0)
        {
          equipRing198_ = i;
        }
        else if (position >= count)
        {
          position = 0;
          equipRing198_ = i;
        }
        icon.position1bc = static_cast<std::int16_t>(position);
        const std::uint8_t target = DAT_00570b10_positionAngle_[static_cast<std::size_t>(position)];
        icon.curve198 = scene::FUN_00230608_path_curve(
            DAT_005711f8_ringPath_, static_cast<std::int8_t>(icon.angle1c5), static_cast<std::int8_t>(target), turn);
        icon.angle1c5 = target;
      }
      equipRing1c4_ = scene::kRingStepSlide;
      equipRing1a4_ = 0.0f;
      soundEngine_.FUN_00267d38_play_flat(scene::kEquipCueTurn);
      std::cout << "[equip] ring turned " << (turn > 0 ? "left" : "right") << " at frame " << frameCount_ << '\n';
      return -1;
    }
    case scene::kRingStepSlide:
    {
      // FUN_00231660.
      float elapsed = equipRing1a4_ + static_cast<float>(frameTicks);
      if (elapsed > scene::kRingSlideTicks)
      {
        elapsed = scene::kRingSlideTicks;
      }
      const float t = elapsed / scene::kRingSlideTicks;
      for (int i = 0; i < count; ++i)
      {
        auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)];
        auto *e = entityOf(icon);
        if (e != nullptr && icon.curve198.pointCount() > 2)
        {
          const Vec3 at = icon.curve198.FUN_00266ce8_sample(t);
          e->positionX20 = at.x;
          e->positionZ24 = at.y;
          e->positionY28 = at.z;
          e->groundHeight4c = e->positionY28;
        }
      }
      if (t == 1.0f)
      {
        // :30-41. The front icon's name and description, and its highlight.
        equipRing1c4_ = scene::kRingStepBrowse;
        if (equipRing198_ >= 0)
        {
          auto &front = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(equipRing198_)];
          if (!front.name19c.empty())
          {
            DAT_00570dec_spellName_ = front.name19c;
            DAT_00570df4_description_ = scene::FUN_00230ce0_split(front.description1a0);
            if (auto *e = entityOf(front))
            {
              FUN_00229820_equip_record(e->typeId00);
            }
          }
          if (auto *e = entityOf(front))
          {
            e->animationA0 = 1;
          }
        }
      }
      else
      {
        equipRing1a4_ = elapsed;
      }
      return -1;
    }
    case scene::kRingStepSwap:
      // 0x002317B0.
      if (equipRing198_ < 0)
      {
        return -1;
      }
      if (picked != nullptr)
      {
        *picked = equipRing198_;
      }
      return DAT_00570ba0_ringIcons_[static_cast<std::size_t>(equipRing198_)].index1be;
    case scene::kRingStepShrink:
    {
      // FUN_002317E0. The list keeps its entries after the release; the next
      // FUN_00230910 releases them again, as the original does.
      int live = 0;
      for (int i = 0; i < count; ++i)
      {
        auto &icon = DAT_00570ba0_ringIcons_[static_cast<std::size_t>(i)];
        auto *e = entityOf(icon);
        if (e == nullptr || e->typeId00 <= 0)
        {
          continue;
        }
        entity::FUN_00229ef0_set_scale(
            *e, e->scale14c - static_cast<float>(frameTicks) * scene::kDAT_00352560_shrinkPerTick,
            &descriptorTable_);
        if (0.0f < e->scale14c)
        {
          ++live;
        }
        else
        {
          entity::FUN_00265ec0_destroy_entity(static_cast<std::size_t>(icon.slot), entityPool_,
                                              &sceneScript_.state().DAT_00343888_lights);
        }
      }
      return live == 0 ? 1 : 0;
    }
    default:
      // 0x002318B8.
      return 0;
    }
  }

  // FUN_002333E8.
  void PortRuntime::FUN_002333e8_ring_fade(std::uint32_t frameTicks)
  {
    auto &ring = entityPool_.slot(scene::kEquipRingSlot);
    const std::uint16_t mode = ring.state60;
    int level = 0;
    if (static_cast<std::uint16_t>(mode - 1) < 2)
    {
      level = static_cast<std::int16_t>(ring.fadeRamp62 - static_cast<int>(frameTicks) * 4);
      ring.fadeRamp62 = static_cast<std::uint16_t>(level);
      if (level < 0x80)
      {
        if (mode != scene::kRingFadeOutRelease)
        {
          ring.state60 = 0;
          return;
        }
        // :17-21. Not reachable from this screen: nothing sets mode 1.
        entity::FUN_00265ec0_destroy_entity(scene::kEquipRingSlot, entityPool_,
                                            &sceneScript_.state().DAT_00343888_lights);
        return;
      }
    }
    else
    {
      if (mode != scene::kRingFadeIn)
      {
        return;
      }
      level = static_cast<std::int16_t>(ring.fadeRamp62 + static_cast<int>(frameTicks) * 4);
      ring.fadeRamp62 = static_cast<std::uint16_t>(level);
      if (level > scene::kRingFadeFull)
      {
        ring.state60 = 0;
        ring.fadeLevel134 = 0;
        return;
      }
    }
    ring.fadeLevel134 = static_cast<std::uint8_t>((level < 0 ? level + 0x1F : level) >> 5);
  }

  std::vector<orphen::ported::text::DialogueSprite> PortRuntime::buildEquipScreenSprites() const
  {
    scene::EquipScreenDraw draw;
    draw.state = DAT_00354da0_equipState_;
    draw.equip = fieldMenu_.uGpffffae34_selected() == scene::kFieldMenuEquipItem;
    draw.descriptionTwice = equipDescriptionTwice_;
    draw.characterName = DAT_00570de8_characterName_;
    draw.spellName = DAT_00570dec_spellName_;
    draw.descriptionLines = DAT_00570df4_description_;
    draw.slotNames = DAT_00570dd8_slotName_;
    for (std::size_t row = 0; row < draw.slotIcon.size(); ++row)
    {
      draw.slotIcon[row] = DAT_00570da0_slotIcon_[row] >= 0;
    }
    draw.rowX = equipRowX_;
    draw.caption = FUN_0025b9e8_text(scene::kFieldMenuCaptionMessage);
    draw.noSpellsBox = equipNoSpellsDrawn_;
    if (equipNoSpellsDrawn_)
    {
      draw.noSpellsMessage = DAT_00570df0_message_;
      draw.noSpellsCaption = FUN_0025b9e8_text(static_cast<std::size_t>(equipNoItemsCaption_));
    }
    draw.itemBrowseLine = !equipItemLine_.empty();
    draw.itemLine = equipItemLine_;
    return scene::FUN_0022e910_layout(draw, dialogueFont_);
  }

} // namespace orphen::port
