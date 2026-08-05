#pragma once

// The VU1 per-vertex lighting model, read out of the microprogram rather than
// fitted to screenshots.
//
// Source: `vu1MicroMem.bin` and `vu1Memory.bin` from a PCSX2 save state,
// disassembled with scripts/vu_disasm.py. The full reference for the
// microprogram -- every entry point, the data-memory map, the draw header -- is
// docs/vu1_microprogram.md; this header carries only the part the port
// implements.
//
// The vertex loop shared by the map and entity geometry paths runs at
// 0x01b2..0x01e0; entries 0x12c / 0x13b / 0x14b all converge on it, which is
// why one model covers both. The relevant half:
//
//   01b3  LQI.xyzw vf16, (vi08++)          per-vertex normal, packed ints
//   01b4  LQI.xyzw vf17, (vi04++)          per-vertex colour, packed ints
//   01b8  ILW.w vi12, 2(vi01)              header byte 15
//   01b9  ITOF0.xyz vf16, vf16   I=1/128
//   01ba  ITOF0.xyzw vf19, vf17
//         IBNE vi12, vi00 -> 01e1          byte 15 != 0: skip lighting entirely
//   01bd  MULi.xyz  vf16, vf16, I          normal *= 1/128  -> unit normal
//   01be  MTIR vi13, vf16w                 normal.w is a bone matrix address
//   01c2  MULAx.xyz  ACC,  vf27, vf16x  \  optional: rotate the normal by the
//   01c3  MADDAy.xyz ACC,  vf28, vf16y   >  bone matrix at 192(vi13), when
//   01c5  MADDz.xyz  vf16, vf29, vf16z  /   header byte 10 says so
//   01ca  MULAx.xyzw  ACC,  vf24, vf16x \  vf24..vf26 = 0x2a0..0x2a2, whose
//   01cb  MADDAy.xyzw ACC,  vf25, vf16y  >  *columns* are the four light
//   01cc  MADDz.xyzw  vf16, vf26, vf16z /   directions -> four N.L values
//   01cd  MULx.xyz vf19, vf19, vf01x       colour *= 1/256
//   01ce  ADDw.xyzw vf16, vf16, vf00w      N.L + 1
//   01cf  MUL.xyz vf18, vf28, vf19         vf28 = 0x2a7 = ambient; * colour
//   01d0  MULy.xyzw vf16, vf16, vf01y      * 0.5   -> half-Lambert
//   01d1  MAXz.xyzw vf16, vf16, vf15z      floor, from header byte 14
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
// Where each field comes from
// ---------------------------
//
// Light 0 and the ambient are the scene's, uploaded once per frame by
// FUN_00200e38:121-167:
//
//   0x2a0..0x2a2 lane x  VIFcode 0x6c0302a0, three quadwords holding only .x:
//                        (-DAT_003439c8, -DAT_003439cc, -DAT_003439d0). The EE
//                        negates on upload, so the dot product is against -D.
//   0x2a3  lane x/y/z <- uGpffffb702 / b701 / b700, i.e. the u32 at ffffb700
//                        read as 0xRRGGBB.
//   0x2a7  lane x/y/z <- uGpffffb6fe / b6fd / b6fc, same convention.
//
// Lights 1..3 are per *entity*, in lanes y/z/w. FUN_0020eec0:112-142 uploads
// the entity's block at +0xB0 -- three records of {float3 direction, u32 rgb} --
// to 0x27a..0x27c (the colours, as bytes) and 0x27d..0x27f (the directions, as
// floats), and micro-program 0x015 transposes them into 0x2a0..0x2a2 lanes
// y/z/w and ITOF0s the colours into 0x2a4..0x2a6. Micro-program 0x07b, which
// FUN_00212058:354 appends after every entity, clears all three again --
// which is why a save state taken mid-frame shows 0x2a4..0x2a6 as (0,0,0,1).
//
// FUN_0020eec0:67-94 is what fills entity+0xB0: for each of three entries in
// the global light table at DAT_00343898 (stride 5 floats, first float zero
// means disabled) it calls VU0 program 0x198, which returns a normalised
// direction and a distance-attenuated colour. So lights 1..3 are the three
// nearest dynamic point lights, resolved to directional lights per entity.
//
// The port leaves them black. Reproducing them needs the light table and the
// VU0 falloff, which is its own piece of work; in every scene examined here the
// table is empty and the block is zero, so nothing is currently lost.
//
// Not implemented here: the second additive term at 0x01da..0x01e0, enabled by
// header byte 11 (hardcoded 1 in both builders). It adds `extra * colour / 128`
// from a fifth per-vertex stream at TOPS+0x2f. For map draws that stream is the
// dynamic point-light contribution, computed by the VU0 program at instruction
// 0x1c -- see the doc. For entity draws micro-program 0x015 broadcasts
// entity+0x1BC into all four slots, which FUN_0020eec0:44-64 fills from the
// same VU0 loop over whatever lights did not make the top three. Zero in every
// scene examined, so the term contributes nothing today.

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
    // The columns of the matrix at 0x2a0..0x2a2, already negated the way the EE
    // uploads them, so the intensity is a plain dot product against this.
    orphen::ported::psm2::Vec3 lightDirection[kLightCount]{};
    // 0x2a7, same units as lightColour.
    float ambient[3]{};
    // False until a scene pushes the block through, so an unlit fallback stays
    // available rather than silently rendering everything black.
    bool active = false;

    // ---- The specular pass ------------------------------------------------
    //
    // A *second* directional light, and the thing that puts the sheen on the
    // treasure chests. FUN_00212058:221-258 appends a whole extra GIF packet to
    // a primitive's draw whenever its +0x0C byte is non-zero, and VU1 0x0200
    // fills it: same triangles, no texture, gouraud, ABE on, drawn in the
    // scene's light-0 colour with a per-vertex alpha of
    //
    //   alpha = max(0, (dot(N, H) - t) * Q) * vertexAlpha * 0.5     [GS units]
    //     t = primitive +0x0D / 256                     the same byte as the
    //                                                   light floor, reused
    //     Q = (primitive +0x0C / 256) / (1 - t)
    //
    // The GS state it selects is register block 2, whose ALPHA_1 is 0x48 --
    // `(Cs - 0) * As >> 7 + Cd`, pure additive -- with ZBUF ZMSK set, so it
    // tests depth but does not write it.
    //
    // H is VU1 mem[0x18], which FUN_00200e38:55-66 builds as
    //
    //   H = normalise(-(DAT_0058bea0 + DAT_003439c8))
    //
    // DAT_0058bea0 is `normalise(lookAtTarget - eye)`, the camera forward
    // (FUN_00216aa0:436-449), and DAT_003439c8 is the scene light vector. Both
    // point *away* from the surface, so negating the sum gives
    // `normalise(toEye + toLight)` -- the textbook Blinn-Phong half-vector.
    // Verified against s01_e24.bin to six decimals.
    //
    // The threshold-and-rescale is a specular exponent by another name: it
    // shifts the highlight's onset and renormalises so it still reaches full
    // strength at dot == 1. grp_0172 uses three settings -- (+0x0C, +0x0D) of
    // (32, 0), (128, 127) and (255, 223) -- giving a broad faint sheen, a
    // medium one, and a tight bright one that only fires within 30 degrees.
    orphen::ported::psm2::Vec3 gleamDirection{};
    float gleamColour[3]{}; // 0x2a3, the light-0 colour, in 0..255 byte units
    bool gleamActive = false;

    // The opacity before the per-vertex alpha factor, in 0..1 rather than the
    // GS's 0..128. Returns 0 when the primitive does not carry the pass.
    static float gleamOpacity(float dotNH, std::uint8_t thresholdByte,
                              std::uint8_t scaleByte)
    {
      const float threshold = static_cast<float>(thresholdByte) / 256.0f;
      const float span = 1.0f - threshold;
      if (scaleByte == 0 || span <= 0.0f)
      {
        return 0.0f;
      }
      const float scale = (static_cast<float>(scaleByte) / 256.0f) / span;
      return std::max(0.0f, (dotNH - threshold) * scale);
    }

    // vf15.z, the MAXz floor under every intensity. The draw header carries it
    // as `~b` in byte 14 and the VU scales by vf01.z = 1/320, so a source byte
    // of 255 means no floor and 0 means 0.797. The values that actually occur
    // land on a clean ladder -- 0.05, 0.1, 0.2, 0.4, 0.5, 0.6 -- which is what
    // an authored per-material minimum brightness looks like.
    static float floorFromSourceByte(std::uint8_t sourceByte)
    {
      return static_cast<float>(static_cast<std::uint8_t>(~sourceByte)) / 320.0f;
    }

    // The factor the port's draw paths must multiply their `colour / 128` by.
    void modulator(const orphen::ported::psm2::Vec3 &normal, float intensityFloor,
                   float out[3]) const
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
