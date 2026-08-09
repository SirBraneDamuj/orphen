#include "orphen_globals.h"

/**
 * The treasure chest cutscene: lead player states 0x0C..0x15.
 *
 * Entered from FUN_00252828's chest branch, which does
 * FUN_00225bf0(player, 0xC, 1) after writing the chest to player +0x198 and
 * 0x4B00 to player +0x1B8. Every state after that comes out of the player
 * state table at PTR_FUN_0031e0e8, dispatched by FUN_00251ed8:
 *
 *   0x0C  0x00254d58  FUN_00254d58
 *   0x0D  0x00254db0  FUN_00254db0
 *   0x0E  0x00254f18  FUN_00254f18
 *   0x0F  0x00254f60  FUN_00254f60
 *   0x10  0x002550f0  (no Ghidra function -- table-only)
 *   0x11  0x00255148  (no Ghidra function -- table-only)
 *   0x12  0x00255260  (no Ghidra function -- table-only)
 *   0x13  0x002552b0  (no Ghidra function -- table-only)
 *   0x14  0x00255448  (no Ghidra function -- table-only)
 *   0x15  0x00255498  FUN_00255498
 *
 * Six of the ten have no `src/FUN_*.c`: nothing calls them directly, so Ghidra
 * never made functions there. They were read from the disassembly.
 *
 * ---------------------------------------------------------------------------
 * The fade
 * ---------------------------------------------------------------------------
 *
 * Two independent blocks, DAT_00571DC0 (in) and DAT_00571DD0 (out), armed by
 * FUN_0025d1c0(direction, speed, colour) and stepped by FUN_0025d2f8 and
 * FUN_0025d238. Level is 0..0x1FE0 and the overlay alpha is level >> 5.
 *
 * The out block also carries a hold: once it reaches full coverage it counts
 * DAT_00571DDA down from 0xA0 before reporting done, which is the five frames
 * of solid colour the work happens behind. At the cutscene's speed of 0xC and
 * the nominal 0x20 ticks that is 21 frames of ramp plus 5 of hold -- and the
 * port measures 28 frames from state 0x0C to 0x0E, which is those 27 plus the
 * frame 0x0C itself takes.
 *
 * The first transition fades through **black** (colour 0) and the second
 * through **white** (0xFFFFFF).
 *
 * ---------------------------------------------------------------------------
 * Game mode 6
 * ---------------------------------------------------------------------------
 *
 * State 0x0D sets iGpffffadbc (DAT_00354D2C) to 6 and state 0x13 puts it back
 * to 0 through FUN_002241d8. FUN_002239c8 reads it at the top of the frame and
 * hands to PTR_FUN_00318a88[mode]; entry 6 is FUN_002245d8:
 *
 *     FUN_00251ed8   the lead player
 *     FUN_00239ce0   the actors
 *     FUN_00237fc0   the text overlay
 *     FUN_002261e0 / FUN_00208450 / FUN_00208ee8 / FUN_00208f28 /
 *     FUN_0020c5a8 / FUN_0020f3e0 / FUN_0020c290    the draw
 *
 * What it leaves out is the point: no FUN_0025b778 or FUN_0025b918 (the scene
 * script) and **no FUN_00216aa0** (the field camera). That last omission is
 * how a cutscene camera stays where it was put.
 *
 * Note the mode is read at the *top* of FUN_002239c8, so the frame that sets
 * it still finishes as a field frame -- FUN_00216aa0 does run once more after
 * state 0x0D. It does nothing, because FUN_00216aa0:79 gives the frame away to
 * the manual camera whenever cGpffffb6e1 is non-zero, and FUN_00217d70 has
 * just set it to 0x23.
 *
 * ---------------------------------------------------------------------------
 * Why the room goes black
 * ---------------------------------------------------------------------------
 *
 * Three separate things in FUN_002342c0, and none of them is the fade:
 *
 * 1. Its tail loop walks the entity pool from DAT_0058C260 -- slot 2 -- for 254
 *    slots, raising +0x04 bit 0x4000 and **+0x08 bit 0** on each. FUN_0020c5a8:69
 *    skips any slot whose +0x08 bit 0 is set (and raises bit 0x10 on it, so the
 *    pose sampler knows there is no previous frame to blend out of). Slot 0,
 *    the lead player, is below the loop's start; state 0x0D clears both bits on
 *    the chest. Everything else in the scene -- the other six chests, the
 *    party, the enemies, and the player's own bandana in slot 4 -- stops being
 *    drawn.
 *
 * 2. It then spins FUN_002340e0 until that function's done byte flips. The
 *    first pass takes the darken branch (`+0x1DA8E` is 0, which FUN_002342c0
 *    just set) and leaves **DAT_00355700 = 3**. FUN_00209140:91 hands that to
 *    VU1 as the cap on every map primitive's fade byte, against the same
 *    0x80 = x1.0 scale the occlusion fade uses -- so the whole room draws at
 *    about 2%. The spin exits after one iteration because the ramp starts at
 *    0x1FE0 and immediately saturates.
 *
 * 3. It replaces the scene's lighting and fog:
 *
 *      DAT_0035566C = 0x404040   the VU1 ambient
 *      DAT_00355670 = 0x808080   light 0's colour
 *      DAT_00355674 = 0          the fog colour
 *      DAT_003439C8 = (0, 0, -1) the light direction, straight down
 *
 *    The fog colour matters as much as the cap: a capped primitive is nearly
 *    *transparent*, not nearly black, so what the room reads as is whatever is
 *    behind it -- and that is the fog-colour clear.
 *
 * All three are undone by FUN_00234400: its own loop restores +0x04 and +0x08
 * per slot from the snapshot (with 0x10 set), and FUN_00233eb8 puts the camera,
 * the lights, the fog and DAT_00355700 back. DAT_00355700's saved copy is the
 * byte at snapshot +0x1DA65, which Ghidra names uGpffffb790.
 *
 * ---------------------------------------------------------------------------
 * The camera
 * ---------------------------------------------------------------------------
 *
 * FUN_00217d70(eye, lookAt) latches cGpffffad2f, saves the current eye/yaw/
 * pitch to DAT_0055F8D8.., sets submode 0x23 and zeroes both interpolation
 * counters -- so FUN_00217b88, which submode 0x23 routes to, has nothing to
 * move and the pose stands. FUN_00217e18 drops the latch, restoring the saved
 * pose when its argument is non-zero.
 *
 * Nothing in states 0x0C..0x15 releases it. The release lives in the item UI:
 * FUN_00234400 -> FUN_00233eb8 restores the whole camera block from the
 * snapshot FUN_002342c0 took on the way in, submode included.
 *
 * ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------------
 *
 *   0x00352958  3.14159  fGpffff89e8  stand behind the chest's facing
 *   0x0035295C  0.372    fGpffff89ec  how far in front of it to stand
 *   0x00352960  0.785398 fGpffff89f0  pi/4, the camera's yaw offset
 *   0x00352964  0.6      fGpffff89f4  how high above the chest to look
 *   0x00352968  3.14159               state 0x13's own copy of pi
 *   0x0035296C  0.3                   ground probe lift on the way out
 *   0x00352970  0.2                   clearance above the ground on the way out
 *   0x00352974  0.001                 the nudge written to player +0x30
 *
 * The camera's 2.5 trail and 0.5 lift are immediates in FUN_00254db0.
 */

void chest_cutscene_state_0C(int player) /* FUN_00254d58 */
{
  FUN_00237b38(0);              /* close the item text window */
  FUN_0025d1c0(1, 0xc, 0);      /* arm the fade to black */
  *(short *)(player + 0x60) = 0xd;
  DAT_0031e190 = 0;
  DAT_0031e194 = 0;
}

void chest_cutscene_state_0D(int player) /* FUN_00254db0 */
{
  int chest;
  float standAngle;
  float cameraAngle;

  if (FUN_0025d238() == 0) /* still fading, or still holding */
  {
    return;
  }

  chest = *(int *)(player + 0x198);

  /* Stand fGpffff89ec in front of the chest, on the far side of its facing. */
  standAngle = FUN_00216690(*(float *)(chest + 0x5c) + fGpffff89e8);
  *(float *)(player + 0x20) = *(float *)(chest + 0x20) + cosf(standAngle) * fGpffff89ec;
  *(float *)(player + 0x24) = *(float *)(chest + 0x24) + sinf(standAngle) * fGpffff89ec;
  *(float *)(player + 0x28) = *(float *)(chest + 0x28);
  *(float *)(player + 0x4c) = *(float *)(chest + 0x4c);

  FUN_002342c0(); /* the item scene: snapshots the camera, busy-waits on load */

  cameraAngle = FUN_00216690(standAngle + fGpffff89f0);
  FUN_00217e18(0);
  FUN_00217d70(*(float *)(chest + 0x20) + cosf(cameraAngle) * 2.5f,
               *(float *)(chest + 0x24) + sinf(cameraAngle) * 2.5f,
               *(float *)(chest + 0x28) + 0.5f,
               *(float *)(chest + 0x20),
               *(float *)(chest + 0x24),
               *(float *)(chest + 0x28) + fGpffff89f4);

  *(unsigned short *)(chest + 8) &= 0xfffe;
  *(unsigned short *)(chest + 4) &= 0xbfff;
  *(float *)(player + 0x5c) = *(float *)(chest + 0x5c);

  FUN_0025d1c0(0, 0xc, 0); /* arm the fade in from black */
  *(short *)(player + 0x60) = 0xe;
  iGpffffadbc = 6;         /* the cutscene frame loop */
  *(unsigned short *)(player + 4) |= 0x100;
}

void chest_cutscene_state_0E(int player) /* FUN_00254f18 */
{
  if (FUN_0025d2f8() != 0)
  {
    FUN_00225bf0(player, 0xf, 0x57);
    /* +0x06 bit 0x80: hold the timeline on its last keyframe rather than
       looping, so the pose stays put through states 0x10..0x13. */
    *(unsigned short *)(player + 6) |= 0x80;
  }
  FUN_00255ce8(0xff);
}

/*
 * Animation 0x57 is the twelve-keyframe chest open. Read out of grp_0001's
 * animation table (record 0x57 at header +0x0C, timeline at 0x26446):
 *
 *   [ 0] col 604  dur 20
 *   [ 1] col 604  dur 30  trail 0x0100   <-- the event
 *   [ 2] col 605  dur 12
 *   [ 3] col 606  dur 10
 *   [ 4] col 607  dur 10  trail 0x0200
 *   [ 5] col 607  dur 10
 *   [ 6] col 608  dur 16
 *   [ 7] col 609  dur 6
 *   [ 8] col 610  dur 60
 *   [ 9] col 610  dur 30
 *   [10] col 611  dur 40
 *   [11] col 612  dur 10  LAST
 *
 * 254 duration units, which at the nominal tick is 254 frames -- exactly what
 * the port measures between states 0x0F and 0x12.
 *
 * The trailing word lands in entity +0xAA, and FUN_00225c90 raises +0x06 bit 8
 * on the frame a new entry is taken and bit 0 when the last one expires. So
 * "0xAA has 0x100 and +0x06 has 8" is "the animation just reached keyframe 1",
 * which is where the lid comes up and the flag is set.
 */
void chest_cutscene_state_0F(int player) /* FUN_00254f60 */
{
  int chest = *(int *)(player + 0x198);
  unsigned short flags = *(unsigned short *)(player + 6);

  if ((*(unsigned short *)(player + 0xaa) & 0x100) != 0 && (flags & 8) != 0)
  {
    FUN_002663a0(*(unsigned int *)(chest + 0x198)); /* open the chest */

    if (*(short *)(chest + 0x130) < 0)
    {
      /* No item: skip the display entirely. State 0x0F's completion test then
         routes straight to 0x12. */
      *(int *)(player + 0x19c) = 0;
      *(short *)(chest + 0x12a) = 0;
    }
    else
    {
      /* Build the item entity in pool slot 2 (0x0058C260) from the chest's id
         byte + 0x1F1, hang it off the chest's role-1 bone, and record the slot
         in chest +0x12A so states 0x10/0x11/0x13 can find it. */
      (&DAT_003437b8)[*(short *)(chest + 0x130)]++;
      *(int *)(player + 0x19c) = FUN_0025b9e8(0);
      FUN_00229c40(0x58c260, *(short *)(chest + 0x130) + 0x1f1);
      FUN_00225bc8(0x58c260, 4);
      /* ... bone attach and ramp setup ... */
      *(short *)(chest + 0x12a) = 2;
    }
    goto done;
  }

  if ((flags & 1) != 0) /* the animation finished */
  {
    if (*(int *)(player + 0x19c) == 0)
    {
      FUN_00237b38(FUN_0025b9e8(1)); /* "The chest is empty." */
      *(short *)(player + 0x60) = 0x12;
    }
    else
    {
      *(short *)(player + 0x62) = 0;
      *(short *)(player + 0x60) = 0x10;
    }
  }

done:
  FUN_00255ce8(0xff);
}

/*
 * 0x10 counts player +0x62 down -- state 0x0F left it at zero, so this is one
 * frame -- and clears the item entity's +0x08 bit 0 to reveal it.
 *
 * 0x11 is the cross-fade. Both ramps are +0x62 >> 5 stepping four ticks a
 * frame; chest and player +0x134 fall from 0x7F and then both are hidden,
 * while the item's climbs from 3 and is set to *zero* when it tops out --
 * zero being "not fading", which FUN_0020c810:142 turns into 0x80. When the
 * item is solid and the chest is hidden, FUN_00237b38 raises the caption and
 * the state hands to 0x12.
 *
 * == Where the item's model comes from ==
 *
 * The 0x1F1 band shares one descriptor (0x3198E0) and indexes its 0x2C-byte
 * model record by the raw type id: DAT_0031A95C + typeId * 0x2C. Type 0x231
 * reads mesh 0x41, texture 0x171 in slot 0x28, and +0x06 == 'd' -- statically
 * bound. The *mesh* is not in any scene bundle: FUN_00221fd8's model pass walks
 * PTR_DAT_0031FEC8 and pulls each record's mesh through FUN_00221B78, which is
 * the category-4 table of contents -- ITM.BIN.
 *
 * The preview spins because animation 4 of that mesh spins. Nothing writes the
 * item's +0x5C after state 0x0F copies the chest's facing into it.
 *
 * == The two messages ==
 *
 * Both branches put a window up. FUN_0025b9e8(index) reads dword 5 of the
 * item-database blob -- SCR.BIN resource 1 -- as a table of message-stream
 * offsets, and FUN_00254f60 picks index 0 with an item and index 1 without:
 *
 *     index 0    1B 09 05 | 14 <id> | 01
 *     index 1    1B 09 05 | 07 | "The chest is empty." | 01
 *
 * The codes, from the 31-entry handler table at 0x0031C640:
 *
 *   0x1B  FUN_00239AA0  set the event flag in the next *two* bytes. 0x0509
 *                       here, which FUN_002391D0 tests before it draws
 *                       anything. (0x1C is the same handler's clear path.)
 *   0x07  FUN_00239368  FUN_00238F98, a new line
 *   0x14  FUN_002397F0  splice in an item name, one operand byte. It calls
 *                       FUN_00229820(id, 0, &cursor, 0), which repoints the
 *                       stream cursor at the item's name string and prints it
 *                       inline before restoring the cursor
 *   0x01  FUN_002391D0  raise the prompt, wait for Cross, close
 *
 * 0x1B taking two operands is what makes the leading `1B 09 05` readable: as
 * three codes it would look like a prompt-and-wait before any text exists.
 *
 * FUN_00237CA0(stream, 0x14) finds the 0x14 and state 0x0F writes the chest's
 * id byte into its operand, so even the item caption is one control code plus a
 * table lookup rather than authored text.
 *
 * The empty line sits one row lower than the item line: FUN_00238F18 clears
 * iGpffffbcd8 when the window opens, and only stream 1 leads with 0x07.
 *
 * player +0x19C holds the *stream pointer*, not an item id. State 0x0F stages
 * FUN_0025b9e8(0) there with the id already patched in and state 0x11 opens it;
 * the no-item branch opens FUN_0025b9e8(1) directly and leaves +0x19C at zero,
 * which is exactly the test the completion branch reads.
 *
 * The name table is SCR.BIN resource 1 (FUN_00228e28:119). Its dword 8 points
 * at per-group triples; group 0's [1] and [2] are u32 -> u16 -> string chains
 * for names and descriptions, and the strings are plain ASCII. Item 64 is
 * "Blue Lantern".
 *
 * The line is drawn in the dialogue font -- texture 0x173 in slot 0x2E, eleven
 * columns of 22x22 cells, proportional widths measured at boot by FUN_00238c90
 * -- from the window origin FUN_00237b38 sets at entry (-0x130, -0x78), which
 * is screen (16, 344) on the 640x448 overlay screen. FUN_002391d0 puts the
 * flipping-book prompt after it, out of slot 0x2A at 15x15 drawn to 20x22,
 * cycling the four cells at (96,32), (96,48), (112,48), (112,32).
 *
 * == Sound ==
 *
 * 0x00255240, in state 0x11's item branch, is `jal FUN_00257b10` on the frame
 * the caption goes up -- FUN_00267d38(8, player), the item fanfare. It is the
 * only sound call in the whole 0x0C..0x15 range; the chest's own lid cue, 0x9F,
 * comes from FUN_002d1ea8. See analyzed/sound_effect_playback.c.
 *
 * == The camera does not stand still ==
 *
 * States 0x0D..0x0F never move the camera again after FUN_00254db0 installs
 * it. The swing during the lid animation belongs to the *chest*: FUN_002d1ea8
 * sees its own 0x100 keyframe, checks that it has contents and that a script
 * camera is installed, and hands FUN_00217fe8 a three-second spline that
 * orbits 135 degrees and zooms 3x. See
 * analyzed/actor_behaviors/type_0x3A_treasure_chest.c. An empty chest fails
 * the contents test, which is why the no-item cutscene looks static.
 */

void chest_cutscene_state_12(int player) /* 0x00255260 */
{
  if (FUN_00237c60() == 0) /* the item window has closed */
  {
    FUN_0025d1c0(1, 0xc, 0xffffff); /* arm the fade to white */
    *(short *)(player + 0x60) = 0x13;
  }
  FUN_00255ce8(0xff);
}

void chest_cutscene_state_13(int player) /* 0x002552b0 */
{
  int chest;
  float angle;
  float reach;
  float offsetX;
  float offsetY;
  float ground;

  if (FUN_0025d238() == 0)
  {
    goto done;
  }

  FUN_00234400(); /* tear the item scene down; restores the camera block */

  chest = *(int *)(player + 0x198);
  if (*(short *)(chest + 0x12a) != 0)
  {
    FUN_00265ec0(0x58beb0 + *(short *)(chest + 0x12a) * 0x1d8);
  }

  /* Clearing +0x94 makes FUN_002d1ea8 run its first-tick branch again, which
     reads the event flag and settles on animation 6 -- open. */
  *(char *)(chest + 0x94) = 0;

  FUN_00225bf0(player, 0x14, 1);
  *(unsigned short *)(player + 8) = (*(unsigned short *)(player + 8) & 0xfffe) | 0x10;
  *(unsigned short *)(player + 4) &= 0xfeff;

  /* Push the player clear of the chest: one radius sum out along the chest's
     back, plus a second chest radius when both axes are actually moving. */
  angle = FUN_00216690(*(float *)(chest + 0x5c) + 3.14159f);
  reach = *(float *)(chest + 0x54) + *(float *)(player + 0x54);
  offsetX = reach * cosf(angle);
  offsetY = reach * sinf(angle);
  if ((int)(offsetX * 1000.0f) != 0 && (int)(offsetY * 1000.0f) != 0)
  {
    offsetX += *(float *)(chest + 0x54) * cosf(angle);
    offsetY += *(float *)(chest + 0x54) * sinf(angle);
  }

  *(float *)(player + 0x20) = *(float *)(chest + 0x20) + offsetX;
  *(float *)(player + 0x24) = *(float *)(chest + 0x24) + offsetY;
  ground = FUN_00227798(*(float *)(player + 0x20),
                        *(float *)(player + 0x24),
                        *(float *)(player + 0x28) + 0.3f);
  *(float *)(player + 0x30) = 0.001f;
  *(float *)(player + 0x4c) = ground;
  *(float *)(player + 0x28) = ground + 0.2f;

  FUN_002241d8();        /* iGpffffadbc = 0, back to the field frame */
  DAT_00355658 = 1.0f;

done:
  FUN_00255ce8(0xff);
}

void chest_cutscene_state_14(int player) /* 0x00255448 */
{
  FUN_0025d238();

  /* +0x0C bit 0 is grounded: the player has fallen the 0.2 clearance state
     0x13 gave them, so the screen can come back. */
  if ((*(unsigned int *)(player + 0xc) & 1) != 0)
  {
    FUN_0025d1c0(0, 0xc, 0xffffff);
    *(short *)(player + 0x60) = 0x15;
  }
}

void chest_cutscene_state_15(int player) /* FUN_00255498 */
{
  if (FUN_0025d2f8() != 0)
  {
    FUN_00252d88(player);
    *(short *)(player + 0x1b8) = 0xa0;
  }
}
