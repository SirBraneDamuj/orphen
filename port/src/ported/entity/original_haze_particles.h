#pragma once

// The haze field opcode 0x109 arms -- the **ninth** particle pool, and the one
// s01_e013 fills its lower deck with:
//
//   src/FUN_0021be58.c  0x0021BE58  carve it: 0x960 bytes, 100 x 0x18
//   src/FUN_0021bd30.c  0x0021BD30  set the live count -- opcode 0x109's target
//   src/FUN_0021bef0.c  0x0021BEF0  place the field, then walk all 100
//   src/FUN_0021c288.c  0x0021C288  one record: step it, then build its quad
//   src/FUN_002625b8.c  0x002625B8  opcode 0x109 itself
//   src/FUN_00219368.c  0x00219368  the debug menu's own call, which is what
//                                   names the six arguments
//
// The globals are all gp-relative and Ghidra spells several of them twice.
// They are the same words: `DAT_00355B38` is `fGpffffbbc8`, `DAT_00355B3C` is
// `iGpffffbbcc`, `DAT_00355B40` is `fGpffffbbd0`, `DAT_00355B48` is
// `uGpffffbbd8`. Nothing configures this pool except FUN_0021BD30's six
// arguments -- there is no second setter anywhere in the executable.
//
// ---- what the six arguments mean -----------------------------------------
//
// FUN_0021BD30(size, speed, angle, count, magnitude, entity), and s01_e013
// passes 150, 0.05, 0.7854, 7, -5, -1. So:
//
//   size       the quad's half-extent, x16 into DAT_00355B38. At 150 that is
//              2400 GS units per corner unit, and the corners are +/-10, so a
//              sprite is 1500 x 750 **pixels** at unit depth. These are not
//              motes; they are a haze layer the size of the screen.
//   speed      how far a record drifts per frame, scaled by the frame's ticks
//   angle      the direction it drifts in **and** the yaw the whole field is
//              turned by -- one word does both jobs
//   count      how many records to make live. See the halving below.
//   magnitude  the spawn radius, and with no entity also how far in front of
//              the camera the field sits. **Its sign is the draw order**:
//              negative sets every record's +0x15, which sends the quad to
//              display list 0x1005 instead of 0x1000 -- later in the frame,
//              but still depth-tested. See the packet note below.
//   entity     a pool index, or negative for "none"
//
// ---- the count only ever gets halfway there ------------------------------
//
// FUN_0021BD30's growth loop tests `added < target - liveCount` and increments
// **both** sides each pass (0x0021BDD4-0x0021BDE4 reloads the count from gp and
// adds one before the compare). Asking for 7 from empty therefore makes 4 live,
// not 7. A scene that re-arms every frame converges on the target; s01_e013
// arms once, so it runs four. That is the original's arithmetic, verified in
// the disassembly, not a decompiler artifact.
//
// ---- what a record is ----------------------------------------------------
//
// A position in the field's own plane and a countdown, and nothing else. z is
// written zero at spawn and never touched again, so every record in a field
// sits at the same height; the field's matrix is what puts them in the world.
//
// When the countdown runs out the record **respawns in place** rather than
// dying -- a fresh angle, radius and life -- so a field never empties and never
// needs a second opcode. The respawn frame draws nothing.
//
// The alpha is a triangle over the record's life: `r = remaining / total`, then
// `r * 255` while r <= 0.5 and `255 - r * 255` above it. It peaks at 127 in the
// middle and is zero at both ends, so a record fades in as it appears and out
// as it expires. FUN_00207DE8 then halves it again, which is why the layer
// reads as haze rather than as seven grey rectangles.
//
// ---- where the field sits ------------------------------------------------
//
// With no entity it is **camera-locked**: the point (0, 0, magnitude) put
// through pitch, yaw and the camera's position -- the same three matrices
// FUN_0021EBE8 builds for a camera-relative fountain, down to the two quarter
// turns. It therefore never falls behind you and never comes closer.
//
// With an entity it is that entity's x/y at `FUN_00227798(x, y, +0x4C) - 0.7`,
// so the field lies just under the ground the entity is standing on.
//
// Either way the records are then turned by Rz(angle + pi) about that point.
//
// ---- the corner table is mutable, and that is deliberate -----------------
//
// DAT_00315638's four corner pairs start at (-10,-10), (-10,0), (10,0),
// (10,-10) -- a sprite standing **on** its origin, which is what an entity
// field wants. The no-entity branch of FUN_0021BEF0 rewrites the four y
// components to -5, +5, +5, -5 so the sprite is centred on its origin instead,
// and it writes them into the executable's own .data every frame. Nothing ever
// writes them back. A scene that arms a camera-locked field therefore changes
// the shape of every entity field for the rest of the run, which is why
// `DAT_00315638_corners_` below survives FUN_0021BE58_reset.
//
// ---- the quad ------------------------------------------------------------
//
// Screen-space, the way the dust and fountain quads are: the record's world
// position is projected to a GS integer origin and each corner is an offset of
// `corner * DAT_00355B38 * q` from it, clamped above at 24000 in x and 48000 in
// y. At s01_e013's size of 150 the x clamp lands exactly on its limit and has
// no effect; it is reproduced because a larger size would reach it.
//
// Texture slot 0x21 -- the same sheet the spray and fountain pools come off --
// with the packet's halfword a plain 0x0021, so CLUT bank 0. The UV rectangle
// is DAT_00315658's, (10.4, 10.4) to (118.4, 110.4) in texels, which is a far
// bigger patch of that sheet than either of the other two takes, and s01_e013's
// texture 0x19B holds a single soft cloud puff filling exactly that box. Bank 0
// and the plain page render identically for this sheet, checked both ways.
//
// Packet +0x0C is 0x10004080: bit 0x4000 is the first rung of FUN_00207DE8's
// `& 0x1C000` ladder, so blend mode 1, and bit 0x10000000 puts the vertices
// through as raw integers -- which is also why the GS gets the record's own
// projected depth. The 0x1005 display list therefore only reorders the quad;
// it does not put it in front of the world the way FUN_0020F510's matching
// bucket does, because that one writes a z of 0xFFFF as well.
//
// ---- the one thing here that is not reproduced ---------------------------
//
// FUN_0020B6A0 returns the VU0 clip flags for the projected point, and
// FUN_0021C288 drops the record when `flags & 0xE0` is set -- a screen-rect
// test with a guard margin in vf7/vf8/vf14, registers no function in `src/`
// writes. **No pool in this port models it**, and for the dust, spray and
// fountain it is worth a pixel. Here the sprite is most of the screen wide, so
// a record whose *centre* has slid off the edge is one the original stops
// drawing and this port keeps: expect the port to be hazier at the moments a
// record passes out of frame. Pinning the margin down needs the VU0 register
// state, so it is left out rather than guessed at.
//
// Hardware could not settle it from the save state to hand: forcing the pool on
// in s01_e014 draws nothing, because DAT_003429A8 slot 0x21 is empty there. The
// numbers below all come from the disassembly, and the sheet dump for s01_e013
// confirms the UV box lands squarely on the cloud.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_sprite_pass.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace orphen::ported::entity
{

  // One entry of the pool at DAT_00355B50. Offsets are the entry's own; +0x0C
  // is never read or written by anything.
  struct HazeParticle
  {
    // +0x00. Where it is **in the field's plane**, not in the world. The draw
    // walk copies these three into the scratchpad, FUN_0021C288 steps them
    // there, and they are copied back.
    float x00 = 0.0f;
    float y04 = 0.0f;
    // +0x08. Set to zero at every respawn and never otherwise touched.
    float z08 = 0.0f;

    // +0x10 / +0x12. Ticks left and ticks it was given. FUN_0021BE58 writes
    // 0xFFFF into +0x10, which is why a fresh pool respawns on its first step.
    std::uint16_t remaining10 = 0xFFFF;
    std::uint16_t total12 = 0;

    // +0x14. Negative is free. FUN_0021BE58 and the shrink path both write
    // 0xFF; the grow path writes 1.
    std::int8_t alive14 = -1;
    // +0x15. Set when the magnitude was negative, and read once, at the very
    // end of FUN_0021C288, to pick display list 0x1005 over 0x1000.
    std::int8_t front15 = 0;

    bool alive() const { return alive14 >= 0; }
  };

  // What FUN_0021BEF0's no-entity branch needs. The same five words the
  // fountain pool's FountainCameraFrame carries, and the same two quarter turns
  // get folded into them.
  struct HazeCameraFrame
  {
    float fGpffffb6d4_yaw = 0.0f;
    float fGpffffb6d8_pitch = 0.0f;
    float DAT_0058c0a8_eyeX = 0.0f;
    float DAT_0058c0ac_eyeY = 0.0f;
    float DAT_0058c0b0_eyeZ = 0.0f;
  };

  // What the entity branch needs, resolved by the caller because the pool has
  // no way to reach the entity pool or the terrain: the selected entity's +0x20
  // and +0x24, and FUN_00227798's height under (+0x20, +0x24, +0x4C).
  struct HazeEntityAnchor
  {
    float positionX20 = 0.0f;
    float positionY24 = 0.0f;
    float FUN_00227798_groundHeight = 0.0f;
  };

  // One record that survived its step, in world space. The distance fade is
  // **not** applied here: it needs the projected q, which only the publish pass
  // has, and the original applies it there too.
  struct HazeParticleDraw
  {
    orphen::ported::psm2::Vec3 world;
    // 0..255 from the life triangle, before the near fade.
    int alpha = 0;
    // The record's +0x15: display list 0x1005 and no depth test.
    bool front = false;
  };

  class HazeParticlePool
  {
  public:
    // 0x960 bytes of 0x18.
    static constexpr std::size_t kCount = 100;

    // FUN_0021BE58. Zeroes the block, marks every record free and drops the
    // gate. It does **not** touch the corner table -- see the note above.
    void FUN_0021be58_reset();

    // FUN_0021BD30. `entityIndex` is opcode 0x109's sixth expression as the
    // opcode leaves it: negative or at 0x100 and above means no entity.
    void FUN_0021bd30_arm(float size, float speed, float angle,
                          int count, std::int32_t magnitude, int entityIndex);

    // FUN_002620A8 case 5, opcode 0x100's inline byte 5: a bare store of zero
    // into uGpffffad48. The records keep their positions and their countdowns
    // and come straight back if the scene arms the field again.
    void FUN_002620a8_clear_gate() { DAT_00354cb8_gate_ = false; }

    // FUN_0021BEF0 plus FUN_0021C288's step half. `anchor` is the resolved
    // entity when DAT_00355b4c_entityIndex() names one, and nullopt otherwise;
    // passing nullopt is what takes the camera-locked branch.
    void FUN_0021bef0_step(std::uint32_t frameTicks,
                           const HazeCameraFrame &camera,
                           const std::optional<HazeEntityAnchor> &anchor,
                           const std::function<std::uint32_t()> &random);

    const std::vector<HazeParticleDraw> &drawList() const { return draws_; }
    // Emptied on a frame FUN_002192C0 never runs, so the pool emits nothing
    // while its records stand still. See PortRuntime's gate on DAT_00354D2C.
    void clearFrameDraws() { draws_.clear(); }
    const std::array<HazeParticle, kCount> &particles() const { return particles_; }
    int iGpffffbbc4_aliveCount() const { return live_; }
    bool DAT_00354cb8_gate() const { return DAT_00354cb8_gate_; }
    int DAT_00355b4c_entityIndex() const { return entityIndex_; }
    // DAT_00355B38, already multiplied by 16. The quad builder needs it.
    float DAT_00355b38_cornerScale() const { return cornerScale_; }
    // DAT_00315638, in the state the last camera-locked field left it.
    const std::array<float, 8> &DAT_00315638_corners() const { return DAT_00315638_corners_; }

  private:
    std::array<HazeParticle, kCount> particles_{};
    std::vector<HazeParticleDraw> draws_;
    int live_ = 0;                    // iGpffffbbc4
    bool DAT_00354cb8_gate_ = false;  // uGpffffad48
    float cornerScale_ = 0.0f;        // DAT_00355B38, = size * 16
    std::int32_t magnitude_ = 0;      // DAT_00355B3C, always the absolute value
    float angle_ = 0.0f;              // DAT_00355B40
    float speed_ = 0.0f;              // DAT_00355B44
    std::int16_t lifeSpread_ = 200;   // DAT_00355B48, and it is only ever 200
    int entityIndex_ = -1;            // DAT_00355B4C, as a pool index

    // DAT_00315638. Four (x, y) pairs in the executable's .data, which the
    // camera-locked branch rewrites in place and nothing restores. Deliberately
    // outlives FUN_0021be58_reset, because in the original it outlives the
    // scene.
    std::array<float, 8> DAT_00315638_corners_{
        -10.0f, -10.0f, -10.0f, 0.0f, 10.0f, 0.0f, 10.0f, -10.0f};
  };

  // What FUN_0021C288 needs from the projection to place one record.
  struct HazeQuadInputs
  {
    std::int32_t gsOriginX = 0;
    std::int32_t gsOriginY = 0;
    float viewZ = 1.0f;
    float projectionScaleX = 7680.0f;
    float projectionScaleY = 3456.0f;
    float screenCentreX = 32768.0f;
    float screenCentreY = 32768.0f;
    // DAT_00355B38 and DAT_00315638, straight off the pool.
    float cornerScale = 0.0f;
    std::array<float, 8> corners{};
    int alpha = 0;
    bool front = false;
  };

  orphen::ported::render::SpriteQuad FUN_0021c288_build_haze_quad(const HazeQuadInputs &inputs);

  // DAT_00352350 / 54 / 58. A record whose projected q is above the first is
  // dropped outright; between the second and the first its alpha is scaled by
  // `(0.6 - q) / 0.2`, so the layer fades out as it closes on the eye rather
  // than filling the screen. q is 1/viewZ, so 0.6 is 1.67 units away.
  inline constexpr float kDAT_00352350_hazeNearCutoff = 0.60000002384185791f;
  inline constexpr float kDAT_00352354_hazeFadeStart = 0.40000000596046448f;
  inline constexpr float kDAT_00352358_hazeFadeSpan = 0.20000000298023224f;

} // namespace orphen::ported::entity
