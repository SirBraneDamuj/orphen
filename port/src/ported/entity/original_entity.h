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
    // Defaults are type id 1's static descriptor (DAT_00318b68 + 0x00, the
    // record FUN_0022a418 falls back to when DAT_0058beb0 == 0 -- the lead
    // player's own type), read directly out of SLUS_200.11: radius0x08 =
    // 0.15, height0x0c = 0.8. Prior placeholders of 0.35/1.25 were guesses
    // and made the lead player noticeably wider and taller than every
    // descriptor-driven script actor, which use this same table.
    float radius54 = 0.15f;                  // +0x54: collision radius, descriptor +0x08.
    float height58 = 0.8f;                   // +0x58: collision height, descriptor +0x0C.
    float facingRadians5c = 0.0f;            // +0x5C: facing angle.
    std::uint16_t state60 = 0;               // +0x60: field/player movement state.
    std::uint16_t fadeRamp62 = 0;            // +0x62: FUN_0023a568's fade ramp position.
    // +0x68: the entity this one is currently holding as an interaction
    // target, as a pool slot rather than the original's pointer. -1 when none.
    std::int32_t interactTarget68 = -1;
    std::uint32_t flagWord6c = 0;            // +0x6C: flag word the 0x8B warp mask tests, and opcode 0x61's primary word.
    std::uint32_t flagWord70 = 0;            // +0x70: opcode 0x61's alternate word.
    std::uint32_t rejectTerrainMask74 = 0;   // +0x74: reject terrain when 0x78-record +0x04 overlaps this mask.
    std::uint32_t requiredTerrainMask78 = 0; // +0x78: require common footprint terrain flags to overlap this mask.
    float maxStepHeight80 = 0.75f;           // +0x80: maximum step-up height accepted by FUN_002262c0.

    // +0x94: FUN_00266240's last argument, a spawn parameter whose meaning is
    // per-type. Type 0x3A repurposes it as "this entity has ticked once".
    std::uint8_t spawnParam94 = 0;
    // +0x95: the byte next to it, object register 0x11. s01_e024's init writes it
    // once per party member. FUN_0025c8f8 writes it unsigned and FUN_0025c548
    // reads it back *signed*, which is the pair's only asymmetry.
    std::uint8_t byte95 = 0;
    // +0x98: index of the map placement record this entity was built from.
    // Written by both of FUN_0025eb48's branches; opcode 0x5A searches on it.
    std::int32_t placementRecordIndex98 = -1;

    // +0xA0: animation id, an index into the table at +0x9C. FUN_00229c40
    // leaves it at the slot clear's zero; only FUN_00225bc8 ever selects one.
    std::uint16_t animationA0 = 0;
    std::uint16_t previousSubstateA2 = 0xffff;
    // +0xA4: state timer. FUN_00225bc8 resets it to 999 on an animation change,
    // and FUN_0023a068 advances it by the frame tick while the entity is frozen
    // so a timed state does not lose the frozen frames.
    std::uint16_t stateResetA4 = 999;
    std::uint16_t substateFrameA8 = 0;
    std::uint16_t flagsAa = 0;               // +0xAA: bit 0x100 gates type 0x3A's effect.
    // +0xBD: freeze / hit-stop countdown in frames, read by FUN_0023a068.
    std::int8_t freezeTimerBd = 0;

    // +0xBE: damage taken since the last tick, drained by FUN_002cd0a0.
    std::uint16_t pendingDamageBe = 0;
    std::int16_t recordId130 = -1;           // +0x130: the placement record's id byte.
    std::uint16_t staggerTimer12a = 0;       // +0x12A: hit points the +0xBE drain eats into.
    // +0x192: the interaction candidate gate FUN_00252a18 tests, signed and
    // required negative. FUN_00229c40 seeds it to 0xFFFF for every entity it
    // builds, so an entity is interactable until something takes it out.
    std::int16_t interactGate192 = -1;
    std::uint8_t fadeLevel134 = 0;           // +0x134: FUN_0023a568's fade-out level.
    std::uint32_t fadeColor138 = 0;          // +0x138: packed RGB ramp, 0x00FFFFFF when fully in.

    // +0x198: one field, meaning per type. Type 0x3A reads it as an event flag
    // id (the placement record's param + 0x400) -- see
    // analyzed/actor_behaviors/type_0x3A_treasure_chest.c -- and type 0x62 reads
    // it as the number of companion clones to spawn. Object register 0x38 writes
    // it, which is how s01_e024's script tells its enemy to bring five friends.
    std::uint32_t eventFlagId198 = 0;
    // +0x198 doubles as the player's interaction target: FUN_00252828 writes
    // the candidate there. Kept apart from eventFlagId198 because the port
    // stores a slot index, not a pointer, and the two uses never overlap -- the
    // chest is never the one interacting.
    std::int32_t interactTarget198 = -1;
    std::uint16_t interactParam1b8 = 0;      // +0x1B8: 0x4B00 for the chest path.
    std::uint16_t effectTimer19c = 0;        // +0x19C: type 0x3A one-shot effect timer.
    std::uint8_t effectActive19e = 0;        // +0x19E: that timer's enable.

    // Type 0x62's own block, all written by FUN_002cd210 and read by its states.
    std::int32_t targetIndex19c = 0;         // +0x19C: pool index of the chase target.
    std::int32_t targetSlot1a0 = -1;         // +0x1A0: that target, resolved.
    std::int32_t secondaryTarget1a4 = -1;    // +0x1A4: alternate target used when +0x1C4 == 2.
    float desiredFacing1a8 = 0.0f;           // +0x1A8: the angle state 3 turns toward.
    float desiredHeight1ac = 0.0f;           // +0x1AC: the height state 3 holds; also the party's move speed.
    float homeX1b4 = 0.0f;                   // +0x1B4: spawn position, three floats.
    float homeZ1b8 = 0.0f;
    float homeY1bc = 0.0f;
    std::int16_t attackChance1c0 = 0;        // +0x1C0: attack roll threshold out of 1000.
    std::uint16_t hitFlash1c2 = 0;           // +0x1C2: hit tint countdown.
    std::int16_t alertState1c4 = 0;          // +0x1C4: 1 forces state 2, 2 selects the alternate target.
    std::uint16_t repathTimer1c6 = 0;        // +0x1C6: FUN_002cdb28's re-roll period.
    std::uint8_t enemyFlags1c8 = 0;          // +0x1C8: 0 halves state 3's repath period.
    // +0x14C: the entity's size scale, applied to the descriptor's radius and
    // height by FUN_00229ef0. 1.0 for a normally spawned actor; the type 0x62
    // leader gives its clones its own scale times 0.7.
    float scale14c = 1.0f;

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
