/*
 * The Particle Pool -- DAT_00355620
 *
 * Original functions:
 *   FUN_002d3290  0x002d3290  clear the pool and drop the behaviour
 *   FUN_002d3218  0x002d3218  step and draw all 1536 entries
 *   FUN_002d3058  0x002d3058  one particle's screen quad
 *   FUN_002d2348  0x002d2348  the spark behaviour
 *   FUN_002d3320  0x002d3320  the ambient-dust behaviour (not ported)
 *   FUN_002d2470  lines 120-165, the magic projectile's impact burst
 *
 * There is exactly ONE pool and exactly ONE behaviour at a time. The pool is
 * 0x18000 bytes at 0x01C69A00 -- inside the script memory window -- so 1536
 * entries of 0x40, and DAT_00355e0c holds the function pointer every entry is
 * stepped through. FUN_002d3218 walks all 1536 once a frame, from
 * FUN_002239c8:125, immediately after FUN_00239ce0's actor loop. A null
 * behaviour skips the pool entirely, however much of it is marked alive.
 *
 * FUN_0022a418:377 resets the pool at scene init, just before the entity setup.
 * FUN_002d3290 draws from FUN_00216868 once per entry -- 1536 draws -- and
 * writes the result into +0x22 as a stagger. Only FUN_002d3320 reads that; the
 * spark path overwrites +0x22 on spawn, so for sparks the reset's only
 * observable effect is the advance of the shared RNG.
 *
 * THE ENTRY, 0x40 bytes. +0x0C is genuinely unused by both behaviours:
 *
 *   +0x00 f32  x                    world, the axes an entity's +0x20 uses
 *   +0x04 f32  y
 *   +0x08 f32  z                    vertical
 *   +0x10 f32  vertical velocity
 *   +0x14 f32  horizontal speed
 *   +0x18 u32  GS RGBA, alpha in the high byte
 *   +0x1C s16  alive; the spawn loop tests this and nothing else
 *   +0x1E s16  quad width  in units of 40 GS 12.4 units at unit depth
 *   +0x20 s16  quad height in units of 20 -- the 2:1 GS pixel again
 *   +0x22 s16  ticks before the fade starts; allowed to go negative
 *   +0x24 f32  gravity
 *   +0x28 f32  heading, radians
 *   +0x2C s16  the spawning entity's pool slot; written, never read
 *
 * VERIFIED against a PCSX2 save state taken with the sparks live: exactly 100
 * entries alive, all with +0x2C equal to the projectile's slot, headings
 * stepping by 0.0349066, colours with red in {0, 0xFF}, green and blue each in
 * [0xC0, 0xFF] and alpha 0xE0. Every field above was read out of it.
 *
 * PORTED 2026-08-30 into port/src/ported/entity/original_particles.{h,cpp} (the
 * pool, the reset, the step and the burst), FUN_002d3058_build_particle_quad in
 * port/src/ported/render/original_sprite_pass.cpp (the quad) and
 * PortRuntime::publishSpriteQuads (the draw walk).
 *
 * NOT PORTED: FUN_002d3320, the ambient dust FUN_002d36f8 installs. It reseeds
 * its own particles off the player's bones through FUN_0020dc88 and reads a
 * per-character colour table at DAT_00326708; it is a separate effect that
 * happens to share the pool.
 */

#include "orphen_globals.h"

/* --------------------------------------------------------------------------
 * FUN_002d2470 lines 120-165 -- the impact burst.
 *
 * Reached when the projectile's +0x0C picks up a collision bit in 0x266, or it
 * outlives 0x2580 ticks, or the swept hit test returned something. All three
 * land here, and the burst reads the projectile's position and facing, so it
 * runs before the state change to 4.
 */
void spawn_impact_burst(entity *e)
{
  byte *p = (byte *)DAT_00355620;
  int remaining = 100;
  float angle = DAT_00354690; /* pi/2 -- the fan starts square to the flight */
  uint offset = 0;

  /* The walk tests the CURRENT entry, then advances and reads the NEXT entry's
   * +0x1C before looping -- so a fragmented pool is filled in place and a burst
   * arriving while an earlier one is still alive simply gets fewer than 100. */
  while (1)
  {
    if (*(short *)(p + 0x1c) == 0)
    {
      /* Red is all or nothing on a coin flip; green and blue are each a random
       * 0xC0..0xFF. So half the shower is white and half is cyan, and none of
       * it is dim. Alpha 0xE0 is 1.75 in GS terms, and the blend is additive. */
      byte red = (FUN_00216868() % 1000 > 500) ? 0x00 : 0xff;
      byte green = (FUN_00216868() & 0x3f) | 0xc0;
      byte blue = (FUN_00216868() & 0x3f) | 0xc0;

      *(float *)(p + 0x00) = e->position_20;
      *(float *)(p + 0x04) = e->position_24;
      *(float *)(p + 0x08) = e->position_28;
      *(uint *)(p + 0x18) = (blue << 16) | (green << 8) | red | 0xe0000000;

      /* Bit 1 of the draw, not bit 0: 0.0005 or 0.0015, nothing between. */
      *(float *)(p + 0x10) = (float)((FUN_00216868() & 2) + 1) / 2000.0;
      *(float *)(p + 0x14) = (float)(FUN_00216868() % 10 + 5) / DAT_00354694;

      /* Width and height take the SAME value, 1 or 3 -- a spark is either one
       * pixel or three times that, and never anything else. */
      short units = (FUN_00216868() & 2) + 1;
      *(short *)(p + 0x1c) = 1;
      *(short *)(p + 0x1e) = units;
      *(short *)(p + 0x20) = units;

      *(short *)(p + 0x22) = (FUN_00216868() % 0x1e + 0x1e) * 0x20; /* 30-59 frames */

      /* The fan advances once per particle SEEDED, not once per slot examined. */
      *(float *)(p + 0x28) = FUN_00216690(e->facing_5C + angle);
      *(float *)(p + 0x24) = (float)(FUN_00216868() % 5 + 1) / DAT_00354698;
      *(short *)(p + 0x2c) = pool_slot_of(e);

      angle = angle + DAT_0035469c; /* two degrees */
      remaining = remaining - 1;
    }

    offset = offset + 0x40;
    if (remaining == 0 || 0x17fff < offset)
      break;
    p = p + 0x40;
  }

  DAT_00355e0c = FUN_002d2348;
  e->state_60 = 4;
  e->halfword_08 |= 1; /* hidden -- the projectile itself stops drawing here */
}

/* --------------------------------------------------------------------------
 * FUN_002d2348 -- one spark, one frame.
 */
void step_spark(float *p, uint frameTicks)
{
  if (*(short *)(p + 7) == 0)
    return; /* a free entry does not age */

  /* A 16-bit subtract that is allowed to go negative, which is exactly what
   * makes the fade below run every frame once the timer has expired. */
  short remaining = (short)((ushort) * (short *)((int)p + 0x22) - (ushort)frameTicks);
  *(short *)((int)p + 0x22) = remaining;
  if (remaining < 1)
  {
    int alpha = ((uint)p[6] >> 24) - 2;
    if (alpha < 1)
      *(short *)(p + 7) = 0; /* dead, and the slot is free again */
    else
      p[6] = (float)(((uint)p[6] & 0xffffff) | (alpha << 24));
  }

  /* Two different tick scalings, and they are not the same quantity: the
   * vertical integration works in ticks/8 and the horizontal step in ticks/2. */
  float verticalStep = (float)(int)frameTicks * 0.125;
  float horizontalStep = p[5] * 0.5 * (float)(int)frameTicks;

  /* The decay test reads the speed from BEFORE the step, so every spark gets
   * one frame at its full speed. */
  bool decaying = DAT_00354678 < p[5];

  p[0] = p[0] + horizontalStep * FUN_00305130(p[10]); /* cos */
  p[1] = p[1] + horizontalStep * FUN_00305218(p[10]); /* sin */

  /* A properly integrated arc: the position takes the OLD velocity plus half
   * the acceleration term, and the velocity is updated from that same old
   * value. Swapping the two lines changes the trajectory. */
  float velocity = p[4];
  p[4] = velocity - p[9] * verticalStep;
  p[2] = p[2] + (velocity * verticalStep - p[9] * verticalStep * verticalStep * 0.5);

  if (decaying)
    p[5] = p[5] - DAT_00354678;
}

/* --------------------------------------------------------------------------
 * FUN_002d3058 -- one particle's quad.
 *
 * FUN_0020b600's vftoi0 is masked to .xyz, so lane W survives as a float: it
 * holds Q = 1 / max(viewZ, eps). This function multiplies its two literals by
 * that directly, which means a particle's size is a plain 1/z with no
 * projection term in it at all -- one to three pixels at any normal depth.
 *
 * The projected point is the quad's TOP-LEFT corner, not its centre.
 *
 * The texture is the boot sheet in slot 0x2B: the packet carries 0x2B and that
 * field is the slot itself. Texture 0x177 sits there, and its
 * (65,177)-(79,191) is a round white-blue spark exactly filling the rectangle,
 * while slot 0x2A's 0x178 has flat grey noise. FUN_0020f510 writes `slot + 1`
 * into the same field, so the two producers of it disagree by one; see
 * analyzed/sword_hit_test_and_damage.c, which settles it the same way from the
 * hit sparks. The UV rectangle is stored normalised over 256 and scaled by 4096
 * on the way into the GS's 1/16-texel units. DAT_10008080's bit 0x8000 selects
 * blend mode 2, additive.
 *
 * FUN_00207de8(0x1000) puts every particle in one display-list bucket, above
 * all of FUN_0020f510's depth-sorted 1..0xFFF and below its 0x1005.
 */
void draw_particle(float *p)
{
  int screen[4];

  /* No near, far or window test -- the only guard is FUN_0020b600's clip flags,
   * unlike the sprite pass, which culls three ways before it builds anything. */
  if ((FUN_0020b600(p, screen, ...) & 0xe0) != 0)
    return;

  float q = *(float *)&screen[3];
  int halfW = (int)((float)*(short *)((int)p + 0x1e) * 40.0 * q);
  int halfH = (int)((float)*(short *)((int)p + 0x20) * 20.0 * q);

  submit_colour_all_four_vertices(*(uint *)(p + 6));
  submit_depth_all_four_vertices(screen[2]);
  submit_quad(screen[0], screen[1], screen[0] + halfW, screen[1] + halfH);
  submit_uv(65, 177, 79, 191);
  FUN_00207de8(0x1000);
}
