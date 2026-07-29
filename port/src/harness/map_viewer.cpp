#include "harness/map_viewer.h"

#include "harness/scene_resource_tree.h"
#include "ported/psm2/decoded_psm2_loader.h"
#include "ported/psc3/psc3_runtime.h"

#include <SDL_opengl.h>

#include <algorithm>
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

    void drawDebugPlayerProbe(const orphen::port::PlayerDebugProbeState &probe)
    {
      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glLineWidth(3.0f);

      if (probe.groundHit.has_value())
      {
        glColor3f(1.0f, 0.86f, 0.18f);
        glBegin(GL_LINE_LOOP);
        for (const auto &vertex : probe.groundHit->vertices)
        {
          const auto viewerVertex = toViewerSpace(vertex);
          glVertex3f(viewerVertex.x, viewerVertex.y + 0.05f, viewerVertex.z);
        }
        glEnd();
      }

      const auto foot = toViewerSpace(probe.position);
      const float bodyHeight = 1.25f;
      const float bodyHalfWidth = 0.35f;
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
      glVertex3f(foot.x + std::cos(probe.facingRadians) * facingLength,
             foot.y + bodyHeight * 0.45f,
                 foot.z - std::sin(probe.facingRadians) * facingLength);
      glEnd();

      glLineWidth(1.0f);
    }

    orphen::ported::psm2::Vec3 psc3GalleryVertex(const LoadedSceneModel &sceneModel,
                                                 const orphen::ported::psm2::Vec3 &localVertex)
    {
      const auto &bounds = sceneModel.model.bounds;
      const orphen::ported::psm2::Vec3 center{(bounds.min.x + bounds.max.x) * 0.5f,
                                              (bounds.min.y + bounds.max.y) * 0.5f,
                                              0.0f};
      return {localVertex.x - center.x + sceneModel.galleryOffset.x,
              localVertex.y - center.y + sceneModel.galleryOffset.y,
              localVertex.z - bounds.min.z + sceneModel.galleryOffset.z};
    }

    void emitSceneModelVertex(const LoadedSceneModel &sceneModel, const orphen::ported::psm2::Vec3 &localVertex)
    {
      const auto viewerVertex = toViewerSpace(psc3GalleryVertex(sceneModel, localVertex));
      glVertex3f(viewerVertex.x, viewerVertex.y, viewerVertex.z);
    }

    void drawSceneModelGallery(const std::vector<LoadedSceneModel> &sceneModels)
    {
      if (sceneModels.empty())
      {
        return;
      }

      glDisable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, 0);
      glLineWidth(1.0f);
      glBegin(GL_LINES);

      for (std::size_t modelIndex = 0; modelIndex < sceneModels.size(); ++modelIndex)
      {
        const auto &sceneModel = sceneModels[modelIndex];
        const float colorMix = static_cast<float>(modelIndex % 13) / 12.0f;
        glColor3f(0.25f + colorMix * 0.45f, 0.72f, 0.95f - colorMix * 0.35f);

        for (const auto &triangle : sceneModel.model.triangles)
        {
          emitSceneModelVertex(sceneModel, triangle.vertices[0]);
          emitSceneModelVertex(sceneModel, triangle.vertices[1]);
          emitSceneModelVertex(sceneModel, triangle.vertices[1]);
          emitSceneModelVertex(sceneModel, triangle.vertices[2]);
          emitSceneModelVertex(sceneModel, triangle.vertices[2]);
          emitSceneModelVertex(sceneModel, triangle.vertices[0]);
        }
      }

      glEnd();
    }

    void emitVertex(const orphen::ported::psm2::Psm2RuntimeState &map, std::uint16_t vertexIndex)
    {
      const auto &source = map.DAT_0035569c_sectionCRecords.at(vertexIndex).position;
      const auto viewerPosition = toViewerSpace(source);
      glVertex3f(viewerPosition.x, viewerPosition.y, viewerPosition.z);
    }

    std::optional<std::size_t> texturePageForPrimitive(const orphen::ported::psm2::Psm2RuntimeState &map, std::size_t primitiveIndex)
    {
      if (primitiveIndex >= map.DAT_003556ac_dRecords80.size())
      {
        return std::nullopt;
      }

      const std::uint16_t sectionEIndex = map.DAT_003556ac_dRecords80[primitiveIndex].sectionEIndex;
      if (sectionEIndex >= 0x8000 || static_cast<std::size_t>(sectionEIndex) >= map.DAT_003556b4_sectionERecords.size())
      {
        return std::nullopt;
      }

      return map.DAT_003556b4_sectionERecords[sectionEIndex].bytes[8];
    }

    std::pair<float, float> textureCoordinateForCorner(const orphen::ported::psm2::Psm2RuntimeState &map,
                                                       const orphen::ported::psm2::TriangleRecord &triangle,
                                                       std::size_t triangleCornerIndex)
    {
      if (triangle.primitiveIndex >= map.DAT_003556ac_dRecords80.size())
      {
        return {0.0f, 0.0f};
      }

      const std::uint16_t sectionEIndex = map.DAT_003556ac_dRecords80[triangle.primitiveIndex].sectionEIndex;
      if (sectionEIndex >= 0x8000 || static_cast<std::size_t>(sectionEIndex) >= map.DAT_003556b4_sectionERecords.size())
      {
        return {0.0f, 0.0f};
      }

      const std::uint8_t sourceCorner = triangle.cornerIndices[triangleCornerIndex] & 3;
      const auto &record = map.DAT_003556b4_sectionERecords[sectionEIndex];
      return {static_cast<float>(record.bytes[sourceCorner * 2]) / 256.0f,
              static_cast<float>(record.bytes[sourceCorner * 2 + 1]) / 256.0f};
    }

    void emitTexturedVertex(const orphen::ported::psm2::Psm2RuntimeState &map,
                            const orphen::ported::psm2::TriangleRecord &triangle,
                            std::size_t triangleCornerIndex)
    {
      const auto [u, v] = textureCoordinateForCorner(map, triangle, triangleCornerIndex);
      glTexCoord2f(u, v);
      emitVertex(map, triangle.vertexIndices[triangleCornerIndex]);
    }

    void drawMap(const orphen::ported::psm2::Psm2RuntimeState &map, const std::vector<unsigned int> &textureIds)
    {
      for (const auto &triangle : map.derivedTriangles)
      {
        const std::optional<std::size_t> texturePage = texturePageForPrimitive(map, triangle.primitiveIndex);
        const bool hasTexture = texturePage.has_value() && *texturePage < textureIds.size() && textureIds[*texturePage] != 0;

        if (hasTexture)
        {
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, textureIds[*texturePage]);
          glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else
        {
          glDisable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D, 0);
        }

        glBegin(GL_TRIANGLES);
        const float shade = 0.35f + 0.35f * static_cast<float>(triangle.primitiveIndex % 17) / 16.0f;
        if (!hasTexture)
        {
          glColor3f(0.22f + shade * 0.35f, 0.42f + shade * 0.25f, 0.50f + shade * 0.30f);
        }
        emitTexturedVertex(map, triangle, 0);
        emitTexturedVertex(map, triangle, 1);
        emitTexturedVertex(map, triangle, 2);
        glEnd();
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
    sceneModels_.clear();
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
    loadSceneModels();
    loadedSourceDescription_ = sceneName(selection) + " map_" + loadedMap.resourceIdHex;
    currentDiscSceneIndex_ = sceneIndex;
    ++loadedMapGeneration_;
    resetCamera();
  }

  void MapViewer::loadSceneModels()
  {
    sceneModels_.clear();
    if (!sceneResources_.has_value())
    {
      return;
    }

    std::size_t skippedPsc3Count = 0;
    for (const SceneResourceRecord &record : sceneResources_->records())
    {
      try
      {
        std::vector<std::uint8_t> decoded = sceneResources_->decodeRecord(record);
        if (!orphen::ported::psc3::hasPsc3Magic(decoded))
        {
          continue;
        }

        auto model = orphen::ported::psc3::loadPsc3Model(decoded);
        if (model.triangles.empty())
        {
          ++skippedPsc3Count;
          continue;
        }

        sceneModels_.push_back({std::move(model), {}, record.category, record.resourceId});
      }
      catch (const std::exception &)
      {
        ++skippedPsc3Count;
      }
    }

    layoutSceneModelGallery();
    if (!sceneModels_.empty() || skippedPsc3Count != 0)
    {
      std::size_t triangleCount = 0;
      for (const auto &sceneModel : sceneModels_)
      {
        triangleCount += sceneModel.model.triangles.size();
      }
      std::cout << "[scene-objects] loaded " << sceneModels_.size()
                << " PSC3 resources as debug gallery triangles=" << triangleCount
                << " skipped=" << skippedPsc3Count << '\n';
    }
  }

  void MapViewer::layoutSceneModelGallery()
  {
    if (sceneModels_.empty())
    {
      return;
    }

    constexpr std::size_t kColumnCount = 8;
    constexpr float kSpacing = 3.0f;
    constexpr float kForwardStart = 4.0f;
    const float centerOffset = (static_cast<float>(kColumnCount) - 1.0f) * 0.5f;

    for (std::size_t modelIndex = 0; modelIndex < sceneModels_.size(); ++modelIndex)
    {
      const std::size_t column = modelIndex % kColumnCount;
      const std::size_t row = modelIndex / kColumnCount;
      sceneModels_[modelIndex].galleryOffset = {kForwardStart + static_cast<float>(row) * kSpacing,
                                                (static_cast<float>(column) - centerOffset) * kSpacing,
                                                0.0f};
    }
  }

  void MapViewer::setDebugPlayerProbe(std::optional<orphen::port::PlayerDebugProbeState> probe)
  {
    debugPlayerProbe_ = std::move(probe);
    if (debugPlayerProbe_.has_value() && !runtimeCameraView_.has_value())
    {
      cameraTarget_ = toViewerSpace(debugPlayerProbe_->position);
    }
  }

  void MapViewer::setRuntimeCameraView(std::optional<orphen::port::RuntimeCameraView> view)
  {
    runtimeCameraView_ = std::move(view);
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
    runtimeCameraView_.reset();
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
  }

  void MapViewer::update(float deltaSeconds, const orphen::port::InputSnapshot &input)
  {
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

    if (runtimeCameraView_.has_value())
    {
      return;
    }

    cameraYawDegrees_ += input.rotateX * kYawSpeed * deltaSeconds;
    cameraPitchDegrees_ = std::clamp(cameraPitchDegrees_ + input.rotateY * kPitchSpeed * deltaSeconds, -85.0f, -10.0f);

    if (!debugPlayerProbe_.has_value())
    {
      const float yawRadians = cameraYawDegrees_ * static_cast<float>(kPi / 180.0);
      const float panDistance = std::max(cameraDistance_, 10.0f) * kPanSpeed * deltaSeconds;
      const orphen::ported::psm2::Vec3 right{std::cos(yawRadians), 0.0f, -std::sin(yawRadians)};
      const orphen::ported::psm2::Vec3 forward{std::sin(yawRadians), 0.0f, std::cos(yawRadians)};

      cameraTarget_.x += (right.x * input.moveX + forward.x * input.moveY) * panDistance;
      cameraTarget_.z += (right.z * input.moveX + forward.z * input.moveY) * panDistance;
    }
    cameraDistance_ *= std::exp(-input.zoom * kZoomSpeed * deltaSeconds);
    cameraDistance_ = std::max(cameraDistance_, 1.0f);
  }

  void MapViewer::render(int framebufferWidth, int framebufferHeight) const
  {
    ensureTexturesUploaded();

    const bool hasRuntimeCamera = runtimeCameraView_.has_value();
    const float verticalFovDegrees = hasRuntimeCamera ? runtimeCameraView_->verticalFovDegrees : 60.0f;
    const float farPlaneHint = hasRuntimeCamera ? runtimeCameraView_->farPlaneHint : cameraDistance_ * 8.0f;
    setPerspective(framebufferWidth, framebufferHeight, verticalFovDegrees, farPlaneHint);
    if (hasRuntimeCamera)
    {
      applyLookAtCamera(toViewerSpace(runtimeCameraView_->eye), toViewerSpace(runtimeCameraView_->target));
    }
    else
    {
      applyCamera(cameraTarget_, cameraDistance_, cameraYawDegrees_, cameraPitchDegrees_);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    const float gridRadius = hasRuntimeCamera
                                 ? std::clamp(viewerDistance(toViewerSpace(runtimeCameraView_->eye), toViewerSpace(runtimeCameraView_->target)) * 2.0f, 2.0f, 40.0f)
                                 : cameraDistance_;
    drawGrid(gridRadius);
    if (map_.has_value())
    {
      drawMap(*map_, uploadedTextureIds_);
    }
    drawSceneModelGallery(sceneModels_);
    if (debugPlayerProbe_.has_value())
    {
      drawDebugPlayerProbe(*debugPlayerProbe_);
    }

    glDisable(GL_DEPTH_TEST);
    drawOriginAxisIndicator(gridRadius);
    glEnable(GL_DEPTH_TEST);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  const orphen::ported::psm2::Psm2RuntimeState *MapViewer::loadedMap() const
  {
    return map_.has_value() ? &*map_ : nullptr;
  }

} // namespace orphen::harness
