#include "ported/render/original_sprite_pass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace orphen::ported::render
{
  namespace
  {
    // FUN_0020f510:0x0020f5f0. The literal 280.0 the projected scale is built
    // from, before the >> 8 / >> 9 that turn it into GS units.
    constexpr float kSpriteProjectionScale = 280.0f;

    // FUN_0030bd20 is float -> int truncation toward zero.
    int FUN_0030bd20_trunc(float value)
    {
      return static_cast<int>(value);
    }

    std::uint16_t u16At(std::span<const std::uint8_t> bytes, std::size_t at)
    {
      return static_cast<std::uint16_t>(bytes[at]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[at + 1]) << 8);
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t at)
    {
      return static_cast<std::uint32_t>(bytes[at]) |
             (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    }

    float f32At(std::span<const std::uint8_t> bytes, std::size_t at)
    {
      const std::uint32_t bits = u32At(bytes, at);
      float value = 0.0f;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }

    bool fits(std::span<const std::uint8_t> bytes, std::size_t at, std::size_t needed)
    {
      return at <= bytes.size() && needed <= bytes.size() - at;
    }
  } // namespace

  SpriteRecord readSpriteRecord(std::span<const std::uint8_t> blob, std::size_t at)
  {
    SpriteRecord record;
    if (!fits(blob, at, 0x10))
    {
      return record;
    }
    record.flags = blob[at + 0x00];
    record.u = blob[at + 0x01];
    record.v = blob[at + 0x02];
    record.width1 = blob[at + 0x03];
    record.height1 = blob[at + 0x04];
    record.alpha2 = blob[at + 0x05];
    record.offsetX = static_cast<std::int8_t>(blob[at + 0x06]);
    record.offsetY = static_cast<std::int8_t>(blob[at + 0x07]);
    record.depthBias = static_cast<std::int8_t>(blob[at + 0x08]);
    record.byte09 = blob[at + 0x09];
    record.scale = f32At(blob, at + 0x0C);
    return record;
  }

  std::size_t FUN_0020f510_build_quads(const orphen::ported::model::Psc3Model &model,
                                       std::uint16_t poseColumn,
                                       const SpriteBuildInputs &inputs,
                                       std::vector<SpriteQuad> &out)
  {
    if (!model.spriteStrip || model.blob.size() < 0x10 || inputs.viewZ <= 0.0f)
    {
      return 0;
    }
    const std::span<const std::uint8_t> blob{model.blob};

    const std::uint16_t columnCount = u16At(blob, 0x00);
    const std::uint32_t columnTable = u32At(blob, 0x04);
    const std::uint32_t recordTable = u32At(blob, 0x08);

    // FUN_0020f510:0x0020f7bc. A column past the end draws nothing at all --
    // there is no clamp, and the whole record loop is inside the test.
    if (poseColumn >= columnCount)
    {
      return 0;
    }
    if (!fits(blob, columnTable + static_cast<std::size_t>(poseColumn) * 4, 4))
    {
      return 0;
    }
    const std::size_t columnAt = columnTable + static_cast<std::size_t>(poseColumn) * 4;
    const std::uint16_t firstRecord = u16At(blob, columnAt + 0);
    const std::uint16_t recordCount = u16At(blob, columnAt + 2);
    if (recordCount == 0)
    {
      return 0;
    }

    // FUN_0020f510:0x0020f5f0-0x0020f648. Both scales are **truncated to
    // integers** and then used as fixed point, so a distant sprite's size
    // quantises in visible steps. Y only gets its own divide when +0x150
    // differs from +0x14C.
    // 0x0020f6dc-0x0020f6f0, the bit-0x1000 branch: a flat `+0x14C * 256` with
    // no perspective divide, so a screen-space sprite is the same size wherever
    // its target stands.
    int projScaleX = 0;
    int projScaleY = 0;
    if (inputs.screenSpace1000)
    {
      projScaleX = FUN_0030bd20_trunc(inputs.scaleX14c * 256.0f);
      projScaleY = FUN_0030bd20_trunc(inputs.scaleY150 * 256.0f);
    }
    else
    {
      projScaleX = FUN_0030bd20_trunc(inputs.scaleX14c * kSpriteProjectionScale *
                                      inputs.projectionG / inputs.viewZ);
      projScaleY = projScaleX;
      if (inputs.scaleX14c != inputs.scaleY150)
      {
        projScaleY = FUN_0030bd20_trunc(inputs.scaleY150 * kSpriteProjectionScale *
                                        inputs.projectionG / inputs.viewZ);
      }
    }

    // FUN_0020f510:0x0020f6b0. Either a flat 0x80 or the average of the staged
    // ambient and the point-light contribution, saturated.
    float colour[3];
    for (int channel = 0; channel < 3; ++channel)
    {
      const int level =
          (inputs.flatColour || inputs.screenSpace1000)
              ? 0x80
              : std::min(0xFF, (static_cast<int>(inputs.ambient[channel]) +
                                static_cast<int>(inputs.dynamic[channel])) >>
                                   1);
      colour[channel] = static_cast<float>(level) / 128.0f;
    }

    std::size_t built = 0;
    // **Back to front.** The original seeds its cursor at the last record of the
    // run and walks down to the first.
    for (int index = static_cast<int>(recordCount) - 1; index >= 0; --index)
    {
      const std::size_t at = recordTable + (static_cast<std::size_t>(firstRecord) +
                                            static_cast<std::size_t>(index)) *
                                               0x10;
      if (!fits(blob, at, 0x10))
      {
        continue;
      }
      const SpriteRecord record = readSpriteRecord(blob, at);

      // FUN_0020f510:0x0020f960. The depth the GS z is keyed off is the view
      // depth plus the *entity's* bias (+0x133 * DAT_003520a0) plus this
      // record's own (+0x08 / 100). The screen position is unaffected by both:
      // it came out of projScale, which used the unbiased viewZ.
      const float depth =
          inputs.viewZ + inputs.entityDepthBias + static_cast<float>(record.depthBias) / 100.0f;
      int gsZ = 0;
      if (inputs.screenSpace1000)
      {
        // 0x0020f6a0: the branch reads +0x28 back as an integer, and only
        // rescales it when the entity carries a +0x133 bias.
        gsZ = inputs.screenSpaceGsZ;
        if (inputs.screenSpaceDepthBias != 0 && gsZ != 0)
        {
          float keyed = kDAT_00352090_screenDepthNumerator / static_cast<float>(gsZ) +
                        static_cast<float>(inputs.screenSpaceDepthBias) *
                            kDAT_00352094_screenDepthBiasScale;
          if (keyed < kDAT_00352098_screenDepthFloor)
          {
            keyed = kDAT_00352098_screenDepthFloor;
          }
          gsZ = FUN_0030bd20_trunc(kDAT_00352090_screenDepthNumerator / keyed);
        }
        if (gsZ < 0)
        {
          gsZ = 0;
        }
      }
      else if (inputs.forceFront)
      {
        // 0x0020f9a0: +0x08 bit 0x40 pins the GS z at 0xFFFF and buckets the
        // packet at 0x1005, past the 1..0xFFF the sorted ones use.
        gsZ = 0xFFFF;
      }
      else
      {
        gsZ = FUN_0030bd20_trunc(kDAT_003555a4_depthNumerator / depth + kDAT_003555a0_depthOffset);
        if (gsZ < 0)
        {
          gsZ = 0;
        }
      }

      // `w`/`h` are one *less* than the record's bytes, and the quad is built in
      // 1/16 sprite units so the centring divide keeps its fraction.
      const int w = static_cast<int>(record.width1) - 1;
      const int h = static_cast<int>(record.height1) - 1;
      if (w <= 0 || h <= 0)
      {
        continue;
      }
      const int recordScale = FUN_0030bd20_trunc(record.scale * 256.0f);
      const int quadW = (w * recordScale) >> 4;
      const int quadH = (h * recordScale) >> 4;

      int x0 = static_cast<int>(record.offsetX) * 16 + (w * 16 - quadW) / 2;
      int y0 = static_cast<int>(record.offsetY) * 16 + (h * 16 - quadH) / 2;
      int x1 = x0 + quadW;
      int y1 = y0 + quadH;

      // The flips swap the corners rather than the texture coordinates, which
      // is why the quad can come out with x1 < x0.
      if ((record.flags & 0x10) != 0)
      {
        x1 = x0;
        x0 = x0 + quadW;
      }
      if ((record.flags & 0x20) != 0)
      {
        y1 = y0;
        y0 = y0 + quadH;
      }

      // FUN_0020f510:0x0020fa4c-0x0020fa78. **The projected origin is the base
      // of every corner** -- pauVar27[1] is FUN_0020b600's integer output for
      // the entity position, and the sprite offsets are added onto it in GS
      // units. Note the asymmetry: X shifts by 8 and Y by 9, because the GS
      // output is 640x224 shown at 4:3 and its pixels are 2:1.
      const int gsX0 = inputs.gsOriginX + ((x0 * projScaleX) >> 8);
      const int gsX1 = inputs.gsOriginX + ((x1 * projScaleX) >> 8);
      const int gsY0 = inputs.gsOriginY + ((y0 * projScaleY) >> 9);
      const int gsY1 = inputs.gsOriginY + ((y1 * projScaleY) >> 9);

      // ---- host adaptation, and the only one in this function --------------
      //
      // The original hands those four GS numbers straight to the GIF. This port
      // draws through the same projection the world does, so the corners are
      // un-projected back to view space at the depth the GS z was keyed off.
      // Re-projecting them reproduces gsX0..gsY1 exactly -- this is the inverse
      // of the transform two lines up, not a restatement of it -- and the sprite
      // then depth-tests against world geometry with the record bias applied.
      float vertexDepth = depth;
      if (inputs.screenSpace1000)
      {
        // The original hands the GS four numbers and never has a world point at
        // all. The port re-projects, so the quad needs *a* depth: any value
        // reproduces the same pixels, but only the one the GS z was keyed off
        // depth-tests against the world the way the original's z does. The
        // caller recovers it by inverting the depth key, so the cursor sits at
        // its target's distance and is occluded by anything in front of it.
        vertexDepth = inputs.viewZ;
      }
      else if (inputs.forceFront || vertexDepth < kDAT_0035209c_spriteNearClip)
      {
        vertexDepth = inputs.viewZ;
      }
      const float perX =
          inputs.projectionScaleX != 0.0f ? vertexDepth / inputs.projectionScaleX : 0.0f;
      const float perY =
          inputs.projectionScaleY != 0.0f ? vertexDepth / inputs.projectionScaleY : 0.0f;

      SpriteQuad quad;
      quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
      quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
      quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
      quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
      quad.viewZ = vertexDepth;

      quad.u0 = static_cast<float>(record.u);
      quad.v0 = static_cast<float>(record.v);
      quad.u1 = static_cast<float>(record.u + w);
      quad.v1 = static_cast<float>(record.v + h);

      quad.blendMode = record.flags & 3;
      quad.colour[0] = colour[0];
      quad.colour[1] = colour[1];
      quad.colour[2] = colour[2];
      // Mode 0 never enables ABE, so its alpha is not consulted; the original
      // still writes 0x80 there.
      quad.colour[3] =
          quad.blendMode == 0 ? 1.0f : static_cast<float>(record.alpha2 >> 1) / 128.0f;
      quad.textureSlot = inputs.textureSlot;

      // FUN_0020f510:0x00210170. The display list is 4096 depth buckets plus one
      // above them; gsZ rises as the sprite comes nearer, so ascending bucket is
      // back to front and 0x1005 is drawn over everything.
      quad.displayListBucket = inputs.forceFront ? 0x1005 : std::clamp(gsZ >> 4, 1, 0xFFF);
      quad.depthTest = !inputs.forceFront;

      out.push_back(quad);
      ++built;
    }

    return built;
  }

  SpriteQuad FUN_002d3058_build_particle_quad(const ParticleQuadInputs &inputs)
  {
    SpriteQuad quad;

    // FUN_0020b600 leaves Q -- 1/w, with w clamped to a floor -- in lane W,
    // because its vftoi0 is masked to xyz. FUN_002d3058 reads it back as a
    // float and multiplies the two literals by it directly, so a particle's
    // size is a plain 1/z with no projection term in it at all.
    const float viewZ = inputs.viewZ > kDAT_0035209c_spriteNearClip
                            ? inputs.viewZ
                            : kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / viewZ;

    const int halfW = static_cast<int>(static_cast<float>(inputs.widthUnits) * 40.0f * q);
    const int halfH = static_cast<int>(static_cast<float>(inputs.heightUnits) * 20.0f * q);

    // 0x002d30d8-0x002d3128. The origin is the top-left corner.
    const int gsX0 = inputs.gsOriginX;
    const int gsY0 = inputs.gsOriginY;
    const int gsX1 = inputs.gsOriginX + halfW;
    const int gsY1 = inputs.gsOriginY + halfH;

    const float perX =
        inputs.projectionScaleX != 0.0f ? viewZ / inputs.projectionScaleX : 0.0f;
    const float perY =
        inputs.projectionScaleY != 0.0f ? viewZ / inputs.projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
    quad.viewZ = viewZ;

    quad.u0 = kParticleU0;
    quad.v0 = kParticleV0;
    quad.u1 = kParticleU1;
    quad.v1 = kParticleV1;

    // GS RGBA where 0x80 is 1.0, so a particle can be brighter than white
    // before the additive blend -- and with red at 0xFF it is, by exactly 2x.
    quad.colour[0] = static_cast<float>(inputs.colour & 0xFFu) / 128.0f;
    quad.colour[1] = static_cast<float>((inputs.colour >> 8) & 0xFFu) / 128.0f;
    quad.colour[2] = static_cast<float>((inputs.colour >> 16) & 0xFFu) / 128.0f;
    quad.colour[3] = static_cast<float>((inputs.colour >> 24) & 0xFFu) / 128.0f;

    quad.blendMode = kParticleBlendMode;
    quad.textureSlot = kParticleTextureSlot;
    quad.displayListBucket = kParticleDisplayListBucket;
    quad.depthTest = true;
    return quad;
  }

} // namespace orphen::ported::render
