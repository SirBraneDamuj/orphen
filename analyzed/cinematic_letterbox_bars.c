/*
 * The cinematic letterbox bars
 * Original: FUN_0025cfb8 (0x0025cfb8)  the two sprites, and the slide
 *           FUN_0025fd10 (0x0025fd10)  opcode 0x6D, the only thing that arms them
 *
 * Called from FUN_0025b778:57, the per-frame scene-script entry, and again from
 * FUN_00224320. Two flat black sprites, full width, one hard against the top
 * edge of the frame and one hard against the bottom, both the same height. The
 * bars are part of the game's picture -- GS primitives inside the 640x224
 * field -- not a border drawn around it.
 *
 * ---------------------------------------------------------------------------
 * The two globals
 * ---------------------------------------------------------------------------
 *
 * With gp = 0x00359F70:
 *
 *     iGpffffb0e4 == DAT_00355054   mode: 0 off, 1 opening/open, -1 closing
 *     iGpffffbd8c == DAT_00355CFC   level, a 0..0x780 ramp
 *     iGpffffb64c == DAT_003555BC   the per-frame tick count
 *
 * Opcode 0x6D is the only writer of either. Its signed byte operand:
 *
 *     -1   mode = 1, level = 0       bars slide in
 *     -2   mode = 1, level = 0x780   bars already closed
 *      1   mode = -1                 bars slide back out
 *
 * and the mode is cleared when the level reaches the bottom. FUN_00271220,
 * FUN_0022b300 and FUN_00225340 all zero the mode at a scene change.
 *
 * (analyzed/ops/0x6D_control_character_ai_mode.c used to call this pair an
 * "alignment/positioning mode" and uGpffffbd8c a "timing parameter". Both
 * names were guesses and both are wrong; this is the letterbox.)
 *
 * ---------------------------------------------------------------------------
 * FUN_0025cfb8
 * ---------------------------------------------------------------------------
 *
 *     if (mode == 0) return;
 *
 *     height = level >> 5;                    // biased first, so it rounds
 *                                             // toward zero for a negative
 *     entry = scratchpad_alloc(0x10 words);
 *     entry[0]   = 0xFFFFFFFF;                // FUN_00239020's untextured path
 *     entry[1]   = 0xFFFFEFF9;                // negative => GS sort bucket 0x1007
 *     entry[2]   = 0xFFFFFEC0;                // x = -320, the left edge
 *     entry[4]   = 0x280;                     // width 640, the full width
 *     entry[5]   = height;
 *     entry[0xA] = 0;                         // no flip
 *     entry[0xB] = 0;                         // no alpha blending
 *     entry[0xC] = 0xFF000000;                // RGBAQ: opaque black
 *
 *     entry[3] = height - 0xE0;  FUN_00239020(entry);   // the bottom bar
 *     entry[3] = 0xE0;           FUN_00239020(entry);   // the top bar
 *
 *     if (mode < 1) {                         // closing
 *       if (level > 0) {
 *         level -= frameTicks * 8;
 *         if (level < 1) { level = 0; mode = 0; }
 *       }
 *     } else if (level < 0x780) level += frameTicks * 8;
 *     else                      level  = 0x780;
 *
 * The sprites are drawn from the level as it stands and the level is advanced
 * afterwards, so the frame a -1 operand lands on draws nothing.
 *
 * Speed: 0x780 / (0x20 * 8) = 7.5 frames at the nominal tick rate.
 *
 * ---------------------------------------------------------------------------
 * Geometry
 * ---------------------------------------------------------------------------
 *
 * FUN_00207938 writes x at << 4 and y at << 3 about the 2048-pixel GS centre,
 * so the entry coordinates address a 640x448 virtual screen whose y unit is
 * half a scanline of the 224-line field -- the field as it looked once the
 * display stretched it back to 4:3. FUN_00239020 negates y on the way in:
 *
 *     screenX = entryX + 320
 *     screenY = 224 - entryY          and the sprite extends `height` downward
 *
 * So entry y = height - 224 is the bottom bar (screen y 448 - height) and
 * entry y = 224 is the top bar (screen y 0). A full level gives height 60, or
 * 30 scanlines each, leaving a 328-unit picture between the bars.
 *
 * ---------------------------------------------------------------------------
 * Draw order
 * ---------------------------------------------------------------------------
 *
 * FUN_00207938 head-inserts each packet into one of the 0x1010 sort buckets at
 * DAT_7000000C, which FUN_00200c48 chains in ascending order -- so a higher
 * bucket draws later, and within one bucket a later submission draws earlier.
 *
 *     0x1007  the letterbox bars, and the full-screen fade (FUN_0025d0e0 calls
 *             FUN_00207de8(0x1007)). Both FUN_002239c8 and FUN_00224320 submit
 *             the fade first, so the bars draw first and the fade tints them.
 *     0x1009  the dialogue glyphs (FUN_00237b38 seeds every slot's word 1 with
 *             0xFFFFEFF7) and the debug text (FUN_00268410 passes the same).
 *             Both draw over the bars.
 *
 * A normal sprite passes a non-negative word 1 and lands in bucket
 * `word1 >> 4` clamped to 2..0xFFF, so nothing in the scene can reach either of
 * these two.
 *
 * ---------------------------------------------------------------------------
 * What else reads the mode
 * ---------------------------------------------------------------------------
 *
 * FUN_00238a08:36-45 (and FUN_00239848 and FUN_00239b00, the same test in the
 * choice-list and speaker paths) moves a dialogue glyph clear of whichever bar
 * its window would run into, while the mode is positive:
 *
 *     window origin y == 0xD0   (the top window)     entry y -= 0x2D
 *     window origin y == -0x78  (the bottom window)  entry y += 0x1E
 *
 * A larger entry y is further up the screen, so the bottom window rises: its
 * third line then ends at screen y 380, eight units above the 388 the bottom
 * bar starts at. It is a live read taken as each glyph is enqueued, not a value
 * latched when the record opened.
 *
 * FUN_00216aa0 and FUN_00224ff0 also test the mode, as "a movie is playing":
 * the field camera's look toggle and the pause menu are both refused while it
 * is set. FUN_0020c5a8 copies it into a VU1 packet byte at +0x200.
 *
 * PS2-specific notes
 * - DAT_70000000 is the scratchpad allocation cursor; the entry is a 64-byte
 *   temporary that FUN_00239020 turns into a GIF packet and never outlives the
 *   call. FUN_0026bf90 is the overflow abort at 0x70003FFF.
 * - The colour is written straight into RGBAQ, where 0x80 is x1.0 through the
 *   GS's (Ct * Cv) >> 7. 0xFF000000 is black at alpha 0xFF, and with entry
 *   +0x2C zero there is no alpha blending, so the bars are opaque.
 */

#include <stdint.h>

extern int DAT_00355054; // iGpffffb0e4, mode
extern int DAT_00355cfc; // iGpffffbd8c, level
extern int DAT_003555bc; // iGpffffb64c, per-frame tick count

extern void FUN_00239020(int *entry); // one entry -> one GS sprite

#define LETTERBOX_FULL_LEVEL 0x780
#define LETTERBOX_SCREEN_HALF_HEIGHT 0xE0 /* 224, in entry units */

void cinematic_letterbox_bars(int *entry) // FUN_0025cfb8
{
  int height;

  if (DAT_00355054 == 0)
  {
    return;
  }

  height = DAT_00355cfc;
  if (height < 0)
  {
    height += 0x1F; // round toward zero before the arithmetic shift
  }
  height >>= 5;

  entry[0x00] = -1;                   // untextured
  entry[0x01] = -0x1007;              // sort bucket 0x1007, z = 0xFFFF
  entry[0x02] = -0x140;               // x = -320
  entry[0x04] = 0x280;                // width 640
  entry[0x05] = height;
  ((unsigned char *)entry)[0x28] = 0; // no flip
  entry[0x0B] = 0;                    // no alpha blending
  entry[0x0C] = 0xFF000000;           // opaque black

  entry[0x03] = height - LETTERBOX_SCREEN_HALF_HEIGHT; // bottom bar
  FUN_00239020(entry);
  entry[0x03] = LETTERBOX_SCREEN_HALF_HEIGHT; // top bar
  FUN_00239020(entry);

  if (DAT_00355054 < 1)
  {
    // Closing. Note the guard: a mode of -1 sitting on a level of 0 stays -1.
    if (DAT_00355cfc > 0)
    {
      DAT_00355cfc -= DAT_003555bc * 8;
      if (DAT_00355cfc < 1)
      {
        DAT_00355cfc = 0;
        DAT_00355054 = 0;
      }
    }
  }
  else if (DAT_00355cfc < LETTERBOX_FULL_LEVEL)
  {
    DAT_00355cfc += DAT_003555bc * 8;
  }
  else
  {
    DAT_00355cfc = LETTERBOX_FULL_LEVEL;
  }
}
