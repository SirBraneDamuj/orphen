/*
 * Player Magic Cast -- state 0x1D, and the homing projectile it throws
 *
 * Original functions:
 *   FUN_002562b0  0x002562b0  the state handler, PTR_FUN_0031e160[1]
 *   FUN_002d2e00  0x002d2e00  spawn the projectile, type 0x44
 *   FUN_002d2ca8  0x002d2ca8  pick what it will chase
 *   FUN_002d2470  0x002d2470  type 0x44's actor behaviour
 *   FUN_00257b50  0x00257b50  FUN_00267d38(0xA5, e), the cast cue
 *   FUN_00257b40  0x00257b40  FUN_00267d38(0xA6, e), the launch cue
 *
 * Entered from FUN_00256bb8 (analyzed/update_player_grounded_field_state.c)
 * through its *use* branch -- mapped action 0x10, Triangle -- not through the
 * attack branch. The dispatch is the same FUN_002298d0 type-id lookup the sword
 * swing uses, and the lead player is type 1, class 0, which here means
 * state 0x1D with animation 0x14.
 *
 * The class-0 branch also clears the player's +0x198 on the way in. That word
 * is shared with the sword blade of state 0x1C and with the interaction
 * candidate FUN_00252828 writes; FUN_002562b0 tests it against zero to decide
 * whether the cast produced anything, so entering with a stale value in it
 * would launch whatever happened to be there.
 *
 * Constants (gp = 0x00359F70, values read from s01_e24.bin):
 *   DAT_0031e0a8  (0.071, -0.010, -0.106)  the spawn offset in the hand bone
 *   fGpffffa738   1.0472   60 degrees, the target search's elevation cone
 *   uGpffffa73c   0.01     the projectile's starting radius and height
 *   uGpffffa740   0.0018   its speed, per tick
 *   DAT_0035467c  3.14159  the charge orbit's facing bias
 *   DAT_00354680  0.0001   the charge orbit's step
 *   DAT_00354684  0.1      charge growth per frame, to a scale of 2.0
 *   DAT_00354688  0.05     trail-ghost shrink per frame
 *   DAT_003546a0  0.005    turn-rate ramp per frame
 *   DAT_003546a4  0.349066 turn-rate cap, 20 degrees
 *
 * PORTED 2026-08-30. FUN_002562b0 into
 * port/src/ported/player/original_player_controller.cpp; FUN_002d2e00,
 * FUN_002d2ca8 and FUN_002d2470 into
 * port/src/ported/entity/actor_frame_update.cpp. See port/README.md.
 *
 * FUN_00215ac8 and its parameters are analysed and ported -- see
 * analyzed/sword_hit_test_and_damage.c. It returns the CONTACT COUNT, which the
 * decompiler hides behind an apparently unassigned local, and that count is the
 * third of the three things that detonate the bolt.
 *
 * FUN_00216078(casterType, 1, ...) fills the parameters at +0x1AC that the test
 * is the only reader of, and it too is ported. The hundred-particle impact
 * burst into DAT_00355620 was ported earlier. So the whole lifetime is now
 * covered, and a bolt that reaches an enemy detonates on it instead of flying
 * on to its timeout.
 *
 * DRAWN BY THE OTHER PASS. Type 0x44's +0x02 carries bit 0x200, and
 * FUN_0020c5a8 refuses to put such an entity in the skeletal draw list at all --
 * they belong to FUN_0020f3e0, which walks the pool for exactly what the first
 * pass skipped and hands each to FUN_0020f510. Its model, grp_017e, is a strip
 * of 16-byte billboard sprite records with no PSC3 magic. Both are analysed in
 * analyzed/billboard_sprite_pass.c and ported.
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * FUN_002562b0 -- state 0x1D.
 *
 * Two points in animation 0x14's timeline, and a per-frame hold between them.
 */
void update_player_magic_cast(entity *e)
{
  float handPoint[4];
  int bone;

  if (player_sword_attack_finished(e)) /* FUN_002560e8; shared with state 0x1C */
  {
    return;
  }

  if (e->timelineCursor_A8 == 6 && (e->flags_06 & 8) != 0)
  {
    bone = FUN_0020dd78(e, 4); /* role 4: the right hand */
    FUN_0020dc88(e, bone, DAT_0031e0a8, handPoint);

    e->actionEffect_198 = FUN_002d2e00(e, handPoint);
    if (e->actionEffect_198 == 0)
    {
      /* Pool full, or the floor is above the hand. Same answer either way. */
      FUN_00252d88(e);
      return;
    }
  }
  else if (e->timelineCursor_A8 == 10 && (e->flags_06 & 8) != 0)
  {
    int projectile = e->actionEffect_198;
    if (projectile == 0)
    {
      FUN_00252d88(e);
      return;
    }

    *(short *)(projectile + 0x60) = 1;                              /* launched */
    *(ushort *)(projectile + 4) &= 0xfeff;                          /* physics on */
    FUN_00257b40(e);                                                /* cue 0xA6 */
    FUN_00257ad0();                                                 /* FUN_0023bbd8(0, 3) */
  }

  /* Every frame it is still charging, the hand point is copied straight into
   * the projectile's +0x20. This is a copy, not an attachment -- the
   * projectile's +0x192 is never set -- which is why an interrupted cast leaves
   * the charge where it was instead of dragging it. */
  if (e->actionEffect_198 != 0 && *(short *)(e->actionEffect_198 + 0x60) == 0)
  {
    bone = FUN_0020dd78(e, 4);
    FUN_0020dc88(e, bone, DAT_0031e0a8, (float *)(e->actionEffect_198 + 0x20));
  }
}

/* --------------------------------------------------------------------------
 * FUN_002d2ca8 -- pick the target, once.
 *
 * Slots 10 upward, so never the caster. Nearest candidate inside ten units.
 * Read the elevation branch carefully: a candidate more than two units away
 * vertically SKIPS the cone test rather than failing it.
 */
entity *find_magic_homing_target(entity *projectile)
{
  entity *best = NULL;
  float bestDistance = 10.0;
  int slot;

  for (slot = 10; slot < 0x100; slot++)
  {
    entity *candidate = &DAT_0058beb0[slot];
    float distance;

    if (DAT_005a96b0[slot] == 0)
      continue;
    if ((candidate->descriptorFlags_02 & 8) == 0)
      continue;
    if ((candidate->flags_04 & 0x10) != 0)
      continue;

    distance = FUN_0023a4e8(projectile, candidate); /* horizontal only */
    if (distance >= bestDistance)
      continue;

    if (ABS(candidate->positionY_28 - projectile->positionY_28) < 2.0)
    {
      /* Within two units of our height: also has to be inside a 60 degree
       * cone, measured to the candidate's waist. */
      float elevation = FUN_00305408((candidate->positionY_28 + candidate->height_58 * 0.5) -
                                         projectile->positionY_28,
                                     distance);
      if (ABS(elevation) >= fGpffffa738)
        continue;
    }

    bestDistance = distance;
    best = candidate;
  }

  return best;
}

/* --------------------------------------------------------------------------
 * FUN_002d2e00 -- spawn the projectile at a world point.
 *
 * The one guard is the floor: FUN_00227798 under the hand must not be above it.
 */
int spawn_magic_projectile(entity *caster, float *handPoint)
{
  float groundHeight;
  int projectile;
  char lightSlot;

  groundHeight = FUN_00227798(handPoint[0], handPoint[1], handPoint[2]);
  if (groundHeight > handPoint[2])
  {
    return 0;
  }

  projectile = FUN_00265e28(0x44);
  if (projectile == 0)
  {
    return 0;
  }

  FUN_00267da0(projectile + 0x20, handPoint, 0xc);
  *(float *)(projectile + 0x4c) = groundHeight;
  *(float *)(projectile + 0x5c) = caster->facing_5C;
  *(undefined4 *)(projectile + 0x74) = 0;
  *(undefined4 *)(projectile + 0x78) = 0;

  /* bit 0x002 single-point ground sampling; bit 0x100 physics off, so the
   * caster can hold it by writing +0x20. The launch clears 0x100. */
  *(ushort *)(projectile + 4) |= 0x102;

  *(float *)(projectile + 0x54) = uGpffffa73c;  /* radius */
  *(float *)(projectile + 0x58) = uGpffffa73c;  /* height */
  *(float *)(projectile + 0x11c) = uGpffffa73c; /* and their two mirrors */
  *(float *)(projectile + 0x120) = uGpffffa73c;
  *(float *)(projectile + 0x19c) = uGpffffa740; /* speed, per tick */

  FUN_00216078(1, 1, projectile + 0x1ac); /* hit test parameters */

  lightSlot = FUN_00266050();
  *(char *)(projectile + 0x195) = lightSlot;
  if (lightSlot >= 0)
  {
    *(uint *)(&DAT_00343894 + lightSlot * 0x14) = 0x40404; /* rgb 4, almost black */
    (&DAT_00343898)[lightSlot * 5] = 2.0;                  /* radius */
    FUN_002660d0(projectile);                              /* put it at the projectile */
  }

  *(int *)(projectile + 0x198) = (int)find_magic_homing_target(projectile);
  *(short *)(projectile + 0x1aa) = 0xa0;   /* hit test cooldown */
  *(short *)(projectile + 0x1a8) = 0x2580; /* homing ticks */

  return projectile;
}

/* --------------------------------------------------------------------------
 * FUN_002d2470 -- type 0x44.
 *
 * Four states on +0x60, and it is a lifetime rather than a machine: each one
 * only moves forward.
 *
 *   0  charging in the caster's hand
 *   1  flying: home, move, drop a trail ghost every other frame
 *   2  a trail ghost
 *   4  the impact
 */
void update_magic_projectile(entity *p)
{
  FUN_0023a068(p); /* the freeze gate, and the result is discarded */

  if (p->state_60 == 0)
  {
    /* DAT_0058bf10 is pool slot 0's +0x60: the caster leaving state 0x1D takes
     * the charge with it. */
    if (DAT_0058bf10 != 0x1d)
    {
      FUN_00265ec0(p);
      return;
    }

    /* A movement request the physics never spends -- +0x04 bit 0x100 is on for
     * the whole of this state. It is here because the launch clears that bit,
     * and whatever has piled up becomes the first push. */
    {
      float orbit = p->facing_5C + DAT_0035467c;
      p->desiredDeltaX_30 += FUN_00305130(orbit) * DAT_00354680;
      p->desiredDeltaZ_34 += FUN_00305218(orbit) * DAT_00354680;
    }

    if (p->scale_14C < 2.0)
    {
      FUN_00229ef0(p->scale_14C + DAT_00354684, p);
    }

    if (p->lightSlot_195 >= 0)
    {
      int level = (byte)(&DAT_00343894)[p->lightSlot_195 * 0x14] + 4;
      if (level > 0xff)
        level = 0xff;
      (&DAT_00343894)[p->lightSlot_195 * 0x14] = (byte)level;
      (&DAT_00343895)[p->lightSlot_195 * 0x14] = (byte)level;
      (&DAT_00343896)[p->lightSlot_195 * 0x14] = (byte)level;
    }
    return;
  }

  if (p->state_60 == 2)
  {
    /* A trail ghost. Its lifetime is the animation's, which is the only reason
     * the sprite-strip half of FUN_00225c90 had to be ported. */
    if ((p->flags_06 & 1) != 0)
    {
      FUN_00265ec0(p);
      return;
    }
    FUN_00229ef0(p->scale_14C - DAT_00354688, p);
    return;
  }

  if (p->state_60 == 4)
  {
    if (p->lightSlot_195 < 0)
    {
      FUN_00265ec0(p);
      return;
    }
    /* The original compares and decrements the packed rgb word at +0x0C as one
     * u32. That is only equivalent to three ramps because the three bytes stay
     * equal, and they do -- nothing ever writes them apart. */
    if (*(uint *)(&DAT_00343894 + p->lightSlot_195 * 0x14) > 0x80808)
    {
      *(uint *)(&DAT_00343894 + p->lightSlot_195 * 0x14) -= 0x20202;
      return;
    }
    FUN_00265ec0(p);
    return;
  }

  /* --- state 1: flying --------------------------------------------------- */

  if (p->hitCooldown_1AA == 0)
  {
    /* FUN_00215ac8, the swept box test against the entity pool using the
     * parameters at +0x1AC. NOT ANALYSED. Its hit is one of the three things
     * that detonate the projectile. */
    hit = FUN_00215ac8(p, box, &p->hitParams_1AC);
  }
  else
  {
    p->hitCooldown_1AA -= DAT_003555bc;
    if (p->hitCooldown_1AA < 0)
      p->hitCooldown_1AA = 0;
  }

  p->lifetime_62 += DAT_003555bc;

  /* +0x04 bit 0 disables the entity-vs-entity clamp; it is dropped after 0x140
   * ticks so the projectile can leave the caster first. */
  if ((p->flags_04 & 1) != 0 && p->lifetime_62 > 0x13f)
  {
    p->flags_04 &= 0xfffe;
  }

  if ((p->collisionFlags_0C & 0x266) != 0 || p->lifetime_62 > 0x2580 || hit != 0)
  {
    /* Up to a hundred particles into DAT_00355620 -- random colours, sizes and
     * headings off the projectile's own facing -- and FUN_002d2348 installed at
     * DAT_00355e0c as their stepper. NOT ANALYSED. */
    build_impact_particles(p);
    p->state_60 = 4;
    p->halfword_08 |= 1;
    return;
  }

  if (p->homingTimer_1A8 != 0)
  {
    entity *target = (entity *)p->homingTarget_198;
    float rate = p->turnRate_1A4 + DAT_003546a0;
    if (rate > DAT_003546a4)
      rate = DAT_003546a4;
    p->turnRate_1A4 = rate;

    if (target != NULL && (target->flags_04 & 0x10) == 0)
    {
      float step;

      step = FUN_0023a320(p->facing_5C, FUN_0023a4b8(p, target), rate);
      if (step != 0.0)
        p->facing_5C += step;

      step = FUN_0023a320(p->pitch_1A0,
                          FUN_00305408((target->positionY_28 + target->height_58 * 0.5) -
                                           p->positionY_28,
                                       FUN_0023a4e8(p, target)),
                          rate);
      if (step != 0.0)
        p->pitch_1A0 += step;
    }

    p->homingTimer_1A8 -= DAT_003555bc;
    if (p->homingTimer_1A8 < 0)
      p->homingTimer_1A8 = 0;
  }

  /* Speed is per tick; the pitch splits it, the yaw spreads the horizontal
   * part. */
  {
    float step = p->speed_19C * (float)DAT_003555bc;
    float horizontal = step * FUN_00305130(p->pitch_1A0);
    p->desiredDeltaY_38 += step * FUN_00305218(p->pitch_1A0);
    p->desiredDeltaX_30 += horizontal * FUN_00305130(p->facing_5C);
    p->desiredDeltaZ_34 += horizontal * FUN_00305218(p->facing_5C);
  }

  FUN_002660d0(p); /* the light rides along */

  /* Every other frame, a ghost of itself, in state 2, with physics off so it
   * hangs where it was dropped. */
  if ((DAT_003555b4 & 1) != 0)
  {
    int ghost = FUN_00265e28(p->typeId_00);
    if (ghost != 0)
    {
      FUN_00267da0(ghost + 0x20, &p->positionX_20, 0xc);
      *(float *)(ghost + 0x4c) = p->groundHeight_4C;
      FUN_00229ef0(p->scale_14C, ghost);
      *(short *)(ghost + 0x60) = 2;
      *(short *)(ghost + 0xa0) = 1;
      *(ushort *)(ghost + 4) |= 0x100;
    }
  }
}
