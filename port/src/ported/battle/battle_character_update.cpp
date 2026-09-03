#include "ported/battle/battle_character_update.h"

#include "ported/entity/actor_frame_update.h"

#include <array>
#include <cmath>

namespace orphen::ported::battle
{
  namespace
  {
    using orphen::ported::entity::OriginalEntity;

    // The five action-pair members plus the three the battle module writes
    // itself. Signed-char spellings from the decompilation in the comments.
    constexpr std::uint8_t kActionIdle06 = 0x06;
    constexpr std::uint8_t kActionRecover87 = 0x87;  // -0x79
    constexpr std::uint8_t kActionStagger83 = 0x83;  // -0x7D
    constexpr std::uint8_t kActionSummon94 = 0x94;   // -0x6C

    // FUN_00249270: min(party record +0x3C, 0x2580) / divisor. With 0x780 that
    // is the **charge level, 0..4** -- the number the fire states stamp into
    // the effect entity's +0x94, which is what makes a charged Hand of Pyro
    // throw more projectiles than a tapped one.
    std::int32_t FUN_00249270_charge(const BattleParty &party, std::uint32_t member, std::int16_t divisor)
    {
      std::int32_t timer =
          party.tables().read<std::int16_t>(BattleTables::partyRecord(member) + record::kChargeTimer3c);
      if (timer > 0x2580)
      {
        timer = 0x2580;
      }
      if (timer == 0 || divisor == 0)
      {
        return 0;
      }
      return timer / divisor;
    }

    // FUN_00249128: accumulate that timer by this frame's ticks, capped at
    // 0x2D00. The first tick also starts the charge sound loop.
    // FUN_00249218 (0x00249218): the raw charge accumulator, party record +0x3C,
    // divided. Unlike FUN_00249270 it does not cap.
    std::int32_t FUN_00249218_charge(const BattleParty &party, std::uint32_t member,
                                     std::int16_t divisor)
    {
      const std::int32_t timer =
          party.tables().read<std::int16_t>(BattleTables::partyRecord(member) + record::kChargeTimer3c);
      if (timer == 0 || divisor == 0)
      {
        return 0;
      }
      return timer / divisor;
    }

    // FUN_0024c058:60-76 and :98-103, the spell voice's load-and-speak walk.
    // Shared verbatim by state 113, which carries the same block.
    //
    // The steps are the original's: 1 asks for the bank, 2 waits for the load,
    // 3 is "loaded and not yet spoken". Step 3 becomes 100 at the charge marker
    // once the clip actually starts, and 100 is what state 112 reads to know it
    // owes a release line.
    void FUN_0024c058_step_spell_voice(const BattleUpdateEnvironment &environment,
                                       std::uint32_t slot)
    {
      BattleParty &party = *environment.party;
      const std::uint32_t at = kDAT_0031da60_voiceState + slot * 2;
      const std::int16_t step = party.tables().read<std::int16_t>(at);
      if (step == 1)
      {
        const std::uint32_t bank =
            party.tables().read<std::uint32_t>(kDAT_0031da54_family + slot * 4);
        if (environment.FUN_00206ae0_cache_voice &&
            environment.FUN_00206ae0_cache_voice(bank, slot))
        {
          party.tables().write<std::int16_t>(at, 2);
        }
      }
      if (party.tables().read<std::int16_t>(at) != 2)
      {
        return;
      }
      if (environment.FUN_00206c28_voice_load_idle && environment.FUN_00206c28_voice_load_idle())
      {
        party.tables().write<std::int16_t>(at, 3);
      }
    }

    // FUN_002d9b78 (0x002d9b78): drive the caster's ground ring from the charge,
    // and answer whether the charge is allowed to keep building.
    //
    // **This is the gate the port was missing.** States 111, 113 and 107 do not
    // accumulate unconditionally -- they accumulate only while this returns
    // non-zero, and it returns non-zero only while the ring (type 0x18F,
    // DAT_0031da8c) is at animation 0, its open state. A ring that is closed,
    // opening, or gone stops the charge dead. That is why the ring is not
    // decoration: it *is* the charge indicator, and the state it is in is the
    // charge's own state.
    //
    // `scaleRing` is the original's param_2 as a boolean: the hold states pass
    // 1 and get the ring resized, FUN_0024bae0's release passes 0 and only
    // reads the gate.
    std::int32_t FUN_002d9b78_drive_cast_ring(const BattleUpdateEnvironment &environment,
                                              std::uint32_t member,
                                              bool scaleRing)
    {
      BattleParty &party = *environment.party;
      const std::int32_t markerSlot =
          party.entitySlotAt(kDAT_0031da8c_slotEntity + member * 4);
      if (markerSlot == kNoEntity)
      {
        return -1; // 0xffff, the original's "no ring" answer -- still non-zero.
      }
      auto &marker = environment.pool->slot(static_cast<std::size_t>(markerSlot));

      std::int32_t charge = 0;
      if (scaleRing)
      {
        charge = FUN_00249218_charge(party, member, 0x3C);
        if (static_cast<std::int16_t>(marker.animationA0) == 2)
        {
          orphen::ported::entity::FUN_00225bc8_set_animation(marker, 1);
        }
      }
      if (static_cast<std::int16_t>(marker.animationA0) != 0)
      {
        return 0;
      }
      // 1.4 and 1.2 over 160: at a full 0x2D00 charge the ring reaches about
      // 7.7x wide and 6.2x tall.
      marker.scale14c = (static_cast<float>(charge) * 1.4f) / 160.0f + 1.0f;
      marker.scaleZ150 = (static_cast<float>(charge) * 1.2f) / 160.0f + 0.5f;
      marker.markerCharge19a =
          static_cast<std::uint16_t>(FUN_00249218_charge(party, member, 0x20));
      return charge | 1;
    }

    void FUN_00249128_accumulate_charge(BattleParty &party,
                                        std::uint32_t member,
                                        std::uint16_t frameTicks)
    {
      const std::uint32_t at = BattleTables::partyRecord(member) + record::kChargeTimer3c;
      std::uint32_t value = party.tables().read<std::uint16_t>(at);
      value += frameTicks;
      if (value > 0x2D00)
      {
        value = 0x2D00;
      }
      party.tables().write<std::uint16_t>(at, static_cast<std::uint16_t>(value));
    }

    // FUN_00248ee0: set the animation and clear entity +0x06 bits 0 and 0x11.
    void FUN_00248ee0_set_animation(OriginalEntity &entity, std::uint16_t animation)
    {
      orphen::ported::entity::FUN_00225bc8_set_animation(entity, animation);
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEE);
    }

    // FUN_00248e98: the same, but only when the animation is actually changing.
    void FUN_00248e98_set_animation_if_changed(OriginalEntity &entity, std::uint16_t animation)
    {
      if (entity.animationA0 != animation)
      {
        orphen::ported::entity::FUN_00225bc8_set_animation(entity, animation);
      }
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEE);
    }

    // FUN_00249348: "is this the player, and is the player a class that gets
    // the spell voice line". Class 4 and 7 do not.
    bool FUN_00249348_is_voiced_player(const BattleParty &party, const OriginalEntity &entity)
    {
      if (entity.byte95 != 1)
      {
        return false;
      }
      const std::int16_t leadClass =
          party.tables().read<std::int16_t>(BattleTables::partyRecord(0) + record::kClass00);
      return leadClass != 4 && leadClass != 7;
    }

    // FUN_00245a00: spawn an effect entity and put it in the state the battle
    // states expect -- animation 2, unattached, scale 1.
    std::int32_t FUN_00245a00_spawn_effect(const BattleUpdateEnvironment &environment,
                                           std::int32_t typeId)
    {
      if (environment.pool == nullptr || environment.descriptors == nullptr || typeId == 0)
      {
        return kNoEntity;
      }
      const std::size_t slot =
          environment.pool->FUN_00265e28_allocate_and_initialize(typeId, *environment.descriptors);
      if (slot >= orphen::ported::entity::kEntitySlotCount)
      {
        return kNoEntity;
      }
      auto &effect = environment.pool->slot(slot);
      effect.animationA0 = 2;
      effect.parentSlot192 = -1;
      effect.flags06 = static_cast<std::uint16_t>(effect.flags06 | 0x11);
      effect.state60 = 0;
      effect.attachBone194 = 0;
      return static_cast<std::int32_t>(slot);
    }

    // The three lines every spell state repeats on entry: destroy whatever
    // effect entity the slot held, spawn the one the loadout names, and hang it
    // off the caster's hand bone.
    //
    //   FUN_00265ec0(table[member]);
    //   table[member] = FUN_00245a00(DAT_0031da3a[member*6 + slot*2]);
    //   effect->+0x192 = casterSlot;  effect->+0x194 = 0x12 for class 1
    //
    // Bone 0x12 is the hand for Orphen (class 1) and 0x0E for class 4.
    std::int32_t respawnSlotEffect(const BattleUpdateEnvironment &environment,
                                   std::uint32_t member,
                                   std::uint32_t tableBase,
                                   std::size_t casterSlot)
    {
      BattleParty &party = *environment.party;
      const auto &caster = environment.pool->slot(casterSlot);
      const std::uint8_t chosen = party.selectedSlot(static_cast<std::int16_t>(caster.byte95));
      const std::uint16_t effectType = party.tables().read<std::uint16_t>(
          kDAT_0031da3a_effectTypes + member * 6 + static_cast<std::uint32_t>(chosen) * 2);

      const std::int32_t previous = party.entitySlotAt(tableBase + member * 4);
      if (previous != kNoEntity)
      {
        environment.pool->releaseSlot(static_cast<std::size_t>(previous));
      }
      const std::int32_t spawned = FUN_00245a00_spawn_effect(environment, effectType);
      party.setEntitySlotAt(tableBase + member * 4, spawned);
      if (spawned == kNoEntity)
      {
        return kNoEntity;
      }

      auto &effect = environment.pool->slot(static_cast<std::size_t>(spawned));
      effect.parentSlot192 = static_cast<std::int16_t>(casterSlot);
      const std::int16_t characterClass =
          party.tables().read<std::int16_t>(BattleTables::partyRecord(member) + record::kClass00);
      if (characterClass == 1)
      {
        effect.attachBone194 = 0x12;
      }
      else if (characterClass == 4)
      {
        effect.attachBone194 = 0x0E;
      }

      // +0x12C is the effect's attack power: the caster's, plus the per-slot
      // byte the party record took from the item's +0x07.
      effect.attackPower12c = static_cast<std::uint16_t>(
          caster.attackPower12c +
          party.tables().read<std::int8_t>(BattleTables::partyRecord(member) + record::kSpellByte14 +
                                           chosen));
      // +0x198 points at the party record's four-byte element block for the
      // slot, which is what the hit test reads the element and power out of.
      effect.hitParameters198 = BattleTables::partyRecord(member) + record::kSpellBlock18 +
                                static_cast<std::uint32_t>(chosen) * 4;
      return spawned;
    }

    // ------------------------------------------------------------ the handlers
    //
    // Signature matches the original's: the entity and its +0x62, returning the
    // value to write back into +0x62.

    struct StateContext
    {
      const BattleUpdateEnvironment *environment;
      BattleParty *party;
      OriginalEntity *entity;
      std::size_t entitySlot;
      std::uint32_t member;
      std::uint32_t control;
    };

    using StateHandler = std::uint16_t (*)(const StateContext &, std::uint16_t);

    void setAction(const StateContext &context, std::uint8_t action)
    {
      context.party->tables().write<std::uint8_t>(context.control + control::kCurrentAction0f, action);
    }

    // LAB_0024a538 / LAB_0024a868 / LAB_0024cf18: `jr ra; move v0, zero`. States
    // 101, 103, 104 and 119 do nothing at all.
    std::uint16_t stateNothing(const StateContext &, std::uint16_t charge) { return charge; }

    // LAB_0024bd08 (state 116) and LAB_0024cef8 (state 118): end the action and
    // fall back to the battle-ready idle. Four instructions each.
    //
    //   DAT_00355cb8[3] = 6;  entity->+0x60 = 120;  [+0x06 &= 0xFFEF for 116]
    std::uint16_t stateEndAction116(const StateContext &context, std::uint16_t charge)
    {
      setAction(context, kActionIdle06);
      context.entity->state60 = 120;
      context.entity->flags06 = static_cast<std::uint16_t>(context.entity->flags06 & 0xFFEF);
      return charge;
    }
    std::uint16_t stateEndAction118(const StateContext &context, std::uint16_t charge)
    {
      setAction(context, kActionIdle06);
      context.entity->state60 = 120;
      return charge;
    }

    // FUN_0024cf20 (states 120 and 122), and FUN_0024d128 (121) which tail-calls
    // it. The battle-ready idle: hold the stance, and if the character has
    // drifted from where the control block last recorded it, hand over to the
    // walk-back state 108.
    std::uint16_t stateIdle120(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      auto &tables = context.party->tables();

      if (static_cast<std::int16_t>(entity.pendingDamageBe) < 0)
      {
        entity.pendingDamageBe = 0;
      }
      context.party->FUN_00249108_clear_turn_flags(context.member);

      // The control block's target is forced positive here, and to 1 when it is
      // zero -- a leftover of the enemy-side bookkeeping. With no enemies this
      // parks it at 1 rather than -1, so it is written exactly as the original
      // does and the no-target branch keys off `< 3` rather than off -1.
      std::int16_t target = tables.read<std::int16_t>(context.control + control::kTarget2c);
      if (target < 0)
      {
        target = static_cast<std::int16_t>(-target);
        tables.write<std::int16_t>(context.control + control::kTarget2c, target);
      }
      if (target == 0)
      {
        tables.write<std::int16_t>(context.control + control::kTarget2c, 1);
      }

      if ((entity.state60 & 0x4000) != 0 || (entity.flags06 & 0x10) != 0 ||
          entity.stateResetA4 == 0x7FFF)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        if ((entity.animationA0 != 2 && entity.animationA0 != 7) || (entity.flags06 & 0x10) != 0)
        {
          FUN_00248e98_set_animation_if_changed(entity, 2);
        }
      }
      if (tables.read<std::uint32_t>(kDAT_0031da6c_memberFlags + context.member * 4) != 0)
      {
        FUN_00248e98_set_animation_if_changed(entity, 6);
      }
      if ((entity.flags06 & 1) != 0)
      {
        entity.state60 = 0x4078;
        setAction(context, kActionIdle06);
      }

      // The walk-back timer lives in party record +0x28 + 0x1A. While it runs
      // nothing else happens; when it expires and the character has drifted
      // more than fGpffff8844 from its recorded spot, state 108 walks it home.
      const std::uint32_t timerAt = BattleTables::partyRecord(context.member) + record::kReturnTimer42;
      const std::int16_t walkTimer = tables.read<std::int16_t>(timerAt);
      if (walkTimer != 0)
      {
        const std::uint16_t stepped =
            FUN_00248e58_step_timer(static_cast<std::uint16_t>(walkTimer),
                                    context.environment->frameTicks);
        tables.write<std::int16_t>(timerAt, static_cast<std::int16_t>(stepped));
        if (stepped != 0)
        {
          return charge;
        }
      }
      tables.write<std::int16_t>(timerAt, FUN_00248e48_arm_timer(0x3C));

      const float homeX =
          static_cast<float>(tables.read<std::int16_t>(context.control + control::kPosX14)) / 10.0f;
      const float homeZ =
          static_cast<float>(tables.read<std::int16_t>(context.control + control::kPosY16)) / 10.0f;
      const float distance = std::hypot(homeZ - entity.positionZ24, homeX - entity.positionX20);
      // fGpffff8844. The original's threshold; anything closer than this counts
      // as "already home".
      constexpr float kReturnDistance = 0.3f;
      if (distance <= kReturnDistance)
      {
        return charge;
      }
      entity.state60 = 0x406C;
      setAction(context, kActionRecover87);
      return charge;
    }

    // FUN_0024b7d0 (state 107): the kind-0 charge, which with the shipped
    // loadout is Cross holding Sword of the Fallen Devil.
    //
    // On entry it swaps the attack entity for the one this slot names, hangs it
    // off the hand, plays animation 0x33 and resets the charge accumulator.
    // After that it does nothing per frame *except* grow the effect: the
    // accumulation happens in FUN_002d9b78's branch, and the scale comes
    // straight out of the charge level.
    std::uint16_t stateChargeAttack107(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;

      if (party.entitySlotAt(kDAT_0031daac_shieldEntity + context.member * 4) == kNoEntity)
      {
        entity.state60 = 0x4078;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        setAction(context, kActionIdle06);
        return charge;
      }

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        respawnSlotEffect(*context.environment, context.member, kDAT_0031da9c_attackEntity,
                          context.entitySlot);
        FUN_00248e98_set_animation_if_changed(entity, 0x33);
        party.FUN_00249108_clear_turn_flags(context.member);
      }

      // +0xAA bit 0x800 is the timeline marker the animation raises on the
      // frames the charge is allowed to build; +0x06 bit 2 is "the timeline
      // advanced this frame".
      if ((entity.flagsAa & 0x800) != 0 && (entity.flags06 & 4) != 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        FUN_00249128_accumulate_charge(party, context.member, context.environment->frameTicks);
        const std::int32_t attack = party.entitySlotAt(kDAT_0031da9c_attackEntity + context.member * 4);
        if (attack != kNoEntity)
        {
          auto &effect = context.environment->pool->slot(static_cast<std::size_t>(attack));
          if (effect.animationA0 == 2)
          {
            FUN_00248ee0_set_animation(effect, 1);
          }
          // FUN_00249270(entity, 6): the same accumulator on a finer divisor,
          // so the visible growth is continuous rather than in four steps.
          effect.scaleZ150 =
              static_cast<float>(FUN_00249270_charge(party, context.member, 6) + 700) / 1000.0f;
          effect.scale14c = 1.0f;
        }
      }
      return charge;
    }

    // FUN_0024ac88 (state 105): the kind-0 release -- the swing that actually
    // fires. Ported as its state machine: the three-animation swing chain
    // (0x33 -> 0x30 -> 0x31), the effect entity re-spawn at the charge level,
    // and the two exits back to 108.
    std::uint16_t stateFireAttack105(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      const std::uint8_t chosen = party.selectedSlot(static_cast<std::int16_t>(entity.byte95));

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEF);
        if (entity.animationA0 != 0x33)
        {
          FUN_00248e98_set_animation_if_changed(entity, 0x33);
        }
        // The effect entity is re-spawned only when the slot's type has
        // changed under it, then stamped with the charge level: +0x94 is what
        // the effect's own behaviour reads to decide how much of itself to
        // produce.
        const std::int32_t attack =
            respawnSlotEffect(*context.environment, context.member, kDAT_0031da9c_attackEntity,
                              context.entitySlot);
        if (attack != kNoEntity)
        {
          auto &effect = context.environment->pool->slot(static_cast<std::size_t>(attack));
          effect.spawnParam94 =
              static_cast<std::uint8_t>(FUN_00249270_charge(party, context.member, 0x780));
          effect.scaleZ150 =
              static_cast<float>(FUN_00249270_charge(party, context.member, 6) + 700) / 1000.0f;
          effect.scale14c = 1.0f;
        }
        (void)chosen;
        return charge;
      }

      // The swing chain. +0xAA bit 0x200 marks the frames a swing may advance
      // on; +0x06 bits 0 and 2 are the timeline's end and step markers.
      if ((entity.flagsAa & 0x200) != 0 && (entity.flags06 & 5) != 0)
      {
        if (entity.animationA0 == 0x33)
        {
          FUN_00248ee0_set_animation(entity, 0x30);
        }
        else if (entity.animationA0 == 0x30)
        {
          FUN_00248ee0_set_animation(entity, 0x31);
        }
        else
        {
          FUN_00248ee0_set_animation(entity, 0x33);
        }
      }

      // Either exit hands back to 108, the walk-back state, with action 0x87.
      if ((entity.flags06 & 1) != 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        entity.state60 = 0x406C;
        setAction(context, kActionRecover87);
        const std::int32_t attack = party.entitySlotAt(kDAT_0031da9c_attackEntity + context.member * 4);
        if (attack != kNoEntity)
        {
          FUN_00248ee0_set_animation(context.environment->pool->slot(static_cast<std::size_t>(attack)), 2);
        }
      }
      return charge;
    }

    // FUN_0024b410 (state 106): the approach run between releasing a kind-0
    // charge and the swing landing. With a real target it runs the character at
    // it; **with no target it is a pass-through**, and that is not a shortcut
    // taken here -- it is the original's own arithmetic:
    //
    //   sVar3 = block +0x20;  if (sVar3 < 1) { pending = 0x84; return 0; }
    //   block +0x20 = -sVar3;                       // mark "approaching"
    //   ...
    //   iVar7 = (int)((uint)(u16)block+0x20 * -0x10000) >> 0x10;   // negate
    //   if (iVar7 < 2) goto LAB_0024b7ac;           // pending = 0x84
    //
    // FUN_0024cf20 has already forced the target positive and, when it was
    // zero, to 1. So the negate gives 1, 1 < 2, and the state asks for 0x84 --
    // state 105, the swing -- on its first frame. The approach body below the
    // test needs the target's world position and cannot run without an enemy.
    std::uint16_t stateApproach106(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      auto &tables = party.tables();
      const std::uint32_t targetAt = context.control + control::kTarget2c;

      const auto requestSwing = [&] {
        tables.write<std::uint8_t>(context.control + control::kPendingAction0e, 0x84);
      };

      if ((tables.read<std::uint32_t>(kDAT_0031da0c_memberFlags2 + context.member * 4) & 3) != 0)
      {
        requestSwing();
        return 0;
      }
      if (party.entitySlotAt(kDAT_0031da9c_attackEntity + context.member * 4) == kNoEntity)
      {
        entity.state60 = 0x406C;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        setAction(context, kActionRecover87);
        return 1;
      }

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        const std::int16_t characterClass = tables.read<std::int16_t>(
            BattleTables::partyRecord(context.member) + record::kClass00);
        FUN_00248e98_set_animation_if_changed(entity, (characterClass == 6) ? 0xC5 : 8);
        const std::int16_t target = tables.read<std::int16_t>(targetAt);
        if (target < 1)
        {
          requestSwing();
          return 0;
        }
        tables.write<std::int16_t>(targetAt, static_cast<std::int16_t>(-target));
        // The turn timer the approach would spend is derived from the angle to
        // the target, which there is not one of.
      }

      const std::uint16_t stored = tables.read<std::uint16_t>(targetAt);
      if (stored != 0)
      {
        const std::int32_t negated =
            static_cast<std::int32_t>(-(static_cast<std::int32_t>(stored) << 16)) >> 16;
        if (negated < 2)
        {
          requestSwing();
          return 0;
        }
        party.recordTargetFacingReached();
      }
      requestSwing();
      return 0;
    }

    // FUN_0024bae0 (state 109) -- and the tail of FUN_0024c3e0 (112) and
    // FUN_0024c4e0 (110), both of which end by calling it. The spell *release*:
    // the frame the timeline reaches its throw marker, the effect entity is
    // handed the charge level and cut loose.
    std::uint16_t stateReleaseSpell109(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      const std::int32_t shield = party.entitySlotAt(kDAT_0031daac_shieldEntity + context.member * 4);
      if (shield == kNoEntity)
      {
        entity.state60 = 0x4078;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        setAction(context, kActionIdle06);
        return charge;
      }
      auto &effect = context.environment->pool->slot(static_cast<std::size_t>(shield));

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        FUN_00248e98_set_animation_if_changed(entity, 0x14);
        // FUN_00215e48: forget whatever the previous cast already hit.
        orphen::ported::entity::FUN_00215e48_clear_hit_set(entity);
      }

      // +0xAA bit 0x100 is the throw marker.
      if ((entity.flagsAa & 0x100) != 0 && (entity.flags06 & 4) != 0)
      {
        const std::uint8_t chosen = party.selectedSlot(static_cast<std::int16_t>(entity.byte95));
        const std::uint32_t recordBase = BattleTables::partyRecord(context.member);
        effect.attackPower12c = static_cast<std::uint16_t>(
            entity.attackPower12c +
            party.tables().read<std::int8_t>(recordBase + record::kSpellByte14 + chosen));
        effect.hitParameters198 =
            recordBase + record::kSpellBlock18 + static_cast<std::uint32_t>(chosen) * 4;
        effect.state60 = 1;
        // **The charge level.** FUN_00249270(entity, 0x780) is 0..4, and
        // `(level + 1) - 1` puts it into the effect's +0x94 -- the projectile
        // count for Hand of Pyro, the blade length for a sword spell.
        effect.spawnParam94 =
            static_cast<std::uint8_t>(FUN_00249270_charge(party, context.member, 0x780));
        // FUN_0024bae0:38. Read-only here: it stops the ring pulsing without
        // resizing it, because the charge is about to be spent.
        FUN_002d9b78_drive_cast_ring(*context.environment, context.member, false);
      }

      if ((entity.flags06 & 1) == 0)
      {
        return charge;
      }
      if (effect.animationA0 != 2)
      {
        FUN_00248ee0_set_animation(effect, 2);
        effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 0x10);
      }
      setAction(context, kActionIdle06);
      const std::uint32_t roll =
          context.environment->FUN_00216868_random ? context.environment->FUN_00216868_random() : 0;
      entity.animationA0 = (roll & 1) ? 0x13 : 0x2F;
      entity.state60 = 120;
      setAction(context, kActionIdle06);
      return charge;
    }

    // FUN_0024c058 (state 111): the kind > 0 hold -- Triangle with Hand of
    // Pyro. Same shape as 107: swap the effect in on entry, then accumulate.
    std::uint16_t stateChargeSpellA111(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      if (party.entitySlotAt(kDAT_0031daac_shieldEntity + context.member * 4) == kNoEntity)
      {
        entity.state60 = 0x4078;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        setAction(context, kActionIdle06);
        return charge;
      }

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        respawnSlotEffect(*context.environment, context.member, kDAT_0031daac_shieldEntity,
                          context.entitySlot);
        FUN_00248e98_set_animation_if_changed(entity, 0x14);
        // :37-40. Only a voiced character arms the spell voice.
        if (FUN_00249348_is_voiced_player(party, entity))
        {
          party.tables().write<std::int16_t>(
              kDAT_0031da60_voiceState + party.selectedSlot(static_cast<std::int16_t>(entity.byte95)) * 2,
              1);
        }
        party.FUN_00249108_clear_turn_flags(context.member);
      }

      // :60-76. The bank load walks whether or not the charge is building.
      const std::uint32_t voiceSlot = party.selectedSlot(static_cast<std::int16_t>(entity.byte95));
      if (FUN_00249348_is_voiced_player(party, entity))
      {
        FUN_0024c058_step_spell_voice(*context.environment, voiceSlot);
      }

      if ((entity.flagsAa & 0x800) != 0 && (entity.flags06 & 4) != 0)
      {
        const std::int32_t shield =
            party.entitySlotAt(kDAT_0031daac_shieldEntity + context.member * 4);
        if (entity.animationA0 == 0x14 && (entity.flags06 & 0x10) == 0 && shield != kNoEntity)
        {
          auto &effect = context.environment->pool->slot(static_cast<std::size_t>(shield));
          FUN_00248ee0_set_animation(effect, 1);
          effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 0x10);
        }
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        // FUN_0024c058:95. The charge builds only while the ground ring is
        // open; FUN_002d9b78 both resizes it and answers the gate.
        if (FUN_002d9b78_drive_cast_ring(*context.environment, context.member, true) != 0)
        {
          FUN_00249128_accumulate_charge(party, context.member, context.environment->frameTicks);
          // :98-103. The incantation, clip 0, on the first charge frame after
          // the bank has landed -- and only if nothing else is speaking, which
          // is what stops a spell talking over a cutscene line.
          if (FUN_00249348_is_voiced_player(party, entity) && entity.state60 != 0x6B &&
              party.tables().read<std::int16_t>(kDAT_0031da60_voiceState + voiceSlot * 2) == 3 &&
              context.environment->FUN_00206a90_voice_busy &&
              !context.environment->FUN_00206a90_voice_busy() &&
              context.environment->FUN_00206f08_play_voice &&
              context.environment->FUN_00206f08_play_voice(voiceSlot, 0))
          {
            party.tables().write<std::int16_t>(kDAT_0031da60_voiceState + voiceSlot * 2, 100);
          }
        }
      }
      return charge;
    }

    // FUN_0024c3e0 (state 112): the kind > 0 release. Its own body is the
    // voice-line bookkeeping; the work is FUN_0024bae0, which it tail-calls.
    std::uint16_t stateReleaseSpellA112(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEF);
      if (entity.timelineCursorA8 == 10 && (entity.flags06 & 4) != 0 &&
          FUN_00249348_is_voiced_player(*context.party, entity))
      {
        if (FUN_00249270_charge(*context.party, context.member, 0x780) > 4)
        {
          context.party->FUN_0023f620_count(3, static_cast<std::int8_t>(entity.byte95));
        }
        // :21-31. The release shout, clip 1 of the same bank. 101 means it was
        // spoken, 102 that something else had the voice and it was dropped --
        // the original does not queue it.
        BattleParty &party = *context.party;
        const std::uint32_t voiceSlot =
            party.selectedSlot(static_cast<std::int16_t>(entity.byte95));
        const std::uint32_t at = kDAT_0031da60_voiceState + voiceSlot * 2;
        if (party.tables().read<std::int16_t>(at) == 100)
        {
          const bool busy = context.environment->FUN_00206a90_voice_busy &&
                            context.environment->FUN_00206a90_voice_busy();
          if (!busy)
          {
            if (context.environment->FUN_00206f08_play_voice)
            {
              context.environment->FUN_00206f08_play_voice(voiceSlot, 1);
            }
            party.tables().write<std::int16_t>(at, 0x65);
          }
          else
          {
            party.tables().write<std::int16_t>(at, 0x66);
          }
        }
      }
      return stateReleaseSpell109(context, charge);
    }

    // FUN_0024c538 (state 113): the kind < 0 hold -- Circle with Bite of
    // Lightning. Unlike 111 this one steers: while party record +0x28+0x1D is
    // not -1 the character is aiming, and the charge accumulates.
    std::uint16_t stateChargeSpellB113(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      if (party.entitySlotAt(kDAT_0031daac_shieldEntity + context.member * 4) == kNoEntity)
      {
        entity.state60 = 0x4078;
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
        setAction(context, kActionIdle06);
        return charge;
      }

      const std::uint32_t aimAt = BattleTables::partyRecord(context.member) + record::kAimMarker45;
      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        const std::int32_t shield =
            respawnSlotEffect(*context.environment, context.member, kDAT_0031daac_shieldEntity,
                              context.entitySlot);
        FUN_00248e98_set_animation_if_changed(entity, 0x3A);
        party.FUN_00249108_clear_turn_flags(context.member);
        party.tables().write<std::int8_t>(aimAt, -1);
        if (shield != kNoEntity)
        {
          auto &effect = context.environment->pool->slot(static_cast<std::size_t>(shield));
          FUN_00248ee0_set_animation(effect, 1);
          effect.halfword08 = static_cast<std::uint16_t>(effect.halfword08 | 0x10);
        }
      }

      // +0xAA bit 0x200 takes the aim marker off -1 and starts the charge.
      if ((entity.flagsAa & 0x200) != 0)
      {
        party.tables().write<std::int8_t>(
            aimAt, static_cast<std::int8_t>(static_cast<std::int16_t>(entity.timelineCursorA8) / 2));
      }
      if (party.tables().read<std::int8_t>(aimAt) != -1)
      {
        FUN_00249128_accumulate_charge(party, context.member, context.environment->frameTicks);
      }
      return charge;
    }

    // FUN_0024c910 (state 114): the kind < 0 release. Like 112 it ends in
    // FUN_0024bae0.
    std::uint16_t stateReleaseSpellB114(const StateContext &context, std::uint16_t charge)
    {
      return stateReleaseSpell109(context, charge);
    }

    // FUN_0024cba0 (state 117): the shield. Holds animation 0x3C, raises the
    // guard entity, and -- the part that matters for damage -- writes entity
    // +0x124, the guard arc FUN_00216140 tests an incoming hit against.
    std::uint16_t stateGuard117(const StateContext &context, std::uint16_t charge)
    {
      auto &entity = *context.entity;
      BattleParty &party = *context.party;
      const std::int32_t guard = party.entitySlotAt(kDAT_0031dabc_guardEntity + context.member * 4);

      if ((entity.state60 & 0x4000) != 0)
      {
        entity.state60 = static_cast<std::uint16_t>(entity.state60 & 0xBFFF);
        FUN_00248e98_set_animation_if_changed(entity, 0x3C);
        if (guard != kNoEntity)
        {
          auto &shield = context.environment->pool->slot(static_cast<std::size_t>(guard));
          FUN_00248ee0_set_animation(shield, 1);
          shield.spawnParam94 = static_cast<std::uint8_t>(context.entitySlot);
          shield.halfword04 = static_cast<std::uint16_t>(shield.halfword04 | 0x11);
        }
        if (context.environment->FUN_00267d38_play_at_entity)
        {
          context.environment->FUN_00267d38_play_at_entity(0xE7, context.entitySlot);
        }
      }
      if (entity.animationA0 != 0x3C)
      {
        FUN_00248e98_set_animation_if_changed(entity, 0x3C);
      }
      // The action byte is forced back to 0x90 every frame, which is what stops
      // anything else claiming the character while the shield is up.
      if (party.tables().read<std::uint8_t>(context.control + control::kCurrentAction0f) != 0x90)
      {
        setAction(context, 0x90);
      }
      // uGpffff883c. The guard arc: a hit arriving inside this half-angle of
      // the character's facing is negated by FUN_00216140.
      constexpr float kGuardArc = 1.0471976f; // 60 degrees
      entity.guardArc124 = kGuardArc;
      if ((entity.flags06 & 1) != 0)
      {
        entity.flags06 = static_cast<std::uint16_t>(entity.flags06 | 0x10);
      }
      // The damage half of this handler -- the halved hit, the 0x1C7 spark and
      // the stagger threshold -- needs FUN_00216140 to be routing damage at the
      // player, which nothing in this slice does.
      return charge;
    }

    // The class-1 table at 0x0031DD60, states 100..123. A null entry is a state
    // whose handler this slice does not port; --battle-report names any that a
    // run actually reached.
    constexpr std::array<StateHandler, 24> kClass1States = {
        nullptr,               // 100 -- the original's own null entry
        stateNothing,          // 101 LAB_0024a538
        nullptr,               // 102 FUN_0024a540, the damage/death handler
        stateNothing,          // 103 LAB_0024a868
        stateNothing,          // 104 LAB_0024a868
        stateFireAttack105,    // 105 FUN_0024ac88
        stateApproach106,      // 106 FUN_0024b410
        stateChargeAttack107,  // 107 FUN_0024b7d0
        nullptr,               // 108 FUN_0024a870, the walk back to the mark
        stateReleaseSpell109,  // 109 FUN_0024bae0
        stateReleaseSpell109,  // 110 FUN_0024c4e0, which tail-calls it
        stateChargeSpellA111,  // 111 FUN_0024c058
        stateReleaseSpellA112, // 112 FUN_0024c3e0
        stateChargeSpellB113,  // 113 FUN_0024c538
        stateReleaseSpellB114, // 114 FUN_0024c910
        nullptr,               // 115 FUN_0024bd30
        stateEndAction116,     // 116 LAB_0024bd08
        stateGuard117,         // 117 FUN_0024cba0
        stateEndAction118,     // 118 LAB_0024cef8
        stateNothing,          // 119 LAB_0024cf18
        stateIdle120,          // 120 FUN_0024cf20
        stateIdle120,          // 121 FUN_0024d128, which tail-calls it
        stateIdle120,          // 122 FUN_0024cf20
        nullptr,               // 123 -- the original's own null entry
    };
  } // namespace

  bool class1StateIsPorted(std::uint16_t state)
  {
    const std::uint32_t index = static_cast<std::uint32_t>(state & 0xBFFF) - 100u;
    return index < kClass1States.size() && kClass1States[index] != nullptr;
  }

  std::uint32_t FUN_0024a360_take_pending_action(const BattleUpdateEnvironment &environment,
                                                 std::size_t entitySlot)
  {
    BattleParty &party = *environment.party;
    BattleTables &tables = party.tables();
    auto &entity = environment.pool->slot(entitySlot);
    const std::uint32_t member = static_cast<std::uint32_t>(entity.byte95) - 1u;
    if (entity.byte95 == 0 || member >= kControlBlockCount)
    {
      return 0;
    }
    const std::uint32_t control = BattleTables::controlBlock(member);

    const auto pending = [&] { return tables.read<std::uint8_t>(control + control::kPendingAction0e); };
    const auto setPending = [&](std::uint8_t value) {
      tables.write<std::uint8_t>(control + control::kPendingAction0e, value);
    };
    const auto setAction = [&](std::uint8_t value) {
      tables.write<std::uint8_t>(control + control::kCurrentAction0f, value);
    };

    entity.freezeTimerBd = 0;

    // :15-19. A pending 6 while the character is not already in it means "the
    // battle module wants the idle back", which is state 120 with the restart
    // bit.
    if (pending() == kActionIdle06)
    {
      entity.state60 = 0x4078;
      setAction(kActionIdle06);
      setPending(0);
    }
    if (pending() == 0x82)
    {
      setPending(0);
    }
    // :23-28. Actions 10 and 11 pass straight through and re-select whatever
    // animation the entity already has.
    if (static_cast<std::uint8_t>(pending() - 10) < 2)
    {
      setAction(pending());
      setPending(0);
      entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEF);
      FUN_00248e98_set_animation_if_changed(entity, entity.animationA0);
    }
    if (pending() == 1)
    {
      setPending(0);
    }

    if (entity.pendingDamageBe == 0)
    {
      if (static_cast<std::int8_t>(tables.read<std::uint8_t>(control + control::kCurrentAction0f)) ==
          static_cast<std::int8_t>(kActionRecover87))
      {
        setPending(0);
        return 2;
      }
      if (tables.read<std::uint8_t>(control + control::kCurrentAction0f) == kActionStagger83)
      {
        setPending(0);
        return 3;
      }

      // :47-51. **The translation.** A pending byte in 0x84..0x92 becomes
      // `state = byte + 0x3FE5`, i.e. states 105..119 with the 0x4000 restart
      // bit set, and the byte moves from pending to current.
      const std::uint8_t request = pending();
      if (static_cast<std::uint8_t>(request + 0x7C) < 0x0F)
      {
        entity.state60 = static_cast<std::uint16_t>(request + 0x3FE5);
        setAction(request);
        setPending(0);
      }
      // :54-58. 0x94 is the summon, state 121.
      if (pending() == kActionSummon94)
      {
        entity.state60 = 0x4079;
        setAction(0x94);
        setPending(0);
      }
      return 0;
    }

    if (static_cast<std::int16_t>(entity.pendingDamageBe) < 1)
    {
      setPending(0);
    }
    else
    {
      // Taking a hit overrides everything: action 0x83, state 102.
      if ((entity.halfword04 & 8) != 0)
      {
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFF7);
      }
      setAction(kActionStagger83);
      entity.state60 = 0x4066;
    }
    return 1;
  }

  void FUN_00249610_battle_character_update(const BattleUpdateEnvironment &environment,
                                            std::size_t entitySlot)
  {
    if (environment.party == nullptr || environment.pool == nullptr)
    {
      return;
    }
    BattleParty &party = *environment.party;
    BattleTables &tables = party.tables();
    auto &entity = environment.pool->slot(entitySlot);

    if (entity.byte95 == 0 || entity.byte95 > kControlBlockCount)
    {
      return;
    }
    const std::uint32_t member = static_cast<std::uint32_t>(entity.byte95) - 1u;
    const std::uint32_t control = BattleTables::controlBlock(member);
    const std::uint32_t recordBase = BattleTables::partyRecord(member);
    const std::int16_t characterClass = tables.read<std::int16_t>(recordBase + record::kClass00);

    // :33-41. A class-0x16 member is not a fighter and skips the floor below;
    // everything else gets its state floored at 101, which is what stops a
    // freshly built member falling out of the table.
    if (characterClass != 0x16)
    {
      if (static_cast<std::int16_t>(entity.state60 & 0xBFFF) < 100)
      {
        entity.state60 = 0x65;
      }
    }

    // :42-44. Bit 0x4000 of +0x04 is "this entity is not participating".
    if ((entity.halfword04 & 0x4000) != 0)
    {
      return;
    }

    // :84-101. Death, and the lead's own end-of-battle path.
    if (static_cast<std::int32_t>(entity.staggerTimer12a) -
            static_cast<std::int32_t>(entity.pendingDamageBe) <
        1)
    {
      if (entitySlot == 0)
      {
        entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFF7);
        return;
      }
    }

    // :110-135. The per-frame reset, then FUN_0024a360.
    entity.freezeTimerBd = 0;
    const std::uint8_t previousAction =
        tables.read<std::uint8_t>(control + control::kCurrentAction0f);
    if (previousAction == 0x0B)
    {
      // Action 0x0B is a hard park: nothing runs, not even the state handler.
      return;
    }
    tables.write<std::uint32_t>(control + control::kFlags38,
                                tables.read<std::uint32_t>(control + control::kFlags38) & 0xFFFFFFFEu);
    FUN_0024a360_take_pending_action(environment, entitySlot);

    // :139-144. Two gates on running the state at all: a state of zero, and
    // DAT_00354fc2 bits 0/1, which the battle setup raises.
    if ((entity.state60 & 0xBFFF) == 0)
    {
      return;
    }
    if ((party.DAT_00354fc2() & 3) == 0)
    {
      return;
    }

    // :145-165. The target, and the no-target branch. FUN_002493b8 answers -1
    // for every member while the enemy table is empty, so `lVar11 < 3` is taken
    // and the entire face-the-target block below it is skipped -- the mode this
    // slice runs in.
    const std::int32_t target = party.FUN_002493b8_target(member);
    if (target >= 3)
    {
      // The turn-toward-target block. Unreachable with no enemies; when the
      // enemy side lands it goes here, driving entity +0x5C through
      // FUN_0023a320 toward FUN_00305408 of the target's offset.
      party.recordTargetFacingReached();
    }

    // :311-349. The dispatch. Only class 1 has a table here.
    const std::uint16_t state = entity.state60;
    const std::uint32_t index = static_cast<std::uint32_t>(state & 0xBFFF) - 100u;
    if (characterClass != 1)
    {
      if (environment.trace != nullptr)
      {
        environment.trace->recordState(state, false);
      }
      return;
    }
    if (index >= kClass1States.size() || kClass1States[index] == nullptr)
    {
      if (environment.trace != nullptr)
      {
        environment.trace->recordState(state, false);
      }
      return;
    }
    if (environment.trace != nullptr)
    {
      environment.trace->recordState(state, true);
    }

    const StateContext context{&environment, &party, &entity, entitySlot, member, control};
    // The handler takes +0x62 and its return goes straight back there. That
    // round trip is the charge value travelling between frames.
    entity.fadeRamp62 = kClass1States[index](context, entity.fadeRamp62);

    // :362-364. A non-zero +0x62 raises bit 0 of the control block's flag word,
    // which is what FUN_002462c8 tests before it will accept a new press.
    if (entity.fadeRamp62 != 0)
    {
      tables.write<std::uint32_t>(control + control::kFlags38,
                                  tables.read<std::uint32_t>(control + control::kFlags38) | 1u);
    }

    // :366-373. The pre-start lock: while DAT_00354ecc is set an idle member is
    // forced into state 122, which is the battle opener holding it still.
    if (party.DAT_00354ecc() != 0 &&
        tables.read<std::uint8_t>(control + control::kCurrentAction0f) == kActionIdle06)
    {
      entity.state60 = ((entity.state60 & 0x4000) != 0) ? 0x407A : 0x7A;
      tables.write<std::uint8_t>(control + control::kCurrentAction0f, 0x96);
    }
  }

} // namespace orphen::ported::battle
