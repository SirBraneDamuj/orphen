/*
 * Player Sword Attack -- state 0x1C, and the blade entity it spawns
 *
 * Original functions:
 *   FUN_00256130  0x00256130  the state handler, PTR_FUN_0031e160[0]
 *   FUN_002560e8  0x002560e8  its exit gate
 *   FUN_002d21b8  0x002d21b8  type 0x42, the blade's own actor behaviour
 *   FUN_00266098  0x00266098  release an entity's dynamic light slot
 *
 * Entered from FUN_00256bb8 (analyzed/update_player_grounded_field_state.c)
 * when the attack action is asked for while grounded and FUN_002298d0 answers
 * weapon class 0 for the entity's TYPE ID -- which it does for type 1, the lead
 * player. That branch is two calls: FUN_00225bf0(entity, 0x1C, 0x33) and
 * return 2. Everything below is driven by animation 0x33's own data.
 *
 * DISPATCH. FUN_00251ed8 (analyzed/update_main_character_entity.c) uses two
 * tables:
 *   state <  0x1C   PTR_FUN_0031e0e8[state]
 *   state >= 0x1C   PTR_FUN_0031e160[state - 0x1C]
 * They are adjacent -- 0x0031E160 is 0x0031E0E8 + 0x78 -- but genuinely two
 * tables, offset by two entries; indices 0x1C and 0x1D of the first are null.
 * Read out of s01_e24.bin, PTR_FUN_0031e160 is:
 *   [0] 0x00256130  0x1C  sword swing            [1] 0x002562b0  0x1D
 *   [2] 0x002563e8  0x1E                         [3] 0x00256548  0x1F
 *   [4] 0x00256620  0x20                         [5] 0x002567c0  0x21
 *   [6] 0x002569d0  0x22                         [7] 0x002569d8  0x23
 *
 * ANIMATION FLAGS. FUN_00225c90 (the animation stepper) writes three latches
 * into entity +0x06 that this state is entirely built on:
 *   0x01  the last timeline entry finished -- the animation is over
 *   0x04  the current entry's duration ran out this frame
 *   0x08  a new entry was taken this frame
 * and stages the current keyframe's third halfword in +0xAA, which is where the
 * animation data carries its own events. Animation 0x33 on grp_0001 is nine
 * entries totalling 46 frames; entry 1 is where the blade spawns and entry 7
 * carries +0xAA bit 0x200, the "stop the blade" event.
 *
 * PORTED 2026-08-30 into port/src/ported/player/original_player_controller.cpp
 * (FUN_00256130_update_sword_attack, FUN_002560e8_end_on_animation_complete)
 * and port/src/ported/entity/actor_frame_update.cpp
 * (FUN_00256130_spawn_sword_effect, FUN_002d21b8_sword_effect,
 * FUN_00265ec0_destroy_entity). See port/README.md.
 *
 * NOT ANALYSED: FUN_002148a8, the swept hit test, and FUN_002d59c0, its
 * reaction. FUN_00216078 fills the blade's +0x198 from a per-type parameter
 * table and the hit test is its only reader.
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * FUN_002560e8 -- the only way out of state 0x1C.
 *
 * DAT_00355634 and FUN_00215e48 clear the swing's already-hit set: eight words
 * at entity +0xCC plus +0x06 bit 0x40, so the next swing can hit the same
 * target again.
 */
bool player_sword_attack_finished(entity *e)
{
  if ((e->flags_06 & 1) == 0)
  {
    return false;
  }
  DAT_00355634 = 0;
  FUN_00215e48(e); /* clear the already-hit set */
  FUN_00252d88(e); /* analyzed/reset_entity_animation_to_default.c */
  return true;
}

/* --------------------------------------------------------------------------
 * FUN_00256130 -- state 0x1C.
 *
 * Reads no input. Four points in the timeline, and nothing in between.
 */
void update_player_sword_attack(entity *e)
{
  if (player_sword_attack_finished(e))
  {
    return;
  }

  /* +0xA8 steps by two per timeline entry, so 2 is the second keyframe. */
  if (e->timelineCursor_A8 == 2)
  {
    if ((e->flags_06 & 8) != 0)
    {
      /* The frame the cursor first lands on entry 1: spawn the blade. */
      int blade = FUN_00265e28(0x42);
      if (blade == 0)
      {
        FUN_00252d88(e); /* pool full: abandon the swing rather than play it bare */
        return;
      }

      *(short *)(blade + 0xa0) = 1;                    /* animation 1, the swing */
      *(short *)(blade + 0x192) = pool_slot_of(e);     /* attached to the swinger */
      *(char *)(blade + 0x194) = FUN_0020dd78(e, 5);   /* ...at its role-5 bone */
      *(short *)(blade + 0x12c) = e->attackPower_12C;  /* what the hit test deals */
      *(float *)(blade + 0x5c) = e->facing_5C;
      *(char *)(blade + 0x134) = 4;                    /* fade in from 4/128 alpha */
      *(float *)(blade + 0x158) = DAT_00352998;        /* pi: roll the model round */
      *(short *)(blade + 0x62) = 0x1000;               /* light colour ramp start */

      /* FUN_00266050 allocates from slot 0, so the blade can become a real
       * directional light on a character rather than only a flat tint. */
      {
        char lightSlot = FUN_00266050();
        *(char *)(blade + 0x195) = lightSlot;
        if (lightSlot >= 0)
        {
          *(uint *)(&DAT_00343894 + lightSlot * 0x14) = 0x1000000; /* rgb 0, alpha 1 */
          (&DAT_00343898)[lightSlot * 5] = 2.0f;                   /* radius; makes it live */
        }
      }

      FUN_00216078(e->typeId_00, 0, blade + 0x198); /* hit test parameters */
      e->swordEffect_198 = blade;
      return;
    }

    if ((e->flags_06 & 4) != 0)
    {
      FUN_00257b70(e); /* one call: FUN_00267d38(0xA4, e), the swing */
    }
    return;
  }

  /* Any other keyframe: if this one carries the 0x200 event and we just
   * stepped onto it, tell the blade to dissipate. The `== 0x42` test on the
   * pointer is what stops a recycled +0x198 -- the interaction candidate uses
   * the same word -- being mistaken for a blade. */
  {
    short *blade = (short *)e->swordEffect_198;
    if ((e->keyframeEvent_AA & 0x200) != 0 && (e->flags_06 & 8) != 0 && blade != NULL &&
        *blade == 0x42)
    {
      FUN_00225bc8(blade, 2); /* animation 2, the dissipate */
    }
  }
}

/* --------------------------------------------------------------------------
 * FUN_002d21b8 -- type 0x42, the blade.
 *
 * grp_0179: four bones, three animations. Rides the swinger's role-5 bone --
 * role 4 is the right hand, where a *held* weapon goes; role 5 on grp_0001 is
 * bone 17, a finger. The blade grows out of the fist.
 */
void update_sword_blade_entity(entity *blade)
{
  char lightSlot;

  if (blade->animation_A0 == 1)
  {
    if ((blade->flags_06 & 1) != 0)
    {
      FUN_00225bc8(blade, 0); /* swing over: fall into the hit test's idle */
      goto light;
    }
  }
  else if (blade->animation_A0 > 1)
  {
    if (blade->animation_A0 == 2 && (blade->flags_06 & 1) != 0)
    {
      FUN_00265ec0(blade); /* dissipate over: gone */
    }
    goto light;
  }
  else if (blade->animation_A0 != 0)
  {
    goto light;
  }

  /* Animations 0 and 1 run the swept hit test. NOT ANALYSED. */
  if (FUN_002148a8(blade, &blade->hitParams_198) != 0)
  {
    FUN_002d59c0();
  }

light:
  lightSlot = blade->lightSlot_195;
  if (lightSlot >= 0)
  {
    float pos[3];

    /* 0.65 up the blade's own bone 0. DAT_003266f8 is (0, 0, 0.65). */
    FUN_0020dc88(blade, 0, DAT_003266f8, pos);
    FUN_00267da0(&DAT_00343888 + lightSlot * 5, pos, 0xc);

    /* +0x62 / 32, a signed divide: 0x1000 -> 128 grey, 0x1FE0 -> 255 white. */
    {
      byte level = (byte)(((blade->glowRamp_62 < 0) ? blade->glowRamp_62 + 0x1f
                                                    : blade->glowRamp_62) >> 5);
      (&DAT_00343894)[lightSlot * 0x14] = level;
      (&DAT_00343895)[lightSlot * 0x14] = level;
      (&DAT_00343896)[lightSlot * 0x14] = level;
    }

    if (blade->glowRamp_62 < 0x1fe0)
    {
      int stepped = blade->glowRamp_62 + DAT_003555bc * 8;
      blade->glowRamp_62 = (short)stepped;
      if ((short)stepped > 0x1fdf)
      {
        blade->glowRamp_62 = 0x1fe0;
      }
    }
  }

  /* +0x134 is the draw's alpha over 128, and 0 means opaque -- so the blade
   * fades in over about thirty frames and then draws solid. A flat 4 per
   * frame, not scaled by the frame time. */
  if (blade->fadeLevel_134 != 0)
  {
    byte stepped = blade->fadeLevel_134 + 4;
    blade->fadeLevel_134 = (stepped > 0x78) ? 0 : stepped;
  }

  /* DAT_0058bf10 is pool slot 0's +0x60. The blade cannot outlive the swing
   * even if its own animation has not finished. */
  if (DAT_0058bf10 != 0x1c)
  {
    FUN_00265ec0(blade);
  }
}

/* --------------------------------------------------------------------------
 * FUN_00266098, called from FUN_00265ec0.
 *
 * The ONLY thing that frees a DAT_00343888 light slot: radius 0 is both the
 * light's extent and the allocator's free-list marker. A destroy path that
 * skips it leaks a live light.
 */
void release_entity_light_slot(entity *e)
{
  if (e->lightSlot_195 >= 0)
  {
    (&DAT_00343898)[e->lightSlot_195 * 5] = 0.0f;
    e->lightSlot_195 = -1;
  }
}
