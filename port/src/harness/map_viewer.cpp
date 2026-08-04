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
    // FUN_0022a418:344 and :347 set the band: DAT_0035567c is drawDistance*0.25
    // and DAT_00355680 is drawDistance, so 8..32 at the usual 32.
    constexpr float kFogStartFraction = 0.25f;
    // DAT_00355674, the fog colour, and DAT_00355678 beside it. FUN_0022a418:346
    // and :348 set both to 0x505050, and s01_e24.bin holds exactly that. It is
    // per scene, not a constant -- eeMemory.bin is a different scene and has
    // both at 0x000000 with a 5..8 band -- so this belongs in the ported scene
    // environment setup eventually. Hardcoded to the s01_e024 value until then.
    //
    // 0..255, not the 0..0x80 the vertex colours use: FUN_0026f108 sets this
    // same pair to 0xfed261, and 0xfe does not fit the halved convention.
    constexpr float kFogColourChannel = 0x50 / 255.0f;

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

    // amount = (1/near - 1/z) / (1/near - 1/far), clamped: 0 at the band's near
    // edge, 1 at the far edge. applyFogState sets GL_FOG_START 0 and
    // GL_FOG_END 1, so GL's linear ramp passes this straight through.
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

      glBegin(GL_TRIANGLES);
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

        // FUN_00212058 draws one pass per active subdraw index, in order, and
        // the port draws only one of them because it has no per-pass blend
        // state yet. **Take the first, not the last.** Pass 0 is the opaque
        // base layer; later passes are translucent overlays, and taking the
        // last one rendered the overlay's UVs opaquely with the base skipped
        // entirely -- a polygon that is drawn, but samples a patch of the sheet
        // it was never meant to, which reads on screen as a hole.
        //
        // grp_0006 prim 110 is the case that showed it up: passes [106, 107],
        // where 106 is texFlags 0x0000 (blend mode 0) and 107 is 0x8019 (blend
        // mode 2, alpha 25 of 127). 10 primitives in that model have a second
        // pass, 18 in grp_0008 and 60 in grp_0009.
        int chosen = -1;
        for (std::size_t pass = 0; pass < 4; ++pass)
        {
          if (primitive.subdrawIndices[pass] >= 0)
          {
            chosen = primitive.subdrawIndices[pass];
            break;
          }
        }

        const orphen::ported::model::Psc3Subdraw *subdraw =
            (chosen >= 0 && static_cast<std::size_t>(chosen) < model.subdraws.size())
                ? &model.subdraws[static_cast<std::size_t>(chosen)]
                : nullptr;

        std::array<orphen::ported::psm2::Vec3, 4> points{};
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          points[corner] = posed(primitive.vertexIndices[corner]);
        }

        const auto emit = [&](std::size_t corner) {
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
          // per-vertex ones have a colour per corner.
          const std::size_t colourEntry =
              primitive.perVertexColour() ? primitive.colourIndex + corner : primitive.colourIndex;
          if (colourEntry * 3 + 2 < model.colours.size())
          {
            // The game's colour bytes run to 0x80 for full brightness, the same
            // convention the map path divides by 128 for.
            glColor3f(std::min(1.0f, model.colours[colourEntry * 3 + 0] / 128.0f),
                      std::min(1.0f, model.colours[colourEntry * 3 + 1] / 128.0f),
                      std::min(1.0f, model.colours[colourEntry * 3 + 2] / 128.0f));
          }
          else
          {
            glColor3f(1.0f, 1.0f, 1.0f);
          }
          emitFogCoord(points[corner].x, points[corner].y, points[corner].z);
          glVertex3f(points[corner].x, points[corner].y, points[corner].z);
        };

        emit(0);
        emit(1);
        emit(2);
        if (corners == 4)
        {
          emit(0);
          emit(2);
          emit(3);
        }
      }
      glEnd();

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

  // FUN_0022a418 derives the fog band from the draw distance: DAT_0035567c is
  // drawDistance * 0.25 and DAT_00355680 is drawDistance, so 8..32 at the
  // usual 32. The falloff across it is linear in 1/z, not in distance -- see
  // emitFogCoord, which supplies the curve per vertex when glFogCoordf is
  // reachable and leaves GL to do the blend.
  void MapViewer::applyFogState(bool enabled) const
  {
    const float fogStart = drawDistance_ * kFogStartFraction;
    g_vertexFog.active = false;
    if (!enabled || fogStart <= 0.0f || fogStart >= drawDistance_)
    {
      glDisable(GL_FOG);
      return;
    }

    const GLfloat fogColour[4] = {kFogColourChannel, kFogColourChannel, kFogColourChannel, 1.0f};
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, fogColour);

    const bool haveFogCoord = orphen::port::gl::loadFogCoordExtension();

    static bool reportedFogPath = false;
    if (!reportedFogPath)
    {
      reportedFogPath = true;
      std::cout << "[render] fog " << fogStart << ".." << drawDistance_
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
      g_vertexFog.inverseSpan = 1.0f / (1.0f / fogStart - 1.0f / drawDistance_);
      g_vertexFog.active = true;
    }
    else
    {
      // No 1.4 entry point: GL's linear-in-distance ramp over the same band.
      // Thinner than the hardware's near the camera, but the band is right.
      glFogf(GL_FOG_START, fogStart);
      glFogf(GL_FOG_END, drawDistance_);
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

  void MapViewer::setTextureSlotCache(const orphen::ported::resource::TextureSlotCache *slots)
  {
    if (textureSlots_ == slots)
    {
      return;
    }
    textureSlots_ = slots;
    slotTextureUploadDirty_ = true;
  }

  // One GL texture per occupied cache slot. This is where the port stops
  // following FUN_00210368 / FUN_002103d0, which move the decoded pixels into GS
  // VRAM through a BITBLT packet: a texture object is the GL equivalent and the
  // slot index stays the same currency the subdraw records use.
  void MapViewer::ensureSlotTexturesUploaded() const
  {
    if (!slotTextureUploadDirty_)
    {
      return;
    }
    slotTextureUploadDirty_ = false;

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
      glClearColor(kFogColourChannel, kFogColourChannel, kFogColourChannel, 1.0f);
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
