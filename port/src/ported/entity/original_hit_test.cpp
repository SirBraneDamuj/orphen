#include "ported/entity/original_hit_test.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::model::Matrix4;
    using orphen::ported::model::Psc3HitVolume;
    using orphen::ported::model::Psc3Model;
    using orphen::ported::psm2::Vec3;

    // FUN_00215e48 clears eight words; the tests read words 1..8 of the same
    // array. Nine covers both. See OriginalEntity::alreadyHitD0.
    constexpr std::size_t kAlreadyHitWords = 9;
    constexpr std::size_t kClearedWords = 8;

    // +0x06 bit 0x40: "the cached swept endpoints at +0xF0/+0x100 are this
    // attacker's, from last frame". FUN_00215e48 is the only thing that drops
    // it, which is why an already-hit set and a sweep history are cleared
    // together.
    constexpr std::uint16_t kSweptEndpointsValid06 = 0x0040;

    // The subdivision cap. FUN_002148a8 abandons the whole fill the moment the
    // box count would exceed 31, mid-segment, rather than clamping and
    // finishing -- so a very fast swing gets fewer boxes at its far end, not
    // coarser ones along its length.
    constexpr int kMaxBoxes = 31;

    // FUN_002148a8's box, as the six floats the entity test reads:
    // {minX, maxX, minY, maxY, minZ, maxZ}, in the entity axis order
    // (+0x20, +0x24, +0x28).
    struct SweptBox
    {
      std::array<float, 6> bounds{};
    };

    // DAT_0035217c, read out of eeMemory.bin: pi.
    constexpr float kDAT_0035217c_guardFacing = 3.14159202575683594f;

    // FUN_0030bd20, float -> int truncation toward zero.
    int FUN_0030bd20_trunc(float value) { return static_cast<int>(value); }

    // The interpolation factor FUN_002148a8 builds twice, in 12.12 fixed point.
    // `remaining` is entity +0xA4, the countdown left in the current timeline
    // entry, and `duration` is +0xA6, what it started at -- so the factor is 1
    // at the start of an entry and 0 at its end, and the volume runs from the
    // *entering* column's record back toward the one being left.
    int interpolationFactor(int remaining, int duration)
    {
      if (remaining < 1)
      {
        return 0;
      }
      if (remaining < duration)
      {
        return (remaining << 12) / duration;
      }
      return 0x1000;
    }

    // `(base + ((target - base) * t >> 12)) * 0.00390625`. Integer throughout
    // until the final scale, so the endpoints land on 1/256 steps.
    float interpolateComponent(std::int16_t base, std::int16_t target, int factor)
    {
      const int value = static_cast<int>(base) +
                        (((static_cast<int>(target) - static_cast<int>(base)) * factor) >> 12);
      return static_cast<float>(value) * 0.00390625f;
    }

    Vec3 vecFrom(const std::array<float, 3> &values)
    {
      return Vec3{values[0], values[1], values[2]};
    }
  } // namespace

  namespace
  {
    HitTestStats g_hitTestStats;
  }

  const HitTestStats &hitTestStats() { return g_hitTestStats; }
  void resetHitTestStats() { g_hitTestStats = HitTestStats{}; }

  void FUN_00215e48_clear_hit_set(OriginalEntity &attacker)
  {
    for (std::size_t index = 0; index < kClearedWords; ++index)
    {
      attacker.alreadyHitD0[index] = 0;
    }
    attacker.flags06 = static_cast<std::uint16_t>(attacker.flags06 & 0xFFBFu);
  }

  void FUN_00216140_apply_hit(OriginalEntity &attacker,
                              const orphen::ported::resource::HitParameters &parameters,
                              OriginalEntity &victim,
                              std::size_t attackerSlot,
                              const HitTestEnvironment &environment,
                              HitScratch &scratch)
  {
    (void)attackerSlot;

    // The victim is already carrying damage this frame. The original returns
    // before writing anything at all, including +0xC2 and +0xBC -- so the first
    // hit of a frame owns the reaction, not the last.
    if (victim.pendingDamageBe != 0)
    {
      return;
    }

    victim.hitFlagsC2 = parameters.flags;
    victim.hitReactionBc = parameters.reaction;

    // Type 0x38 is the script-driven NPC shell; its real identity for stat
    // purposes is at +0x1CE. The port does not model that field, and no type
    // 0x38 entity is attackable in s01_e024, so the remap is a no-op here and
    // is written out rather than silently dropped.
    const std::int16_t statType = victim.typeId00;

    // Which resistance table applies. Three sources, chosen off the victim's
    // +0x02, and the fallback is *all hundreds* rather than "no table".
    std::array<std::uint8_t, 0x10> resistance{};
    resistance.fill(100); // 'd'
    bool playerSideVictim = false;
    std::int16_t sparkSourceSide = 0;

    if (statType < 0x272)
    {
      if ((victim.descriptorFlags02 & 0x0003u) == 0)
      {
        if ((victim.descriptorFlags02 & 0x0008u) != 0)
        {
          // FUN_0025bae8(0, type, ...): the enemy table, indexed by
          // `type - 0x7C`. Negative for the type 0x62 flyer; see
          // CharacterStats::FUN_0025bae8_record.
          if (environment.DAT_00354d68_stats != nullptr)
          {
            const auto record = environment.DAT_00354d68_stats->FUN_0025bae8_record(0, statType);
            if (record.has_value())
            {
              resistance = record->tail18;
            }
          }
        }
        // else: the all-hundreds fill above, which is what the original writes
        // into its own stack buffer.
      }
      else
      {
        // FUN_0025bae8(1, type, ...): a party character. Type 0x5C remaps
        // through FUN_0023ef10 first; nothing in s01_e024 is type 0x5C.
        playerSideVictim = true;
        sparkSourceSide = 1;
        if (environment.DAT_00354d68_stats != nullptr)
        {
          const auto record = environment.DAT_00354d68_stats->FUN_0025bae8_record(1, statType);
          if (record.has_value())
          {
            resistance = record->tail18;
          }
        }
      }
    }
    else
    {
      // FUN_0025ba98: the streamed-type table, SCR.BIN resource 0xBD rather
      // than 0xBF. **Not reachable from the sword or the bolt**: both carry
      // +0x02 bit 0x1000, which selects candidate mask 0x2048, and a streamed
      // prop's +0x02 is 0x0180 -- so it is never a candidate. Left as the
      // all-hundreds fill rather than guessed at, and this comment is the
      // record of the one input that is missing if some other attacker ever
      // reaches it.
    }

    // The element: the lowest set bit of the record's flags, capped at 15. Bit
    // 0 is checked first and separately, so flags of zero also select 0.
    std::uint32_t element = 0;
    if ((parameters.flags & 1u) == 0)
    {
      for (element = 1; element < 0x10 && (parameters.flags & (1u << element)) == 0; ++element)
      {
      }
    }
    if (element > 0xF)
    {
      element = 0;
    }

    std::int16_t damage = 0;
    // Both sides need +0x96 bit 0x40 for the instant-kill path. The port models
    // neither entity's +0x96 -- it is zero on every entity in both EE dumps --
    // so the condition is written as the original's and always takes the first
    // branch. FUN_0023f620(5, 1) and the 0x1D reaction override live on the
    // other side of it.
    damage = static_cast<std::int16_t>(FUN_0030bd20_trunc(
        (static_cast<float>(static_cast<std::int8_t>(resistance[element])) / 100.0f) *
        (static_cast<float>(parameters.powerBonus + 100) / 100.0f) *
        static_cast<float>(static_cast<std::int16_t>(attacker.attackPower12c))));

    scratch.damage834 = damage;
    const std::int16_t defence = static_cast<std::int16_t>(victim.defence12e);
    scratch.defence838 = defence;
    int net = static_cast<int>(damage) - static_cast<int>(defence);
    if (net < 1)
    {
      // A hit always costs at least one point, however good the armour.
      net = 1;
    }
    victim.pendingDamageBe =
        static_cast<std::uint16_t>(static_cast<std::int16_t>(victim.pendingDamageBe) +
                                   static_cast<std::int16_t>(net));
    g_hitTestStats.damage += static_cast<std::uint32_t>(net);

    // The guard arc. +0x124 is a half-angle; zero means the victim cannot
    // block, which is every enemy in s01_e024.
    bool blocked = false;
    if (victim.guardArc124 != 0.0f && (parameters.flags & 0x4000u) == 0)
    {
      const float away = orphen::ported::model::FUN_00216690_wrap_angle(victim.hitDirectionC4 +
                                                                       kDAT_0035217c_guardFacing);
      const float delta =
          orphen::ported::model::FUN_002166e8_angle_delta(victim.facingRadians5c, away);
      if (delta <= victim.guardArc124 && delta >= -victim.guardArc124)
      {
        // Inside the arc: the damage is *negated*, not cancelled. The victim's
        // own behaviour reads the sign to pick a block reaction.
        victim.pendingDamageBe = static_cast<std::uint16_t>(
            -static_cast<std::int16_t>(victim.pendingDamageBe));
        blocked = true;
      }
    }

    if (!blocked)
    {
      // The HP bar, and then the sparks. Both are outside the block branch --
      // a blocked hit shows neither.
      if (static_cast<std::int16_t>(victim.pendingDamageBe) > 0 &&
          (victim.descriptorFlags02 & 0x004Bu) != 0 && (victim.battleFlags96 & 0x20u) == 0 &&
          environment.FUN_002d5630_damage_bar)
      {
        environment.FUN_002d5630_damage_bar(
            (victim.descriptorFlags02 & 0x0048u) != 0,
            static_cast<std::int16_t>(victim.staggerTimer12a),
            static_cast<std::int16_t>(victim.maxHitPoints128),
            static_cast<std::int16_t>(victim.pendingDamageBe));
      }
      if (environment.FUN_002206a8_spawn_hit_sparks)
      {
        environment.FUN_002206a8_spawn_hit_sparks(victim, sparkSourceSide);
      }

      // Reaction 0x1E suppresses the follow-up, and the follow-up itself --
      // FUN_00265e28(0x122), the blood puff a party member throws -- is gated
      // on DAT_003555D3, which is zero in both EE dumps. Not ported: it needs a
      // type 0x122 descriptor the port has never had to resolve, and it cannot
      // fire for an enemy victim.
      (void)playerSideVictim;
    }

    // +0x83F. This is the caller's return value -- see HitScratch. Incremented
    // for a blocked contact as well as a landed one, because the block branch
    // falls into the same tail.
    scratch.contacts83f = static_cast<std::int8_t>(scratch.contacts83f + 1);
  }

  namespace
  {
    // The candidate mask, read off the *attacker's* +0x02. The four cases are
    // exclusive in the order the original tests them.
    std::uint32_t candidateMaskFor(const OriginalEntity &attacker)
    {
      if ((attacker.descriptorFlags02 & 0x1001u) != 0)
      {
        return 0x2048; // a player-side effect: enemies only
      }
      if ((attacker.descriptorFlags02 & 0x2008u) != 0)
      {
        return 0x1001;
      }
      if ((attacker.descriptorFlags02 & 0x0040u) != 0)
      {
        return 0x3009;
      }
      return 0xFDFF;
    }

    // Everything before the shape test. FUN_002148a8 and FUN_00215ac8 walk the
    // pool identically and differ only in the shape they then measure.
    bool candidateAccepted(const EntityPool &pool, std::size_t slot, std::size_t attackerSlot,
                           const OriginalEntity &attacker, std::uint32_t candidateMask,
                           std::size_t word, std::uint32_t bit)
    {
      // `'\0' < (char)DAT_005a96b0[i]`: strictly positive, so a slot merely
      // reserved by FUN_00265dc0 (status -1) is not a candidate yet.
      if (static_cast<std::int8_t>(pool.status(slot)) < 1 || slot == attackerSlot)
      {
        return false;
      }
      if ((attacker.alreadyHitD0[word] & bit) != 0)
      {
        return false;
      }
      const OriginalEntity &victim = pool.slot(slot);
      if ((victim.descriptorFlags02 & candidateMask) == 0)
      {
        return false;
      }
      if ((victim.halfword04 & 0x0010u) != 0)
      {
        return false; // already dying
      }
      // Already committed to a hit this frame, in the original's order.
      return victim.hitSourceC0 == 0 && victim.pendingDamageBe == 0;
    }
  } // namespace

  std::int8_t FUN_00215ac8_box_hit_test(OriginalEntity &attacker,
                                        std::size_t attackerSlot,
                                        const std::array<float, 6> &box,
                                        const orphen::ported::resource::HitParameters &parameters,
                                        const HitTestEnvironment &environment)
  {
    HitScratch scratch;
    if (environment.entityPool == nullptr)
    {
      return 0;
    }

    const std::uint32_t candidateMask = candidateMaskFor(attacker);
    const float hitDirection = attacker.facingRadians5c + attacker.hitDirectionBiasC8;

    EntityPool &pool = *environment.entityPool;
    if (environment.DAT_003151c8_hitList != nullptr)
    {
      environment.DAT_003151c8_hitList->clear();
    }
    ++g_hitTestStats.tests;
    ++g_hitTestStats.boxes;
    g_hitTestStats.lastSweepBounds = box;
    g_hitTestStats.lastSweepValid = true;

    for (std::size_t slot = 0; slot < kEntitySlotCount; ++slot)
    {
      const std::size_t word = (slot >> 5) + 1;
      const std::uint32_t bit = 1u << (slot & 31u);
      if (word >= kAlreadyHitWords)
      {
        break;
      }
      if (!candidateAccepted(pool, slot, attackerSlot, attacker, candidateMask, word, bit))
      {
        continue;
      }
      OriginalEntity &victim = pool.slot(slot);

      const float radius = victim.hitVolumeRadius11c;
      const float centreX = victim.positionX20 + victim.hitVolumeOffset110[0];
      const float centreY = victim.positionZ24 + victim.hitVolumeOffset110[1];
      const float footZ = victim.positionY28 + victim.hitVolumeOffset110[2];
      const float headZ = footZ + victim.hitVolumeHeight120;

      if (!(centreX - radius < box[1] && box[0] < centreX + radius))
      {
        continue;
      }
      if (!(centreY - radius < box[3] && box[2] < centreY + radius))
      {
        continue;
      }
      if (!(footZ < box[5] && box[4] < headZ))
      {
        continue;
      }
      if (victim.freezeTimerBd != 0)
      {
        continue;
      }

      victim.lastAttackerSlotCc = static_cast<std::int16_t>(attackerSlot);
      attacker.alreadyHitD0[word] |= bit;
      victim.hitDirectionC4 = hitDirection;
      FUN_00216140_apply_hit(attacker, parameters, victim, attackerSlot, environment, scratch);
      if (environment.DAT_003151c8_hitList != nullptr)
      {
        environment.DAT_003151c8_hitList->push_back(static_cast<std::uint16_t>(slot));
      }
      victim.hitSourceKindBb =
          static_cast<std::uint8_t>((attacker.descriptorFlags02 & 0x1001u) != 0 ? 1 : 0);
      ++g_hitTestStats.contacts;
      // **No break.** FUN_00215ac8 keeps walking the pool, so one box can catch
      // several things at once. FUN_002148a8's break only leaves its *box* loop
      // for the victim it just hit; this test has no box loop to leave.
    }

    return scratch.negate83e != 0 ? static_cast<std::int8_t>(-scratch.contacts83f)
                                  : scratch.contacts83f;
  }

  std::int8_t FUN_002148a8_swept_hit_test(OriginalEntity &attacker,
                                          std::size_t attackerSlot,
                                          const orphen::ported::resource::HitParameters &parameters,
                                          const HitTestEnvironment &environment)
  {
    // The scratch block FUN_00216140 writes through. Its +0x83F *is* this
    // function's return value -- see HitScratch -- so every early exit below
    // returns zero simply because nothing has been charged yet.
    HitScratch scratch;
    const std::int8_t result = 0;

    if (environment.entityPool == nullptr)
    {
      return result;
    }

    // The model's two hit-volume sections. `+0x30` zero is the early out that
    // makes this a no-op for every model that is not a weapon effect.
    const Psc3Model *model =
        environment.modelForSlot ? environment.modelForSlot(attackerSlot) : nullptr;
    if (model == nullptr || model->hitVolumeColumnMap.empty())
    {
      return result;
    }
    if (model->hitVolumes.empty())
    {
      return result;
    }

    // +0xAE, the column the animation is leaving. Read as a signed short, and a
    // negative one is the "no pose yet" case.
    const std::int16_t leavingColumn = static_cast<std::int16_t>(attacker.previousPoseColumnAe);
    if (leavingColumn < 0)
    {
      return result;
    }
    const std::size_t leavingIndex = static_cast<std::size_t>(leavingColumn);
    const std::size_t enteringIndex = static_cast<std::size_t>(attacker.poseColumnAc);
    if (leavingIndex >= model->hitVolumeColumnMap.size() ||
        enteringIndex >= model->hitVolumeColumnMap.size())
    {
      return result;
    }
    const std::uint8_t towardRecord = model->hitVolumeColumnMap[leavingIndex];
    if (towardRecord == 0xFF)
    {
      return result;
    }
    const std::uint8_t fromRecord = model->hitVolumeColumnMap[enteringIndex];
    if (fromRecord == 0xFF)
    {
      return result;
    }
    if (towardRecord >= model->hitVolumes.size() || fromRecord >= model->hitVolumes.size())
    {
      return result;
    }

    // `psVar24` and `iVar26`. The interpolation runs *from* the column being
    // entered *toward* the one being left, because the factor is the countdown
    // still to run rather than the time already spent.
    const Psc3HitVolume &toward = model->hitVolumes[towardRecord];
    const Psc3HitVolume &from = model->hitVolumes[fromRecord];

    int duration = static_cast<std::int16_t>(attacker.keyframeTicksA6);
    if (duration < 1)
    {
      duration = 1;
    }
    const std::int16_t remaining = static_cast<std::int16_t>(attacker.stateResetA4);

    // Slots 2 and 3: where the volume was last frame. On the first frame of a
    // swing there is no history, so it is evaluated at the current countdown --
    // which makes the first sweep exactly one frame long, the same as every
    // later one.
    std::array<float, 3> previousA{};
    std::array<float, 3> previousB{};
    if ((attacker.flags06 & kSweptEndpointsValid06) == 0)
    {
      attacker.flags06 = static_cast<std::uint16_t>(attacker.flags06 | kSweptEndpointsValid06);
      const int factor = interpolationFactor(remaining, duration);
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        previousA[axis] = interpolateComponent(from.pointA[axis], toward.pointA[axis], factor);
        previousB[axis] = interpolateComponent(from.pointB[axis], toward.pointB[axis], factor);
      }
    }
    else
    {
      previousA = attacker.sweptPreviousAf0;
      previousB = attacker.sweptPreviousB100;
    }

    // Slots 0 and 1: where the volume will be on the *next* frame. The sweep is
    // forward-looking, which is why the test runs before the animation step
    // rather than after it.
    const int nextFactor =
        interpolationFactor(remaining - static_cast<int>(environment.frameTicks), duration);
    std::array<float, 3> currentA{};
    std::array<float, 3> currentB{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      currentA[axis] = interpolateComponent(from.pointA[axis], toward.pointA[axis], nextFactor);
      currentB[axis] = interpolateComponent(from.pointB[axis], toward.pointB[axis], nextFactor);
    }
    attacker.sweptPreviousAf0 = currentA;
    attacker.sweptPreviousB100 = currentB;

    // The box count and half-extent both come off the record being interpolated
    // *toward*, not off a blend of the two.
    int steps = static_cast<int>(toward.steps);
    // `((v << 16) >> 8) / 40 * (1/256)` -- an integer divide by 40 with a round
    // trip through 8.8 that changes the answer, so it is kept.
    const float halfExtent =
        static_cast<float>((static_cast<int>(toward.radius) * 256) / 0x28) * 0.00390625f *
        attacker.scale14c;

    if (!environment.FUN_0020cdc0_entity_matrix)
    {
      return result;
    }
    const auto matrix = environment.FUN_0020cdc0_entity_matrix(attackerSlot);
    if (!matrix.has_value())
    {
      return result;
    }

    const Vec3 worldCurrentA = orphen::ported::model::transformPoint(vecFrom(currentA), *matrix);
    const Vec3 worldCurrentB = orphen::ported::model::transformPoint(vecFrom(currentB), *matrix);
    const Vec3 worldPreviousA = orphen::ported::model::transformPoint(vecFrom(previousA), *matrix);
    const Vec3 worldPreviousB = orphen::ported::model::transformPoint(vecFrom(previousB), *matrix);

    if (steps < 1)
    {
      // The original still runs the entity scan with a zero box count, which can
      // never hit. Kept as an early return of the same value.
      return result;
    }

    const std::array<float, 3> curA{worldCurrentA.x, worldCurrentA.y, worldCurrentA.z};
    const std::array<float, 3> curB{worldCurrentB.x, worldCurrentB.y, worldCurrentB.z};
    const std::array<float, 3> prvA{worldPreviousA.x, worldPreviousA.y, worldPreviousA.z};
    const std::array<float, 3> prvB{worldPreviousB.x, worldPreviousB.y, worldPreviousB.z};

    // Lay `steps` boxes along each of the two segments. The first box is
    // centred half a step in, so the row is centred on the segment rather than
    // starting at its end.
    std::array<SweptBox, kMaxBoxes> current{};
    std::array<SweptBox, kMaxBoxes> previous{};
    const int baseSteps = std::min(steps, kMaxBoxes);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
      const float currentStep = (curB[axis] - curA[axis]) / static_cast<float>(steps);
      const float previousStep = (prvB[axis] - prvA[axis]) / static_cast<float>(steps);
      float currentAt = curA[axis] - currentStep * 0.5f;
      float previousAt = prvA[axis] - previousStep * 0.5f;
      for (int box = 0; box < baseSteps; ++box)
      {
        currentAt += currentStep;
        previousAt += previousStep;
        current[box].bounds[axis * 2 + 0] = currentAt - halfExtent;
        current[box].bounds[axis * 2 + 1] = currentAt + halfExtent;
        previous[box].bounds[axis * 2 + 0] = previousAt - halfExtent;
        previous[box].bounds[axis * 2 + 1] = previousAt + halfExtent;
      }
    }

    // Fill the gap between where each box was and where it is. A swing that
    // moved more than four half-extents in a frame gets extra boxes appended,
    // interpolated corner-wise from the current box back toward the previous
    // one -- so the sweep is a chain of axis-aligned boxes, not a capsule.
    int boxCount = baseSteps;
    for (int box = 0; box < baseSteps; ++box)
    {
      float squared = 0.0f;
      for (std::size_t axis = 0; axis < 3; ++axis)
      {
        const float delta = current[box].bounds[axis * 2] - previous[box].bounds[axis * 2];
        squared += delta * delta;
      }
      const float travelled = std::sqrt(squared);
      const float span = halfExtent * 4.0f;
      if (span > travelled)
      {
        continue;
      }
      const int divisions = FUN_0030bd20_trunc(travelled / span) + 1;
      bool exhausted = false;
      for (int step = 1; step < divisions; ++step)
      {
        const float ratio = static_cast<float>(step) / static_cast<float>(divisions);
        for (std::size_t component = 0; component < 6; ++component)
        {
          const float here = current[box].bounds[component];
          const float there = previous[box].bounds[component];
          current[boxCount].bounds[component] = here + (there - here) * ratio;
        }
        ++boxCount;
        if (boxCount > kMaxBoxes - 1)
        {
          // The original's `if (0x1e < iStack_178) goto LAB_00214f00` -- it
          // abandons the whole fill here rather than clamping, so the boxes
          // already laid down are what the scan gets.
          exhausted = true;
          break;
        }
      }
      if (exhausted)
      {
        break;
      }
    }

    // Which entities can be hit at all.
    const std::uint32_t candidateMask = candidateMaskFor(attacker);

    // +0xC4, the direction the blow came from, stamped on every victim.
    const float hitDirection = attacker.facingRadians5c + attacker.hitDirectionBiasC8;

    ++g_hitTestStats.tests;
    g_hitTestStats.boxes += static_cast<std::uint32_t>(boxCount);
    for (std::size_t component = 0; component < 6; ++component)
    {
      float extreme = current[0].bounds[component];
      for (int box = 1; box < boxCount; ++box)
      {
        extreme = (component % 2 == 0) ? std::min(extreme, current[box].bounds[component])
                                       : std::max(extreme, current[box].bounds[component]);
      }
      g_hitTestStats.lastSweepBounds[component] = extreme;
    }
    g_hitTestStats.lastSweepValid = true;

    EntityPool &pool = *environment.entityPool;
    if (environment.DAT_003151c8_hitList != nullptr)
    {
      environment.DAT_003151c8_hitList->clear();
    }

    for (std::size_t slot = 0; slot < kEntitySlotCount; ++slot)
    {
      // The word index is `(slot >> 5) + 1`, because the pointer is stepped
      // before the first slot rather than after it.
      const std::size_t word = (slot >> 5) + 1;
      const std::uint32_t bit = 1u << (slot & 31u);
      if (word >= kAlreadyHitWords)
      {
        break;
      }
      if (!candidateAccepted(pool, slot, attackerSlot, attacker, candidateMask, word, bit))
      {
        continue;
      }
      OriginalEntity &victim = pool.slot(slot);

      const float radius = victim.hitVolumeRadius11c;
      const float centreX = victim.positionX20 + victim.hitVolumeOffset110[0];
      const float centreY = victim.positionZ24 + victim.hitVolumeOffset110[1];
      const float footZ = victim.positionY28 + victim.hitVolumeOffset110[2];
      const float headZ = footZ + victim.hitVolumeHeight120;

      for (int box = 0; box < boxCount; ++box)
      {
        const auto &bounds = current[box].bounds;
        if (!(centreX - radius < bounds[1] && bounds[0] < centreX + radius))
        {
          continue;
        }
        if (!(centreY - radius < bounds[3] && bounds[2] < centreY + radius))
        {
          continue;
        }
        // The vertical span is *not* symmetric: it runs from the feet to the
        // body height, which is why a low sweep passes under a tall actor.
        if (!(footZ < bounds[5] && bounds[4] < headZ))
        {
          continue;
        }
        // +0xBD, the freeze/hit-stop countdown. A victim inside one is
        // untouchable, which is what stops a single swing landing twice while
        // the world is paused on the first hit.
        if (victim.freezeTimerBd != 0)
        {
          continue;
        }

        victim.lastAttackerSlotCc = static_cast<std::int16_t>(attackerSlot);
        attacker.alreadyHitD0[word] |= bit;
        victim.hitDirectionC4 = hitDirection;

        FUN_00216140_apply_hit(attacker, parameters, victim, attackerSlot, environment, scratch);

        if (environment.DAT_003151c8_hitList != nullptr)
        {
          environment.DAT_003151c8_hitList->push_back(static_cast<std::uint16_t>(slot));
        }
        victim.hitSourceKindBb =
            static_cast<std::uint8_t>((attacker.descriptorFlags02 & 0x1001u) != 0 ? 1 : 0);
        ++g_hitTestStats.contacts;
        break;
      }
    }

    (void)result;
    return scratch.negate83e != 0 ? static_cast<std::int8_t>(-scratch.contacts83f)
                                  : scratch.contacts83f;
  }

} // namespace orphen::ported::entity
