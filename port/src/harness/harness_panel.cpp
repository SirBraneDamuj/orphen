#include "harness/harness_panel.h"

#include <SDL_opengl.h>

namespace orphen::harness::panel
{

  namespace
  {
    void restore(GLenum capability, unsigned char wasEnabled)
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
  } // namespace

  float scaleFor(int framebufferHeight) { return framebufferHeight >= 1400 ? 2.0f : 1.0f; }

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

  void drawGlyphs(const DebugTextRenderer &text,
                  const DebugFont &font,
                  int framebufferWidth,
                  int framebufferHeight,
                  float scale,
                  const std::vector<orphen::ported::debug::DebugGlyph> &glyphs,
                  float red,
                  float green,
                  float blue)
  {
    text.drawOriginalOverlay(framebufferWidth, framebufferHeight, 0.0f, 0.0f, scale, scale, glyphs, font.texture,
                             font.width, font.height, red, green, blue);
  }

  void fillRect(float scale, float left, float top, float width, float height, float red, float green, float blue,
                float alpha)
  {
    glColor4f(red, green, blue, alpha);
    glBegin(GL_QUADS);
    glVertex2f(left * scale, top * scale);
    glVertex2f((left + width) * scale, top * scale);
    glVertex2f((left + width) * scale, (top + height) * scale);
    glVertex2f(left * scale, (top + height) * scale);
    glEnd();
  }

  void strokeRect(float scale, float left, float top, float width, float height, float red, float green,
                  float blue, float alpha)
  {
    // Half a pixel in, so the line lands on pixel centres.
    const float x0 = left * scale + 0.5f;
    const float y0 = top * scale + 0.5f;
    const float x1 = (left + width) * scale - 0.5f;
    const float y1 = (top + height) * scale - 0.5f;
    glColor4f(red, green, blue, alpha);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
  }

  void fillPanel(float scale, float left, float top, float width, float height)
  {
    fillRect(scale, left, top, width, height, 0.04f, 0.05f, 0.06f, 0.78f);
  }

  OverlayState::OverlayState(int framebufferWidth, int framebufferHeight)
      : depth_(glIsEnabled(GL_DEPTH_TEST)), texture_(glIsEnabled(GL_TEXTURE_2D)), fog_(glIsEnabled(GL_FOG)),
        blend_(glIsEnabled(GL_BLEND)), cull_(glIsEnabled(GL_CULL_FACE)), lighting_(glIsEnabled(GL_LIGHTING)),
        alphaTest_(glIsEnabled(GL_ALPHA_TEST))
  {
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(framebufferWidth), static_cast<double>(framebufferHeight), 0.0, -1.0, 1.0);
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

  OverlayState::~OverlayState()
  {
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    restore(GL_DEPTH_TEST, depth_);
    restore(GL_TEXTURE_2D, texture_);
    restore(GL_FOG, fog_);
    restore(GL_BLEND, blend_);
    restore(GL_CULL_FACE, cull_);
    restore(GL_LIGHTING, lighting_);
    restore(GL_ALPHA_TEST, alphaTest_);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
  }

} // namespace orphen::harness::panel
