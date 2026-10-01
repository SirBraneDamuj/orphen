#include "ported/entity/original_breakable_prop.h"

#include "ported/entity/entity_pool.h"

#include <cmath>
#include <optional>

namespace orphen::ported::entity
{
  namespace
  {
    // A break record, 0x24 bytes. See analyzed/ops/0xD9_register_entity_callback.c.
    constexpr std::uint32_t kBreakRecordSize = 0x24;
    constexpr std::uint16_t kBreakTableEnd = 0xFFFF;

    // The two cues, both literals in the instruction stream.
    constexpr std::uint16_t kFUN_002cfe08_breakCue = 0x2C5;
    constexpr std::uint16_t kFUN_002cffa8_remnantCue = 0x2C6;

    // DAT_003545D8 .. DAT_003545E4, four separate words that all hold 100000:
    // the pieces' offsets, then the light's x, y and z.
    constexpr float kDAT_003545d8_pieceUnit = 100000.0f;
    constexpr float kDAT_003545dc_lightUnitX = 100000.0f;
    constexpr float kDAT_003545e0_lightUnitY = 100000.0f;
    constexpr float kDAT_003545e4_lightUnitZ = 100000.0f;
    // fGpffffa664, 0x003545D4: what state 2 bleeds off its speed each frame.
    constexpr float kFGpffffa664_pushDecay = 0.00499999989f;
    // `lui 0x42C8`, and the light's floor, `lui 0x4000`.
    constexpr float kPercent = 100.0f;
    constexpr float kMinimumLightRadius = 2.0f;
    // FUN_002D0058's scale roll: `divu` by 25, plus 50.
    constexpr std::uint32_t kPieceScaleRoll = 0x19;
    constexpr std::uint32_t kPieceScaleBase = 0x32;

    // FUN_00225C90's +0x06 bits: 0x01 the strip finished, 0x04 the running
    // entry expired this frame.
    constexpr std::uint16_t kAnimationFinished06 = 0x0001;
    constexpr std::uint16_t kEntryExpired06 = 0x0004;

    std::optional<std::uint16_t> readU16(std::span<const std::uint8_t> blob, std::uint32_t offset)
    {
      if (static_cast<std::size_t>(offset) + 2 > blob.size())
      {
        return std::nullopt;
      }
      return static_cast<std::uint16_t>(blob[offset] | (blob[offset + 1] << 8));
    }

    std::optional<std::uint32_t> readU32(std::span<const std::uint8_t> blob, std::uint32_t offset)
    {
      if (static_cast<std::size_t>(offset) + 4 > blob.size())
      {
        return std::nullopt;
      }
      return static_cast<std::uint32_t>(blob[offset]) |
             (static_cast<std::uint32_t>(blob[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(blob[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(blob[offset + 3]) << 24);
    }

    std::uint32_t roll(const ActorEnvironment &environment)
    {
      return environment.random ? environment.random() : 0u;
    }

    // The `bltz` / `srl` / `or` / `add.s` ladder at 0x002D026C: a u32 to float
    // conversion. The three light offsets are read as unsigned.
    float unsignedToFloat(std::uint32_t value) { return static_cast<float>(value); }

    // State 2, FUN_002CFEE8: slide along +0x1B4 at half of +0x1B0, bleeding
    // 0.005 off it a frame, and stop at zero -- or at once if the last physics
    // pass hit anything (+0x0C & 0x263).
    void FUN_002cfee8_push(OriginalEntity &entity, std::uint32_t frameTicks)
    {
      if ((entity.collisionFlags0c & 0x263u) != 0)
      {
        entity.state60 = 0;
        return;
      }
      const float heading = entity.pushHeading1b4;
      const float step = entity.breakSpeedOrScale1b0 * 0.5f *
                         static_cast<float>(static_cast<std::int32_t>(frameTicks)) * 0.03125f;
      entity.desiredDeltaX30 += step * std::cos(heading);
      const float speed = entity.breakSpeedOrScale1b0 - kFGpffffa664_pushDecay;
      entity.breakSpeedOrScale1b0 = speed;
      entity.desiredDeltaZ34 += step * std::sin(heading);
      if (speed <= 0.0f)
      {
        entity.state60 = 0;
      }
    }

    // State 3, FUN_002CFFA8.
    void FUN_002cffa8_await_remnant(OriginalEntity &entity,
                                    std::size_t slot,
                                    const ActorEnvironment &environment)
    {
      const auto remaining = static_cast<std::uint16_t>(
          static_cast<std::uint32_t>(entity.fadeRamp62) - (environment.frameTicks & 0xFFFFu));
      entity.fadeRamp62 = remaining;
      if (static_cast<std::int16_t>(remaining) > 0 || entity.breakSpawnType1ac == 0 ||
          environment.entityPool == nullptr || environment.descriptors == nullptr)
      {
        return;
      }
      const std::size_t remnantSlot = environment.entityPool->FUN_00265e28_allocate_and_initialize(
          static_cast<std::int16_t>(entity.breakSpawnType1ac), *environment.descriptors);
      if (remnantSlot >= kEntitySlotCount)
      {
        return;
      }
      OriginalEntity &remnant = environment.entityPool->slot(remnantSlot);
      // FUN_00267DA0(+0x20, +0x20, 12): the three position words.
      remnant.positionX20 = entity.positionX20;
      remnant.positionZ24 = entity.positionZ24;
      remnant.positionY28 = entity.positionY28;
      remnant.groundHeight4c = entity.groundHeight4c;
      FUN_00229ef0_set_scale(remnant, entity.breakSpeedOrScale1b0, environment.descriptors);
      remnant.breakParent198 = static_cast<std::int16_t>(slot);
      entity.state60 = 1;
      if (environment.FUN_00267d38_playSound)
      {
        environment.FUN_00267d38_playSound(kFUN_002cffa8_remnantCue, entity);
      }
    }
  } // namespace

  bool FUN_002cfe08_streamed_prop(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
  {
    // The gate runs for its side effects only; the original ignores the result
    // and runs the state whether or not the prop is frozen.
    (void)FUN_0023a068_freeze_gate(entity, environment.frameTicks);

    if (static_cast<std::int16_t>(entity.pendingDamageBe) != 0)
    {
      if (entity.breakTable1a8 == 0)
      {
        entity.hitFlagsC2 = 0;
      }
      else
      {
        const auto blob = environment.iGpffffb0e8_sceneScript;
        std::uint32_t record = entity.breakTable1a8;
        for (auto kind = readU16(blob, record); kind.has_value() && *kind != kBreakTableEnd;
             record += kBreakRecordSize, kind = readU16(blob, record))
        {
          // `sllv` takes the low five bits; the `andi 0xffff` drops bits 16..31.
          const std::uint32_t bit = (1u << (*kind & 0x1Fu)) & 0xFFFFu;
          if ((entity.hitFlagsC2 & bit) == 0)
          {
            continue;
          }
          const auto left = static_cast<std::uint16_t>(
              static_cast<std::uint32_t>(entity.staggerTimer12a) -
              static_cast<std::uint32_t>(entity.pendingDamageBe));
          entity.staggerTimer12a = left;
          if (static_cast<std::int16_t>(left) > 0)
          {
            continue;
          }
          entity.staggerTimer12a = 0;
          FUN_002d0058_break_prop(entity, slot, record, environment);
          if (environment.FUN_00267d38_playSound)
          {
            environment.FUN_00267d38_playSound(kFUN_002cfe08_breakCue, entity);
          }
          break;
        }
        entity.hitFlagsC2 = 0;
      }
      entity.pendingDamageBe = 0;
    }

    // PTR_LAB_003266D0[+0x60].
    switch (entity.state60)
    {
    case 0: // 0x002CFED8, jr ra
    case 1: // 0x002CFEE0, jr ra
      return true;
    case 2:
      FUN_002cfee8_push(entity, environment.frameTicks);
      return true;
    case 3:
      FUN_002cffa8_await_remnant(entity, slot, environment);
      return true;
    default:
      return false;
    }
  }

  void FUN_002d0058_break_prop(OriginalEntity &prop,
                               std::size_t slot,
                               std::uint32_t recordOffset,
                               const ActorEnvironment &environment)
  {
    const auto blob = environment.iGpffffb0e8_sceneScript;
    const auto field16 = [&](std::uint32_t at) -> std::int16_t {
      return static_cast<std::int16_t>(readU16(blob, recordOffset + at).value_or(0));
    };
    const auto field32 = [&](std::uint32_t at) -> std::uint32_t {
      return readU32(blob, recordOffset + at).value_or(0);
    };

    // :20-58. The pieces. The position cursor only moves on a successful
    // allocation, so a full pool skips a piece without skipping its offsets.
    const std::int16_t pieceType = field16(0x02);
    if (pieceType != 0 && environment.entityPool != nullptr && environment.descriptors != nullptr)
    {
      std::uint32_t cursor = recordOffset + 0x20 + field32(0x20);
      const std::int16_t count = field16(0x04);
      const std::int16_t scaleBias = field16(0x08);
      for (std::int16_t left = static_cast<std::int16_t>(count - 1); left != -1;
           left = static_cast<std::int16_t>(left - 1))
      {
        const std::size_t pieceSlot =
            environment.entityPool->FUN_00265e28_allocate_and_initialize(pieceType,
                                                                        *environment.descriptors);
        if (pieceSlot >= kEntitySlotCount)
        {
          continue; // FUN_0026BFC0, a debug print
        }
        const auto offsetX = static_cast<std::int32_t>(readU32(blob, cursor).value_or(0));
        const auto offsetY = static_cast<std::int32_t>(readU32(blob, cursor + 4).value_or(0));
        const auto offsetZ = static_cast<std::int32_t>(readU32(blob, cursor + 8).value_or(0));
        cursor += 12;

        OriginalEntity &piece = environment.entityPool->slot(pieceSlot);
        piece.positionX20 = static_cast<float>(offsetX) / kDAT_003545d8_pieceUnit + prop.positionX20;
        piece.positionZ24 = static_cast<float>(offsetY) / kDAT_003545d8_pieceUnit + prop.positionZ24;
        piece.positionY28 = static_cast<float>(offsetZ) / kDAT_003545d8_pieceUnit + prop.positionY28;
        const float ground =
            environment.FUN_00227798_probe
                ? environment.FUN_00227798_probe(piece.positionX20, piece.positionZ24,
                                                 piece.positionY28)
                      .height
                : piece.positionY28;
        piece.breakParent198 = static_cast<std::int16_t>(slot);
        piece.groundHeight4c = ground;
        piece.halfword04 = static_cast<std::uint16_t>(piece.halfword04 | 0x19u);
        const std::uint32_t scaleRoll = roll(environment) % kPieceScaleRoll + kPieceScaleBase;
        FUN_00229ef0_set_scale(piece,
                               static_cast<float>(static_cast<std::int32_t>(scaleRoll)) / kPercent +
                                   static_cast<float>(scaleBias) / kPercent,
                               environment.descriptors);
        if (environment.FUN_00225c90_advance_slot)
        {
          environment.FUN_00225c90_advance_slot(pieceSlot);
        }
        piece.timelineCursorA8 = static_cast<std::uint16_t>((roll(environment) & 3u) << 1);
      }
    }

    // :59-78. The light.
    const std::uint32_t colour = field32(0x0C);
    if (colour != 0)
    {
      std::int8_t lightSlot = -1;
      if (environment.DAT_00343888_lights != nullptr)
      {
        lightSlot = static_cast<std::int8_t>(
            environment.DAT_00343888_lights->FUN_00266008_allocateFromThree());
      }
      prop.lightSlot195 = lightSlot;
      // `blez`: slot 0 would be skipped too, but FUN_00266008 starts at 3.
      if (lightSlot > 0 && environment.DAT_00343888_lights != nullptr)
      {
        auto &light = environment.DAT_00343888_lights->slot(static_cast<std::uint32_t>(lightSlot));
        light.red = static_cast<std::uint8_t>(colour);
        light.green = static_cast<std::uint8_t>(colour >> 8);
        light.blue = static_cast<std::uint8_t>(colour >> 16);
        light.alpha = static_cast<std::uint8_t>(colour >> 24);
        const float radius = prop.radius54 + prop.radius54;
        light.radius = radius < kMinimumLightRadius ? kMinimumLightRadius : radius;
        light.x = unsignedToFloat(field32(0x14)) / kDAT_003545dc_lightUnitX + prop.positionX20;
        light.y = unsignedToFloat(field32(0x18)) / kDAT_003545e0_lightUnitY + prop.positionZ24;
        light.z = unsignedToFloat(field32(0x1C)) / kDAT_003545e4_lightUnitZ + prop.positionY28;
        environment.DAT_00343888_lights->noteRadius(static_cast<std::uint32_t>(lightSlot),
                                                    light.radius);
      }
    }

    // :79-85.
    prop.state60 = 3;
    prop.fadeRamp62 = static_cast<std::uint16_t>(field16(0x06) << 5);
    prop.halfword04 = static_cast<std::uint16_t>(prop.halfword04 | 0x10u);
    prop.breakSpawnType1ac = static_cast<std::uint16_t>(field16(0x10));
    prop.breakSpeedOrScale1b0 = static_cast<float>(field16(0x12)) / kPercent;
  }

  void FUN_002d04e0_break_remnant(OriginalEntity &entity,
                                  std::size_t slot,
                                  const ActorEnvironment &environment)
  {
    // FUN_00265EC0 leaves a freed slot's fields where they were and the
    // original reads them straight after freeing itself; the port's release
    // blanks the slot, so take what is read first.
    const std::uint16_t flags06 = entity.flags06;
    const std::uint16_t cursor = entity.timelineCursorA8;
    const std::int16_t parent = entity.breakParent198;

    // FUN_00239E80.
    if ((flags06 & kAnimationFinished06) != 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
    if (cursor == 4 && (flags06 & kEntryExpired06) != 0 && parent >= 0)
    {
      FUN_00265ec0_destroy_entity(static_cast<std::size_t>(parent), environment);
    }
  }

  void LAB_002d05c0_break_piece(OriginalEntity &entity,
                                std::size_t slot,
                                const ActorEnvironment &environment)
  {
    if (entity.breakParent198 < 0 || environment.entityPool == nullptr)
    {
      return;
    }
    const auto parent = static_cast<std::size_t>(entity.breakParent198);
    if (parent < kEntitySlotCount && environment.entityPool->slot(parent).typeId00 == 0)
    {
      FUN_00265ec0_destroy_entity(slot, environment);
    }
  }

} // namespace orphen::ported::entity
