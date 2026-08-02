#pragma once

#include <cstdint>

namespace orphen::ported::entity
{

  // One slot of the original's entity pool at DAT_0058beb0.
  //
  // The pool is 256 slots of 0x1D8 bytes each, immediately followed by the
  // per-slot status array DAT_005a96b0 -- see
  // analyzed/entity_pool_and_descriptors.c for the three confirmations of that
  // stride, which is 0xEC *halfwords*, not 0xEC bytes.
  //
  // This is a field-named struct rather than a raw 472-byte block: the port
  // works with the fields it has ported and leaves the rest unmodelled. Field
  // names carry their PS2 byte offset so the mapping back to src/ stays
  // mechanical.
  //
  // Slot 0 is the lead player. The camera reads DAT_0058bed0, which is
  // 0x58BEB0 + 0x20 -- slot 0's +0x20 world X.
  struct OriginalEntity
  {
    // Identity, written by FUN_00229c40 from the type descriptor.
    std::int16_t typeId00 = 0;               // +0x00: entity type id.
    std::uint16_t descriptorFlags02 = 0;     // +0x02: descriptor +0x04.
    std::uint16_t halfword04 = 0;            // +0x04: descriptor +0x18.
    std::uint16_t flags06 = 0;               // +0x06: animation/contact flags used by FUN_002534d8.
    std::uint16_t halfword08 = 0;            // +0x08: descriptor +0x16, OR 0x20 when typeId < 0x272.

    std::uint32_t collisionFlags0c = 0;      // +0x0C: physics result flags from FUN_002262c0; bit 0 is grounded.
    float positionX20 = 0.0f;                // +0x20: world X.
    float positionZ24 = 0.0f;                // +0x24: world Z, mapped to PSM2 horizontal Y in the port.
    float positionY28 = 0.0f;                // +0x28: vertical position.
    float previousY2c = 0.0f;                // +0x2C: previous/smoothed vertical position.
    float desiredDeltaX30 = 0.0f;            // +0x30: per-frame X movement request consumed by physics.
    float desiredDeltaZ34 = 0.0f;            // +0x34: per-frame Z movement request consumed by physics.
    float desiredDeltaY38 = 0.0f;            // +0x38: per-frame vertical delta accumulated by physics.
    float velocityX3c = 0.0f;                // +0x3C: per-frame X velocity published by FUN_00256ab0.
    float velocityZ40 = 0.0f;                // +0x40: per-frame Z velocity published by FUN_00256ab0.
    float verticalVelocity44 = 0.0f;         // +0x44: vertical velocity/jump vector field.
    float verticalAcceleration48 = 24.0f;    // +0x48: downward acceleration used by FUN_002262c0.
    float groundHeight4c = 0.0f;             // +0x4C: sampled ground height; opcode 0x55 writes here.
    float previousGroundHeight50 = 0.0f;     // +0x50: previous sampled ground height.
    float radius54 = 0.35f;                  // +0x54: collision radius, descriptor +0x08.
    float height58 = 1.25f;                  // +0x58: collision height, descriptor +0x0C.
    float facingRadians5c = 0.0f;            // +0x5C: facing angle.
    std::uint16_t state60 = 0;               // +0x60: field/player movement state.
    std::uint32_t flagWord6c = 0;            // +0x6C: flag word the 0x8B warp mask tests.
    std::uint32_t rejectTerrainMask74 = 0;   // +0x74: reject terrain when 0x78-record +0x04 overlaps this mask.
    std::uint32_t requiredTerrainMask78 = 0; // +0x78: require common footprint terrain flags to overlap this mask.
    float maxStepHeight80 = 0.75f;           // +0x80: maximum step-up height accepted by FUN_002262c0.
    std::uint16_t animationA0 = 1;           // +0xA0: animation id; see FUN_00256bb8.
    std::uint16_t previousSubstateA2 = 0xffff;
    std::uint16_t stateResetA4 = 999;
    std::uint16_t substateFrameA8 = 0;
    std::uint16_t idleTimer1b6 = 0;          // +0x1B6: idle fidget timer, 16-bit wrap.
    std::uint8_t motionFlags1bb = 0;

    // Port-side bookkeeping with no PS2 offset. These are decisions the ported
    // code has already made this frame, not fields of the original struct.
    bool running = false;
    bool pendingJumpImpulse = false;

    // Index of the descriptor's model record, or -1 when unresolved. The
    // original stores a resolved pointer at +0x15C/+0x160; the port keeps the
    // index because it has no loaded model to point at.
    std::int32_t modelIndex = -1;
  };

} // namespace orphen::ported::entity
