#include "harness/map_pick.h"

#include "harness/entity_probe.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <limits>

namespace orphen::harness
{

  namespace
  {
    using orphen::ported::psm2::Psm2RuntimeState;
    using orphen::ported::psm2::Vec3;

    Vec3 toViewer(const Vec3 &game) { return Vec3{game.x, game.z, -game.y}; }

    Vec3 vertexAt(const Psm2RuntimeState &map, std::uint16_t index)
    {
      return toViewer(map.DAT_0035569c_sectionCRecords[index].position);
    }

    bool trianglesValid(const Psm2RuntimeState &map, const orphen::ported::psm2::DRecord80 &record80)
    {
      return record80.firstTriangle + record80.triangleCount <= map.derivedTriangles.size();
    }

    bool vertexValid(const Psm2RuntimeState &map, std::uint16_t index)
    {
      return index < map.DAT_0035569c_sectionCRecords.size();
    }

    void emitTriangles(const Psm2RuntimeState &map, const orphen::ported::psm2::DRecord80 &record80)
    {
      glBegin(GL_TRIANGLES);
      for (std::size_t offset = 0; offset < record80.triangleCount; ++offset)
      {
        const auto &triangle = map.derivedTriangles[record80.firstTriangle + offset];
        if (!std::all_of(triangle.vertexIndices.begin(), triangle.vertexIndices.end(),
                         [&](std::uint16_t index) { return vertexValid(map, index); }))
        {
          continue;
        }
        for (const std::uint16_t index : triangle.vertexIndices)
        {
          const Vec3 vertex = vertexAt(map, index);
          glVertex3f(vertex.x, vertex.y, vertex.z);
        }
      }
      glEnd();
    }

    // The primitive's own corners, 3 or 4 -- the ground query's test, a
    // repeated third index -- rather than the triangles' edges, so a quad
    // outlines as a quad and not as two triangles.
    void emitOutline(const Psm2RuntimeState &map, const orphen::ported::psm2::DRecord78 &record78)
    {
      const auto &indices = record78.vertexIndices;
      const std::size_t cornerCount = indices[2] == indices[3] ? 3 : 4;
      glBegin(GL_LINE_LOOP);
      for (std::size_t corner = 0; corner < cornerCount; ++corner)
      {
        if (vertexValid(map, indices[corner]))
        {
          const Vec3 vertex = vertexAt(map, indices[corner]);
          glVertex3f(vertex.x, vertex.y, vertex.z);
        }
      }
      glEnd();
    }
  } // namespace

  std::optional<MapPickHit> pickMapPrimitive(const Psm2RuntimeState &map,
                                             const std::vector<orphen::ported::render::MapDrawItem> &drawList,
                                             const Vec3 &origin,
                                             const Vec3 &direction)
  {
    std::optional<MapPickHit> nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    const auto &records = map.DAT_003556ac_dRecords80;
    for (const auto &item : drawList)
    {
      if (item.primitiveIndex >= records.size())
      {
        continue;
      }
      const auto &record80 = records[item.primitiveIndex];
      if (!trianglesValid(map, record80))
      {
        continue;
      }
      // The bounding sphere first: a few thousand of these is nothing, a few
      // thousand triangle tests every frame is still not much, but most
      // primitives are nowhere near the pointer. The collision groups keep the
      // centre current when a door moves (psm2_collision_groups.cpp).
      const Vec3 centre = toViewer(record80.center);
      const Vec3 toCentre{centre.x - origin.x, centre.y - origin.y, centre.z - origin.z};
      const float along = toCentre.x * direction.x + toCentre.y * direction.y + toCentre.z * direction.z;
      const float radius = record80.radius;
      if (along + radius < 0.0f || along - radius > nearestDistance)
      {
        continue;
      }
      const float offRaySquared =
          toCentre.x * toCentre.x + toCentre.y * toCentre.y + toCentre.z * toCentre.z - along * along;
      if (offRaySquared > radius * radius)
      {
        continue;
      }
      for (std::size_t offset = 0; offset < record80.triangleCount; ++offset)
      {
        const auto &triangle = map.derivedTriangles[record80.firstTriangle + offset];
        if (!std::all_of(triangle.vertexIndices.begin(), triangle.vertexIndices.end(),
                         [&](std::uint16_t index) { return vertexValid(map, index); }))
        {
          continue;
        }
        float distance = 0.0f;
        if (rayHitsTriangle(origin, direction, vertexAt(map, triangle.vertexIndices[0]),
                            vertexAt(map, triangle.vertexIndices[1]), vertexAt(map, triangle.vertexIndices[2]),
                            distance) &&
            distance < nearestDistance)
        {
          nearestDistance = distance;
          nearest = MapPickHit{item.primitiveIndex, distance};
        }
      }
    }
    return nearest;
  }

  void drawMapPrimitiveHighlight(const Psm2RuntimeState &map, std::size_t primitiveIndex, bool selected)
  {
    if (primitiveIndex >= map.DAT_003556ac_dRecords80.size() || primitiveIndex >= map.DAT_003556b0_dRecords78.size())
    {
      return;
    }
    const auto &record80 = map.DAT_003556ac_dRecords80[primitiveIndex];
    const auto &record78 = map.DAT_003556b0_dRecords78[primitiveIndex];
    if (!trianglesValid(map, record80))
    {
      return;
    }
    const float red = selected ? 0.3f : 1.0f;
    const float green = selected ? 0.9f : 0.75f;
    const float blue = selected ? 1.0f : 0.2f;

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT |
                 GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (selected)
    {
      // Faint through everything, the way the entity highlight is, so a tile
      // stays findable after flying round behind a wall.
      glDisable(GL_DEPTH_TEST);
      glLineWidth(1.5f);
      glColor4f(red, green, blue, 0.3f);
      emitOutline(map, record78);
    }

    // Pulled towards the camera so the tint wins the depth test against the
    // primitive it sits on.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glColor4f(red, green, blue, selected ? 0.4f : 0.3f);
    emitTriangles(map, record80);
    glLineWidth(2.0f);
    glColor4f(red, green, blue, 1.0f);
    emitOutline(map, record78);

    glPopAttrib();
  }

} // namespace orphen::harness
