#include "ported/battle/battle_command_input.h"

namespace orphen::ported::battle
{
  namespace
  {
    // The action bytes, by the signed chars Ghidra prints them as. Named once
    // so the branches below read as the state machine they are.
    constexpr std::uint8_t kActionBlocked0b = 0x0B;  // '\v'
    constexpr std::uint8_t kActionIdle06 = 0x06;     // the neutral stance
    constexpr std::uint8_t kActionStagger83 = 0x83;  // -0x7D
    constexpr std::uint8_t kActionDead87 = 0x87;     // -0x79
    constexpr std::uint8_t kActionFire84 = 0x84;     // -0x7C
    constexpr std::uint8_t kActionFire85 = 0x85;     // -0x7B
    constexpr std::uint8_t kActionCharge86 = 0x86;   // -0x7A
    constexpr std::uint8_t kActionSpellA8a = 0x8A;   // -0x76
    constexpr std::uint8_t kActionSpellA8b = 0x8B;   // -0x75
    constexpr std::uint8_t kActionSpellB8c = 0x8C;   // -0x74
    constexpr std::uint8_t kActionSpellB8d = 0x8D;   // -0x73
    constexpr std::uint8_t kActionSpellC8e = 0x8E;   // -0x72
    constexpr std::uint8_t kActionSpellC8f = 0x8F;   // -0x71
    constexpr std::uint8_t kActionGuard90 = 0x90;    // -0x70
    constexpr std::uint8_t kActionGuardEnd91 = 0x91; // -0x6F

    // The `do { if (mask & DAT_0031d168[i] & pressed) ... } while (i < 3)` loop
    // that appears four times, once per press arm. It records which of the
    // three assignable buttons actually fired, which is the index every
    // per-slot cooldown is then keyed by.
    int slotForButton(std::uint32_t latched, std::uint32_t pressed)
    {
      for (int index = 0; index < 3; ++index)
      {
        if ((latched & kDAT_0031d168_slotButtons[index] & pressed) != 0)
        {
          return index;
        }
      }
      return -1;
    }
  } // namespace

  std::int32_t FUN_002462c8_battle_command_input(const CommandInputEnvironment &environment)
  {
    if (environment.party == nullptr)
    {
      return 0;
    }
    BattleParty &party = *environment.party;
    BattleTables &tables = party.tables();

    // :27. DAT_00354f80 = FUN_00248e58(DAT_00354f80), the target-cycle repeat
    // timer. Stepped even on the frames the function returns early below.
    party.stepTargetTimers(environment.frameTicks);

    const std::int16_t playerSlot = party.DAT_00354ebe_playerSlot();
    const std::int32_t member = playerSlot - 1;
    if (member < 0 || member >= static_cast<std::int32_t>(kControlBlockCount))
    {
      return 0;
    }
    const std::uint32_t memberU = static_cast<std::uint32_t>(member);
    const std::uint32_t control = BattleTables::controlBlock(memberU);
    const std::uint32_t masks = BattleTables::buttonMask(memberU);

    const auto pending = [&] { return tables.read<std::uint8_t>(control + control::kPendingAction0e); };
    const auto setPending = [&](std::uint8_t value) {
      tables.write<std::uint8_t>(control + control::kPendingAction0e, value);
    };
    const auto action = [&] { return tables.read<std::uint8_t>(control + control::kCurrentAction0f); };
    const auto maskWord = [&](std::uint32_t field) {
      return tables.read<std::uint32_t>(masks + field);
    };
    const auto setMaskWord = [&](std::uint32_t field, std::uint32_t value) {
      tables.write<std::uint32_t>(masks + field, value);
    };
    const auto controlFlags = [&] { return tables.read<std::uint32_t>(control + control::kFlags38); };
    const auto memberFlags = [&] {
      return tables.read<std::uint32_t>(kDAT_0031da6c_memberFlags + memberU * 4);
    };

    // :29-48. Six early returns, in the original's order. The first matters
    // most: 0x0B on the *pending* byte is how the battle module parks the
    // player while something else has control.
    if (pending() == kActionBlocked0b)
    {
      return -0xC8;
    }
    if (action() == kActionStagger83)
    {
      // A stagger arms the guard timer so the shield cannot be mashed out of a
      // hit. FUN_00248e48(5) into DAT_0031dd10.
      tables.write<std::int16_t>(BattleTables::cooldown(memberU, kCooldownGuardIndex),
                                 FUN_00248e48_arm_timer(5));
      return -0xC9;
    }
    if (action() == kActionDead87)
    {
      return -0xCA;
    }
    if (party.DAT_00354ecc() != 0)
    {
      // The pre-start lock. FUN_00249610 forces state 122 while this is set,
      // which is the battle opener holding the player still.
      return -0xCB;
    }
    if ((controlFlags() & 8) != 0)
    {
      return -0xCC;
    }
    if ((memberFlags() & 0x400) != 0)
    {
      return -0xCD;
    }

    std::uint32_t pressed = environment.DAT_003555f6_pressedPad;
    std::uint32_t held = environment.DAT_003555f4_heldPad;

    // :51-63. Confusion, bit 0x1000 of the member's flag word: a face-button
    // press becomes a random one of the three spell buttons, and a button
    // merely held counts as nothing at all.
    if ((memberFlags() & 0x1000) != 0)
    {
      if ((environment.DAT_003555f6_pressedPad & 0x70) == 0)
      {
        if ((environment.DAT_003555f4_heldPad & 0x70) != 0)
        {
          held = 0;
          pressed = 0;
        }
      }
      else
      {
        held = 0;
        const std::uint32_t roll =
            environment.FUN_00216868_random ? environment.FUN_00216868_random() : 0;
        pressed = kDAT_0031d168_slotButtons[roll % 3];
      }
    }

    // :64-72. Bit 2 of the control block's flag word swallows one frame of
    // input and turns itself into bit 3, so the frame a battle starts on does
    // not act on whatever happened to be held already.
    if ((controlFlags() & 4) == 0)
    {
      party.FUN_0023f620_count(8, 1);
    }
    else
    {
      pressed = 0;
      held = 0;
      tables.write<std::uint32_t>(control + control::kFlags38,
                                  (controlFlags() & 0xFFFFFFFBu) | 8u);
    }

    // :73-148. Target cycling, skipped while the character is mid-action.
    // Reproduced as its gate only: the enemy table (DAT_00354eb4 /
    // DAT_00354eba) is empty in this slice, so FUN_002493b8 answers -1 for
    // every member and FUN_002476c0 has nothing to cycle through.
    const bool midAction = static_cast<std::uint8_t>(action() + 0x7C) < 0x0F ||
                           (memberFlags() & 0x20) != 0;
    if (!midAction)
    {
      party.recordTargetCycleReached();
    }

    // ------------------------------------------------------------ LAB_00246770
    //
    // The action machine. Every arm has the same shape: if the character is
    // already in one half of a pair, test the *held* mask latched at the press
    // and emit the release action when it clears; otherwise test the trigger
    // mask against the newly pressed word, emit the press action and latch the
    // button that fired. That latch is what makes charge-and-release work.

    // :152. A pending byte not yet spent wins over anything new.
    if (pending() != 0)
    {
      return -1;
    }

    // :155-162. Any face button cancels the target-cycle hold.
    if ((pressed & 0xF0) != 0)
    {
      party.clearTargetTimers();
    }

    // :163-219. The kind-0 pair. Press charges (0x86), release fires (0x85, or
    // 0x84 when the member's flag word asks for the alternate ending).
    if (action() != kActionSpellA8a)
    {
      const std::int32_t attackEntity = party.entitySlotAt(kDAT_0031da9c_attackEntity + memberU * 4);
      if (attackEntity != kNoEntity)
      {
        if (action() == kActionFire84)
        {
          if ((pressed & maskWord(mask::kHeldAttack00)) != 0)
          {
            if ((controlFlags() & 1) != 0)
            {
              return 0x84;
            }
            setPending(kActionFire84);
          }
          return 0x84;
        }
        if (action() == kActionFire85)
        {
          if ((pressed & maskWord(mask::kHeldAttack00)) == 0)
          {
            return 0x84;
          }
          setPending(kActionFire84);
          return 0x84;
        }
        if (action() == kActionCharge86)
        {
          // Still held: keep charging. This one test is the whole of "hold to
          // charge" -- the state handler behind 0x86 accumulates into +0x62 for
          // as long as the action stays put.
          if ((held & maskWord(mask::kHeldAttack00)) != 0)
          {
            return 0x84;
          }
          setPending(kActionFire85);
          if ((tables.read<std::uint32_t>(kDAT_0031da0c_memberFlags2 + memberU * 4) & 3) == 0)
          {
            return 0x84;
          }
          setPending(kActionFire84);
          return 0x84;
        }

        const std::uint32_t fired = pressed & maskWord(mask::kTriggerAttack04);
        if (fired != 0)
        {
          setPending(kActionCharge86);
          setMaskWord(mask::kHeldAttack00, fired);
          if (environment.pool != nullptr)
          {
            environment.pool->slot(static_cast<std::size_t>(attackEntity)).animationA0 = 2;
          }
          const int slot = slotForButton(fired, pressed);
          if (slot >= 0)
          {
            party.setSelectedSlot(playerSlot, static_cast<std::uint8_t>(slot));
          }
          // FUN_00206a90 / FUN_00206ce0 duck the music while a spell charges.
          // Not wired to the sound engine in this slice.
          party.FUN_0023f620_count(7, 1);
          return 0x84;
        }
      }
    }

    // :222-229. All five per-member timers step here rather than in
    // FUN_0023fd30, so they only run on the frames the player can act.
    for (std::uint32_t index = 0; index < 5; ++index)
    {
      const std::uint32_t at = BattleTables::cooldown(memberU, index);
      const std::uint16_t value = tables.read<std::uint16_t>(at);
      if (value != 0)
      {
        tables.write<std::uint16_t>(at, FUN_00248e58_step_timer(value, environment.frameTicks));
      }
    }

    // :232-238. The kind > 0 pair -- Triangle with the shipped loadout.
    if (action() == kActionSpellA8a)
    {
      if ((held & maskWord(mask::kHeldSpellA08)) == 0)
      {
        setPending(kActionSpellA8b);
      }
      return 0x88;
    }
    // :239-261. Its press arm.
    if ((controlFlags() & 1) == 0)
    {
      const std::uint32_t fired = pressed & maskWord(mask::kTriggerSpellA0c);
      if (fired != 0 && party.entitySlotAt(kDAT_0031daac_shieldEntity + memberU * 4) != kNoEntity)
      {
        setMaskWord(mask::kHeldSpellA08, fired);
        setPending(kActionSpellA8a);
        const int slot = slotForButton(maskWord(mask::kHeldSpellA08), pressed);
        if (slot >= 0)
        {
          setMaskWord(mask::kHeldSpellA08, kDAT_0031d168_slotButtons[slot]);
          party.setSelectedSlot(playerSlot, static_cast<std::uint8_t>(slot));
        }
        return 0x88;
      }
    }

    // :262. FUN_002494e0 -- how long the current action has been running,
    // capped at 0x780. Evaluated before the 0x8C test because both halves of
    // that arm read it.
    const std::int64_t elapsed = party.FUN_002494e0_elapsed(environment.pool, memberU, 0x780);

    if (action() == kActionSpellB8c)
    {
      // :265-267. Past the ceiling the latched mask is cleared outright, which
      // forces the release even while the button is still down: the charge has
      // a maximum and holding past it does nothing.
      if (elapsed > 5)
      {
        setMaskWord(mask::kHeldSpellB10, 0);
      }
      if ((held & maskWord(mask::kHeldSpellB10)) == 0)
      {
        setPending(kActionSpellB8d);
        tables.write<std::int16_t>(
            BattleTables::cooldown(memberU, party.selectedSlot(playerSlot)),
            FUN_00248e48_arm_timer(0x96));
      }
      // :275. Unconditional, release or not: using this pair holds the shield
      // off for 240 frames' worth of ticks.
      tables.write<std::int16_t>(BattleTables::cooldown(memberU, kCooldownGuardIndex),
                                 FUN_00248e48_arm_timer(0xF0));
      return 0x89;
    }

    // :279. The 0x8C press arm's three conditions. When any of them fails the
    // original falls into the 0x8E / 0x90 block instead, which is why those
    // two pairs sit inside this else.
    const bool canPressSpellB =
        (controlFlags() & 1) == 0 && (pressed & maskWord(mask::kTriggerSpellB14)) != 0 &&
        party.entitySlotAt(kDAT_0031daac_shieldEntity + memberU * 4) != kNoEntity;

    if (!canPressSpellB)
    {
      // :283-289. The kind == -1 pair's release.
      if (action() == kActionSpellC8e)
      {
        if ((held & maskWord(mask::kHeldSpellC18)) != 0 &&
            party.effectAnimation(environment.pool, kDAT_0031daac_shieldEntity, memberU) != 2)
        {
          return 0x90;
        }
        setPending(kActionSpellC8f);
        return 0x90;
      }

      // :291-313. Its press arm, only out of the neutral stance.
      if (action() == kActionIdle06)
      {
        const std::uint32_t fired = pressed & maskWord(mask::kTriggerSpellC1c);
        if (fired != 0)
        {
          setMaskWord(mask::kHeldSpellC18, fired);
          setPending(kActionSpellC8e);
          const int slot = slotForButton(maskWord(mask::kHeldSpellC18), pressed);
          if (slot >= 0)
          {
            setMaskWord(mask::kHeldSpellC18, kDAT_0031d168_slotButtons[slot]);
            party.setSelectedSlot(playerSlot, static_cast<std::uint8_t>(slot));
          }
          return 0x90;
        }
      }

      // :316-341. Square.
      if (action() != kActionGuard90)
      {
        const std::uint32_t guardTimerAt = BattleTables::cooldown(memberU, kCooldownGuardIndex);
        // :318-321. While the guard timer is running a *held* Square counts;
        // once it has expired only a fresh press does. That is what lets the
        // shield be re-raised by simply keeping the button down.
        const std::uint32_t accepted =
            (tables.read<std::int16_t>(guardTimerAt) == 0) ? pressed : (pressed | held);
        if ((memberFlags() & 2) != 0)
        {
          return 1;
        }
        if (action() != kActionIdle06)
        {
          return 1;
        }
        const std::uint32_t guardTrigger = maskWord(mask::kTriggerGuard24);
        if ((accepted & guardTrigger) == 0)
        {
          return 1;
        }
        setPending(kActionGuard90);
        setMaskWord(mask::kHeldGuard20, accepted & guardTrigger);
        tables.write<std::int16_t>(guardTimerAt, 0);
        return 0x90;
      }

      // :337-341. The shield ends when Square is let go, or when the guard
      // entity's own animation reaches 2 -- which is the duration wearing off
      // while the button is still held.
      if ((held & maskWord(mask::kHeldGuard20)) != 0 &&
          party.effectAnimation(environment.pool, kDAT_0031dabc_guardEntity, memberU) != 2)
      {
        return 0x90;
      }
      setPending(kActionGuardEnd91);
      return 0x90;
    }

    // :345-365. The kind < 0 press, once its three conditions hold. The scan
    // both picks the slot and enforces its cooldown: a slot still counting down
    // refuses the press and the whole frame is dropped.
    {
      const std::uint32_t trigger = maskWord(mask::kTriggerSpellB14);
      int index = 0;
      std::uint32_t button = 0;
      for (; index < 3; ++index)
      {
        button = kDAT_0031d168_slotButtons[index];
        if ((trigger & button & pressed) != 0)
        {
          break;
        }
      }
      if (index >= 3)
      {
        return 0x89;
      }
      if (tables.read<std::int16_t>(BattleTables::cooldown(memberU, static_cast<std::uint32_t>(index))) != 0)
      {
        return -0x6F;
      }
      setPending(kActionSpellB8c);
      setMaskWord(mask::kHeldSpellB10, button);
      party.FUN_00249108_clear_turn_flags(memberU);
      party.setSelectedSlot(playerSlot, static_cast<std::uint8_t>(index));
    }
    return 0x89;
  }

} // namespace orphen::ported::battle
