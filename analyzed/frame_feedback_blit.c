/*
 * frame_feedback_blit — the screen smear
 *
 * Original: FUN_00201a38 (0x00201a38)
 * Caller:   FUN_002000c0:214, the main loop:
 *
 *     if ((DAT_00355661 != '\0') || (DAT_00354b88 != 0)) {
 *       FUN_00201a38();
 *     }
 *
 * Draws the *previous* frame back over the current one, alpha-blended. Because
 * each frame re-samples a frame that already contains the last blend, the
 * result compounds into an exponential trail rather than a single ghost — that
 * is the distorted/blurry smear scripts reach for on impacts, lightning and
 * sword swings.
 *
 * The source is a real framebuffer, not a copy. FUN_002f9620 builds TEX0 with
 *
 *     TBP0 = iGpffffacbc * 0x8C0,  TBW = 10,  PSM = 1 (PSMCT24),
 *     TW = 10, TH = 8, TFX = 0 (MODULATE)
 *
 * `iGpffffacbc` is DAT_00354C2C, which FUN_002000c0 sets to `DAT_00354c28 & 1`
 * while the draw environment goes to the *other* parity — so TBP0 names the
 * buffer being displayed, i.e. the finished previous frame. TBW 10 is 640
 * pixels and the two bases 0 and 0x8C000 are 640*224*4 apart, so the frame is
 * one 224-line field.
 *
 * FUN_00201a38 then clears two things in the TEX0 word it was handed
 * (`& 0xE07FFFFBFFFFFFFF`): bit 34, TCC, so the sampled alpha is discarded and
 * RGBAQ's is used instead; and bits 55..60, the CLUT fields, which PSMCT24 has
 * no use for.
 *
 * The packet is four A+D pairs — TEX1_1, TEX0_1, CLAMP_1 (0, so REPEAT) and
 * ALPHA_1 — followed by a 10-register REGLIST of
 * PRIM, RGBAQ, then four (UV, XYZF2) pairs:
 *
 *     PRIM    0x155   TRI_FAN | TME | ABE | FST
 *     ALPHA_1 0x44    (Cs - Cd) * As + Cd
 *     RGBAQ   alpha << 24 | 0x808080
 *
 * 0x80 is unity for MODULATE on the GS, so the RGB passes the texture through
 * untouched and alpha is the entire effect. Alpha is a blend factor over 128:
 * 0x80 replaces the frame outright, and the 0x7E scripts favour is 98%.
 *
 * The finished packet is head-inserted at DAT_7000000C + 0x10064. FUN_00207de8's
 * tail shows the bucket stride is 0x10 with the head pointer at +4, so that is
 * sort bucket 0x1006 — under the letterbox bars and the fullscreen fade (both
 * 0x1007) and under every text overlay (0x1009). The smear covers the world and
 * the fade tints the smear.
 *
 * Alpha ramp. DAT_00355661 is the target and DAT_00354B88 (`sGpffffac18`, the
 * same address the gate in the main loop reads as the second condition) is the
 * current level. Note the branch order below: while the current level is zero
 * the function takes the target verbatim and never ramps at all. Only opcode
 * 0xBE's table entry 12 (`sh a0,-0x53e8(gp)`) ever seeds a non-zero current
 * level, so every VM caller — which is every script use of 0xC8 and 0xC9 — gets
 * exactly the alpha it wrote and does its own fading with an 0x90/0x91/0x92
 * parameter ramp.
 *
 * Geometry. The base quad is +/-5104 x +/-1776 in GS 12.4 fixed point, which is
 * +/-319 x +/-111 pixels, and the UVs it carries are the 1..639 x 1..223
 * rectangle of the source. Untransformed, that is a 1:1 copy — which pins the
 * two origin constants the function adds last: 0x7FF8 and 0x7FFE *are* the
 * screen centre, and 0x7FF6 is the same point half a line up for the other
 * interlace field (`iGpffffacc4` is DAT_00354C34, zero in both EE dumps).
 *
 * Only the destination is transformed; the source rectangle never moves. So the
 * transform stretches the picture rather than panning across it.
 *
 * The doubling and halving of y in the rotate and offset steps is the field
 * correction: the quad is 224 lines over 640 pixels, so a line is worth two
 * pixels and a 90-degree spin would otherwise come out squashed.
 *
 * fGpffff8010 is 0.017453289 (pi/180), so DAT_00343880 is tenths of a degree.
 * FUN_00305218 is sinf and FUN_00305130 is cosf — for |x| < pi/4 they tail into
 * __kernel_sin (FUN_00308258) and __kernel_cos (FUN_00307820) respectively.
 * FUN_0030bd20 is float -> int, truncating toward zero.
 *
 * Written up in port/src/ported/render/original_frame_feedback.{h,cpp}, which
 * ports everything here except the packet.
 */

#include <math.h>
#include <stdint.h>

/* Target and current blend alpha. */
extern unsigned char DAT_00355661; /* bGpffffb6f1 */
extern short DAT_00354b88;         /* sGpffffac18 */

/* Opcode 0xC9's five transform shorts. */
extern short DAT_00343878; /* offset X */
extern short DAT_0034387a; /* offset Y */
extern short DAT_0034387c; /* scale X */
extern short DAT_0034387e; /* scale Y */
extern short DAT_00343880; /* rotation, tenths of a degree */

extern int DAT_00354c2c; /* iGpffffacbc — parity of the displayed buffer */
extern int DAT_00354c34; /* iGpffffacc4 — interlace field */

extern float DAT_00351f80; /* fGpffff8010 — pi/180 */

extern float sinf_(float);  /* orig FUN_00305218 */
extern float cosf_(float);  /* orig FUN_00305130 */
extern int   to_int(float); /* orig FUN_0030bd20 — truncate toward zero */

/* Returns the alpha to draw with, or -1 for a frame that draws nothing. */
static int frame_feedback_step_alpha(void)
{
  int target = (int)DAT_00355661;
  int current;

  /* No ramp and no cutoff while the current level is zero. */
  if (DAT_00354b88 == 0)
  {
    return target;
  }

  current = (int)DAT_00354b88;
  if (current < target)
  {
    current++;
    if (target < current)
    {
      current = target;
    }
  }
  else if (current > target)
  {
    current--;
    if (current < target)
    {
      current = target;
    }
    /* Floors at 1, not 0: reaching zero would drop back into the branch above
     * and start taking the target verbatim again. */
    if (current < 1)
    {
      current = 1;
    }
  }
  DAT_00354b88 = (short)current;

  return current < 2 ? -1 : current;
}

/* Vertex order is the order the packet emits its four (UV, XYZ) pairs. */
struct feedback_vertex
{
  int x;
  int y;
  int u; /* 12.4 fixed, over the source frame */
  int v;
};

/*
 * The whole of FUN_00201a38 apart from the DMA/GIF packet build: everything
 * that decides *what* is drawn.
 */
void frame_feedback_build(struct feedback_vertex out[4], int *alpha_out)
{
  struct feedback_vertex quad[4] = {
      {-5104, -1776, 0x0010, 0x0010},
      {-5104, +1776, 0x0010, 0x0DF0},
      {+5104, +1776, 0x27F0, 0x0DF0},
      {+5104, -1776, 0x27F0, 0x0010},
  };
  int index;

  *alpha_out = frame_feedback_step_alpha();
  if (*alpha_out < 0)
  {
    return;
  }

  if (DAT_0034387c != 0 || DAT_0034387e != 0)
  {
    for (index = 0; index < 4; index++)
    {
      int px = quad[index].x * ((int)DAT_0034387c + 0x400);
      int py = quad[index].y * ((int)DAT_0034387e + 0x400);
      quad[index].x = (px < 0 ? px + 0x3FF : px) >> 10;
      quad[index].y = (py < 0 ? py + 0x3FF : py) >> 10;
    }
  }

  if (DAT_00343880 != 0)
  {
    float radians = ((float)DAT_00343880 / 10.0f) * DAT_00351f80;
    float s = sinf_(radians);
    float c = cosf_(radians);
    for (index = 0; index < 4; index++)
    {
      float x = (float)quad[index].x;
      float y = (float)quad[index].y + (float)quad[index].y;
      quad[index].x = to_int(x * c - y * s);
      quad[index].y = to_int(x * s + y * c) / 2;
    }
  }

  if (DAT_00343878 != 0 || DAT_0034387a != 0)
  {
    int offset_y = ((int)DAT_0034387a - ((int)DAT_0034387a >> 31)) >> 1;
    for (index = 0; index < 4; index++)
    {
      quad[index].x += (int)DAT_00343878;
      quad[index].y += offset_y;
    }
  }

  /* The screen centre. 0x7FF6 is the same point half a line up, for the other
   * interlace field. */
  for (index = 0; index < 4; index++)
  {
    quad[index].x += 0x7FF8;
    quad[index].y += DAT_00354c34 == 0 ? 0x7FFE : 0x7FF6;
    out[index] = quad[index];
  }
}
