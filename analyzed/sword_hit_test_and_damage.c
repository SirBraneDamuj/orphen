/*
 * Contact, Damage and Death
 *
 * Original functions:
 *   FUN_002148a8  0x002148a8  the swept hit test a weapon effect runs
 *   FUN_00215ac8  0x00215ac8  the unswept form: one caller-supplied box
 *   FUN_00215670  0x00215670  a third form, used by enemy attacks (not ported)
 *   FUN_00216140  0x00216140  what one contact costs the victim
 *   FUN_00215e48  0x00215e48  clear a swing's already-hit set
 *   FUN_00216078  0x00216078  read an attack's four parameter bytes
 *   FUN_0025bae8  0x0025bae8  the victim's element resistance table
 *   FUN_002206a8  0x002206a8  the hit sparks
 *   FUN_002d5630  0x002d5630  the on-screen HP bar
 *   FUN_002cd0a0  0x002cd0a0  type 0x62 draining +0xBE against its hit points
 *   FUN_002cda60  0x002cda60  type 0x62's state 6, the death
 *   FUN_00251dc0  0x00251dc0  the lead's hit points, attack and defence
 *
 * THE SHAPE OF IT. A hit is three separate things, and they happen on three
 * different frames:
 *
 *   1. an *effect* entity -- the sword blade, the magic bolt -- sweeps a volume
 *      against the pool and calls FUN_00216140 for anything it touches;
 *   2. FUN_00216140 works out what that contact costs and *adds it to the
 *      victim's +0xBE*. It never looks at hit points and never kills anything;
 *   3. the victim's own behaviour, next time it is ticked, drains +0xBE against
 *      its +0x12A and decides whether that killed it.
 *
 * +0xBE is a mailbox, not a subtraction. That is why a hit landed after a
 * victim has already run this frame still registers, and why two attackers in
 * the same frame both count.
 *
 * The attacker in step 1 is the *effect*, not the swinger: +0x12C, +0x02 and
 * +0x96 are all read off the blade. FUN_00256130 copies the player's +0x12C
 * onto it at spawn, which is the only place the two are connected.
 *
 * PORTED 2026-08-30 into port/src/ported/entity/original_hit_test.{h,cpp} (the
 * two hit tests, the damage and the hit-set clear),
 * port/src/ported/resource/hit_parameter_table.{h,cpp} (FUN_00216078's blob),
 * port/src/ported/model/psc3_model.cpp (the two hit-volume sections),
 * FUN_002cda60_enemy62_death and the two call sites in
 * port/src/ported/entity/actor_frame_update.cpp, and
 * FUN_00251dc0_load_player_stats in
 * port/src/ported/script/scene_command_interpreter.h.
 *
 * FUN_002206a8, the hit sparks, is ported too -- see the section at the end and
 * port/src/ported/entity/original_hit_sparks.{h,cpp}.
 *
 * NOT PORTED: FUN_00215670 (the enemy-attack form), FUN_002d5630 (the HP bar,
 * which is gated on DAT_003555D3 and never draws in s01_e024), and the
 * FUN_0025ba98 branch of the resistance lookup, which no attacker in the scene
 * can reach.
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * THE ATTACK RECORD -- FUN_00216078, SCR.BIN resource 0xBE (uGpffffadfc).
 *
 * The blob's +0x24 points at {typeId, offset, count} triples, zero terminated.
 * A record is four bytes and describes one *attack*, not one attacker: the
 * player has two, and FUN_00256130 asks for index 0 while FUN_002d2e00 asks for
 * index 1.
 *
 *   +0x00 u16  element / behaviour bits. The element is the LOWEST SET BIT,
 *              which indexes the victim's sixteen-byte resistance table. Bit
 *              0x4000 additionally skips the guard-arc test.
 *   +0x02 s8   percentage bonus; the scale is (bonus + 100) / 100
 *   +0x03 u8   the reaction the victim plays, stored at +0xBC
 *
 * Read out of eeMemory.bin, type 1 (the lead player) has exactly two:
 *
 *   index 0  01 00 1e 00   element 0, +30%   the sword
 *   index 1  10 00 0a 00   element 4, +10%   the magic bolt
 *
 * The sword's record matches the blade entity's own +0x198 in the same dump,
 * byte for byte, which is what confirms the direction of the copy.
 */

/* --------------------------------------------------------------------------
 * FUN_002148a8 -- the swept hit test.
 *
 * THE VOLUME COMES OUT OF THE MODEL. PSC3 header +0x30 and +0x34 are two
 * sections nothing else reads, and only weapon-effect models carry them --
 * grp_0179 (the blade) has both, grp_0001 (the player) has neither, which is
 * the early return that makes this a no-op for everything that is not a weapon.
 *
 *   +0x30  one byte per pose column: a record index, or 0xFF for "no volume"
 *   +0x34  0x10-byte records: {s16 steps, s16 radius, s16 A[3], s16 B[3]}
 *
 * grp_0179's map is 16 bytes -- ff 00 ff 01 02 ff ff ff ff ff ff 03 04 00 00 00
 * -- so the blade is only dangerous on columns 1, 3, 4, 11 and 12, and its five
 * records run from a 0.35-radius stub to a 0.85-long blade.
 *
 * THE SWEEP IS FORWARD-LOOKING. The interpolation factor is +0xA4 / +0xA6 --
 * the countdown *still to run* over the entry's duration -- so it is 1 at the
 * start of a timeline entry and 0 at its end, and the volume runs from the
 * column being entered back toward the one being left. The pair the test
 * actually measures is [now, next frame]: slots 0/1 are evaluated at
 * `remaining - frameTicks` and cached to +0xF0/+0x100, and slots 2/3 are last
 * frame's cache. +0x06 bit 0x40 says the cache is valid.
 *
 * FUN_00215e48 clears both, which is why forgetting the already-hit set and
 * forgetting the sweep history are the same operation.
 *
 * THE ALREADY-HIT SET DOES NOT LINE UP WITH ITS OWN CLEAR. Both hit tests start
 * a `uint *` at +0xD0 and step it *before* the first slot, so slot n's bit is
 * in word (n >> 5) + 1: +0xD4 for slots 0..31, +0xF0 for 224..255. FUN_00215e48
 * clears eight words from +0xD0 down. So it clears +0xD0, which no slot uses,
 * and misses +0xF0, which slots 224..255 do -- a slot that high stays flagged
 * for the rest of the scene once hit. On top of that, +0xF0 is also where this
 * function caches last frame's blade endpoints, so on the real machine those
 * slots test the float bits of a cached coordinate.
 *
 * THE RETURN VALUE IS NOT WHAT THE DECOMPILER SAYS. `cStack_161` looks like a
 * variable nothing assigns. It is sp+0x83F, and the scratch buffer this
 * function hands FUN_00216140 starts at sp+0 -- 0x002164D0 is
 * `sb v0, 2111(s2)`, FUN_00216140 storing to exactly that byte. Ghidra never
 * connects the two. **Both hit tests return the number of contacts.** That is
 * what makes FUN_002d21b8 play its hit cue and what makes the magic bolt
 * detonate on something alive rather than only on a wall or a timeout.
 *
 * The shared scratch block, for the record:
 *   +0x834 s16  damage the last contact charged
 *   +0x838 s16  the defence it was reduced by
 *   +0x83E s8   negates the return; nothing on these paths sets it
 *   +0x83F s8   the contact count -- the return value
 *   +0x840 u8   written 2 by the caller before the scan; unread
 *   +0x843 u8   the DAT_003151c8 write cursor
 */
char swept_hit_test(entity *attacker, hit_record *params)
{
  hit_scratch scratch = {0};
  model *m = attacker->model_15C;

  if (m->hitColumnMap_30 == 0 || m->hitVolumes_34 == 0)
    return 0;
  if (attacker->previousPoseColumn_AE < 0)
    return 0;

  byte toward = m->hitColumnMap_30[attacker->previousPoseColumn_AE];
  byte from = m->hitColumnMap_30[attacker->poseColumn_AC];
  if (toward == 0xff || from == 0xff)
    return 0;

  /* ... build the two segments, transform by FUN_0020cdc0, lay `steps` boxes
   * along each, then fill the gap between them: any box that moved more than
   * four half-extents in a frame gets extra boxes appended, interpolated
   * corner-wise. The cap is 31, and it ABANDONS THE WHOLE FILL mid-segment
   * rather than clamping -- so a very fast swing gets fewer boxes at its far
   * end, not coarser ones along its length. ... */

  /* Which entities are candidates. Read off the ATTACKER's +0x02, and the four
   * cases are exclusive in this order. A player-side effect carries 0x1000, so
   * the sword and the bolt both take the first: they can only ever hit 0x2048,
   * which the party (0x4004) and the map props (0x0180) are not in. */
  uint mask;
  if (attacker->flags_02 & 0x1001)      mask = 0x2048;
  else if (attacker->flags_02 & 0x2008) mask = 0x1001;
  else if (attacker->flags_02 & 0x40)   mask = 0x3009;
  else                                  mask = 0xfdff;

  for (int slot = 0; slot < 0x100; slot++)
  {
    if (DAT_005a96b0[slot] < 1) continue;          /* not live */
    if (&DAT_0058beb0[slot] == attacker) continue; /* self */
    if (already_hit(attacker, slot)) continue;
    entity *victim = &DAT_0058beb0[slot];
    if ((victim->flags_02 & mask) == 0) continue;
    if (victim->flags_04 & 0x10) continue;         /* already dying */
    if (victim->hitSource_C0 != 0) continue;       /* committed to another attack */
    if (victim->pendingDamage_BE != 0) continue;   /* already hit this frame */

    /* The victim's volume is +0x110..+0x120, NOT +0x54/+0x58 -- a separate pair
     * of fields FUN_00229ef0 happens to fill from the same two numbers. The
     * vertical span is asymmetric: feet to body height, not centre plus or
     * minus, which is why a low sweep passes under a tall actor. */
    for (int box = 0; box < boxCount; box++)
    {
      if (!overlaps(boxes[box], victim)) continue;
      if (victim->freezeTimer_BD != '\0') continue; /* untouchable in hit-stop */

      victim->lastAttacker_CC = attacker;
      mark_already_hit(attacker, slot);
      victim->hitDirection_C4 = attacker->facing_5C + attacker->bias_C8;
      FUN_00216140(&scratch, params, attacker, victim);
      DAT_003151c8[scratch.cursor_843++] = slot;
      victim->hitSourceKind_BB = (attacker->flags_02 & 0x1001) ? 1 : 0;
      break;                                        /* next victim, not next box */
    }
  }
  DAT_003151c8[scratch.cursor_843] = 0xffff;
  return scratch.contacts_83F;
}

/* --------------------------------------------------------------------------
 * FUN_00215ac8 -- the same scan with a caller-supplied box.
 *
 * Identical candidate filter, identical already-hit set, identical
 * FUN_00216140. It simply skips everything above that builds a swept volume out
 * of animation data, because its callers already know the box they want.
 *
 * ONE REAL DIFFERENCE: it does not break out of the pool walk on a contact, so
 * one box can catch several things at once. FUN_002148a8's `break` only leaves
 * the box loop for the victim it just hit.
 *
 * The magic bolt's box, from FUN_002d2470: DAT_0035468c (0.15) either side in
 * the horizontal plane, and vertically the bolt's own +0x28 to +0x28 + +0x58 --
 * sitting on its feet, not centred on it. It runs once +0x1AA has counted down,
 * and the test and the countdown are the two halves of one `if`.
 */

/* --------------------------------------------------------------------------
 * FUN_00216140 -- what one contact costs.
 */
void apply_hit(hit_scratch *scratch, hit_record *params, entity *attacker, entity *victim)
{
  if (victim->pendingDamage_BE != 0)
    return; /* the FIRST hit of a frame owns the reaction, not the last */

  victim->hitFlags_C2 = params->flags;
  victim->hitReaction_BC = params->reaction;

  /* The resistance table, sixteen bytes, chosen off the victim's +0x02. The
   * fallback is ALL HUNDREDS, not "no table". */
  char resistance[16];
  short statType = (victim->type_00 == 0x38) ? victim->realType_1CE : victim->type_00;
  if (statType >= 0x272)
    FUN_0025ba98(statType, resistance);              /* streamed types, SCR 0xBD */
  else if (victim->flags_02 & 3)
    FUN_0025bae8(1, statType, resistance);           /* a party character */
  else if (victim->flags_02 & 8)
    FUN_0025bae8(0, statType, resistance);           /* an enemy */
  else
    memset(resistance, 'd', 16);                     /* 100 across the board */

  /* THE ENEMY TABLE IS INDEXED BY `type - 0x7C`, AND IT IS NOT BOUNDS CHECKED.
   * The type 0x62 flyer is below the enemy range, so its index is -26 and the
   * read runs backwards out of the group into the blob's string pool. It is
   * perfectly deterministic -- the same file, the same offset -- and it comes
   * out as 46 for element 0. So the sword's 1.3x multiplier lands on
   * trunc(0.46 * 1.3 * 1) = 0, and the hit only costs anything at all because
   * of the floor below. */

  int element = 0;
  if ((params->flags & 1) == 0)
    for (element = 1; element < 16 && (params->flags & (1 << element)) == 0; element++)
      ;
  if (element > 15) element = 0;

  short damage = FUN_0030bd20((resistance[element] / 100.0) *
                              ((params->powerBonus + 100) / 100.0) *
                              attacker->attackPower_12C);
  scratch->damage_834 = damage;
  scratch->defence_838 = victim->defence_12E;

  int net = damage - victim->defence_12E;
  if (net < 1)
    net = 1; /* a hit always costs at least one point, however good the armour */
  victim->pendingDamage_BE += net;

  /* The guard arc. +0x124 is a HALF-ANGLE; zero means the victim cannot block,
   * which is every enemy in s01_e024. Inside the arc the damage is NEGATED, not
   * cancelled -- the victim's own behaviour reads the sign to pick a block
   * reaction -- and the sparks and the HP bar are both skipped. */
  bool blocked = false;
  if (victim->guardArc_124 != 0.0 && (params->flags & 0x4000) == 0)
  {
    float away = FUN_00216690(victim->hitDirection_C4 + DAT_0035217c);
    float delta = FUN_002166e8(victim->facing_5C, away);
    if (delta <= victim->guardArc_124 && delta >= -victim->guardArc_124)
    {
      victim->pendingDamage_BE = -victim->pendingDamage_BE;
      blocked = true;
    }
  }

  if (!blocked)
  {
    if (0 < victim->pendingDamage_BE && (victim->flags_02 & 0x4b) &&
        !(victim->byte_96 & 0x20))
      FUN_002d5630((victim->flags_02 & 0x48) != 0, victim->hp_12A, victim->maxHp_128, net);
    FUN_002206a8(victim, playerSideVictim);          /* the hit sparks */
  }

  scratch->contacts_83F++;                           /* the caller's return value */
}

/* --------------------------------------------------------------------------
 * FUN_002cd0a0 lines 12-27, and FUN_002cda60 -- the receiving end, for type
 * 0x62.
 *
 * The drain is the top of the type's wrapper, before its state dispatch:
 *
 *   +0x12A -= +0xBE
 *   if that reached zero: +0x12A = 0, state 6 with animation 4, +0x04 loses
 *   bit 8 and gains 0x10 (which is what makes it stop being a hit candidate),
 *   +0x0C loses bit 0 so the death starts airborne, +0x134 = 0x7C, death cry
 *   +0x138 = 0xC0 and +0x1C2 = 0x1E0 either way -- the white hit flash
 *   +0xBE = 0
 *
 * eeMemory.bin catches two flyers mid-death: slots 24 and 25, state 6, animation
 * 4, +0x0C bit 0 clear, +0x134 at 0x7C, +0x138 at 0xC0, +0x1C2 at 32 and 64 --
 * one frame apart at 32 ticks a frame. Their +0xCC both point at slot 23, the
 * blade, and their +0xC2 is 1, the sword record's flags.
 *
 * **The flyers have no hit points at all.** +0x128 and +0x12A are both zero on
 * every type 0x62 in the dump, so `0 - damage < 1` on the first hit and any
 * contact kills. Combined with the damage floor above, every fly dies to one
 * point of damage and throws exactly ten hit sparks.
 *
 * State 6 itself, FUN_002cda60, splits on +0x0C bit 0 -- the grounded flag:
 *
 *   airborne  pitch toward pi/2 (uGpffffa5dc) at 0.00545 a tick and spin about
 *             +0x5C at 0.0109, exactly twice the pitch rate, so it rolls
 *             through half a turn while it tips over. Nothing here drives it
 *             downward; +0x48 does.
 *   grounded  animation 5, +0x154 back to zero, +0x08 bit 0x10 so the pose
 *             filter snaps rather than easing. When THAT animation completes,
 *             +0x06 is ASSIGNED 0x10 -- not or-ed, so every animation latch
 *             goes with it -- and +0x04 gains 0x800, handing the slot to
 *             FUN_0023a568 to fade out and free itself.
 */

/* --------------------------------------------------------------------------
 * FUN_00251dc0 -- where the lead player's numbers come from.
 *
 * FUN_0022a418:206 calls it at scene init with DAT_0058beb0 outright, so it is
 * always slot 0. It copies four fields off the party record FUN_002294d0 loaded
 * into DAT_00343688 + class * 0x28:
 *
 *   +0x128 max hit points   record +0x02   (already copied from +0x06)
 *   +0x12A hit points       record +0x06
 *   +0x12C attack power     record +0x07
 *   +0x12E defence          record +0x08
 *
 * Orphen's record gives 50 / 50 / 1 / 0, which is what eeMemory.bin holds at
 * slot 0. Attack power is the number FUN_00216140 scales, so without this every
 * hit falls through to the damage floor by accident.
 *
 * --------------------------------------------------------------------------
 * FUN_002206a8 -- the hit sparks.
 *
 * PORTED 2026-08-31 into port/src/ported/entity/original_hit_sparks.{h,cpp}.
 *
 * A THIRD particle system, sharing nothing with the other two: 1000 entries of
 * 0x38 bytes at DAT_00355b74, divided into ten fixed groups of 100 through the
 * table at DAT_00355b78 (one byte of buffer index, one halfword of live count),
 * no behaviour pointer, and a quad oriented in world space. FUN_002205d0 carves
 * both out of the heap cursor at DAT_0035572c and marks every entry dead with
 * +0x28 = 0xFFFF and +0x2A = 0xFF.
 *
 * A burst takes the first group whose count is not positive, so at most ten
 * hits can be showing at once and an eleventh silently shows nothing. The count
 * is `min(100, damage * 10)`, which is the arithmetic the save state confirms:
 * two flyers hit for one point each, two groups holding ten particles each.
 *
 * Each particle gets the victim's position at three quarters of its body
 * height, a random yaw over the full circle, and a place in a fan stepping
 * `(360 / count) * 2pi / 360` from a start of 30 degrees -- an INTEGER division
 * on the left, so a burst of seven leaves a gap rather than closing the circle.
 * +0x24 is a lifetime of 0x780 ticks, +0x2C is 0.3 (or 0.6 for reactions 0x1B
 * and 0x1D) and +0x30 is a fixed speed of 100.
 *
 * FUN_00220910 walks all ten groups once a frame -- in the DRAW half, from
 * FUN_002192c0, not the simulation half where FUN_002d3218 steps DAT_00355620
 * -- and FUN_00220c00 steps and draws one particle. The lifetime falls by the
 * frame ticks; the particle slides outward along its fan angle at
 * `100 * frameTicks / 32000`, in the burst's own local frame rather than in
 * world space.
 *
 * The quad is a STREAK, not a billboard: four corners at (-1, 0.01),
 * (-1, -0.01), (1, -0.01), (1, 0.01) in the local x/z plane, with y a flat zero
 * and ONLY the long axis scaled by +0x2C. It is 0.6 units long and 0.02 wide
 * however far away it is drawn.
 *
 * Two matrices place it, and they have to be separate because the streak points
 * along its own travel while the burst as a whole is oriented off the camera:
 *
 *   Z(-fanAngle)                              the streak's own turn
 *   Y(yaw) -> Z(-cameraYaw - pi/2) -> T(pos)  the burst's placement
 *
 * with the travelled offset added BETWEEN them. That second Z is the exact
 * inverse of the view matrix's own yaw -- FUN_0020bec8 uses +(cameraYaw + pi/2)
 * -- so the fan opens across the screen.
 *
 * TWO THINGS HAVE TO COME OUT OF THE DISASSEMBLY.
 *
 * The matrix sequence. FUN_00220910 loads vf20..23 with the identity once and
 * never writes them again, so every `sqc2 vf20` in the middle of the run is
 * RESTORING THE IDENTITY to the scratchpad before the next rotation builder
 * writes its four entries over it, and `vcallms 0xC` accumulates into vf28..31
 * oldest first. Read as C it looks like four builders each overwriting part of
 * a product matrix, which would be meaningless.
 *
 * The texture slot. FUN_002190f8's last argument is 0x0A22 / 0x0B22 / 0x0122
 * and its low byte is the slot ITSELF -- 0x22, texture 0x19A. At exactly the
 * two rectangles the descriptors at 0x003159B8 name, 0x19A carries two
 * lens-shaped streaks, blue at v 63..79 for a party-side victim and gold at
 * v 80..95 for everything else, each the exact size of its rectangle. Slot 0x21
 * (texture 0x19B) has a smoke puff across both and nothing streak-shaped
 * anywhere on it, and the flies_hit screenshot shows gold.
 *
 * FUN_0020f510 writes `slot + 1` into the same packet field, so the two
 * producers of it disagree by one and only the sheets say which the consumer
 * honours. The same reading corrected DAT_00355620's particles, whose 0x2B is
 * slot 0x2B (texture 0x177, a round spark exactly filling the rectangle) and
 * not slot 0x2A (texture 0x178, flat grey noise there).
 *
 * The descriptors are eight floats each, ST over 256, scaled by 4096 on the way
 * into the GS's 1/16-texel units -- so they are plain texels times 256. The
 * vertex colour is a flat 0xF0F0F0F0 on all four corners, 1.875x white before
 * an additive blend, and FUN_00218ee0 drops the whole quad unless all four
 * corners have Q = 1/viewZ at or under fGpffff8350 (0.7).
 *
 * VERIFIED AGAINST THE SAVE STATE, field for field. Two groups of ten, side 0,
 * scale 0.300, speed 100.0, big 0, lifetimes 1472 and 1504 out of 1920; fan
 * angles 0.5236 / 1.1519 / 1.7802, stepping 36 degrees; and offsets that are
 * exactly `0.1 * elapsedFrames` along `(cos fan, sin fan)` -- (1.2124, 0.7000)
 * at 14 frames for fan = 30 degrees.
 */
