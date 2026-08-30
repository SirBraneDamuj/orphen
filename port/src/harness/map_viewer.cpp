#include "harness/map_viewer.h"

#include "harness/entity_probe.h"

#include "ported/model/psc3_skeleton.h"
#include "ported/psm2/psm2_uv_animation.h"
#include "ported/render/original_entity_draw.h"

#include <string>

#include "harness/scene_resource_tree.h"
#include "ported/psm2/decoded_psm2_loader.h"

#include "platform/gl_extensions.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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

    // Entity +0x134 for the model currently being drawn, already turned into a
    // 0..1 multiplier. 1.0 is the not-fading case, which is what a zero +0x134
    // means -- FUN_0020c810:142 substitutes 0x80 for it.
    float g_entityFadeAlpha = 1.0f;

    // Set only while a diagnostic run is collecting, so --gleam-report can
    // report the specular pass's dot products and normal lengths without
    // needing a second traversal.
    std::vector<orphen::harness::GleamProbe> *g_gleamProbes = nullptr;

    // Set for the duration of render() when a --frame-stats run is collecting.
    // Null on every other run, so the counter sites below are a predicted
    // not-taken branch rather than work.
    orphen::harness::RenderStats *g_renderStats = nullptr;

    // --map-no-blend: force every map primitive back to the opaque path, so the
    // ABE block can be A/B'd against the way this drew before it was ported.
    bool g_mapBlendDisabled = false;

    // --map-base-slot: draw only material slot 0, the way this drew before
    // FUN_00211230's slot loop was ported. The same A/B handle as above.
    bool g_mapBaseSlotOnly = false;

    // --entity-bound-texture: draw every textured PSC3 pass with the entity's
    // bound slot, ignoring the subdraw's own selector, the way this drew before
    // FUN_00212058's byte-6 block was ported.
    bool g_entityBoundTextureOnly = false;

    // Wall-clock for one render phase, added to `sink` on scope exit. Coarse by
    // design -- see RenderStats.
    class PhaseTimer
    {
    public:
      explicit PhaseTimer(std::uint64_t *sink)
          : sink_(sink), start_(sink != nullptr ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{})
      {
      }
      ~PhaseTimer()
      {
        if (sink_ != nullptr)
        {
          *sink_ += static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - start_)
                  .count());
        }
      }
      PhaseTimer(const PhaseTimer &) = delete;
      PhaseTimer &operator=(const PhaseTimer &) = delete;

    private:
      std::uint64_t *sink_;
      std::chrono::steady_clock::time_point start_;
    };

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

    // Immediate mode costs four GL calls per vertex -- glTexCoord2f, glColor4f,
    // glFogCoordf, glVertex3f -- and s01_e024 emits roughly 59,000 vertices a
    // frame between the map, the models and the specular pass. That is about
    // 205,000 driver calls per frame, and it measured as the entire frame
    // budget: the total pinned at 16.6 ms no matter which call the stall
    // happened to surface in. Removing a 7 ms glGetFloatv did not make the
    // frame faster, it just moved the 7 ms into the next call that filled the
    // queue, which is the signature of being submission-bound rather than slow
    // at any one thing.
    //
    // Vertex arrays are the fixed-function answer: pack the vertices into a CPU
    // buffer and hand GL one glDrawArrays per state change. No shaders, no VBOs
    // and nothing newer than GL 1.1 except the fog-coord pointer, which is
    // resolved next to glFogCoordf and falls back to GL's own eye-distance fog
    // when the driver has neither.
    //
    // Draw order is preserved exactly. The buffer is flushed wherever glEnd
    // used to be -- on every blend-mode change, texture bind and pass boundary
    // -- so the sequence of primitives reaching the GS-equivalent is unchanged.
    struct DrawVertex
    {
      float position[3];
      float texCoord[2];
      float colour[4];
      float fogCoord;
    };

    // Reused across frames and across all three draw paths, so the per-frame
    // allocation is one growth to the high-water mark and nothing after.
    std::vector<DrawVertex> g_batchVertices;

    // What the accumulated vertices need switched on when they go out. Tracked
    // rather than inferred so a batch with no texture does not leave the
    // texture-coordinate array enabled for the next one.
    bool g_batchTextured = false;

    void batchReset(bool textured)
    {
      g_batchVertices.clear();
      g_batchTextured = textured;
    }

    void batchPush(const orphen::ported::psm2::Vec3 &position,
                   float u, float v,
                   const float colour[4],
                   float fogCoord)
    {
      DrawVertex &vertex = g_batchVertices.emplace_back();
      vertex.position[0] = position.x;
      vertex.position[1] = position.y;
      vertex.position[2] = position.z;
      vertex.texCoord[0] = u;
      vertex.texCoord[1] = v;
      vertex.colour[0] = colour[0];
      vertex.colour[1] = colour[1];
      vertex.colour[2] = colour[2];
      vertex.colour[3] = colour[3];
      vertex.fogCoord = fogCoord;
    }

    // One draw call for everything accumulated since the last flush. Leaves the
    // client-state switches off on the way out, because the debug overlay and
    // the HUD still draw in immediate mode and a stale enabled array would make
    // GL read vertices out of a buffer they know nothing about.
    void batchFlush()
    {
      if (g_batchVertices.empty())
      {
        return;
      }

      const DrawVertex *base = g_batchVertices.data();
      const GLsizei stride = static_cast<GLsizei>(sizeof(DrawVertex));

      glEnableClientState(GL_VERTEX_ARRAY);
      glVertexPointer(3, GL_FLOAT, stride, &base->position[0]);
      glEnableClientState(GL_COLOR_ARRAY);
      glColorPointer(4, GL_FLOAT, stride, &base->colour[0]);
      if (g_batchTextured)
      {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, &base->texCoord[0]);
      }
      const bool fogged = g_vertexFog.active && orphen::port::gl::hasFogCoordPointer();
      if (fogged)
      {
        glEnableClientState(orphen::port::gl::kFogCoordinateArray);
        orphen::port::gl::fogCoordPointer(stride, &base->fogCoord);
      }

      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_batchVertices.size()));

      glDisableClientState(GL_VERTEX_ARRAY);
      glDisableClientState(GL_COLOR_ARRAY);
      if (g_batchTextured)
      {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
      }
      if (fogged)
      {
        glDisableClientState(orphen::port::gl::kFogCoordinateArray);
      }

      g_batchVertices.clear();
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

      // FUN_0020eec0:67-94, once per model rather than per vertex: the entity's
      // own position resolves table slots 0..2 into VU1's directional lights
      // 1..3, and everything past them is summed flat into the additive tint at
      // ctx+0x1BC. The original reads that position from the per-draw context at
      // +0xA0; the port uses the entity root, which is what the context is built
      // from. Nothing in the two dumps pins +0xA0 down independently, so that is
      // the one assumption in this path.
      orphen::ported::render::SceneLighting::DynamicContribution entityLights;
      const orphen::ported::render::SceneLighting::DynamicContribution *entityLightsPointer =
          nullptr;
      if (g_sceneLighting != nullptr && g_sceneLighting->pointLightCount != 0)
      {
        g_sceneLighting->buildEntityContribution(
            {object.position.x, object.position.y, object.position.z}, entityLights);
        entityLightsPointer = &entityLights;
      }

      // FUN_0020c810:140. A zero +0x134 is "not fading" and becomes 0x80.
      g_entityFadeAlpha = object.fadeLevel == 0
                              ? 1.0f
                              : std::min(1.0f, static_cast<float>(object.fadeLevel) / 128.0f);

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

      if (g_renderStats != nullptr)
      {
        ++g_renderStats->entityModels;
      }

      batchReset(texture != 0);

      // Texturing is per *pass*, not per model: a model with a bound texture can
      // still carry bit-15 passes that draw flat colour, and a pass can name a
      // different sheet entirely (see passTextureFor below). Switching it closes
      // the batch, the same way a blend-mode change does.
      unsigned int activeTexture = texture;
      const auto setTexture = [&activeTexture](unsigned int id) {
        if (activeTexture == id)
        {
          return;
        }
        batchFlush();
        activeTexture = id;
        if (id != 0)
        {
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, id);
        }
        else
        {
          glDisable(GL_TEXTURE_2D);
        }
        g_batchTextured = id != 0;
      };

      int activeBlendMode = -1;
      const auto setBlendMode = [&activeBlendMode](int mode) {
        if (mode == activeBlendMode)
        {
          return;
        }
        if (g_renderStats != nullptr)
        {
          ++g_renderStats->entityBatches;
        }
        // Where glEnd used to be: everything accumulated under the old blend
        // state goes out before the new state is set, so the order the GS would
        // have seen is preserved.
        batchFlush();
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
      };

      // GL's current texture coordinate, carried by hand. Scoped to the model
      // the same way the GL state it replaces effectively was.
      float currentU = 0.0f;
      float currentV = 0.0f;

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

        // Entity +0x168's hidden bones. One corner is enough: on VU1 that
        // corner has no finite screen position, so the whole primitive is gone.
        if (!object.hiddenBones.empty())
        {
          bool touchesHidden = false;
          for (std::size_t corner = 0; corner < corners && !touchesHidden; ++corner)
          {
            touchesHidden =
                object.boneHidden(model.vertices[primitive.vertexIndices[corner]].boneIndex);
          }
          if (touchesHidden)
          {
            continue;
          }
        }

        // FUN_00212058 draws one pass per active subdraw index, in order, each
        // with the blend mode its texFlags select. Pass 0 is the opaque base and
        // the later ones are overlays -- additive glow layers, mostly. Drawing
        // only the base is what the port did before it had per-pass blend state,
        // and it is what made map_009f's chest look flat next to the real frame.
        std::array<orphen::ported::psm2::Vec3, 4> points{};
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          points[corner] = posed(primitive.vertexIndices[corner]);
        }
        if (g_renderStats != nullptr)
        {
          ++g_renderStats->entityPrimitives;
          g_renderStats->vertexTransforms += corners;
        }

        const auto emit = [&](std::size_t corner,
                              const orphen::ported::model::Psc3Subdraw *subdraw,
                              float passAlpha,
                              int colourOverride) {
          // Immediate mode carried the last glTexCoord2f forward when a pass
          // did not set one; the array path has to say so explicitly, so the
          // pair persists across emits exactly as GL's current-texcoord did.
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
            currentU = static_cast<float>(packed & 0xFF) / 256.0f;
            currentV = static_cast<float>((packed >> 8) & 0xFF) / 256.0f;
          }
          // Flat-shaded primitives hold one colour at colourIndex; only the
          // per-vertex ones have a colour per corner. Same for the normal:
          // +0x06 per vertex under flag 0x8, +0x16 for the whole primitive
          // otherwise.
          // FUN_002129b8 line 87: a bit-15 pass replaces the primitive's own
          // +0x0A with the low 15 bits of the pass index. Everything else about
          // the lookup -- per-corner under flag 0x8, corner 0's colour reused
          // otherwise -- is the same either way.
          const bool untexturedPass = colourOverride >= 0;
          const std::size_t colourBase =
              untexturedPass ? static_cast<std::size_t>(colourOverride)
                             : primitive.colourIndex;
          const std::size_t colourEntry =
              primitive.perVertexColour() ? colourBase + corner : colourBase;

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
              if (g_renderStats != nullptr)
              {
                ++g_renderStats->lightingEvaluations;
              }
              // Header byte 14, FUN_00212058:228: the complement of the
              // primitive's +0x0D, which VU1 scales by 1/320 into vf15.z.
              g_sceneLighting->modulator(
                  orphen::harness::posedWorldNormal(model, palette, bone,
                                                    model.normals[normalIndex]),
                  g_sceneLighting->applyLightFloor
                      ? orphen::ported::render::SceneLighting::floorFromSourceByte(
                            primitive.alphaByte)
                      : 0.0f,
                  light, entityLightsPointer);
            }
          }

          float colour[4] = {0.0f, 0.0f, 0.0f, passAlpha};
          if (colourEntry * 3 + 2 < model.colours.size())
          {
            // **Two different conventions, and which applies depends on TME.**
            // A textured pass modulates: the GS computes (Ct * Cv) >> 7, so a
            // vertex colour of 0x80 means "x1.0" and the divisor is 128, the
            // same one the map path uses. An untextured pass has no texture to
            // modulate -- FUN_00212058's mode 2 never sets TME -- so the colour
            // register goes straight to the framebuffer over 0..255.
            //
            // grp_001E is the case that shows it. Its one colour entry is
            // (191, 0, 0): meaningless as a modulator (x1.49, clamped to a
            // blown-out pure red) and exactly right as a colour, 0xBF.
            const float scale = untexturedPass ? 1.0f / 255.0f : 1.0f / 128.0f;
            colour[0] = std::min(1.0f, model.colours[colourEntry * 3 + 0] * scale * light[0]);
            colour[1] = std::min(1.0f, model.colours[colourEntry * 3 + 1] * scale * light[1]);
            colour[2] = std::min(1.0f, model.colours[colourEntry * 3 + 2] * scale * light[2]);
          }
          else
          {
            colour[0] = std::min(1.0f, light[0]);
            colour[1] = std::min(1.0f, light[1]);
            colour[2] = std::min(1.0f, light[2]);
          }
          batchPush(points[corner], currentU, currentV, colour,
                    fogAmountAt(points[corner].x, points[corner].y, points[corner].z));
        };

        for (std::size_t pass = 0; pass < 4; ++pass)
        {
          const std::int16_t index = primitive.subdrawIndices[pass];
          // **Only -1 skips a pass.** FUN_00212058 line 106 tests for exactly
          // that value; any other negative is a pass that draws *untextured*,
          // with the low 15 bits standing in for the primitive's colour index
          // (FUN_002129b8 lines 85-111). Treating every negative as "no pass"
          // dropped 26 passes on grp_0001, 43 on grp_0009 -- and all 20 of
          // grp_001E, which is why the bandana was invisible.
          if (index == -1)
          {
            continue;
          }
          const bool untexturedPass = index < 0;
          const int colourOverride = untexturedPass ? (index & 0x7FFF) : -1;
          const orphen::ported::model::Psc3Subdraw *subdraw =
              !untexturedPass && static_cast<std::size_t>(index) < model.subdraws.size()
                  ? &model.subdraws[static_cast<std::size_t>(index)]
                  : nullptr;

          // FUN_00212058:137-141. Mode 0 never enables ABE, so its alpha field
          // is not read at all; otherwise 0x7F is the sentinel for 0x80, fully
          // opaque, and anything else is the value straight out of the low
          // seven bits.
          int mode = 0;
          float passAlpha = g_entityFadeAlpha;
          if (subdraw != nullptr)
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
              passAlpha *= g_entityFadeAlpha;
            }
            // A fading entity has to blend whatever its passes say. The
            // original gets this for free: +0x134 rides in the draw header and
            // VU1 folds it into the vertex alpha before the GS's own ALPHA
            // register ever sees it.
            if (mode == 0 && g_entityFadeAlpha < 1.0f)
            {
              mode = 1;
            }
          }
          setBlendMode(mode);

          // **A pass picks its own sheet.** FUN_00212058:180-208 reads the
          // subdraw's texFlags bits 10..7 and writes packet byte 6 from it:
          //
          //   selector 0     -> 0x3F, the entity's own bound slot
          //   selector 0xF   -> 0x3E with byte 5 = 0x11, the special mode the
          //                     map path reaches through its own type 9
          //   selector 1..0xE, primitive flag 0x800 clear
          //                  -> byte 6 = the selector itself, and on the map
          //                     path (FUN_00211230:186) byte 6 is
          //                     `globalTextureSlot + 1` -- so this names global
          //                     slot `selector - 1`, not the bound one
          //   selector 1..0xE, primitive flag 0x800 set
          //                  -> byte 6 goes back to 0x3F and the selector rides
          //                     in byte 5 instead, so the bound slot still wins
          //
          // Drawing every pass with the bound slot is what put the shop's gold
          // medallion on the window curtains: grp_01d5's 48 passes all ask for
          // global slot 3 (tex_0253, the white sheer curtain) while the entity
          // is bound to slot 22 (tex_0133). The window frames, the lantern
          // flames and the barrels were all reaching past their bound slot too.
          unsigned int passTexture = texture;
          if (!g_entityBoundTextureOnly && !untexturedPass && subdraw != nullptr &&
              (primitive.flags & 0x0800u) == 0)
          {
            const std::uint16_t selector = subdraw->textureSlot();
            if (selector != 0 && selector != 0xF)
            {
              const std::size_t globalSlot = static_cast<std::size_t>(selector) - 1u;
              passTexture = globalSlot < slotTextures.size() ? slotTextures[globalSlot] : 0u;
            }
          }
          // The bit-15 branch never reaches FUN_00212058's alpha/ABE block, so
          // it draws opaque -- FUN_002129b8 plants 0xFF in the alpha byte
          // outright -- and through FUN_00212cf0, the untextured colour path.
          setTexture(untexturedPass ? 0u : passTexture);

          emit(0, subdraw, passAlpha, colourOverride);
          emit(1, subdraw, passAlpha, colourOverride);
          emit(2, subdraw, passAlpha, colourOverride);
          if (corners == 4)
          {
            emit(0, subdraw, passAlpha, colourOverride);
            emit(2, subdraw, passAlpha, colourOverride);
            emit(3, subdraw, passAlpha, colourOverride);
          }
          if (g_renderStats != nullptr)
          {
            g_renderStats->entityTriangles += corners == 4 ? 2u : 1u;
          }
        }
      }
      batchFlush();
      glPopAttrib();

      // The specular pass, VU1 0x0200. FUN_00212058 appends a second GIF packet
      // -- untextured, gouraud, ABE on, additive, depth-tested but not
      // depth-written -- to any primitive whose +0x0C is non-zero, drawn in the
      // scene's light-0 colour. See SceneLighting's gleam block.
      const bool gleamDraw = g_sceneLighting != nullptr && g_sceneLighting->gleamActive;
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

        batchReset(false);
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
          if (g_renderStats != nullptr)
          {
            g_renderStats->vertexTransforms += corners;
          }

          const auto cornerOpacity = [&](std::size_t corner) -> float {
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
              if (g_renderStats != nullptr)
              {
                ++g_renderStats->lightingEvaluations;
              }
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
            return opacity;
          };

          // Every corner is evaluated whether or not the result is drawn: the
          // probe's whole job is to report the dot products, including the ones
          // that came to nothing.
          std::array<float, 4> opacities{};
          bool anyLit = false;
          for (std::size_t corner = 0; corner < corners; ++corner)
          {
            opacities[corner] = std::clamp(cornerOpacity(corner), 0.0f, 1.0f);
            anyLit = anyLit || opacities[corner] > 0.0f;
          }

          // An additive pass at alpha zero adds zero. Roughly half of the
          // 6,672 specular triangles a frame in s01_e024 are facing away from
          // the half-vector and contribute nothing, so submitting them was
          // paying full vertex cost for an invisible result.
          if (!gleamDraw || !anyLit)
          {
            continue;
          }
          if (g_renderStats != nullptr)
          {
            g_renderStats->gleamTriangles += corners == 4 ? 2u : 1u;
          }

          // /255, not the /128 the rest of the file uses. That 128 is the GS's
          // 1.0 for *texture modulation*, and this pass has TME off -- with no
          // texture the RGBAQ value is the fragment colour itself and goes into
          // the blend unit as a plain 8-bit level, where full scale is 255.
          // Using /128 here made the highlight almost exactly twice as bright
          // as the hardware and clipped its blue channel at 1.0, which skewed
          // the hue too.
          float gleamColour[4] = {
              std::min(1.0f, g_sceneLighting->gleamColour[0] / 255.0f),
              std::min(1.0f, g_sceneLighting->gleamColour[1] / 255.0f),
              std::min(1.0f, g_sceneLighting->gleamColour[2] / 255.0f),
              0.0f};
          const auto pushGleam = [&](std::size_t corner) {
            gleamColour[3] = opacities[corner];
            batchPush(points[corner], 0.0f, 0.0f, gleamColour,
                      fogAmountAt(points[corner].x, points[corner].y, points[corner].z));
          };

          pushGleam(0);
          pushGleam(1);
          pushGleam(2);
          if (corners == 4)
          {
            pushGleam(0);
            pushGleam(2);
            pushGleam(3);
          }
        }
        batchFlush();

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
        // worldOrigin, not position: an attached entity's position fields are a
        // bone-local offset and would put its box at the world origin.
        const auto foot = toViewerSpace(object.worldOrigin);
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

    orphen::ported::psm2::Vec3 viewerVertex(const orphen::ported::psm2::Psm2RuntimeState &map,
                                            std::uint16_t vertexIndex)
    {
      return toViewerSpace(map.DAT_0035569c_sectionCRecords.at(vertexIndex).position);
    }

    void emitVertex(const orphen::ported::psm2::Psm2RuntimeState &map, std::uint16_t vertexIndex)
    {
      const auto viewerPosition = viewerVertex(map, vertexIndex);
      emitFogCoord(viewerPosition.x, viewerPosition.y, viewerPosition.z);
      glVertex3f(viewerPosition.x, viewerPosition.y, viewerPosition.z);
    }

    // FUN_0022c3d8 has already resolved the selector, so the type byte here is
    // the texture page and a negative type means the primitive is untextured
    // rather than missing.
    const orphen::ported::psm2::MaterialSlot *materialSlotForPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                                        std::size_t primitiveIndex,
                                                                        std::size_t slotIndex)
    {
      if (primitiveIndex >= map.DAT_003556ac_dRecords80.size())
      {
        return nullptr;
      }
      return &map.DAT_003556ac_dRecords80[primitiveIndex].materialSlots[slotIndex];
    }

    std::optional<std::size_t> texturePageForSlot(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                  std::size_t primitiveIndex,
                                                  std::size_t slotIndex)
    {
      const auto *slot = materialSlotForPrimitive(map, primitiveIndex, slotIndex);
      if (slot == nullptr || !slot->textured())
      {
        return std::nullopt;
      }

      return slot->type;
    }

    std::pair<float, float> textureCoordinateForCorner(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                       const orphen::ported::psm2::TriangleRecord &triangle,
                                                       std::size_t triangleCornerIndex,
                                                       std::size_t slotIndex)
    {
      const auto *slot = materialSlotForPrimitive(map, triangle.primitiveIndex, slotIndex);
      if (slot == nullptr || !slot->textured())
      {
        return {0.0f, 0.0f};
      }

      const std::uint8_t sourceCorner = triangle.cornerIndices[triangleCornerIndex] & 3;
      // Section E byte 9 picks a UV animation track, and FUN_0020eec0 hands
      // VU1 its accumulated offset to add to these baked coordinates. Byte 9
      // is zero on all but a few hundred slots, so this is a no-op almost
      // everywhere -- but it is the whole of the rain outside the windows.
      const auto offset =
          orphen::ported::psm2::uvOffsetForMaterialByte9(map.DAT_003556f4_uvAnimation, slot->byte9);
      return {static_cast<float>(slot->textureCoordinates[sourceCorner * 2]) / 256.0f + offset.u,
              static_cast<float>(slot->textureCoordinates[sourceCorner * 2 + 1]) / 256.0f +
                  offset.v};
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
                            std::size_t slotIndex,
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
        // The map path runs the *whole* dynamic light list per vertex -- VU0
        // program 0x1c reads the count at quadword 2 and the list at 3, then
        // falls into the loop at 0x52. Unlike the model path there are no
        // directional slots here; the point lights arrive only through VU1's
        // second additive term.
        orphen::ported::render::SceneLighting::DynamicContribution dynamic;
        const orphen::ported::render::SceneLighting::DynamicContribution *dynamicPointer = nullptr;
        if (g_sceneLighting->pointLightCount != 0)
        {
          const auto &world =
              map.DAT_0035569c_sectionCRecords[triangle.vertexIndices[triangleCornerIndex]].position;
          g_sceneLighting->FUN_0020b430_pointLightBytes(world, 0, dynamic.additive);
          dynamic.active = true;
          dynamicPointer = &dynamic;
        }

        float light[3];
        g_sceneLighting->modulator(
            record80.normal,
            g_sceneLighting->applyLightFloor
                ? orphen::ported::render::SceneLighting::floorFromSourceByte(
                      record80.staticAlpha)
                : 0.0f,
            light, dynamicPointer);
        red *= light[0];
        green *= light[1];
        blue *= light[2];
      }

      if (modulateByFlatColour)
      {
        const std::uint32_t flat = record80.materialSlots[slotIndex].flatColour();
        red *= static_cast<float>(flat & 0xff) / 64.0f;
        green *= static_cast<float>((flat >> 8) & 0xff) / 64.0f;
        blue *= static_cast<float>((flat >> 16) & 0xff) / 64.0f;
      }

      const float vertexColour[4] = {std::min(red, 1.0f), std::min(green, 1.0f),
                                     std::min(blue, 1.0f), alpha};

      const auto [u, v] = textureCoordinateForCorner(map, triangle, triangleCornerIndex, slotIndex);
      const auto position = viewerVertex(map, triangle.vertexIndices[triangleCornerIndex]);
      batchPush(position, u, v, vertexColour,
                fogAmountAt(position.x, position.y, position.z));
    }

    // What the accumulated map vertices were built under. A run of primitives
    // sharing a texture page and cull mode is one draw call, which is the whole
    // point: s01_e024's map is 407 visible primitives for 778 triangles, so
    // per-primitive batching meant 407 draws and 407 texture binds to move less
    // geometry than a single character model.
    struct MapBatchState
    {
      unsigned int texture = 0;
      bool cullDisabled = false;
      int blendMode = 0;
      bool valid = false;
    };

    // FUN_00211230:143-158, the block that turns the PRIM word's ABE bit on. It
    // reads the *base* material slot -- byte +0x0B for flags, +0x0A for alpha --
    // and picks the same 0..3 mode number the PSC3 path uses:
    //
    //   flags & 0x70 == 0            -> 0, opaque, ABE never enabled
    //   flags & 0x40                 -> 1, alpha blend; but alpha 0x80 is fully
    //                                   opaque, so it folds back to 0
    //   flags & 0x40 == 0, & 0x10    -> 3
    //   flags & 0x40 == 0, & 0x10==0 -> 2, additive
    //
    // Line 160 then sets `plVar8[1] |= 0x40` -- PRIM bit 6, ABE -- and line 161
    // records it on the primitive as 0x40, which is the flag
    // `psm2_material_expansion` was already computing and nothing was reading.
    //
    // Asked per slot, because the block sits inside FUN_00211230's slot loop and
    // each pass carries its own flags, alpha and UVs. The `& 0x70` block is also
    // inside the `type >= 0` arm, so an untextured slot never blends however its
    // flag byte reads -- true of every untextured slot in s01_e012 and s01_e024
    // anyway, but the original is what decides it.
    int mapBlendMode(const orphen::ported::psm2::MaterialSlot &slot)
    {
      if (g_mapBlendDisabled || !slot.textured() || (slot.flags & 0x70) == 0)
      {
        return 0;
      }
      if ((slot.flags & 0x40) != 0)
      {
        return slot.alpha == 0x80 ? 0 : 1;
      }
      return (slot.flags & 0x10) != 0 ? 3 : 2;
    }

    // The same GS reading the PSC3 path uses, because both feed VU1 programs
    // that select GS state by this mode number. See drawObjectModel's
    // setBlendMode for the derivation of each case.
    void setMapBlendMode(int mode)
    {
      switch (mode)
      {
      case 2:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        break;
      case 1:
      case 3:
        // Mode 3's reverse subtract needs glBlendEquation, which is not in the
        // fixed-function entry points this harness links. It falls back to
        // straight alpha so it reads as wrong rather than as missing.
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
    }

    // Flushes and clears the batch state, so whatever comes next starts clean.
    // Called before an entity is drawn: entities are interleaved into the map
    // by depth bucket and set their own GL state, so any map vertices still
    // accumulated have to go out first or they would be drawn after the entity
    // that was supposed to be in front of them.
    void flushMapBatch(MapBatchState &state)
    {
      batchFlush();
      if (state.cullDisabled)
      {
        glEnable(GL_CULL_FACE);
        state.cullDisabled = false;
      }
      if (state.blendMode != 0)
      {
        setMapBlendMode(0);
        state.blendMode = 0;
      }
      state.valid = false;
    }

    // One material slot of one primitive: the same geometry, this slot's texture
    // page, UVs, flat colour, alpha and blend mode. FUN_00211230's inner loop
    // builds exactly one GS packet per call of this.
    void drawPrimitiveSlot(const orphen::ported::psm2::Psm2RuntimeState &map,
                           const std::vector<unsigned int> &textureIds,
                           std::size_t primitiveIndex,
                           std::size_t slotIndex,
                           float alpha,
                           bool forceBlend,
                           bool cullingEnabled,
                           MapBatchState &state)
    {
      const std::optional<std::size_t> texturePage =
          texturePageForSlot(map, primitiveIndex, slotIndex);
      const bool hasTexture = texturePage.has_value() && *texturePage < textureIds.size() && textureIds[*texturePage] != 0;
      const unsigned int texture = hasTexture ? textureIds[*texturePage] : 0u;

      const bool modulate = !hasTexture;
      const auto &record80 = map.DAT_003556ac_dRecords80[primitiveIndex];
      const bool twoSided = (record80.primitiveFlags & kRecord80TwoSidedBit) != 0;
      const bool wantCullDisabled = cullingEnabled && twoSided;

      // FUN_00211230:143-158. The slot's own alpha rides in the vertex colour
      // alongside the occlusion fade, the way the GS gets it from the vertex
      // rather than from a register.
      const orphen::ported::psm2::MaterialSlot *slot =
          materialSlotForPrimitive(map, primitiveIndex, slotIndex);
      int blendMode = slot != nullptr ? mapBlendMode(*slot) : 0;
      if (blendMode != 0 && slot != nullptr)
      {
        // 0x80 is the GS's fully-opaque, so the divisor is 128 and not 255.
        alpha *= static_cast<float>(slot->alpha) / 128.0f;
      }
      // A map primitive held down by DAT_00355700 has to blend whatever its
      // material slot says, exactly the way a fading entity does two hundred
      // lines up -- and for the same reason: the fade rides in the vertex alpha
      // here, and an opaque primitive never reads it. Without this the chest
      // cutscene's fade cap of 3 was computed, emitted, folded into the vertex
      // colour and then thrown away, so the room the cutscene means to black
      // out stayed fully lit behind the item.
      if (blendMode == 0 && forceBlend)
      {
        blendMode = 1;
      }

      // The fade alpha rides in the vertex colour, so it is not part of the
      // key -- only the three things that are actual GL state are.
      if (!state.valid || state.texture != texture ||
          state.cullDisabled != wantCullDisabled || state.blendMode != blendMode)
      {
        batchFlush();

        if (state.blendMode != blendMode || !state.valid)
        {
          setMapBlendMode(blendMode);
          state.blendMode = blendMode;
        }

        if (texture != 0)
        {
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, texture);
        }
        else
        {
          glDisable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (wantCullDisabled != state.cullDisabled)
        {
          if (wantCullDisabled)
          {
            glDisable(GL_CULL_FACE);
          }
          else
          {
            glEnable(GL_CULL_FACE);
          }
        }

        state.texture = texture;
        state.cullDisabled = wantCullDisabled;
        state.valid = true;
        batchReset(texture != 0);

        if (g_renderStats != nullptr)
        {
          ++g_renderStats->mapBatches;
          ++g_renderStats->mapTextureBinds;
        }
      }

      if (g_renderStats != nullptr)
      {
        g_renderStats->mapTriangles += record80.triangleCount;
      }

      for (std::size_t offset = 0; offset < record80.triangleCount; ++offset)
      {
        const auto &triangle = map.derivedTriangles[record80.firstTriangle + offset];
        emitTexturedVertex(map, triangle, 0, slotIndex, alpha, modulate);
        emitTexturedVertex(map, triangle, 1, slotIndex, alpha, modulate);
        emitTexturedVertex(map, triangle, 2, slotIndex, alpha, modulate);
      }
    }

    // FUN_00211230:104-360. The slot loop, and the reason a curtain that should
    // read as sheer white read as the pattern printed on its base layer: the
    // original draws **a pass per present material slot**, not just slot 0.
    // `-2 < type` (line 111) is the same test as MaterialSlot::present(), so an
    // untextured slot still gets its pass -- it just carries a flat colour
    // instead of a page.
    //
    // s01_e012 leans on this: 202 of its 3948 primitives carry two slots and 68
    // carry three. Drawing only the first left the second and third layers --
    // most of them additive at f=0x20 -- off the screen entirely.
    void drawPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map,
                       const std::vector<unsigned int> &textureIds,
                       std::size_t primitiveIndex,
                       float alpha,
                       bool forceBlend,
                       bool cullingEnabled,
                       MapBatchState &state)
    {
      const auto &record80 = map.DAT_003556ac_dRecords80[primitiveIndex];
      if (g_renderStats != nullptr)
      {
        ++g_renderStats->mapPrimitives;
      }

      for (std::size_t slotIndex = 0; slotIndex < record80.materialSlots.size(); ++slotIndex)
      {
        if (!record80.materialSlots[slotIndex].present())
        {
          continue;
        }
        drawPrimitiveSlot(map, textureIds, primitiveIndex, slotIndex, alpha, forceBlend,
                          cullingEnabled, state);
        if (g_mapBaseSlotOnly)
        {
          break;
        }
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
      MapBatchState batchState;
      std::size_t entityCursor = 0;
      const auto drawEntitiesUpTo = [&](int bucket) {
        if (entityCursor < entityDrawList.size() &&
            entityDrawList[entityCursor].depthBucket <= bucket)
        {
          flushMapBatch(batchState);
        }
        while (entityCursor < entityDrawList.size() &&
               entityDrawList[entityCursor].depthBucket <= bucket)
        {
          // Timed here rather than inside drawObjectModel so the map's own cost
          // can be had by subtraction: the two are interleaved by depth bucket
          // and cannot be timed as separate spans.
          PhaseTimer timer(g_renderStats != nullptr ? &g_renderStats->entityDrawMicros
                                                    : nullptr);
          drawObjectModel(objects[entityDrawList[entityCursor].viewIndex], slotTextures);
          ++entityCursor;
        }
      };

      for (const auto &item : drawList)
      {
        drawEntitiesUpTo(item.depthBucket);
        drawPrimitive(map, textureIds, item.primitiveIndex, alphaForFade(item.fade),
                      item.globalFadeCapped, cullingEnabled, batchState);
      }
      // Anything nearer than the last map primitive, plus the blended bucket.
      drawEntitiesUpTo(orphen::ported::render::entityDraw::kBlendedBucket);
      flushMapBatch(batchState);

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
    }

    // The whole map, unsorted and opaque. Used by the free-fly viewer, which
    // has no player and so no occlusion fade to compute.
    void drawMapUnsorted(const orphen::ported::psm2::Psm2RuntimeState &map, const std::vector<unsigned int> &textureIds)
    {
      MapBatchState batchState;
      for (std::size_t primitiveIndex = 0; primitiveIndex < map.DAT_003556ac_dRecords80.size(); ++primitiveIndex)
      {
        if ((map.DAT_003556ac_dRecords80[primitiveIndex].primitiveFlags & kRecord80HiddenBit) != 0)
        {
          continue;
        }
        drawPrimitive(map, textureIds, primitiveIndex, 1.0f, false, false, batchState);
      }
      flushMapBatch(batchState);

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

  void MapViewer::setScreenFadeOverlay(std::uint32_t packedRgb, std::uint8_t alpha)
  {
    screenFadeRgb_ = packedRgb;
    screenFadeAlpha_ = alpha;
  }

  void MapViewer::setDialogueSprites(std::vector<orphen::ported::text::DialogueSprite> sprites)
  {
    dialogueSprites_ = std::move(sprites);
  }

  MapViewer::ScreenFit MapViewer::originalScreenFit(int framebufferWidth, int framebufferHeight) const
  {
    namespace debugText = orphen::ported::debug::text;
    // The free viewer has no 4:3 box -- the whole window is its picture.
    const bool useOriginalCamera = leadPlayerView_.has_value() && renderCamera_.has_value();
    const ViewportRect view = useOriginalCamera
                                  ? gameViewportRect(framebufferWidth, framebufferHeight)
                                  : ViewportRect{0, 0, framebufferWidth, framebufferHeight};

    ScreenFit fit;
    // The rect is in GL's bottom-up window space; these overlays draw under a
    // top-left ortho, so flip the origin over.
    fit.offsetX = static_cast<float>(view.x);
    fit.offsetY = static_cast<float>(framebufferHeight - (view.y + view.height));
    fit.scaleX = static_cast<float>(view.width) / debugText::kScreenWidth;
    fit.scaleY = static_cast<float>(view.height) / debugText::kScreenHeight;
    return fit;
  }

  // FUN_0025cfb8's two sprites: entry x = -320 and width 640, so the full width
  // of the screen, and a height of `iGpffffbd8c >> 5` each. The bottom one is
  // entry y = height - 224 (screen y 448 - height) and the top one entry y = 224
  // (screen y 0). Untextured, blending off, colour 0xFF000000 -- flat black.
  void MapViewer::drawLetterboxBars(int framebufferWidth, int framebufferHeight) const
  {
    if (letterboxBarHeight_ <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0)
    {
      return;
    }

    namespace debugText = orphen::ported::debug::text;
    const ScreenFit fit = originalScreenFit(framebufferWidth, framebufferHeight);
    const float height = static_cast<float>(letterboxBarHeight_) * fit.scaleY;
    const float left = fit.offsetX;
    const float right = fit.offsetX + debugText::kScreenWidth * fit.scaleX;
    const float topBarBottom = fit.offsetY + height;
    const float bottomBarTop = fit.offsetY + debugText::kScreenHeight * fit.scaleY - height;
    const float bottomBarBottom = fit.offsetY + debugText::kScreenHeight * fit.scaleY;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(framebufferWidth), static_cast<double>(framebufferHeight), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean fogWasEnabled = glIsEnabled(GL_FOG);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);

    glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(left, fit.offsetY);
    glVertex2f(right, fit.offsetY);
    glVertex2f(right, topBarBottom);
    glVertex2f(left, topBarBottom);

    glVertex2f(left, bottomBarTop);
    glVertex2f(right, bottomBarTop);
    glVertex2f(right, bottomBarBottom);
    glVertex2f(left, bottomBarBottom);
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (blendWasEnabled == GL_TRUE)
    {
      glEnable(GL_BLEND);
    }
    if (fogWasEnabled == GL_TRUE)
    {
      glEnable(GL_FOG);
    }
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

  void MapViewer::drawDialogueSprites(int framebufferWidth, int framebufferHeight) const
  {
    if (textureSlots_ == nullptr || framebufferWidth <= 0 || framebufferHeight <= 0)
    {
      return;
    }

    // The same fit the bars and the ported debug overlay use, so the 0x1E
    // FUN_00238a08 lifts a cutscene line by lands where the bar edge is.
    const ScreenFit fit = originalScreenFit(framebufferWidth, framebufferHeight);
    const float offsetX = fit.offsetX;
    const float offsetY = fit.offsetY;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(framebufferWidth), static_cast<double>(framebufferHeight), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean fogWasEnabled = glIsEnabled(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The entry colour at +0x30 goes straight into RGBAQ, where 0x80 is x1.0
    // through the GS's (Ct * Cv) >> 7. Most entries carry 0x80808080, plain
    // white; the speaker's name is 0x80606000.
    const auto submitColor = [](std::uint32_t packed) {
      const float scale = 1.0f / 128.0f;
      glColor4f(static_cast<float>(packed & 0xFF) * scale,
                static_cast<float>((packed >> 8) & 0xFF) * scale,
                static_cast<float>((packed >> 16) & 0xFF) * scale,
                static_cast<float>((packed >> 24) & 0xFF) * scale);
    };

    unsigned int boundTexture = 0;
    std::uint32_t submittedColor = 0;
    for (const auto &sprite : dialogueSprites_)
    {
      const auto slot = static_cast<std::size_t>(sprite.textureSlot);
      if (slot >= slotTextureIds_.size() || slotTextureIds_[slot] == 0)
      {
        continue;
      }
      const auto &texture = textureSlots_->slot(slot).texture;
      if (texture.width == 0 || texture.height == 0)
      {
        continue;
      }

      if (slotTextureIds_[slot] != boundTexture)
      {
        boundTexture = slotTextureIds_[slot];
        glBindTexture(GL_TEXTURE_2D, boundTexture);
      }
      if (sprite.color != submittedColor)
      {
        submittedColor = sprite.color;
        submitColor(submittedColor);
      }

      const float u0 = static_cast<float>(sprite.u) / texture.width;
      const float u1 = static_cast<float>(sprite.u + sprite.sourceWidth) / texture.width;
      const float v0 = static_cast<float>(sprite.v) / texture.height;
      const float v1 = static_cast<float>(sprite.v + sprite.sourceHeight) / texture.height;

      const float left = offsetX + sprite.x * fit.scaleX;
      const float top = offsetY + sprite.y * fit.scaleY;
      const float right = left + sprite.width * fit.scaleX;
      const float bottom = top + sprite.height * fit.scaleY;

      glBegin(GL_QUADS);
      glTexCoord2f(u0, v0);
      glVertex2f(left, top);
      glTexCoord2f(u1, v0);
      glVertex2f(right, top);
      glTexCoord2f(u1, v1);
      glVertex2f(right, bottom);
      glTexCoord2f(u0, v1);
      glVertex2f(left, bottom);
      glEnd();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    if (fogWasEnabled == GL_TRUE)
    {
      glEnable(GL_FOG);
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

  void MapViewer::setOriginalDebugGlyphs(std::vector<orphen::ported::debug::DebugGlyph> glyphs)
  {
    originalDebugGlyphs_ = std::move(glyphs);
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

  std::size_t MapViewer::dumpTexturePages(const std::filesystem::path &directory) const
  {
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    std::size_t written = 0;
    for (std::size_t pageIndex = 0; pageIndex < texturePages_.size(); ++pageIndex)
    {
      const auto &page = texturePages_[pageIndex];
      const auto &texture = page.texture;
      if (texture.rgbaPixels.empty())
      {
        continue;
      }

      std::filesystem::path path =
          directory / ("page" + std::to_string(pageIndex) + "_tex_" + page.resourceIdHex + ".pam");
      std::ofstream output(path, std::ios::binary);
      if (!output)
      {
        continue;
      }
      output << "P7\nWIDTH " << texture.width << "\nHEIGHT " << texture.height
             << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
      output.write(reinterpret_cast<const char *>(texture.rgbaPixels.data()),
                   static_cast<std::streamsize>(texture.rgbaPixels.size()));
      if (output.good())
      {
        ++written;
        std::cout << "[textures] page " << pageIndex << " tex_" << page.resourceIdHex << " "
                  << texture.width << "x" << texture.height << " -> " << path.string() << '\n';
      }
    }

    // The entity side of the same question. Map pages are indexed by a material
    // slot's type byte; entity models go through the resident slot cache
    // instead, so a texture can be present in one and absent from the other.
    if (textureSlots_ != nullptr)
    {
      for (std::size_t slot = 0; slot < orphen::ported::resource::kTextureSlotCount; ++slot)
      {
        const auto &state = textureSlots_->slot(slot);
        if (state.texture.rgbaPixels.empty())
        {
          continue;
        }
        std::ostringstream name;
        name << "slot" << std::setw(2) << std::setfill('0') << slot << "_tex_" << std::hex
             << std::setw(4) << std::setfill('0') << state.DAT_003429a8_residentId << ".pam";
        std::filesystem::path path = directory / name.str();
        std::ofstream output(path, std::ios::binary);
        if (!output)
        {
          continue;
        }
        output << "P7\nWIDTH " << state.texture.width << "\nHEIGHT " << state.texture.height
               << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
        output.write(reinterpret_cast<const char *>(state.texture.rgbaPixels.data()),
                     static_cast<std::streamsize>(state.texture.rgbaPixels.size()));
        if (output.good())
        {
          ++written;
          std::cout << "[textures] slot " << slot << " " << name.str() << '\n';
        }
      }
    }
    return written;
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
      // REPEAT, not clamp: a UV animation track scrolls its offset across a
      // whole 256-texel page and relies on wrapping round. Filtering is
      // GL_NEAREST and every static primitive's coordinates sit inside 0..1,
      // where the two modes sample identically, so this changes nothing else.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
      // REPEAT, not clamp: a UV animation track scrolls its offset across a
      // whole 256-texel page and relies on wrapping round. Filtering is
      // GL_NEAREST and every static primitive's coordinates sit inside 0..1,
      // where the two modes sample identically, so this changes nothing else.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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

  // The 4:3 box the game's picture occupies inside the window, and the bars
  // around it. render() sets the 3D viewport to this and the fade quad covers
  // exactly the same rectangle -- FUN_0025d0e0's sprite is drawn in GS screen
  // space, so it covers the picture and nothing else. Letting it cover the
  // whole window whited out the bars too, which the original has no way to do.
  MapViewer::ViewportRect MapViewer::gameViewportRect(int framebufferWidth, int framebufferHeight)
  {
    const float aspect = orphen::ported::render::constants::kDisplayAspect;
    int width = framebufferWidth;
    int height = static_cast<int>(static_cast<float>(framebufferWidth) / aspect + 0.5f);
    if (height > framebufferHeight)
    {
      height = framebufferHeight;
      width = static_cast<int>(static_cast<float>(framebufferHeight) * aspect + 0.5f);
    }
    return {(framebufferWidth - width) / 2, (framebufferHeight - height) / 2, width, height};
  }

  // The source half of FUN_00201a38: keep a copy of the frame the player is
  // looking at, so the next one can blend it back in.
  //
  // The original gets this for free -- it names the other framebuffer in TEX0
  // and the GS reads it in place. There is no equivalent here, so the game's
  // picture is copied out of the back buffer once per presented frame.
  //
  void MapViewer::captureFrameFeedbackSource(const ViewportRect &gameView) const
  {
    if (screenSmearDisabled_ || gameView.width <= 0 || gameView.height <= 0)
    {
      return;
    }

    const auto nextPowerOfTwo = [](int value) {
      int result = 1;
      while (result < value)
      {
        result <<= 1;
      }
      return result;
    };
    const int wantWidth = nextPowerOfTwo(gameView.width);
    const int wantHeight = nextPowerOfTwo(gameView.height);

    if (frameFeedbackTexture_ == 0)
    {
      GLuint texture = 0;
      glGenTextures(1, &texture);
      frameFeedbackTexture_ = texture;
      frameFeedbackTextureWidth_ = 0;
      frameFeedbackTextureHeight_ = 0;
    }

    glBindTexture(GL_TEXTURE_2D, frameFeedbackTexture_);
    if (wantWidth != frameFeedbackTextureWidth_ || wantHeight != frameFeedbackTextureHeight_)
    {
      // RGBA, not RGB. The back buffer is RGBA8, and a three-channel target
      // makes glCopyTexSubImage2D convert every pixel on the way in. Measured
      // on s01_e024 at 1280x960, --render-bench 8: 4.0 ms a render with no
      // capture at all, 4.4 ms with this, 5.15 ms with GL_RGB.
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wantWidth, wantHeight, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      // The transformed quad can reach past the source rectangle. TEX0's CLAMP
      // is cleared to REPEAT by FUN_00201a38, but the original's texture *is*
      // the framebuffer and its wrap lands on the neighbouring page rather than
      // on the opposite edge of the picture; clamping is the closer of the two
      // wrong answers, and it is what a zoom-in never reaches anyway.
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      frameFeedbackTextureWidth_ = wantWidth;
      frameFeedbackTextureHeight_ = wantHeight;
    }

    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gameView.x, gameView.y, gameView.width,
                        gameView.height);
    frameFeedbackCapturedWidth_ = gameView.width;
    frameFeedbackCapturedHeight_ = gameView.height;
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // The draw half. One alpha-blended textured quad over the game's picture,
  // with the vertex positions FUN_00201a38 computed and the source rectangle it
  // never moves.
  void MapViewer::drawFrameFeedbackQuad(const ViewportRect &gameView) const
  {
    if (screenSmearDisabled_ || !frameFeedbackQuad_.has_value() || frameFeedbackTexture_ == 0 ||
        frameFeedbackCapturedWidth_ <= 0 || frameFeedbackCapturedHeight_ <= 0 ||
        gameView.width <= 0 || gameView.height <= 0)
    {
      return;
    }

    const orphen::ported::render::FeedbackQuad &quad = *frameFeedbackQuad_;

    // The quad's UVs are in the original's 640x224 frame and the capture is the
    // whole of that frame, so the first step is a plain ratio. The second is
    // the used fraction of the power-of-two texture.
    //
    // v flips. GS v counts down from the top row; a texture copied out of the
    // back buffer has its origin at the bottom.
    const float usedU = static_cast<float>(frameFeedbackCapturedWidth_) /
                        static_cast<float>(frameFeedbackTextureWidth_);
    const float usedV = static_cast<float>(frameFeedbackCapturedHeight_) /
                        static_cast<float>(frameFeedbackTextureHeight_);
    const auto textureU = [usedU](float u) {
      return (u / orphen::ported::render::kFeedbackScreenWidth) * usedU;
    };
    const auto textureV = [usedV](float v) {
      return (1.0f - v / orphen::ported::render::kFeedbackScreenHeight) * usedV;
    };

    // Scissored to the game's picture for the reason the fade quad is: a
    // rotated or zoomed-out quad would otherwise spill the picture onto the
    // letterbox bars, which are outside the frame the GS sprite lives in.
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint previousScissor[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glEnable(GL_SCISSOR_TEST);
    glScissor(gameView.x, gameView.y, gameView.width, gameView.height);

    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glViewport(gameView.x, gameView.y, gameView.width, gameView.height);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    // Straight into the original's own units: x across 640, y down 224.
    glOrtho(0.0, orphen::ported::render::kFeedbackScreenWidth,
            orphen::ported::render::kFeedbackScreenHeight, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean fogWasEnabled = glIsEnabled(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    // ALPHA_1 = 0x44 is (Cs - Cd) * As + Cd, which is this.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, frameFeedbackTexture_);

    // RGBAQ is `alpha << 24 | 0x808080` under MODULATE, and 0x80 is unity on
    // the GS -- so the colour is the texture's own and only alpha does work.
    glColor4f(1.0f, 1.0f, 1.0f, quad.blendFactor);
    glBegin(GL_TRIANGLE_FAN);
    for (const auto &vertex : quad.vertices)
    {
      glTexCoord2f(textureU(vertex.u), textureV(vertex.v));
      glVertex2f(vertex.x, vertex.y);
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    if (fogWasEnabled == GL_TRUE)
    {
      glEnable(GL_FOG);
    }
    if (depthWasEnabled == GL_TRUE)
    {
      glEnable(GL_DEPTH_TEST);
    }

    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    if (scissorWasEnabled != GL_TRUE)
    {
      glDisable(GL_SCISSOR_TEST);
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
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
      // Yaw and pan only. The pitch and zoom controls this used to have were
      // bound to I/K and Q/E, which are not pad buttons and were only ever the
      // old map viewer's; `R` still resets the camera to its default framing.
      cameraYawDegrees_ += input.rotateX * kYawSpeed * deltaSeconds;
      const float panDistance = std::max(cameraDistance_, 10.0f) * kPanSpeed * deltaSeconds;
      const auto basis = viewerGroundBasis(cameraYawDegrees_);

      cameraTarget_.x += (basis.right.x * input.moveX + basis.forward.x * input.moveY) * panDistance;
      cameraTarget_.z += (basis.right.z * input.moveX + basis.forward.z * input.moveY) * panDistance;
    }
  }

  void MapViewer::render(int framebufferWidth, int framebufferHeight) const
  {
    const auto prologueStart = std::chrono::steady_clock::now();
    ensureTexturesUploaded();
    ensureSlotTexturesUploaded();
    lastFramebufferWidth_ = framebufferWidth;
    lastFramebufferHeight_ = framebufferHeight;
    g_sceneLighting = &sceneLighting_;
    g_gleamProbes = gleamProbeSink_;
    g_renderStats = renderStatsSink_;
    g_mapBlendDisabled = mapBlendDisabled_;
    g_mapBaseSlotOnly = mapBaseSlotOnly_;
    g_entityBoundTextureOnly = entityBoundTextureOnly_;
    if (g_gleamProbes != nullptr)
    {
      g_gleamProbes->clear();
    }
    if (g_renderStats != nullptr)
    {
      ++g_renderStats->frames;
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
      const ViewportRect view = gameViewportRect(framebufferWidth, framebufferHeight);
      const int viewX = view.x;
      const int viewY = view.y;
      const int viewWidth = view.width;
      const int viewHeight = view.height;
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
                                                              orphen::ported::render::constants::kGeometryNearClip,
                                                              drawDistance_ + kFarPlaneMargin);
      glMatrixMode(GL_PROJECTION);
      glLoadMatrixf(camera.projection.data());
      glMatrixMode(GL_MODELVIEW);
      glLoadMatrixf(camera.modelView.data());

      // Keep the copies we just uploaded rather than reading them back. A
      // glGetFloatv is a sync point -- the driver has to finish everything
      // queued before it can answer -- and the pair below measured 7.05 ms per
      // frame, 42% of the whole frame, on a scene that draws 13k triangles.
      // Nothing was learned for it: these are exactly the matrices glCameraFor
      // just returned.
      probeModelView_ = camera.modelView;
      probeProjection_ = camera.projection;
      probeMatricesValid_ = true;
    }
    else
    {
      setPerspective(framebufferWidth, framebufferHeight, 60.0f, cameraDistance_ * 8.0f);
      applyCamera(cameraTarget_, cameraDistance_, cameraYawDegrees_, cameraPitchDegrees_);

      // The free viewer builds its matrices through glFrustum and glRotatef, so
      // there is no CPU-side copy to keep and the readback stays. It costs the
      // same sync, but this path has no game camera to be fast for.
      PhaseTimer timer(g_renderStats != nullptr ? &g_renderStats->matrixReadMicros : nullptr);
      glGetFloatv(GL_MODELVIEW_MATRIX, probeModelView_.data());
      glGetFloatv(GL_PROJECTION_MATRIX, probeProjection_.data());
      probeMatricesValid_ = true;
    }

    // Snapshot what the frame is actually being drawn with, so probeAt can
    // build its ray from the same matrices instead of a reconstruction.
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

    if (g_renderStats != nullptr)
    {
      g_renderStats->prologueMicros += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - prologueStart)
              .count());
    }

    // Sorted here rather than on the simulation step, because unlike the map's
    // fade byte nothing about it is per-frame state -- it is a pure function of
    // the camera and the entity positions, and it must not run headless.
    std::vector<orphen::ported::render::EntityDrawItem> entityDrawList;
    if (renderCamera_.has_value() && !sceneObjectViews_.empty())
    {
      PhaseTimer timer(g_renderStats != nullptr ? &g_renderStats->entityListMicros : nullptr);
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
        // The two are interleaved by depth bucket, so the map's own cost is the
        // span minus whatever the entity timer collected inside it.
        const std::uint64_t entityBefore =
            g_renderStats != nullptr ? g_renderStats->entityDrawMicros : 0;
        const auto spanStart = std::chrono::steady_clock::now();

        drawMap(*map_, uploadedTextureIds_, mapDrawList_, useOriginalCamera && !wireframe_,
                sceneObjectViews_, entityDrawList, slotTextureIds_);

        if (g_renderStats != nullptr)
        {
          const auto spanMicros = static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - spanStart)
                  .count());
          const std::uint64_t entitySpan = g_renderStats->entityDrawMicros - entityBefore;
          g_renderStats->mapDrawMicros += spanMicros > entitySpan ? spanMicros - entitySpan : 0;
        }
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
      PhaseTimer timer(g_renderStats != nullptr ? &g_renderStats->overlayMicros : nullptr);
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

    // The 4:3 box the game's picture occupies. Both halves of the smear work in
    // it: the quad is drawn inside it and the next frame's source is copied out
    // of it.
    const ViewportRect gameView =
        useOriginalCamera ? gameViewportRect(framebufferWidth, framebufferHeight)
                          : ViewportRect{0, 0, framebufferWidth, framebufferHeight};

    // FUN_00201a38, sort bucket 0x1006: over the world, under the bars and the
    // fade below.
    drawFrameFeedbackQuad(gameView);

    // FUN_0025cfb8's bars. They and the fade share GS sort bucket 0x1007, and
    // both FUN_002239c8 and FUN_00224320 submit the fade first -- insertion is
    // LIFO within a bucket, so the bars are the earlier draw and the fade tints
    // them. Every text overlay is bucket 0x1009 and lands on top of both.
    drawLetterboxBars(framebufferWidth, framebufferHeight);

    if (screenFadeAlpha_ != 0)
    {
      // FUN_0025d0e0's quad. It covers the scene, so it goes down before the
      // debug overlays -- the original's debug text is drawn by FUN_00268270
      // after the fade for the same reason.
      //
      // **Only over the game's picture.** The original's sprite is a GS
      // primitive inside the 640x224 frame; there is nothing outside that frame
      // for it to cover. The port's frame is the letterboxed 4:3 box, so the
      // quad is scissored to it and the bars stay the window's own colour --
      // drawn full-window, the fade to white washed the bars out too.
      const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
      GLint previousScissor[4] = {0, 0, 0, 0};
      glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
      glEnable(GL_SCISSOR_TEST);
      glScissor(gameView.x, gameView.y, gameView.width, gameView.height);

      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();

      const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
      const GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);
      const GLboolean fogWasEnabled = glIsEnabled(GL_FOG);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_TEXTURE_2D);
      glDisable(GL_FOG);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      glColor4f((screenFadeRgb_ & 0xFF) / 255.0f,
                ((screenFadeRgb_ >> 8) & 0xFF) / 255.0f,
                ((screenFadeRgb_ >> 16) & 0xFF) / 255.0f,
                screenFadeAlpha_ / 255.0f);
      glBegin(GL_QUADS);
      glVertex2f(0.0f, 0.0f);
      glVertex2f(1.0f, 0.0f);
      glVertex2f(1.0f, 1.0f);
      glVertex2f(0.0f, 1.0f);
      glEnd();
      glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

      if (fogWasEnabled == GL_TRUE)
      {
        glEnable(GL_FOG);
      }
      if (textureWasEnabled == GL_TRUE)
      {
        glEnable(GL_TEXTURE_2D);
      }
      if (depthWasEnabled == GL_TRUE)
      {
        glEnable(GL_DEPTH_TEST);
      }
      glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
      if (scissorWasEnabled != GL_TRUE)
      {
        glDisable(GL_SCISSOR_TEST);
      }

      glMatrixMode(GL_MODELVIEW);
      glPopMatrix();
      glMatrixMode(GL_PROJECTION);
      glPopMatrix();
      glMatrixMode(GL_MODELVIEW);
    }

    if (!dialogueSprites_.empty())
    {
      drawDialogueSprites(framebufferWidth, framebufferHeight);
    }

    // The game's picture is finished here, so this is the frame the next one
    // samples: world, smear, bars, fade and subtitles, and nothing from the
    // harness. On the console the buffer would also hold FUN_00268270's debug
    // text, but the port's overlays are the harness's own and would smear a HUD
    // the game never drew.
    //
    // Note the ordering: the capture happens *after* this frame's own quad, so
    // each frame samples a picture that already contains the last blend. That
    // compounding is the effect -- capture before the quad and it degrades to a
    // one-frame ghost.
    //
    // Unconditional, not gated on the effect being up. The console always has
    // the other framebuffer sitting there, so the frame a script arms the smear
    // on already has its source; gating the copy on the quad would leave that
    // first frame with nothing to sample, or worse, with whatever the last
    // burst left in the texture.
    captureFrameFeedbackSource(gameView);

    {
      PhaseTimer timer(g_renderStats != nullptr ? &g_renderStats->hudMicros : nullptr);
      // FUN_00268270's output first, because it owns the original's own
      // corner of the screen; the harness HUD then stacks underneath it.
      //
      // The atlas is texture slot 0x30, already resident and already uploaded
      // by ensureSlotTexturesUploaded -- FUN_00221fd8 binds it at boot and the
      // model store reproduces that bind.
      GLuint fontTexture = 0;
      int fontWidth = 0;
      int fontHeight = 0;
      if (textureSlots_ != nullptr &&
          static_cast<std::size_t>(orphen::ported::debug::text::kFontTextureSlot) < slotTextureIds_.size())
      {
        fontTexture = slotTextureIds_[orphen::ported::debug::text::kFontTextureSlot];
        const auto &slotState = textureSlots_->slot(orphen::ported::debug::text::kFontTextureSlot);
        fontWidth = slotState.texture.width;
        fontHeight = slotState.texture.height;
      }
      const ScreenFit fit = originalScreenFit(framebufferWidth, framebufferHeight);
      const float overlayBottom = debugText_.drawOriginalOverlay(
          framebufferWidth, framebufferHeight, fit.offsetX, fit.offsetY, fit.scaleX, fit.scaleY,
          originalDebugGlyphs_, fontTexture, fontWidth, fontHeight);
      if (hudVisible_)
      {
        debugText_.draw(framebufferWidth, framebufferHeight, hudLines_, 11.0f, overlayBottom);
      }
    }

    if (g_renderStats != nullptr)
    {
      // Immediate mode only queues; the driver translates and submits later, so
      // without this the cost of a draw shows up in whatever call the queue
      // fills on -- which is why the phase timings summed to a third of the
      // measured render time before this existed.
      PhaseTimer timer(&g_renderStats->gpuDrainMicros);
      glFinish();
    }

    g_renderStats = nullptr;
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
