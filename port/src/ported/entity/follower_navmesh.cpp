#include "ported/entity/follower_navmesh.h"

#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // The constant block at 0x003529ec, and the two gp-relative floats the
    // corner cut uses.
    constexpr float kDAT_003529ec_edgeProbeTurn = 1.57079601f;   // +-90 degrees off the edge
    constexpr float kDAT_003529f0_edgeProbeReach = 0.2f;         // how far outside the edge
    constexpr float kDAT_003529f4_edgeProbeLift = 0.3f;          // above the source's centre
    constexpr float kDAT_003529f8_twoPi = 6.28318405f;
    constexpr float kDAT_003529fc_twoPi = 6.28318405f;
    constexpr float kDAT_00352a00_stepUpLimit = 0.28f;
    constexpr float kDAT_00352a04_stepDownLimit = -0.28f;
    constexpr float kfGpffff8a9c_twoPi = 6.28318405f;
    constexpr float kfGpffff8aa0_cutDropLimit = -0.1f;
    constexpr float kfGpffff8aa4_cutRiseLimit = 0.1f;
    constexpr float kfGpffff8aa8_cutProbeReach = 0.3f;

    // FUN_00258080:44. The slope is stored in radians and compared as whole
    // degrees, so anything under one degree reads as flat.
    constexpr int kWalkableSlopeDegrees = 0x32;

    // FUN_00258080:45 and FUN_002584b0's own copy. 0x0D000000 is the surface
    // class a follower rejects outright; 0x02000000 makes a sloped neighbour
    // fall back to the height test instead of the shared-vertex one.
    constexpr std::uint32_t kRejectedSurfaceClasses = 0x0D000000u;
    constexpr std::uint32_t kSlopeExemptClass = 0x02000000u;
    constexpr std::uint32_t kCornerCutRejectClasses = 0x07000000u;

    constexpr std::size_t kQueueMask = 0xFFF; // the 4096-entry ring in scratchpad
    constexpr std::size_t kNavLanes = 4;

    // FUN_0030bd20 is float -> int, truncating toward zero.
    int FUN_0030bd20_toInt(float value) { return static_cast<int>(value); }

    int slopeDegrees(float radians, float divisor)
    {
      return FUN_0030bd20_toInt((radians * 360.0f) / divisor);
    }

    std::size_t primitiveOf(std::int32_t packed)
    {
      return static_cast<std::size_t>(packed) & 0x3FFFu;
    }

    // FUN_00258080:41 reads `+0x70 + (packed >> 14) * 4`, the stored slope of
    // the half that answered. Only two halves exist, so the selector is
    // clamped rather than allowed to read past the pair.
    float storedSlope(const orphen::ported::psm2::DRecord78 &record, std::int32_t packed)
    {
      const std::size_t half = (static_cast<std::uint32_t>(packed) >> 14) & 3u;
      return record.slopeAngle[half < record.slopeAngle.size() ? half : 0];
    }

    // FUN_00216690, wrap to (-pi, pi].
    float FUN_00216690_wrap(float angle)
    {
      constexpr float kPi = 3.14159203f;
      while (angle > kPi)
      {
        angle -= kDAT_003529f8_twoPi;
      }
      while (angle <= -kPi)
      {
        angle += kDAT_003529f8_twoPi;
      }
      return angle;
    }

    float FUN_00216648_distance3(float dx, float dy, float dz)
    {
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float FUN_00216608_length(float dx, float dy) { return std::sqrt(dx * dx + dy * dy); }
  } // namespace

  void FollowerNavmesh::FUN_00257fc0_reset(std::size_t primitiveCount)
  {
    DAT_00355038_records_.assign(primitiveCount, NavRecord{});
    for (std::size_t index = 0; index < primitiveCount; ++index)
    {
      NavRecord &record = DAT_00355038_records_[index];
      record.ownIndex = static_cast<std::int16_t>(index);
      record.packedPrimitive = -1;
      record.neighbour.fill(-1);
      record.laneDepth.fill(-1);
      record.edgeDepth.fill(-1);
    }
  }

  std::size_t FollowerNavmesh::reachedCount() const
  {
    return static_cast<std::size_t>(
        std::count_if(DAT_00355038_records_.begin(), DAT_00355038_records_.end(),
                      [](const NavRecord &record) { return record.packedPrimitive >= 0; }));
  }

  NavRecord *FollowerNavmesh::at(std::int32_t packedPrimitive)
  {
    if (packedPrimitive < 0)
    {
      return nullptr;
    }
    const std::size_t index = primitiveOf(packedPrimitive);
    return index < DAT_00355038_records_.size() ? &DAT_00355038_records_[index] : nullptr;
  }

  const NavRecord *FollowerNavmesh::recordFor(std::int32_t packedPrimitive) const
  {
    if (packedPrimitive < 0)
    {
      return nullptr;
    }
    const std::size_t index = primitiveOf(packedPrimitive);
    return index < DAT_00355038_records_.size() ? &DAT_00355038_records_[index] : nullptr;
  }

  // FUN_00258080: does edge `edge` of `primitive` have a walkable neighbour?
  //
  // The edge runs from the primitive's corner `edge` to corner `edge + 1`. The
  // probe stands at the edge's midpoint and steps `0.2` sideways -- once to
  // each side, because the winding is not known -- from a height `0.3` above
  // the source primitive's centre, and asks the ordinary ground query what is
  // underfoot. Whatever answers, if it is a different primitive, is the
  // neighbour, provided it is under 50 degrees and not one of the rejected
  // surface classes.
  std::int16_t FollowerNavmesh::FUN_00258080_probe_edge(
      const orphen::ported::psm2::Psm2RuntimeState &map,
      const NavProbeFn &probe,
      NavRecord &record,
      std::size_t primitive,
      int edge,
      std::int16_t &neighbourSlot)
  {
    neighbourSlot = -1;

    const auto &record78 = map.DAT_003556b0_dRecords78[primitive];
    const auto &record80 = map.DAT_003556ac_dRecords80[primitive];
    const std::size_t firstCorner = record78.vertexIndices[static_cast<std::size_t>(edge)];
    const std::size_t secondCorner =
        record78.vertexIndices[static_cast<std::size_t>((edge + 1) & 3)];
    if (firstCorner == secondCorner || firstCorner >= map.DAT_0035569c_sectionCRecords.size() ||
        secondCorner >= map.DAT_0035569c_sectionCRecords.size())
    {
      return -1;
    }

    const auto &from = map.DAT_0035569c_sectionCRecords[firstCorner].position;
    const auto &to = map.DAT_0035569c_sectionCRecords[secondCorner].position;
    const float edgeAngle = std::atan2(to.y - from.y, to.x - from.x);
    const float edgeLength = FUN_00216608_length(to.x - from.x, to.y - from.y);

    float sideTurn = kDAT_003529ec_edgeProbeTurn;
    for (int side = 0; side < 2; ++side, sideTurn = -sideTurn)
    {
      const float outward = edgeAngle + sideTurn;
      const float x = from.x + edgeLength * 0.5f * std::cos(edgeAngle) +
                      kDAT_003529f0_edgeProbeReach * std::cos(outward);
      const float y = from.y + edgeLength * 0.5f * std::sin(edgeAngle) +
                      kDAT_003529f0_edgeProbeReach * std::sin(outward);
      const NavGroundProbe hit = probe(x, y, record80.center.z + kDAT_003529f4_edgeProbeLift);
      if (hit.DAT_00354d4e_packedPrimitive < 0)
      {
        continue;
      }
      const std::size_t found = primitiveOf(hit.DAT_00354d4e_packedPrimitive);
      if (found == primitiveOf(record.packedPrimitive) || found >= DAT_00355038_records_.size())
      {
        continue;
      }

      const auto &found78 = map.DAT_003556b0_dRecords78[found];
      if (slopeDegrees(storedSlope(found78, hit.DAT_00354d4e_packedPrimitive),
                       kDAT_003529f8_twoPi) >= kWalkableSlopeDegrees ||
          (found78.terrainFlags & kRejectedSurfaceClasses) != 0)
      {
        continue;
      }

      // The link is recorded whether or not the neighbour is new; only a new
      // one is handed back for the queue.
      neighbourSlot = hit.DAT_00354d4e_packedPrimitive;
      NavRecord &neighbour = DAT_00355038_records_[found];
      if (neighbour.packedPrimitive >= 0)
      {
        return -1;
      }
      neighbour.packedPrimitive = hit.DAT_00354d4e_packedPrimitive;
      return hit.DAT_00354d4e_packedPrimitive;
    }

    return -1;
  }

  void FollowerNavmesh::FUN_002582d0_build(const orphen::ported::psm2::Psm2RuntimeState &map,
                                           const NavProbeFn &probe,
                                           float x,
                                           float y,
                                           float z)
  {
    const std::size_t primitiveCount =
        std::min(map.DAT_003556b0_dRecords78.size(), map.DAT_003556ac_dRecords80.size());
    if (DAT_00355038_records_.size() != primitiveCount)
    {
      FUN_00257fc0_reset(primitiveCount);
    }
    if (primitiveCount == 0 || !probe)
    {
      return;
    }

    NavGroundProbe seed = probe(x, y, z);
    if (seed.DAT_00354d4e_packedPrimitive < 0)
    {
      // FUN_002582d0:19-25 prints the seed point and re-reads DAT_00354d4e,
      // which is still negative. Nothing else happens, so the graph stays
      // empty and every follower falls back to the recovery state.
      return;
    }

    std::vector<std::int16_t> queue;
    queue.reserve(primitiveCount);
    std::size_t cursor = 0;

    std::int16_t current = seed.DAT_00354d4e_packedPrimitive;
    NavRecord *record = at(current);
    if (record == nullptr)
    {
      return;
    }
    record->packedPrimitive = current;

    for (;;)
    {
      const std::size_t primitive = primitiveOf(record->packedPrimitive);
      for (int edge = 0; edge < 4; ++edge)
      {
        std::int16_t slot = -1;
        const std::int16_t discovered =
            FUN_00258080_probe_edge(map, probe, *record, primitive, edge, slot);
        record->neighbour[static_cast<std::size_t>(edge)] = slot;
        if (discovered >= 0)
        {
          queue.push_back(discovered);
        }
      }

      if (cursor >= queue.size())
      {
        break;
      }
      current = queue[cursor++];
      record = at(current);
      if (record == nullptr)
      {
        break;
      }
    }
  }

  void FollowerNavmesh::FUN_002584b0_flood(const orphen::ported::psm2::Psm2RuntimeState &map,
                                           const NavProbeFn &probe,
                                           float x,
                                           float y,
                                           float z,
                                           int lane)
  {
    if (DAT_00355038_records_.empty() || !probe)
    {
      return;
    }

    // `param_4 + (param_4 < 0 ? param_4 + 3 : param_4) / 4 * -4` is the
    // compiler's signed modulo: the lane is taken mod 4.
    lane %= static_cast<int>(kNavLanes);
    if (lane < 0)
    {
      lane += static_cast<int>(kNavLanes);
    }
    const std::size_t laneIndex = static_cast<std::size_t>(lane);

    // Only this lane's slice is cleared; the other three keep whatever the
    // followers using them left behind.
    for (NavRecord &record : DAT_00355038_records_)
    {
      record.laneDepth[laneIndex] = -1;
      for (std::size_t edge = 0; edge < 4; ++edge)
      {
        record.edgeDepth[laneIndex * 4 + edge] = -1;
      }
    }

    const NavGroundProbe seed = probe(x, y, z);
    if (seed.DAT_00354d4e_packedPrimitive < 0)
    {
      return;
    }
    NavRecord *start = at(seed.DAT_00354d4e_packedPrimitive);
    if (start == nullptr)
    {
      return;
    }
    start->laneDepth[laneIndex] = 0;

    std::vector<std::int16_t> queue;
    queue.reserve(DAT_00355038_records_.size());
    std::size_t head = 0;
    NavRecord *current = start;

    for (;;)
    {
      const std::size_t currentPrimitive = primitiveOf(current->packedPrimitive);
      if (currentPrimitive >= map.DAT_003556b0_dRecords78.size())
      {
        break;
      }
      const auto &current78 = map.DAT_003556b0_dRecords78[currentPrimitive];
      const auto &current80 = map.DAT_003556ac_dRecords80[currentPrimitive];

      for (std::size_t edge = 0; edge < 4; ++edge)
      {
        const std::int16_t packedNeighbour = current->neighbour[edge];
        if (packedNeighbour < 0)
        {
          continue;
        }
        if (current->edgeDepth[laneIndex * 4 + edge] >= 0)
        {
          continue; // already crossed this edge in this flood
        }
        NavRecord *neighbour = at(packedNeighbour);
        if (neighbour == nullptr || neighbour->packedPrimitive < 0)
        {
          continue;
        }
        const std::size_t neighbourPrimitive = primitiveOf(neighbour->packedPrimitive);
        if (neighbourPrimitive >= map.DAT_003556b0_dRecords78.size())
        {
          continue;
        }
        const auto &neighbour78 = map.DAT_003556b0_dRecords78[neighbourPrimitive];
        const auto &neighbour80 = map.DAT_003556ac_dRecords80[neighbourPrimitive];

        // FUN_002584b0:88-137. Two ways a pair can be connected, chosen by
        // whether either side is sloped:
        //
        //   both flat        the centres must be within 0.28 of each other
        //   either sloped    they must share a vertex *position*, unless the
        //                    sloped one carries 0x02000000, which sends it
        //                    back to the height test
        //
        // The height test is what keeps a follower off a ledge it could
        // otherwise step onto sideways; the shared-vertex test is what lets a
        // ramp connect to the floor it meets at an angle.
        const bool neighbourSloped =
            slopeDegrees(storedSlope(neighbour78, neighbour->packedPrimitive),
                         kDAT_003529fc_twoPi) != 0;
        const bool currentSloped =
            slopeDegrees(storedSlope(current78, current->packedPrimitive),
                         kDAT_003529fc_twoPi) != 0;

        bool connected = false;
        const bool useSharedVertex =
            (neighbourSloped || currentSloped) &&
            (neighbour78.terrainFlags & kSlopeExemptClass) == 0;
        if (useSharedVertex)
        {
          for (std::size_t mine = 0; mine < 4 && !connected; ++mine)
          {
            const std::size_t mineIndex = current78.vertexIndices[mine];
            if (mineIndex >= map.DAT_0035569c_sectionCRecords.size())
            {
              continue;
            }
            const auto &minePosition = map.DAT_0035569c_sectionCRecords[mineIndex].position;
            for (std::size_t theirs = 0; theirs < 4; ++theirs)
            {
              const std::size_t theirIndex = neighbour78.vertexIndices[theirs];
              if (theirIndex >= map.DAT_0035569c_sectionCRecords.size())
              {
                continue;
              }
              const auto &theirPosition = map.DAT_0035569c_sectionCRecords[theirIndex].position;
              if (minePosition.x == theirPosition.x && minePosition.y == theirPosition.y &&
                  minePosition.z == theirPosition.z)
              {
                connected = true;
                break;
              }
            }
          }
        }
        else
        {
          const float rise = current80.center.z - neighbour80.center.z;
          connected = rise <= kDAT_00352a00_stepUpLimit && rise >= kDAT_00352a04_stepDownLimit;
        }
        if (!connected)
        {
          continue;
        }

        // Find the neighbour's edge that points back at us, and hang the
        // distances off both sides of it.
        for (std::size_t back = 0; back < 4; ++back)
        {
          if (neighbour->neighbour[back] < 0 ||
              primitiveOf(neighbour->neighbour[back]) != currentPrimitive)
          {
            continue;
          }
          if (neighbour->laneDepth[laneIndex] < 0)
          {
            queue.push_back(packedNeighbour);
            neighbour->laneDepth[laneIndex] =
                static_cast<std::int16_t>(current->laneDepth[laneIndex] + 1);
          }
          current->edgeDepth[laneIndex * 4 + edge] =
              static_cast<std::int16_t>(current->laneDepth[laneIndex] + 1);
          neighbour->edgeDepth[laneIndex * 4 + back] = current->laneDepth[laneIndex];
          break;
        }
      }

      if (head >= queue.size() || head > kQueueMask)
      {
        break;
      }
      NavRecord *next = at(queue[head++]);
      if (next == nullptr)
      {
        break;
      }
      current = next;
    }
  }

  const NavRecord *FollowerNavmesh::FUN_00258c70_step(
      const orphen::ported::psm2::Psm2RuntimeState &map,
      const NavProbeFn &probe,
      const OriginalEntity *self,
      EntityPool *pool,
      float positionX,
      float positionY,
      float positionZ,
      int lane,
      bool skipCornerCut)
  {
    if (DAT_00355038_records_.empty() || !probe)
    {
      return nullptr;
    }
    const std::size_t laneIndex = static_cast<std::size_t>(lane) % kNavLanes;

    const NavGroundProbe here = probe(positionX, positionY, positionZ);
    if (here.DAT_00354d4e_packedPrimitive < 0)
    {
      return nullptr;
    }
    NavRecord *current = at(here.DAT_00354d4e_packedPrimitive);
    if (current == nullptr)
    {
      return nullptr;
    }

    const std::int16_t depth = current->laneDepth[laneIndex];
    if (depth < 0)
    {
      return nullptr; // the flood never reached this cell
    }
    if (depth < 1)
    {
      return current; // standing on the target
    }

    // FUN_00258c70:36-49. Every other live actor that is neither hidden
    // (+0x04 bit 0x2000) nor collision-disabled (bit 1) blocks the cell it is
    // standing on, so two followers do not queue into the same primitive.
    std::vector<const OriginalEntity *> blockers;
    if (self != nullptr && pool != nullptr)
    {
      for (std::size_t index = 1; index < EntityPool::slotCount(); ++index)
      {
        if (static_cast<std::int8_t>(pool->status(index)) <= 0)
        {
          continue;
        }
        const OriginalEntity &other = pool->slot(index);
        if (&other == self || (other.halfword04 & 0x2001u) != 0)
        {
          continue;
        }
        blockers.push_back(&other);
      }
    }

    std::uint16_t best = static_cast<std::uint16_t>(depth + 1);
    NavRecord *chosen = nullptr;
    for (std::size_t edge = 0; edge < 4; ++edge)
    {
      const std::int16_t packedNeighbour = current->neighbour[edge];
      if (packedNeighbour < 0)
      {
        continue;
      }
      const std::int16_t mark = current->edgeDepth[laneIndex * 4 + edge];
      if (mark < 0 || static_cast<std::uint16_t>(mark) >= best)
      {
        continue;
      }

      bool occupied = false;
      for (const OriginalEntity *other : blockers)
      {
        if ((static_cast<std::uint16_t>(other->groundPrimitive0a) & 0x3FFFu) ==
            (static_cast<std::uint16_t>(packedNeighbour) & 0x3FFFu))
        {
          // The original writes 0xff over the edge mark so the cell stays
          // refused for the rest of this frame's scan.
          current->edgeDepth[laneIndex * 4 + edge] = 0xFF;
          occupied = true;
          break;
        }
      }
      if (occupied)
      {
        continue;
      }

      const std::size_t neighbourPrimitive = primitiveOf(packedNeighbour);
      if (neighbourPrimitive >= map.DAT_003556b0_dRecords78.size())
      {
        continue;
      }
      const std::uint32_t required = self != nullptr ? self->requiredTerrainMask78 : 0;
      if (required != 0 &&
          (map.DAT_003556b0_dRecords78[neighbourPrimitive].terrainFlags & required) == 0)
      {
        continue;
      }
      best = static_cast<std::uint16_t>(current->edgeDepth[laneIndex * 4 + edge]);
      chosen = at(packedNeighbour);
    }

    if (chosen == nullptr)
    {
      return nullptr;
    }
    if (skipCornerCut || best == 0)
    {
      return chosen;
    }

    // FUN_00258c70:106-176, the corner cut. Look one cell further along the
    // gradient and, if the straight line to it stays level to within 0.1 over
    // four probes, walk to *that* instead. This is what stops a follower
    // tracing the outline of every primitive it crosses.
    const std::uint16_t nextDepth = static_cast<std::uint16_t>(best - 1);
    if (nextDepth == 0)
    {
      return chosen;
    }

    float closest = 100.0f;
    std::int16_t shortcut = -1;
    for (std::size_t edge = 0; edge < 4; ++edge)
    {
      const std::int16_t packedNeighbour = chosen->neighbour[edge];
      if (packedNeighbour < 0)
      {
        continue;
      }
      const std::int16_t mark = chosen->edgeDepth[laneIndex * 4 + edge];
      if (mark < 0 || static_cast<std::uint16_t>(mark) != nextDepth)
      {
        continue;
      }
      const std::size_t neighbourPrimitive = primitiveOf(packedNeighbour);
      if (neighbourPrimitive >= map.DAT_003556b0_dRecords78.size())
      {
        continue;
      }
      // The second half of the original's test indexes the 0x78 array with the
      // *depth* rather than a primitive, which is almost certainly a slip in
      // the original -- it is reproduced, bounded, because it can refuse a
      // shortcut and leaving it out would change which cell is picked.
      if ((map.DAT_003556b0_dRecords78[neighbourPrimitive].terrainFlags &
           kCornerCutRejectClasses) != 0)
      {
        continue;
      }
      if (nextDepth < map.DAT_003556b0_dRecords78.size() &&
          (map.DAT_003556b0_dRecords78[nextDepth].terrainFlags & kCornerCutRejectClasses) != 0)
      {
        continue;
      }
      const auto &center = map.DAT_003556ac_dRecords80[neighbourPrimitive].center;
      const float distance = FUN_00216648_distance3(positionX - center.x, positionY - center.y,
                                                    positionZ - center.z);
      if (distance < closest)
      {
        closest = distance;
        shortcut = packedNeighbour;
      }
    }

    if (shortcut < 0)
    {
      return chosen;
    }

    const auto &target = map.DAT_003556ac_dRecords80[primitiveOf(shortcut)].center;
    const float toTargetX = target.x - positionX;
    const float toTargetY = target.y - positionY;
    const float bearing = std::atan2(toTargetY, toTargetX);
    const float span = FUN_00216608_length(toTargetX, toTargetY);
    const float midX = positionX + span * 0.5f * std::cos(bearing);
    const float midY = positionY + span * 0.5f * std::sin(bearing);

    bool level = true;
    int offset = 0;
    for (int step = 0; step < 4; ++step, offset += 0x5A)
    {
      const float around =
          FUN_00216690_wrap(bearing + (static_cast<float>(offset) * kfGpffff8a9c_twoPi) / 360.0f);
      const NavGroundProbe hit = probe(midX + kfGpffff8aa8_cutProbeReach * std::cos(around),
                                       midY + kfGpffff8aa8_cutProbeReach * std::sin(around),
                                       positionZ);
      const float rise = hit.height - positionZ;
      if (rise < kfGpffff8aa0_cutDropLimit || rise > kfGpffff8aa4_cutRiseLimit)
      {
        level = false;
        break;
      }
    }

    return level ? at(shortcut) : chosen;
  }

} // namespace orphen::ported::entity
