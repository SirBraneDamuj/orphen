/*
 * Motion Trails -- the ribbon the sword leaves as it swings
 *
 * Original functions:
 *   FUN_0020e840  0x0020e840  record a sample, build the ribbon, submit it
 *   FUN_0020e760  0x0020e760  claim one of 32 trail slots
 *   FUN_0020e7b0  0x0020e7b0  release one
 *   FUN_00266a78  0x00266a78  load a natural cubic spline with n points
 *   FUN_00266460  0x00266460  its tridiagonal solve
 *   FUN_00266610  0x00266610  which segment a parameter falls in
 *   FUN_00266668  0x00266668  evaluate the cubic
 *   FUN_0020d820  0x0020d820  project a run of points, reject the off-screen
 *
 * PORTED 2026-08-31 into port/src/ported/render/original_weapon_trail.{h,cpp},
 * with the header +0x38 table in port/src/ported/model/psc3_model.{h,cpp}, the
 * two entity fields in port/src/ported/entity/original_entity.h, the step in
 * PortRuntime::publishOneSceneObjectView and the draw in
 * PortRuntime::publishSpriteQuads / MapViewer::drawSpriteQuads.
 *
 * WHERE IT LIVES, AND WHY THAT IS THE WHOLE PUZZLE. This is *not* one of the
 * standalone effect systems FUN_002192c0 runs. FUN_0020c810 calls it last, per
 * entity, after the bone palette is composed and after FUN_0020eec0 has worked
 * out the entity's depth bucket -- so a motion trail is part of a model's own
 * draw. In the sword_trail save state every gate global of every system in
 * FUN_002192c0's list reads zero while the trail is plainly on screen, which is
 * what rules all of them out; the state that matters is in the entity and in a
 * static pool at 0x004FBC7C.
 *
 * WHAT DRIVES IT. The low eight bits of entity +0xAA -- the halfword
 * FUN_00225c90 stages out of the current keyframe -- are an enable mask, one
 * bit per descriptor in the model's header +0x38 table:
 *
 *   +0xB0       last frame's copy of that byte, so an edge either way is seen
 *   +0xB1..+0xB8  the eight one-based slot handles, zero for "none held"
 *
 * A bit going clear -> set claims a slot and clears its sample count; set ->
 * clear releases it. FUN_0020e760 walks 32 records of 0x184 bytes from
 * 0x004FBE00 against the mask DAT_00355A3C and returns index + 1, so handle h
 * is at 0x004FBC7C + h * 0x184. The claim is what zeroes the count, not the
 * release -- a slot handed straight back out starts empty however it was
 * given up.
 *
 * THE MODEL'S SHARE, header +0x38, three words per descriptor:
 *
 *   +0x00 u32  GS colour: rgb in the low three bytes, alpha in the top
 *   +0x04 u16  vertex index, one edge of the ribbon
 *   +0x06 u16  vertex index, the other
 *   +0x08 s32  how many recorded samples the spline runs through
 *
 * Seven models in s00_e000 carry one. grp_0179, the sword blade, has two:
 *
 *   [0] rgb (0xAA,0xF9,0xD1) a 0xC8   vertices 98 and 100   3 samples
 *   [1] rgb (0x0C,0x7C,0x66) a 0x96   vertices 98 and 100   4 samples
 *
 * -- a bright pale-green ribbon over three samples and a darker one over four,
 * on the same pair of blade vertices. Its **animation 0** carries 0x0003 and
 * animations 1 and 2 carry nothing, so the trail is alive for exactly the
 * phase that also runs the swept hit test: the long stretch after the
 * six-frame spawn flourish. That is the whole of the sword trail's authoring.
 *
 * The table has no stored length. FUN_0020e840 indexes it by bit position, and
 * +0xAA has eight bits, so eight is the ceiling. Some models store -1 for the
 * second vertex on an entry no animation ever enables; the original would read
 * off the front of the vertex stream for one of those.
 *
 * THE SHAPE. Both named vertices are skinned by their own bones, through the
 * same vertex stream, the same +0x18 bone bytes and the same matrix buffer at
 * ctx+0x158 that the mesh draw uses, and the resulting world pair is pushed
 * into a 16-deep history with the newest at index 0. The shift stops at
 * fifteen so the drop off the end costs nothing, and only then does the count
 * grow.
 *
 * Each frame the newest `sampleCount` pairs -- clamped to [3, 16] and then to
 * what has been recorded -- become the control points of a natural cubic
 * spline, one per edge, over uniform knots 0..1. Twelve samples come off each,
 * and the twelve pairs are stitched into eleven quads in the order
 * A[i], B[i], B[i+1], A[i+1]. The alpha ramps as
 *
 *   near edge  alpha * (12 - i) / 12
 *   far edge   alpha * (11 - i) / 12
 *
 * so the newest end is full and the oldest is a twelfth. FUN_00207de8 then
 * halves that alpha -- after masking its low bit -- and leaves the rgb alone,
 * which is the *untextured* branch of its colour fixup; a textured packet has
 * all four channels halved. 0x80 is 1.0 on the GS, so grp_0179's first trail
 * arrives at 100/128 alpha with an rgb that runs to 1.95x on green.
 *
 * THE ONLY UNTEXTURED PRIMITIVE IN THE EXECUTABLE. FUN_0020e840 writes 0xFFFF
 * into the packet's texture halfword. FUN_00207de8 reads that field,
 * increments it and stores it back before use, so 0xFFFF arrives as zero and
 * takes the branch that emits PRIM 0x0D -- a gouraud triangle fan with TME
 * clear -- instead of 0x1D. Every other caller passes a real slot. The mode
 * word DAT_10004780 raises ABE through the `& 0x1C000` ladder at 0x4000, which
 * is blend mode 1 rather than the hit sparks' additive mode 2, and bit
 * 0x10000000 says the coordinates are already integers, which they are:
 * FUN_0020d820 wrote them through vftoi0.
 *
 * The submission is FUN_00207de8(ctx+0x1D8 + 1) -- one bucket in front of the
 * entity the trail belongs to, so the ribbon draws over its own model.
 *
 * FUN_0020d820 rejects a corner whose projected z reaches 0xFFFE. uGpffff80b0
 * is 65534.0 and FUN_0020bd58 maps the near plane to exactly it, so that test
 * is "in front of the near plane" written in screen units. Its other reject,
 * the `& 0xE0` on the accumulated screen bounding box, only drops a ribbon
 * that is wholly off one edge of the screen; the port does not reproduce it
 * because it cannot change a pixel.
 *
 * VERIFIED AGAINST THE SAVE STATE. Desktop/save_state/sword_trail, mid-swing:
 * DAT_00355A3C is 0x00000003 and the blade in pool slot 23 holds +0xAA = 0x0003,
 * +0xB0 = 0x03, +0xB1 = 1, +0xB2 = 2 with animation 0 running -- two trails,
 * slots 1 and 2. Both slots hold fourteen samples of the same pair, because
 * both descriptors name the same two vertices, and |A - B| is 0.775 at every
 * one of them. The port reaches mask 0x3 in two slots at the same point in the
 * swing and its own |A - B| is 0.73, which is the check that the two vertices
 * are the right two.
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * FUN_0020e760 / FUN_0020e7b0 -- the slot pool.
 *
 * DAT_00355A3C is a 32-bit allocation mask over the records at 0x004FBE00.
 * The handle is one-based so that zero can mean "none held" in the entity's
 * byte, which is why every index below is `handle - 1`.
 */
int claim_trail_slot(void)
{
  uint8_t *record = (uint8_t *)0x4fbe00;
  uint32_t bit = 1;
  int index = 0;

  do
  {
    if ((DAT_00355a3c & bit) == 0)
    {
      *record = 0; /* the claim clears the sample count, not the release */
      DAT_00355a3c |= bit;
      return index + 1;
    }
    index++;
    bit <<= 1;
    record += 0x184;
  } while (index < 0x20);

  return 0; /* all 32 busy: the caller draws nothing and tries again next frame */
}

void release_trail_slot(int handle)
{
  if ((uint32_t)(handle - 1) < 0x20)
  {
    DAT_00355a3c &= ~(1u << ((handle - 1) & 0x1f));
  }
}

/* --------------------------------------------------------------------------
 * FUN_0020e840 -- one entity's motion trails.
 *
 * param_1 is the entity, param_2 the render context FUN_0020c810 filled:
 * +0x158 the bone matrices, +0x168 the model, +0x1D8 the depth bucket.
 */
void update_and_draw_motion_trails(entity *e, render_context *ctx)
{
  uint8_t *model = ctx->model_168;
  trail_descriptor *table = model->trailTable_38 ? model + model->trailTable_38 : NULL;

  if (table == NULL)
  {
    return;
  }

  uint16_t mask = e->flags_AA;
  uint8_t previous = e->previousTrailMask_B0;
  if (mask == 0 && previous == 0)
  {
    return;
  }

  int16_t *vertices = model + model->vertexRecords_14; /* stride 10 */
  uint8_t *boneOf = model + model->vertexBoneIndices_18;

  for (int index = 0; index < 8; index++)
  {
    uint32_t bit = 1u << index;

    if ((mask & bit) == 0)
    {
      /* Gone this frame: hand the slot back and leave the samples where they
       * are. Nothing reads them again. */
      if (e->trailSlot_B1[index] != 0)
      {
        release_trail_slot(e->trailSlot_B1[index]);
        e->trailSlot_B1[index] = 0;
      }
      continue;
    }

    int handle = e->trailSlot_B1[index];
    if (handle == 0)
    {
      handle = claim_trail_slot();
      if (handle == 0)
      {
        continue;
      }
      e->trailSlot_B1[index] = (uint8_t)handle;
    }
    else if ((uint32_t)(handle - 1) >= 0x20)
    {
      e->trailSlot_B1[index] = 0;
      continue;
    }

    trail_slot *slot = (trail_slot *)(0x4fbc7c + handle * 0x184);

    /* Clear -> set: start the history over, so a second swing does not draw a
     * ribbon back to where the first one ended. */
    if ((previous & bit) == 0)
    {
      slot->sampleCount = 0;
    }

    trail_descriptor *descriptor = &table[index];
    int edgeVertex[2] = {(int16_t)descriptor->vertexA, (int16_t)descriptor->vertexB};
    float sample[2][3];

    for (int edge = 0; edge < 2; edge++)
    {
      int16_t *record = &vertices[edgeVertex[edge] * 5];
      float *bone = ctx->boneMatrices_158 + boneOf[edgeVertex[edge]] * 0x40;
      float local[3] = {record[0] * 0.00048828125f,  /* the model's own 1/2048 */
                        record[1] * 0.00048828125f,
                        record[2] * 0.00048828125f};
      vu0_transform_point(local, bone, sample[edge]);
    }

    /* Shift down and put the new pair at the front. The shift skips the write
     * that would fall off the end, and the count only then grows. */
    for (int age = slot->sampleCount; age > 0; age--)
    {
      if (age < 16)
      {
        slot->edgeA[age] = slot->edgeA[age - 1];
        slot->edgeB[age] = slot->edgeB[age - 1];
      }
    }
    if (slot->sampleCount < 16)
    {
      slot->sampleCount++;
    }
    slot->edgeA[0] = sample[0];
    slot->edgeB[0] = sample[1];

    if (slot->sampleCount <= 1)
    {
      continue; /* one sample is a point, not a ribbon */
    }

    int length = descriptor->sampleCount;
    if (length < 3)
    {
      length = 3;
    }
    else if (length > 16)
    {
      length = 16;
    }
    if (slot->sampleCount < length)
    {
      length = slot->sampleCount;
    }

    /* Twelve points per edge off a natural cubic through the newest `length`
     * samples. Uniform knots -- FUN_00266a78's last argument is zero, so the
     * chord-length branch is never taken from here. */
    float curve[2][12][4];
    for (int edge = 0; edge < 2; edge++)
    {
      cubic_spline spline;
      FUN_00266a78(&spline, edge == 0 ? slot->edgeA : slot->edgeB, length, 0);
      for (int step = 0; step < 12; step++)
      {
        FUN_00266ce8(step / 11.0f, &spline, curve[edge][step]);
      }
    }

    /* One call for all 24 points; the returned clip flags are the accumulated
     * screen bounding box against the screen's own bounds. */
    if ((FUN_0020d820(curve, projected, ctx, 0x18) & 0xe0) != 0)
    {
      continue;
    }

    uint32_t rgb = descriptor->colour & 0xffffff;
    uint32_t alpha = descriptor->colour >> 24;

    for (int quad = 0; quad < 11; quad++)
    {
      /* 0xFFFE is what the projection puts at the near plane. */
      if (projected[0][quad].z >= 0xfffe || projected[1][quad].z >= 0xfffe ||
          projected[0][quad + 1].z >= 0xfffe || projected[1][quad + 1].z >= 0xfffe)
      {
        continue;
      }

      uint32_t nearColour = ((alpha * (12 - quad)) / 12) << 24 | rgb;
      uint32_t farColour = ((alpha * (11 - quad)) / 12) << 24 | rgb;

      packet->vertexCount = 4;
      packet->texture = 0xffff;  /* FUN_00207de8 increments this to zero */
      packet->mode = DAT_10004780;
      packet->colour[0] = nearColour; /* A[quad]     */
      packet->colour[1] = nearColour; /* B[quad]     */
      packet->colour[2] = farColour;  /* B[quad + 1] */
      packet->colour[3] = farColour;  /* A[quad + 1] */
      packet->xyz[0] = projected[0][quad];
      packet->xyz[1] = projected[1][quad];
      packet->xyz[2] = projected[1][quad + 1];
      packet->xyz[3] = projected[0][quad + 1];
      FUN_00207de8(ctx->depthBucket_1D8 + 1);
    }
  }

  /* Only reached when the outer test passed, which is why a mask that has been
   * zero for two frames costs nothing. */
  e->previousTrailMask_B0 = (uint8_t)e->flags_AA;
}

/* --------------------------------------------------------------------------
 * FUN_00266460 -- the spline solve, and the two things in it that read wrong.
 *
 * It is a Thomas algorithm on the natural-cubic tridiagonal system, run over
 * two arrays it never reserves: the PS2 scratchpad at DAT_70000000 holds the
 * knot intervals at [0] and the divided differences at [17].
 *
 * FIRST: the array at [17] means two different things at two different times.
 * The forward sweep OVERWRITES each divided difference with the eliminated
 * diagonal as it goes, and the back substitution divides by that. Read as
 * "slopes" throughout, the last loop is nonsense.
 *
 * SECOND: the last row is reduced once before the back substitution and then
 * again by the substitution's own first step. That is only harmless because
 * the term it subtracts is `coefficient[n - 1]`, and the very first thing the
 * function does is set that to zero -- the natural boundary condition.
 *
 * FUN_00266668 evaluates it in a grouping of its own: its stored coefficient
 * is a third of the textbook second derivative and the powers are arranged to
 * match, so the solve and the evaluation are only right together. Ported as
 * written rather than normalised, for that reason.
 */
