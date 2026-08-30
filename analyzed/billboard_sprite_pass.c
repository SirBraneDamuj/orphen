/*
 * The Billboard Sprite Pass -- the second entity draw
 *
 * Original functions:
 *   FUN_0020f3e0  0x0020f3e0  the pool walk
 *   FUN_0020f510  0x0020f510  build and submit one entity's quads
 *
 * There are TWO entity draw passes and they partition the pool rather than
 * filtering it. FUN_0020c5a8, the skeletal one, refuses any entity whose +0x02
 * carries bit 0x200; FUN_0020f3e0 then walks the same 256 slots for exactly
 * those. A character is never a candidate for this pass and an effect entity is
 * never a candidate for the other, so an entity reported as "not drawn" by the
 * skeletal walk is not a bug -- it is the wrong pass looking.
 *
 * FUN_00225c90 branches on the same bit at its first line, so the model kind and
 * the draw kind are chosen by one flag.
 *
 * THE MODEL. No PSC3 magic. grp_017e (the magic projectile's) is 324 bytes:
 *
 *   +0x00 u16  column count            (12)
 *   +0x02 u16  animation count         (2)
 *   +0x04 u32  column table: {u16 firstRecord, u16 recordCount} per column
 *   +0x08 u32  sprite record table, 16 bytes each
 *   +0x0C u32  animation table, one u32 timeline offset per animation
 *
 * The animation table sits at the same header offset a PSC3's does, which is
 * what lets FUN_00225c90 read entity +0x9C the same way for both kinds.
 *
 * THE RECORD, 16 bytes:
 *
 *   +0x00 u8   bits 0-1 blend mode; 0x10 flip X; 0x20 flip Y
 *   +0x01 u8   texel origin U
 *   +0x02 u8   texel origin V
 *   +0x03 u8   width, ONE MORE than the real extent
 *   +0x04 u8   height, likewise
 *   +0x05 u8   twice the alpha byte
 *   +0x06 s8   X offset, in whole sprite units
 *   +0x07 s8   Y offset
 *   +0x08 s8   depth bias; over 100, added to the view depth
 *   +0x09 u8   mip bias, only read when the texture slot is above 0x17
 *   +0x0C f32  scale
 *
 * Records in a column's run are drawn BACK TO FRONT: FUN_0020f510 seeds its
 * cursor at the last record and walks down to the first.
 *
 * THE DEPTH is keyed off a value the screen position never sees:
 *
 *   depth = viewZ + (char)entity+0x133 * DAT_003520a0   (0.08)
 *                 + (char)record+0x08 / 100.0
 *   gsZ   = trunc(DAT_003555a4 / depth + DAT_003555a0), floored at 0
 *
 * Both biases move the sprite in depth only -- the quad's position and size
 * came out of projScale, which used the unbiased viewZ. Entity +0x133 is copied
 * from descriptor +0x02 by FUN_00229c40:75 and most effect descriptors carry
 * -12, so the usual effect sits 0.96 units nearer the camera than it stands.
 *
 * Entity +0x08 bit 0x40 replaces all of that with a flat gsZ of 0xFFFF, which
 * is nearer than anything the world writes, and buckets the packet at 0x1005.
 *
 * THE ORDER is a 4096-bucket display list keyed on gsZ >> 4, clamped to
 * 1..0xFFF, plus the 0x1005 bucket above it. gsZ rises as a sprite comes
 * nearer, so ascending bucket is back to front. Within one bucket packets are
 * linked in at the head, so co-bucketed sprites come out reversed among
 * themselves.
 *
 * PORTED 2026-08-30 into port/src/ported/render/original_sprite_pass.{h,cpp}
 * (the record parse and the quad build), PortRuntime::publishSpriteQuads (the
 * pool walk) and MapViewer::drawSpriteQuads (the submit). The blob parse is
 * loadSpriteStripModel in port/src/ported/model/psc3_model.cpp, and
 * FUN_00225c90's matching branch is FUN_00225c90_advance_sprite_strip.
 *
 * NOT PORTED, both flagged at their sites: the rotation branch (+0x08 bit
 * 0x400, which rebuilds the quad as four independently rotated corners off the
 * angle table at entity +0x168, and only for runs of fewer than nine records),
 * and the HUD branch (+0x08 bit 0x1000, which takes a position already in
 * screen space instead of projecting one).
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * FUN_0020f3e0 -- the pool walk.
 *
 * The staged colour is the interesting part: max(ambient, light 0) per channel,
 * byte-reversed out of the two packed globals. FUN_0020f510 averages it with the
 * VU0 point-light contribution.
 */
void draw_billboard_entities(void)
{
  byte workspace[116];
  int slot;

  /* pauVar27[7] bytes 4..6, 8..10 and 12..14: the two light colours and their
   * per-channel maximum. Only the maximum is read again. */
  for (int i = 0; i < 3; i++)
  {
    byte ambient = *((byte *)&DAT_0035566c + (2 - i)); /* the scene ambient */
    byte light0 = *((byte *)&DAT_00355670 + (2 - i));  /* light 0's colour */
    workspace[0x74 + i] = ambient;
    workspace[0x78 + i] = light0;
    workspace[0x7c + i] = (ambient < light0) ? light0 : ambient;
  }

  *(float *)(workspace + 0x80) = DAT_00355628;       /* the draw distance */
  *(float *)(workspace + 0x84) = DAT_00355658 * 2.0; /* G, the projection term */
  *(float *)(workspace + 0x8c) = DAT_003555a4;       /* the depth key's numerator */
  *(float *)(workspace + 0x90) = DAT_003555a0;       /* and its offset */

  for (slot = 0xff; slot >= 0; slot--)
  {
    entity *e = &DAT_0058beb0[slot];

    /* The mirror image of FUN_0020c5a8's guards. */
    if (e->typeId_00 == 0)
      continue;
    if ((e->descriptorFlags_02 & 0x200) == 0)
      continue;
    if ((e->halfword_08 & 1) != 0)
      continue;

    /* A "was drawn this frame" latch. FUN_0020f510 sets it back on anything it
     * actually submits, and nothing else reads it. */
    e->collisionFlags_0C &= 0xffffefff;

    FUN_0020f510(e, workspace);
  }
}

/* --------------------------------------------------------------------------
 * FUN_0020f510 -- one entity's quads.
 *
 * Only the geometry is reproduced here; the GS packet marshalling that makes up
 * most of the original is left out.
 *
 * THE QUAD IS BUILT IN SCREEN SPACE, which is why a sprite never rotates with
 * the camera:
 *
 *   projScaleX = trunc(entity+0x14C * 280.0 * G / viewZ)
 *   projScaleY = trunc(entity+0x150 * 280.0 * G / viewZ)   (equal when 14C == 150)
 *   gsX = screenX + (spriteX * projScaleX >> 8)     spriteX in 1/16 sprite units
 *   gsY = screenY + (spriteY * projScaleY >> 9)     note the extra shift
 *
 * The extra shift on Y is not an error. The GS output is 640x224 shown at 4:3,
 * so its pixels are 2:1 and a square sprite has to be twice as wide in pixels.
 *
 * Every corner is an offset from the PROJECTED ORIGIN, screen[0]/screen[1]. The
 * offsets alone are meaningless: drop the origin and every sprite in the game
 * lands at the middle of the screen at its own depth, which is what the first
 * cut of the port did.
 *
 * Both projScales are truncated to integers before being used as fixed point,
 * so a distant sprite's size quantises in visible steps rather than shrinking
 * smoothly. The record scale at +0x0C is truncated the same way.
 */
void build_billboard_quads(entity *e, byte *workspace)
{
  float viewSpace[4];
  int screen[4];
  byte *record;
  int first, count, index;

  /* Project the origin. The near clip here is DAT_0035209c, 0.3 -- NOT the 0.4
   * the geometry path uses -- and the far one is the scene's draw distance. */
  transform_to_view_space(e->position_20, viewSpace);
  if (viewSpace[2] <= DAT_0035209c)
    return;
  if (*(float *)(workspace + 0x80) <= viewSpace[2])
    return;

  FUN_0020b600(workspace, screen, ...); /* view space -> GS screen position */

  /* A generous cull window about the 2048-pixel GS centre: +/-448 in X and
   * +/-176 in Y, both in whole pixels, so a partly on-screen sprite survives. */
  if (screen[0] < 0x6400 || 0x9c00 < screen[0])
    return;
  if (screen[1] < 0x7500 || 0x8b00 < screen[1])
    return;

  {
    float G = *(float *)(workspace + 0x84);
    int projScaleX = (int)(e->scale_14C * 280.0 * G / viewSpace[2]);
    int projScaleY = projScaleX;
    if (e->scale_14C != e->scaleZ_150)
    {
      projScaleY = (int)(e->scaleZ_150 * 280.0 * G / viewSpace[2]);
    }

    /* The vertex colour. +0x08 bit 0x4000 skips the lighting for a flat 0x80;
     * otherwise it is the average of the staged maximum and the VU0 point-light
     * contribution at this position, saturated. 0x80 is 1.0. */
    byte colour[3];
    if ((e->halfword_08 & 0x4000) == 0)
    {
      byte dynamic[3];
      vu0_point_light_bytes(e->position_20, dynamic);
      for (int i = 0; i < 3; i++)
      {
        int level = ((int)workspace[0x7c + i] + (int)dynamic[i]) >> 1;
        colour[i] = (level > 0xff) ? 0xff : (byte)level;
      }
    }
    else
    {
      colour[0] = colour[1] = colour[2] = 0x80;
    }

    /* The column table, indexed by the animation's current pose column. A
     * column past the end draws nothing -- there is no clamp. */
    {
      ushort *blob = e->model_15C;
      ushort columnCount = blob[0];
      int columnTable = *(int *)(blob + 2);
      int recordTable = *(int *)(blob + 4);

      if (e->poseColumn_AC >= columnCount)
        return;

      first = *(ushort *)(columnTable + (int)blob + e->poseColumn_AC * 4);
      count = *(ushort *)(columnTable + (int)blob + e->poseColumn_AC * 4 + 2);
      record = (byte *)((int)blob + recordTable + first * 0x10 + count * 0x10 - 0x10);
    }

    for (index = count - 1; index >= 0; index--, record -= 0x10)
    {
      int w = record[3] - 1;
      int h = record[4] - 1;
      int recordScale = (int)(*(float *)(record + 0xc) * 256.0);
      int quadW = (w * recordScale) >> 4;
      int quadH = (h * recordScale) >> 4;

      /* In 1/16 sprite units, so the centring divide keeps its fraction. */
      int x0 = (char)record[6] * 16 + (w * 16 - quadW) / 2;
      int y0 = (char)record[7] * 16 + (h * 16 - quadH) / 2;
      int x1 = x0 + quadW;
      int y1 = y0 + quadH;

      /* The flips swap the corners, not the texture coordinates -- so the quad
       * can come out with x1 < x0. */
      if ((record[0] & 0x10) != 0)
      {
        x1 = x0;
        x0 = x0 + quadW;
      }
      if ((record[0] & 0x20) != 0)
      {
        y1 = y0;
        y0 = y0 + quadH;
      }

      /* To GS screen coordinates. */
      x0 = screen[0] + ((x0 * projScaleX) >> 8);
      x1 = screen[0] + ((x1 * projScaleX) >> 8);
      y0 = screen[1] + ((y0 * projScaleY) >> 9);
      y1 = screen[1] + ((y1 * projScaleY) >> 9);

      /* UVs. u1/v1 are inclusive, hence the extent rather than the extent+1. */
      submit_uv(record[1], record[2], record[1] + w, record[2] + h);

      /* The depth key: the view depth plus the entity's bias and this record's
       * own, which is what lets one strip layer its records against each other.
       * Neither bias touches the quad above. */
      submit_depth(DAT_003555a4 /
                       (viewSpace[2] + (float)(char)e->depthBias_133 * DAT_003520a0 +
                        (float)(char)record[8] / 100.0) +
                   DAT_003555a0);

      /* PRIM. 0x0D untextured / 0x1D textured, both triangle-strip with IIP and
       * with FGE clear -- a sprite is never fogged. Bit 0x40 is ABE, and it goes
       * on for any non-zero blend mode, so mode 0 does not blend at all and its
       * alpha is never consulted. The mode is the same 0..3 the map and PSC3
       * paths use. */
      submit_prim((e->textureSlot_136 >= 0 ? 0x1d : 0x0d) |
                  ((record[0] & 3) != 0 ? 0x40 : 0));
      submit_colour(colour[0], colour[1], colour[2],
                    (record[0] & 3) == 0 ? 0x80 : (record[5] >> 1));
      submit_quad(x0, y0, x1, y1);
    }

    e->collisionFlags_0C |= 0x1000; /* drawn this frame */
  }
}
