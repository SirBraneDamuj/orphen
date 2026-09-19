#pragma once

// The background model: the fog cylinder a scene stands inside.
//
//   src/FUN_0020c290.c   the per-frame walk of four descriptors
//   src/FUN_0020c2f0.c   the draw of one of them
//   src/FUN_0022ce60.c   the PSB4 parse
//   src/FUN_00211b80.c   the packet build, a near-twin of the map's FUN_00211230
//   src/FUN_0022cde8.c   install by slot; FUN_0022a418:134 calls it at load
//   src/FUN_002651a0.c   opcode 0xE5, load a PSB4 into a slot
//   src/FUN_00265200.c   opcode 0xE6, that slot's shade byte and Z rotation
//
// **FUN_0020C290 is not a display-list kick**, which is what it looks like
// next to FUN_0020C5A8 and FUN_0020F3E0 in FUN_002239C8's draw block. It walks
// the four descriptors at DAT_00345A18 (stride 0x24, counted *down* from
// DAT_00345A84) and draws each one. It is the last call in that block and was
// the last one this port did not have, which is why s01_e013's boat deck sat
// in a black void where the retail game has weather.
//
// == Where the model comes from ==
//
// A PSB4 record in the scene bundle's **category 2**, the same category the
// PSM2 map lives in -- s01_e013's is id 0x009E, and it is the *first* record
// there, which is why `loadFirstPsm2FromSceneResources` walks straight past it
// looking for PSM2 magic.
//
// Nothing in the script installs it for that scene. FUN_0022A418:134 calls
// FUN_0022CDE8(sceneDescriptor, 0), which reads the **halfword at scene
// descriptor +0x08** and loads that id into slot 0 with its shade seeded to
// 0x80. Opcode 0xE5 is the other way in, for slots 1..3, and those start with
// a shade of 0 -- invisible until opcode 0xE6 raises it.
//
// == The file ==
//
// A 16-byte header: 'PSB4', then three section offsets.
//
//   +0x04  vertices: an s16 count, then xyz floats from +4 at a stride of 12
//   +0x08  primitives: an s16 count, then 22 bytes each
//   +0x0C  the UV animation script, the same format the map's section G uses
//
// Each primitive is a flags halfword, four u16 vertex indices, and four
// three-byte groups. `flags & 0x800` picks how those three bytes are read, and
// it is the same bit the map and PSC3 builders test:
//
//   set    (u, v, textureSlot) -- FUN_0022CE60 files u/v at record +0x18 and
//          the third byte at +0x08, and FUN_00211B80 reads +0x08 as the slot,
//          emitting `slot + 1` in the packet. That is the map path's
//          `globalSlot + 1`, so the byte **is a global texture slot**: every
//          one of s01_e013's 391 primitives says 8, the map's ninth and last
//          texture page, which no map primitive uses.
//   clear  a flat RGB, the untextured case, exactly as a map material slot
//          with a negative type carries one.
//
// The rest of the flags word, all of it confirmed against a GS dump of the
// live frame:
//
//   0x7000  the blend mode: 0x4000 -> 1, 0x2000 -> 2, else 3, and 0 when none
//           of the three are set. Every textured primitive here is mode 2,
//           additive, and the dump's ALPHA register reads 0x48 -- Cs*As + Cd.
//   0x0800  textured, above
//   0x0700  a CLUT bank, emitted as `bank + 7` in the packet's byte 7. It is
//           dead for an 8-bit page like this one, and the dump confirms it:
//           every backdrop draw carries the same CBP whatever the bank says.
//   0x00FE  **the vertex alpha**, halved. FUN_00211B80 writes
//           `(flags & 0xFE) << 23` into the colour's top byte, so 0x2F3F is
//           alpha 0x1F and 0x2B7F is 0x3F. With no blend mode it is 0x80.
//   0x8000  the UV animation selector, as the packet's byte 9. No primitive in
//           s01_e013's model sets it; 48 across the disc's 32 PSB4 files do.
//
// The colour itself is not in the file. FUN_00211B80 hands VU1 the literal
// DAT_00808080 for a textured primitive, which is why every backdrop vertex in
// the dump reads (128, 128, 128) -- neutral GS modulate, x1.0.
//
// == How it is placed ==
//
// FUN_0020C2F0 builds `Rz(descriptor +0x1C) * translate(DAT_0058BE80..88)` and
// multiplies it by the view matrix. FUN_0020BEC8:43-45 sets that translation to
// **the camera eye's x and y**, with z = eye z + fGpffff808c = 0.4. So the
// model rides the camera exactly, and a mesh 35 units across reads as a
// cylinder wrapped around the whole scene however far you walk.
//
// == Why it never occludes anything ==
//
// Its packets end in `MSCAL 0x228`, a different VU1 microprogram from the
// map's 0x13B/0x14B, and the dump shows what that program does differently:
// every backdrop vertex reaches the GS at **Z = 2** -- the far end of that
// frame's 2..1108 range, whatever the geometry's real depth -- with PRIM.FGE
// clear where every map primitive has it set, and ZBUF.ZMSK set where the map
// clears it. Fog off, depth write off, always behind.
//
// The port reproduces that by drawing the backdrop first, before the map, with
// both the depth test and the depth mask off. That is the same result by the
// only means GL offers: there is no way to ask for "compute this vertex's depth
// and then ignore it" that is cheaper or clearer than not testing at all.

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace orphen::ported::render
{

  // One primitive of a PSB4, in the form FUN_0022CE60 leaves in RAM.
  struct Psb4Primitive
  {
    std::array<std::uint16_t, 4> vertexIndices{};
    // Four (u, v) byte pairs, one per corner, in texels of a 256-texel page.
    std::array<std::uint8_t, 8> textureCoordinates{};
    // Record +0x08 for a textured primitive: a global texture slot.
    std::uint8_t textureSlot = 0;
    // The flat RGB an untextured primitive carries in the same three bytes.
    std::array<std::uint8_t, 3> flatColour{};
    std::uint16_t flags = 0;

    // FUN_00211B80:38. Three corners when the last two indices repeat.
    bool triangle() const { return vertexIndices[2] == vertexIndices[3]; }
    std::size_t cornerCount() const { return triangle() ? 3u : 4u; }
    bool textured() const { return (flags & 0x0800u) != 0; }

    // FUN_00211B80:44-58, tested in this order.
    int blendMode() const
    {
      if ((flags & 0x7000u) == 0)
      {
        return 0;
      }
      if ((flags & 0x4000u) != 0)
      {
        return 1;
      }
      return (flags & 0x2000u) != 0 ? 2 : 3;
    }

    // FUN_00211B80:120-131. Mode 0 is left at the packet's own 0x80.
    std::uint8_t alpha() const
    {
      return blendMode() == 0 ? 0x80u : static_cast<std::uint8_t>((flags & 0x00FEu) >> 1);
    }
  };

  struct Psb4Model
  {
    bool valid = false;
    std::string diagnostic;
    std::vector<orphen::ported::psm2::Vec3> vertices;
    std::vector<Psb4Primitive> primitives;
  };

  // FUN_0022CE60. A blob that is not PSB4, or whose sections run off the end,
  // comes back invalid with `diagnostic` saying which.
  Psb4Model FUN_0022ce60_parse_psb4(std::span<const std::uint8_t> decoded);

  // One of the four entries at DAT_00345A18.
  struct BackgroundSlot
  {
    Psb4Model model;
    std::uint16_t resourceId = 0;
    // +0x1C, set by opcode 0xE6 as `angle / DAT_00352cd4` (100000).
    float DAT_00345a34_angleZ = 0.0f;
    // +0x20. FUN_0020C2F0 returns immediately when this is zero.
    std::uint8_t DAT_00345a38_shade = 0;

    bool drawable() const { return model.valid && DAT_00345a38_shade != 0; }
  };

  // What the renderer needs for one primitive, in world space.
  struct BackgroundQuad
  {
    std::array<orphen::ported::psm2::Vec3, 4> corner{};
    std::array<float, 4> u{};
    std::array<float, 4> v{};
    std::size_t cornerCount = 4;
    int blendMode = 0;
    // -1 for the untextured case, which draws `colour` alone.
    int textureSlot = -1;
    // **Raw GS bytes**, alpha last, because the two halves scale differently:
    // the map path divides rgb by 256 and lets GL_RGB_SCALE 2 put the octave
    // back, so the GS's x1.99 ceiling survives, while alpha is a plain 0..1
    // where 0x80 is fully opaque. Handing the renderer the bytes keeps that
    // decision at the point that makes it.
    std::array<std::uint8_t, 4> colour{0x80, 0x80, 0x80, 0x80};
  };

  // FUN_0020C2F0's transform and FUN_00211B80's per-primitive packet, together.
  // `origin` is DAT_0058BE80..88, which FUN_0020BEC8 sets from the camera eye.
  void FUN_0020c2f0_build_background_quads(const BackgroundSlot &slot,
                                           const orphen::ported::psm2::Vec3 &origin,
                                           std::vector<BackgroundQuad> &out);

  // fGpffff808c, the constant FUN_0020BEC8:29 adds to the eye's z.
  inline constexpr float kfGpffff808c_backgroundLift = 0.4f;

} // namespace orphen::ported::render
