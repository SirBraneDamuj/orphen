#pragma once

#include "harness/debug_text.h"
#include "ported/psm2/psm2_runtime.h"

#include <cstddef>
#include <string>
#include <vector>

namespace orphen::harness
{

  // The fly camera's entity tree and the inspector window beside it. PC-only;
  // nothing in the original corresponds to either. Both read the pool on the
  // rendered frame and never write it, so the simulation cannot see them.

  // One row: an occupied pool slot, in tree order. The tree is entity +0x192,
  // the slot an entity is attached to -- FUN_0020dc88 walks the same link to
  // the root of a chain -- so the lead's bandana and the party's held weapons
  // sit under whoever carries them.
  struct EntityTreeEntry
  {
    std::size_t slot = 0;
    int depth = 0;
    // The row text after the slot column: type and what is known of it.
    std::string label;
    // Where the entity stands in the world, and the volume the click frames.
    // An attached entity's +0x20..+0x28 are bone-local, so this is the
    // SceneObjectView world origin wherever there is one.
    orphen::ported::psm2::Vec3 origin{};
    float radius = 0.0f;
    float height = 0.0f;
  };
  using EntityTree = std::vector<EntityTreeEntry>;

  struct InspectorLine
  {
    std::string text;
    bool heading = false;
  };
  using InspectorLines = std::vector<InspectorLine>;

  // The panel's geometry for one framebuffer. Text is laid out in units of the
  // debug font's 10x20 cell and scaled to the window by `scale`, so hit tests
  // and drawing agree by construction.
  struct EntityTreeLayout
  {
    float scale = 1.0f;
    float left = 0.0f;  // units
    float top = 0.0f;   // units
    float width = 0.0f; // units
    float height = 0.0f;
    int columns = 0;     // characters a row has room for
    int visibleRows = 0;
    int firstRow = 0; // the scroll position, clamped
    int rowCount = 0;

    bool covers(int pixelX, int pixelY) const;
    // The tree index under the pixel, or -1.
    int rowAt(int pixelX, int pixelY) const;
  };

  // `scroll` is the requested first row; the layout clamps it.
  EntityTreeLayout layoutEntityTree(int framebufferWidth, int framebufferHeight, const EntityTree &tree,
                                    int scroll);

  struct DebugFont
  {
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
  };

  void drawEntityTree(const DebugTextRenderer &text,
                      const DebugFont &font,
                      int framebufferWidth,
                      int framebufferHeight,
                      const EntityTreeLayout &layout,
                      const EntityTree &tree,
                      std::size_t selectedSlot,
                      int hoveredRow);

  // The fly camera's key help, bottom left, in the tree's panel and text size
  // rather than the game's own debug-text scale, so it does not read as part of
  // the original's overlay. The first line is drawn as a heading. The tree
  // leaves room above it for kHelpPanelLines lines.
  inline constexpr int kHelpPanelLines = 3;
  void drawHelpPanel(const DebugTextRenderer &text,
                     const DebugFont &font,
                     int framebufferWidth,
                     int framebufferHeight,
                     const std::vector<std::string> &lines);

  // The inspector window's whole picture: one line per row, headings in the
  // gizmo's yellow, starting `scroll` lines down.
  void drawEntityInspector(const DebugTextRenderer &text,
                           const DebugFont &font,
                           int framebufferWidth,
                           int framebufferHeight,
                           const InspectorLines &lines,
                           int scroll);

  // The selected entity in the fly view: its collision cylinder, drawn once
  // faint through everything and once solid where it is in front. Emits
  // viewer-space vertices; expects the fly camera's matrices to be current.
  void drawEntityHighlight(const orphen::ported::psm2::Vec3 &origin, float radius, float height);

} // namespace orphen::harness
