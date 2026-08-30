/*
 * Player Grounded Field State Machine
 * Original function: FUN_00256bb8
 * Address: 0x00256bb8
 *
 * The grounded half of the field-mode player update, reached from
 * FUN_00251ed8 (analyzed/update_main_character_entity.c) through its state
 * function pointer table. Its airborne counterpart is FUN_002534d8
 * (analyzed/update_player_airborne_state.c).
 *
 * Decides, in priority order: forced fall off a ledge, jump, special mode,
 * attack, interact, then locomotion or idle.
 *
 * Return value drives the caller:
 *   0  handled, entity stays grounded
 *   1  state changed to airborne (or the update was suppressed by a lock)
 *   2  state changed to an action state; the caller should not run further
 *
 * IMPORTANT -- pointer arithmetic. param_1 is treated as `undefined2 *`, so
 * every puVar9[N] below is at BYTE offset N*2. The mapping to the entity
 * layout documented in analyzed/process_entity_physics_and_collision.c is:
 *
 *   puVar9[0x03]  +0x006  flags
 *   puVar9[0x06]  +0x00C  collision flags; bit 0 is grounded
 *   puVar9[0x14]  +0x028  vertical position
 *   puVar9[0x28]  +0x050  ground height under the entity
 *   puVar9[0x2e]  +0x05C  facing angle
 *   puVar9[0x30]  +0x060  state: 0 idle, 1 moving, 2 airborne
 *   puVar9[0x34]  +0x068  linked entity pointer (equipped weapon)
 *   puVar9[0x50]  +0x0A0  animation id
 *   puVar9[0xcc]  +0x198  spawned effect entity pointer
 *   puVar9[0xdb]  +0x1B6  idle timer
 *   puVar9[0xdd]  +0x1BA  cutscene / lock byte
 *   (int)puVar9 + 0x1bb   +0x1BB  motion flags
 *
 * NOTE on +0x60 / +0xA0. The port had these as "state" and "substate". This
 * function shows +0xA0 is really the ANIMATION ID: the grounded path writes
 * 0x0E for run, 0x0B for walk, 0x01 for stand, and 0x17 / 0x6A for the idle
 * fidget, while FUN_00225bf0 (analyzed/set_entity_animation_state.c) writes it
 * as its third argument. The airborne values 0x0C / 0x0D / 0x10 are therefore
 * jump-rise / jump-fall / land animations, not abstract substates. +0x60 is the
 * coarse state that selects which update function runs.
 *
 * Constants (gp = 0x00359F70, values read from eeMemory.bin):
 *   fGpffff8a48  DAT_003529b8  0.37   fall threshold: height above ground that
 *                                     forces the falling animation
 *   fGpffff8a4c  DAT_003529bc  0.045  run speed per nominal frame
 *   fGpffff8a50  DAT_003529c0  0.023  walk speed per nominal frame
 *
 * Input globals:
 *   FUN_0023b890(8)  mapped action bits for the player pad
 *                      0x80 jump, 0x20 attack, 0x10 interact
 *   uGpffffb68a      newly pressed mapped actions; 0x40 gates the special mode
 *   fGpffffb678      analog stick magnitude; > 100.0 selects run over walk
 *   iGpffffb64c      elapsed frame ticks (DAT_003555bc)
 *
 * Callees:
 *   FUN_0023b890   read mapped action bits
 *   FUN_002298d0   move-set class for an entity type id (see below)
 *   FUN_00252cc0   special-mode availability test
 *   FUN_002686a0   enter the special mode
 *   FUN_00255d88   begin a state transition (arg 2 = jump)
 *   FUN_00267d38   play the associated sound
 *   FUN_00225bf0   set state + animation (analyzed/set_entity_animation_state.c)
 *   FUN_00252d88   return to idle (analyzed/reset_entity_animation_to_default.c)
 *   FUN_00257b50   action-state setup
 *   FUN_00265e28   allocate an effect entity
 *   FUN_00216078   copy a transform into the spawned effect
 *   FUN_00256ff8   locomotion bookkeeping; second arg is "is running"
 *   FUN_00256ab0   apply the movement impulse and facing
 *
 * FUN_002298d0 IS A TYPE-ID TABLE, not an item lookup. It is a bare switch:
 *
 *     type 1 -> 0    type 3 -> 1    type 4 -> 2    type 5 -> 3
 *     type 6 -> 4    type 7 -> 5    type 0x16 -> 6    anything else -> 7
 *
 * Types 1 and 3..7 are the playable cast, so the "class" selects a character's
 * move set. The lead player is type 1, class 0 -- which is why the attack
 * branch below reaches state 0x1C, the sword swing, and the jump branch's
 * `< 7` test on an equipped item's class refuses a jump while holding one of
 * the six recognised things.
 *
 * State 0x1C is analysed in analyzed/player_states/sword_attack_0x1C.c.
 *
 * UNVERIFIED / open:
 * - The +0x1BA lock byte's writers have not been traced. Value 0x1D is special:
 *   it is the one nonzero value that does NOT suppress the update.
 * - puVar9[0x50] == 0x2F is treated as a sticky animation that normal
 *   locomotion must not overwrite; its meaning is unknown.
 */

undefined4 update_player_grounded_field_state(entity *e)
{
  ulong mappedActions;
  short previousAnimation;
  short animation;
  long weaponClass;
  float speed;

  mappedActions = FUN_0023b890(8);
  previousAnimation = e->animation_A0;

  /* --- cutscene / lock gate ---------------------------------------------- */
  if (e->lock_1BA != 0)
  {
    if (e->lock_1BA > 2)
    {
      e->animation_A0 = 0;
    }
    if (e->lock_1BA != 0x1d)
    {
      return 1; /* suppressed */
    }
  }

  /* --- 1. forced fall -----------------------------------------------------
   * More than 0.37 above the ground under us: switch to the falling animation
   * and hand off to the airborne state. This is what makes walking off a ledge
   * enter the fall state rather than merely losing ground contact. */
  if (e->positionY_28 - e->groundHeight_50 > fGpffff8a48)
  {
    e->motionFlags_1BB = (e->motionFlags_1BB & 0xef) | 2;
    FUN_00225bf0(e, 2, 0x0d); /* state 2 airborne, animation 0x0D falling */
    return 1;
  }

  /* --- 2. jump -------------------------------------------------------------
   * Requires the jump action, grounded, and either no equipped item or an item
   * whose weapon class is >= 7. The original writes this as the negation of the
   * "not jumping" condition; it is inverted here for readability. */
  {
    const bool jumpHeld = (mappedActions & 0x80) != 0;
    const bool grounded = (e->collisionFlags_0C & 1) != 0;
    const bool weaponBlocksJump =
        e->linkedEntity_68 != NULL && FUN_002298d0(*e->linkedEntity_68) < 7;

    if (jumpHeld && grounded && !weaponBlocksJump)
    {
      FUN_00267d38(FUN_00255d88(e, 2), e);
      FUN_00225bf0(e, 2, 0x0c); /* state 2 airborne, animation 0x0C rising */
      return 1;
    }
  }

  /* --- 3. special mode ---------------------------------------------------- */
  if ((uGpffffb68a & 0x40) != 0 && FUN_00252cc0(e) != 0)
  {
    FUN_002686a0();
    return 1;
  }

  /* --- 4. attack ---------------------------------------------------------- */
  if ((mappedActions & 0x20) != 0)
  {
    weaponClass = FUN_002298d0(e->typeId_00);

    if (weaponClass == 0)
    {
      /* The lead player's sword swing. FUN_00225bf0 is the whole branch: the
       * blade, the sound and the exit all come out of the state handler,
       * FUN_00256130. See analyzed/player_states/sword_attack_0x1C.c. */
      FUN_00225bf0(e, 0x1c, 0x33);
      return 2;
    }

    if (weaponClass == 3 || weaponClass == 4)
    {
      int effectId;

      if (weaponClass == 3)
      {
        effectId = 0x4e;
        FUN_00225bf0(e, 0x1e, 0x33);
      }
      else
      {
        effectId = 0x50;
        FUN_00225bf0(e, 0x20, 199);
      }

      /* Spawn the swing effect and seed it from the player. */
      {
        int effect = FUN_00265e28(effectId);
        if (effect == 0)
        {
          return 2;
        }

        *(undefined2 *)(effect + 2) = 0x1000;
        if (effectId == 0x4e)
        {
          *(undefined1 *)(effect + 0x194) = 0xd7;
          *(undefined1 *)(effect + 0x134) = 3;
          *(undefined2 *)(effect + 0x62) = 0x60;
          *(undefined2 *)(effect + 0xa0) = 3;
          *(undefined4 *)(effect + 0x5c) = e->facing_5C;
        }
        else
        {
          *(undefined2 *)(effect + 0xa0) = 1;
          *(undefined1 *)(effect + 0x194) = 0xd;
        }
        *(undefined2 *)(effect + 0x192) = 0;
        *(undefined2 *)(effect + 8) = 0;
        *(undefined2 *)(effect + 300) = e->field_12C;
        *(undefined2 *)(effect + 0x60) = e->state_60;
        FUN_00216078(e->typeId_00, 0, effect + 0x198);
        e->spawnedEffect_198 = effect;
        return 2;
      }
    }

    if (weaponClass == 1 || weaponClass == 2)
    {
      FUN_00252d88(e); /* no attack for this class: drop back to idle */
      return 0;
    }
    /* any other class falls through to locomotion */
  }

  /* --- 5. interact / use -------------------------------------------------- */
  else if ((mappedActions & 0x10) != 0)
  {
    weaponClass = FUN_002298d0(e->typeId_00);

    if (weaponClass == 3)
    {
      FUN_00225bf0(e, 0x1f, 0x14);
      FUN_00257b50(e);
      return 2;
    }
    if (weaponClass == 0)
    {
      e->spawnedEffect_198 = 0;
      FUN_00225bf0(e, 0x1d, 0x14);
      FUN_00257b50(e);
      return 2;
    }
    if (weaponClass == 4)
    {
      FUN_00225bf0(e, 0x21, 0x14);
      e->field_19C = 0;
      e->spawnedEffect_198 = 0;
      e->flags_06 |= 0x80;
      return 2;
    }
    if (weaponClass == 5)
    {
      FUN_00225bf0(e, 0x23, 0x14);
      return 2;
    }
    /* any other class falls through to locomotion */
  }

  /* --- 6. locomotion or idle ---------------------------------------------- */
  e->state_60 = 0;

  /* 0x2F is a sticky animation that normal locomotion must not clobber unless
   * flags bit 0 is set. */
  if (e->animation_A0 != 0x2f || (e->flags_06 & 1) != 0)
  {
    e->animation_A0 = 1; /* stand */
  }

  if (fGpffffb678 == 0.0f)
  {
    /* Idle. Accumulate frame ticks in a 16-bit counter and fire the fidget
     * animation when it rolls past the sign bit -- 0x8000 ticks is 1024 frames,
     * about 17 seconds at 60 fps. */
    short previousTimer = e->idleTimer_1B6;
    e->idleTimer_1B6 = previousTimer + (short)iGpffffb64c;

    if ((short)(previousTimer + (short)iGpffffb64c) < 0)
    {
      short fidget;

      if (e->animation_A0 == 0x2f)
      {
        return 0;
      }

      fidget = (FUN_002298d0(e->typeId_00) == 3) ? 0x6a : 0x17;

      if (previousAnimation == fidget)
      {
        /* Already playing it: restart the timer instead of retriggering,
         * unless flags bit 0 forces a replay. */
        if ((e->flags_06 & 1) == 0)
        {
          e->animation_A0 = fidget;
        }
        else
        {
          e->idleTimer_1B6 = 0;
        }
      }
      else
      {
        e->animation_A0 = fidget;
      }
    }
  }
  else
  {
    /* Moving. Walk below a stick magnitude of 100, run above it. */
    const bool running = fGpffffb678 > 100.0f;

    e->state_60 = 1;
    e->idleTimer_1B6 = 0;

    speed = running ? fGpffff8a4c : fGpffff8a50; /* 0.045 : 0.023 */

    FUN_00256ff8(e, running);
    FUN_00256ab0((float)iGpffffb64c * speed * 0.03125f, e);

    e->animation_A0 = running ? 0x0e : 0x0b;
  }

  return 0;
}
