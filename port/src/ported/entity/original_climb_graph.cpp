#include "ported/entity/original_climb_graph.h"

#include "ported/model/psc3_skeleton.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{

  namespace
  {
    using orphen::ported::model::FUN_002166e8_angle_delta;
    using orphen::ported::model::FUN_00216690_wrap_angle;
    using orphen::ported::psm2::Psm2RuntimeState;
    using orphen::ported::psm2::Vec3;

    // DAT_003529C4..DAT_003529D4.
    constexpr float kDAT_003529c4_pi = 3.141592025756836f;
    constexpr float kDAT_003529c8_faceConeLow = -1.0471973419189453f;
    constexpr float kDAT_003529cc_faceConeHigh = 1.0471973419189453f;
    constexpr float kDAT_003529d0_halfPi = 1.570796012878418f;
    constexpr float kDAT_003529d4_negHalfPi = -1.570796012878418f;
    // FUN_00257160's `SQRT(...) < 1.0`.
    constexpr float kFaceReach = 1.0f;

    std::size_t primitiveCount(const Psm2RuntimeState &map)
    {
      return std::min(map.DAT_003556b0_dRecords78.size(), map.DAT_003556ac_dRecords80.size());
    }

    bool samePoint(const Vec3 &a, const Vec3 &b)
    {
      return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    // The 0x80 record's +0x00 normal, as the heading it points along.
    float normalHeading(const Psm2RuntimeState &map, std::size_t primitive)
    {
      const Vec3 &normal = map.DAT_003556ac_dRecords80[primitive].normal;
      return std::atan2(normal.y, normal.x);
    }
  } // namespace

  ClimbGraph FUN_00257610_build_climb_graph(const Psm2RuntimeState &map)
  {
    ClimbGraph graph;
    const std::size_t count = primitiveCount(map);
    const auto &positions = map.DAT_0035569c_sectionCRecords;
    const auto position = [&](std::uint16_t index) -> const Vec3 *
    {
      return index < positions.size() ? &positions[index].position : nullptr;
    };

    for (std::size_t own = 0; own < count; ++own)
    {
      const auto &own78 = map.DAT_003556b0_dRecords78[own];
      if (!isClimbableTerrain(own78.terrainFlags))
      {
        continue;
      }

      // The original writes neighbours as it finds them with no bound, so a
      // fifth one lands in the next record's primitive halfword and is
      // overwritten when that record starts -- but the duplicate check below
      // still reads it. Hence the full list here and the truncation at the end.
      std::vector<std::int16_t> found;
      for (std::size_t other = 0; other < count; ++other)
      {
        const auto &other78 = map.DAT_003556b0_dRecords78[other];
        if (other == own || !isClimbableTerrain(other78.terrainFlags))
        {
          continue;
        }

        // All four corner slots of each, a triangle's repeated third corner
        // included: a triangle that shares that corner counts it twice.
        int shared = 0;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
          const Vec3 *mine = position(own78.vertexIndices[corner]);
          if (mine == nullptr)
          {
            continue;
          }
          for (std::size_t theirs = 0; theirs < 4; ++theirs)
          {
            const Vec3 *candidate = position(other78.vertexIndices[theirs]);
            if (candidate != nullptr && samePoint(*mine, *candidate))
            {
              ++shared;
              break;
            }
          }
        }
        if (shared != 2)
        {
          continue;
        }

        // A face whose centre matches one already taken is the same face
        // again -- the halves of a split quad -- and is skipped.
        const Vec3 &centre = map.DAT_003556ac_dRecords80[other].center;
        bool duplicate = false;
        for (const std::int16_t taken : found)
        {
          if (samePoint(map.DAT_003556ac_dRecords80[static_cast<std::size_t>(taken)].center, centre))
          {
            duplicate = true;
            break;
          }
        }
        if (!duplicate)
        {
          found.push_back(static_cast<std::int16_t>(other));
        }
      }

      ClimbFaceRecord record;
      record.primitive = static_cast<std::int16_t>(own);
      for (std::size_t slot = 0; slot < record.neighbours.size() && slot < found.size(); ++slot)
      {
        record.neighbours[slot] = found[slot];
      }
      graph.push_back(record);
    }
    return graph;
  }

  std::int16_t FUN_00257160_find_climb_face(const OriginalEntity &entity,
                                            const ClimbGraph &graph,
                                            const Psm2RuntimeState &map)
  {
    // The scratchpad point at the entity's radius ahead, and FUN_0020B810's
    // transform of it, are computed and never read. Only the height is used.
    const float facing = FUN_00216690_wrap_angle(entity.facingRadians5c + 0.0f);
    const float bodyMiddle = entity.positionY28 + entity.height58 * 0.5f;
    const float lookingInto = facing + kDAT_003529c4_pi;
    const std::size_t count = primitiveCount(map);

    std::int16_t chosen = -1;
    for (const ClimbFaceRecord &record : graph)
    {
      const auto primitive = static_cast<std::size_t>(record.primitive);
      if (primitive >= count)
      {
        continue;
      }
      const auto &bounds = map.DAT_003556b0_dRecords78[primitive].bounds;
      if (!(bounds.min.z <= bodyMiddle && bodyMiddle < bounds.max.z))
      {
        continue;
      }
      const float heading = normalHeading(map, primitive);
      const float facingError = FUN_002166e8_angle_delta(lookingInto, heading);
      if (!(kDAT_003529c8_faceConeLow < facingError && facingError < kDAT_003529cc_faceConeHigh))
      {
        continue;
      }
      const Vec3 &centre = map.DAT_003556ac_dRecords80[primitive].center;
      const float offsetX = entity.positionX20 - centre.x;
      const float offsetY = entity.positionZ24 - centre.y;
      if (!(std::sqrt(offsetX * offsetX + offsetY * offsetY) < kFaceReach))
      {
        continue;
      }
      // And the entity has to be on the side the face points to.
      const float sideError = FUN_002166e8_angle_delta(std::atan2(offsetY, offsetX), heading);
      if (kDAT_003529c8_faceConeLow < sideError && sideError < kDAT_003529cc_faceConeHigh)
      {
        chosen = record.primitive;
      }
    }
    return chosen;
  }

  std::int16_t FUN_002573d8_find_climb_neighbour(const OriginalEntity &entity,
                                                 const ClimbGraph &graph,
                                                 const Psm2RuntimeState &map,
                                                 std::int16_t primitive,
                                                 int direction)
  {
    // The original walks off the end of the list into whatever follows it when
    // the face is not there. Every caller passes a face it got from the list.
    const ClimbFaceRecord *own = nullptr;
    for (const ClimbFaceRecord &record : graph)
    {
      if (record.primitive == primitive)
      {
        own = &record;
        break;
      }
    }
    const std::size_t count = primitiveCount(map);
    if (own == nullptr || graph.empty() || static_cast<std::size_t>(primitive) >= count)
    {
      return -1;
    }
    const auto &ownBounds = map.DAT_003556b0_dRecords78[static_cast<std::size_t>(primitive)].bounds;

    for (const std::int16_t neighbour : own->neighbours)
    {
      if (neighbour < 0)
      {
        continue;
      }
      // Only a neighbour that is itself in the list counts.
      bool listed = false;
      for (const ClimbFaceRecord &record : graph)
      {
        if (&record != own && record.primitive == neighbour)
        {
          listed = true;
          break;
        }
      }
      if (!listed || static_cast<std::size_t>(neighbour) >= count)
      {
        continue;
      }

      const Vec3 &centre = map.DAT_003556ac_dRecords80[static_cast<std::size_t>(neighbour)].center;
      switch (direction)
      {
      case 1:
        if (ownBounds.max.z < centre.z)
        {
          return neighbour;
        }
        break;
      case 3:
        if (centre.z < ownBounds.min.z)
        {
          return neighbour;
        }
        break;
      case 0:
      case 2:
        if (centre.z < ownBounds.max.z && ownBounds.min.z < centre.z)
        {
          const float towards =
              std::atan2(centre.y - entity.positionZ24, centre.x - entity.positionX20);
          const float side = direction == 0
                                 ? FUN_00216690_wrap_angle(entity.facingRadians5c + kDAT_003529d0_halfPi)
                                 : FUN_00216690_wrap_angle(entity.facingRadians5c - kDAT_003529d0_halfPi);
          const float error = FUN_002166e8_angle_delta(side, towards);
          if (kDAT_003529d4_negHalfPi < error && error < kDAT_003529d0_halfPi)
          {
            return neighbour;
          }
        }
        break;
      default:
        break;
      }
    }
    return -1;
  }

} // namespace orphen::ported::entity
