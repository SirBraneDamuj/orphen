#include "harness/debug_text.h"

#include <SDL_opengl.h>

#include <cctype>

namespace orphen::harness
{
  namespace
  {

    // Each glyph is a short run of segments on a 0..1 box, +Y up. Kept
    // deliberately blocky: this only has to be readable at 11 px.
    const StrokeSegment *glyphStrokesImpl(char character, int &strokeCount)
    {
      static const StrokeSegment k0[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 1}};
      static const StrokeSegment k1[] = {{0.5f, 0, 0.5f, 1}, {0.2f, 0.8f, 0.5f, 1}};
      static const StrokeSegment k2[] = {{0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}, {0, 0.5f, 0, 0}, {0, 0, 1, 0}};
      static const StrokeSegment k3[] = {{0, 1, 1, 1}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0.5f, 1, 0.5f}};
      static const StrokeSegment k4[] = {{0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 1, 1, 0}};
      static const StrokeSegment k5[] = {{1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const StrokeSegment k6[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0.5f}, {1, 0.5f, 0, 0.5f}};
      static const StrokeSegment k7[] = {{0, 1, 1, 1}, {1, 1, 0.3f, 0}};
      static const StrokeSegment k8[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0.5f, 1, 0.5f}};
      static const StrokeSegment k9[] = {{1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}};

      static const StrokeSegment kA[] = {{0, 0, 0.5f, 1}, {0.5f, 1, 1, 0}, {0.2f, 0.4f, 0.8f, 0.4f}};
      static const StrokeSegment kB[] = {{0, 0, 0, 1}, {0, 1, 0.8f, 1}, {0.8f, 1, 0.8f, 0.5f}, {0.8f, 0.5f, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const StrokeSegment kC[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}};
      static const StrokeSegment kD[] = {{0, 0, 0, 1}, {0, 1, 0.7f, 1}, {0.7f, 1, 1, 0.7f}, {1, 0.7f, 1, 0.3f}, {1, 0.3f, 0.7f, 0}, {0.7f, 0, 0, 0}};
      static const StrokeSegment kE[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0.5f, 0.7f, 0.5f}};
      static const StrokeSegment kF[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0.5f, 0.7f, 0.5f}};
      static const StrokeSegment kG[] = {{1, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 0.5f}, {1, 0.5f, 0.5f, 0.5f}};
      static const StrokeSegment kH[] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {0, 0.5f, 1, 0.5f}};
      static const StrokeSegment kI[] = {{0.5f, 0, 0.5f, 1}, {0.2f, 1, 0.8f, 1}, {0.2f, 0, 0.8f, 0}};
      static const StrokeSegment kJ[] = {{1, 1, 1, 0.2f}, {1, 0.2f, 0.5f, 0}, {0.5f, 0, 0, 0.2f}};
      static const StrokeSegment kK[] = {{0, 0, 0, 1}, {1, 1, 0, 0.5f}, {0, 0.5f, 1, 0}};
      static const StrokeSegment kL[] = {{0, 1, 0, 0}, {0, 0, 1, 0}};
      static const StrokeSegment kM[] = {{0, 0, 0, 1}, {0, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 1, 1}, {1, 1, 1, 0}};
      static const StrokeSegment kN[] = {{0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 1, 1}};
      static const StrokeSegment kO[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}};
      static const StrokeSegment kP[] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}};
      static const StrokeSegment kQ[] = {{0, 0, 1, 0}, {1, 0, 1, 1}, {1, 1, 0, 1}, {0, 1, 0, 0}, {0.6f, 0.4f, 1, 0}};
      static const StrokeSegment kR[] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 0.5f}, {1, 0.5f, 0, 0.5f}, {0.4f, 0.5f, 1, 0}};
      static const StrokeSegment kS[] = {{1, 1, 0, 1}, {0, 1, 0, 0.5f}, {0, 0.5f, 1, 0.5f}, {1, 0.5f, 1, 0}, {1, 0, 0, 0}};
      static const StrokeSegment kT[] = {{0, 1, 1, 1}, {0.5f, 1, 0.5f, 0}};
      static const StrokeSegment kU[] = {{0, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 1, 1}};
      static const StrokeSegment kV[] = {{0, 1, 0.5f, 0}, {0.5f, 0, 1, 1}};
      static const StrokeSegment kW[] = {{0, 1, 0.2f, 0}, {0.2f, 0, 0.5f, 0.6f}, {0.5f, 0.6f, 0.8f, 0}, {0.8f, 0, 1, 1}};
      static const StrokeSegment kX[] = {{0, 0, 1, 1}, {0, 1, 1, 0}};
      static const StrokeSegment kY[] = {{0, 1, 0.5f, 0.5f}, {1, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0}};
      static const StrokeSegment kZ[] = {{0, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}};

      static const StrokeSegment kMinus[] = {{0.1f, 0.5f, 0.9f, 0.5f}};
      static const StrokeSegment kPlus[] = {{0.1f, 0.5f, 0.9f, 0.5f}, {0.5f, 0.15f, 0.5f, 0.85f}};
      static const StrokeSegment kDot[] = {{0.4f, 0, 0.6f, 0}};
      static const StrokeSegment kComma[] = {{0.5f, 0.15f, 0.3f, -0.15f}};
      static const StrokeSegment kColon[] = {{0.4f, 0.7f, 0.6f, 0.7f}, {0.4f, 0.25f, 0.6f, 0.25f}};
      static const StrokeSegment kSlash[] = {{0, 0, 1, 1}};
      static const StrokeSegment kEquals[] = {{0.1f, 0.65f, 0.9f, 0.65f}, {0.1f, 0.35f, 0.9f, 0.35f}};
      static const StrokeSegment kLParen[] = {{0.7f, 1, 0.3f, 0.6f}, {0.3f, 0.6f, 0.3f, 0.4f}, {0.3f, 0.4f, 0.7f, 0}};
      static const StrokeSegment kRParen[] = {{0.3f, 1, 0.7f, 0.6f}, {0.7f, 0.6f, 0.7f, 0.4f}, {0.7f, 0.4f, 0.3f, 0}};
      static const StrokeSegment kLBracket[] = {{0.7f, 1, 0.3f, 1}, {0.3f, 1, 0.3f, 0}, {0.3f, 0, 0.7f, 0}};
      static const StrokeSegment kRBracket[] = {{0.3f, 1, 0.7f, 1}, {0.7f, 1, 0.7f, 0}, {0.7f, 0, 0.3f, 0}};

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

  const StrokeSegment *glyphStrokeSegments(char character, int &segmentCount)
  {
    return glyphStrokesImpl(character, segmentCount);
  }

  void DebugTextRenderer::drawGlyph(char character, float originX, float originY, float scale) const
  {
    int strokeCount = 0;
    const StrokeSegment *strokes = glyphStrokesImpl(character, strokeCount);
    if (strokes == nullptr)
    {
      return;
    }

    for (int strokeIndex = 0; strokeIndex < strokeCount; ++strokeIndex)
    {
      const StrokeSegment &stroke = strokes[strokeIndex];
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
