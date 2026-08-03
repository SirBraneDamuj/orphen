#include "ported/entity/actor_frame_update.h"

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_00239ce0 starts at &DAT_0058c260, which is the pool base plus
    // 2 * 0x1D8. Slot 0 is the lead player, updated by FUN_00251ed8 on its own
    // path; slot 1 is skipped with it.
    constexpr std::size_t kFirstTickedSlot = 2;

    // The guard bits, at the entity offsets FUN_00239ce0 tests them at.
    constexpr std::uint16_t kHidden02 = 0x0800;    // +0x02
    constexpr std::uint16_t kSuspended04 = 0x4000; // +0x04
    constexpr std::uint16_t kFading04 = 0x0800;    // +0x04
  } // namespace

  bool FUN_0023a068_freeze_gate(OriginalEntity &entity, std::uint32_t frameTicks)
  {
    const std::int8_t remaining = entity.freezeTimerBd;
    const bool frozen = remaining != 0;
    if (frozen)
    {
      entity.freezeTimerBd = static_cast<std::int8_t>(remaining - 1);
      entity.stateResetA4 = static_cast<std::uint16_t>(entity.stateResetA4 + frameTicks);
    }
    // The last frozen frame still runs the behavior.
    return frozen && remaining != 1;
  }

  void FUN_00225bc8_set_animation(OriginalEntity &entity, std::uint16_t animation)
  {
    entity.animationA0 = animation;
    entity.stateResetA4 = 999;
    entity.previousSubstateA2 = 0xFFFF;
    entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFF38u);
    entity.substateFrameA8 = 0;
  }

  void FUN_00225bf0_set_state_and_animation(OriginalEntity &entity,
                                            std::uint16_t state,
                                            std::uint16_t animation)
  {
    entity.state60 = state;
    FUN_00225bc8_set_animation(entity, animation);
  }

  void FUN_0023a568_fade(EntityPool &pool, std::size_t slot, std::uint32_t frameTicks)
  {
    OriginalEntity &entity = pool.slot(slot);
    constexpr std::uint32_t kFullyFadedIn = 0x00FFFFFFu;

    if (entity.fadeColor138 != kFullyFadedIn)
    {
      // Fading in: the ramp climbs at four ticks per frame.
      const std::int32_t ramp =
          static_cast<std::int16_t>(entity.fadeRamp62 + static_cast<std::uint16_t>(frameTicks * 4u));
      entity.fadeRamp62 = static_cast<std::uint16_t>(ramp);
      if (ramp < 0x2000)
      {
        // `iVar2 = iVar3 + 0x1f; if (-1 < iVar3) iVar2 = iVar3; iVar2 >> 5` is
        // the compiler's signed divide-by-32, which truncates toward zero rather
        // than flooring. The bound above keeps the result under 0x100, so the
        // three shifted copies do not overlap.
        const std::uint32_t level = static_cast<std::uint32_t>(ramp / 32);
        entity.fadeColor138 = (level << 16) | (level << 8) | level;
      }
      else
      {
        entity.fadeColor138 = kFullyFadedIn;
        entity.fadeRamp62 = 0x0FE0;
      }
      return;
    }

    // Fading out: half the rate, and the slot is released at the bottom.
    const std::int32_t ramp =
        static_cast<std::int16_t>(entity.fadeRamp62 - static_cast<std::uint16_t>(frameTicks * 2u));
    entity.fadeRamp62 = static_cast<std::uint16_t>(ramp);
    if (ramp > 0x80)
    {
      entity.fadeLevel134 = static_cast<std::uint8_t>(ramp / 32);
      return;
    }

    // FUN_00265ec0. The original also runs the script's word-4 teardown entry
    // when the entity's +0x02 has bit 0x8000; the port does not drive that entry
    // yet, so this is a plain release.
    pool.releaseSlot(slot);
  }

  void FUN_002d1ea8_treasure_chest(OriginalEntity &entity, const ActorEnvironment &environment)
  {
    const auto flagSet = [&environment](std::uint32_t flagId) {
      return environment.eventFlag ? environment.eventFlag(flagId) : false;
    };

    // First tick: pick the closed or the already-opened pose from the flag.
    // FUN_00266240 leaves +0x94 at 0 for group-3 spawns, which is what gets us
    // here exactly once.
    if (entity.spawnParam94 == 0)
    {
      entity.animationA0 = flagSet(entity.eventFlagId198) ? 6 : 4;
      entity.spawnParam94 = 1;
      return;
    }

    if (entity.animationA0 == 4)
    {
      // Closed, watching the flag. Nothing here sets it -- the chest only
      // observes. The interaction path (script header word 3) is what opens it,
      // and the port does not drive that entry, so in practice a chest stays
      // closed. That is faithful, not a stub.
      if (flagSet(entity.eventFlagId198))
      {
        FUN_00225bc8_set_animation(entity, 5);
      }
      return;
    }

    if (entity.animationA0 != 5)
    {
      return; // 6 is terminal
    }

    // Opening. The original's contents effect is gated on cGpffffb6e1 == 0x23, a
    // global mode the port never enters, and its body builds a GS packet at
    // 0x70000000. The port has no primitive submission path, so that branch is
    // deliberately absent; with it absent +0x19E is never set and the timer tail
    // below never runs either. Both are kept so the shape survives.
    constexpr std::uint16_t kEffectDuration = 0x1680; // 0x120 frames at 0x20 ticks
    if (entity.effectActive19e != 0)
    {
      if (entity.effectTimer19c < kEffectDuration)
      {
        entity.effectTimer19c = static_cast<std::uint16_t>(entity.effectTimer19c + environment.frameTicks);
      }
      else
      {
        entity.effectActive19e = 0;
      }
    }
  }

  // FUN_0025ab68 (party members, types 0x03..0x07): freeze gate, then
  // PTR_LAB_0031e1d0[+0x60].
  //
  // Entities spawn in state 0, and state 0 -- like state 6 -- is 0x0025ABB8,
  // which is `jr ra; nop` in the executable. So an idle party member genuinely
  // does nothing every frame, and the room's characters keep the facing the
  // scene's init gave them through object register 13. That is the whole of
  // their visible behavior until something moves them out of state 0.
  void FUN_0025ab68_party_member(OriginalEntity &entity,
                                 const ActorEnvironment &environment,
                                 ActorTrace &trace)
  {
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_LAB_0031e1d0_partyStates, kPartyStateCount, entity.state60);
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, handler == kLAB_0025abb8_noOp);
  }

  // FUN_002cd210: the type 0x62 enemy's state 0, which is its one-shot init.
  void FUN_002cd210_enemy62_init(OriginalEntity &entity, EntityPool &pool)
  {
    // Straight to the chase state.
    FUN_00225bf0_set_state_and_animation(entity, 3, 2);

    entity.enemyFlags1c8 = 1;
    entity.attackChance1c0 = 1000;
    entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 1u);

    // +0x1A0 is the target, resolved from the pool index at +0x19C. The
    // original stores a pointer; the port stores the slot.
    entity.targetSlot1a0 = entity.targetIndex19c;

    // FUN_00267da0(+0x1B4, +0x20, 0xC): the home position, three floats copied
    // out of the live position. State 3 falls back to it when it has no target.
    entity.homeX1b4 = entity.positionX20;
    entity.homeZ1b8 = entity.positionZ24;
    entity.homeY1bc = entity.positionY28;

    // The original also spawns +0x198 companion clones here, each a copy of this
    // type placed at the same spot with its own home position and a back-pointer
    // at +0x1A0. s01_e024's single enemy carries a count of 0, so that loop does
    // not run; it is not ported.
    (void)pool;
  }

  // FUN_002cd0a0 (type 0x62): freeze gate, the +0xBE hit reaction, the +0x1C2
  // countdown, then PTR_FUN_00326660[+0x60].
  void FUN_002cd0a0_enemy62(OriginalEntity &entity,
                            EntityPool &pool,
                            const ActorEnvironment &environment,
                            ActorTrace &trace)
  {
    if (FUN_0023a068_freeze_gate(entity, environment.frameTicks))
    {
      return;
    }

    // +0xBE is damage taken since the last tick. Draining it to zero forces
    // state 6 and seeds the stagger. The port has no damage source, so this
    // never fires -- but it is the wrapper's first act, so it is kept.
    if (entity.pendingDamageBe != 0)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity.staggerTimer12a) - static_cast<std::int32_t>(entity.pendingDamageBe);
      entity.staggerTimer12a = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(entity.staggerTimer12a) < 1)
      {
        entity.staggerTimer12a = 0;
        FUN_00225bf0_set_state_and_animation(entity, 6, 4);
        entity.halfword04 = static_cast<std::uint16_t>((entity.halfword04 & 0xFFF7u) | 0x10u);
        entity.collisionFlags0c &= ~1u;
        entity.fadeLevel134 = 0x7C;
      }
      entity.fadeColor138 = 0xC0;
      entity.hitFlash1c2 = 0x1E0;
      entity.pendingDamageBe = 0;
    }

    // +0x1C2 counts the hit flash down, clearing the tint when it expires.
    if (entity.hitFlash1c2 != 0)
    {
      const std::int32_t remaining =
          static_cast<std::int32_t>(entity.hitFlash1c2) - static_cast<std::int32_t>(environment.frameTicks);
      entity.hitFlash1c2 = static_cast<std::uint16_t>(remaining);
      if (static_cast<std::int16_t>(entity.hitFlash1c2) < 1)
      {
        entity.hitFlash1c2 = 0;
        entity.fadeColor138 = 0;
      }
    }

    const std::uint32_t handler = environment.dispatchTable->stateHandler(
        kPTR_FUN_00326660_enemy62States, kEnemy62StateCount, entity.state60);

    // Only state 0, the init, is ported. It hands straight to state 3, the
    // hover-and-chase state, which needs the shared non-player physics step
    // before anything it writes to +0x30/+0x34/+0x38 would move the entity.
    const bool implemented = entity.state60 == 0;
    trace.recordStateDispatch(entity.typeId00, entity.state60, handler, implemented);
    if (implemented)
    {
      FUN_002cd210_enemy62_init(entity, pool);
    }
  }

  bool actorHandlerIsImplemented(std::uint32_t handlerAddress)
  {
    switch (handlerAddress)
    {
    case kFUN_00239e78_noOp:
    case 0x002D1EA8u: // FUN_002d1ea8, type 0x3A
    case 0x0025AB68u: // FUN_0025ab68, party members
    case 0x002CD0A0u: // FUN_002cd0a0, the type 0x62 enemy
      return true;
    default:
      return false;
    }
  }

  const char *actorHandlerName(std::uint32_t handlerAddress)
  {
    switch (handlerAddress)
    {
    case kFUN_00239e78_noOp:
      return "FUN_00239e78 (no-op)";
    case 0x002D1EA8u:
      return "FUN_002d1ea8 (treasure chest)";
    case 0x0025AB68u:
      return "FUN_0025ab68 (party member)";
    case 0x002CD0A0u:
      return "FUN_002cd0a0 (enemy)";
    case kFUN_002cfe08_streamedProp:
      return "FUN_002cfe08 (map-streamed prop)";
    default:
      return nullptr;
    }
  }

  void FUN_00239ce0_update_actors(const ActorEnvironment &environment, ActorTrace &trace)
  {
    if (environment.entityPool == nullptr || environment.dispatchTable == nullptr)
    {
      return;
    }

    EntityPool &pool = *environment.entityPool;
    const ActorDispatchTable &table = *environment.dispatchTable;
    if (!table.available())
    {
      trace.noteTableUnavailable();
    }

    for (std::size_t slot = kFirstTickedSlot; slot < kEntitySlotCount; ++slot)
    {
      // The original's test is `'\0' < (char)status`, a signed compare, so
      // Allocated (0xFF, reserved but not yet initialised) is skipped along with
      // Free. Only a fully built, positive-typed entity ticks.
      if (pool.status(slot) != SlotStatus::ScriptSpawned)
      {
        continue;
      }

      OriginalEntity &entity = pool.slot(slot);
      if ((entity.descriptorFlags02 & kHidden02) != 0)
      {
        trace.recordHidden();
        continue;
      }
      if ((entity.halfword04 & kSuspended04) != 0)
      {
        trace.recordSuspended();
        continue;
      }
      if ((entity.halfword04 & kFading04) != 0)
      {
        trace.recordFading();
        FUN_0023a568_fade(pool, slot, environment.frameTicks);
        continue;
      }

      const ActorHandler handler = table.FUN_00239ce0_resolve(entity.typeId00);
      const bool implemented = handler.address != 0 && actorHandlerIsImplemented(handler.address);
      trace.recordDispatch(entity.typeId00, slot, handler, implemented);
      if (!implemented)
      {
        continue;
      }

      switch (handler.address)
      {
      case 0x002D1EA8u:
        FUN_002d1ea8_treasure_chest(entity, environment);
        break;
      case 0x0025AB68u:
        FUN_0025ab68_party_member(entity, environment, trace);
        break;
      case 0x002CD0A0u:
        FUN_002cd0a0_enemy62(entity, pool, environment, trace);
        break;
      case kFUN_00239e78_noOp:
      default:
        break;
      }
    }
  }

} // namespace orphen::ported::entity
