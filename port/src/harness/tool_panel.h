#pragma once

// A titled, closable panel of text and buttons laid out on a character grid --
// what the fly view's menu items open (harness/harness_menu.h). PC-only.
//
// A panel is rebuilt from the current numbers whenever it is needed, for a hit
// test or for drawing, so the two always agree and nothing is cached across a
// change. Buttons carry an integer id that the panel's owner decodes.

#include "harness/harness_panel.h"

#include <optional>
#include <string>
#include <vector>

namespace orphen::harness
{

  class ToolPanel
  {
  public:
    enum class Tone
    {
      Body,
      Heading,
      Dim,
    };

    // `columns` is the text width in characters; the box adds padding.
    ToolPanel(std::string title, int columns, float scale, float left, float top);

    // Text and buttons go on the current row, at a character column.
    void text(int column, std::string text, Tone tone = Tone::Body);
    void button(int column, std::string label, int id, bool enabled = true);
    // A heading on a row of its own, with a little space above it.
    void heading(std::string text);
    // Move down a row. Rows holding buttons want the default pitch, which
    // leaves a gap between button boxes.
    void nextRow(float pitch = static_cast<float>(panel::kRowPitch + 3));

    float scale() const { return scale_; }
    panel::Rect box() const;
    bool covers(int pixelX, int pixelY) const;
    bool closeAt(int pixelX, int pixelY) const;
    // The enabled button under the pixel.
    std::optional<int> buttonAt(int pixelX, int pixelY) const;

    void draw(const DebugTextRenderer &text,
              const DebugFont &font,
              int framebufferWidth,
              int framebufferHeight,
              std::optional<int> hoveredButton,
              bool closeHovered) const;

  private:
    struct Span
    {
      float x = 0.0f;
      float y = 0.0f;
      std::string text;
      Tone tone = Tone::Body;
    };
    struct Button
    {
      panel::Rect rect;
      int id = 0;
      bool enabled = true;
    };

    std::string title_;
    int columns_;
    float scale_;
    float left_;
    float top_;
    float rowTop_;
    std::vector<Span> spans_;
    std::vector<Button> buttons_;

    float textLeft() const { return left_ + panel::kPadding; }
    panel::Rect closeRect() const;
  };

} // namespace orphen::harness
