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
    // +0x0A: the map primitive the last ground sample landed on, written by
    // FUN_00227070 and read back by FUN_002262c0:41-85 -- negative means "no
    // cached surface". The original compares the *material* byte of the cached
    // primitive against the newly sampled one and only lifts the actor when
    // they agree; the port compares the primitive index, which is narrower but
    // needs no material table. See actor_frame_update's floor block.
    std::int16_t groundPrimitive0a = -1;

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
    // +0x64: the entity this one's movement was blocked against this frame.
    // FUN_002262c0 clears it at the top of every solve and the four clamp
    // functions store the blocker's pool pointer here. Kept as a slot index
    // with -1 for "none", matching interactTarget68 rather than the original's
    // null pointer.
    std::int32_t blockedBy64 = -1;
    // +0x68: the entity this one is currently holding as an interaction
    // target, as a pool slot rather than the original's pointer. -1 when none.
    std::int32_t interactTarget68 = -1;
    std::uint32_t flagWord6c = 0;            // +0x6C: flag word the 0x8B warp mask tests, and opcode 0x61's primary word.
    std::uint32_t flagWord70 = 0;            // +0x70: opcode 0x61's alternate word.
    std::uint32_t rejectTerrainMask74 = 0;   // +0x74: reject terrain when 0x78-record +0x04 overlaps this mask.
    std::uint32_t requiredTerrainMask78 = 0; // +0x78: require common footprint terrain flags to overlap this mask.
    // +0x80 is the **maximum walkable slope**, in radians, not a step height.
    // FUN_002262c0's only use of it is
    //   if ((float)puVar11[2] <= *(float *)(iVar12 + 0x80))
    // where workspace +0x08 is the destination surface's stored slope angle
    // (record78 +0x70). Type 1's descriptor value is 0.872665 = 50 degrees,
    // which is also the constant FUN_0022d258 compares the same field against.
    // The step height is a global, DAT_00352434 = 0.26.
    float slopeLimit80 = 0.872664626f;

    // +0x84..+0x90: the four corner heights FUN_00227070:133-138 publishes after
    // a four-corner sample, in its own order -- (-r,-r), (+r,-r), (+r,+r),
    // (-r,+r). FUN_002262c0:130-159 reads them back to decide which way an actor
    // is overhanging, so they are state, not diagnostics.
    float cornerHeight84 = 0.0f;
    float cornerHeight88 = 0.0f;
    float cornerHeight8c = 0.0f;
    float cornerHeight90 = 0.0f;

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
    // +0xA6/+0xA8/+0xAA/+0xAC/+0xAE and +0x13C: FUN_00225c90's animation
    // cursor. +0xA8 steps by 2 per six-byte timeline entry, +0xAC is the pose
    // column every bone track is indexed by, +0xAE the column it came from,
    // and +0x13C the blend between them.
    //
    // **+0xA8 has exactly one owner.** It used to be modelled twice -- once here
    // as the timeline cursor and once as a per-frame "substate frame" the player
    // controller incremented -- and script object register 6, which is
    // `param_1[0x54]` in FUN_0025c548, addresses this same halfword. A cutscene
    // that sets an animation and then polls register 6 for a keyframe therefore
    // watched a counter nothing was advancing, which is what stalled s01_e012's
    // opening. FUN_002534d8's jump-startup tests read it too, as keyframes
    // rather than frames.
    std::uint16_t keyframeTicksA6 = 0;
    std::uint16_t timelineCursorA8 = 0;
    std::uint16_t poseColumnAc = 0;
    std::uint16_t previousPoseColumnAe = 0;
    float animationBlend13c = 1.0f;
    std::uint16_t flagsAa = 0;               // +0xAA: bit 0x100 gates type 0x3A's effect.
    // +0xBD: freeze / hit-stop countdown in frames, read by FUN_0023a068.
    std::int8_t freezeTimerBd = 0;

    // +0xBE: damage taken since the last tick, drained by FUN_002cd0a0.
    std::uint16_t pendingDamageBe = 0;
    // +0x130: the placement record's id byte for a spawned prop. Script opcode
    // 0xB7 also writes it outright, which is how s01_e012's init tags its cast.
    std::int16_t recordId130 = -1;
    std::uint16_t staggerTimer12a = 0;       // +0x12A: hit points the +0xBE drain eats into.
    // +0x192: the pool slot of the entity this one is *attached to*, -1 when it
    // stands on its own. FUN_00229c40 seeds it to 0xFFFF for everything it
    // builds. FUN_0020cdc0 branches on it to build the world matrix, FUN_0020dc88
    // walks it to the root of an attachment chain, and FUN_00252a18 requires it
    // negative -- so "interactable" really means "not attached to anything".
    std::int16_t parentSlot192 = -1;
    // +0x194: which of the parent's bones to ride, as a *signed byte*. Negative
    // means follow the bone's position only, keeping this entity's own facing
    // (FUN_0020cdc0's middle branch); non-negative means inherit the whole bone
    // matrix. Only meaningful when +0x192 names a parent.
    std::int8_t attachBone194 = 0;
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
    // +0x19C on the *lead player*: the entity holding the item a chest just
    // gave up, as a pool slot rather than the original's pointer. Zero means
    // there is none, which is what routes the chest cutscene's state 0x0F
    // straight to 0x12 instead of through the item display.
    std::int32_t itemEntity19c = 0;
    std::uint8_t effectActive19e = 0;        // +0x19E: that timer's enable.

    // Type 0x62's own block, all written by FUN_002cd210 and read by its states.
    std::int32_t targetIndex19c = 0;         // +0x19C: pool index of the chase target.
    std::int32_t targetSlot1a0 = -1;         // +0x1A0: that target, resolved.
    // +0x1A0 again, under the party reading: the type this entity had before
    // opcode 0xAC made it a type 0x37 follower, which FUN_002589c0 restores when
    // the slot is released. Held separately from targetSlot1a0 rather than
    // aliased, the same way eventFlagId198 and interactTarget198 are -- an
    // entity is never both a party follower and a type 0x62 enemy.
    std::int16_t partyOriginalType1a0 = 0;

    // Type 0x28's block. FUN_002d2f40 builds a three-entity rig the first time
    // it runs -- FUN_00265e28(0x27), (0x26) and (0x19) -- parks the three pool
    // slots at +0x198 / +0x19C / +0x1A0 and latches +0x94 so it never runs
    // again. Held apart from the other readings of those bytes for the same
    // reason eventFlagId198 and interactTarget198 are: the original reuses the
    // storage per type, and an entity is never two of these things at once.
    std::int32_t rigHair198 = -1;  // +0x198: the type 0x27 (hair) on the bust's role-1 bone
    std::int32_t rigBust19c = -1;  // +0x19C: the type 0x26 bust on this entity's role-1 bone
    std::int32_t rigCloth1a0 = -1; // +0x1A0: the type 0x19 cloth on the bust's role-2 bone
    std::int32_t secondaryTarget1a4 = -1;    // +0x1A4: alternate target used when +0x1C4 == 2.
    float desiredFacing1a8 = 0.0f;           // +0x1A8: the angle state 3 turns toward.
    float desiredHeight1ac = 0.0f;           // +0x1AC: the height state 3 holds; also the party's move speed.
    // +0x1B0: FUN_002cdb28's wing phase, in degrees. Ramps 25 per frame and
    // wraps from 60 back to -40, so one flap is four frames.
    float wingPhase1b0 = 0.0f;
    float homeX1b4 = 0.0f;                   // +0x1B4: spawn position, three floats.
    float homeZ1b8 = 0.0f;
    float homeY1bc = 0.0f;
    std::int16_t attackChance1c0 = 0;        // +0x1C0: attack roll threshold out of 1000.
    std::uint16_t hitFlash1c2 = 0;           // +0x1C2: hit tint countdown.
    std::int16_t alertState1c4 = 0;          // +0x1C4: 1 forces state 2, 2 selects the alternate target.
    std::uint16_t repathTimer1c6 = 0;        // +0x1C6: FUN_002cd0a0's FUN_002cde50 period.
    std::uint8_t enemyFlags1c8 = 0;          // +0x1C8: 0 halves state 3's repath period.

    // The script-driven NPC block, type 0x38. It **overlaps** the type 0x62
    // fields above -- +0x1C0, +0x1C4, +0x1C6 and +0x1C8 are the same bytes under
    // both readings, and the fields here are only the offsets the enemy has no
    // name for. Opcode 0x66 converts an entity to type 0x38 and clears all of
    // them together, so a slot is never both things at once.
    //
    // +0x1BC is the step counter the choreography opcodes advance: 0xEE..0xF1
    // increment it on arrival, 0xEC writes it and 0xED reads it. That counter is
    // how a cutscene keeps its place across frames, because the VM has no yield
    // and every script slot restarts from the top each frame.
    std::uint8_t stepCounter1bc = 0;
    // +0x1BE: which movement opcode ran last, so 0xEE..0xF1 can tell a fresh
    // move from a continuing one and only stamp the animation once.
    std::int16_t lastMoveOpcode1be = 0;
    // +0x1C4 and +0x1C8 again, as the choreography opcodes read them: the turn
    // rate in radians per frame and the angle being turned toward. Held apart
    // from alertState1c4 / enemyFlags1c8 for the same reason the other unions
    // are -- an entity is never both a script-driven actor and a type 0x62.
    float npcTurnRate1c4 = 0.0f;
    float npcTargetAngle1c8 = 0.0f;
    std::uint16_t npcWord1ca = 0;            // +0x1CA
    // +0x1CC: the "was just interacted with" byte. FUN_0025b978 sets it on a
    // type 0x38 before running the interaction entry; opcode 0xE9 reads and
    // clears it.
    std::uint8_t interactPulse1cc = 0;
    // +0x1CE: the type this entity had before 0x66 made it a 0x38. Opcode 0xF1
    // and FUN_002298d0 read it to recover the real character class.
    std::int16_t originalType1ce = 0;

    // The type to resolve a model, descriptor or character class from.
    //
    // Type 0x38 is a *role*, not a character: opcode 0x66 stamps it over the
    // entity's real type when a scene takes an actor over for choreography, and
    // parks the real one at +0x1CE. Every original that needs the character
    // behind the role does this same test -- FUN_002298d0 for the class,
    // FUN_002658c0 for the walk animation. Looking a model up by the raw type
    // after 0x66 finds nothing, which is what stopped s01_e012's cast animating
    // and left the cutscene waiting on a frame counter that never advanced.
    std::int16_t effectiveTypeId() const
    {
      if (typeId00 == 0x38)
      {
        return originalType1ce;
      }
      // 0x37 is the same idea one rung up: opcode 0xAC stamps it over a
      // follower's real type and parks that at +0x1A0, which FUN_002589c0
      // restores when the slot is released.
      if (typeId00 == 0x37)
      {
        return partyOriginalType1a0;
      }
      return typeId00;
    }
    // +0x14C: the entity's size scale, applied to the descriptor's radius and
    // height by FUN_00229ef0. 1.0 for a normally spawned actor; the type 0x62
    // leader gives its clones its own scale times 0.7.
    float scale14c = 1.0f;
    // +0x150/+0x154/+0x158: the rest of what FUN_0020cdc0 feeds FUN_0020cf28 to
    // build the entity's root matrix -- z scale, then pitch and roll layered on
    // top of the facing in +0x5C. FUN_00229c40 sets +0x14C and +0x150 to 1.0
    // and leaves the two angles at the slot clear's zero.
    float scaleZ150 = 1.0f;
    float rotationX154 = 0.0f;
    float rotationY158 = 0.0f;

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
