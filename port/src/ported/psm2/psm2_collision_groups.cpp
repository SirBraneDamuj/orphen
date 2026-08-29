#include <iostream>
#include "ported/psm2/psm2_collision_groups.h"

#include "ported/model/psc3_skeleton.h"
#include "ported/psm2/psm2_geometry_builder.h"

#include <algorithm>
#include <cstdint>

namespace orphen::ported::psm2
{
  namespace
  {

    // FUN_00208450:277. Every primitive a group moves is marked dynamic, and
    // FUN_00227840 reads that bit: a dynamic primitive answers with the top of
    // its bounding box instead of solving its plane. Same 0x10000 the port's
    // ground query already calls kDynamicPrimitiveBit.
    constexpr std::uint32_t kDynamicPrimitiveBit = 0x10000;
    // FUN_00208450:271-276. Set when the primitive came out flat in z, cleared
    // when it did not -- so a door that rotates out of horizontal loses it.
    constexpr std::uint32_t kFlatHeightBit = 0x200;

    Vec3 transformGroupPoint(const Vec3 &point, const orphen::ported::model::Matrix4 &matrix)
    {
      return {point.x * matrix[0] + point.y * matrix[4] + point.z * matrix[8] + matrix[12],
              point.x * matrix[1] + point.y * matrix[5] + point.z * matrix[9] + matrix[13],
              point.x * matrix[2] + point.y * matrix[6] + point.z * matrix[10] + matrix[14]};
    }

  } // namespace

  void FUN_00260738_set_group_rotation(Psm2RuntimeState &state,
                                       std::size_t group,
                                       std::size_t channel,
                                       float radians)
  {
    if (group >= state.DAT_003556e0_collisionGroups.size() || channel > 2)
    {
      return;
    }
    CollisionGroup &record = state.DAT_003556e0_collisionGroups[group];
    float *const channels = &record.rotation.x;
    channels[channel] = radians;
    if (gGroupProbe)
    {
      std::cout << "[group] rot g" << group << " ch" << channel << " = " << radians
                << " rad (" << (radians * 57.2957795f) << " deg)" << "\n";
    }
    // The original is `status < 2 ? 2 : status | 2` on a *signed* char. That
    // signed compare is what makes a write after FUN_00208450 left 0xFF behind
    // re-arm the pass instead of sticking at "already applied".
    record.dirty5a = record.dirty5a < 2 ? static_cast<std::int8_t>(2)
                                        : static_cast<std::int8_t>(record.dirty5a | 2);
  }

  bool gGroupProbe = false;

  void FUN_00260738_set_group_translation(Psm2RuntimeState &state,
                                          std::size_t group,
                                          std::size_t channel,
                                          float distance)
  {
    if (group >= state.DAT_003556e0_collisionGroups.size() || channel > 2)
    {
      return;
    }
    CollisionGroup &record = state.DAT_003556e0_collisionGroups[group];
    float *const channels = &record.translation.x;
    channels[channel] = distance;
    record.dirty5a = record.dirty5a < 1 ? static_cast<std::int8_t>(1)
                                        : static_cast<std::int8_t>(record.dirty5a | 1);
  }

  std::size_t FUN_00208450_update_collision_groups(Psm2RuntimeState &state)
  {
    std::size_t moved = 0;

    for (CollisionGroup &group : state.DAT_003556e0_collisionGroups)
    {
      if (group.dirty5a == 0)
      {
        continue;
      }
      // FUN_00208450:77, `DAT_003555d0 = 1`. The flag is raised for **any**
      // group with a live dirty byte, and it is raised *before* the bit-7 test
      // below -- so the frame a group settles on still counts. That one frame
      // of grace matters: it is the last chance the push-out gets to eject
      // anything the group swallowed on its way to a stop.
      ++moved;

      // FUN_00208450:80-84. Bit 7 means "applied last frame": clear and stop.
      if ((static_cast<std::uint8_t>(group.dirty5a) & 0x80u) != 0)
      {
        group.dirty5a = 0;
        continue;
      }
      const std::uint8_t applied = static_cast<std::uint8_t>(group.dirty5a);
      group.dirty5a = static_cast<std::int8_t>(0xFF);

      // FUN_0020cf28(1, 1, rot.xyz, trans.xyz + pivot.xyz, matrix, 0). Order 0
      // is the bone path's ZXY, not the entity root's XYZ.
      const orphen::ported::model::Matrix4 matrix = orphen::ported::model::FUN_0020cf28_compose(
          {group.translation.x + group.pivot.x, group.translation.y + group.pivot.y,
           group.translation.z + group.pivot.z},
          1.0f, 1.0f, group.rotation, orphen::ported::model::ComposeOrder::ZXY);

      // The vertices. These are the same records the renderer draws from, so
      // this is the only place a door needs to move -- there is no second copy
      // of the geometry to keep in step.
      const std::size_t vertexCount =
          std::min<std::size_t>(group.vertexCount, group.restVertices.size());
      for (std::size_t offset = 0; offset < vertexCount; ++offset)
      {
        const std::size_t vertexIndex = static_cast<std::size_t>(group.firstVertex) + offset;
        if (vertexIndex >= state.DAT_0035569c_sectionCRecords.size())
        {
          break;
        }
        state.DAT_0035569c_sectionCRecords[vertexIndex].position =
            transformGroupPoint(group.restVertices[offset], matrix);
      }

      // FUN_00208450:137-146. The rest centre sits one past the vertices and
      // lands back in the descriptor.
      if (group.descriptorIndex < state.DAT_003556d8_collisionDescriptors.size())
      {
        state.DAT_003556d8_collisionDescriptors[group.descriptorIndex].center =
            transformGroupPoint(group.restCenter, matrix);
      }

      // The primitives, and the group box built out of them.
      bool boundsValid = false;
      for (std::size_t offset = 0; offset < group.primitiveCount; ++offset)
      {
        const std::size_t primitiveIndex = static_cast<std::size_t>(group.firstPrimitive) + offset;
        if (primitiveIndex >= state.DAT_003556b0_dRecords78.size() ||
            primitiveIndex >= state.DAT_003556ac_dRecords80.size())
        {
          break;
        }

        // FUN_00208450:248-253 only runs the normal recompute when a rotation
        // was written; a pure translation leaves every plane pointing where it
        // was. Bounds and centre are rebuilt either way.
        if ((applied & 2u) != 0)
        {
          if (!rebuildPsm2Primitive(state, primitiveIndex))
          {
            continue;
          }
        }
        else
        {
          DRecord78 &record78 = state.DAT_003556b0_dRecords78[primitiveIndex];
          DRecord80 &record80 = state.DAT_003556ac_dRecords80[primitiveIndex];
          const auto &indices = record80.vertexIndices;
          const std::size_t corners = indices[2] == indices[3] ? 3u : 4u;
          record78.bounds = {};
          Vec3 centre{};
          bool ok = true;
          for (std::size_t corner = 0; corner < corners; ++corner)
          {
            if (static_cast<std::size_t>(indices[corner]) >=
                state.DAT_0035569c_sectionCRecords.size())
            {
              ok = false;
              break;
            }
            const Vec3 &position = state.DAT_0035569c_sectionCRecords[indices[corner]].position;
            includePoint(record78.bounds, position);
            centre.x += position.x;
            centre.y += position.y;
            centre.z += position.z;
          }
          if (!ok)
          {
            continue;
          }
          const float inverse = 1.0f / static_cast<float>(corners);
          record80.center = {centre.x * inverse, centre.y * inverse, centre.z * inverse};
        }

        DRecord78 &record78 = state.DAT_003556b0_dRecords78[primitiveIndex];
        if (!record78.bounds.valid)
        {
          continue;
        }

        record78.leadingWord = record78.bounds.min.z == record78.bounds.max.z
                                   ? (record78.leadingWord | kFlatHeightBit)
                                   : (record78.leadingWord & ~kFlatHeightBit);
        record78.leadingWord |= kDynamicPrimitiveBit;

        if (!boundsValid)
        {
          group.minX = record78.bounds.min.x;
          group.maxX = record78.bounds.max.x;
          group.minY = record78.bounds.min.y;
          group.maxY = record78.bounds.max.y;
          group.minZ = record78.bounds.min.z;
          group.maxZ = record78.bounds.max.z;
          boundsValid = true;
          continue;
        }
        group.minX = std::min(group.minX, record78.bounds.min.x);
        group.maxX = std::max(group.maxX, record78.bounds.max.x);
        group.minY = std::min(group.minY, record78.bounds.min.y);
        group.maxY = std::max(group.maxY, record78.bounds.max.y);
        group.minZ = std::min(group.minZ, record78.bounds.min.z);
        group.maxZ = std::max(group.maxZ, record78.bounds.max.z);
      }
      group.boundsValid = boundsValid;
    }

    return moved;
  }

} // namespace orphen::ported::psm2
