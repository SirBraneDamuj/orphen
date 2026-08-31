#pragma once

// Native counterpart of src/FUN_0020f3e0.c (0x0020f3e0) and src/FUN_0020f510.c
// (0x0020f510): the **second** entity draw pass, the one that draws billboards.
//
// FUN_0020c5a8 -- the skeletal entity pass -- refuses any entity whose +0x02
// carries bit 0x200, and FUN_0020f3e0 then walks the same 256 slots for exactly
// those. So the two passes partition the pool rather than filtering it, and an
// effect entity is never a candidate for the skinned path at all.
//
// The model behind them is the sprite strip loadSpriteStripModel parses: no
// PSC3 magic, no bones, and a header whose +0x00 is a **column** count rather
// than a record count.
//
//   +0x00 u16  column count
//   +0x02 u16  animation count
//   +0x04 u32  column table:  {u16 firstRecord, u16 recordCount} per column
//   +0x08 u32  sprite record table, 16 bytes each
//   +0x0C u32  animation table, one u32 timeline offset per animation
//
// The animation picks a column (entity +0xAC) and the column names a run of
// sprite records. They are drawn **back to front**: FUN_0020f510 starts at the
// last record of the run and walks down.
//
// ---- Where the quad comes from -------------------------------------------
//
// FUN_0020b600 projects the entity origin to an integer GS screen position, and
// every corner is built **on top of that origin**, in GS units:
//
//     projScaleX = trunc(entity+0x14C * 280.0 * G / viewZ)   G = 2 * DAT_00355658
//     gsX = originX + (spriteX * projScaleX >> 8)            spriteX in 1/16 units
//     gsY = originY + (spriteY * projScaleY >> 9)            note the extra shift
//
// The quad is axis-aligned in screen space, which is why a sprite never rotates
// with the camera, and both projScales are truncated to integers before use, so
// a distant sprite quantises in visible size steps.
//
// The extra shift on Y is not an error: the GS output is 640x224 shown at 4:3,
// so its pixels are 2:1 and a square sprite has to be twice as wide in pixels.
// X uses +0x14C and Y uses +0x150, so a non-uniform entity scale stretches it.
//
// This port reproduces all of that in GS integers and then un-projects the four
// finished corners back into view space, because it draws through the same
// projection the world does rather than writing GIF packets. Re-projecting them
// gives back the same GS numbers exactly -- it is the inverse of the transform
// above, applied at the depth the GS z was keyed off -- and it is the only
// adaptation in the function.

#include "ported/model/psc3_model.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::render
{

  // DAT_0035209c, 0.3 -- the sprite pass's own near clip, and *not* the 0.4 the
  // geometry path uses (DAT_00351FE8).
  inline constexpr float kDAT_0035209c_spriteNearClip = 0.300000012f;
  // DAT_003520a0. Entity +0x133 is a signed byte scaled by this before it joins
  // the view depth, so the -12 most effect descriptors carry pulls the sprite
  // very nearly a whole unit toward the camera.
  inline constexpr float kDAT_003520a0_entityDepthBiasScale = 0.0799999982f;
  // The depth key FUN_0020f3e0 stages into its workspace at +0x8C / +0x90.
  inline constexpr float kDAT_003555a4_depthNumerator = 19706.0859f;
  inline constexpr float kDAT_003555a0_depthOffset = -152.953781f;

  // FUN_0020f510:0x0020f594-0x0020f5cc. The cull window around the GS centre,
  // in 1/16 pixel units: +/-448 px in X and +/-176 px in Y, both generous
  // enough that a partly on-screen sprite survives.
  inline constexpr int kSpriteCullMinX = 0x6400;
  inline constexpr int kSpriteCullMaxX = 0x9C00;
  inline constexpr int kSpriteCullMinY = 0x7500;
  inline constexpr int kSpriteCullMaxY = 0x8B00;

  // One 16-byte sprite record. Offsets are the record's own.
  struct SpriteRecord
  {
    // +0x00. Bits 0-1 are the blend mode, the same 0..3 the map and PSC3 paths
    // use -- FUN_0020f510 ORs PRIM bit 6 (ABE) for anything non-zero and leaves
    // blending off for 0. Bit 0x10 flips the quad in X, bit 0x20 in Y.
    std::uint8_t flags = 0;
    std::uint8_t u = 0;       // +0x01, texel origin
    std::uint8_t v = 0;       // +0x02
    std::uint8_t width1 = 0;  // +0x03, one *more* than the extent
    std::uint8_t height1 = 0; // +0x04
    std::uint8_t alpha2 = 0;  // +0x05, twice the alpha byte
    std::int8_t offsetX = 0;  // +0x06, in whole sprite units
    std::int8_t offsetY = 0;  // +0x07
    // +0x08. Divided by 100 and added to the view depth before the depth key is
    // computed, so a strip can order its own layers against each other. It does
    // not move the sprite on screen.
    std::int8_t depthBias = 0;
    std::uint8_t byte09 = 0; // +0x09, the mip bias when the texture slot is > 0x17
    float scale = 1.0f;      // +0x0C
  };

  // One quad, ready to draw. Coordinates are in the original's view space, so
  // the caller draws them with the modelview at identity and the scene's own
  // projection.
  struct SpriteQuad
  {
    // The four corners, already flipped if the record asked for it. x0/y0 is
    // the first corner and x1/y1 the opposite one; either may be the larger.
    // Ignored when `oriented` is set.
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float viewZ = 0.0f;

    // Set for a quad whose corners are four independent points rather than an
    // axis-aligned rectangle at one depth. Nothing in the sprite pass itself
    // produces one -- FUN_0020f510 and FUN_002d3058 both build their corners in
    // GS screen units around a projected origin -- but the hit sparks
    // (FUN_00220c00) build a streak in **world** space and only project at the
    // end, so their four corners each carry their own depth and their own
    // texel. They still reach the GS through the same display list, which is
    // why they are carried here rather than in a list of their own.
    bool oriented = false;
    // View space, in the corner order the packet uses. u/v are texels, matched
    // to the same corner index.
    float cornerX[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cornerY[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cornerZ[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cornerU[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cornerV[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Texels, not normalised -- the caller divides by the bound texture's size
    // the way the dialogue sprites do.
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;

    // 0..1, with 1.0 standing for the GS's 0x80. Alpha included.
    float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    int blendMode = 0;
    int textureSlot = -1;
    // FUN_0020f510's own submission order: the packet is pushed into one of
    // 4096 depth buckets, clamped to 1..0xFFF, or into 0x1005 when the entity
    // asked to be drawn over everything. Ascending is back to front.
    int displayListBucket = 1;
    // False only for the 0x1005 bucket, which the GS reaches with a z of 0xFFFF
    // and therefore in front of all world geometry.
    bool depthTest = true;
  };

  // What FUN_0020f510 reads that does not come out of the record.
  struct SpriteBuildInputs
  {
    float scaleX14c = 1.0f; // entity +0x14C
    float scaleY150 = 1.0f; // entity +0x150

    // FUN_0020b600's integer output for the entity origin, in GS 12.4 units.
    // **Every corner is an offset from this**; without it each sprite lands at
    // the middle of the screen.
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;

    // 2 * DAT_00355658, staged into FUN_0020f3e0's workspace at +0x84.
    float projectionG = 2.0f;
    // The original projection's two scale terms and its screen centre -- what
    // the un-projection back to view space needs. ViewProjection::projection at
    // (0,0), (1,1) and (2,0).
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;

    // (float)(signed char)entity+0x133 * DAT_003520a0, staged at workspace +0x88.
    float entityDepthBias = 0.0f;
    // Entity +0x08 bit 0x40: GS z 0xFFFF and the 0x1005 bucket, so the sprite
    // is drawn over the world regardless of where it is.
    bool forceFront = false;

    // FUN_0020f3e0 stages `max(DAT_0035566c, DAT_00355670)` per channel -- the
    // scene ambient against light 0's colour -- and FUN_0020f510 averages it
    // with the VU0 point-light contribution at the sprite's position. Both are
    // GS bytes, 0x80 = 1.0.
    std::uint8_t ambient[3] = {0x80, 0x80, 0x80};
    std::uint8_t dynamic[3] = {0, 0, 0};
    // Entity +0x08 bit 0x4000: skip the lighting entirely and use a flat 0x80.
    bool flatColour = false;

    int textureSlot = -1;
  };

  // FUN_0020f510's record loop for one entity, in draw order. Appends to `out`.
  // `poseColumn` is entity +0xAC, which the animation stepper owns.
  //
  // NOT REPRODUCED, and neither bit is set by any descriptor either scene
  // spawns: the rotation branch at entity +0x08 bit 0x400, which rebuilds the
  // quad as four independently rotated corners off the per-record angle array at
  // entity +0x168 and only for record indices under nine; and the screen-space
  // branch at bit 0x1000, which takes a position that is already in GS units
  // instead of projecting one. The caller rejects an entity carrying either.
  std::size_t FUN_0020f510_build_quads(const orphen::ported::model::Psc3Model &model,
                                       std::uint16_t poseColumn,
                                       const SpriteBuildInputs &inputs,
                                       std::vector<SpriteQuad> &out);

  // FUN_002d3058 (0x002d3058): one particle's quad, from the same pool walk.
  //
  // A particle is not a sprite record -- it has no strip, no blend byte and no
  // texture of its own -- but its quad reaches the GS the same way, so it lands
  // in the same list.
  //
  //   Q      = 1 / max(viewZ, eps)          FUN_0020b600 leaves it in lane W,
  //                                         because vftoi0 only converts xyz
  //   halfW  = trunc(+0x1E * 40.0 * Q)      GS 12.4 units
  //   halfH  = trunc(+0x20 * 20.0 * Q)      the same 2:1 pixel aspect again
  //
  // The projected point is the quad's **top-left corner**, not its centre: the
  // packet puts the origin at two vertices and origin+half at the other two. At
  // the depth these are used, that is a one to three pixel dot either way.
  //
  // The texture is the boot sheet in slot 0x2B: FUN_002d3058 writes 0x2B into
  // the packet's texture field and that field is the slot itself. Slot 0x2B
  // holds texture 0x177, whose (65,177)-(79,191) is a round white-blue spark
  // exactly filling the rectangle; slot 0x2A's texture 0x178 has a flat grey
  // noise field there. See original_hit_sparks.h, which reaches the same
  // reading from the other effect path -- and note that FUN_0020f510 writes
  // `slot + 1` into the same field, so the two producers disagree by one.
  //
  // The UV rectangle is fixed, and DAT_10008080's bit 0x8000 selects blend
  // mode 2, additive.
  inline constexpr int kParticleTextureSlot = 0x2B;
  inline constexpr float kParticleU0 = 65.0f;  // 0.2539 * 256, the packet stores
  inline constexpr float kParticleV0 = 177.0f; // normalised and scales by 4096
  inline constexpr float kParticleU1 = 79.0f;  // to reach GS 1/16-texel units
  inline constexpr float kParticleV1 = 191.0f;
  inline constexpr int kParticleBlendMode = 2;
  // FUN_002d3058's tail: FUN_00207de8(0x1000). Every particle goes into one
  // fixed bucket, above all the depth-sorted sprites and below the 0x1005 ones.
  inline constexpr int kParticleDisplayListBucket = 0x1000;

  struct ParticleQuadInputs
  {
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    std::int16_t widthUnits = 1;
    std::int16_t heightUnits = 1;
    std::uint32_t colour = 0xE0FFFFFFu; // GS RGBA, alpha in the high byte
  };

  SpriteQuad FUN_002d3058_build_particle_quad(const ParticleQuadInputs &inputs);

  // Exposed for the tests and for the report; the loop above uses it.
  SpriteRecord readSpriteRecord(std::span<const std::uint8_t> blob, std::size_t at);

} // namespace orphen::ported::render
