#include "ported/entity/original_element_object.h"

#include "ported/battle/battle_tables.h"
#include "ported/entity/original_hit_test.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_00354B74. The same 0.08 the billboard pass scales +0x133 by; see
    // original_entity.h.
    constexpr float kDAT_00354b74_depthBiasUnit = 0.07999999821186066f;

    // FUN_0030BD20, the EE's float-to-int truncation.
    int FUN_0030bd20_trunc(float value) { return static_cast<int>(value); }

    // The lamp's constants, all read out of SLUS_200.11.
    constexpr float kDAT_00352724_droppedBody = 0.00999999977648258f;    // 0x3C23D70A
    constexpr float kDAT_00352728_dropNudge = 0.0010000000474974513f;    // 0x3A83126F
    constexpr float kDAT_0035272c_burstScale = 0.699999988079071f;       // 0x3F333333
    constexpr float kDAT_00352730_twoPi = 6.283184051513672f;            // 0x40C90FD8
    constexpr float kFlameScale = 2.0f;                                  // lui 0x4000
    // DAT_00354F88, FUN_002484D0's only reader and nothing writes it: element
    // 0, +10% power, reaction 0.
    constexpr std::uint32_t kDAT_00354f88_burnHit = 0x000A0001u;
    constexpr std::int32_t kBurnFrames = 0x4B0;
    constexpr std::int16_t kFlameType = 0x47;
    constexpr std::int16_t kBurstType = 0x122;

    constexpr std::int8_t kPhaseFalling = 100; // 'd'
    constexpr std::int8_t kPhaseLanded = 101;  // 'e'
    constexpr std::int8_t kPhaseBurning = 102; // 'f'

    // FUN_002484D0:96-127, the ring of flames. Returns false when the pool
    // ran out, which in the original is a `return` out of the whole function.
    bool spawnFlames(OriginalEntity &lamp, std::size_t lampSlot, const ActorEnvironment &environment)
    {
      if (environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return false;
      }
      EntityPool &pool = *environment.entityPool;
      for (int flame = 0; flame < 10; ++flame)
      {
        // `divu`, not `div`: Ghidra prints the two remainders as signed and
        // casts the second through uint, but both are unsigned, so the angle
        // is one of 36 ten-degree steps and the reach 0.50..0.79.
        const std::uint32_t angleRoll = environment.random ? environment.random() : 0u;
        const std::uint32_t degrees = (angleRoll % 0x24u) * 10u;
        const float angle = (static_cast<float>(static_cast<std::int32_t>(degrees)) * kDAT_00352730_twoPi) / 360.0f;
        const std::uint32_t reachRoll = environment.random ? environment.random() : 0u;
        const float reach = static_cast<float>(static_cast<std::int32_t>(reachRoll % 0x1Eu)) / 100.0f + 0.5f;
        // FUN_00305130 is cosf, FUN_00305218 sinf.
        const float x = lamp.positionX20 + reach * std::cos(angle);
        const float y = lamp.positionZ24 + reach * std::sin(angle);
        const float height = lamp.positionY28;

        // FUN_002D6C68: FUN_00265E28 plus a diagnostic when the pool is full.
        const std::size_t spawned = pool.FUN_00265e28_allocate_and_initialize(kFlameType, *environment.descriptors);
        if (spawned >= kEntitySlotCount)
        {
          return false;
        }
        auto &piece = pool.slot(spawned);
        piece.scale14c = kFlameScale;
        piece.scaleZ150 = kFlameScale;
        piece.positionX20 = x;
        piece.halfword04 = static_cast<std::uint16_t>((piece.halfword04 & 0xFFF7u) | 0x10u);
        piece.descriptorFlags02 = static_cast<std::uint16_t>((piece.descriptorFlags02 & 0xFFF7u) | 0x1000u);
        piece.positionZ24 = y;
        // +0x198 is the lamp's address in the original; LAB_002D05C0 reads its
        // type through it.
        piece.breakParent198 = static_cast<std::int16_t>(lampSlot);
        piece.facingRadians5c = angle;
        piece.positionY28 = height;
      }
      return true;
    }
  } // namespace

  std::int32_t FUN_002d6ce0_spawn_burst(float scaleX,
                                        float scaleZ,
                                        std::uint16_t animation,
                                        float x,
                                        float y,
                                        float z,
                                        const ActorEnvironment &environment)
  {
    if (environment.entityPool == nullptr || environment.descriptors == nullptr)
    {
      return -1;
    }
    EntityPool &pool = *environment.entityPool;
    const std::size_t spawned = pool.FUN_00265e28_allocate_and_initialize(kBurstType, *environment.descriptors);
    if (spawned >= kEntitySlotCount)
    {
      return -1;
    }
    auto &burst = pool.slot(spawned);
    burst.scale14c = scaleX;
    burst.scaleZ150 = scaleZ;
    burst.animationA0 = animation;
    burst.positionX20 = x;
    burst.halfword08 = static_cast<std::uint16_t>(burst.halfword08 | 0x4040u);
    burst.positionZ24 = y;
    burst.positionY28 = z;
    burst.groundHeight4c = z;
    burst.previousGroundHeight50 = z;
    // FUN_00225740(+0x164, -1) resets every animation channel of the new
    // entity's model; a slot fresh out of FUN_00265E28 has nothing to reset.
    return static_cast<std::int32_t>(spawned);
  }

  void LAB_00239f80_burst(OriginalEntity &entity,
                          std::size_t slot,
                          const ActorEnvironment &environment)
  {
    if (static_cast<std::int8_t>(entity.spawnParam94) == 0)
    {
      entity.spawnParam94 = static_cast<std::uint8_t>(entity.spawnParam94 + 1);
    }
    if ((entity.flags06 & 1u) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

  void FUN_002f11c8_lamp(OriginalEntity &lamp,
                         std::size_t slot,
                         const ActorEnvironment &environment)
  {
    // 0x002F11C8: the type's own two bits, then the tail call.
    lamp.descriptorFlags02 = static_cast<std::uint16_t>(lamp.descriptorFlags02 & 0xFE7Fu);

    // FUN_002484D0:26-41.
    if (static_cast<std::int8_t>(lamp.spawnParam94) == 1)
    {
      lamp.halfword04 = static_cast<std::uint16_t>(lamp.halfword04 & 0xFFEFu);
    }
    if (static_cast<std::int8_t>(lamp.spawnParam94) == 0)
    {
      lamp.pendingDamageBe = 0;
      lamp.fadeRamp62 = 0;
      lamp.halfword04 = static_cast<std::uint16_t>(lamp.halfword04 & 0xFFEFu);
      lamp.spawnParam94 = 1;
    }
    if (lamp.freezeTimerBd != 0)
    {
      lamp.freezeTimerBd = 0;
    }
    if ((lamp.descriptorFlags02 & 0x1000u) == 0)
    {
      lamp.descriptorFlags02 = static_cast<std::uint16_t>(lamp.descriptorFlags02 | 8u);
    }
    lamp.hitVolumeHeight120 = 1.0f;
    lamp.radius54 = 1.0f;
    lamp.height58 = 1.0f;
    lamp.hitVolumeRadius11c = 1.0f;

    // :128-142. Struck: let go.
    if (lamp.pendingDamageBe != 0)
    {
      lamp.pendingDamageBe = 0;
      lamp.staggerTimer12a = 0;
      lamp.descriptorFlags02 = static_cast<std::uint16_t>((lamp.descriptorFlags02 & 0xFFF7u) | 0x1000u);
      lamp.halfword04 = static_cast<std::uint16_t>((lamp.halfword04 & 0xFFF7u) | 0x10u);
      lamp.desiredDeltaY38 = kDAT_00352728_dropNudge;
      lamp.hitVolumeRadius11c = kDAT_00352724_droppedBody;
      // A bare store, not FUN_00225BC8.
      lamp.animationA0 = 4;
      lamp.spawnParam94 = static_cast<std::uint8_t>(kPhaseFalling);
      lamp.desiredDeltaX30 = kDAT_00352728_dropNudge;
      lamp.radius54 = kDAT_00352724_droppedBody;
      lamp.height58 = kDAT_00352724_droppedBody;
      FUN_00215e48_clear_hit_set(lamp);
      return;
    }

    // :47-58. Falling, until FUN_002262C0 reports ground under it.
    if (static_cast<std::int8_t>(lamp.spawnParam94) == kPhaseFalling)
    {
      if ((lamp.collisionFlags0c & 4u) != 0)
      {
        lamp.fadeRamp62 = static_cast<std::uint16_t>(orphen::ported::battle::FUN_00248e48_arm_timer(kBurnFrames));
        lamp.spawnParam94 = static_cast<std::uint8_t>(kPhaseLanded);
      }
      lamp.attackPower12c = 1;
      lamp.defence12e = 0;
      return;
    }

    // :61-93. Burning.
    if (static_cast<std::int8_t>(lamp.spawnParam94) >= kPhaseBurning)
    {
      if ((lamp.flags06 & 1u) != 0)
      {
        FUN_00215e48_clear_hit_set(lamp);
      }
      if (environment.hitTest != nullptr)
      {
        const auto parameters = orphen::ported::resource::HitParameters::unpack(kDAT_00354f88_burnHit);
        const std::int8_t contacts = FUN_002148a8_swept_hit_test(lamp, slot, parameters, *environment.hitTest);
        // A burst on the first slot DAT_003151C8 names, and only the first.
        if (contacts != 0 && environment.hitTest->DAT_003151c8_hitList != nullptr &&
            environment.entityPool != nullptr)
        {
          const auto &hitList = *environment.hitTest->DAT_003151c8_hitList;
          for (std::size_t index = 0; index < hitList.size() && index < 0x100; ++index)
          {
            if (static_cast<std::int16_t>(hitList[index]) >= 0)
            {
              const auto &victim = environment.entityPool->slot(hitList[index]);
              FUN_002d6ce0_spawn_burst(kDAT_0035272c_burstScale, kDAT_0035272c_burstScale, 1,
                                       victim.positionX20, victim.positionZ24, victim.positionY28,
                                       environment);
              break;
            }
          }
        }
      }
      if (lamp.fadeRamp62 != 0)
      {
        lamp.fadeRamp62 = orphen::ported::battle::FUN_00248e58_step_timer(
            lamp.fadeRamp62, static_cast<std::uint16_t>(environment.frameTicks));
        if (lamp.fadeRamp62 == 0)
        {
          FUN_00265ec0_destroy_entity(slot, environment);
          return;
        }
      }
    }

    // :94-127. Landed: light the ring, once.
    if (static_cast<std::int8_t>(lamp.spawnParam94) == kPhaseLanded)
    {
      lamp.spawnParam94 = static_cast<std::uint8_t>(kPhaseBurning);
      spawnFlames(lamp, slot, environment);
    }
  }

  bool FUN_002f0608_element_object(OriginalEntity &entity,
                                   const orphen::ported::resource::StatRecord &record,
                                   ElementDamageTable &damage)
  {
    // :10. Cleared before the kind test, so even a non-element placement loses
    // these two bits on the way through.
    entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 & 0xFE7Fu);

    // :12-14. The row's +0x27 read as a *signed* char. 0 is "not one of these",
    // and the two explicit tests for 0x0E and 0x0F are already covered by
    // `> 9`; they are kept because the original keeps them.
    const auto kind = static_cast<std::int8_t>(record.tail18[0x0F]);
    if (kind == 0 || kind > 9 || kind == 0x0E || kind == 0x0F)
    {
      return false;
    }

    // :17. FUN_00267E78(entity + 0x198, 0x40) clears +0x198..+0x1D7. At the one
    // call site the entity was built by FUN_00229C40 a few lines earlier and
    // nothing has written that range, so the clear has nothing to do; the port
    // models those bytes as half a dozen per-behaviour readings rather than one
    // buffer, and zeroing them all here would mean picking a reading.
    entity.elementOriginalType19e = entity.typeId00;
    // The model does **not** follow the retype. FUN_00229C40 bound it at spawn
    // into +0x15C/+0x160 off the placement's own type, and FUN_002F0608 rewrites
    // only +0x00 -- so a Darkness Element keeps the 0x37C prop it was placed as.
    // Without this the port re-resolves the model from the live type every
    // frame and draws 0x72, which is a party character: the element came out as
    // a second Orphen standing in the arena.
    entity.modelTypeId15c = entity.typeId00;
    const auto typeId = static_cast<std::int16_t>(kind + 0x6B);
    entity.typeId00 = typeId;

    // :22-45. The damage row, keyed by the *new* type id.
    const std::size_t row = static_cast<std::uint16_t>(typeId) < ElementDamageTable::kRowCount
                                ? static_cast<std::size_t>(static_cast<std::uint16_t>(typeId))
                                : 0u;
    std::uint32_t elementIndex = 0;
    if (record.tail18[0] == 0)
    {
      // Walk +0x19 upward for the first non-zero entry; the index that lands on
      // it is the element, and the byte is the power. The walk is allowed to run
      // one past the elemental block and read the kind byte itself at index 15,
      // which is the original's own bound.
      std::uint32_t at = 0;
      const std::uint8_t *found = nullptr;
      while (true)
      {
        elementIndex = at + 1;
        if (elementIndex > 0xF)
        {
          break;
        }
        const std::uint8_t *candidate = &record.tail18[1 + at];
        at = elementIndex;
        if (*candidate != 0)
        {
          found = candidate;
          break;
        }
      }
      if (found != nullptr)
      {
        damage.rows[row].power = *found;
        damage.rows[row].byte03 = record.byte08;
        damage.rows[row].elementMask = static_cast<std::uint16_t>(1u << (elementIndex & 0x1Fu));
      }
    }
    else
    {
      damage.rows[row].elementMask = 1;
      damage.rows[row].byte03 = record.byte08;
      damage.rows[row].power = record.tail18[0];
    }

    // :47-50.
    entity.flags06 = static_cast<std::uint16_t>(entity.flags06 & 0xFFEFu);
    FUN_00225bc8_set_animation(entity, 0);
    entity.spawnParam94 = 0;

    // :51-66. Kind 9 -- the elemental field effects, the 'kouka' rows -- is the
    // odd one: it stays hidden, takes the collision bit, and its hit points are
    // the element index rather than the row's own. Everything else is a solid
    // object the player can hit.
    if (kind == 9)
    {
      entity.state60 = 1;
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 | 1u);
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 | 0x10u);
      entity.staggerTimer12a = static_cast<std::uint16_t>(elementIndex);
    }
    else
    {
      entity.halfword08 = static_cast<std::uint16_t>(entity.halfword08 & 0xFFFEu);
      entity.descriptorFlags02 = static_cast<std::uint16_t>(entity.descriptorFlags02 | 8u);
      entity.halfword04 = static_cast<std::uint16_t>(entity.halfword04 & 0xFFEFu);
      entity.state60 = 0;
      entity.staggerTimer12a = record.byte06;
    }

    // :67-79. The body, straight off the same row.
    entity.radius54 = record.radius0c;
    entity.hitVolumeRadius11c = record.radius0c;
    entity.attackPower12c = record.byte07;
    entity.height58 = record.height10;
    entity.hitVolumeHeight120 = record.height10;
    entity.depthBias133 =
        static_cast<std::int8_t>(FUN_0030bd20_trunc(record.float14 / kDAT_00354b74_depthBiasUnit));
    // :81. +0x26 doubled. FUN_002F08F8 reads it back as the respawn delay.
    entity.fadeRamp62 = static_cast<std::uint16_t>(record.tail18[0x0E] << 1);
    return true;
  }

} // namespace orphen::ported::entity
