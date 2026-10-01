#include "harness/entity_tree.h"

#include "ported/debug/original_debug_text.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>

namespace orphen::harness
{

  namespace
  {
    namespace debugText = orphen::ported::debug::text;
    using orphen::ported::psm2::Vec3;

    constexpr int kAdvance = debugText::kGlyphCellWidth;
    constexpr int kRowPitch = debugText::kGlyphCellHeight;
    constexpr int kPadding = 6;
    constexpr int kMargin = 8;
    // "255  ": the slot column and the gap after it.
    constexpr int kSlotColumns = 5;
    constexpr int kMinimumColumns = 26;
    constexpr int kMaximumColumns = 48;

    // Room left at the bottom for the fly camera's three lines of help, which
    // are laid out on the original's 640x448 screen from y = 448 - 8 - 60.
    constexpr float kBottomReserveFraction = 76.0f / debugText::kScreenHeight;

    float scaleFor(int framebufferHeight) { return framebufferHeight >= 1400 ? 2.0f : 1.0f; }

    int rowsTop(const EntityTreeLayout &layout) { return static_cast<int>(layout.top) + kPadding + kRowPitch + 4; }

    void appendText(std::vector<orphen::ported::debug::DebugGlyph> &glyphs, const std::string &text, int x, int y,
                    int columns)
    {
      for (int index = 0; index < static_cast<int>(text.size()) && index < columns; ++index)
      {
        if (text[index] != ' ')
        {
          glyphs.push_back({text[index], x + index * kAdvance, y});
        }
      }
    }

    // Glyphs are in layout units; this is the one place they become pixels.
    void drawGlyphs(const DebugTextRenderer &text, const DebugFont &font, int framebufferWidth,
                    int framebufferHeight, float scale,
                    const std::vector<orphen::ported::debug::DebugGlyph> &glyphs, float red, float green,
                    float blue)
    {
      text.drawOriginalOverlay(framebufferWidth, framebufferHeight, 0.0f, 0.0f, scale, scale, glyphs,
                               font.texture, font.width, font.height, red, green, blue);
    }

    // A flat rectangle in layout units, over whatever is there.
    void fillRect(float scale, float left, float top, float width, float height, float red, float green,
                  float blue, float alpha)
    {
      glColor4f(red, green, blue, alpha);
      glBegin(GL_QUADS);
      glVertex2f(left * scale, top * scale);
      glVertex2f((left + width) * scale, top * scale);
      glVertex2f((left + width) * scale, (top + height) * scale);
      glVertex2f(left * scale, (top + height) * scale);
      glEnd();
    }

    // The 2D state the panels draw in, and back.
    struct OverlayState
    {
      GLboolean depth, texture, fog, blend, cull, lighting, alphaTest;

      OverlayState(int framebufferWidth, int framebufferHeight)
          : depth(glIsEnabled(GL_DEPTH_TEST)), texture(glIsEnabled(GL_TEXTURE_2D)), fog(glIsEnabled(GL_FOG)),
            blend(glIsEnabled(GL_BLEND)), cull(glIsEnabled(GL_CULL_FACE)), lighting(glIsEnabled(GL_LIGHTING)),
            alphaTest(glIsEnabled(GL_ALPHA_TEST))
      {
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(framebufferWidth), static_cast<double>(framebufferHeight), 0.0, -1.0,
                1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_FOG);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }

      ~OverlayState()
      {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        restore(GL_DEPTH_TEST, depth);
        restore(GL_TEXTURE_2D, texture);
        restore(GL_FOG, fog);
        restore(GL_BLEND, blend);
        restore(GL_CULL_FACE, cull);
        restore(GL_LIGHTING, lighting);
        restore(GL_ALPHA_TEST, alphaTest);
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
      }

      static void restore(GLenum capability, GLboolean wasEnabled)
      {
        if (wasEnabled == GL_TRUE)
        {
          glEnable(capability);
        }
        else
        {
          glDisable(capability);
        }
      }
    };

    std::string rowText(const EntityTreeEntry &entry)
    {
      std::string slot = std::to_string(entry.slot);
      slot.insert(0, slot.size() < 3 ? 3 - slot.size() : 0, ' ');
      return slot + "  " + std::string(static_cast<std::size_t>(entry.depth) * 2, ' ') + entry.label;
    }

    void emitViewer(const Vec3 &game) { glVertex3f(game.x, game.z, -game.y); }
  } // namespace

  bool EntityTreeLayout::covers(int pixelX, int pixelY) const
  {
    const float x = static_cast<float>(pixelX) / scale;
    const float y = static_cast<float>(pixelY) / scale;
    return x >= left && x < left + width && y >= top && y < top + height;
  }

  int EntityTreeLayout::rowAt(int pixelX, int pixelY) const
  {
    if (!covers(pixelX, pixelY))
    {
      return -1;
    }
    const float y = static_cast<float>(pixelY) / scale - static_cast<float>(rowsTop(*this));
    if (y < 0.0f)
    {
      return -1;
    }
    const int row = static_cast<int>(y) / kRowPitch;
    if (row >= visibleRows || firstRow + row >= rowCount)
    {
      return -1;
    }
    return firstRow + row;
  }

  EntityTreeLayout layoutEntityTree(int framebufferWidth, int framebufferHeight, const EntityTree &tree, int scroll)
  {
    EntityTreeLayout layout;
    layout.scale = scaleFor(framebufferHeight);
    layout.rowCount = static_cast<int>(tree.size());
    const float screenWidth = static_cast<float>(framebufferWidth) / layout.scale;
    const float screenHeight = static_cast<float>(framebufferHeight) / layout.scale;

    int widest = 0;
    for (const auto &entry : tree)
    {
      widest = std::max(widest, static_cast<int>(rowText(entry).size()));
    }
    const int roomFor = static_cast<int>(screenWidth * 0.4f) / kAdvance;
    layout.columns = std::clamp(widest, kMinimumColumns, std::max(kMinimumColumns, std::min(kMaximumColumns, roomFor)));

    layout.left = static_cast<float>(kMargin);
    layout.top = static_cast<float>(kMargin);
    layout.width = static_cast<float>(layout.columns * kAdvance + 2 * kPadding);
    const float available =
        screenHeight * (1.0f - kBottomReserveFraction) - static_cast<float>(rowsTop(layout)) - kPadding;
    layout.visibleRows = std::max(1, static_cast<int>(available) / kRowPitch);
    layout.visibleRows = std::min(layout.visibleRows, std::max(1, layout.rowCount));
    layout.firstRow = std::clamp(scroll, 0, std::max(0, layout.rowCount - layout.visibleRows));
    layout.height =
        static_cast<float>(rowsTop(layout)) - layout.top + static_cast<float>(layout.visibleRows * kRowPitch) +
        kPadding;
    return layout;
  }

  void drawEntityTree(const DebugTextRenderer &text,
                      const DebugFont &font,
                      int framebufferWidth,
                      int framebufferHeight,
                      const EntityTreeLayout &layout,
                      const EntityTree &tree,
                      std::size_t selectedSlot,
                      int hoveredRow)
  {
    const float scale = layout.scale;
    const int textLeft = static_cast<int>(layout.left) + kPadding;
    const int firstRowTop = rowsTop(layout);
    {
      OverlayState state(framebufferWidth, framebufferHeight);
      fillRect(scale, layout.left, layout.top, layout.width, layout.height, 0.04f, 0.05f, 0.06f, 0.78f);
      for (int row = 0; row < layout.visibleRows; ++row)
      {
        const int index = layout.firstRow + row;
        if (index >= layout.rowCount)
        {
          break;
        }
        const float top = static_cast<float>(firstRowTop + row * kRowPitch);
        const float barLeft = layout.left + 2.0f;
        const float barWidth = layout.width - 4.0f;
        if (tree[index].slot == selectedSlot)
        {
          fillRect(scale, barLeft, top, barWidth, kRowPitch, 1.0f, 0.85f, 0.2f, 0.35f);
        }
        else if (index == hoveredRow)
        {
          fillRect(scale, barLeft, top, barWidth, kRowPitch, 1.0f, 1.0f, 1.0f, 0.12f);
        }
      }
    }

    std::vector<orphen::ported::debug::DebugGlyph> header;
    std::string title = "ENTITIES " + std::to_string(tree.size());
    if (layout.visibleRows < layout.rowCount)
    {
      title += "  " + std::to_string(layout.firstRow + 1) + "-" +
               std::to_string(layout.firstRow + layout.visibleRows);
    }
    appendText(header, title, textLeft, static_cast<int>(layout.top) + kPadding, layout.columns);
    const std::string hint = "F5";
    appendText(header, hint, textLeft + (layout.columns - static_cast<int>(hint.size())) * kAdvance,
               static_cast<int>(layout.top) + kPadding, layout.columns);
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, header, 1.0f, 0.85f, 0.2f);

    std::vector<orphen::ported::debug::DebugGlyph> rows;
    for (int row = 0; row < layout.visibleRows; ++row)
    {
      const int index = layout.firstRow + row;
      if (index >= layout.rowCount)
      {
        break;
      }
      appendText(rows, rowText(tree[index]), textLeft, firstRowTop + row * kRowPitch, layout.columns);
    }
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, rows, 1.0f, 1.0f, 1.0f);
  }

  void drawEntityInspector(const DebugTextRenderer &text,
                           const DebugFont &font,
                           int framebufferWidth,
                           int framebufferHeight,
                           const InspectorLines &lines,
                           int scroll)
  {
    const float scale = scaleFor(framebufferHeight);
    const int columns = std::max(1, static_cast<int>(static_cast<float>(framebufferWidth) / scale) / kAdvance - 2);
    std::vector<orphen::ported::debug::DebugGlyph> headings;
    std::vector<orphen::ported::debug::DebugGlyph> body;
    int y = kMargin - scroll * kRowPitch;
    for (const auto &line : lines)
    {
      if (y > -kRowPitch && static_cast<float>(y) * scale < static_cast<float>(framebufferHeight))
      {
        appendText(line.heading ? headings : body, line.text, kMargin, y, columns);
      }
      y += kRowPitch;
    }
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, headings, 1.0f, 0.85f, 0.2f);
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale, body, 1.0f, 1.0f, 1.0f);
  }

  void drawEntityHighlight(const Vec3 &origin, float radius, float height)
  {
    constexpr int kSegments = 32;
    constexpr float kTwoPi = 6.28318531f;
    // A point-sized effect still gets a ring the eye can find.
    const float ringRadius = std::max(radius, 0.08f);
    const float top = std::max(height, 0.02f);

    const auto ringPoint = [&](int index, float z) {
      const float angle = kTwoPi * static_cast<float>(index) / static_cast<float>(kSegments);
      return Vec3{origin.x + std::cos(angle) * ringRadius, origin.y + std::sin(angle) * ringRadius, origin.z + z};
    };
    const auto draw = [&](float alpha) {
      glColor4f(0.3f, 0.9f, 1.0f, alpha);
      glLineWidth(2.0f);
      for (const float z : {0.0f, top})
      {
        glBegin(GL_LINE_LOOP);
        for (int index = 0; index < kSegments; ++index)
        {
          emitViewer(ringPoint(index, z));
        }
        glEnd();
      }
      glBegin(GL_LINES);
      for (int index = 0; index < kSegments; index += kSegments / 4)
      {
        emitViewer(ringPoint(index, 0.0f));
        emitViewer(ringPoint(index, top));
      }
      // A stalk above the head, so the selection reads from across the map.
      emitViewer({origin.x, origin.y, origin.z + top});
      emitViewer({origin.x, origin.y, origin.z + top + 0.6f});
      glEnd();
    };

    const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean lightingWasEnabled = glIsEnabled(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    draw(0.35f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    draw(1.0f);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glLineWidth(1.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (textureWasEnabled == GL_TRUE)
    {
      glEnable(GL_TEXTURE_2D);
    }
    if (lightingWasEnabled == GL_TRUE)
    {
      glEnable(GL_LIGHTING);
    }
  }

} // namespace orphen::harness
