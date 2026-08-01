#include "harness/debug_text.h"

#include <SDL_opengl.h>

#include <cctype>

namespace orphen::harness
{
  namespace
  {

    struct Stroke
    {
      float x0, y0, x1, y1;
    };

    // Each glyph is a short run of segments on a 0..1 box, +Y up. Kept
    // deliberately blocky: this only has to be readable at 11 px.
    const Stroke *glyphStrokes(char character, int &strokeCount)
    {
      static const Stroke k0[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 1}};
      static const Stroke k1[] = {{0.5f, 0, 0.5f, 1}, {0.2f, 0.8f, 0.5f, 1}};
      static const Stroke k2[] = {{0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}, {0, 0.5f, 0, 0}, {0, 0, 1, 0}};
      static const Stroke k3[] = {{0, 1, 1, 1}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0.5f, 1, 0.5f}};
      static const Stroke k4[] = {{0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 1, 1, 0}};
      static const Stroke k5[] = {{1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const Stroke k6[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0.5f}, {1, 0.5f, 0, 0.5f}};
      static const Stroke k7[] = {{0, 1, 1, 1}, {1, 1, 0.3f, 0}};
      static const Stroke k8[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0.5f, 1, 0.5f}};
      static const Stroke k9[] = {{1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}};

      static const Stroke kA[] = {{0, 0, 0.5f, 1}, {0.5f, 1, 1, 0}, {0.2f, 0.4f, 0.8f, 0.4f}};
      static const Stroke kB[] = {{0, 0, 0, 1}, {0, 1, 0.8f, 1}, {0.8f, 1, 0.8f, 0.5f}, {0.8f, 0.5f, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const Stroke kC[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}};
      static const Stroke kD[] = {{0, 0, 0, 1}, {0, 1, 0.7f, 1}, {0.7f, 1, 1, 0.7f}, {1, 0.7f, 1, 0.3f}, {1, 0.3f, 0.7f, 0}, {0.7f, 0, 0, 0}};
      static const Stroke kE[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0.5f, 0.7f, 0.5f}};
      static const Stroke kF[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0.5f, 0.7f, 0.5f}};
      static const Stroke kG[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0.5f}, {1, 0.5f, 0.5f, 0.5f}};
      static const Stroke kH[] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {0, 0.5f, 1, 0.5f}};
      static const Stroke kI[] = {{0.5f, 0, 0.5f, 1}, {0.2f, 1, 0.8f, 1}, {0.2f, 0, 0.8f, 0}};
      static const Stroke kJ[] = {{1, 1, 1, 0.2f}, {1, 0.2f, 0.5f, 0}, {0.5f, 0, 0, 0.2f}};
      static const Stroke kK[] = {{0, 0, 0, 1}, {1, 1, 0, 0.5f}, {0, 0.5f, 1, 0}};
      static const Stroke kL[] = {{0, 1, 0, 0}, {0, 0, 1, 0}};
      static const Stroke kM[] = {{0, 0, 0, 1}, {0, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 1, 1}, {1, 1, 1, 0}};
      static const Stroke kN[] = {{0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 1, 1}};
      static const Stroke kO[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}};
      static const Stroke kP[] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}};
      static const Stroke kQ[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0.6f, 0.4f, 1, 0}};
      static const Stroke kR[] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}, {0.4f, 0.5f, 1, 0}};
      static const Stroke kS[] = {{1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const Stroke kT[] = {{0, 1, 1, 1}, {0.5f, 1, 0.5f, 0}};
      static const Stroke kU[] = {{0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 1}};
      static const Stroke kV[] = {{0, 1, 0.5f, 0}, {0.5f, 0, 1, 1}};
      static const Stroke kW[] = {{0, 1, 0.2f, 0}, {0.2f, 0, 0.5f, 0.6f}, {0.5f, 0.6f, 0.8f, 0}, {0.8f, 0, 1, 1}};
      static const Stroke kX[] = {{0, 0, 1, 1}, {0, 1, 1, 0}};
      static const Stroke kY[] = {{0, 1, 0.5f, 0.5f}, {1, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0}};
      static const Stroke kZ[] = {{0, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}};

      static const Stroke kMinus[] = {{0.1f, 0.5f, 0.9f, 0.5f}};
      static const Stroke kPlus[] = {{0.1f, 0.5f, 0.9f, 0.5f}, {0.5f, 0.15f, 0.5f, 0.85f}};
      static const Stroke kDot[] = {{0.4f, 0, 0.6f, 0}};
      static const Stroke kComma[] = {{0.5f, 0.15f, 0.3f, -0.15f}};
      static const Stroke kColon[] = {{0.4f, 0.7f, 0.6f, 0.7f}, {0.4f, 0.25f, 0.6f, 0.25f}};
      static const Stroke kSlash[] = {{0, 0, 1, 1}};
      static const Stroke kEquals[] = {{0.1f, 0.65f, 0.9f, 0.65f}, {0.1f, 0.35f, 0.9f, 0.35f}};
      static const Stroke kLParen[] = {{0.7f, 1, 0.3f, 0.6f}, {0.3f, 0.6f, 0.3f, 0.4f}, {0.3f, 0.4f, 0.7f, 0}};
      static const Stroke kRParen[] = {{0.3f, 1, 0.7f, 0.6f}, {0.7f, 0.6f, 0.7f, 0.4f}, {0.7f, 0.4f, 0.3f, 0}};
      static const Stroke kLBracket[] = {{0.7f, 1, 0.3f, 1}, {0.3f, 1, 0.3f, 0}, {0.3f, 0, 0.7f, 0}};
      static const Stroke kRBracket[] = {{0.3f, 1, 0.7f, 1}, {0.7f, 1, 0.7f, 0}, {0.7f, 0, 0.3f, 0}};

#define ORPHEN_GLYPH(table)                                              \
  strokeCount = static_cast<int>(sizeof(table) / sizeof(table[0]));      \
  return table

      switch (character)
      {
      case '0': ORPHEN_GLYPH(k0);
      case '1': ORPHEN_GLYPH(k1);
      case '2': ORPHEN_GLYPH(k2);
      case '3': ORPHEN_GLYPH(k3);
      case '4': ORPHEN_GLYPH(k4);
      case '5': ORPHEN_GLYPH(k5);
      case '6': ORPHEN_GLYPH(k6);
      case '7': ORPHEN_GLYPH(k7);
      case '8': ORPHEN_GLYPH(k8);
      case '9': ORPHEN_GLYPH(k9);
      case 'A': ORPHEN_GLYPH(kA);
      case 'B': ORPHEN_GLYPH(kB);
      case 'C': ORPHEN_GLYPH(kC);
      case 'D': ORPHEN_GLYPH(kD);
      case 'E': ORPHEN_GLYPH(kE);
      case 'F': ORPHEN_GLYPH(kF);
      case 'G': ORPHEN_GLYPH(kG);
      case 'H': ORPHEN_GLYPH(kH);
      case 'I': ORPHEN_GLYPH(kI);
      case 'J': ORPHEN_GLYPH(kJ);
      case 'K': ORPHEN_GLYPH(kK);
      case 'L': ORPHEN_GLYPH(kL);
      case 'M': ORPHEN_GLYPH(kM);
      case 'N': ORPHEN_GLYPH(kN);
      case 'O': ORPHEN_GLYPH(kO);
      case 'P': ORPHEN_GLYPH(kP);
      case 'Q': ORPHEN_GLYPH(kQ);
      case 'R': ORPHEN_GLYPH(kR);
      case 'S': ORPHEN_GLYPH(kS);
      case 'T': ORPHEN_GLYPH(kT);
      case 'U': ORPHEN_GLYPH(kU);
      case 'V': ORPHEN_GLYPH(kV);
      case 'W': ORPHEN_GLYPH(kW);
      case 'X': ORPHEN_GLYPH(kX);
      case 'Y': ORPHEN_GLYPH(kY);
      case 'Z': ORPHEN_GLYPH(kZ);
      case '-': ORPHEN_GLYPH(kMinus);
      case '+': ORPHEN_GLYPH(kPlus);
      case '.': ORPHEN_GLYPH(kDot);
      case ',': ORPHEN_GLYPH(kComma);
      case ':': ORPHEN_GLYPH(kColon);
      case '/': ORPHEN_GLYPH(kSlash);
      case '=': ORPHEN_GLYPH(kEquals);
      case '(': ORPHEN_GLYPH(kLParen);
      case ')': ORPHEN_GLYPH(kRParen);
      case '[': ORPHEN_GLYPH(kLBracket);
      case ']': ORPHEN_GLYPH(kRBracket);
      default: break;
      }

#undef ORPHEN_GLYPH

      strokeCount = 0;
      return nullptr;
    }

  } // namespace

  void DebugTextRenderer::drawGlyph(char character, float originX, float originY, float scale) const
  {
    int strokeCount = 0;
    const Stroke *strokes = glyphStrokes(character, strokeCount);
    if (strokes == nullptr)
    {
      return;
    }

    for (int strokeIndex = 0; strokeIndex < strokeCount; ++strokeIndex)
    {
      const Stroke &stroke = strokes[strokeIndex];
      glVertex2f(originX + stroke.x0 * scale, originY - stroke.y0 * scale);
      glVertex2f(originX + stroke.x1 * scale, originY - stroke.y1 * scale);
    }
  }

  void DebugTextRenderer::draw(int framebufferWidth,
                               int framebufferHeight,
                               const std::vector<std::string> &lines,
                               float pixelsPerGlyph) const
  {
    if (lines.empty() || framebufferWidth <= 0 || framebufferHeight <= 0)
    {
      return;
    }

    const float glyphWidth = pixelsPerGlyph * 0.62f;
    const float advance = pixelsPerGlyph * 0.78f;
    const float lineHeight = pixelsPerGlyph * 1.6f;
    const float marginX = 10.0f;
    const float marginY = 10.0f;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(framebufferWidth), static_cast<double>(framebufferHeight), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    GLint previousPolygonMode[2] = {GL_FILL, GL_FILL};
    glGetIntegerv(GL_POLYGON_MODE, previousPolygonMode);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // A dark panel behind the text so it stays readable over bright geometry.
    float widestLine = 0.0f;
    for (const auto &line : lines)
    {
      widestLine = std::max(widestLine, static_cast<float>(line.size()) * advance);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glBegin(GL_QUADS);
    const float panelRight = marginX + widestLine + 8.0f;
    const float panelBottom = marginY + static_cast<float>(lines.size()) * lineHeight + 4.0f;
    glVertex2f(marginX - 6.0f, marginY - 6.0f);
    glVertex2f(panelRight, marginY - 6.0f);
    glVertex2f(panelRight, panelBottom);
    glVertex2f(marginX - 6.0f, panelBottom);
    glEnd();

    glColor4f(0.85f, 0.95f, 0.85f, 1.0f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    float penY = marginY + pixelsPerGlyph;
    for (const auto &line : lines)
    {
      float penX = marginX;
      for (const char rawCharacter : line)
      {
        const char character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(rawCharacter)));
        drawGlyph(character, penX, penY, glyphWidth);
        penX += advance;
      }
      penY += lineHeight;
    }
    glEnd();

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPolygonMode(GL_FRONT, static_cast<GLenum>(previousPolygonMode[0]));
    glPolygonMode(GL_BACK, static_cast<GLenum>(previousPolygonMode[1]));
    if (textureWasEnabled == GL_TRUE)
    {
      glEnable(GL_TEXTURE_2D);
    }
    if (depthWasEnabled == GL_TRUE)
    {
      glEnable(GL_DEPTH_TEST);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

} // namespace orphen::harness
