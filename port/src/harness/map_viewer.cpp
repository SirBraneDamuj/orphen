#include "harness/map_viewer.h"

#include "harness/entity_probe.h"

#include "ported/model/psc3_skeleton.h"
#include "ported/render/original_entity_draw.h"

#include <string>

#include "harness/scene_resource_tree.h"
#include "ported/psm2/decoded_psm2_loader.h"

#include "platform/gl_extensions.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orphen::harness
{
  namespace
  {

    constexpr double kPi = 3.14159265358979323846;
    constexpr std::uint32_t kRecord80HiddenBit = 0x20;

    // Flag bit 0 is the two-sided bit. FUN_00211230:190 hands it to the VU1
    // program as a byte of its own -- `*(byte *)(packet + 9) = flags & 1` --
    // at the offset beside the vertex count and material parameters, which is
    // where a per-primitive cull toggle belongs.
    //
    // On s01_e024 exactly 32 of 1630 primitives carry it, and they are exactly
    // 16 coincident perpendicular pairs: the four hanging chains at
    // (+/-0.45, +/-4.30), four vertical segments each, built as crossed planes.
    // Nothing else in the map sets it. Culling those is what made them vanish
    // from one side.
    constexpr std::uint32_t kRecord80TwoSidedBit = 0x1;

    // FUN_00211230:201 hands bit 13 to the VU1 program as draw header byte 15,
    // and VU1 0x01ba branches past the entire lighting block when it is set.
    constexpr std::uint32_t kRecord80UnlitBit = 0x2000;

    // The original's frustum ends at DAT_00355628 and it fogs everything out
    // before that, so nothing needs to survive past it. The margin keeps
    // primitives whose centre is inside the draw distance but whose far
    // corners are not from being clipped by GL.
    constexpr float kFarPlaneMargin = 8.0f;

    // **There are two distance effects and they are not the same one.**
    //
    // The GS fog (PRIM's FGE bit) uses DAT_0035567c, which FUN_0022a418:344
    // sets to drawDistance * 0.25, and FUN_0020a2c0:514 drops the FGE bit
    // whenever that start is 5.0 or more. At the usual 32 unit draw distance
    // the start is 8.0, so GS fog is off in most maps -- including s01_e024.
    //
    // What is actually visible in the distance is a second effect, done in the
    // VU1 microprogram rather than by the GS. FUN_00209140:83-94 uploads
    // (depthOffset, depthScale, drawDistance, drawDistance - 10) and the
    // microcode attenuates vertex colours across that last band. It is
    // unconditional, which is why the greying is there when FGE fog is not.
    //
    // GL has one fog unit, so it stands in for the VU1 fade -- the band and
    // colour are the fade's, not the GS fog's.
    //
    // The band and colour themselves are per scene and arrive through
    // setFogBand / setFogColour; see PortRuntime::applySceneEnvironment.

    // GL's own fog ramps linearly in eye distance. The GS does not: the depth
    // it interpolates is the perspective term FUN_0020bd58 builds,
    // screenZ = DAT_003555a0 + DAT_003555a4 / z, so the fade is linear in 1/z
    // and piles up close to the near edge of the band. Feeding GL a fog
    // coordinate per vertex replaces its distance with that curve while leaving
    // the blend where it belongs -- after texturing, so a fogged surface washes
    // out rather than being tinted through its own texture.
    //
    // File scope because the draw path is four calls deep (drawMap ->
    // drawPrimitive -> emitTexturedVertex -> emitVertex) and immediate-mode GL
    // is already a single global state machine.
    struct VertexFogState
    {
      bool active = false;
      // Row of the modelview that yields eye-space depth, GL column-major.
      float depthRow[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      float inverseNear = 0.0f;
      float inverseSpan = 0.0f;
    };

    VertexFogState g_vertexFog;

    // Set for the duration of render(), the same way g_vertexFog is: the draw
    // helpers are free functions and lambdas well below the MapViewer instance,
    // and threading the block through every one of them buys nothing.
    const orphen::ported::render::SceneLighting *g_sceneLighting = nullptr;

    // Set only while a diagnostic run is collecting. When non-null the specular
    // loop runs and measures even if applyGleamPass is off, so the numbers can
    // be read without the pass touching the framebuffer.
    std::vector<orphen::harness::GleamProbe> *g_gleamProbes = nullptr;

    // amount = (1/near - 1/z) / (1/near - 1/far), clamped: 0 at the band's near
    // edge, 1 at the far edge. applyFogState sets GL_FOG_START 0 and
    // GL_FOG_END 1, so GL's linear ramp passes this straight through.
    // 0 at the near edge of the band, 1 at the far edge. VU1 0x01e1 derives the
    // entity's per-vertex alpha from the same curve with its own near/far pair
    // (fGpffffb70c / fGpffffb710), which in s01_e024 is the same 8..32 the fog
    // uses -- so `1 - fogAmountAt()` is that alpha, scaled to 0..1.
    float fogAmountAt(float x, float y, float z)
    {
      if (!g_vertexFog.active)
      {
        return 0.0f;
      }
      const float depth = g_vertexFog.depthRow[0] * x + g_vertexFog.depthRow[1] * y +
                          g_vertexFog.depthRow[2] * z + g_vertexFog.depthRow[3];
      float amount = 0.0f;
      if (depth > 0.0f)
      {
        amount = (g_vertexFog.inverseNear - 1.0f / depth) * g_vertexFog.inverseSpan;
      }
      return std::clamp(amount, 0.0f, 1.0f);
    }

    void emitFogCoord(float x, float y, float z)
    {
      if (!g_vertexFog.active)
      {
        return;
      }
      const float depth = g_vertexFog.depthRow[0] * x + g_vertexFog.depthRow[1] * y +
                          g_vertexFog.depthRow[2] * z + g_vertexFog.depthRow[3];
      // Behind the eye 1/z has the wrong sign, and the limit approaching the
      // eye from in front is 0 anyway, so clamp that side to unfogged. A
      // triangle straddling the near plane then interpolates from 0 rather
      // than jumping to full fog at the camera.
      float amount = 0.0f;
      if (depth > 0.0f)
      {
        amount = (g_vertexFog.inverseNear - 1.0f / depth) * g_vertexFog.inverseSpan;
      }
      orphen::port::gl::fogCoord(std::clamp(amount, 0.0f, 1.0f));
    }

    std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path &path)
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
      {
        throw std::runtime_error("failed to open PSM2 file: " + path.string());
      }

      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    orphen::ported::psm2::Vec3 toViewerSpace(const orphen::ported::psm2::Vec3 &position)
    {
      return {position.x, position.z, -position.y};
    }

    orphen::ported::psm2::Vec3 viewerSubtract(const orphen::ported::psm2::Vec3 &left, const orphen::ported::psm2::Vec3 &right)
    {
      return {left.x - right.x, left.y - right.y, left.z - right.z};
    }

    orphen::ported::psm2::Vec3 cross(const orphen::ported::psm2::Vec3 &left, const orphen::ported::psm2::Vec3 &right)
    {
      return {left.y * right.z - left.z * right.y,
              left.z * right.x - left.x * right.z,
              left.x * right.y - left.y * right.x};
    }

    orphen::ported::psm2::Vec3 normalize(const orphen::ported::psm2::Vec3 &value)
    {
      const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
      if (length <= std::numeric_limits<float>::epsilon())
      {
        return {};
      }
      return {value.x / length, value.y / length, value.z / length};
    }

    float viewerDistance(const orphen::ported::psm2::Vec3 &left, const orphen::ported::psm2::Vec3 &right)
    {
      const auto delta = viewerSubtract(left, right);
      return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    }

    struct ViewerGroundBasis
    {
      orphen::ported::psm2::Vec3 right{};
      orphen::ported::psm2::Vec3 forward{};
    };

    ViewerGroundBasis viewerGroundBasis(float yawDegrees)
    {
      const float yawRadians = yawDegrees * static_cast<float>(kPi / 180.0);
      return {{std::cos(yawRadians), 0.0f, -std::sin(yawRadians)},
              {std::sin(yawRadians), 0.0f, std::cos(yawRadians)}};
    }

    void setPerspective(int framebufferWidth, int framebufferHeight, float verticalFovDegrees, float farPlaneHint)
    {
      const double safeHeight = static_cast<double>(std::max(framebufferHeight, 1));
      const double aspect = static_cast<double>(std::max(framebufferWidth, 1)) / safeHeight;
      const double nearPlane = 0.1;
      const double farPlane = std::max(1000.0, static_cast<double>(farPlaneHint));
      const double verticalFovRadians = std::clamp(static_cast<double>(verticalFovDegrees), 30.0, 100.0) * kPi / 180.0;
      const double top = std::tan(verticalFovRadians * 0.5) * nearPlane;
      const double right = top * aspect;

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glFrustum(-right, right, -top, top, nearPlane, farPlane);
    }

    void applyCamera(const orphen::ported::psm2::Vec3 &target, float distance, float yawDegrees, float pitchDegrees)
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glTranslatef(0.0f, 0.0f, -distance);
      glRotatef(pitchDegrees, 1.0f, 0.0f, 0.0f);
      glRotatef(yawDegrees, 0.0f, 1.0f, 0.0f);
      glTranslatef(-target.x, -target.y, -target.z);
    }

    void applyLookAtCamera(const orphen::ported::psm2::Vec3 &eye, const orphen::ported::psm2::Vec3 &target)
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      orphen::ported::psm2::Vec3 forward = normalize(viewerSubtract(target, eye));
      if (forward.x == 0.0f && forward.y == 0.0f && forward.z == 0.0f)
      {
        forward = {0.0f, 0.0f, -1.0f};
      }

      const orphen::ported::psm2::Vec3 worldUp{0.0f, 1.0f, 0.0f};
      orphen::ported::psm2::Vec3 right = normalize(cross(forward, worldUp));
      if (right.x == 0.0f && right.y == 0.0f && right.z == 0.0f)
      {
        right = {1.0f, 0.0f, 0.0f};
      }
      const orphen::ported::psm2::Vec3 up = cross(right, forward);

      const GLfloat viewMatrix[16] = {
          right.x, up.x, -forward.x, 0.0f,
          right.y, up.y, -forward.y, 0.0f,
          right.z, up.z, -forward.z, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f};

      glMultMatrixf(viewMatrix);
      glTranslatef(-eye.x, -eye.y, -eye.z);
    }

    void drawGrid(float radius)
    {
      const int lineCount = 20;
      const float gridRadius = std::max(radius, 10.0f);
      const float step = gridRadius / static_cast<float>(lineCount / 2);

      glLineWidth(1.0f);
      glBegin(GL_LINES);

      glColor3f(0.16f, 0.18f, 0.18f);
      for (int lineIndex = -lineCount; lineIndex <= lineCount; ++lineIndex)
      {
        const float coordinate = static_cast<float>(lineIndex) * step;
        glVertex3f(coordinate, 0.0f, -gridRadius);
        glVertex3f(coordinate, 0.0f, gridRadius);
        glVertex3f(-gridRadius, 0.0f, coordinate);
        glVertex3f(gridRadius, 0.0f, coordinate);
      }

      glColor3f(0.75f, 0.18f, 0.16f);
      glVertex3f(-gridRadius, 0.0f, 0.0f);
      glVertex3f(gridRadius, 0.0f, 0.0f);

      glColor3f(0.16f, 0.56f, 0.82f);
      glVertex3f(0.0f, 0.0f, -gridRadius);
      glVertex3f(0.0f, 0.0f, gridRadius);

      glEnd();
    }

    orphen::ported::psm2::Vec3 glyphPoint(const orphen::ported::psm2::Vec3 &origin,
                                          const orphen::ported::psm2::Vec3 &horizontalAxis,
                                          const orphen::ported::psm2::Vec3 &verticalAxis,
                                          float horizontalScale,
                                          float verticalScale)
    {
      return {origin.x + horizontalAxis.x * horizontalScale + verticalAxis.x * verticalScale,
              origin.y + horizontalAxis.y * horizontalScale + verticalAxis.y * verticalScale,
              origin.z + horizontalAxis.z * horizontalScale + verticalAxis.z * verticalScale};
    }

    void emitStrokeSegment(const orphen::ported::psm2::Vec3 &origin,
                           const orphen::ported::psm2::Vec3 &horizontalAxis,
                           const orphen::ported::psm2::Vec3 &verticalAxis,
                           float startHorizontal,
                           float startVertical,
                           float endHorizontal,
                           float endVertical)
    {
      const auto start = glyphPoint(origin, horizontalAxis, verticalAxis, startHorizontal, startVertical);
      const auto end = glyphPoint(origin, horizontalAxis, verticalAxis, endHorizontal, endVertical);
      glVertex3f(start.x, start.y, start.z);
      glVertex3f(end.x, end.y, end.z);
    }

    void drawStrokeGlyph(char glyph,
                         const orphen::ported::psm2::Vec3 &origin,
                         const orphen::ported::psm2::Vec3 &horizontalAxis,
                         const orphen::ported::psm2::Vec3 &verticalAxis)
    {
      glBegin(GL_LINES);
      switch (glyph)
      {
      case 'X':
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, -1.0f, -1.0f, 1.0f, 1.0f);
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, -1.0f, 1.0f, 1.0f, -1.0f);
        break;
      case 'Y':
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, -1.0f, 1.0f, 0.0f, 0.0f);
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, 1.0f, 1.0f, 0.0f, 0.0f);
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, 0.0f, 0.0f, 0.0f, -1.0f);
        break;
      case 'Z':
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, -1.0f, 1.0f, 1.0f, 1.0f);
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, 1.0f, 1.0f, -1.0f, -1.0f);
        emitStrokeSegment(origin, horizontalAxis, verticalAxis, -1.0f, -1.0f, 1.0f, -1.0f);
        break;
      default:
        break;
      }
      glEnd();
    }

    void drawOriginAxisIndicator(float cameraDistance)
    {
      const float axisLength = std::clamp(cameraDistance * 0.18f, 0.5f, 8.0f);
      const float arrowSize = axisLength * 0.12f;
      const float labelOffset = axisLength + axisLength * 0.22f;
      const float labelSize = axisLength * 0.08f;

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glLineWidth(3.0f);

      glBegin(GL_LINES);

      // Game +X -> viewer +X.
      glColor3f(0.95f, 0.18f, 0.12f);
      glVertex3f(0.0f, 0.0f, 0.0f);
      glVertex3f(axisLength, 0.0f, 0.0f);
      glVertex3f(axisLength, 0.0f, 0.0f);
      glVertex3f(axisLength - arrowSize, arrowSize, 0.0f);
      glVertex3f(axisLength, 0.0f, 0.0f);
      glVertex3f(axisLength - arrowSize, -arrowSize, 0.0f);

      // Game +Y -> viewer -Z.
      glColor3f(0.12f, 0.42f, 1.0f);
      glVertex3f(0.0f, 0.0f, 0.0f);
      glVertex3f(0.0f, 0.0f, -axisLength);
      glVertex3f(0.0f, 0.0f, -axisLength);
      glVertex3f(arrowSize, 0.0f, -axisLength + arrowSize);
      glVertex3f(0.0f, 0.0f, -axisLength);
      glVertex3f(-arrowSize, 0.0f, -axisLength + arrowSize);

      // Game +Z -> viewer +Y (up).
      glColor3f(0.22f, 0.88f, 0.24f);
      glVertex3f(0.0f, 0.0f, 0.0f);
      glVertex3f(0.0f, axisLength, 0.0f);
      glVertex3f(0.0f, axisLength, 0.0f);
      glVertex3f(arrowSize, axisLength - arrowSize, 0.0f);
      glVertex3f(0.0f, axisLength, 0.0f);
      glVertex3f(-arrowSize, axisLength - arrowSize, 0.0f);

      glEnd();

      glColor3f(0.95f, 0.18f, 0.12f);
      drawStrokeGlyph('X', {labelOffset, 0.0f, 0.0f}, {0.0f, 0.0f, labelSize}, {0.0f, labelSize, 0.0f});
      glColor3f(0.12f, 0.42f, 1.0f);
      drawStrokeGlyph('Y', {0.0f, 0.0f, -labelOffset}, {labelSize, 0.0f, 0.0f}, {0.0f, labelSize, 0.0f});
      glColor3f(0.22f, 0.88f, 0.24f);
      drawStrokeGlyph('Z', {0.0f, labelOffset, 0.0f}, {labelSize, 0.0f, 0.0f}, {0.0f, 0.0f, -labelSize});

      glLineWidth(1.0f);
    }

    void drawLeadPlayer(const orphen::port::PlayerViewState &player)
    {
      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glLineWidth(3.0f);

      if (player.groundHit.has_value())
      {
        glColor3f(1.0f, 0.86f, 0.18f);
        glBegin(GL_LINE_LOOP);
        for (const auto &vertex : player.groundHit->vertices)
        {
          const auto viewerVertex = toViewerSpace(vertex);
          glVertex3f(viewerVertex.x, viewerVertex.y + 0.05f, viewerVertex.z);
        }
        glEnd();
      }

      const auto foot = toViewerSpace(player.position);
      // Matches OriginalEntity's radius54/height58 defaults: type id 1's
      // static descriptor (DAT_00318b68), the lead player's own type.
      const float bodyHeight = 0.8f;
      const float bodyHalfWidth = 0.15f;
      const float minX = foot.x - bodyHalfWidth;
      const float maxX = foot.x + bodyHalfWidth;
      const float minY = foot.y;
      const float maxY = foot.y + bodyHeight;
      const float minZ = foot.z - bodyHalfWidth;
      const float maxZ = foot.z + bodyHalfWidth;
      glColor3f(1.0f, 0.15f, 0.82f);
      glBegin(GL_LINES);
      glVertex3f(minX, minY, minZ);
      glVertex3f(maxX, minY, minZ);
      glVertex3f(maxX, minY, minZ);
      glVertex3f(maxX, minY, maxZ);
      glVertex3f(maxX, minY, maxZ);
      glVertex3f(minX, minY, maxZ);
      glVertex3f(minX, minY, maxZ);
      glVertex3f(minX, minY, minZ);
      glVertex3f(minX, maxY, minZ);
      glVertex3f(maxX, maxY, minZ);
      glVertex3f(maxX, maxY, minZ);
      glVertex3f(maxX, maxY, maxZ);
      glVertex3f(maxX, maxY, maxZ);
      glVertex3f(minX, maxY, maxZ);
      glVertex3f(minX, maxY, maxZ);
      glVertex3f(minX, maxY, minZ);
      glVertex3f(minX, minY, minZ);
      glVertex3f(minX, maxY, minZ);
      glVertex3f(maxX, minY, minZ);
      glVertex3f(maxX, maxY, minZ);
      glVertex3f(maxX, minY, maxZ);
      glVertex3f(maxX, maxY, maxZ);
      glVertex3f(minX, minY, maxZ);
      glVertex3f(minX, maxY, maxZ);

      const float facingLength = bodyHalfWidth * 2.4f;
      glVertex3f(foot.x, foot.y + bodyHeight * 0.45f, foot.z);
      glVertex3f(foot.x + std::cos(player.facingRadians) * facingLength,
                 foot.y + bodyHeight * 0.45f,
                 foot.z - std::sin(player.facingRadians) * facingLength);
      glEnd();

      glLineWidth(1.0f);
    }

    // Draws a short label at a world position, facing the camera. The basis
    // comes out of the current modelview matrix rather than being passed in, so
    // this works from either camera path without either of them knowing.
    std::string hexLabel(std::int32_t value)
    {
      static const char *digits = "0123456789ABCDEF";
      const std::uint32_t unsignedValue = static_cast<std::uint32_t>(value);
      std::string text;
      bool started = false;
      for (int shift = 28; shift >= 0; shift -= 4)
      {
        const std::uint32_t nibble = (unsignedValue >> shift) & 0xF;
        if (nibble != 0 || started || shift == 0)
        {
          text.push_back(digits[nibble]);
          started = true;
        }
      }
      return text;
    }

    void drawBillboardLabel(const std::string &text,
                            const orphen::ported::psm2::Vec3 &viewerAnchor,
                            float glyphHeight)
    {
      // Rows 0 and 1 of the modelview are the world directions that map to
      // view-space +x and +y. Which way those point on screen depends on the
      // projection, and the two camera paths disagree: the free viewer's
      // glFrustum is y-up, while the ported camera's view space is y-down and
      // its projection negates y to compensate (see glCameraFor). Taking the
      // sign from the projection's diagonal keeps this correct for both
      // instead of baking in one convention.
      float modelview[16] = {};
      float projection[16] = {};
      glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
      glGetFloatv(GL_PROJECTION_MATRIX, projection);

      const float horizontalSign = projection[0] < 0.0f ? -1.0f : 1.0f;
      const float verticalSign = projection[5] < 0.0f ? -1.0f : 1.0f;

      const orphen::ported::psm2::Vec3 right{modelview[0] * horizontalSign,
                                             modelview[4] * horizontalSign,
                                             modelview[8] * horizontalSign};
      const orphen::ported::psm2::Vec3 up{modelview[1] * verticalSign,
                                          modelview[5] * verticalSign,
                                          modelview[9] * verticalSign};

      const float glyphWidth = glyphHeight * 0.62f;
      const float advance = glyphWidth * 1.25f;
      const float totalWidth = advance * static_cast<float>(text.size());
      float penOffset = -totalWidth * 0.5f;

      glBegin(GL_LINES);
      for (char character : text)
      {
        const char upperCase = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        int segmentCount = 0;
        const StrokeSegment *segments = glyphStrokeSegments(upperCase, segmentCount);
        for (int index = 0; index < segmentCount; ++index)
        {
          const StrokeSegment &segment = segments[index];
          const float x0 = penOffset + segment.x0 * glyphWidth;
          const float x1 = penOffset + segment.x1 * glyphWidth;
          const float y0 = segment.y0 * glyphHeight;
          const float y1 = segment.y1 * glyphHeight;
          glVertex3f(viewerAnchor.x + right.x * x0 + up.x * y0,
                     viewerAnchor.y + right.y * x0 + up.y * y0,
                     viewerAnchor.z + right.z * x0 + up.z * y0);
          glVertex3f(viewerAnchor.x + right.x * x1 + up.x * y1,
                     viewerAnchor.y + right.y * x1 + up.y * y1,
                     viewerAnchor.z + right.z * x1 + up.z * y1);
        }
        penOffset += advance;
      }
      glEnd();
    }

    void emitBoxEdges(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
    {
      glVertex3f(minX, minY, minZ); glVertex3f(maxX, minY, minZ);
      glVertex3f(maxX, minY, minZ); glVertex3f(maxX, minY, maxZ);
      glVertex3f(maxX, minY, maxZ); glVertex3f(minX, minY, maxZ);
      glVertex3f(minX, minY, maxZ); glVertex3f(minX, minY, minZ);
      glVertex3f(minX, maxY, minZ); glVertex3f(maxX, maxY, minZ);
      glVertex3f(maxX, maxY, minZ); glVertex3f(maxX, maxY, maxZ);
      glVertex3f(maxX, maxY, maxZ); glVertex3f(minX, maxY, maxZ);
      glVertex3f(minX, maxY, maxZ); glVertex3f(minX, maxY, minZ);
      glVertex3f(minX, minY, minZ); glVertex3f(minX, maxY, minZ);
      glVertex3f(maxX, minY, minZ); glVertex3f(maxX, maxY, minZ);
      glVertex3f(maxX, minY, maxZ); glVertex3f(maxX, maxY, maxZ);
      glVertex3f(minX, minY, maxZ); glVertex3f(minX, maxY, maxZ);
    }

    // Script-spawned entities, drawn as pink boxes at the collision size their
    // type descriptor gives. An entity whose descriptor could not be resolved --
    // ids from 0x272 up ship with the map, not the executable -- is drawn in a
    // duller shade at a default size and labelled so, rather than pretending to
    // a size nobody knows.
    // The entity's root world matrix, standing in for FUN_0020cdc0 (which has
    // not been analysed). Built from the fields the debug box already proves
    // correct: position +0x20/+0x24/+0x28, facing +0x5C about the world's
    // vertical axis, and scale +0x14C.
    orphen::ported::model::Matrix4 entityRootMatrix(const orphen::port::SceneObjectView &object)
    {
      return orphen::ported::model::FUN_0020cdc0_entity_root(
          {object.position.x, object.position.y, object.position.z}, object.facingRadians,
          object.rotationX154, object.rotationY158, object.scale, object.scaleZ150);
    }

    // Draws one model's primitives. Vertices are transformed on the CPU by their
    // own bone's matrix -- one rigid bone per vertex, which is what the vertex
    // bone table at PSC3 header +0x18 selects and what FUN_0020eec0's palette
    // upload implies -- then emitted in viewer space, so the camera fold in
    // glCameraFor keeps working unchanged.
    //
    // This is the rasterising half of FUN_00212058 / FUN_002129b8. The GIF
    // command buffer those build is not reproduced; the primitive walk, the
    // triangle-vs-quad test, the per-corner UVs and the colour lookup are.
    void drawObjectModel(const orphen::port::SceneObjectView &object,
                         const std::vector<unsigned int> &slotTextures)
    {
      const auto &model = *object.model;
      if (model.submeshes.empty() || model.primitives.empty())
      {
        return;
      }

      // Built during the simulation step, in PortRuntime::attachModel, because
      // FUN_0020d188's filter is stateful across frames. Anything published
      // without one is a model the pose walk could not reach.
      const std::vector<orphen::ported::model::Matrix4> &palette = object.bonePalette;
      if (palette.empty())
      {
        return;
      }

      // **Models are drawn double-sided.** The backface culling set up for the
      // map does not belong to this path and produces holes here: it was
      // derived from FUN_0022c6e8's corner order and the map VU1 program at
      // 0xE0, and neither says anything about FUN_00212058's PSC3 path, whose
      // VU1 program is not in the decompilation. The GS has no culling hardware
      // to fall back on.
      //
      // The assets settle it. Every party model ships with a handful of
      // primitives whose winding opposes their own stored normal -- 4 in
      // grp_0001, 12 in grp_0003, 3 in grp_0006, 16 in grp_000a, out of ~700
      // testable each. Culling turns each one into a hole that is visible from
      // the front and absent from behind, which is exactly how this was
      // reported. An asset that ships that way was never being culled.
      const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
      glDisable(GL_CULL_FACE);

      // **Discard fully transparent texels before the depth write.** Blending
      // alone makes them invisible but they still occlude: a cutout texel wrote
      // depth, so anything drawn behind it afterwards was rejected. That is why
      // grp_0003's hair, which drapes over the thigh and has transparent
      // regions, took the jeans behind it out along with itself.
      //
      // The GS does this with its own alpha test rather than by not writing
      // depth, but the visible result is the same and GL_ALPHA_TEST is the
      // fixed-function equivalent. Only exactly-zero alpha is discarded, so
      // nothing that was visible before can disappear.
      glEnable(GL_ALPHA_TEST);
      glAlphaFunc(GL_GREATER, 0.0f);

      const unsigned int texture =
          (object.textureSlot >= 0 &&
           static_cast<std::size_t>(object.textureSlot) < slotTextures.size())
              ? slotTextures[static_cast<std::size_t>(object.textureSlot)]
              : 0u;
      if (texture != 0)
      {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
      }
      else
      {
        glDisable(GL_TEXTURE_2D);
      }

      // Shared with the click probe, deliberately: a probe that computed vertex
      // positions its own way could agree with itself and still not describe
      // what was drawn.
      const auto posed = [&](std::uint16_t vertexIndex) {
        return orphen::harness::posedViewerVertex(model, palette, vertexIndex);
      };

      const bool multiPass =
          g_sceneLighting != nullptr && g_sceneLighting->applySubdrawPasses;

      // The blend state a subdraw's mode nibble selects, from the GS register
      // blocks at VU1 memory 608 + mode*3 (see SceneLighting). State changes are
      // illegal between glBegin/glEnd, so switching mode closes the current
      // batch and opens a new one; passes are emitted in the original's order,
      // so a run of same-mode passes still costs one batch.
      // Blend and depth state are restored on the way out rather than reset to
      // assumed values: the map path draws after this one and has its own
      // blending, and hardcoding a "default" here turned its alpha-blended
      // geometry -- the hanging chains -- opaque.
      glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      int activeBlendMode = -1;
      const auto setBlendMode = [&activeBlendMode](int mode) {
        if (mode == activeBlendMode)
        {
          return;
        }
        if (activeBlendMode >= 0)
        {
          glEnd();
        }
        activeBlendMode = mode;
        switch (mode)
        {
        case 2:
          // ALPHA 0x48, (Cs - 0) * As + Cd. ZMSK is set on this block, so it
          // tests depth without writing, and LEQUAL because an overlay pass sits
          // at exactly the depth the base pass just wrote.
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE);
          glDepthMask(GL_FALSE);
          glDepthFunc(GL_LEQUAL);
          break;
        case 1:
        case 3:
          // Mode 1 is ALPHA 0x44 with ZMSK set. Mode 3 is ALPHA 0xa1,
          // (Cd - Cs) * 128 >> 7, a reverse subtract that needs
          // glBlendEquation -- not in the fixed-function entry points this
          // harness links, and unused by every model in s01_e024. It falls back
          // to straight alpha so it shows up as wrong rather than as missing.
          glEnable(GL_BLEND);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glDepthMask(GL_FALSE);
          glDepthFunc(GL_LEQUAL);
          break;
        case 0:
        default:
          glDisable(GL_BLEND);
          glDepthMask(GL_TRUE);
          glDepthFunc(GL_LESS);
          break;
        }
        glBegin(GL_TRIANGLES);
      };

      for (const auto &primitive : model.primitives)
      {
        if (primitive.skipped())
        {
          continue;
        }
        const std::size_t corners = primitive.cornerCount();
        bool inRange = true;
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          inRange = inRange && primitive.vertexIndices[corner] < model.vertices.size();
        }
        if (!inRange)
        {
          continue;
        }

        // FUN_00212058 draws one pass per active subdraw index, in order, each
        // with the blend mode its texFlags select. With --lighting-passes off
        // the port draws only the first, which is what it did before per-pass
        // blend state existed: pass 0 is the opaque base and the later ones are
        // overlays, so taking the first is the safe single-pass choice.
        //
        // grp_0006 prim 110 is why "first" and not "last": passes [106, 107],
        // where 106 is texFlags 0x0000 (mode 0) and 107 is 0x8019 (mode 2,
        // alpha 25 of 127). Drawing only the overlay sampled a patch of the
        // sheet it was never meant to, which read on screen as a hole.
        std::array<orphen::ported::psm2::Vec3, 4> points{};
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          points[corner] = posed(primitive.vertexIndices[corner]);
        }

        const auto emit = [&](std::size_t corner,
                              const orphen::ported::model::Psc3Subdraw *subdraw,
                              float passAlpha) {
          if (subdraw != nullptr)
          {
            const std::uint16_t packed = subdraw->packedUv[corner];
            // (V << 8) | U -- low byte is U, high byte is V, 8-bit texel
            // coordinates over a 256x256 page, no V flip.
            //
            // The decompiled code cannot settle the byte order: FUN_002129b8
            // copies the halfword into the VIF packet verbatim and the split
            // happens in the VU1 microprogram, which is not in the
            // decompilation. This order is the one
            // tools/resource_extract/v2/psc3_gltf.py uses, which was arrived at
            // by looking at textured exports rather than by reading code.
            glTexCoord2f(static_cast<float>(packed & 0xFF) / 256.0f,
                         static_cast<float>((packed >> 8) & 0xFF) / 256.0f);
          }
          // Flat-shaded primitives hold one colour at colourIndex; only the
          // per-vertex ones have a colour per corner. Same for the normal:
          // +0x06 per vertex under flag 0x8, +0x16 for the whole primitive
          // otherwise.
          const std::size_t colourEntry =
              primitive.perVertexColour() ? primitive.colourIndex + corner : primitive.colourIndex;

          float light[3] = {1.0f, 1.0f, 1.0f};
          // Draw header byte 15, FUN_00212058:229: primitive flag bit 8 makes
          // VU1 0x01ba branch past the whole lighting block, so vf17 keeps the
          // raw LQI'd vertex colour and the authored value reaches the GS
          // untouched.
          //
          // grp_0172 is the case that shows why this matters. The chest lid is
          // 72 primitives -- the only bit-8 set in the model -- and all 72 share
          // colourIndex 21, whose four corners are (0,0,0), (0,0,0),
          // (128,128,128), (128,128,128): a ramp to full brightness, not a flat
          // colour. Lit, the bright corners get multiplied by the modulator
          // (about 0.54, 0.77, 1.10 for an up-facing normal) and come out dimmer
          // and bluer; unlit they stay at 1.0 and keep the texture's warm red.
          // That is exactly the difference between the port and the real frame.
          const bool unlit =
              g_sceneLighting != nullptr && g_sceneLighting->applyUnlitFlag &&
              (primitive.flags & orphen::ported::model::kPrimitiveUnlit) != 0;
          if (g_sceneLighting != nullptr && g_sceneLighting->active && !unlit)
          {
            const std::uint16_t normalIndex =
                primitive.perVertexColour()
                    ? model.vertices[primitive.vertexIndices[corner]].normalIndex
                    : primitive.flatNormalIndex;
            if (normalIndex < model.normals.size())
            {
              const std::uint16_t bone =
                  model.vertices[primitive.vertexIndices[corner]].boneIndex;
              // Header byte 14, FUN_00212058:228: the complement of the
              // primitive's +0x0D, which VU1 scales by 1/320 into vf15.z.
              g_sceneLighting->modulator(
                  orphen::harness::posedWorldNormal(model, palette, bone,
                                                    model.normals[normalIndex]),
                  g_sceneLighting->applyLightFloor
                      ? orphen::ported::render::SceneLighting::floorFromSourceByte(
                            primitive.alphaByte)
                      : 0.0f,
                  light);
            }
          }

          if (colourEntry * 3 + 2 < model.colours.size())
          {
            // The game's colour bytes run to 0x80 for full brightness, the same
            // convention the map path divides by 128 for.
            glColor4f(std::min(1.0f, model.colours[colourEntry * 3 + 0] / 128.0f * light[0]),
                      std::min(1.0f, model.colours[colourEntry * 3 + 1] / 128.0f * light[1]),
                      std::min(1.0f, model.colours[colourEntry * 3 + 2] / 128.0f * light[2]),
                      passAlpha);
          }
          else
          {
            glColor4f(std::min(1.0f, light[0]), std::min(1.0f, light[1]),
                      std::min(1.0f, light[2]), passAlpha);
          }
          emitFogCoord(points[corner].x, points[corner].y, points[corner].z);
          glVertex3f(points[corner].x, points[corner].y, points[corner].z);
        };

        for (std::size_t pass = 0; pass < 4; ++pass)
        {
          const std::int16_t index = primitive.subdrawIndices[pass];
          if (index < 0)
          {
            continue;
          }
          const orphen::ported::model::Psc3Subdraw *subdraw =
              static_cast<std::size_t>(index) < model.subdraws.size()
                  ? &model.subdraws[static_cast<std::size_t>(index)]
                  : nullptr;

          // FUN_00212058:137-141. Mode 0 never enables ABE, so its alpha field
          // is not read at all; otherwise 0x7F is the sentinel for 0x80, fully
          // opaque, and anything else is the value straight out of the low
          // seven bits.
          int mode = 0;
          float passAlpha = 1.0f;
          if (multiPass && subdraw != nullptr)
          {
            mode = static_cast<int>(subdraw->blendMode());
            if (mode != 0)
            {
              const std::uint16_t raw = subdraw->alpha();
              passAlpha = raw == 0x7F ? 1.0f : static_cast<float>(raw) / 128.0f;
              // A fully opaque alpha blend is pointless, so the original folds
              // it straight back to the opaque mode -- and only for mode 1, not
              // for the additive one, where full alpha still means something.
              // grp_0172 is 60 passes of exactly this, and blending them is what
              // made every ordinary chest translucent.
              if (raw == 0x7F && mode == 1)
              {
                mode = 0;
              }
            }
          }
          setBlendMode(mode);

          emit(0, subdraw, passAlpha);
          emit(1, subdraw, passAlpha);
          emit(2, subdraw, passAlpha);
          if (corners == 4)
          {
            emit(0, subdraw, passAlpha);
            emit(2, subdraw, passAlpha);
            emit(3, subdraw, passAlpha);
          }

          if (!multiPass)
          {
            break;
          }
        }
      }
      if (activeBlendMode >= 0)
      {
        glEnd();
      }
      glPopAttrib();

      // The specular pass, VU1 0x0200. FUN_00212058 appends a second GIF packet
      // -- untextured, gouraud, ABE on, additive, depth-tested but not
      // depth-written -- to any primitive whose +0x0C is non-zero, drawn in the
      // scene's light-0 colour. See SceneLighting's gleam block.
      const bool gleamDraw = g_sceneLighting != nullptr &&
                             g_sceneLighting->gleamActive &&
                             g_sceneLighting->applyGleamPass;
      if (g_sceneLighting != nullptr && g_sceneLighting->gleamActive &&
          (gleamDraw || g_gleamProbes != nullptr))
      {
        GleamProbe probe;
        probe.slot = object.slot;
        probe.typeId = object.typeId;
        probe.minNormalLength = 1e9f;

        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT |
                     GL_TEXTURE_BIT);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        // Depth-test but do not write, matching ZBUF.ZMSK on the GS state this
        // pass selects. GL_LEQUAL, not the default GL_LESS: this is the same
        // geometry at the same depth the main pass just wrote, so under LESS
        // every fragment fails and the highlight never appears. The GS has no
        // such problem -- its depth test is GEQUAL against a reversed Z, which
        // passes on equality.
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);

        if (gleamDraw)
        {
          glBegin(GL_TRIANGLES);
        }
        for (const auto &primitive : model.primitives)
        {
          // FUN_00212058:84-89 zeroes the source byte when flag bit 8 is set,
          // so a bit-8 primitive never carries the pass either. Redundant with
          // the fogByte test on every model seen so far, kept because the two
          // come from different lines of the original.
          if (primitive.skipped() || primitive.fogByte == 0 ||
              (primitive.flags & orphen::ported::model::kPrimitiveUnlit) != 0)
          {
            continue;
          }
          const std::size_t corners = primitive.cornerCount();
          bool inRange = true;
          for (std::size_t corner = 0; corner < corners; ++corner)
          {
            inRange = inRange && primitive.vertexIndices[corner] < model.vertices.size();
          }
          if (!inRange)
          {
            continue;
          }
          ++probe.primitivesTested;

          std::array<orphen::ported::psm2::Vec3, 4> points{};
          for (std::size_t corner = 0; corner < corners; ++corner)
          {
            points[corner] = posed(primitive.vertexIndices[corner]);
          }

          const auto emitGleam = [&](std::size_t corner) {
            const std::uint16_t normalIndex =
                primitive.perVertexColour()
                    ? model.vertices[primitive.vertexIndices[corner]].normalIndex
                    : primitive.flatNormalIndex;
            float opacity = 0.0f;
            ++probe.cornersEvaluated;
            if (normalIndex < model.normals.size())
            {
              const std::uint16_t bone =
                  model.vertices[primitive.vertexIndices[corner]].boneIndex;
              const orphen::ported::psm2::Vec3 normal = orphen::harness::posedWorldNormal(
                  model, palette, bone, model.normals[normalIndex]);
              const float normalLength = std::sqrt(
                  normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
              probe.minNormalLength = std::min(probe.minNormalLength, normalLength);
              probe.maxNormalLength = std::max(probe.maxNormalLength, normalLength);
              const orphen::ported::psm2::Vec3 &half = g_sceneLighting->gleamDirection;
              const float dotNH =
                  normal.x * half.x + normal.y * half.y + normal.z * half.z;
              probe.maxDot = std::max(probe.maxDot, dotNH);
              // * vertexAlpha * 0.5 in GS units is * vertexAlpha / 256 once the
              // 128-is-one convention is divided out, and vertexAlpha is
              // 255 * (1 - fog).
              opacity = orphen::ported::render::SceneLighting::gleamOpacity(
                            dotNH, primitive.alphaByte, primitive.fogByte) *
                        (1.0f - fogAmountAt(points[corner].x, points[corner].y,
                                            points[corner].z)) *
                        (255.0f / 256.0f);
              probe.maxOpacity = std::max(probe.maxOpacity, opacity);
              if (opacity > 0.0f)
              {
                ++probe.cornersLit;
              }
            }
            if (!gleamDraw)
            {
              return;
            }
            glColor4f(std::min(1.0f, g_sceneLighting->gleamColour[0] / 128.0f),
                      std::min(1.0f, g_sceneLighting->gleamColour[1] / 128.0f),
                      std::min(1.0f, g_sceneLighting->gleamColour[2] / 128.0f),
                      std::clamp(opacity, 0.0f, 1.0f));
            glVertex3f(points[corner].x, points[corner].y, points[corner].z);
          };

          emitGleam(0);
          emitGleam(1);
          emitGleam(2);
          if (corners == 4)
          {
            emitGleam(0);
            emitGleam(2);
            emitGleam(3);
          }
        }
        if (gleamDraw)
        {
          glEnd();
        }

        // GL_DEPTH_BUFFER_BIT in the push covers the mask and the func, but the
        // explicit restore keeps the pass readable next to its setup.
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glPopAttrib();

        if (g_gleamProbes != nullptr)
        {
          if (probe.minNormalLength > 1e8f)
          {
            probe.minNormalLength = 0.0f;
          }
          g_gleamProbes->push_back(probe);
        }
      }

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glDisable(GL_ALPHA_TEST);
      if (cullWasEnabled)
      {
        glEnable(GL_CULL_FACE);
      }
    }

    void drawObjectModels(const orphen::port::SceneObjectViewList &objects,
                          const std::vector<unsigned int> &slotTextures)
    {
      for (const auto &object : objects)
      {
        if (object.model != nullptr)
        {
          drawObjectModel(object, slotTextures);
        }
      }
    }

    void drawSceneObjects(const orphen::port::SceneObjectViewList &objects)
    {
      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glLineWidth(2.0f);

      for (const auto &object : objects)
      {
        if (!object.drawDebugBox)
        {
          continue;
        }
        const auto foot = toViewerSpace(object.position);
        const float halfWidth = object.descriptorResolved ? std::max(object.radius, 0.05f) : 0.25f;
        const float height = object.descriptorResolved ? std::max(object.height, 0.1f) : 0.6f;

        if (object.descriptorResolved)
        {
          glColor3f(1.0f, 0.35f, 0.72f);
        }
        else
        {
          glColor3f(0.62f, 0.28f, 0.5f);
        }

        glBegin(GL_LINES);
        emitBoxEdges(foot.x - halfWidth, foot.y, foot.z - halfWidth,
                     foot.x + halfWidth, foot.y + height, foot.z + halfWidth);

        const float facingLength = halfWidth * 2.0f;
        glVertex3f(foot.x, foot.y + height * 0.5f, foot.z);
        glVertex3f(foot.x + std::cos(object.facingRadians) * facingLength,
                   foot.y + height * 0.5f,
                   foot.z - std::sin(object.facingRadians) * facingLength);
        glEnd();

        std::string label = "#" + std::to_string(object.slot) + " T" + hexLabel(object.typeId);
        if (object.descriptorResolved)
        {
          label += " M" + std::to_string(object.modelIndex);
        }
        else
        {
          label += " ?";
        }

        glColor3f(1.0f, 0.72f, 0.9f);
        drawBillboardLabel(label, {foot.x, foot.y + height + 0.14f, foot.z}, 0.11f);
      }

      glLineWidth(1.0f);
    }

    void emitVertex(const orphen::ported::psm2::Psm2RuntimeState &map, std::uint16_t vertexIndex)
    {
      const auto &source = map.DAT_0035569c_sectionCRecords.at(vertexIndex).position;
      const auto viewerPosition = toViewerSpace(source);
      emitFogCoord(viewerPosition.x, viewerPosition.y, viewerPosition.z);
      glVertex3f(viewerPosition.x, viewerPosition.y, viewerPosition.z);
    }

    // Material slot 0 is the base pass. FUN_0022c3d8 has already resolved the
    // selector, so the type byte here is the texture page and a negative type
    // means the primitive is untextured rather than missing.
    const orphen::ported::psm2::MaterialSlot *baseSlotForPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                                   std::size_t primitiveIndex)
    {
      if (primitiveIndex >= map.DAT_003556ac_dRecords80.size())
      {
        return nullptr;
      }
      return &map.DAT_003556ac_dRecords80[primitiveIndex].materialSlots[0];
    }

    std::optional<std::size_t> texturePageForPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map, std::size_t primitiveIndex)
    {
      const auto *slot = baseSlotForPrimitive(map, primitiveIndex);
      if (slot == nullptr || !slot->textured())
      {
        return std::nullopt;
      }

      return slot->type;
    }

    std::pair<float, float> textureCoordinateForCorner(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                       const orphen::ported::psm2::TriangleRecord &triangle,
                                                       std::size_t triangleCornerIndex)
    {
      const auto *slot = baseSlotForPrimitive(map, triangle.primitiveIndex);
      if (slot == nullptr || !slot->textured())
      {
        return {0.0f, 0.0f};
      }

      const std::uint8_t sourceCorner = triangle.cornerIndices[triangleCornerIndex] & 3;
      return {static_cast<float>(slot->textureCoordinates[sourceCorner * 2]) / 256.0f,
              static_cast<float>(slot->textureCoordinates[sourceCorner * 2 + 1]) / 256.0f};
    }

    // The GS treats 0x80 as fully opaque, so the fade byte divides by 128. A
    // fade of 0 means the primitive is not fading at all -- see
    // FUN_00209140's ceiling branch, which emits 0 rather than the byte.
    float alphaForFade(std::uint8_t fade)
    {
      if (fade == 0)
      {
        return 1.0f;
      }
      return std::min(1.0f, static_cast<float>(fade) / 128.0f);
    }

    void emitTexturedVertex(const orphen::ported::psm2::Psm2RuntimeState &map,
                            const orphen::ported::psm2::TriangleRecord &triangle,
                            std::size_t triangleCornerIndex,
                            float alpha,
                            bool modulateByFlatColour)
    {
      const auto &record80 = map.DAT_003556ac_dRecords80[triangle.primitiveIndex];
      const std::uint8_t corner = triangle.cornerIndices[triangleCornerIndex] & 3;

      // FUN_00211230:266-289. Textured primitives take the vertex colour
      // straight; untextured ones modulate it against the slot's flat colour
      // with a >> 6, which is why the flat colours look like 0x40 mid greys.
      std::uint32_t colour = record80.vertexColours[corner];
      float red = static_cast<float>(colour & 0xff) / 128.0f;
      float green = static_cast<float>((colour >> 8) & 0xff) / 128.0f;
      float blue = static_cast<float>((colour >> 16) & 0xff) / 128.0f;

      // VU1 0x01b2..0x01e0. The map path is unskinned -- the microprogram's
      // per-vertex bone rotation is gated on header byte 10, which
      // FUN_00211230:191 writes as a constant zero -- so the face normal goes
      // straight into the dot products. Byte 14 is the complement of +0x2D and
      // becomes the MAXz floor.
      //
      // Byte 15 (flags bit 13) skips the whole block, the same way flag bit 8
      // does on the entity path. No map primitive in s01_e024 sets it.
      const bool unlit = g_sceneLighting != nullptr &&
                         g_sceneLighting->applyUnlitFlag &&
                         (record80.primitiveFlags & kRecord80UnlitBit) != 0;
      if (g_sceneLighting != nullptr && g_sceneLighting->active && !unlit)
      {
        float light[3];
        g_sceneLighting->modulator(
            record80.normal,
            g_sceneLighting->applyLightFloor
                ? orphen::ported::render::SceneLighting::floorFromSourceByte(
                      record80.staticAlpha)
                : 0.0f,
            light);
        red *= light[0];
        green *= light[1];
        blue *= light[2];
      }

      if (modulateByFlatColour)
      {
        const std::uint32_t flat = record80.materialSlots[0].flatColour();
        red *= static_cast<float>(flat & 0xff) / 64.0f;
        green *= static_cast<float>((flat >> 8) & 0xff) / 64.0f;
        blue *= static_cast<float>((flat >> 16) & 0xff) / 64.0f;
      }

      glColor4f(std::min(red, 1.0f), std::min(green, 1.0f), std::min(blue, 1.0f), alpha);

      const auto [u, v] = textureCoordinateForCorner(map, triangle, triangleCornerIndex);
      glTexCoord2f(u, v);
      emitVertex(map, triangle.vertexIndices[triangleCornerIndex]);
    }

    void drawPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                       const std::vector<unsigned int> &textureIds,
                       std::size_t primitiveIndex,
                       float alpha,
                       bool cullingEnabled)
    {
      const std::optional<std::size_t> texturePage = texturePageForPrimitive(map, primitiveIndex);
      const bool hasTexture = texturePage.has_value() && *texturePage < textureIds.size() && textureIds[*texturePage] != 0;

      if (hasTexture)
      {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, textureIds[*texturePage]);
      }
      else
      {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
      }

      const bool modulate = !hasTexture;
      const auto &record80 = map.DAT_003556ac_dRecords80[primitiveIndex];

      const bool twoSided = (record80.primitiveFlags & kRecord80TwoSidedBit) != 0;
      if (cullingEnabled && twoSided)
      {
        glDisable(GL_CULL_FACE);
      }

      glBegin(GL_TRIANGLES);
      for (std::size_t offset = 0; offset < record80.triangleCount; ++offset)
      {
        const auto &triangle = map.derivedTriangles[record80.firstTriangle + offset];
        emitTexturedVertex(map, triangle, 0, alpha, modulate);
        emitTexturedVertex(map, triangle, 1, alpha, modulate);
        emitTexturedVertex(map, triangle, 2, alpha, modulate);
      }
      glEnd();

      if (cullingEnabled && twoSided)
      {
        glEnable(GL_CULL_FACE);
      }
    }

    // Back to front over the depth buckets the visibility pass produced.
    // Both lists are ordered far to near by bucket, so this walks them
    // together the way the original walks its one shared bucket table: an
    // entity is drawn at its depth among the map's primitives, not after them.
    void drawMap(const orphen::ported::psm2::Psm2RuntimeState &map,
                 const std::vector<unsigned int> &textureIds,
                 const std::vector<orphen::ported::render::MapDrawItem> &drawList,
                 bool cullingEnabled,
                 const orphen::port::SceneObjectViewList &objects,
                 const std::vector<orphen::ported::render::EntityDrawItem> &entityDrawList,
                 const std::vector<unsigned int> &slotTextures)
    {
      std::size_t entityCursor = 0;
      const auto drawEntitiesUpTo = [&](int bucket) {
        while (entityCursor < entityDrawList.size() &&
               entityDrawList[entityCursor].depthBucket <= bucket)
        {
          drawObjectModel(objects[entityDrawList[entityCursor].viewIndex], slotTextures);
          ++entityCursor;
        }
      };

      for (const auto &item : drawList)
      {
        drawEntitiesUpTo(item.depthBucket);
        drawPrimitive(map, textureIds, item.primitiveIndex, alphaForFade(item.fade), cullingEnabled);
      }
      // Anything nearer than the last map primitive, plus the blended bucket.
      drawEntitiesUpTo(orphen::ported::render::entityDraw::kBlendedBucket);

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    // The whole map, unsorted and opaque. Used by the free-fly viewer, which
    // has no player and so no occlusion fade to compute.
    void drawMapUnsorted(const orphen::ported::psm2::Psm2RuntimeState &map, const std::vector<unsigned int> &textureIds)
    {
      for (std::size_t primitiveIndex = 0; primitiveIndex < map.DAT_003556ac_dRecords80.size(); ++primitiveIndex)
      {
        if ((map.DAT_003556ac_dRecords80[primitiveIndex].primitiveFlags & kRecord80HiddenBit) != 0)
        {
          continue;
        }
        drawPrimitive(map, textureIds, primitiveIndex, 1.0f, false);
      }

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    orphen::ported::psm2::Vec3 boundsCenter(const orphen::ported::psm2::Bounds3 &bounds)
    {
      return {(bounds.min.x + bounds.max.x) * 0.5f,
              (bounds.min.y + bounds.max.y) * 0.5f,
              (bounds.min.z + bounds.max.z) * 0.5f};
    }

    float boundsRadius(const orphen::ported::psm2::Bounds3 &bounds)
    {
      const float spanX = bounds.max.x - bounds.min.x;
      const float spanY = bounds.max.y - bounds.min.y;
      const float spanZ = bounds.max.z - bounds.min.z;
      return std::max({spanX, spanY, spanZ}) * 0.75f + 5.0f;
    }

    bool sameScene(McbSceneSelection left, McbSceneSelection right)
    {
      return left.section == right.section && left.entry == right.entry;
    }

    void printLoadedStats(const std::string &source, const orphen::ported::psm2::Psm2Stats &stats, std::size_t texturePageCount)
    {
      std::cout << "[psm2] loaded " << source
                << " positions=" << stats.positionRecordCount
                << " sectionB=" << stats.sectionBRecordCount
                << " primitives=" << stats.primitiveRecordCount
                << " triangles=" << stats.triangleCount
                << " skipped=" << stats.skippedPrimitiveCount
                << " textures=" << texturePageCount << '\n';
    }

  } // namespace

  MapViewer::~MapViewer()
  {
    releaseUploadedTextures();
  }

  void MapViewer::loadDecodedPsm2(const std::filesystem::path &path)
  {
    const std::vector<std::uint8_t> bytes = readBinaryFile(path);
    map_ = orphen::ported::psm2::loadDecodedPsm2(bytes);
    loadedSourceDescription_ = path.string();
    discRoot_.clear();
    discScenes_.clear();
    currentDiscSceneIndex_ = 0;
    sceneResources_.reset();
    setTexturePages({});
    ++loadedMapGeneration_;
    resetCamera();
  }

  void MapViewer::loadDiscSceneMap(const std::filesystem::path &discRoot, McbSceneSelection selection)
  {
    discRoot_ = discRoot;
    discScenes_ = listPopulatedMcbScenes(discRoot_);

    const auto selectedScene = std::find_if(discScenes_.begin(), discScenes_.end(), [selection](McbSceneSelection scene)
                                            { return sameScene(scene, selection); });
    if (selectedScene == discScenes_.end())
    {
      throw std::runtime_error("selected MCB scene slot is empty: " + sceneName(selection));
    }

    loadDiscSceneAtIndex(static_cast<std::size_t>(selectedScene - discScenes_.begin()));
  }

  bool MapViewer::cycleDiscScene(int direction)
  {
    if (discRoot_.empty() || discScenes_.empty() || direction == 0)
    {
      return false;
    }

    const std::size_t sceneCount = discScenes_.size();
    std::size_t nextSceneIndex = currentDiscSceneIndex_;

    for (std::size_t attempt = 0; attempt < sceneCount; ++attempt)
    {
      nextSceneIndex = direction > 0
                           ? (nextSceneIndex + 1) % sceneCount
                           : (nextSceneIndex + sceneCount - 1) % sceneCount;
      try
      {
        loadDiscSceneAtIndex(nextSceneIndex);
        if (map_.has_value())
        {
          printLoadedStats(loadedSourceDescription_, map_->stats, texturePages_.size());
        }
        return true;
      }
      catch (const std::exception &error)
      {
        std::cerr << "[psm2] skipping " << sceneName(discScenes_[nextSceneIndex]) << ": " << error.what() << '\n';
      }
    }

    return false;
  }

  void MapViewer::loadDiscSceneAtIndex(std::size_t sceneIndex)
  {
    if (sceneIndex >= discScenes_.size())
    {
      throw std::out_of_range("disc scene index outside loaded scene list");
    }

    const McbSceneSelection selection = discScenes_[sceneIndex];
    sceneResources_ = SceneResourceProvider::loadFromDisc(discRoot_, selection);
    LoadedDiscMap loadedMap = loadFirstPsm2FromSceneResources(*sceneResources_);
    orphen::ported::psm2::Psm2RuntimeState loadedPsm2 = orphen::ported::psm2::loadDecodedPsm2(loadedMap.decodedPsm2);
    setTexturePages(std::move(loadedMap.texturePages));
    map_ = std::move(loadedPsm2);
    loadedSourceDescription_ = sceneName(selection) + " map_" + loadedMap.resourceIdHex;
    currentDiscSceneIndex_ = sceneIndex;
    ++loadedMapGeneration_;
    resetCamera();
  }

  void MapViewer::setSceneObjectViews(orphen::port::SceneObjectViewList objects)
  {
    sceneObjectViews_ = std::move(objects);
  }

  void MapViewer::setLeadPlayerView(std::optional<orphen::port::PlayerViewState> playerView)
  {
    if (!playerView.has_value())
    {
      leadPlayerView_.reset();
      return;
    }

    leadPlayerView_ = std::move(playerView);
    cameraTarget_ = toViewerSpace(leadPlayerView_->position);
  }

  void MapViewer::setFollowCameraPose(const orphen::ported::camera::CameraPose &pose)
  {
    followCameraPose_ = pose;
  }

  void MapViewer::setRenderCamera(const orphen::ported::render::ViewProjection &viewProjection)
  {
    renderCamera_ = viewProjection;
  }

  void MapViewer::setMapDrawList(std::vector<orphen::ported::render::MapDrawItem> drawList)
  {
    mapDrawList_ = std::move(drawList);
  }

  void MapViewer::setDrawDistance(float drawDistance)
  {
    drawDistance_ = drawDistance;
  }

  void MapViewer::setGleamDirection(float yawRadians, float pitchRadians)
  {
    // FUN_00216aa0:436-449 builds DAT_0058bea0 as normalise(lookAtTarget - eye)
    // and derives fGpffffb6d4 / fGpffffb6d8 from that same vector, so the
    // angles reconstruct it -- checked against s01_e24.bin, which holds
    // yaw 0, pitch -0.165120 and DAT_0058bea0 (0.986399, 0, -0.164370), and
    // cos/sin of those angles reproduce it to six decimals. The pitch the EE
    // feeds the camera matrix is eased toward the target angle while
    // DAT_0058bea0 takes it raw, so the two differ slightly while the camera is
    // still moving; for a specular highlight that is not worth modelling.
    const float cosPitch = std::cos(pitchRadians);
    const orphen::ported::psm2::Vec3 forward{std::cos(yawRadians) * cosPitch,
                                             std::sin(yawRadians) * cosPitch,
                                             std::sin(pitchRadians)};

    // lightDirection[0] already holds -DAT_003439c8, so the microprogram's
    // -(forward + DAT_003439c8) is lightDirection[0] - forward.
    const auto &lightDirection = sceneLighting_.lightDirection[0];
    orphen::ported::psm2::Vec3 half{lightDirection.x - forward.x,
                                    lightDirection.y - forward.y,
                                    lightDirection.z - forward.z};
    const float lengthSquared =
        half.x * half.x + half.y * half.y + half.z * half.z;
    if (lengthSquared > 0.0f)
    {
      const float scale = 1.0f / std::sqrt(lengthSquared);
      half.x *= scale;
      half.y *= scale;
      half.z *= scale;
    }
    sceneLighting_.gleamDirection = half;
  }

  // 0..255 per channel, not the 0..0x80 the vertex colours use: FUN_0026f108
  // sets this same global to 0xfed261, and 0xfe does not fit the halved
  // convention.
  void MapViewer::setFogColour(std::uint32_t packedRgb)
  {
    fogColourPacked_ = packedRgb & 0xFFFFFFu;
    fogColour_[0] = static_cast<float>((packedRgb >> 16) & 0xFF) / 255.0f;
    fogColour_[1] = static_cast<float>((packedRgb >> 8) & 0xFF) / 255.0f;
    fogColour_[2] = static_cast<float>(packedRgb & 0xFF) / 255.0f;
  }

  void MapViewer::setFogBand(float nearDistance, float farDistance)
  {
    fogNear_ = nearDistance;
    fogFar_ = farDistance;
  }

  void MapViewer::setSceneLighting(const orphen::ported::render::SceneLighting &lighting)
  {
    sceneLighting_ = lighting;
  }

  // The band is DAT_0035567c..DAT_00355680 and the colour DAT_00355674, all
  // carried in from the scene environment block rather than derived here --
  // FUN_0022a418 seeds them and the scene script overrides through 0xB9/0xBB.
  // The falloff across the band is linear in 1/z, not in distance; see
  // emitFogCoord, which supplies the curve per vertex when glFogCoordf is
  // reachable and leaves GL to do the blend.
  void MapViewer::applyFogState(bool enabled) const
  {
    const float fogStart = fogNear_;
    const float fogEnd = fogFar_;
    g_vertexFog.active = false;
    if (!enabled || fogStart <= 0.0f || fogStart >= fogEnd)
    {
      glDisable(GL_FOG);
      return;
    }

    const GLfloat fogColour[4] = {fogColour_[0], fogColour_[1], fogColour_[2], 1.0f};
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fogColour);

    const bool haveFogCoord = orphen::port::gl::loadFogCoordExtension();

    static bool reportedFogPath = false;
    if (!reportedFogPath)
    {
      reportedFogPath = true;
      std::cout << "[render] fog " << fogStart << ".." << fogEnd
                << " colour 0x" << std::hex << fogColourPacked_ << std::dec
                << (haveFogCoord ? " curve 1/z (glFogCoordf)"
                                 : " curve linear (glFogCoordf unavailable)")
                << '\n';
    }

    if (haveFogCoord)
    {
      // emitFogCoord hands over the finished 0..1 fade, so GL's ramp
      // f = (end - c) / (end - start) reduces to f = 1 - c.
      glFogi(static_cast<GLenum>(orphen::port::gl::kFogCoordinateSource),
             static_cast<GLint>(orphen::port::gl::kFogCoordinate));
      glFogf(GL_FOG_START, 0.0f);
      glFogf(GL_FOG_END, 1.0f);

      // Eye-space depth is row 2 of the modelview, which render() has already
      // snapshotted for the click probe. Column-major, so the row's four terms
      // are elements 2, 6, 10 and 14.
      g_vertexFog.depthRow[0] = probeModelView_[2];
      g_vertexFog.depthRow[1] = probeModelView_[6];
      g_vertexFog.depthRow[2] = probeModelView_[10];
      g_vertexFog.depthRow[3] = probeModelView_[14];
      g_vertexFog.inverseNear = 1.0f / fogStart;
      g_vertexFog.inverseSpan = 1.0f / (1.0f / fogStart - 1.0f / fogEnd);
      g_vertexFog.active = true;
    }
    else
    {
      // No 1.4 entry point: GL's linear-in-distance ramp over the same band.
      // Thinner than the hardware's near the camera, but the band is right.
      glFogf(GL_FOG_START, fogStart);
      glFogf(GL_FOG_END, fogEnd);
    }

    glEnable(GL_FOG);
  }

  void MapViewer::setHudLines(std::vector<std::string> lines)
  {
    hudLines_ = std::move(lines);
  }

  orphen::ported::psm2::Vec3 MapViewer::freeViewerMovement(float strafe, float forward) const
  {
    const auto basis = viewerGroundBasis(cameraYawDegrees_);
    const float viewerX = basis.right.x * strafe + basis.forward.x * forward;
    const float viewerZ = basis.right.z * strafe + basis.forward.z * forward;
    return {viewerX, -viewerZ, 0.0f};
  }

  void MapViewer::printLoadedSceneTree(std::ostream &output) const
  {
    if (!sceneResources_.has_value())
    {
      output << "[scene-tree] unavailable for " << loadedSourceDescription_ << '\n';
      return;
    }

    printSceneResourceTree(buildSceneResourceTree(*sceneResources_), output);
  }

  void MapViewer::setTexturePages(std::vector<LoadedDiscTexturePage> texturePages)
  {
    releaseUploadedTextures();
    texturePages_ = std::move(texturePages);
    textureUploadDirty_ = true;
  }

  void MapViewer::releaseUploadedTextures() const
  {
    if (!uploadedTextureIds_.empty())
    {
      glDeleteTextures(static_cast<GLsizei>(uploadedTextureIds_.size()), uploadedTextureIds_.data());
      uploadedTextureIds_.clear();
    }
    textureUploadDirty_ = true;
  }

  // Deliberately no "same pointer, nothing to do" shortcut. This is called
  // every simulation step with the address of a cache that lives inside
  // EntityModelStore, so the pointer is the same forever -- including across a
  // scene reload, which resets the cache and refills every slot in place. The
  // upload keys on the cache's generation instead; see
  // ensureSlotTexturesUploaded.
  void MapViewer::setTextureSlotCache(const orphen::ported::resource::TextureSlotCache *slots)
  {
    textureSlots_ = slots;
  }

  // One GL texture per occupied cache slot. This is where the port stops
  // following FUN_00210368 / FUN_002103d0, which move the decoded pixels into GS
  // VRAM through a BITBLT packet: a texture object is the GL equivalent and the
  // slot index stays the same currency the subdraw records use.
  void MapViewer::ensureSlotTexturesUploaded() const
  {
    const std::uint64_t cacheGeneration = textureSlots_ != nullptr ? textureSlots_->generation() : 0;
    if (!slotTextureUploadDirty_ && cacheGeneration == uploadedSlotGeneration_)
    {
      return;
    }
    slotTextureUploadDirty_ = false;
    uploadedSlotGeneration_ = cacheGeneration;

    if (!slotTextureIds_.empty())
    {
      glDeleteTextures(static_cast<GLsizei>(slotTextureIds_.size()), slotTextureIds_.data());
      slotTextureIds_.clear();
    }
    if (textureSlots_ == nullptr)
    {
      return;
    }

    slotTextureIds_.assign(orphen::ported::resource::kTextureSlotCount, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (std::size_t slot = 0; slot < orphen::ported::resource::kTextureSlotCount; ++slot)
    {
      const auto &state = textureSlots_->slot(slot);
      if (state.texture.rgbaPixels.empty())
      {
        continue;
      }
      GLuint textureId = 0;
      glGenTextures(1, &textureId);
      glBindTexture(GL_TEXTURE_2D, textureId);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state.texture.width, state.texture.height, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, state.texture.rgbaPixels.data());
      slotTextureIds_[slot] = textureId;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void MapViewer::ensureTexturesUploaded() const
  {
    if (!textureUploadDirty_)
    {
      return;
    }

    uploadedTextureIds_.assign(texturePages_.size(), 0);
    if (texturePages_.empty())
    {
      textureUploadDirty_ = false;
      return;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (std::size_t pageIndex = 0; pageIndex < texturePages_.size(); ++pageIndex)
    {
      GLuint textureId = 0;
      glGenTextures(1, &textureId);
      glBindTexture(GL_TEXTURE_2D, textureId);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      const auto &texture = texturePages_[pageIndex].texture;
      glTexImage2D(GL_TEXTURE_2D,
                   0,
                   GL_RGBA,
                   texture.width,
                   texture.height,
                   0,
                   GL_RGBA,
                   GL_UNSIGNED_BYTE,
                   texture.rgbaPixels.data());
      uploadedTextureIds_[pageIndex] = textureId;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    textureUploadDirty_ = false;
  }

  void MapViewer::resetCamera()
  {
    if (map_.has_value() && map_->bounds.valid)
    {
      cameraTarget_ = toViewerSpace(boundsCenter(map_->bounds));
      cameraDistance_ = boundsRadius(map_->bounds);
    }
    else
    {
      cameraTarget_ = {};
      cameraDistance_ = 12.0f;
    }

    cameraYawDegrees_ = 35.0f;
    cameraPitchDegrees_ = -55.0f;
    followCameraPose_ = {};
  }

  void MapViewer::update(float deltaSeconds, const orphen::port::InputSnapshot &input)
  {
    if (input.toggleHudRequested)
    {
      hudVisible_ = !hudVisible_;
    }

    if (input.toggleDebugOverlayRequested)
    {
      debugOverlayVisible_ = !debugOverlayVisible_;
      std::cout << "[debug overlay] " << (debugOverlayVisible_ ? "on" : "off") << '\n';
    }

    // Uses last frame's matrices, which is what the click was aimed at anyway.
    if (input.probeRequested)
    {
      probeAt(input.probeX, input.probeY);
    }

    constexpr float kPanSpeed = 0.75f;
    constexpr float kYawSpeed = 70.0f;
    constexpr float kPitchSpeed = 55.0f;
    constexpr float kZoomSpeed = 2.25f;

    if (input.toggleWireframeRequested)
    {
      wireframe_ = !wireframe_;
    }

    if (input.previousMapRequested)
    {
      cycleDiscScene(-1);
    }
    if (input.nextMapRequested)
    {
      cycleDiscScene(1);
    }

    if (!leadPlayerView_.has_value())
    {
      cameraYawDegrees_ += input.rotateX * kYawSpeed * deltaSeconds;
      cameraPitchDegrees_ = std::clamp(cameraPitchDegrees_ + input.rotateY * kPitchSpeed * deltaSeconds, -85.0f, -10.0f);
      const float panDistance = std::max(cameraDistance_, 10.0f) * kPanSpeed * deltaSeconds;
      const auto basis = viewerGroundBasis(cameraYawDegrees_);

      cameraTarget_.x += (basis.right.x * input.moveX + basis.forward.x * input.moveY) * panDistance;
      cameraTarget_.z += (basis.right.z * input.moveX + basis.forward.z * input.moveY) * panDistance;
      cameraDistance_ *= std::exp(-input.zoom * kZoomSpeed * deltaSeconds);
      cameraDistance_ = std::max(cameraDistance_, 1.0f);
    }
  }

  void MapViewer::render(int framebufferWidth, int framebufferHeight) const
  {
    ensureTexturesUploaded();
    ensureSlotTexturesUploaded();
    lastFramebufferWidth_ = framebufferWidth;
    lastFramebufferHeight_ = framebufferHeight;
    g_sceneLighting = &sceneLighting_;
    g_gleamProbes = gleamProbeSink_;
    if (g_gleamProbes != nullptr)
    {
      g_gleamProbes->clear();
    }

    // The ported camera works in game space; the free viewer still works in
    // the viewer space this file has always used, so only one of the two
    // paths applies the axis remap.
    const bool useOriginalCamera = leadPlayerView_.has_value() && renderCamera_.has_value();

    // The original's projection is fixed, so the window has to adapt to it
    // rather than the other way round: fit a 4:3 box inside the window and put
    // bars in whatever is left. Restored to the full window before the HUD,
    // which is screen-space and belongs to the harness rather than the game.
    if (useOriginalCamera)
    {
      const float aspect = orphen::ported::render::constants::kDisplayAspect;
      int viewWidth = framebufferWidth;
      int viewHeight = static_cast<int>(static_cast<float>(framebufferWidth) / aspect + 0.5f);
      if (viewHeight > framebufferHeight)
      {
        viewHeight = framebufferHeight;
        viewWidth = static_cast<int>(static_cast<float>(framebufferHeight) * aspect + 0.5f);
      }
      const int viewX = (framebufferWidth - viewWidth) / 2;
      const int viewY = (framebufferHeight - viewHeight) / 2;
      glViewport(viewX, viewY, viewWidth, viewHeight);

      // Clear the 3D area to the fog colour rather than to the window's
      // near-black. There is no separate backdrop global -- the decompilation
      // has no GS BGCOLOR write at all -- but the value is still determined:
      // fog saturates to DAT_00355674 at DAT_00355680, which is the draw
      // distance, and nothing is drawn past it. A backdrop of any other colour
      // would show as a seam at the horizon. Leaving it near-black is also
      // what made the fog look thin, since distant geometry was fading toward
      // grey against a black void instead of blending into it.
      //
      // Scissored so the letterbox bars stay the window's own colour; they are
      // outside the game's 4:3 image and were never part of the frame.
      glEnable(GL_SCISSOR_TEST);
      glScissor(viewX, viewY, viewWidth, viewHeight);
      glClearColor(fogColour_[0], fogColour_[1], fogColour_[2], 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glDisable(GL_SCISSOR_TEST);
    }

    float renderCameraDistance = cameraDistance_;
    if (useOriginalCamera)
    {
      renderCameraDistance = viewerDistance(toViewerSpace(followCameraPose_.eye),
                                            toViewerSpace(followCameraPose_.target));
      const auto camera = orphen::ported::render::glCameraFor(*renderCamera_,
                                                              framebufferWidth,
                                                              framebufferHeight,
                                                              orphen::ported::render::constants::kNearPlane,
                                                              drawDistance_ + kFarPlaneMargin);
      glMatrixMode(GL_PROJECTION);
      glLoadMatrixf(camera.projection.data());
      glMatrixMode(GL_MODELVIEW);
      glLoadMatrixf(camera.modelView.data());
    }
    else
    {
      setPerspective(framebufferWidth, framebufferHeight, 60.0f, cameraDistance_ * 8.0f);
      applyCamera(cameraTarget_, cameraDistance_, cameraYawDegrees_, cameraPitchDegrees_);
    }

    // Snapshot what the frame is actually being drawn with, so probeAt can
    // build its ray from the same matrices instead of a reconstruction.
    glGetFloatv(GL_MODELVIEW_MATRIX, probeModelView_.data());
    glGetFloatv(GL_PROJECTION_MATRIX, probeProjection_.data());
    probeMatricesValid_ = true;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    // Backface culling is what makes a wall single-sided, and with the fade it
    // is why the camera outside a room sees through it. The GS has no culling
    // hardware -- the original does this in the VU1 microprogram at 0xE0 --
    // but the winding is fully determined by the corner order FUN_0022c6e8
    // uses, so GL can reproduce it. The free viewer keeps culling off so map
    // inspection still shows both sides.
    if (useOriginalCamera && !wireframe_)
    {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(GL_CCW);
    }
    else
    {
      glDisable(GL_CULL_FACE);
    }

    applyFogState(useOriginalCamera);

    // Sorted here rather than on the simulation step, because unlike the map's
    // fade byte nothing about it is per-frame state -- it is a pure function of
    // the camera and the entity positions, and it must not run headless.
    std::vector<orphen::ported::render::EntityDrawItem> entityDrawList;
    if (renderCamera_.has_value() && !sceneObjectViews_.empty())
    {
      entityDrawList = orphen::ported::render::FUN_0020eec0_buildEntityDrawList(sceneObjectViews_,
                                                                                *renderCamera_);
    }

    if (!useOriginalCamera)
    {
      drawGrid(renderCameraDistance);
    }
    if (map_.has_value())
    {
      if (useOriginalCamera)
      {
        drawMap(*map_, uploadedTextureIds_, mapDrawList_, useOriginalCamera && !wireframe_,
                sceneObjectViews_, entityDrawList, slotTextureIds_);
      }
      else
      {
        // The free viewer has no depth-sorted list to merge into, so models go
        // out unsorted after the map and lean on the depth buffer.
        drawMapUnsorted(*map_, uploadedTextureIds_);
        drawObjectModels(sceneObjectViews_, slotTextureIds_);
      }
    }
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    if (debugOverlayVisible_)
    {
      if (leadPlayerView_.has_value())
      {
        drawLeadPlayer(*leadPlayerView_);
      }
      if (!sceneObjectViews_.empty())
      {
        glEnable(GL_DEPTH_TEST);
        drawSceneObjects(sceneObjectViews_);
      }

      glDisable(GL_DEPTH_TEST);
      drawOriginAxisIndicator(renderCameraDistance);
      glEnable(GL_DEPTH_TEST);
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, framebufferWidth, framebufferHeight);

    if (hudVisible_)
    {
      debugText_.draw(framebufferWidth, framebufferHeight, hudLines_);
    }
  }

  orphen::ported::psm2::Psm2RuntimeState *MapViewer::loadedMap()
  {
    return map_.has_value() ? &*map_ : nullptr;
  }

  const orphen::ported::psm2::Psm2RuntimeState *MapViewer::loadedMap() const
  {
    return map_.has_value() ? &*map_ : nullptr;
  }

  const SceneResourceProvider *MapViewer::loadedSceneResources() const
  {
    return sceneResources_.has_value() ? &*sceneResources_ : nullptr;
  }

  void MapViewer::probeAt(int pixelX, int pixelY) const
  {
    if (!probeMatricesValid_)
    {
      std::cout << "[probe] no frame has been drawn yet\n";
      return;
    }
    orphen::ported::psm2::Vec3 origin{};
    orphen::ported::psm2::Vec3 direction{};
    if (!orphen::harness::unprojectPixel(probeModelView_, probeProjection_,
                                         lastFramebufferWidth_, lastFramebufferHeight_, pixelX,
                                         pixelY, origin, direction))
    {
      std::cout << "[probe] could not unproject that pixel\n";
      return;
    }
    const auto hits = orphen::harness::probeEntityRay(sceneObjectViews_, origin, direction);
    orphen::harness::printProbeReport(std::cout, pixelX, pixelY, origin, direction, hits);
  }

} // namespace orphen::harness
