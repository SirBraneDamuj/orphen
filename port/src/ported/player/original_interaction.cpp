#include "ported/player/original_interaction.h"

#include "ported/script/object_registers.h"

#include <cmath>

namespace orphen::ported::player
{
  namespace
  {
    using orphen::ported::entity::EntityPool;
    using orphen::ported::entity::kEntitySlotCount;
    using orphen::ported::entity::OriginalEntity;
    using orphen::ported::entity::SlotStatus;

    // fGpffff88f0: how far ahead of the actor the probe cylinder sits.
    constexpr float kProbeReach = 0.30f;
    // The slack FUN_00252a18 adds to the candidate's collision radius.
    constexpr float kProbeRadiusSlack = 0.25f;
    // fGpffff88e8 / fGpffff88ec: the facing cone, +/- 35 degrees. Both bounds
    // are exclusive, so a candidate exactly on the edge is refused.
    constexpr float kConeMin = -0.6108651161193848f;
    constexpr float kConeMax = 0.6108651161193848f;

    // Descriptor flags at entity +0x02.
    constexpr std::uint16_t kScriptedInteraction02 = 0x4000;
    constexpr std::uint16_t kNativeInteraction02 = 0x0100;
    // Entity +0x04's matching bit, which vetoes the scripted branch.
    constexpr std::uint16_t kSuppressScripted04 = 0x4000;

    constexpr std::int16_t kTypeChest = 0x3A;
    // FUN_00252828 refuses this type outright before the scripted branch.
    constexpr std::int16_t kTypeExcluded = 0x37;
    // The indirection FUN_00252828 opens with: type 0x38 reads its real type
    // out of +0x1CE. The port does not model that field, so 0x38 falls through
    // as itself, which reaches none of the branches.
    constexpr std::int16_t kTypeIndirect = 0x38;

    // FUN_0023a4b8: the angle from one entity to another.
    float FUN_0023a4b8_angle_between(const OriginalEntity &from, const OriginalEntity &to)
    {
      return std::atan2(to.positionZ24 - from.positionZ24, to.positionX20 - from.positionX20);
    }

    // FUN_002166e8: the signed difference between two angles, wrapped.
    float FUN_002166e8_angle_difference(float from, float to)
    {
      return orphen::ported::script::FUN_00216690_wrapAngle(to - from);
    }

    bool inStreamedBand(std::int32_t type)
    {
      return static_cast<std::uint32_t>(type - 0x272) < 0x100 ||
             static_cast<std::uint32_t>(type - 0x373) < 0x100 ||
             static_cast<std::uint32_t>(type - 0x474) < 0x100;
    }
  } // namespace

  std::size_t FUN_00252a18_find_nearest_candidate(EntityPool &pool,
                                                  std::size_t actorSlot,
                                                  std::size_t firstSlot)
  {
    const OriginalEntity &actor = pool.slot(actorSlot);

    // The probe point: one reach ahead of the actor's facing, at its feet, with
    // the actor's own height giving the cylinder's top.
    const float probeX = actor.positionX20 + std::cos(actor.facingRadians5c) * kProbeReach;
    const float probeZ = actor.positionZ24 + std::sin(actor.facingRadians5c) * kProbeReach;
    const float probeFeet = actor.positionY28;
    const float probeHead = actor.positionY28 + actor.height58;

    std::size_t best = kEntitySlotCount;
    float bestDistanceSquared = 1.0f; // the original seeds this at 1.0, not FLT_MAX

    for (std::size_t slot = firstSlot; slot < kEntitySlotCount; ++slot)
    {
      if (pool.status(slot) != SlotStatus::ScriptSpawned)
      {
        continue;
      }
      if (slot == actorSlot)
      {
        continue;
      }

      OriginalEntity &candidate = pool.slot(slot);

      // +0x192 is the candidate gate, a signed halfword the descriptor init
      // (FUN_00229c40) seeds to 0xFFFF. Something else clears it to take an
      // entity out of the running; nothing in this port does yet.
      if (candidate.interactGate192 >= 0)
      {
        continue;
      }

      // When the actor is already holding a target (+0x0C bit 0x100), the thing
      // it is holding is not a candidate.
      if ((actor.collisionFlags0c & 0x100u) != 0 && actor.interactTarget68 == static_cast<std::int32_t>(slot))
      {
        continue;
      }

      const float dx = probeX - candidate.positionX20;
      const float dz = probeZ - candidate.positionZ24;
      const float distanceSquared = dx * dx + dz * dz;

      const float reach = candidate.radius54 + kProbeRadiusSlack;
      if (distanceSquared > reach * reach)
      {
        continue;
      }
      // The vertical band: the candidate's feet below our head, and our feet
      // below the top of the candidate.
      if (candidate.positionY28 > probeHead)
      {
        continue;
      }
      if (probeFeet > candidate.positionY28 + candidate.height58)
      {
        continue;
      }

      if (distanceSquared < bestDistanceSquared)
      {
        // The original then walks the pool again to see whether some *other*
        // actor has already claimed this candidate (its +0x120 pointing here
        // with +0x1DC clear) and skips it if so. That is a multi-actor
        // arbitration the port has no second interacting actor for, so it is
        // deliberately absent rather than guessed at.
        bestDistanceSquared = distanceSquared;
        best = slot;
      }
    }

    return best;
  }

  InteractionResult FUN_00252cc0_probe_for_interaction(EntityPool &pool,
                                                       std::size_t actorSlot,
                                                       const std::function<bool(std::uint32_t)> &eventFlag)
  {
    InteractionResult result;

    // FUN_00252cc0 restarts the scan past each rejected candidate rather than
    // stopping at the nearest one, so a closer object that cannot be interacted
    // with does not mask a usable one behind it.
    for (std::size_t from = 0; from < kEntitySlotCount;)
    {
      const std::size_t slot = FUN_00252a18_find_nearest_candidate(pool, actorSlot, from);
      if (slot >= kEntitySlotCount)
      {
        return result;
      }

      OriginalEntity &actor = pool.slot(actorSlot);
      OriginalEntity &candidate = pool.slot(slot);

      // FUN_00252828 proper.
      std::int32_t type = candidate.typeId00;
      if (type == kTypeIndirect)
      {
        // Would read +0x1CE here; unmodelled, so it stays 0x38 and matches no
        // branch below.
        type = kTypeIndirect;
      }

      const bool scripted = (candidate.descriptorFlags02 & kScriptedInteraction02) != 0 &&
                            (candidate.halfword04 & kSuppressScripted04) == 0 &&
                            candidate.typeId00 != kTypeExcluded;
      if (scripted)
      {
        result.kind = InteractionKind::ScriptedEntity;
        result.targetSlot = slot;
        result.targetType = candidate.typeId00;
        return result;
      }

      if ((candidate.descriptorFlags02 & kNativeInteraction02) != 0)
      {
        if (type == kTypeChest)
        {
          // An already-open chest is not interactable. FUN_002d1ea8 reads the
          // same flag to pick its pose.
          if (eventFlag && eventFlag(candidate.eventFlagId198))
          {
            return result;
          }

          const float toCandidate = FUN_0023a4b8_angle_between(actor, candidate);
          const float difference = FUN_002166e8_angle_difference(actor.facingRadians5c, toCandidate);
          if (difference <= kConeMin || difference >= kConeMax)
          {
            // Outside the cone. The original returns 0 on the near side and
            // falls to the shared "nothing happened" exit on the far side; both
            // end the probe.
            return result;
          }

          actor.interactTarget198 = static_cast<std::int32_t>(slot);
          actor.interactParam1b8 = 0x4B00;
          result.kind = InteractionKind::Chest;
          result.targetSlot = slot;
          result.targetType = candidate.typeId00;
          result.chestFlagId = candidate.eventFlagId198;
          return result;
        }

        if (inStreamedBand(type))
        {
          // The map-streamed branch gates on a byte at +0x132 and a height
          // comparison, then turns the player to face the object and enters
          // state 8 or 9 depending on +0x02 bit 0x10. The port resolves none of
          // the streamed descriptors, so this is recorded and not acted on.
          result.kind = InteractionKind::StreamedProp;
          result.targetSlot = slot;
          result.targetType = candidate.typeId00;
          return result;
        }
      }

      from = slot + 1;
    }

    return result;
  }

} // namespace orphen::ported::player
