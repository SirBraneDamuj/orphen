#pragma once

// The target readout: the enemy's name, its hit points and the elemental
// pentagon, drawn down the right-hand side while the player holds a direction
// to look at something.
//
//   src/FUN_002334e8.c  fills the readout from the entity under the cursor
//   src/FUN_00233818.c  draws it -- two captions and the pentagon
//   src/FUN_0022ec30.c  the pentagon: five arms of three pips each
//   src/FUN_0022eb00.c  one pip, a rotated quad on the FUN_00207de8 UI path
//
// FUN_0023C340 owns all four: it calls FUN_002334E8 once, on the frame the
// display opens (DAT_00354E96 == 0xF00), and FUN_00233818 every frame the timer
// is still running. The same timer freezes the field, so the readout is up for
// exactly as long as the battle is paused.
//
// == The buffer ==
//
// The original keeps one 0xE8-byte block at 0x005715B8 and clears the whole
// thing at the top of FUN_002334E8. That block is not just the stat record:
// 0x005715E0 (the name) and 0x00571660 (the hit-point caption) are the same
// allocation at +0x28 and +0xA8, which is why FUN_00233818's guard is "the name
// is not empty" -- a lookup that found nothing leaves it cleared.
//
// == Where the numbers come from ==
//
// The record is one 0x28-byte FUN_00229688 row, and +0x18..+0x27 is the
// sixteen-entry elemental effectiveness table the pentagon reads. Which table
// it comes from depends on the entity's type id, and FUN_002334E8 splits four
// ways -- see FUN_002334e8_build.
//
// == The pentagon ==
//
// Five arms at 72 degrees, in the order DAT_0031C240 gives: element 1
// lightning, 4 fire, 2 wind, 5 dark, 10 ice, starting 38 degrees up-left and
// running counter-clockwise. Each arm is three stacked bars of increasing width
// -- 30, 56 and 78 units, sixteen tall, at 6, 22 and 38 units out from the
// centre -- and how many are lit is the effectiveness banded at 0x22 and 0x4B.
// A dark bar is the same quad at alpha 0x40 rather than 0xFF.
//
// The colour is not in the vertex: every pip samples the same 87x16 texel patch
// of slot 0x2A and the arm's entry in DAT_0031C250 picks the CLUT bank -- 14
// for lightning (violet), 13 fire (red), 12 wind (green), 11 dark (deep teal),
// 15 ice (cyan). See original_hud_quads.h for how one sheet holds sixteen
// palettes.

#include "ported/entity/entity_pool.h"
#include "ported/render/original_hud_quads.h"
#include "ported/resource/character_stats.h"
#include "ported/text/original_dialogue_text.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orphen::ported::battle
{

  // DAT_0031C240 and DAT_0031C250: which element each pentagon arm reads, and
  // the CLUT bank that colours it.
  inline constexpr std::array<int, 5> kDAT_0031c240_elements{1, 4, 2, 5, 10};
  inline constexpr std::array<int, 5> kDAT_0031c250_clutBanks{14, 13, 12, 11, 15};
  // DAT_0031C2A0, radians. 72 degrees apart, first arm up and to the left.
  inline constexpr std::array<float, 5> kDAT_0031c2a0_armAngles{
      -0.663224936f, -1.95476842f, -3.14159203f, 1.95476842f, 0.663224936f};

  // DAT_0031C260: the three pip rectangles, as {x, y} corner pairs in the order
  // the packet's triangle fan wants them.
  inline constexpr std::array<std::array<std::array<int, 2>, 4>, 3> kDAT_0031c260_pipCorners{{
      {{{0, 0}, {0, 16}, {30, 16}, {30, 0}}},
      {{{0, 0}, {0, 16}, {56, 16}, {56, 0}}},
      {{{0, 0}, {0, 16}, {78, 16}, {78, 0}}},
  }};
  // DAT_0031C290: each pip rotates about a point below itself, so the three
  // stack outward from a shared centre.
  inline constexpr std::array<std::array<int, 2>, 3> kDAT_0031c290_pipPivots{{
      {15, 22}, {28, 38}, {39, 54}}};

  // FUN_0022EB00's fixed UV rectangle in slot 0x2A, and the sheet it names.
  // The packet stores these normalised and scales by 4096 to reach GS 1/16
  // texel units, which is 256 texels to the unit.
  inline constexpr int kPipTextureSlot = 0x2A;
  inline constexpr std::array<std::array<float, 2>, 4> kPipTexels{{
      {168.0f, 112.0f}, {168.0f, 128.0f}, {255.0f, 128.0f}, {255.0f, 112.0f}}};
  // FUN_0022EB00:41-45. A negative bank means "not lit": same quad, alpha 0x40.
  inline constexpr std::uint32_t kPipColorLit = 0xFF'FFFFFFu;
  inline constexpr std::uint32_t kPipColorDim = 0x40'FFFFFFu;

  // FUN_0022EC30's banding of the effectiveness byte when its param_4 is 1 --
  // the enemy readout. Zero lights nothing at all.
  inline constexpr int kEffectivenessTwoPips = 0x22;
  inline constexpr int kEffectivenessThreePips = 0x4B;

  // FUN_00233818's placement. The two captions are right-aligned to entry x
  // 0x130 and the pentagon's centre is a raw screen position.
  inline constexpr int kCaptionRightEdge = 0x130;
  inline constexpr int kNameCaptionY = 200;
  inline constexpr int kHitPointsCaptionY = 0xB2;
  inline constexpr int kCaptionCellWidth = 0x14;
  inline constexpr int kCaptionCellHeight = 0x16;
  inline constexpr std::uint32_t kCaptionColor = 0x80808080u;
  inline constexpr int kPentagonCentreX = 0x220;
  inline constexpr int kPentagonCentreY = 0x94;

  // The 0xE8 block at 0x005715B8, with its three tenants named.
  struct TargetDisplayRecord
  {
    // +0x18..+0x27 of the FUN_00229688 row: effectiveness per element index.
    std::array<std::uint8_t, 0x10> DAT_005715d0_effectiveness{};
    std::string DAT_005715e0_name;   // +0x28
    std::string DAT_00571660_hitPoints; // +0xA8

    void FUN_00267e78_clear()
    {
      DAT_005715d0_effectiveness = {};
      DAT_005715e0_name.clear();
      DAT_00571660_hitPoints.clear();
    }
  };

  // What FUN_002334E8 and FUN_00233818 need that is not in the record.
  struct TargetDisplayEnvironment
  {
    const orphen::ported::entity::EntityPool *pool = nullptr;
    // uGpffffadf8 -- SCR.BIN 0xBF. Group 2 is the enemy table FUN_002334E8
    // scans, group 0 the one it indexes for a type at or above 0x7C.
    const orphen::ported::resource::CharacterStats *stats = nullptr;
    // uGpffffadf4 -- SCR.BIN 0xBD, the object table. FUN_002334E8 reaches it
    // for the three type ranges above 0x272; nothing the battle cursor can land
    // on is in them, but the branch is the original's.
    const orphen::ported::resource::CharacterStats *objectStats = nullptr;
    // DAT_00355208, which FUN_0022A418:50 copies from DAT_003551F4 -- the MCB
    // section the scene came from. It is the group index for the first of those
    // three ranges.
    int DAT_00355208_objectGroup = 0;
    // The proportional widths FUN_00238E68 measures with. Without it the
    // captions cannot be placed and only the pentagon is drawn.
    const orphen::ported::text::DialogueFont *font = nullptr;
  };

  // FUN_002334E8 (0x002334e8). `slot` is the pool index of the entity the
  // cursor is on; the original derives it from the pointer.
  void FUN_002334e8_build(TargetDisplayRecord &record,
                          const TargetDisplayEnvironment &environment,
                          std::size_t slot);

  // FUN_00233818 (0x00233818). Appends the two captions and up to fifteen pips.
  // Draws nothing when the record's name is empty, which is how a lookup that
  // found no row reports itself.
  void FUN_00233818_draw(const TargetDisplayRecord &record,
                         const TargetDisplayEnvironment &environment,
                         std::vector<orphen::ported::text::DialogueSprite> &sprites,
                         std::vector<orphen::ported::render::HudQuad> &quads);

  // FUN_0022EC30 (0x0022ec30). `enemyBanding` is its param_4: 1 bands the
  // effectiveness at 0x22/0x4B, 0 divides it by ten and caps at three.
  void FUN_0022ec30_pentagon(int centreX,
                             int centreY,
                             const std::array<std::uint8_t, 0x10> &effectiveness,
                             bool enemyBanding,
                             std::vector<orphen::ported::render::HudQuad> &quads);

} // namespace orphen::ported::battle
