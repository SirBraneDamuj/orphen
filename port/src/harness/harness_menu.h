#pragma once

// The fly view's menu bar, its dropdowns, and the panels its items open.
// PC-only; nothing in the original corresponds to any of it. The menus only
// *ask* for things: a command that touches the simulation goes out as a
// HarnessRequest, which PortRuntime applies between steps.

#include "harness/debug_text.h"
#include "harness/harness_panel.h"
#include "harness/tool_panel.h"
#include "ported/debug/original_player_param_menu.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace orphen::harness
{

  enum class MenuCommand
  {
    ToggleEntityTree,   // F5
    ToggleInset,        // F4
    ToggleWholeMap,     // F3
    SnapToGame,         // F2
    TogglePlayerParams,
    ToggleInventory,
    ToggleItems,
    ToggleSettings,
    ToggleVoiceCheck,
    Heal,               // 0
    HitScratch,         // 1
    HitHit,             // 2
    HitKnockback,       // 3
    HitFlatten,         // 4
    HitLethal,          // 5
  };

  struct MenuItem
  {
    std::string label;
    std::string shortcut;
    MenuCommand command = MenuCommand::ToggleEntityTree;
    // A checkable item draws a box, filled while `checked`.
    bool checkable = false;
    bool checked = false;
  };

  struct Menu
  {
    std::string title;
    std::vector<MenuItem> items;
  };
  using MenuBar = std::vector<Menu>;

  struct MenuBarLayout
  {
    float scale = 1.0f;
    float screenWidth = 0.0f; // units
    std::vector<panel::Rect> titles;
    int openMenu = -1;
    panel::Rect dropdown; // meaningful while openMenu >= 0

    // The title under the pixel, or -1.
    int titleAt(int pixelX, int pixelY) const;
    // The open menu's item under the pixel, or -1.
    int itemAt(int pixelX, int pixelY) const;
    // The bar itself, or the open dropdown.
    bool covers(int pixelX, int pixelY) const;
  };

  MenuBarLayout layoutMenuBar(int framebufferWidth, int framebufferHeight, const MenuBar &bar, int openMenu);

  void drawMenuBar(const DebugTextRenderer &text,
                   const DebugFont &font,
                   int framebufferWidth,
                   int framebufferHeight,
                   const MenuBarLayout &layout,
                   const MenuBar &bar,
                   int hoveredTitle,
                   int hoveredItem);

  // ------------------------------------------------------------- the panels

  // The panels the menus open, stacked down the middle of the view in this
  // order under the menu bar.
  enum class ToolPanelKind
  {
    PlayerParams,
    Inventory,
    Items,
    Settings,
    VoiceCheck,
  };

  // The Player Params panel: FUN_0026bc50's three rows, with a button for each
  // of the four pad bits that step them.
  struct PlayerParamsView
  {
    // False while there is no lead to edit -- the title screen, say.
    bool available = false;
    int hitPoints = 0;    // +0x12A
    int maxHitPoints = 0; // +0x128
    int strength = 0;     // +0x12C
    int defence = 0;      // +0x12E
  };

  // The Inventory panel: the lead's loadout row of DAT_003437A0, and the
  // DAT_003437B8 count of every item that can go in a spell slot.
  struct InventoryItemView
  {
    int item = 0;
    std::string name;
    int count = 0;
    // The loadout slot it is equipped in on the lead's row, or -1.
    int equippedSlot = -1;
  };
  // One tab per party character that has anything it could put in a spell
  // slot: a DAT_003437A0 row and the name SCR.BIN 0xBF group 1 gives its type.
  struct InventoryCharacterTab
  {
    int loadoutRow = 0;
    std::string name;
    bool lead = false;
  };
  struct InventoryView
  {
    // False with no character to show -- no lead and no tab picked, or a lead
    // with no loadout row (FUN_002298D0's 7).
    bool available = false;
    // The row the panels are showing: the picked tab's, or the lead's.
    int loadoutRow = 0;
    std::string characterName;
    std::vector<InventoryCharacterTab> characters;
    std::array<int, 3> loadout{};
    std::array<std::string, 3> loadoutNames;
    // Whether each slot has anything it could be swapped for.
    std::array<bool, 3> canSwap{};
    std::vector<InventoryItemView> items;
    // The Items panel: every item the Item screen could list for this lead
    // (FUN_00230910's slot-3 filter, 0x80), held or not. Never equipped.
    std::vector<InventoryItemView> fieldItems;
  };

  ToolPanel buildPlayerParamsPanel(float scale, float left, float top, const PlayerParamsView &view);
  ToolPanel buildInventoryPanel(float scale, float left, float top, const InventoryView &view);
  ToolPanel buildItemsPanel(float scale, float left, float top, const InventoryView &view);

  // The Settings panel: the port's own preferences, nothing of the game's.
  struct SettingsView
  {
    // The audio device's output level, 0..100.
    int volumePercent = 50;
  };
  ToolPanel buildSettingsPanel(float scale, float left, float top, const SettingsView &view);
  // A Settings button's change to the volume, in percent. The settings never
  // touch the simulation, so they are applied in the viewer rather than going
  // out as a HarnessRequest.
  int settingsVolumeDelta(int buttonId);

  // The Voice Check panel: the debug menu's VOICE CHECK page (FUN_002696a0),
  // which steps a VOICE.BIN id and plays it, one clip of a pack at a time.
  // The id is the viewer's to step; the runtime fills in what the table says
  // about it.
  struct VoiceCheckClipView
  {
    int clip = 0;
    float seconds = 0.0f;
  };
  struct VoiceCheckView
  {
    // False with no voice table at all.
    bool available = false;
    // False without VOICE.BIN's audio or without an audio device; the table
    // can still be browsed. `reason` says which.
    bool canPlay = false;
    std::string reason;
    // FUN_00221c40(0): ids run 1..entryCount.
    int entryCount = 0;
    int voiceId = 0;
    // FUN_00206d98's directory test passed: a pack of clips, not one line.
    bool pack = false;
    std::vector<VoiceCheckClipView> clips;
    bool playing = false;
    // The clip of this id the panel last started, while it is still playing.
    int playingClip = -1;
  };
  // `typed` is the number being typed into the id box, while it has focus.
  ToolPanel buildVoiceCheckPanel(float scale, float left, float top, const VoiceCheckView &view,
                                 const std::optional<std::string> &typed);
  // The Voice Check panel's buttons. Stepping and typing are the viewer's;
  // playing and stopping go out as requests.
  inline constexpr int kVoiceCheckFieldButton = 10;
  inline constexpr int kVoiceCheckStopButton = 11;
  inline constexpr int kVoiceCheckPlayButtonBase = 100; // + clip
  // A step button's change to the id, or 0 for any other button.
  int voiceCheckStep(int buttonId);

  // What a menu asks the runtime to do to the simulation. Applied between
  // steps, outside PortRuntime::update, so a --frames run never sees one.
  struct HarnessRequest
  {
    enum class Kind
    {
      StepPlayerParam, // param, step
      Heal,
      Hit,             // hitKind 1..5, the same kinds as the 1..5 keys
      AdjustItemCount, // item, delta
      CycleLoadout,    // slot, delta (+1 next held spell, -1 previous)
      SelectCharacter, // loadoutRow: the Spells/Items panels' tab
      PlayVoice,       // voiceId, clip: audio only, see applyHarnessRequests
      StopVoice,
    };
    Kind kind = Kind::Heal;
    orphen::ported::debug::PlayerParam param = orphen::ported::debug::PlayerParam::HitPoints;
    orphen::ported::debug::PlayerParamStep step = orphen::ported::debug::PlayerParamStep::Up1;
    int hitKind = 0;
    int item = 0;
    int slot = 0;
    int delta = 0;
    int loadoutRow = 0;
    int voiceId = 0;
    int clip = 0;
  };

  // A panel button's id back into the request it stands for.
  std::optional<HarnessRequest> toolPanelRequest(ToolPanelKind kind, int buttonId);

} // namespace orphen::harness
