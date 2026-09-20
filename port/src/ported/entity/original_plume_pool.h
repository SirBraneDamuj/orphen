#pragma once

// The **tenth** particle system: rising plumes of smoke with flames at their
// base. Opcode 0x10E arms one, and it is what puts the fire and the smoke
// columns on the burning ship in s01_e013's monster animatic.
//
//   src/FUN_0021fa88.c  0x0021FA88  carve both pools and mark every record free
//   src/FUN_0021f6e8.c  0x0021F6E8  open an emitter -- opcode 0x10E's target
//   src/FUN_0021f7a8.c  0x0021F7A8  the same, plus a horizontal drift
//   src/FUN_0021f870.c  0x0021F870  shed one smoke puff
//   src/FUN_0021f980.c  0x0021F980  shed one flame
//   src/FUN_0021fb68.c  0x0021FB68  step one emitter
//   src/FUN_00220210.c  0x00220210  step one puff, and draw it
//   src/FUN_00220028.c  0x00220028  walk both pools -- FUN_002192C0's tenth
//   src/FUN_00262a98.c  0x00262A98  opcode 0x10E itself
//   src/FUN_00219368.c  0x00219368  the debug menu's own call, which is what
//                                   names the arguments
//
// == Two pools, not one ==
//
// FUN_0021FA88 carves 36000 bytes for **1000 puffs of 0x24** and then 6000 for
// **100 emitters of 0x3C**, in that order, and marks all of both free. The
// counts live at iGpffffbbf4 (DAT_00355B64, puffs) and iGpffffbbf8
// (DAT_00355B68, emitters); the gate is iGpffffad54 (DAT_00354CC4), which any
// spawn raises and FUN_00220028 drops once both counts reach zero.
//
// An emitter is a point that rises, sheds a burst of puffs every 0x140 ticks
// and starts over when its life runs out. A puff is one sprite with a life, a
// size and a drift. Nothing else references either pool, so the emitters are
// the only thing that ever fills the puff array.
//
// == What the ten operands mean ==
//
// FUN_00262A98 reads ten expressions and hands FUN_0021F6E8 nine of them in a
// different order, so the two are kept apart in ScriptPlumeEmitter. Five are
// divided by 100000; the rest go through raw. In stream order:
//
//   0  burstCount   how many puffs a burst sheds. Straight into +0x26.
//   1  riseSpeed    how fast the emitter climbs, per tick over 32
//   2  size         the puff's half-extent before the growth below
//   3  lifeUnits    **times 32** into both +0x2A and +0x2C. Zero reads as one.
//   4  x
//   5  y
//   6  z
//   7  cycles       how many times the emitter restarts. See below.
//   8  mode         0, 1 or 2; 1 adds the flame, 2 makes it a one-shot
//   9  colour       a packed RGB, or zero for the sheet's own palette
//
// **`cycles` of 99 is immortal.** FUN_0021FB68 only decrements +0x38 when it is
// **below** 0x63, and 99 *is* 0x63, so an emitter armed with 99 restarts for
// ever -- until the scene drops the gate. s01_e013's ship emitters are all 99.
//
// **`mode` 2 is the one-shot, and it is spelled twice.** FUN_0021F6E8 stores
// `mode == 2` into +0x39 and then stores 0 into +0x24, so a mode-2 emitter has
// no flame. +0x39 does two things: it pins the growth factor at 1.0, and at the
// very end of every step it writes 0 into +0x2A, which makes the *next* step
// take the expiry branch. So a one-shot emitter sheds exactly one burst.
//
// == The growth factor ==
//
// `ratio` is the emitter's remaining life over its total, so it starts at 1 and
// falls to 0. The puff size is multiplied by `(1 - ratio) + 1`, which runs from
// 1 to 2: a plume's puffs get bigger as the emitter ages, which is what makes
// the column widen as it rises.
//
// == The flame ==
//
// With +0x24 set, and only while `ratio` is still above **0.9** -- the first
// tenth of the emitter's life -- every step also sheds one FUN_0021F980 record.
// That one is a different animal from a smoke puff:
//
//   * it does not move at all (its rise is passed as a literal zero, and
//     FUN_00220210 skips the whole movement block for it),
//   * its alpha is a plain ramp from 255 down to 0 rather than the smoke's
//     triangle,
//   * it walks a **six-frame** strip at DAT_003158F8, one frame per drawn
//     frame, instead of holding one of the three at DAT_00315898, and
//   * it takes CLUT bank 5 where a smoke puff takes 1, 2 or 6.
//
// **The strip only advances on a frame the record is actually drawn**, because
// FUN_00220210 advances it after the clip reject and after the near cutoff. A
// flame that spends a moment off screen comes back where it left off, and this
// port reproduces that by advancing it in the publish pass rather than the
// step. A headless run therefore never advances it, which costs nothing:
// nothing but the UV lookup reads +0x21.
//
// == The CLUT bank is a coin flip ==
//
// Every emitter step rolls once and picks bank **1** on an odd word and bank
// **6** on an even one, so consecutive bursts alternate palettes at random.
// With the flame flag set and `ratio` above **0.8**, that is overridden to bank
// **2** -- a brighter palette for the first fifth of the life, under the
// flames. The roll happens whether or not the flag is set, so the generator
// advances identically either way.
//
// == The quad ==
//
// The same screen-space rectangle the dust pool builds: the record's world
// position projects to an integer GS origin and each corner is
// `corner * 400 * q` away from it, where the corners are `+/-size*16` in x and
// `+/-size*8` in y. The 2:1 is the GS's own -- x counts in 1/16 pixel and y in
// 1/8 of a line -- so that is a square puff, not a wide one.
//
// Texture slot **0x20**, which is at or above 0x18 and therefore a 4-bit page,
// so the packet's high byte is a real CLUT bank and bank 0 is a real palette
// rather than "no bank". The smoke frames are three 63x63 tiles along the
// bottom of the sheet; the flame frames are six 23x39 tiles in a row above
// them. A record picks its smoke tile once, at spawn, with `roll() % 3`.
//
// Packet +0x0C is `0x10000080 | 0x4000`, so blend mode 1, the alpha blend --
// unless the colour word carries an alpha byte of its own, in which case the
// packet gets 0x8000 instead and the quad goes out additive with the colour
// used raw. Nothing in s01_e013 takes that path.
//
// == The near cutoff ==
//
// DAT_003523EC / F0 / F4 are 0.7, 0.6 and 0.1: a record whose projected q is
// above 0.7 is dropped, and between 0.6 and 0.7 its alpha is scaled by
// `(0.7 - q) / 0.1`. q is 1/viewZ, so 0.7 is 1.43 units from the eye. The haze
// field has the same three constants at different values.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace orphen::ported::entity
{

  // One record of the 100 x 0x3C pool at puGpffffbbfc.
  struct PlumeEmitter
  {
    // +0x00. Where it is now; the puffs are shed around this.
    float x00 = 0.0f;
    float y04 = 0.0f;
    float z08 = 0.0f;
    // +0x0C. Where it started, and where every restart puts it back.
    float spawnX0c = 0.0f;
    float spawnY10 = 0.0f;
    float spawnZ14 = 0.0f;
    // +0x18. How fast it climbs, per tick over 32. A puff inherits a tenth of
    // it.
    float riseSpeed18 = 0.0f;
    // +0x1C / +0x20. A horizontal drift, and the direction of it. **Only
    // FUN_0021F7A8 writes these** -- FUN_0021F6E8, which is the only thing the
    // script can reach, leaves whatever the previous occupant of the slot had.
    // That is the original's own recycling, reproduced rather than tidied.
    float driftSpeed1c = 0.0f;
    float driftAngle20 = 0.0f;
    // +0x24. Non-zero adds the flame and the bright bank. Forced to zero for a
    // one-shot.
    std::int8_t flameFlag24 = 0;
    // +0x26. Puffs per burst.
    std::int16_t burstCount26 = 0;
    // +0x28. Ticks until the next burst; reloaded with 0x140.
    std::uint16_t burstTimer28 = 0;
    // +0x2A / +0x2C. Ticks left of this cycle, and the cycle's length. Both are
    // the operand times 32.
    std::int16_t remaining2a = 0;
    std::int16_t total2c = 0;
    // +0x30. The puff half-extent, before the growth factor.
    float size30 = 0.0f;
    // +0x34. A packed RGB, or zero.
    std::uint32_t colour34 = 0;
    // +0x38. Restarts left. Negative is free, and 0x63 never decrements.
    std::int8_t cycles38 = -1;
    // +0x39. The one-shot latch.
    std::int8_t oneShot39 = 0;

    bool alive() const { return cycles38 >= 0; }
  };

  // One record of the 1000 x 0x24 pool at puGpffffbc00.
  struct PlumePuff
  {
    // +0x00.
    float x00 = 0.0f;
    float y04 = 0.0f;
    float z08 = 0.0f;
    // +0x0C. Per tick over 32, and it drives the drift as well as the climb.
    float rise0c = 0.0f;
    // +0x10. A fresh random heading at spawn, for both spawners.
    float driftAngle10 = 0.0f;
    // +0x14 / +0x16. Ticks elapsed, counting up, and the total.
    std::uint16_t elapsed14 = 0;
    std::int16_t total16 = 0;
    // +0x18. Half-extent before the 16 and the 400.
    float size18 = 0.0f;
    // +0x1C.
    std::uint32_t colour1c = 0;
    // +0x20. Negative is free, 0 is smoke, 1 is flame.
    std::int8_t kind20 = -1;
    // +0x21. The tile index: fixed at spawn for smoke, stepped per drawn frame
    // for a flame.
    std::uint8_t frame21 = 0;
    // +0x22. The CLUT bank, read as a **signed** byte and shifted into the
    // packet's high byte.
    std::int8_t clutBank22 = 0;

    bool alive() const { return kind20 >= 0; }
  };

  // FUN_0021F6E8's and FUN_0021F7A8's arguments in one shape. `writeDrift` is
  // what tells them apart: FUN_0021F6E8 never touches +0x1C or +0x20.
  struct PlumeEmitterSpawn
  {
    float riseSpeed = 0.0f;
    float size = 0.0f;
    float driftSpeed = 0.0f;
    float driftAngle = 0.0f;
    bool writeDrift = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::int16_t burstCount = 0;
    // Before the times 32. Zero is read as one.
    std::int16_t lifeUnits = 0;
    std::uint8_t cycles = 0;
    // 0, 1 or 2. See the header note.
    std::int8_t mode = 0;
    std::uint32_t colour = 0;
  };

  // The four texels of one tile, as (u0, v0, u1, v1) on a 256-unit sheet.
  struct PlumeTexels
  {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
  };

  // One puff that survived its step. The near cutoff is **not** applied here:
  // it needs the projected q, and the original applies it in the draw too.
  struct PlumePuffDraw
  {
    // Back into the pool, so the publish pass can advance a flame's strip only
    // when the record really is drawn.
    std::size_t index = 0;
    orphen::ported::psm2::Vec3 world;
    float size = 0.0f;
    std::uint32_t colour = 0;
    int alpha = 0;
  };

  class PlumePool
  {
  public:
    static constexpr std::size_t kEmitterCount = 100;
    static constexpr std::size_t kPuffCount = 1000;

    // FUN_0021FA88. Both arrays free, both counts zero, the gate down.
    void FUN_0021fa88_reset();

    // FUN_0021F6E8 with `writeDrift` clear, FUN_0021F7A8 with it set. Both walk
    // for the first free slot and give up silently if there is none.
    void FUN_0021f6e8_open_emitter(const PlumeEmitterSpawn &spawn);

    // FUN_00220028: the emitter walk, then the puff walk, then the gate.
    void FUN_00220028_step(std::uint32_t frameTicks,
                           const std::function<std::uint32_t()> &random);

    const std::vector<PlumePuffDraw> &drawList() const { return draws_; }
    void clearFrameDraws() { draws_.clear(); }

    // FUN_00220210's tail, split out because it mutates: a flame's strip only
    // advances on a frame the record survived both rejects.
    PlumeTexels FUN_00220210_take_texels(std::size_t index);

    // What the publish pass needs off a record the draw list does not carry.
    std::int8_t clutBankOf(std::size_t index) const;

    bool DAT_00354cc4_gate() const { return DAT_00354cc4_gate_; }
    int iGpffffbbf8_emitterCount() const { return emitterCount_; }
    int iGpffffbbf4_puffCount() const { return puffCount_; }
    const std::array<PlumeEmitter, kEmitterCount> &emitters() const { return emitters_; }
    const std::array<PlumePuff, kPuffCount> &puffs() const { return puffs_; }

  private:
    void FUN_0021fb68_step_emitter(PlumeEmitter &emitter, std::uint32_t frameTicks,
                                   const std::function<std::uint32_t()> &random);
    void FUN_00220210_step_puff(PlumePuff &puff, std::size_t index, std::uint32_t frameTicks);
    void FUN_0021f870_shed_puff(float rise, float size, float x, float y, float z,
                                std::int16_t life, std::uint32_t colour, std::uint8_t tile,
                                std::int8_t clutBank,
                                const std::function<std::uint32_t()> &random);
    void FUN_0021f980_shed_flame(float size, float x, float y, float z, std::int16_t life,
                                 std::uint32_t colour,
                                 const std::function<std::uint32_t()> &random);

    std::array<PlumeEmitter, kEmitterCount> emitters_{};
    std::array<PlumePuff, kPuffCount> puffs_{};
    std::vector<PlumePuffDraw> draws_;
    int emitterCount_ = 0;           // iGpffffbbf8
    int puffCount_ = 0;              // iGpffffbbf4
    bool DAT_00354cc4_gate_ = false; // iGpffffad54
  };

  // What FUN_00220210 needs from the projection to place one record.
  struct PlumeQuadInputs
  {
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    float size = 0.0f;
    std::uint32_t colour = 0;
    int alpha = 0;
    std::int8_t clutBank = 0;
    PlumeTexels texels;
  };

  orphen::ported::render::SpriteQuad FUN_00220210_build_plume_quad(const PlumeQuadInputs &inputs);

  // DAT_003523EC / F0 / F4. Above the first the record is dropped; between the
  // second and the first its alpha is scaled by `(0.7 - q) / 0.1`.
  inline constexpr float kDAT_003523ec_plumeNearCutoff = 0.699999988079071f;
  inline constexpr float kDAT_003523f0_plumeFadeStart = 0.6000000238418579f;
  inline constexpr float kDAT_003523f4_plumeFadeSpan = 0.10000000149011612f;

} // namespace orphen::ported::entity
