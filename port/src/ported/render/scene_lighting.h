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
// FUN_0020eec0:67-94 fills that block, which lives in a per-draw scratchpad
// context (DAT_70000000), *not* in the entity record -- FUN_0020c810:245 swaps
// its two arguments on the way in, which is easy to misread. For each of three
// entries in the global light table at DAT_00343898 (stride 0x14: float3
// position, u32 rgb, float radius; radius zero means disabled) it calls VU0
// program 0x198, which returns a normalised direction and a distance-attenuated
// colour. So lights 1..3 are the three nearest dynamic point lights, resolved
// to directional lights per draw.
//
// The port leaves them black. Reproducing them needs the light table and the
// VU0 falloff, which is its own piece of work. In s01_e024 the table holds 14
// entries with real positions and colours but every radius is 0, so the game
// zeroes all three slots too and nothing is currently lost.
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
#include <cmath>
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

    // ---- The per-material light floor ------------------------------------
    //
    // vf15.z, the MAXz at VU1 0x01d1 that puts a lower bound under every
    // intensity. **On by default since 2026-08-31**, when a hardware capture of
    // s01_e012's shop finally pinned it: the wood wall behind Volcan reads
    // (47, 29, 18) and (68, 41, 22) there; the port renders (33, 24, 16) and
    // (15, 16, 13) without the floor, and (45, 28, 19) and (50, 35, 24) with
    // it.
    //
    // It stayed off this long because s01_e024 -- the room the renderer was
    // built against -- cannot show it. Its primitives carry +0x2D = 0xBF, a
    // floor of 0.2, which their half-Lambert intensities already clear; turning
    // the floor on there moves 0.5% of the pixels and the frame mean by 0.02.
    // s01_e012's carry 0x7F, a floor of 0.4, and 52% of that frame changes.
    // --lighting-no-floor restores the old look for an A/B.
    bool applyLightFloor = true;

    // ---- Derived but not yet visually confirmed --------------------------
    //
    // Read straight out of the microprogram and believed correct, but not yet
    // matched against a reference frame. Defaults OFF; --lighting-unlit turns
    // it on. It is the natural explanation for the residual noted with the
    // light floor in port/README.md -- the shop lantern's glass still comes out
    // bluer than hardware, which is exactly the "dimmer and bluer" a lit draw
    // of an authored-bright colour gives -- but no primitive in grp_00c6 sets
    // the bit, so it is not the answer there.
    bool applyUnlitFlag = false;  // draw header byte 15 / primitive flag bit 8
    // Subdraw pass blending is NOT a toggle -- it is always on. The draw loop
    // walks every pass and takes its blend mode from the subdraw's texFlags:
    // FUN_00212058:139 puts the mode nibble in plVar5[6], :217 writes draw
    // header byte 8 = mode * 3, and VU1 0x1a7 loads three GS registers from
    // 608 + mode*3. Those blocks, read out of the save state, are:
    //
    //   0 -> ALPHA 0x44  (Cs-Cd)*As + Cd,  ZMSK 0   the opaque base
    //   1 -> ALPHA 0x44  same,             ZMSK 1   folded to 0 at full alpha
    //   2 -> ALPHA 0x48  (Cs- 0)*As + Cd,  ZMSK 1   additive
    //   3 -> ALPHA 0xa1  (Cd-Cs)*128>>7,   ZMSK 1   reverse subtract

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

    // ---- The dynamic point lights -----------------------------------------
    //
    // `DAT_00343888`, sixteen slots, script opcodes 0xBF..0xC7. `FUN_0020b430`
    // compacts the *live* ones -- radius non-zero -- into a VU0 list at
    // quadword 3 with the count at quadword 2, each entry three quadwords:
    // `{position, (r, r², 1/r²), colour/255}`. Read straight out of
    // `vu0Memory.bin`, which for the Dortin save state holds count 1 and
    // `(5.498, -2.684, -0.468) / (2, 4, 0.25) / (0.502, 0.502, 0.502)` --
    // exactly the one live table slot. That also settles the packing question
    // the doc had open: `FUN_0020b430` builds three *parallel* arrays in the
    // scratchpad and the `VSQI` loop at its tail interleaves them.
    //
    // The list is consumed two different ways, and the split is what the two
    // allocators are for. Table slots 0..2 become VU1's directional lights 1..3
    // on entity draws (`FUN_0020eec0`, VU0 program 0x33); everything from slot 3
    // up is summed flat into a per-entity tint (VU0 program 0x220). Map draws
    // run the whole list per vertex (VU0 program 0x1c falling into the loop at
    // 0x52). So opcode 0xC0, which allocates from slot 0, is the one that can
    // become a real directional light on characters, and 0xBF, which allocates
    // from slot 3, only ever tints them -- the reverse of what the dispatch
    // table's `light_alloc_directional` / `light_alloc_point` names suggest.
    struct PointLight
    {
      orphen::ported::psm2::Vec3 position{};
      float radius = 0.0f;
      float radiusSquared = 0.0f;
      float inverseRadiusSquared = 0.0f;
      float colour[3]{}; // byte / 255, the units FUN_0020b430 stores
      int tableSlot = -1;
    };
    static constexpr int kPointLightCapacity = 16;
    // Slots 0..2 of the table, the ones that reach VU1 as directional lights.
    static constexpr int kDirectionalTableSlots = 3;

    PointLight pointLights[kPointLightCapacity]{};
    int pointLightCount = 0;
    // How many of the compacted entries came from table slots 0..2. The list is
    // built in table order, so these are always the prefix -- which is exactly
    // what `_ctc2(uVar12)` tells VU0 program 0x220 to skip.
    int directionalPointLights = 0;

    // What a draw contributes on top of the scene block. Lights 1..3 are
    // per-entity and `additive` is per-vertex on the map path and per-entity on
    // the model path, so both live here and the caller decides how often to
    // rebuild it.
    struct DynamicContribution
    {
      float lightColour[3][3]{};
      orphen::ported::psm2::Vec3 lightDirection[3]{};
      float additive[3]{}; // 0..255, the byte triple VU1 divides by 128
      bool active = false;
    };

    // VU0 0x52..0x79, the per-point loop, for one point and the list from
    // `firstLight` on. Returns the byte triple VU1's second additive term reads.
    //
    //   reject unless |light - point| < r on every axis   (two SUBx, FMAND 0xE0)
    //   reject unless |d|² < r²                           (SUBy.w, FMAND 0x10)
    //   accumulate colour * clamp(1 - |d|²/r², 0, 1)
    //   min the sum to 2.0, multiply by 127.5, truncate
    //
    // Both rejections are strict: a sign flag is set only for a negative
    // result, so a point exactly on the boundary is rejected.
    void FUN_0020b430_pointLightBytes(const orphen::ported::psm2::Vec3 &point, int firstLight,
                                      float out[3]) const
    {
      float sum[3] = {0.0f, 0.0f, 0.0f};
      for (int index = firstLight; index < pointLightCount; ++index)
      {
        const PointLight &light = pointLights[index];
        const float deltaX = light.position.x - point.x;
        const float deltaY = light.position.y - point.y;
        const float deltaZ = light.position.z - point.z;
        if (!(deltaX < light.radius) || !(-deltaX < light.radius) ||
            !(deltaY < light.radius) || !(-deltaY < light.radius) ||
            !(deltaZ < light.radius) || !(-deltaZ < light.radius))
        {
          continue;
        }
        const float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        if (!(distanceSquared < light.radiusSquared))
        {
          continue;
        }
        const float attenuation =
            std::max(std::min(1.0f - distanceSquared * light.inverseRadiusSquared, 1.0f), 0.0f);
        sum[0] += light.colour[0] * attenuation;
        sum[1] += light.colour[1] * attenuation;
        sum[2] += light.colour[2] * attenuation;
      }

      for (int channel = 0; channel < 3; ++channel)
      {
        // MINIi 2.0, MULi 127.5, FTOI0 -- truncation, not rounding.
        out[channel] = std::trunc(std::min(sum[channel], 2.0f) * 127.5f);
      }
    }

    // VU0 0x33 (`_vcallms(0x198)`), one light against one point. Note it has
    // *no* rejection tests at all -- the clamp on `1 - |d|²/r²` is what turns a
    // light the entity is standing outside of into black.
    void FUN_0020eec0_resolveEntityLight(const PointLight &light,
                                         const orphen::ported::psm2::Vec3 &point,
                                         orphen::ported::psm2::Vec3 &direction,
                                         float colourOut[3]) const
    {
      const float deltaX = light.position.x - point.x;
      const float deltaY = light.position.y - point.y;
      const float deltaZ = light.position.z - point.z;
      const float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

      const float attenuation =
          std::max(std::min(1.0f - distanceSquared * light.inverseRadiusSquared, 1.0f), 0.0f);
      for (int channel = 0; channel < 3; ++channel)
      {
        // MULi by the LOI 255 then FTOI0, so this lands back in the 0..255 byte
        // units lightColour works in.
        colourOut[channel] = std::trunc(light.colour[channel] * attenuation * 255.0f);
      }

      // RSQRT against vf00.w gives 1/|d|. The VU raises a divide flag on a zero
      // denominator and carries on; the port leaves the direction at zero,
      // which the half-Lambert reads as an intensity of exactly 0.5.
      if (distanceSquared > 0.0f)
      {
        const float inverseLength = 1.0f / std::sqrt(distanceSquared);
        direction = {deltaX * inverseLength, deltaY * inverseLength, deltaZ * inverseLength};
      }
      else
      {
        direction = {};
      }
    }

    // Fills lights 1..3 and the flat tint for one entity, from its own position.
    // FUN_0020eec0 walks table slots 0, 1 and 2 in order and zeroes the entry
    // for a slot that is not live, so VU1 light k+1 is always table slot k.
    void buildEntityContribution(const orphen::ported::psm2::Vec3 &entityPosition,
                                 DynamicContribution &out) const
    {
      out = DynamicContribution{};
      if (pointLightCount == 0)
      {
        return;
      }
      out.active = true;

      for (int index = 0; index < directionalPointLights; ++index)
      {
        const PointLight &light = pointLights[index];
        if (light.tableSlot < 0 || light.tableSlot >= kDirectionalTableSlots)
        {
          continue;
        }
        FUN_0020eec0_resolveEntityLight(light, entityPosition,
                                        out.lightDirection[light.tableSlot],
                                        out.lightColour[light.tableSlot]);
      }

      // FUN_0020eec0:44-64. Everything past the directional budget, flat.
      FUN_0020b430_pointLightBytes(entityPosition, directionalPointLights, out.additive);
    }

    // The factor the port's draw paths must multiply their `colour / 128` by.
    void modulator(const orphen::ported::psm2::Vec3 &normal, float intensityFloor,
                   float out[3], const DynamicContribution *dynamic = nullptr) const
    {
      out[0] = out[1] = out[2] = 1.0f;
      if (!active)
      {
        return;
      }

      float accumulated[3] = {ambient[0], ambient[1], ambient[2]};
      for (int light = 0; light < kLightCount; ++light)
      {
        const orphen::ported::psm2::Vec3 &direction =
            (light > 0 && dynamic != nullptr && dynamic->active) ? dynamic->lightDirection[light - 1]
                                                                 : lightDirection[light];
        const float *colour = (light > 0 && dynamic != nullptr && dynamic->active)
                                  ? dynamic->lightColour[light - 1]
                                  : lightColour[light];
        const float dot = normal.x * direction.x + normal.y * direction.y +
                          normal.z * direction.z;
        // ADDw then MULy vf01y: (N.L + 1) * 0.5, then the MAXz floor.
        const float intensity = std::max((dot + 1.0f) * 0.5f, intensityFloor);
        for (int channel = 0; channel < 3; ++channel)
        {
          accumulated[channel] += colour[channel] * intensity;
        }
      }

      for (int channel = 0; channel < 3; ++channel)
      {
        // VU1 0x1da..0x1e0: `out += extra * colour / 128`, where the main term
        // is `colour/256 * accumulated`. Factoring the shared `colour` out --
        // which is what the port's draw paths have already done by dividing
        // their vertex colour by 128 -- leaves `accumulated/256 + extra/128`.
        out[channel] = accumulated[channel] / 256.0f;
        if (dynamic != nullptr && dynamic->active)
        {
          out[channel] += dynamic->additive[channel] / 128.0f;
        }
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
