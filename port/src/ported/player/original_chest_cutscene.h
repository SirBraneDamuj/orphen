#pragma once

// The treasure chest cutscene: player states 0x0C through 0x15.
//
// FUN_00252828's chest branch does `FUN_00225bf0(player, 0xC, 1)` and every
// state after that comes out of the player state table at PTR_FUN_0031e0e8:
//
//   0x0C  0x00254d58  arm the fade to black, hand to 0x0D
//   0x0D  0x00254db0  on black: place the player at the chest, install the
//                     cutscene camera, arm the fade in, enter game mode 6
//   0x0E  0x00254f18  on visible: play animation 0x57, hand to 0x0F
//   0x0F  0x00254f60  watch the animation. Its second keyframe carries the
//                     0x100 event marker; that is where the chest's event flag
//                     is set. On the last keyframe, hand on
//   0x10  0x002550f0  \ the item display: reveal the item entity, then ramp
//   0x11  0x00255148  / the chest and player out and the item in
//   0x12  0x00255260  on the item window closing, arm the fade to white
//   0x13  0x002552b0  on white: free the item entity, put the player back
//                     beside the chest, leave game mode 6
//   0x14  0x00255448  once grounded, arm the fade in from white
//   0x15  0x00255498  on visible, back to idle
//
// Three of those addresses have no `src/FUN_*.c`: Ghidra never made functions
// at 0x002550F0, 0x00255148 or 0x00255260 because nothing calls them directly
// -- they are only ever reached through the state table. They were read from
// the disassembly.
//
// == What the port leaves out ==
//
// **The item display.** The original's state 0x0F builds a second entity in
// pool slot 2 (0x0058C260) carrying the item's model, hangs it off the chest's
// bone 1, and states 0x10/0x11 cross-fade the chest out and the item in while
// a text window names it. That window is FUN_00237b38 / FUN_00237c60 and the
// scene reconfiguration around it is FUN_002342c0 / FUN_00234400, none of
// which is ported.
//
// The original already has a path for a chest with no item: FUN_00254f60's
// `chest +0x130 < 0` branch leaves player +0x19C at zero, and state 0x0F then
// skips straight from the animation to 0x12. The port takes that branch
// unconditionally, so 0x10 and 0x11 are unreachable here. They are written
// out anyway, because the states either side hand to them by number and a
// reader deserves to see where the gap is.
//
// **FUN_002342c0 / FUN_00234400.** These bracket the cutscene. Three of the
// things they do are visible with or without the item window, and the port
// reproduces those three rather than the whole pair:
//
//   1. **Every entity from pool slot 2 up is hidden.** FUN_002342c0's tail
//      loop raises `+0x08` bit 0 and `+0x04` bit 0x4000 on slots 2..255, and
//      FUN_0020c5a8:69 skips a slot whose `+0x08` bit 0 is set. Slot 0, the
//      lead player, is not in the loop; state 0x0D then clears both bits on
//      the chest. That is exactly the "only the chest and the player" look.
//   2. **The world goes black.** FUN_002342c0 spins FUN_002340e0 until its
//      done byte flips, and the first pass through takes the darken branch and
//      leaves `DAT_00355700` at 3. That is the global fade cap the map draw
//      already reads (FUN_00209140:91 hands it to VU1): every primitive is
//      capped at 3 against a 0x80 = x1.0 scale, so the room renders at about
//      2% brightness. The port had the cap plumbed through already and simply
//      never had anything to set it.
//   3. **The camera is snapshotted and restored.** FUN_00233b28 saves the
//      whole camera block and FUN_00233eb8 puts it back, `DAT_00355700`
//      included -- it is the byte at snapshot +0x1DA65. The port uses
//      FUN_00217d70's own save/restore pair for the camera instead, which
//      reaches the same place with only ported code.
//
// What is left out of those two is the item scene proper: the second entity,
// its model, the text window and the lighting and effect state around them.

#include "ported/camera/original_field_camera.h"
#include "ported/entity/entity_pool.h"
#include "ported/render/original_screen_fade.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::player
{

  // The states this module owns. FUN_00251ed8 dispatches them through the
  // table; the port's controller asks this first and falls through to its own
  // field states when the answer is no.
  constexpr std::uint16_t kChestCutsceneFirstState = 0x0C;
  constexpr std::uint16_t kChestCutsceneLastState = 0x15;

  inline bool isChestCutsceneState(std::uint16_t state)
  {
    return state >= kChestCutsceneFirstState && state <= kChestCutsceneLastState;
  }

  // DAT_00354d2c / iGpffffadbc, the frame-loop mode. FUN_00254db0 sets 6 and
  // FUN_002241d8 puts it back to 0. Mode 6 is FUN_002245d8, which runs the
  // player, the actors and the draw -- and *not* the scene script or the field
  // camera, which is what keeps the cutscene camera where it was put.
  constexpr std::uint32_t kGameModeField = 0;
  constexpr std::uint32_t kGameModeCutscene = 6;

  // FUN_002340e0:32, the value the darken branch leaves in DAT_00355700.
  constexpr std::uint8_t kCutsceneFadeCap = 3;

  // FUN_00257b10. Cue 8 is bank 0 program 0 note 67 -- the item fanfare.
  constexpr std::uint16_t kItemFanfareCue = 8;

  // FUN_0025b9e8's two message indices. 0 is the item-get line, whose control
  // code 0x14 state 0x0F patches with the chest's id; 1 is "The chest is
  // empty." and state 0x0F opens it directly.
  constexpr std::size_t kItemGetMessage = 0;
  constexpr std::size_t kEmptyChestMessage = 1;

  struct ChestCutsceneContext
  {
    orphen::ported::entity::EntityPool *pool = nullptr;
    orphen::ported::entity::OriginalEntity *player = nullptr;
    orphen::ported::render::ScreenFade *fade = nullptr;
    orphen::ported::camera::OriginalFieldCamera *camera = nullptr;
    std::uint32_t *DAT_00354d2c_gameMode = nullptr;
    // DAT_00355700, the map draw's global fade cap. FUN_002342c0 leaves it at
    // kCutsceneFadeCap and FUN_00233eb8 restores it from the snapshot.
    std::uint8_t *DAT_00355700_globalFadeCap = nullptr;
    std::uint32_t frameTicks = 0;

    // FUN_002663a0, the event-flag set. This is the thing the whole cutscene
    // exists to do: FUN_002d1ea8 only ever observes the flag.
    std::function<void(std::uint32_t)> FUN_002663a0_setEventFlag;

    // FUN_00267d38(cue, entity): play a sound effect at an entity's position.
    // The cutscene uses it once, at 0x00255240 -- `FUN_00257b10(player)`, which
    // is cue 8, the item fanfare, keyed to the player rather than the chest.
    std::function<void(std::uint16_t cue, const orphen::ported::entity::OriginalEntity &at)>
        FUN_00267d38_playSound;

    // FUN_00227798, the terrain height under a world point. Used once, by
    // state 0x13, to drop the player back onto the floor.
    std::function<std::optional<float>(float x, float y, float z)> FUN_00227798_groundHeight;

    // FUN_00254f60's item branch. Builds the item entity in pool slot 2 from
    // the chest's id byte -- type `id + 0x1F1`, animation 4, positioned on the
    // chest's role-1 bone -- and returns false when the port cannot resolve it,
    // which drops the cutscene onto the no-item path instead of showing
    // nothing. See PortRuntime::buildChestItemEntity.
    std::function<bool(std::size_t chestSlot, std::int16_t itemId)> buildItemEntity;

    // FUN_00237b38(FUN_0025b9e8(index)) and FUN_00237c60: raise one of the two
    // chest messages, and ask whether it is still up. State 0x12 waits on the
    // second. `itemId` is what control code 0x14's operand gets; -1 for the
    // empty-chest stream, which has no 0x14.
    std::function<void(std::size_t messageIndex, std::int32_t itemId)> openItemWindow;
    std::function<bool()> itemWindowOpen;

    // FUN_002342c0:39-47's render-state block -- the two VU1 light colours,
    // the fog colour and the light direction -- and FUN_00233eb8's restore of
    // it. The values live with the renderer, so the caller owns them; see
    // PortRuntime::setItemSceneRenderState for the constants.
    std::function<void(bool enable)> setItemSceneRenderState;
  };

  // One frame of whichever cutscene state the player is in.
  void FUN_00251ed8_step_chest_cutscene(const ChestCutsceneContext &context);

} // namespace orphen::ported::player
