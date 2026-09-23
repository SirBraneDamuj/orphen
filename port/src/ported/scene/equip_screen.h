#pragma once

// The Equip screen: game mode 3, the field menu's items 5 and 6.
//
//   0x00233240 / 0x00233250  PTR_FUN_0031C3C0[5] and [6]. Neither has a src/
//                            file; both are `iGpffffadbc = 3; return 0`.
//   0x002244C8               the mode-3 handler, PTR_FUN_00318A88[3]:
//                            FUN_0022E910, FUN_002261E0, then FUN_00208450,
//                            FUN_00208EE8, FUN_00208F28, FUN_0020C5A8,
//                            FUN_0020F3E0, FUN_0020C290.
//   src/FUN_0022e910.c       the screen's own frame: Triangle, the state
//                            dispatch through PTR_LAB_0031C2F0[DAT_00354DA0],
//                            and the draw.
//
// == What the mode runs ==
//
// No script tick, no actor behaviours, no field camera -- but, unlike the field
// menu's modes 4 and 5, **FUN_002261E0**. That walk is where FUN_00225C90, the
// animation step, lives, so Orphen breathes and the selected slot's icon plays
// its animation while everything else stands still. Confirmed on hardware:
// thirty stepped frames on the slot view change pixels on Orphen and on the
// highlighted icon and nowhere else.
//
// == The states ==
//
//   0  0x0022F010  -> 1
//   1  FUN_0022F020  wait for the lead to be grounded, then build the screen
//   2  FUN_0022F2D8  the fade (FUN_002340E0, twice a frame) and the relight
//   3  0x0022F3E8  highlight slot 0's icon (+0xA0 = 1) -> 4
//   4  FUN_0022F408  Up/Down picks a slot; Cross opens the ring (-> 6), or
//                    says there is nothing to equip (-> 8)
//   5  FUN_0022F588  the ring closing: its icons shrink away and it fades
//                    back down -> 4
//   6  FUN_0022F620  the ring open: Left/Right turn it, Up/Down still move
//                    the slot cursor, Cross swaps the front spell in -> 7
//   7  FUN_0022FA18  the two icons trade places along curves; then the ring
//                    is rebuilt with the old spell at the front -> 6
//   8  FUN_0022FBD0  "no spells" box until Cross -> 4
//   9  0x0022FCA8    the Item screen's ring, FUN_00230910(3, -1) -> 10, or
//                    -> 11 when nothing is held
//  10  FUN_0022FD38  browse it; Cross picks and leaves (-> 13), and
//                    FUN_0022FF20 then uses the item from ring +0x1C8
//  11  FUN_0022FDE8  "no items" box; only Triangle leaves
//  12  FUN_0022FEA8  leave by reloading the scene (a section-14 scene)
//  13  FUN_0022FF20  leave by putting the field back
//
// Triangle is handled by FUN_0022E910 itself, ahead of the dispatch: in state 1
// it backs straight out, and from state 3 on it goes to 13 -- or, in a battle
// scene, arms a fade and goes to 12. State 13 then runs the same frame.
//
// == The ring ==
//
// The ring model (type 0x4C, slot 6) has forty bones round its rim.
// FUN_00230450 turns them into an eighty-point loop -- each bone, then the
// midpoint to the next -- every time the ring opens. FUN_00230910 spawns one
// icon per spell the lead can hold in that slot (inventory count non-zero and
// the item record's flags carrying both the character's bit and 0x100), all
// at point 0, and sends icon i along the loop to point `i * 80 / count` on a
// chord-length spline (FUN_00230608). Turning the ring re-aims every icon one
// position round the same way. The ring's own sub-state, +0x1C4, runs through
// PTR_LAB_0031C370: 0 browse, 1 slide, 2 swap in flight, 3 shrink, 4 nothing.
//
// Swapping (FUN_0022F620) is the whole "equip": the new spell's inventory
// count goes down one, the old one's goes up one, and the loadout byte at
// DAT_003437A0[roster * 3 + slot] is overwritten. Nothing else is written.
//
// == The field is copied, not hidden ==
//
// FUN_00233B28 takes a 0x1DAC8-byte snapshot: the whole 0x1D800-byte pool, the
// slot status bytes, the sixteen lights, the camera, the scene colours, the fog
// and the effect gates. Then state 1 releases every live slot from 10 up.
// FUN_0022FF20 copies slots 2..255 and their status bytes back wholesale and
// FUN_00233EB8 puts the globals back. The order around the copy matters: slots
// 2, 3 and 6 are released and the ring is built in slot 6 *before* the copy is
// taken, so the ring comes back with the rest and FUN_00233EB8 releases it.
//
// The room is blanked the way the game over blanks it: DAT_00355700 goes to 3
// and FUN_00209140 stops walking the map, with FUN_00255CE8's black quad
// behind. FUN_00233EB8 restores DAT_00355700 from snapshot +0x1DA65 -- a byte
// FUN_00233B28 never writes. Hardware reads 0 there after the screen closes.

#include "ported/camera/original_camera_path.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/text/original_dialogue_text.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orphen::ported::scene
{

  // iGpffffadbc (DAT_00354D2C) for the whole screen.
  inline constexpr int kGameModeEquipScreen = 3;
  // uGpffffae34, the field menu row that opened it. FUN_0022F020:81 and
  // FUN_0022E910:36 test for 6; 5 is the Item screen through the same states.
  inline constexpr int kFieldMenuItemItem = 5;
  inline constexpr int kFieldMenuEquipItem = 6;

  // uGpffffae30 (DAT_00354DA0), the index into PTR_LAB_0031C2F0.
  inline constexpr int kEquipStateArm = 0;
  inline constexpr int kEquipStateEnter = 1;
  inline constexpr int kEquipStateFadeIn = 2;
  inline constexpr int kEquipStateFirstSlot = 3;
  inline constexpr int kEquipStatePickSlot = 4;
  inline constexpr int kEquipStateRingClose = 5;
  inline constexpr int kEquipStateRingOpen = 6;
  inline constexpr int kEquipStateSwap = 7;
  inline constexpr int kEquipStateNoSpells = 8;
  inline constexpr int kEquipStateItemList = 9;
  inline constexpr int kEquipStateItemBrowse = 10;
  inline constexpr int kEquipStateNoItems = 11;
  // 0x0022FCA8: the Item screen builds its ring as slot 3, which asks the
  // item record for bit 0x80 instead of 0x100.
  inline constexpr int kItemScreenSlot = 3;
  // FUN_0022FDE8's caption, message 0x2B.
  inline constexpr int kItemNoItemsCaptionMessage = 0x2B;
  inline constexpr int kEquipStateLeaveByReload = 0xC;
  inline constexpr int kEquipStateLeave = 0xD;

  // The ring's +0x1C4, the index into PTR_LAB_0031C370.
  inline constexpr int kRingStepBrowse = 0; // 0x002313F0
  inline constexpr int kRingStepSlide = 1;  // FUN_00231660
  inline constexpr int kRingStepSwap = 2;   // 0x002317B0
  inline constexpr int kRingStepShrink = 3; // FUN_002317E0
  inline constexpr int kRingStepIdle = 4;   // 0x002318B8, `return 0`

  // The ring's +0x60, FUN_002333E8's fade: 1 fade out and release, 2 fade
  // out, 10 fade in. +0x62 is the level, 0xFE0 full.
  inline constexpr std::uint16_t kRingFadeOutRelease = 1;
  inline constexpr std::uint16_t kRingFadeOut = 2;
  inline constexpr std::uint16_t kRingFadeIn = 10;
  inline constexpr std::uint16_t kRingFadeFull = 0xFE0;
  // FUN_0022F408:44, the level the ring starts fading in from.
  inline constexpr std::uint16_t kRingFadeOpenFrom = 0x80;

  // FUN_00230450: forty rim bones, eighty points on the loop.
  inline constexpr int kEquipRingBones = 0x28;
  inline constexpr int kEquipRingPathPoints = 0x50;
  // FUN_00230910:43, the most icons the ring holds.
  inline constexpr int kEquipRingMaxIcons = 0x28;
  // FUN_00230910:34-39: the item record flag every slot needs, and the one
  // slot 3 (the Item screen's) needs instead.
  inline constexpr std::uint16_t kItemFlagSpellSlot = 0x100;
  inline constexpr std::uint16_t kItemFlagSlot3 = 0x80;
  // FUN_00231660: the slide takes 480 ticks; FUN_0022FA18: the swap 1920.
  inline constexpr float kRingSlideTicks = 480.0f;
  inline constexpr float kEquipSwapTicks = 1920.0f;
  // DAT_00352560, the shrink per tick in FUN_002317E0.
  inline constexpr float kDAT_00352560_shrinkPerTick = 0.0015625000232830644f;

  // The cues: FUN_002256B0, FUN_00237AB8, FUN_00237AF8 and FUN_00237AE8.
  inline constexpr int kEquipCuePick = 2;
  inline constexpr int kEquipCueTurn = 6;
  inline constexpr int kEquipCueRingOpen = 10;
  inline constexpr int kEquipCueSwap = 11;
  // FUN_0022F408:39, FUN_0025B9E8(0x3D): "no spells", and FUN_0022FBD0's
  // caption, message 0x27.
  inline constexpr int kEquipNoSpellsMessage = 0x3D;
  inline constexpr int kEquipNoSpellsCaptionMessage = 0x27;

  // FUN_002338F0: the ring, pool slot 6 (0x0058C9C0), type 0x4C, at scale
  // uGpffff8604 = 2.4.
  inline constexpr std::size_t kEquipRingSlot = 6;
  inline constexpr std::int16_t kEquipRingType = 0x4C;
  inline constexpr float kEquipRingScale = 2.4f;
  // FUN_0022F020:22-26, the pool slots released on the way in besides the ring.
  inline constexpr std::size_t kEquipReleasedSlotA = 2;
  inline constexpr std::size_t kEquipReleasedSlotB = 3;
  // FUN_002302F0: a slot's icon is type `item + 0x1F1`.
  inline constexpr std::int16_t kEquipIconTypeBase = 0x1F1;
  inline constexpr int kEquipSlotCount = 3;
  // FUN_0022F020:14, `FUN_0025B9E8(FUN_002298D0(lead) + 0x86)`: the character's
  // name, SCR.BIN resource 1.
  inline constexpr int kEquipCharacterNameMessage = 0x86;

  // FUN_0022E910:10 and :19. Triangle, and the cue FUN_002256A0 plays.
  inline constexpr std::uint16_t kEquipPadTriangle = 0x0010;
  inline constexpr std::uint16_t kEquipPadCross = 0x0040;
  inline constexpr int kEquipCueCancel = 3;
  // FUN_0022F408:13, FUN_002256C0.
  inline constexpr int kEquipCueMove = 1;
  // FUN_0022E910:21, `FUN_0025D1C0(1, 8, 0)` in a battle scene.
  inline constexpr int kEquipReloadFadeTicks = 8;
  // FUN_0022FEA8:12-16.
  inline constexpr std::uint32_t kEquipReloadFlagBytesFirst = 0x342BD4 - 0x342B70;
  inline constexpr std::size_t kEquipReloadFlagByteCount = 0x1C;
  inline constexpr std::uint32_t kEquipReloadFlag = 800;
  inline constexpr std::uint32_t kEquipReloadRequest = 0x20004;

  // FUN_0022F2D8:28-35: the light the screen is lit by once the fade is done.
  inline constexpr std::uint32_t kEquipAmbientRgb = 0x202020;
  inline constexpr std::uint32_t kEquipLightRgb = 0xFFFFFF;

  // FUN_002340E0's three snapshot fields, +0x1DA8C, +0x1DA8E and +0x1DA90.
  // FUN_00233B28 seeds them 0, 0xFE0 and 0.
  struct EquipFade
  {
    std::uint8_t done1da8c = 0;
    std::int16_t fadeOut1da8e = 0xFE0;
    std::int16_t fadeIn1da90 = 0;
  };

  // What one FUN_002340E0 call wrote.
  struct EquipFadeStep
  {
    // DAT_00355700, when it was written.
    bool fadeCapWritten = false;
    std::uint8_t fadeCap = 0;
    // DAT_00355661, the smear's alpha, when it was written.
    bool smearWritten = false;
    std::uint8_t smear = 0;
    // FUN_0022EF30's level, or -1 when it was not called.
    int colourMixLevel = -1;
    // FUN_00255CE8's alpha. Always drawn.
    std::uint8_t underlayAlpha = 0;
  };

  // FUN_002340E0. `titleSceneSpecial` is `DAT_003555D3 == 0 && DAT_003551F4 ==
  // 0xC` -- in section 12 outside battle the fade is skipped outright and the
  // done flag is only raised for entry 0x2A.
  EquipFadeStep FUN_002340e0_step(EquipFade &fade,
                                  bool titleSceneSpecial,
                                  bool titleSceneEntry2a,
                                  std::uint32_t frameTicks);

  // FUN_0022EF30, one channel byte: the saved colour faded toward `target` by
  // `level` / 255. Ambient goes to 0x20 and the light to 0xFF.
  std::uint32_t FUN_0022ef30_mix(std::uint32_t savedRgb, int level, int target);

  // FUN_00230128: where slot `slot`'s icon rests -- a row fanned out behind
  // and to the right of the lead, one radius and one height per slot.
  orphen::ported::psm2::Vec3 FUN_00230128_slot_position(int slot,
                                                        const orphen::ported::psm2::Vec3 &lead,
                                                        float leadFacing);

  // FUN_00230F08 then FUN_002310F8 and FUN_002311E8:29's `+ 0x4B`: the entry-x
  // a slot's name is drawn at. A private projection -- not the view the frame
  // draws with -- built from the camera's yaw and pitch, slot 1's position
  // and the boot constant at slot 1's +0x58.
  int FUN_002311e8_row_x(const orphen::ported::psm2::Vec3 &point,
                         const orphen::ported::psm2::Vec3 &eye,
                         float cameraYaw,
                         float cameraPitch);

  // FUN_00230CE0: split a description on control code 7 into the four lines
  // DAT_00570DF4 holds, 0x100 bytes apart.
  std::array<std::string, 4> FUN_00230ce0_split(const std::string &description);

  // DAT_005711F8: the loop FUN_00230450 builds from the ring's bones.
  using EquipRingPath = std::array<orphen::ported::psm2::Vec3, kEquipRingPathPoints>;
  // FUN_00230450. `bonePoint` is FUN_0020DC88 on the ring with a zero offset.
  EquipRingPath FUN_00230450_ring_path(
      const std::function<orphen::ported::psm2::Vec3(int bone)> &bonePoint);

  // FUN_00230608: the curve from loop point `from` to point `to`, stepping
  // `step` (+1 or -1) round the loop. Every point on the way is a knot, except
  // that more than thirteen steps are thinned to thirteen; the curve is at most
  // sixteen points. `from == to` is the empty curve.
  orphen::ported::camera::Curve3 FUN_00230608_path_curve(const EquipRingPath &path,
                                                          int from,
                                                          int to,
                                                          int step);

  // FUN_00230E50:6-8: the spell name, its description and the pentagon are
  // drawn only with a name set and outside states 3 and 8..11.
  bool FUN_00230e50_spell_shown(int state, const std::string &spellName);
  // FUN_00230E50:11, `FUN_0022EC30(0x220, 0xD0, 0x570DB0, 0)`.
  inline constexpr int kEquipPentagonX = 0x220;
  inline constexpr int kEquipPentagonY = 0xD0;

  // Everything FUN_0022E910's draw reads, for one frame.
  struct EquipScreenDraw
  {
    int state = 0;
    // FUN_0022F408 ran: its closing FUN_00230DB0 is submitted ahead of the
    // dispatcher's draw.
    bool descriptionTwice = false;
    // uGpffffae34 == 6.
    bool equip = true;
    std::string characterName;  // DAT_00570DE8
    std::string spellName;      // DAT_00570DEC; empty is the null pointer
    std::array<std::string, 4> descriptionLines{}; // DAT_00570DF4
    std::array<std::string, kEquipSlotCount> slotNames{}; // DAT_00570DD8
    std::array<bool, kEquipSlotCount> slotIcon{};         // DAT_00570DA0 non-null
    std::array<int, kEquipSlotCount> rowX{};              // FUN_002311E8's asStack_a0
    std::string caption;        // message 0x29
    // FUN_0022FBD0 (state 8) or FUN_0022FDE8 (state 11) ran: DAT_00570DF0 in
    // a box, and message 0x27 or 0x2B.
    bool noSpellsBox = false;
    // FUN_0022FD38 drew (state 10, a front icon): "%s*%d" of its name and
    // count, the description, and bar 1 -- ahead of the dispatcher's draw.
    bool itemBrowseLine = false;
    std::string itemLine;
    std::string noSpellsMessage;
    std::string noSpellsCaption;
  };

  // FUN_0022E910:34-51: FUN_002311A8, FUN_00230E50, FUN_002311E8 and the
  // caption. Only called for `state > 1`. The pentagon FUN_00230E50 draws with
  // FUN_0022EC30 is a HudQuad list, not sprites; the runtime builds it with
  // battle::FUN_0022ec30_pentagon -- the same function the battle target
  // readout calls, as FUN_00233818 does in the original.
  std::vector<orphen::ported::text::DialogueSprite> FUN_0022e910_layout(
      const EquipScreenDraw &draw, const orphen::ported::text::DialogueFont &font);

} // namespace orphen::ported::scene
