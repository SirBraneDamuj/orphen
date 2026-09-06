#pragma once

#include <array>
#include <cstdint>

namespace orphen::ported::entity
{

  struct NavRecord;

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
    // +0x48: downward acceleration, FUN_002262c0:100. eeMemory.bin reads
    // 0.00075 for the lead, for all four party members and for the type 0x16
    // in s01_e012 -- the same value the player controller has always used.
    // The 24.0 that used to sit here was a placeholder nothing integrated.
    float verticalAcceleration48 = 0.000750000007f;
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
    // +0x7C: how far this entity will follow the floor *down* in one step.
    // FUN_002262c0:256 is its only reader -- a drop bigger than this leaves the
    // actor in the air instead of snapping it to the new surface. The lead's
    // copy is 10.0 (FUN_002b1568), and FUN_002596c8 stamps that same value onto
    // a follower so it takes the same drops the lead just took. The port's
    // movement step does not consult it yet; it is written because the original
    // writes it, so the field is there when that step grows the branch.
    float maxStepDown7c = 0.0f;
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
    // +0x96: bit 0 is raised by FUN_0023f8b8 the moment the entity is bound
    // into a battle actor record, so "this thing is a battle participant" can be
    // asked without walking the table. FUN_00216140's hit test and
    // FUN_002446e8 both gate on bits of this byte; neither is ported yet, so
    // only bit 0 is written.
    std::uint8_t battleFlags96 = 0;
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
    // +0xB0 and +0xB1..+0xB8: FUN_0020e840's motion trails. The low eight bits
    // of +0xAA are an enable mask over the model's header +0x38 table; +0xB0 is
    // last frame's copy of that byte, so an edge either way is visible, and the
    // eight bytes after it are the one-based handles into the 32-slot pool at
    // DAT_004FBC7C. Zero means "no slot held". The blade, grp_0179, runs two of
    // them through its animation 0 -- that is the sword trail.
    std::uint8_t previousTrailMaskB0 = 0;
    std::array<std::uint8_t, 8> trailSlotsB1{};
    // +0xBD: freeze / hit-stop countdown in frames, read by FUN_0023a068.
    std::int8_t freezeTimerBd = 0;

    // +0xBE: damage taken since the last tick, drained by FUN_002cd0a0.
    std::uint16_t pendingDamageBe = 0;
    // +0xC4: the direction the last hit came *from*. FUN_00258ab8 turns a
    // follower to face it (plus pi) before playing the stagger, so the
    // character reels away from the blow rather than in a fixed direction.
    float hitDirectionC4 = 0.0f;
    // +0xC8: a bias added to the attacker's +0x5C to get the direction it
    // stamps on a victim. Both hit tests read it; nothing in the executable
    // writes it, and it is zero on every entity in both EE dumps -- so the
    // stamped direction is the attacker's plain facing. Modelled because the
    // read is real, not because the value ever differs.
    float hitDirectionBiasC8 = 0.0f;

    // +0xBB: which kind of thing landed the last hit -- 1 when the attacker's
    // +0x02 carries 0x1001 (a player-side effect), 0 otherwise. Written by both
    // hit tests, FUN_002148a8 and FUN_00215670.
    std::uint8_t hitSourceKindBb = 0;
    // +0xBC: the hit record's byte 3, the reaction the victim should play.
    // FUN_00216140 stores it before it has decided on any damage.
    std::uint8_t hitReactionBc = 0;
    // +0xC0: the attack id FUN_00215670 copies out of its parameter block.
    // FUN_002148a8 only reads it, as a gate: a victim with a non-zero +0xC0 is
    // already committed to another attack this frame and is skipped.
    std::uint16_t hitSourceC0 = 0;
    // +0xC2: the hit record's halfword 0, the element/behaviour bit set. Kept
    // because FUN_00216140 tests bit 0x4000 on it and FUN_00273610 clears it.
    std::uint16_t hitFlagsC2 = 0;
    // +0xCC: the pool slot of whatever landed the last hit. The original stores
    // a pointer; the port stores the slot, and -1 for "nothing".
    std::int16_t lastAttackerSlotCc = -1;

    // +0xD0..+0xF3: the swept hit test's already-hit set, one bit per pool
    // slot, so a single swing cannot hit the same target twice.
    //
    // **The window really is nine words and the two halves disagree.**
    // FUN_002148a8 and FUN_00215670 both start a `uint *` at +0xD0 and step it
    // *before* the first slot, so slot n's bit lives in word `(n >> 5) + 1` --
    // +0xD4 for slots 0..31, up to +0xF0 for slots 224..255. FUN_00215e48
    // clears eight words from +0xD0 down, so it clears +0xD0 (which no slot
    // uses) and misses +0xF0 (which slots 224..255 do). Both halves are
    // reproduced here rather than tidied, because the seam is the behaviour: a
    // slot 224 or above stays flagged for the rest of the scene once hit.
    //
    // A second seam sits on top of it. +0xF0 and +0x100 are also where
    // FUN_002148a8 caches last frame's blade endpoints, so on the real machine
    // the word slots 224..255 test is the float bits of that cached X. The port
    // keeps the two as separate members; s01_e024 never fills the pool past
    // slot 29, so nothing can observe the difference.
    std::array<std::uint32_t, 9> alreadyHitD0{};

    // +0xF0 and +0x100: last frame's interpolated blade endpoints, in the
    // blade's own local space. FUN_002148a8 writes them every frame it runs and
    // reads them back on the next one -- they are what makes the test a sweep
    // rather than an instant. +0x06 bit 0x40 marks them as valid.
    std::array<float, 3> sweptPreviousAf0{};
    std::array<float, 3> sweptPreviousB100{};

    // +0x110..+0x120: the volume the hit tests measure against, which is *not*
    // +0x54/+0x58 even though FUN_00229ef0 fills both from the same numbers.
    // +0x110..+0x118 is a centre offset (zero on everything in s01_e024),
    // +0x11C the horizontal radius and +0x120 the height above +0x28.
    std::array<float, 3> hitVolumeOffset110{};
    float hitVolumeRadius11c = 0.15f;
    float hitVolumeHeight120 = 0.8f;

    // +0x124: the half-angle of the victim's guard arc, in radians. Zero means
    // "no guard", which is every enemy in s01_e024; a non-zero value makes
    // FUN_00216140 compare the hit direction against +0x5C and negate the
    // damage when the blow lands inside the arc.
    float guardArc124 = 0.0f;
    // +0x128: maximum hit points, the denominator of the HP bar.
    std::uint16_t maxHitPoints128 = 0;
    // +0x12E: defence, subtracted from the attacker's scaled +0x12C. The
    // difference is floored at 1, so a hit always costs at least one point.
    std::uint16_t defence12e = 0;
    // +0x130: the placement record's id byte for a spawned prop. Script opcode
    // 0xB7 also writes it outright, which is how s01_e012's init tags its cast.
    std::int16_t recordId130 = -1;
    // +0x132: the object record's +0x09, copied by FUN_0025E7C0. A non-zero
    // value is what makes it raise +0x02 bit 0x100 beside it; nothing else in
    // the port reads either yet.
    std::uint8_t objectByte132 = 0;
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
    // +0x195: the DAT_00343888 light slot this entity owns, -1 when it has
    // none. FUN_00256130 allocates one with FUN_00266050 when it spawns the
    // sword blade and FUN_002d21b8 drives the slot's position and colour from
    // there; nothing releases it explicitly, because the slot's radius is what
    // frees it and FUN_002d21b8 never zeroes that.
    std::int8_t lightSlot195 = -1;
    // +0x12C: attack power, the value FUN_00215670 subtracts the defender's
    // +0x12E from to get the damage a hit deals. FUN_00251dc0 fills it from the
    // party stat table (DAT_0034368F), which the port does not load, so it sits
    // at zero -- but the spawn paths copy it onto the effect entity that carries
    // the hit, so it is modelled rather than dropped.
    std::uint16_t attackPower12c = 0;
    // +0x133: a signed depth bias, copied from descriptor +0x02 by
    // FUN_00229c40:75. FUN_0020f510 scales it by DAT_003520a0 (0.08) and adds it
    // to the view depth before keying the GS z, so an effect descriptor carrying
    // the usual -12 pulls its sprite very nearly a whole unit toward the camera
    // without moving it on screen. Nothing else reads it.
    std::int8_t depthBias133 = 0;
    std::uint8_t fadeLevel134 = 0;           // +0x134: FUN_0023a568's fade-out level.
    std::uint32_t fadeColor138 = 0;          // +0x138: packed RGB ramp, 0x00FFFFFF when fully in.

    // +0x198: one field, meaning per type. Type 0x3A reads it as an event flag
    // id (the placement record's param + 0x400) -- see
    // analyzed/actor_behaviors/type_0x3A_treasure_chest.c -- and type 0x62 reads
    // it as the number of companion clones to spawn. Object register 0x38 writes
    // it, which is how s01_e024's script tells its enemy to bring five friends.
    std::uint32_t eventFlagId198 = 0;
    // +0x198 on an *effect* entity is the four attack bytes FUN_00216078
    // copied out of SCR.BIN resource 0xBE, packed little-endian the way the
    // original stores them. FUN_00256130 fills it when it spawns the sword
    // blade and FUN_002148a8 is the only reader; resource::HitParameters
    // unpacks it. Same halfword as eventFlagId198 in the original -- a blade is
    // never a chest and never an interaction candidate, so the three uses
    // cannot overlap.
    std::uint32_t hitParameters198 = 0;
    // +0x198 doubles as the player's interaction target: FUN_00252828 writes
    // the candidate there. Kept apart from eventFlagId198 because the port
    // stores a slot index, not a pointer, and the two uses never overlap -- the
    // chest is never the one interacting.
    std::int32_t interactTarget198 = -1;
    // +0x198 a third time, still on the lead player: whatever the current
    // action state has spawned, as a pool slot. State 0x1C (FUN_00256130) puts
    // the sword blade here and state 0x1D (FUN_002562b0) puts the magic
    // projectile here; the two states are exclusive, so one field covers both.
    //
    // The original stores it in the same word the interaction candidate uses
    // and does not clear the one before writing the other -- it re-reads the
    // word through a type test (`*effect == 0x42`) instead, which is the only
    // thing keeping a stale interaction target from being mistaken for a blade.
    // That test is reproduced where the slot is consumed, so the two readings
    // are kept in separate fields here the way eventFlagId198 and
    // interactTarget198 are.
    std::int32_t actionEffect198 = -1;
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

    // The *battle enemy* block: types 0x80, 0x8A and the twenty-eight others
    // whose behaviour is a preamble plus a dispatch on +0x60. See
    // ported/entity/original_battle_enemy.h.
    //
    // +0x198 is what FUN_0023f8b8 handed back, which is the battle actor record
    // **plus 0x0C** -- not the record itself. Every read the enemy makes off it
    // is therefore biased: +0x198 + 2 is the record's +0x0E pending action,
    // + 3 is +0x0F, + 0x20 is the +0x2C target and + 0x2C is the +0x38 flag
    // word. The port stores the record's own offset in the encounter blob and
    // does the bias in ActorEnvironment::battleActorRecord, so the field is
    // spelled as a plain record index; -1 is the original's unbound case, where
    // FUN_0023f8b8 returns the scratch at DAT_0031D178 rather than null.
    std::int32_t battleActorRecord198 = -1;
    // +0x19C: the facing the enemy is turning *toward*. State 0 seeds it with
    // the placement's own +0x5C and the idle default (FUN_0027f5c0) re-aims it
    // at the target -- pool slot 0, the player, when there is none.
    float battleDesiredFacing19c = 0.0f;
    // +0x1A0. The two types read this word as different things, and the
    // original can overlay them because a pool slot is a pointer there: type
    // 0x80 parks a plain reach -- 40.0 to close, 60.0 to strike, 30.0 to back
    // off -- and type 0x8A parks the *target*, which 0x80 keeps at +0x1A4
    // instead. The port cannot overlay a float on an index, so both names
    // exist and each type touches only its own.
    float enemyReach1a0 = 0.0f;
    std::int32_t enemyTargetSlot1a0 = -1;
    std::int32_t enemyTargetSlot1a4 = -1;
    // +0x1A4 and +0x1A8 on type 0x8A: the two effect entities an attack links
    // to itself -- the bite volume FUN_002ec920 spawns and the spit
    // FUN_002ecc68 does. Neither is spawned yet, so both stay -1 and the state
    // handlers take their "nothing in flight" branch, which is the branch the
    // original takes between attacks too.
    std::int32_t enemyAttackLink1a4 = -1;
    std::int32_t enemyAttackLink1a8 = -1;
    // +0x1AC on type 0x8A, where type 0x80 keeps the second control point of
    // its X arc -- the same word, two owners, kept apart here for the same
    // reason the pair above is. FUN_0028b740 stamps 2 on a Maneater that grew
    // where a spit landed, and its state 5 reads bit 0 back to decide whether
    // the corpse releases its record. Nothing in the port writes it yet.
    std::uint8_t enemySpawnFlag1ac = 0;
    // +0x1A8..+0x1C8 on type 0x80: three quadratic Bezier control points, one
    // array per axis, that FUN_0023a990 walks to carry a leaping enemy from
    // where it stood to a point two units past its target. +0x1CC accumulates
    // ticks and +0x62 is the total the ratio is taken against, so the leap
    // lasts exactly the travel time FUN_0023a6d0 costed when the order landed.
    std::array<float, 3> enemyArcX1a8{};
    std::array<float, 3> enemyArcZ1b4{};
    std::array<float, 3> enemyArcY1c0{};
    float enemyArcProgress1cc = 0.0f;
    // +0x1D0: FUN_00280850's wobble phase in degrees, swept 25 per frame and
    // wrapped from 60 back to -40. Type 0x80 only.
    float enemyWobblePhase1d0 = 0.0f;

    // Type 0x28's block. FUN_002d2f40 builds a three-entity rig the first time
    // it runs -- FUN_00265e28(0x27), (0x26) and (0x19) -- parks the three pool
    // slots at +0x198 / +0x19C / +0x1A0 and latches +0x94 so it never runs
    // again. Held apart from the other readings of those bytes for the same
    // reason eventFlagId198 and interactTarget198 are: the original reuses the
    // storage per type, and an entity is never two of these things at once.
    std::int32_t rigHair198 = -1;  // +0x198: the type 0x27 (hair) on the bust's role-1 bone
    std::int32_t rigBust19c = -1;  // +0x19C: the type 0x26 bust on this entity's role-1 bone
    std::int32_t rigCloth1a0 = -1; // +0x1A0: the type 0x19 cloth on the bust's role-2 bone

    // Type 0x44's block, the homing magic projectile. FUN_002d2e00 seeds it and
    // FUN_002d2470 spends it. Held apart from the readings above for the same
    // reason those are held apart from each other: the original reuses the
    // storage per type and an entity is never two of these things at once.
    //
    // +0x198: the entity it is chasing, as a pool slot, chosen once at spawn by
    // FUN_002d2ca8 and never revisited -- so the projectile locks on and stays
    // locked, which is why it can be dodged by moving after it is cast.
    std::int32_t homingTarget198 = -1;
    // +0x19C: units per tick. uGpffffa740, 0.0018 -- about 0.058 per frame.
    float projectileSpeed19c = 0.0f;
    // +0x1A0: the elevation it is travelling at, in radians. The yaw is the
    // ordinary +0x5C; this is the second angle a homing projectile needs and
    // the reason it can climb to something standing above it.
    float projectilePitch1a0 = 0.0f;
    // +0x1A4: how far it may turn per frame, ramped from 0 by 0.005 up to
    // 0.349 (20 degrees). It starts unable to turn at all, which is what makes
    // the first half of its flight look aimed rather than guided.
    float projectileTurnRate1a4 = 0.0f;
    // +0x1A8: ticks of homing left, 0x2580. Once it runs out the projectile
    // flies straight.
    std::uint16_t homingTimer1a8 = 0;
    // +0x1AA: ticks before the hit test is allowed to run, 0xA0 -- five frames,
    // so a projectile cannot hit whatever it was just launched out of.
    std::uint16_t hitCooldown1aa = 0;
    std::int32_t secondaryTarget1a4 = -1;    // +0x1A4: alternate target used when +0x1C4 == 2.
    float desiredFacing1a8 = 0.0f;           // +0x1A8: the angle state 3 turns toward.
    float desiredHeight1ac = 0.0f;           // +0x1AC: the height state 3 holds; also the party's move speed.
    // +0x1AC on a type 0x44 magic projectile: the four attack bytes
    // FUN_00216078 copied out of SCR.BIN resource 0xBE, packed the way the
    // original stores them. Record *1* of the caster's type, where the sword
    // blade takes record 0 -- `10 00 0a 00` for the lead player, element 4 at
    // +10%. FUN_00215ac8 is the only reader. Kept apart from desiredHeight1ac
    // because a projectile never holds a hover height and a flyer never carries
    // an attack record.
    std::uint32_t hitParameters1ac = 0;
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

    // The type 0x18F (399) cast marker, FUN_002d9c88 -- the ring on the ground
    // under a battle character. One is spawned per party member by
    // FUN_00242df0 and parked in DAT_0031da8c; it also spawns *copies of
    // itself* as the expanding pulses, and a copy is told to shrink and die by
    // being given a +0x198 that names another marker rather than a character
    // (its +0x95 is 0, which is the whole test).
    std::uint16_t markerCaster198 = 0;   // +0x198: the pool slot it rides.
    std::uint16_t markerCharge19a = 0;   // +0x19A: the charge, from FUN_002d9b78.
    std::uint16_t markerRingTimer19c = 0; // +0x19C: ticks to the next pulse.
    std::uint16_t markerFlags19e = 0;    // +0x19E: bit 0 suppresses one pulse cue.

    // +0x19E again, under the elemental-object reading: FUN_002F0608 parks the
    // type id the placement spawned -- a streamed 0x373-band id -- here before
    // it overwrites +0x00 with the 0x6C..0x74 element type the readout knows.
    // Nothing reads it back yet; it is stored because the original stores it.
    std::int16_t elementOriginalType19e = 0;

    // The type 0x192 target cursor, FUN_002d73e8 -- the marker drawn over an
    // enemy while a battle is running. One is spawned per bound actor record by
    // FUN_002d86b0 the frame the pre-battle countdown reaches zero, and its
    // pool slot is parked in the record's +0x0D. Yet another reading of the
    // same bytes; a cursor is never a marker, a fireball or a follower.
    std::int16_t cursorTarget19a = -1;   // +0x19A: the pool slot it rides.
    std::uint8_t cursorFlags198 = 0;     // +0x198: bit 4 hides it, bit 0x20 dims it.
    float cursorOffsetX1a0 = 0.0f;       // +0x1A0/+0x1A4/+0x1A8: a world offset,
    float cursorOffsetY1a4 = 0.0f;       // zero for a battle cursor and non-zero
    float cursorOffsetZ1a8 = 0.0f;       // only for the scripted markers.
    float cursorScreenX1ac = 0.0f;       // +0x1AC/+0x1B0/+0x1B4: where FUN_002d73e8
    float cursorScreenY1b0 = 0.0f;       // last projected it, which FUN_002d8690
    float cursorScreenZ1b4 = 0.0f;       // hands back to the on-screen pickers.
    // +0x28 again. A screen-space entity keeps FUN_0020b600's *integer* depth
    // word there rather than a world z, and FUN_0020f510's bit-0x1000 branch
    // reads it back as an int -- so the port holds it beside +0x28 rather than
    // reinterpreting a float.
    std::int32_t cursorProjectedDepth28 = 0;

    // The type 0x68 health bar, FUN_002d5748 -- the segmented gauge that slides
    // on when something takes a hit. Two live at once, pool slots 2 and 3, both
    // built by FUN_0022a418 and both screen-space (+0x08 bit 0x1000), so they
    // share the cursor's reading of +0x28 above rather than a world z.
    std::uint16_t healthBarPhase198 = 0;      // +0x198: 0..0x4F, the whole animation
    float healthBarY19c = 0.0f;               // +0x19C: the screen row it slides along
    float healthBarStep1a0 = 0.0f;            // +0x1A0: how far it slides per frame
    std::int32_t healthBarSegments1a4 = 0;    // +0x1A4: pips still lit, drains toward
    std::int32_t healthBarTarget1a8 = 0;      // +0x1A8: the pip count the hit leaves
    std::uint16_t healthBarAnimationBase1ac = 0; // +0x1AC: 0 for the lower bar, 0x14 upper

    // The four effect entities the two battle enemies throw. See
    // original_enemy_attack.h; each is a distinct reading of the same bytes,
    // written by exactly one spawner and read by exactly one behaviour.
    //
    //   0x10E  the flyer's shot         FUN_002ebad8 -> FUN_002eb990
    //   0x10F  the swoop's dust ring    FUN_002ebd20 -> FUN_002ebc30
    //   0x112  the Maneater's seed      FUN_002ec920 -> FUN_002ec750
    //   0x113  its poison spores        FUN_002ecc68 -> FUN_002ecb08
    //
    // The shot reads its attack record out of hitParameters198 above, which is
    // the same +0x198 the sword blade uses -- one field, one meaning, so it is
    // not repeated here.
    float shotDrop19c = 0.0f;   // +0x19C: type 0x10E, how fast the shot sinks
    float shotSpeed1a0 = 0.0f;  // +0x1A0: type 0x10E, units per 32000 ticks

    std::uint16_t puffLife198 = 0; // +0x198: type 0x10F, the lifetime its scale ramps against

    // Type 0x112. The same three-point Bezier the enemies fly, sixteen bytes
    // earlier in the struct.
    std::array<float, 3> seedArcX198{};
    std::array<float, 3> seedArcZ1a4{};
    std::array<float, 3> seedArcY1b0{};
    float seedProgress1bc = 0.0f; // +0x1BC

    float sporeSpeed198 = 0.0f;   // +0x198: type 0x113, units per 32000 ticks
    float sporeRise19c = 0.0f;    // +0x19C: and how fast it climbs
    std::int16_t sporeLife1a4 = 0; // +0x1A4: the lifetime its scale ramps against

    // The type 0x118 status aura, one per party member at DAT_0031DA7C.
    // FUN_002d8b38 arms it when a hit lands a status: it floats over the
    // victim, plays the icon for whichever status it was handed, and runs its
    // own +0x62 out. FUN_002d8ce0, the behaviour that draws it, is not ported.
    std::uint16_t auraVictim198 = 0; // +0x198: the pool slot it hangs over
    std::int16_t auraStatus19a = 0;  // +0x19A: which status, 0..12
    float auraHeight19c = 0.0f;      // +0x19C: the offset it floats at

    // The type 0x15B fireball, FUN_002dae60 -- what Hand of Pyro actually
    // launches. Another overlapping reading of the same bytes, seeded in one
    // place (FUN_002dab70) and read in one place, so nothing else can collide
    // with it.
    //
    // The chain is the interesting part. A cast produces exactly **one**
    // fireball; each fireball arms +0x62 with FUN_00248e48(8 - chargeLevel) and
    // spawns its own successor when that expires, passing `chainIndex + 1`.
    // FUN_002dab70 only arms the timer while `chargeLevel != chainIndex`, so the
    // chain stops after `chargeLevel + 1` of them -- which is why holding
    // Triangle longer throws more fire, with tighter spacing as the charge rises.
    float fireballOriginX19c = 0.0f; // +0x19C/+0x1A0/+0x1A4: where it was cast from,
    float fireballOriginZ1a0 = 0.0f; // kept so the successor spawns at the same
    float fireballOriginY1a4 = 0.0f; // point rather than at this one's position.
    float fireballVelX1a8 = 0.0f;    // +0x1A8/+0x1AC: horizontal velocity components.
    float fireballVelZ1ac = 0.0f;
    float fireballRise1b0 = 0.0f;    // +0x1B0: vertical rate, per tick.
    float fireballBaseFacing1b4 = 0.0f; // +0x1B4: the yaw the spread is measured from.
    float fireballRiseOffset1b8 = 0.0f; // +0x1B8: this frame's vertical trim.
    float fireballSpeed1bc = 0.0f;      // +0x1BC: horizontal speed, per tick.
    std::int16_t fireballTarget1c0 = 0; // +0x1C0: the pool slot it was aimed at, 0/1 for none.
    std::int16_t fireballCaster1c2 = 0; // +0x1C2: the pool slot that cast it.
    std::uint16_t fireballLife1c4 = 0;  // +0x1C4: ticks left; 0 destroys it.
    std::uint8_t fireballChain1c6 = 0;  // +0x1C6: position in the chain, 0 for the first.
    std::uint8_t fireballCharge1c7 = 0; // +0x1C7: the charge level the cast was released at.
    // +0x19B: the hit-spark variant, chargeLevel + 0x14, or 0x18 at full charge.
    std::uint8_t fireballSparkId19b = 0;
    // +0x96 bit 0x40: set on the first fireball of a chain only.
    std::uint8_t effectFlags96 = 0;

    // The Bite of Lightning burst block, shared by types 0x15C (the ground
    // disc FUN_002de650 lays down and the one FUN_002de9e8 plants on each
    // victim) and 0x178 (the one-shot flash). It **overlaps** the enemy and
    // fireball blocks above the same way those overlap each other -- +0x1AC is
    // the enemy's hover height and the fireball's horizontal velocity, +0x1B0
    // the wing phase and the rise rate -- and an entity is never two of these
    // at once.
    std::int16_t lightningTarget1ac = 0; // +0x1AC: the pool slot the cast was aimed at.
    std::int16_t lightningCaster1ae = 0; // +0x1AE: the pool slot that cast it.
    std::uint16_t lightningTimer1b0 = 0; // +0x1B0: FUN_00248e48(0x20), 32 frames of life.
    std::uint8_t lightningByte1b2 = 0;   // +0x1B2: cleared on spawn; nothing in src/ reads it.
    std::int8_t lightningLevel1b3 = 0;   // +0x1B3: the charge level, 1..5.

    // The type 0x37 party follower's block, PTR_FUN_0031e1a0's states. It
    // **overlaps** the two blocks above the same way they overlap each other --
    // +0x1B0..+0x1BC are the enemy's wing phase and home position, +0x1C0 its
    // attack roll, +0x1C8 its flags -- and an entity is never two of these
    // things at once, because opcode 0xAC stamps type 0x37 over whatever it was.
    //
    // FUN_002631f0 seeds +0x1A2 (180) and +0x1C6/+0x1C7; FUN_002596c8, state 0,
    // seeds +0x1BC.
    std::int16_t followSpeedBase1a2 = 0; // +0x1A2: base walk speed, in the 200000ths FUN_0025a500 divides by.
    // +0x1B0/+0x1B4/+0x1B8: the point this follower is walking to, in the
    // original's x/z/y order. Either the formation spot beside the lead
    // (FUN_00259520) or a navmesh cell centre (FUN_00259378).
    float followTargetX1b0 = 0.0f;
    float followTargetZ1b4 = 0.0f;
    float followTargetY1b8 = 0.0f;
    // +0x1BC: where in the ring around the lead this follower walks, as an
    // angle added to the lead's facing. FUN_002596c8 hands the first follower
    // +150 degrees and every later one the negation of the previous one's, so
    // two followers end up on opposite shoulders.
    float followFormationAngle1bc = 0.0f;
    // +0x198 again, this time as the follower's current path node: the
    // FUN_00355038 record FUN_00258c70 last handed back. The original keeps a
    // raw pointer there; so does this, for the same reason the other +0x198
    // aliases exist.
    const struct NavRecord *pathNode198 = nullptr;
    std::int16_t followPathNode1c0 = -1; // +0x1C0: waypoint index states 5 and 6 walk to.
    std::int8_t followSpot1c6 = 0;       // +0x1C6: which of three collision lanes this follower paths in.
    std::int8_t followPartySlot1c7 = 0;  // +0x1C7: the party slot it was bound to.
    std::int8_t followBumpCount1c8 = 0;  // +0x1C8: consecutive walls hit in state 2; three gives up to state 6.
    std::int8_t followStuckCount1c9 = 0; // +0x1C9: consecutive blocked frames in state 8.
    // +0x1CA: set while a *recovery* state is re-entering state 1, so state 1
    // skips its idle look-at and goes straight to the follow decision.
    std::int8_t followBlocked1ca = 0;

    // The type to resolve a model, descriptor or character class from.
    //
    // Type 0x38 is a *role*, not a character: opcode 0x66 stamps it over the
    // entity's real type when a scene takes an actor over for choreography, and
    // parks the real one at +0x1CE. Every original that needs the character
    // behind the role does this same test -- FUN_002298d0 for the class,
    // FUN_002658c0 for the walk animation. Looking a model up by the raw type
    // after 0x66 finds nothing, which is what stopped s01_e012's cast animating
    // and left the cutscene waiting on a frame counter that never advanced.
    // +0x15C in the original: the model FUN_00229c40 bound at spawn. **A later
    // rewrite of +0x00 does not move it**, which is what makes FUN_002de650's
    // retype trick work -- both of its spawns are allocated as type 0x174 and
    // then stamped 0x15C and 0x178, so they get 0x174's model and the other
    // two types' behaviour. -1 is "resolve the model from the type id", which
    // is what every normally spawned entity does; only a deliberate retype
    // fills it in.
    std::int16_t modelTypeId15c = -1;

    std::int16_t effectiveTypeId() const
    {
      if (modelTypeId15c >= 0)
      {
        return modelTypeId15c;
      }
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

    // +0x168..+0x180: seven per-quad rotation angles, in radians. **This
    // aliases the 42 per-bone override modes** the skinned path keeps at the
    // same offset -- a sprite entity carries +0x02 bit 0x200 and can never
    // reach FUN_0020C5A8, so the two uses never coexist. The port splits them:
    // bone modes live in EntityBoneOverrides, angles here. FUN_0020F510
    // reads them only when +0x08 carries bit 0x400, and then uses index 0 to
    // swing the quad's centre about the sprite origin and index N -- N being
    // the sprite record's own descending index -- to spin the quad about its
    // own centre. Every writer so far fills them all with the same value, so
    // the two indices only differ in principle.
    float spriteAngle168[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

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
