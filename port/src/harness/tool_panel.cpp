#include "harness/tool_panel.h"

#include <utility>

namespace orphen::harness
{

  namespace
  {
    using namespace panel;
    using Glyphs = std::vector<orphen::ported::debug::DebugGlyph>;

    // The title row, and the space under it.
    constexpr float kTitleHeight = static_cast<float>(kRowPitch + 6);
  } // namespace

  ToolPanel::ToolPanel(std::string title, int columns, float scale, float left, float top)
      : title_(std::move(title)), columns_(columns), scale_(scale), left_(left), top_(top),
        rowTop_(top + kPadding + kTitleHeight)
  {
  }

  void ToolPanel::text(int column, std::string text, Tone tone)
  {
    spans_.push_back(Span{textLeft() + static_cast<float>(column * kAdvance), rowTop_, std::move(text), tone});
  }

  void ToolPanel::button(int column, std::string label, int id, bool enabled)
  {
    // Three units of room round the label, and one off the row's top so a
    // column of buttons has a seam between them.
    const float width = static_cast<float>(label.size() * kAdvance);
    buttons_.push_back(Button{Rect{textLeft() + static_cast<float>(column * kAdvance) - 3.0f, rowTop_ - 1.0f,
                                   width + 6.0f, static_cast<float>(kRowPitch) + 2.0f},
                              id, enabled});
    text(column, std::move(label), enabled ? Tone::Body : Tone::Dim);
  }

  void ToolPanel::heading(std::string text)
  {
    rowTop_ += 4.0f;
    this->text(0, std::move(text), Tone::Heading);
    nextRow(static_cast<float>(kRowPitch + 2));
  }

  void ToolPanel::nextRow(float pitch) { rowTop_ += pitch; }

  Rect ToolPanel::box() const
  {
    return Rect{left_, top_, static_cast<float>(columns_ * kAdvance + 2 * kPadding), rowTop_ - top_ + kPadding};
  }

  Rect ToolPanel::closeRect() const
  {
    const Rect panel = box();
    const float size = static_cast<float>(kRowPitch);
    return Rect{panel.left + panel.width - kPadding - size, top_ + kPadding - 1.0f, size, size};
  }

  bool ToolPanel::covers(int pixelX, int pixelY) const
  {
    return box().contains(static_cast<float>(pixelX) / scale_, static_cast<float>(pixelY) / scale_);
  }

  bool ToolPanel::closeAt(int pixelX, int pixelY) const
  {
    return closeRect().contains(static_cast<float>(pixelX) / scale_, static_cast<float>(pixelY) / scale_);
  }

  std::optional<int> ToolPanel::buttonAt(int pixelX, int pixelY) const
  {
    const float x = static_cast<float>(pixelX) / scale_;
    const float y = static_cast<float>(pixelY) / scale_;
    for (const auto &button : buttons_)
    {
      if (button.enabled && button.rect.contains(x, y))
      {
        return button.id;
      }
    }
    return std::nullopt;
  }

  void ToolPanel::draw(const DebugTextRenderer &text,
                       const DebugFont &font,
                       int framebufferWidth,
                       int framebufferHeight,
                       std::optional<int> hoveredButton,
                       bool closeHovered) const
  {
    const Rect panel = box();
    const Rect close = closeRect();
    {
      OverlayState state(framebufferWidth, framebufferHeight);
      // Twice, like an open menu: panels can sit over the tree.
      fillPanel(scale_, panel.left, panel.top, panel.width, panel.height);
      fillPanel(scale_, panel.left, panel.top, panel.width, panel.height);
      strokeRect(scale_, panel.left, panel.top, panel.width, panel.height, kHeadingRed, kHeadingGreen,
                 kHeadingBlue, 0.5f);
      if (closeHovered)
      {
        fillRect(scale_, close.left, close.top, close.width, close.height, 1.0f, 1.0f, 1.0f, 0.15f);
      }
      for (const auto &button : buttons_)
      {
        const bool hovered = button.enabled && hoveredButton == button.id;
        const float fill = !button.enabled ? 0.02f : hovered ? 0.25f : 0.07f;
        fillRect(scale_, button.rect.left, button.rect.top, button.rect.width, button.rect.height, 1.0f, 1.0f, 1.0f,
                 fill);
        strokeRect(scale_, button.rect.left, button.rect.top, button.rect.width, button.rect.height, 1.0f, 1.0f,
                   1.0f, button.enabled ? 0.35f : 0.12f);
      }
    }

    Glyphs body;
    Glyphs heading;
    Glyphs dim;
    appendText(heading, title_, static_cast<int>(textLeft()), static_cast<int>(top_) + kPadding, columns_);
    appendText(body, "X", static_cast<int>(close.left) + (kRowPitch - kAdvance) / 2, static_cast<int>(close.top) + 1,
               1);
    for (const auto &span : spans_)
    {
      Glyphs &into = span.tone == Tone::Heading ? heading : span.tone == Tone::Dim ? dim : body;
      appendText(into, span.text, static_cast<int>(span.x), static_cast<int>(span.y), columns_);
    }
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale_, body, 1.0f, 1.0f, 1.0f);
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale_, heading, kHeadingRed, kHeadingGreen,
               kHeadingBlue);
    drawGlyphs(text, font, framebufferWidth, framebufferHeight, scale_, dim, 0.55f, 0.6f, 0.65f);
  }

} // namespace orphen::harness
