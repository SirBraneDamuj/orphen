#include "orphen_globals.h"

/**
 * The debug overlay's text pipeline, and POSITION_DISP on top of it.
 *
 *   FUN_002681c0  0x002681C0  debug_printf: append to the overlay buffer
 *   FUN_00268270  0x00268270  lay the buffer out as glyphs, once a frame
 *   FUN_00268410  0x00268410  one glyph, as a GS sprite
 *   FUN_002685e8  0x002685E8  strlen, used by the '~' escape
 *   FUN_002239c8  0x002239C8  the frame function; its cGpffffb128 block is
 *                             POSITION_DISP, the readout FUN_00268d30 toggles
 *
 * State:
 *   DAT_00572c38  the text buffer, capped at 0x800
 *   DAT_003551dc  how much of it is in use
 *   DAT_003555da  cGpffffb66a, the debug-active byte
 *   DAT_003555dc  cGpffffb66c, the output gate
 *   DAT_00355098  cGpffffb128, the POSITION_DISP toggle
 *
 * == Coordinates ==
 *
 * FUN_00268270 works in the units it hands FUN_00268410, which negates the
 * vertical before passing it on. FUN_00207938 then writes x at <<4 and y at
 * <<3, so with the shipped GS geometry (SCISSOR_1 640x224, XYOFFSET_1 centred
 * on 320 x 112) one x unit is one framebuffer pixel and one y unit is half of
 * one -- the framebuffer is a field, displayed at 448 lines. On a 640x448
 * screen that is screenX = 320 + x, screenY = 224 - y: a 16-pixel left margin,
 * a first line at y = 8, a 10x20 glyph cell, a 12-pixel advance and a 20-pixel
 * line pitch, wrapping once x passes 304.
 *
 * == The glyph atlas ==
 *
 * FUN_00268410 textures each quad from a 7x15 texel window at
 * (((c - 0x20) & 0x1F) * 8 + 1, ((c - 0x20) >> 5) * 16 + 1) of texture slot
 * 0x30 -- an 8x16 cell grid, 32 columns, three rows covering 0x20..0x7F, so a
 * 256x48 band. FUN_00221fd8 binds slot 0x30 to texture 0x179, and the s01_e024
 * EE dump agrees: DAT_003429a8[0x30] == 0x179.
 *
 * 0x179 is a 256x256 BMPA sheet shared with the chest and title art, carried
 * both by TEX.BIN and by the s00_e000 boot bundle. **Its font band is at the
 * bottom in storage order**: GS v = 0 is stored row 255, so the sheet has to be
 * flipped vertically before the window coordinates mean anything. Read
 * unflipped, rows 0..47 hold particle sprites from the sheet's other end.
 *
 * Note this is a *different* font from the dialogue one, which
 * render_text_with_scaling.c (FUN_00238608) draws from slots 0x2E / 0x2F on an
 * 11-column grid of 22x22 cells.
 */

/* ------------------------------------------------------------------------ */
/* FUN_002681c0 -- append formatted text to the overlay buffer.             */
/* ------------------------------------------------------------------------ */
void debug_printf(const char *format, ...)
{
  char formatted[4096];
  int length;

  if (DAT_003555dc_output_enabled == '\0')
  {
    return;
  }
  if (DAT_003555da_debug_active == '\0')
  {
    /* Latches the gate off rather than merely dropping this call. */
    DAT_003555dc_output_enabled = '\0';
    return;
  }

  length = vsprintf(formatted, format, varargs); /* FUN_0030e0f8 */

  /* Tested before appending, so an append that would cross the cap is
     dropped whole rather than truncated. */
  if (DAT_003551dc_length + length < 0x800)
  {
    formatted[length] = '\0';
    strcat_at(&DAT_00572c38_text + DAT_003551dc_length, formatted); /* FUN_00268558 */
    DAT_003551dc_length += length;
  }
}

/* ------------------------------------------------------------------------ */
/* FUN_00268270 -- place one glyph per printable character, then drain.     */
/* ------------------------------------------------------------------------ */
void debug_text_layout(void)
{
  int x;
  int y;
  unsigned char *cursor;
  unsigned int character;

  /* An unrelated status line rides the same buffer: a countdown at
     DAT_00355074 emits the string at 0x00573438 as it crosses 0x100. */
  if ((DAT_00355074 != 0) && (DAT_00355074 -= 0x20, (DAT_00355074 & 0x100) != 0))
  {
    debug_printf((char *)0x573438);
  }

  if (DAT_003551dc_length == 0)
  {
    return;
  }

  x = -0x130; /* left margin: screen x 16 */
  y = 0xd8;   /* first line:  screen y 8 (448-space) */

  cursor = &DAT_00572c39;
  character = DAT_00572c38_text;

  while (character != 0)
  {
    if ((character == '\r') || (character == '\n'))
    {
      x = -0x130;
      y -= 0x14;
    }
    else if (character == '~')
    {
      /* Jump to the bottom line and right-align on x = 0x140. The length it
         aligns by is the rest of the *buffer*, not the token that follows --
         FUN_002685e8 gets the raw cursor. */
      y = -0xbe;
      x = string_length(cursor) * -0xc + 0x140;
    }
    else if (character - 0x20 < 0x60)
    {
      if (character != ' ')
      {
        debug_draw_glyph(character, x, y); /* FUN_00268410 */
      }
      x += 0xc;
      if (0x130 < x)
      {
        x = -0x130;
        y -= 0x14;
      }
    }
    /* Anything outside 0x20..0x7F is skipped without advancing. */

    character = *cursor;
    cursor++;
  }

  DAT_003551dc_length = 0;
}

/* ------------------------------------------------------------------------ */
/* FUN_00268410 -- one glyph.                                               */
/* ------------------------------------------------------------------------ */
void debug_draw_glyph(unsigned int character, int x, int y)
{
  unsigned int cell = (character & 0xff) - 0x20;

  /* Ghidra's 12-parameter prototype for FUN_00207938 is right; the call site
     passes eight in registers and four on the stack. Reading the disassembly
     at 0x00268410 gives, in order:
       mode 0, z -0x1009, x, -y, width 10, height 0x14, texture slot 0x30,
       u0 = (cell & 0x1F) * 8 + 1, v0 = (cell >> 5) * 16 + 1,
       uw 7, vh 15, colour 0x80808080 (x1.0 through the GS's (Ct * Cv) >> 7). */
  FUN_00207938(0, -0x1009, x, -y, 10, 0x14, 0x30,
               ((cell & 0x1f) << 3) | 1,
               ((int)cell >> 5) * 0x10 + 1,
               7, 0xf, 0x80808080);
}

/* ------------------------------------------------------------------------ */
/* FUN_002239c8 lines 140-165 -- POSITION_DISP.                             */
/* ------------------------------------------------------------------------ */
void debug_position_display(void)
{
  if (DAT_00355098_position_disp == '\0')
  {
    return;
  }

  /* Detailed when debug output is already on; compact when debug is active
     but output is off, which is the state the menu leaves behind. */
  if ((DAT_003555da_debug_active == '\0') || (DAT_003555dc_output_enabled != '\0'))
  {
    print_entity_position(0x58beb0); /* FUN_00269fa8: lead player's +0x20 */
    debug_printf("MF:%08X AF:%04X SF:%04X NF:%04X\n",
                 DAT_0058bebc,  /* entity +0x0C */
                 DAT_0058beb4,  /* entity +0x04 */
                 DAT_0058beb8,  /* entity +0x08 */
                 DAT_0058beb6); /* entity +0x06 */
    debug_printf("tPOS>");
    print_position(DAT_0058be90, DAT_0058be94, DAT_0058be98); /* camera look-at */
    debug_printf("cPOS>");
    print_entity_position(0x58c088); /* the camera entity, +0x20 */
    debug_printf("MAP>(MP%02d%02d)\n", DAT_003551f4, DAT_003551f0);
  }
  else
  {
    DAT_003555dc_output_enabled = 1;
    if (DAT_003555d3_background_scene == '\0')
    {
      debug_printf("~MP%02d%02d", DAT_003551f4, DAT_003551f0);
    }
    else
    {
      int table = FUN_0022a238(0xd);
      debug_printf("~BG%03d", *(unsigned short *)(table + DAT_003551f8 * 0x10));
    }
    /* Note the tighter format here: no spaces after the commas. */
    debug_printf("(%d,%d,%d)\n",
                 (int)(DAT_0058bed0 * 1000.0f),
                 (int)(DAT_0058bed4 * 1000.0f),
                 (int)(DAT_0058bed8 * 1000.0f));
    DAT_003555dc_output_enabled = '\0';
  }
}

/*
 * FUN_00269fa8 / FUN_0026a048 are the same "(%d, %d, %d)\n" of a triple scaled
 * by 1000 and truncated (FUN_0030bd20); the first takes an entity pointer and
 * reads its +0x20/+0x24/+0x28. Both sprintf into a 256-byte stack buffer and
 * hand *that* to debug_printf as the format string, which is why a position
 * containing a '%' would be re-expanded -- it never can, the text is digits.
 *
 * Read against the s01_e024 EE dump, the detailed readout is:
 *
 *   (-3250, -12750, 0)
 *   MF:00003015 AF:3024 SF:0026 NF:0000
 *   tPOS>(-3250, -12750, 800)
 *   cPOS>(-6101, -12750, 875)
 *   MAP>(MP0124)
 *
 * DAT_003551f4 = 1 and DAT_003551f0 = 0x18 there, which is the MCB section and
 * entry -- the same pair the port names s01_e024.
 */
