#include "harness/entity_probe.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>

namespace orphen::harness
{
  namespace
  {
    using orphen::ported::psm2::Vec3;

    Vec3 sub(const Vec3 &a, const Vec3 &b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
    Vec3 cross(const Vec3 &a, const Vec3 &b)
    {
      return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    // Moller-Trumbore, two-sided: a probe that ignored backfaces could not tell
    // "nothing here" from "only the inside of the far wall here", which is the
    // distinction a hole turns on.
    bool rayTriangle(const Vec3 &origin,
                     const Vec3 &direction,
                     const Vec3 &a,
                     const Vec3 &b,
                     const Vec3 &c,
                     float &distance)
    {
      constexpr float kEpsilon = 1e-8f;
      const Vec3 edge1 = sub(b, a);
      const Vec3 edge2 = sub(c, a);
      const Vec3 h = cross(direction, edge2);
      const float det = dot(edge1, h);
      if (std::fabs(det) < kEpsilon)
      {
        return false;
      }
      const float inverseDet = 1.0f / det;
      const Vec3 s = sub(origin, a);
      const float u = dot(s, h) * inverseDet;
      if (u < 0.0f || u > 1.0f)
      {
        return false;
      }
      const Vec3 q = cross(s, edge1);
      const float v = dot(direction, q) * inverseDet;
      if (v < 0.0f || u + v > 1.0f)
      {
        return false;
      }
      const float t = dot(edge2, q) * inverseDet;
      if (t <= kEpsilon)
      {
        return false;
      }
      distance = t;
      return true;
    }

    // Column-major 4x4 inverse, the layout glGetFloatv hands back.
    bool invert(const std::array<float, 16> &m, std::array<float, 16> &out)
    {
      std::array<float, 16> inv{};
      inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
               m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
      inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
               m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
      inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
               m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
      inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
      inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
               m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
      inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
               m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
      inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
               m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
      inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
      inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
               m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
      inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
               m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
      inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
      inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
      inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
               m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
      inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
               m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
      inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
      inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

      const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
      if (std::fabs(det) < 1e-20f)
      {
        return false;
      }
      const float scale = 1.0f / det;
      for (std::size_t i = 0; i < 16; ++i)
      {
        out[i] = inv[i] * scale;
      }
      return true;
    }

    std::array<float, 16> multiply(const std::array<float, 16> &a, const std::array<float, 16> &b)
    {
      // Column-major: out = a * b applied to a column vector as a*(b*v).
      std::array<float, 16> out{};
      for (std::size_t column = 0; column < 4; ++column)
      {
        for (std::size_t row = 0; row < 4; ++row)
        {
          float sum = 0.0f;
          for (std::size_t k = 0; k < 4; ++k)
          {
            sum += a[k * 4 + row] * b[column * 4 + k];
          }
          out[column * 4 + row] = sum;
        }
      }
      return out;
    }

    const char *reasonText(ProbeSkipReason reason)
    {
      switch (reason)
      {
      case ProbeSkipReason::kDrawn:
        return "DRAWN";
      case ProbeSkipReason::kSkipFlag:
        return "NOT DRAWN (flags bit 0x20)";
      case ProbeSkipReason::kVertexOutOfRange:
        return "NOT DRAWN (vertex index out of range)";
      }
      return "?";
    }
  } // namespace

  bool unprojectPixel(const std::array<float, 16> &modelView,
                      const std::array<float, 16> &projection,
                      int viewportWidth,
                      int viewportHeight,
                      int pixelX,
                      int pixelY,
                      Vec3 &origin,
                      Vec3 &direction)
  {
    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
      return false;
    }
    std::array<float, 16> inverse{};
    if (!invert(multiply(projection, modelView), inverse))
    {
      return false;
    }

    // GL's window origin is bottom-left; SDL reports top-left.
    const float ndcX = 2.0f * static_cast<float>(pixelX) / static_cast<float>(viewportWidth) - 1.0f;
    const float ndcY =
        1.0f - 2.0f * static_cast<float>(pixelY) / static_cast<float>(viewportHeight);

    const auto unproject = [&](float ndcZ, Vec3 &out) {
      const float v[4] = {ndcX, ndcY, ndcZ, 1.0f};
      float r[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      for (std::size_t row = 0; row < 4; ++row)
      {
        for (std::size_t k = 0; k < 4; ++k)
        {
          r[row] += inverse[k * 4 + row] * v[k];
        }
      }
      if (std::fabs(r[3]) < 1e-20f)
      {
        return false;
      }
      out = Vec3{r[0] / r[3], r[1] / r[3], r[2] / r[3]};
      return true;
    };

    Vec3 nearPoint{};
    Vec3 farPoint{};
    if (!unproject(-1.0f, nearPoint) || !unproject(1.0f, farPoint))
    {
      return false;
    }
    origin = nearPoint;
    Vec3 delta = sub(farPoint, nearPoint);
    const float length = std::sqrt(dot(delta, delta));
    if (length < 1e-12f)
    {
      return false;
    }
    direction = Vec3{delta.x / length, delta.y / length, delta.z / length};
    return true;
  }

  std::vector<ProbeHit> probeEntityRay(const orphen::port::SceneObjectViewList &objects,
                                       const Vec3 &origin,
                                       const Vec3 &direction)
  {
    std::vector<ProbeHit> hits;

    for (std::size_t viewIndex = 0; viewIndex < objects.size(); ++viewIndex)
    {
      const auto &object = objects[viewIndex];
      if (object.model == nullptr || object.bonePalette.empty())
      {
        continue;
      }
      const auto &model = *object.model;

      for (std::size_t primitiveIndex = 0; primitiveIndex < model.primitives.size();
           ++primitiveIndex)
      {
        const auto &primitive = model.primitives[primitiveIndex];
        const std::size_t corners = primitive.cornerCount();

        ProbeSkipReason reason =
            primitive.skipped() ? ProbeSkipReason::kSkipFlag : ProbeSkipReason::kDrawn;
        bool inRange = true;
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          inRange = inRange && primitive.vertexIndices[corner] < model.vertices.size();
        }
        if (!inRange)
        {
          // Nothing to intersect -- the positions cannot be read at all.
          continue;
        }

        std::array<Vec3, 4> points{};
        std::array<std::uint8_t, 4> bones{};
        for (std::size_t corner = 0; corner < corners; ++corner)
        {
          points[corner] = posedViewerVertex(model, object.bonePalette,
                                             primitive.vertexIndices[corner]);
          bones[corner] = model.vertices[primitive.vertexIndices[corner]].boneIndex;
        }

        int chosen = -1;
        for (std::size_t pass = 0; pass < 4; ++pass)
        {
          if (primitive.subdrawIndices[pass] >= 0)
          {
            chosen = primitive.subdrawIndices[pass];
          }
        }

        const auto test = [&](std::size_t a, std::size_t b, std::size_t c, int fan) {
          float distance = 0.0f;
          if (!rayTriangle(origin, direction, points[a], points[b], points[c], distance))
          {
            return;
          }
          ProbeHit hit;
          hit.viewIndex = viewIndex;
          hit.slot = object.slot;
          hit.typeId = object.typeId;
          hit.primitiveIndex = primitiveIndex;
          hit.cornerCount = corners;
          hit.bones = bones;
          hit.flags = primitive.flags;
          hit.subdraws = primitive.subdrawIndices;
          hit.chosenSubdraw = chosen;
          hit.textureSlot = object.textureSlot;
          hit.colourIndex = primitive.colourIndex;
          hit.distance = distance;
          hit.skipReason = reason;
          hit.fanTriangle = fan;
          const Vec3 normal = cross(sub(points[b], points[a]), sub(points[c], points[a]));
          hit.frontFacing = dot(normal, direction) < 0.0f;
          hits.push_back(hit);
        };

        test(0, 1, 2, 0);
        if (corners == 4)
        {
          test(0, 2, 3, 1);
        }
      }
    }

    std::sort(hits.begin(), hits.end(),
              [](const ProbeHit &a, const ProbeHit &b) { return a.distance < b.distance; });
    return hits;
  }

  void printProbeReport(std::ostream &output,
                        int pixelX,
                        int pixelY,
                        const Vec3 &origin,
                        const Vec3 &direction,
                        const std::vector<ProbeHit> &hits)
  {
    output << "\n=== entity probe at pixel (" << pixelX << ", " << pixelY << ") ===\n";
    output << std::fixed << std::setprecision(4);
    output << "ray origin=(" << origin.x << ", " << origin.y << ", " << origin.z << ") dir=("
           << direction.x << ", " << direction.y << ", " << direction.z << ")  [viewer space]\n";
    if (hits.empty())
    {
      output << "no entity triangle along this ray\n";
      return;
    }
    for (const auto &hit : hits)
    {
      output << "  t=" << std::setw(8) << hit.distance << "  slot=" << hit.slot << " type=0x"
             << std::hex << hit.typeId << std::dec << "  prim=" << hit.primitiveIndex << "/tri"
             << hit.fanTriangle << "  bones=[";
      for (std::size_t corner = 0; corner < hit.cornerCount; ++corner)
      {
        output << (corner != 0 ? "," : "") << static_cast<int>(hit.bones[corner]);
      }
      output << "]  " << (hit.frontFacing ? "FRONT" : "BACK ") << "  flags=0x" << std::hex
             << hit.flags << std::dec << "  sub=[";
      for (std::size_t pass = 0; pass < 4; ++pass)
      {
        output << (pass != 0 ? "," : "") << hit.subdraws[pass];
      }
      output << "] chosen=" << hit.chosenSubdraw << " texSlot=" << hit.textureSlot
             << " colour=" << hit.colourIndex << "  " << reasonText(hit.skipReason) << '\n';
    }
    output << "=== " << hits.size() << " triangle(s) ===\n";
  }

} // namespace orphen::harness
