#pragma once

// The VU1 per-vertex lighting model, read out of the microprogram rather than
// fitted to screenshots.
//
// Source: `vu1MicroMem.bin` from a PCSX2 save state of s01_e24, disassembled
// with scripts/vu_disasm.py. The vertex loop shared by the map and entity
// geometry paths runs at 0x01b2..0x01e0; entries 0x12c / 0x13b / 0x14b all
// converge on it, which is why one model covers both. The relevant half:
//
//   01b3  LQI.xyzw vf16, (vi08++)          per-vertex normal, packed ints
//   01b4  LQI.xyzw vf17, (vi04++)          per-vertex colour, packed ints
//   01b9  ITOF0.xyz vf16, vf16   I=1/128
//   01ba  ITOF0.xyzw vf19, vf17
//   01bd  MULi.xyz  vf16, vf16, I          normal *= 1/128  -> unit normal
//   01be  MTIR vi13, vf16w                 normal.w is a bone index
//   01c2  MULAx.xyz  ACC,  vf27, vf16x  \  optional: rotate the normal by the
//   01c3  MADDAy.xyz ACC,  vf28, vf16y   >  bone matrix at 192(vi13), when the
//   01c5  MADDz.xyz  vf16, vf29, vf16z  /   header says the model is skinned
//   01ca  MULAx.xyzw  ACC,  vf24, vf16x \  vf24..vf26 = 0x2a0..0x2a2, whose
//   01cb  MADDAy.xyzw ACC,  vf25, vf16y  >  rows are the four light
//   01cc  MADDz.xyzw  vf16, vf26, vf16z /   directions -> four N.L values
//   01cd  MULx.xyz vf19, vf19, vf01x       colour *= 1/256
//   01ce  ADDw.xyzw vf16, vf16, vf00w      N.L + 1
//   01cf  MUL.xyz vf18, vf28, vf19         vf28 = 0x2a7 = ambient; * colour
//   01d0  MULy.xyzw vf16, vf16, vf01y      * 0.5   -> half-Lambert
//   01d1  MAXz.xyzw vf16, vf16, vf15z      floor, from the per-draw header
//   01d2  MULAx.xyzw  ACC,  vf24, vf16x \  vf24..vf27 = 0x2a3..0x2a6, the four
//   01d3  MADDAy.xyzw ACC,  vf25, vf16y  >  light colours, weighted by the
//   01d4  MADDAz.xyzw ACC,  vf26, vf16z  |  four intensities
//   01d5  MADDw.xyzw  vf16, vf27, vf16w /
//   01d6  MUL.xyz vf16, vf16, vf19         * colour
//   01d7  ADD.xyz vf16, vf16, vf18         + ambient * colour
//   01d8  MINI.xyz vf16, vf16, vf29        vf29 = 0x14 = (255, 255, 255)
//   01d9  FTOI0.xyz vf17, vf16             -> the GIF packet's vertex colour
//
// vf01 comes from `LQ.xyzw vf01, 28(vi00)` at instruction 0x0000 and reads
// (1/256, 0.5, 1/320, 0.01) in the dump. vf00 is the hardwired (0, 0, 0, 1),
// so `ADDw ..., vf00w` is `+ 1.0`. So, in full:
//
//   i_k  = max((dot(n, L_k) + 1) * 0.5, floor)          k = 0..3
//   out  = min(colour/256 * (ambient + sum_k C_k * i_k), 255)
//
// Two conventions matter for reproducing this in GL:
//
//   * The colours are byte-valued. The EE uploads 0x2a3..0x2a7 with VIFcode
//     0x6e0542a3 -- UNPACK V4-8 unsigned, five quadwords -- and the VU's own
//     init program at 0x0000..0x000f converts them in place with ITOF0. So
//     0x708090 arrives as the floats (112, 128, 144), and there is no /255
//     anywhere in the microprogram.
//   * The output is a GS vertex colour, which MODULATE applies as
//     `texel * out / 128`. The port's draw paths already divide their vertex
//     colours by 128, so what they need multiplied in is exactly
//     `(ambient + sum_k C_k * i_k) / 256` -- which is what modulator() returns.
//
// The light data reaches VU memory through FUN_00200e38:121-167:
//
//   0x2a0..0x2a2  VIFcode 0x6c0302a0, three quadwords holding only .x:
//                 (-DAT_003439c8, 0, 0, 0), (-DAT_003439cc, ...),
//                 (-DAT_003439d0, ...). Read as rows that is a single
//                 direction in light slot 0 and zero in slots 1..3. The EE
//                 negates on upload, so the microprogram's dot product is
//                 against -D.
//   0x2a3..0x2a7  VIFcode 0x6e0542a3, five quadwords of bytes:
//                 0x2a3 <- uGpffffb700..702 (the scene's "light 2")
//                 0x2a4..0x2a6 <- zero in every scene observed
//                 0x2a7 <- uGpffffb6fc..6fe (the scene's "light 1")
//
// So the port's already-parsed environment block maps straight on:
// uGpffffb6fc is the ambient term and uGpffffb700 is directional light 0.

#include "ported/psm2/psm2_runtime.h"

#include <algorithm>
#include <cstdint>

namespace orphen::ported::render
{

  struct SceneLighting
  {
    static constexpr int kLightCount = 4;

    // 0x2a3..0x2a6, in the microprogram's 0..255 byte units.
    float lightColour[kLightCount][3]{};
    // The rows of the matrix at 0x2a0..0x2a2, already negated the way the EE
    // uploads them, so the intensity is a plain dot product against this.
    orphen::ported::psm2::Vec3 lightDirection[kLightCount]{};
    // 0x2a7, same units as lightColour.
    float ambient[3]{};
    // vf15.z, the MAXz floor on every intensity: `itof(header[2].z) * 1/320`.
    // The header is VIFcode 0x6e03c000 -- UNPACK V4-8 unsigned, three
    // quadwords -- so header[2].z is a 0..255 byte and the floor spans
    // 0..0.797. FUN_00212058:228 writes it as `~*(byte *)(iVar19 + 0xd)`, the
    // complement of a per-draw record byte whose meaning is not yet
    // identified, so what the byte selects is still open.
    //
    // Left at zero, which is what a source byte of 255 produces, and which is
    // a no-op since half-Lambert is already >= 0. Anything else lifts unlit
    // surfaces, so this is the next thing to pin down if shadowed geometry
    // reads too dark.
    float intensityFloor = 0.0f;
    // False until a scene pushes the block through, so an unlit fallback stays
    // available rather than silently rendering everything black.
    bool active = false;

    // The factor the port's draw paths must multiply their `colour / 128` by.
    void modulator(const orphen::ported::psm2::Vec3 &normal, float out[3]) const
    {
      out[0] = out[1] = out[2] = 1.0f;
      if (!active)
      {
        return;
      }

      float accumulated[3] = {ambient[0], ambient[1], ambient[2]};
      for (int light = 0; light < kLightCount; ++light)
      {
        const orphen::ported::psm2::Vec3 &direction = lightDirection[light];
        const float dot = normal.x * direction.x + normal.y * direction.y +
                          normal.z * direction.z;
        // ADDw then MULy vf01y: (N.L + 1) * 0.5, then the MAXz floor.
        const float intensity = std::max((dot + 1.0f) * 0.5f, intensityFloor);
        for (int channel = 0; channel < 3; ++channel)
        {
          accumulated[channel] += lightColour[light][channel] * intensity;
        }
      }

      for (int channel = 0; channel < 3; ++channel)
      {
        out[channel] = accumulated[channel] / 256.0f;
      }
    }

    // Unpacks a 0xRRGGBB scene global into the byte-valued triple the
    // microprogram works in.
    static void unpack(std::uint32_t packedRgb, float out[3])
    {
      out[0] = static_cast<float>((packedRgb >> 16) & 0xFF);
      out[1] = static_cast<float>((packedRgb >> 8) & 0xFF);
      out[2] = static_cast<float>(packedRgb & 0xFF);
    }
  };

} // namespace orphen::ported::render
