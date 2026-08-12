#pragma once

// Waypoint path-follow: what actually walks a cutscene actor from A to B.
//
//   src/FUN_002443f8.c   start   -- allocate a slot, decode the path, build the spline
//   src/FUN_002445c8.c   poll    -- progress, non-zero while still walking
//   src/FUN_00244318.c   alloc   -- find a free slot in the kind-2 array
//   src/FUN_002446e8.c   update  -- per frame, sample the spline and steer
//
// Reached from script opcode `0xBD`, whose handler FUN_00263e80 calls
// FUN_00242a18 with the *selected entity* as param_1 -- it is a method table on
// an entity. Method `0x70` starts, method `0x72` polls. The scene script's shape
// is: call 0x70, and only if it succeeds queue a subproc that spins on 0x72
// until it reads 0.
//
// **This is not the choreography opcode family.** `0xEE`..`0xF1` step an actor a
// fixed pace per frame toward a point; this drives it along a *cubic spline*
// through authored waypoints over a fixed duration, and it is what the opening
// of s01_e012 uses. Dortin's is path `0x366C`, three points, duration 400:
// (5.652,-3.472) -> (5.461,-2.757) -> (5.084,-2.217), walking him at Volcan.
//
// The follower writes into the entity's movement request at +0x30/+0x34, so
// everything downstream -- collision clamps, terrain, the ground follow --
// applies exactly as it does to any other motion.

#include "ported/camera/original_camera_path.h"
#include "ported/entity/entity_pool.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace orphen::ported::entity
{

  using orphen::ported::psm2::Vec3;

  // The manager at DAT_00354fa8 sizes both of its arrays at 3 in the save
  // state (+0x4C and +0x54).
  inline constexpr std::size_t kPathFollowerCount = 3;
  // FUN_00266a78 refuses more than this.
  inline constexpr std::size_t kMaxPathWaypoints = orphen::ported::camera::kMaxSplinePoints;

  // One entry of the kind-2 array, 0x2D8 bytes in the original. Offsets are its
  // own; the spline replaces the raw coefficient block at +0xD4.
  struct PathFollower
  {
    std::uint16_t mode00 = 0;      // +0x00: 1/2/3 by duration, 0 = free
    std::uint16_t count02 = 0;     // +0x02: waypoint count
    std::uint16_t total04 = 0;     // +0x04: duration << 4
    std::uint16_t elapsed06 = 0;   // +0x06
    std::uint16_t flags08 = 0;     // +0x08: bit 0 snap, bit 1 do not steer
    std::uint8_t steer09 = 0;      // +0x09
    std::uint8_t soundBit0b = 0;   // +0x0B
    std::uint16_t soundId0c = 0;   // +0x0C
    std::int32_t entitySlot10 = -1; // +0x10: the entity pointer
    std::size_t waypointCount = 0;
    std::array<Vec3, kMaxPathWaypoints> waypoints{}; // +0x14, 12-byte stride
    std::array<orphen::ported::camera::CubicSpline, 3> spline{}; // +0xD4
  };

  class PathFollowerTable
  {
  public:
    void reset();

    // FUN_002443f8. `waypoints` are already decoded and scaled -- the caller
    // owns that, because the path is a list of VM expressions in the script blob
    // and only the interpreter can evaluate them. Returns FUN_002443f8's own
    // result: 1 on success, -1 when no slot was free.
    int FUN_002443f8_start(std::size_t entitySlot, std::span<const Vec3> waypoints,
                           std::uint32_t duration);

    // FUN_002445c8. `((total - elapsed) * 1000) / total + 1` for a live slot, so
    // non-zero the whole way; 0 once the slot is gone, which is what the script's
    // wait loop tests.
    int FUN_002445c8_progress(std::size_t entitySlot) const;

    // FUN_002446e8. Must run *before* the actor loop: it writes +0x30/+0x34 and
    // the physics pass in that loop is what spends them.
    void FUN_002446e8_update(EntityPool &pool, std::uint32_t frameTicks);

    std::uint32_t activeCount() const;
    std::uint32_t startedCount() const { return started_; }

  private:
    // FUN_00244318(2): first slot that is free, or whose entity has gone away.
    PathFollower *FUN_00244318_allocate();

    std::array<PathFollower, kPathFollowerCount> slots_{};
    std::uint32_t started_ = 0;
  };

} // namespace orphen::ported::entity
