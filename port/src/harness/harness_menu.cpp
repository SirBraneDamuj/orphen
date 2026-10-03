#include "harness/harness_menu.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <string>

namespace orphen::harness
{

  namespace
  {
    using namespace panel;
    using orphen::ported::debug::PlayerParam;
    using orphen::ported::debug::PlayerParamStep;
    using Glyphs = std::vector<orphen::ported::debug::DebugGlyph>;

    // The bar's text sits this far down the strip.
    constexpr int kBarTextTop = 2;
    // A dropdown row's leading columns: the check box and the gap after it.
    constexpr int kCheckColumns = 2;
    // The gap between an item's label and its shortcut.
    constexpr int kShortcutGap = 3;

    // The Player Params panel, in character columns from the text's left edge.
    // "HP   -10 -1  999  +1 +10"
    constexpr int kParamColumns = 24;
    constexpr int kValueColumn = 13; // three digits, right-aligned
    struct ButtonColumn
    {
      PlayerParamStep step;
      int column;
      const char *text;
    };
    constexpr std::array<ButtonColumn, 4> kButtonColumns = {{
        {PlayerParamStep::Down10, 5, "-10"},
        {PlayerParamStep::Down1, 9, "-1"},
        {PlayerParamStep::Up1, 17, "+1"},
        {PlayerParamStep::Up10, 20, "+10"},
    }};
    struct ParamRow
    {
      PlayerParam param;
      const char *label;
    };
    constexpr std::array<ParamRow, 3> kParamRows = {{
        {PlayerParam::HitPoints, "HP"},
        {PlayerParam::Strength, "STR"},
        {PlayerParam::Defence, "DEF"},
    }};

    float unitX(int pixelX, float scale) { return static_cast<float>(pixelX) / scale; }
    float unitY(int pixelY, float scale) { return static_cast<float>(pixelY) / scale; }

    int dropdownColumns(const Menu &menu)
    {
      int label = 0;
      int shortcut = 0;
      for (const auto &item : menu.items)
      {
        label = std::max(label, static_cast<int>(item.label.size()));
        shortcut = std::max(shortcut, static_cast<int>(item.shortcut.size()));
      }
      return kCheckColumns + label + (shortcut > 0 ? kShortcutGap + shortcut : 0);
    }

    int paramValue(const PlayerParamsView &view, PlayerParam param)
    {
      switch (param)
      {
      case PlayerParam::HitPoints: return view.hitPoints;
      case PlayerParam::Strength: return view.strength;
      case PlayerParam::Defence: return view.defence;
      }
      return 0;
    }

    std::string rightAligned(int value, int width)
    {
      std::string text = std::to_string(value);
      if (static_cast<int>(text.size()) < width)
      {
        text.insert(0, static_cast<std::size_t>(width) - text.size(), ' ');
      }
      return text;
    }

    // The Inventory panel's columns.
    //   "TRIANGLE <  Sword of the Fallen Devil >"
    //   "01 Sword of the Fallen Devil TRI   -  1  +"
    constexpr int kInventoryColumns = 40;
    constexpr int kLoadoutPrevColumn = 9;
    constexpr int kLoadoutNameColumn = 12;
    constexpr int kLoadoutNextColumn = 38;
    constexpr int kItemNameColumn = 3;
    constexpr int kItemNameWidth = 25;
    constexpr int kItemSlotColumn = 29;
    constexpr int kItemLessColumn = 34;
    constexpr int kItemCountColumn = 36;
    constexpr int kItemMoreColumn = 39;
    // DAT_003437A0's slot order, which is DAT_0031D168's.
    constexpr std::array<const char *, 3> kSlotNames = {"TRIANGLE", "CIRCLE", "CROSS"};
    constexpr std::array<const char *, 3> kSlotShortNames = {"TRI", "CIR", "CRO"};

    // Button ids. Player Params: param * 4 + step. Inventory: an item's count
    // buttons are item * 2 + (more), the loadout's are kLoadoutButtonBase +
    // slot * 2 + (next).
    constexpr int kLoadoutButtonBase = 0x1000;

    int paramButtonId(PlayerParam param, PlayerParamStep step)
    {
      return static_cast<int>(param) * 4 + static_cast<int>(step);
    }

    std::string hexByte(int value)
    {
      static constexpr char kDigits[] = "0123456789ABCDEF";
      return std::string{kDigits[(value >> 4) & 0xF], kDigits[value & 0xF]};
    }
  } // namespace

  int MenuBarLayout::titleAt(int pixelX, int pixelY) const
  {
    for (std::size_t index = 0; index < titles.size(); ++index)
    {
      if (titles[index].contains(unitX(pixelX, scale), unitY(pixelY, scale)))
      {
        return static_cast<int>(index);
      }
    }
    return -1;
  }

  int MenuBarLayout::itemAt(int pixelX, int pixelY) const
  {
    if (openMenu < 0 || !dropdown.contains(unitX(pixelX, scale), unitY(pixelY, scale)))
    {
      return -1;
    }
    const float y = unitY(pixelY, scale) - dropdown.top - kPadding;
    if (y < 0.0f)
    {
      return -1;
    }
    const int row = static_cast<int>(y) / kRowPitch;
    const int rows = static_cast<int>((dropdown.height - 2.0f * kPadding) / kRowPitch);
    return row < rows ? row : -1;
  }

  bool MenuBarLayout::covers(int pixelX, int pixelY) const
  {
    const float x = unitX(pixelX, scale);
    const float y = unitY(pixelY, scale);
    if (y >= 0.0f && y < static_cast<float>(kMenuBarHeight) && x >= 0.0f && x < screenWidth)
    {
      return true;
    }
    return openMenu >= 0 && dropdown.contains(x, y);
  }

  MenuBarLayout layoutMenuBar(int framebufferWidth, int framebufferHeight, const MenuBar &bar, int openMenu)
  {
    MenuBarLayout layout;
    layout.scale = scaleFor(framebufferHeight);
    layout.screenWidth = static_cast<float>(framebufferWidth) / layout.scale;
    float x = static_cast<float>(kMargin);
    for (const auto &menu : bar)
    {
      // One column of room either side of the title.
      const float width = static_cast<float>((menu.title.size() + 2) * kAdvance);
      layout.titles.push_back(Rect{x, 0.0f, width, static_cast<float>(kMenuBarHeight)});
      x += width;
    }
    if (openMenu >= 0 && openMenu < static_cast<int>(bar.size()))
    {
      const Menu &menu = bar[static_cast<std::size_t>(openMenu)];
      layout.openMenu = openMenu;
      layout.dropdown = Rect{layout.titles[static_cast<std::size_t>(openMenu)].left,
                             static_cast<float>(kMenuBarHeight),
                             static_cast<float>(dropdownColumns(menu) * kAdvance + 2 * kPadding),
                             static_cast<float>(static_cast<int>(menu.items.size()) * kRowPitch + 2 * kPadding)};
    }
    return layout;
  }

  void drawMenuBar(const DebugTextRenderer &text,
                   const DebugFont &font,
                   int framebufferWidth,
                   int framebufferHeight,
                   const MenuBarLayout &layout,
                   const MenuBar &bar,
                   int hoveredTitle,
                   int hoveredItem)
  {
    const float scale = layout.scale;
    const Menu *open = layout.openMenu >= 0 ? &bar[static_cast<std::size_t>(layout.openMenu)] : nullptr;
    {
      OverlayState state(framebufferWidth, framebufferHeight);
      fillPanel(scale, 0.0f, 0.0f, layout.screenWidth, static_cast<float>(kMenuBarHeight));
      // A rule under the bar, so it reads as chrome rather than as one more
      // panel floating over the world.
      fillRect(scale, 0.0f, static_cast<float>(kMenuBarHeight) - 1.0f, layout.screenWidth, 1.0f, kHeadingRed,
               kHeadingGreen, kHeadingBlue, 0.5f);
      for (std::size_t index = 0; index < layout.titles.size(); ++index)
      {
        const Rect &title = layout.titles[index];
        if (static_cast<int>(index) == layout.openMenu)
        {
          fillRect(scale, title.left, title.top, title.width, title.height - 1.0f, kHeadingRed, kHeadingGreen,
                   kHeadingBlue, 0.35f);
        }
        else if (static_cast<int>(index) == hoveredTitle)
        {
          fillRect(scale, title.left, title.top, title.width, title.height - 1.0f, 1.0f, 1.0f, 1.0f, 0.12f);
        }
      }
      if (open != nullptr)
      {
        const Rect &box = layout.dropdown;
        // Twice: a dropdown hangs over the tree, and one coat of the panel
        // colour leaves the tree's rows legible through it.
        fillPanel(scale, box.left, box.top, box.width, box.height);
        fillPanel(scale, box.left, box.top, box.width, box.height);
        strokeRect(scale, box.left, box.top, box.width, box.height, kHeadingRed, kHeadingGreen, kHeadingBlue, 0.5f);
        for (std::size_t index = 0; index < open->items.size(); ++index)
        {
          const MenuItem &item = open->items[index];
          const float top = box.top + kPadding + static_cast<float>(index) * kRowPitch;
          if (static_cast<int>(index) == hoveredItem)
          {
            fillRect(scale, box.left + 2.0f, top, box.width - 4.0f, kRowPitch, 1.0f, 1.0f, 1.0f, 0.15f);
          }
          if (item.checkable)
          {
            const float size = 10.0f;
            const float left = box.left + kPadding + 1.0f;
            const float boxTop = top + (kRowPitch - size) * 0.5f;
            strokeRect(scale, left, boxTop, size, size, 1.0f, 1.0f, 1.0f, 0.8f);
            if (item.checked)
            {
              fillRect(scale, left + 2.0f, boxTop + 2.0f, size - 4.0f, size - 4.0f, kHeadingRed, kHeadingGreen,
                       kHeadingBlue, 1.0f);
            }
          }
        }
      }
    }

    Glyphs titles;
    for (std::size_t index = 0; index < layout.titles.size(); ++index)
    {
      appendText(titles, bar[index].title, static_cast<int>(layout.titles[index].left) + kAdvance, kBarTextTop,
                 static_cast<int>(bar[index].title.size()));
    }
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, titles, 1.0f, 1.0f, 1.0f);

    if (open != nullptr)
    {
      const Rect &box = layout.dropdown;
      const int columns = dropdownColumns(*open);
      Glyphs labels;
      Glyphs shortcuts;
      for (std::size_t index = 0; index < open->items.size(); ++index)
      {
        const MenuItem &item = open->items[index];
        const int textLeft = static_cast<int>(box.left) + kPadding;
        const int top = static_cast<int>(box.top) + kPadding + static_cast<int>(index) * kRowPitch;
        appendText(labels, item.label, textLeft + kCheckColumns * kAdvance, top, columns);
        appendText(shortcuts, item.shortcut,
                   textLeft + (columns - static_cast<int>(item.shortcut.size())) * kAdvance, top, columns);
      }
      drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, labels, 1.0f, 1.0f, 1.0f);
      drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, shortcuts, 0.55f, 0.6f, 0.65f);
    }
  }

  ToolPanel buildPlayerParamsPanel(float scale, float left, float top, const PlayerParamsView &view)
  {
    ToolPanel panel("PLAYER PARAMS", kParamColumns, scale, left, top);
    if (!view.available)
    {
      panel.text(0, "NO LEAD IN THIS SCENE", ToolPanel::Tone::Dim);
      panel.nextRow();
      return panel;
    }
    for (const auto &row : kParamRows)
    {
      panel.text(0, row.label);
      panel.text(kValueColumn, rightAligned(paramValue(view, row.param), 3));
      for (const auto &button : kButtonColumns)
      {
        panel.button(button.column, button.text, paramButtonId(row.param, button.step));
      }
      panel.nextRow();
    }
    // +0x128 follows +0x12A on every edit, so it is shown rather than edited.
    panel.text(0, "MAX HP " + std::to_string(view.maxHitPoints) + "  (+128)", ToolPanel::Tone::Dim);
    panel.nextRow(static_cast<float>(kRowPitch));
    return panel;
  }

  ToolPanel buildInventoryPanel(float scale, float left, float top, const InventoryView &view)
  {
    ToolPanel panel("SPELLS", kInventoryColumns, scale, left, top);
    if (!view.available)
    {
      panel.text(0, "NO LEAD WITH A LOADOUT HERE", ToolPanel::Tone::Dim);
      panel.nextRow();
      return panel;
    }

    panel.heading("EQUIPPED  (LOADOUT ROW " + std::to_string(view.loadoutRow) + ")");
    for (std::size_t slot = 0; slot < view.loadout.size(); ++slot)
    {
      const int id = kLoadoutButtonBase + static_cast<int>(slot) * 2;
      panel.text(0, kSlotNames[slot]);
      panel.button(kLoadoutPrevColumn, "<", id, view.canSwap[slot]);
      panel.text(kLoadoutNameColumn,
                 view.loadout[slot] == 0 ? std::string("(empty)") : view.loadoutNames[slot]);
      panel.button(kLoadoutNextColumn, ">", id + 1, view.canSwap[slot]);
      panel.nextRow();
    }

    panel.heading("SPELL                              HELD");
    for (const auto &item : view.items)
    {
      // Held or equipped is "learned"; neither is dim.
      const bool learned = item.count > 0 || item.equippedSlot >= 0;
      const auto tone = learned ? ToolPanel::Tone::Body : ToolPanel::Tone::Dim;
      panel.text(0, hexByte(item.item), ToolPanel::Tone::Dim);
      panel.text(kItemNameColumn, item.name.substr(0, kItemNameWidth), tone);
      if (item.equippedSlot >= 0 && item.equippedSlot < static_cast<int>(kSlotShortNames.size()))
      {
        panel.text(kItemSlotColumn, kSlotShortNames[static_cast<std::size_t>(item.equippedSlot)],
                   ToolPanel::Tone::Heading);
      }
      panel.button(kItemLessColumn, "-", item.item * 2, item.count > 0);
      panel.text(kItemCountColumn, rightAligned(item.count, 2), tone);
      panel.button(kItemMoreColumn, "+", item.item * 2 + 1, item.count < 99);
      panel.nextRow();
    }
    return panel;
  }

  ToolPanel buildItemsPanel(float scale, float left, float top, const InventoryView &view)
  {
    ToolPanel panel("ITEMS", kInventoryColumns, scale, left, top);
    if (!view.available)
    {
      panel.text(0, "NO LEAD WITH A LOADOUT HERE", ToolPanel::Tone::Dim);
      panel.nextRow();
      return panel;
    }

    panel.heading("ITEM                               HELD");
    for (const auto &item : view.fieldItems)
    {
      const auto tone = item.count > 0 ? ToolPanel::Tone::Body : ToolPanel::Tone::Dim;
      panel.text(0, hexByte(item.item), ToolPanel::Tone::Dim);
      panel.text(kItemNameColumn, item.name.substr(0, kItemNameWidth), tone);
      panel.button(kItemLessColumn, "-", item.item * 2, item.count > 0);
      panel.text(kItemCountColumn, rightAligned(item.count, 2), tone);
      panel.button(kItemMoreColumn, "+", item.item * 2 + 1, item.count < 99);
      panel.nextRow();
    }
    return panel;
  }

  std::optional<HarnessRequest> toolPanelRequest(ToolPanelKind kind, int buttonId)
  {
    HarnessRequest request;
    switch (kind)
    {
    case ToolPanelKind::PlayerParams:
      request.kind = HarnessRequest::Kind::StepPlayerParam;
      request.param = static_cast<PlayerParam>(buttonId / 4);
      request.step = static_cast<PlayerParamStep>(buttonId % 4);
      return request;
    case ToolPanelKind::Inventory:
      if (buttonId >= kLoadoutButtonBase)
      {
        request.kind = HarnessRequest::Kind::CycleLoadout;
        request.slot = (buttonId - kLoadoutButtonBase) / 2;
        request.delta = (buttonId & 1) != 0 ? 1 : -1;
      }
      else
      {
        request.kind = HarnessRequest::Kind::AdjustItemCount;
        request.item = buttonId / 2;
        request.delta = (buttonId & 1) != 0 ? 1 : -1;
      }
      return request;
    case ToolPanelKind::Items:
      request.kind = HarnessRequest::Kind::AdjustItemCount;
      request.item = buttonId / 2;
      request.delta = (buttonId & 1) != 0 ? 1 : -1;
      return request;
    }
    return std::nullopt;
  }

} // namespace orphen::harness
