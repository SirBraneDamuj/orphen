#include "ported/script/object_registers.h"

#include "ported/entity/original_entity.h"

namespace orphen::ported::script
{
  namespace
  {
    // DAT_00352188 / DAT_0035218c / DAT_00352190.
    constexpr float kPi00352188 = 3.141592025756836f;
    constexpr float kTwoPi0035218c = 6.283184051513672f;
    constexpr float kNegPi00352190 = -3.141592025756836f;

    // The operand arrives as a dword; every numeric case reads it signed.
    std::int32_t asSigned(std::uint32_t value)
    {
      return static_cast<std::int32_t>(value);
    }

    float toField(std::uint32_t value)
    {
      return static_cast<float>(asSigned(value)) / kObjectRegisterScale;
    }

    // FUN_0030bd20(field * scale): a plain float-to-int convert, so it truncates
    // toward zero rather than flooring.
    std::uint32_t fromField(float field)
    {
      return static_cast<std::uint32_t>(static_cast<std::int32_t>(field * kObjectRegisterScale));
    }
  } // namespace

  float FUN_00216690_wrapAngle(float radians)
  {
    for (int iteration = 0; iteration < 0x10; ++iteration)
    {
      if (radians > kPi00352188)
      {
        radians -= kTwoPi0035218c;
      }
      else if (radians >= kNegPi00352190)
      {
        return radians;
      }
      else
      {
        radians += kTwoPi0035218c;
      }
    }
    return radians;
  }

  bool FUN_0025c8f8_write_object_register(entity::OriginalEntity &entity,
                                          std::uint32_t index,
                                          std::uint32_t value)
  {
    const auto halfword = static_cast<std::uint16_t>(value);
    const auto byte = static_cast<std::uint8_t>(value);

    switch (index)
    {
    case 0x00: entity.typeId00 = static_cast<std::int16_t>(halfword); return true;
    case 0x01: entity.descriptorFlags02 = halfword; return true;
    case 0x02: entity.collisionFlags0c = value; return true;
    case 0x03: entity.halfword04 = halfword; return true;
    case 0x04: entity.halfword08 = halfword; return true;
    case 0x05: entity.flags06 = halfword; return true;

    // +0xA8, and the only case that scales: the operand is written doubled.
    // FUN_0025c548 halves it back with a signed divide, so a round trip through
    // an odd value loses the low bit.
    case 0x06: entity.timelineCursorA8 = static_cast<std::uint16_t>(asSigned(value) << 1); return true;
    case 0x07: entity.flagsAa = halfword; return true;
    case 0x08: entity.animationA0 = halfword; return true;

    case 0x09: entity.flagWord6c = value; return true;
    case 0x0A: entity.flagWord70 = value; return true;
    case 0x0B: entity.requiredTerrainMask78 = value; return true;
    case 0x0C: entity.rejectTerrainMask74 = value; return true;

    // +0x5C, the facing angle. This is the one the scene init uses to point the
    // room's NPCs, and the only write that normalises rather than storing raw.
    case 0x0D: entity.facingRadians5c = FUN_00216690_wrapAngle(toField(value)); return true;
    case 0x0E: entity.verticalAcceleration48 = toField(value); return true;
    case 0x0F: entity.fadeRamp62 = halfword; return true;
    case 0x10: entity.spawnParam94 = byte; return true;
    case 0x11: entity.byte95 = byte; return true;

    case 0x13: entity.groundHeight4c = toField(value); return true;
    case 0x19: entity.state60 = halfword; return true;

    case 0x1A: entity.desiredDeltaX30 = toField(value); return true;
    case 0x1B: entity.desiredDeltaZ34 = toField(value); return true;
    case 0x1C: entity.velocityX3c = toField(value); return true;
    case 0x1D: entity.velocityZ40 = toField(value); return true;
    case 0x1E: entity.verticalVelocity44 = toField(value); return true;

    // +0x154 / +0x158, the two extra model rotations the renderer already
    // applies in FUN_0020cdc0_entity_root. Stored raw: 0x0D above is the only
    // angle write that normalises. s01_e012 tilts a prop with these during the
    // opening, and they were the last two unmodelled register writes in the
    // scene.
    case 0x1F: entity.rotationX154 = toField(value); return true;
    case 0x20: entity.rotationY158 = toField(value); return true;

    case 0x22: entity.fadeLevel134 = byte; return true;
    case 0x23: entity.fadeColor138 = value; return true;
    case 0x26: entity.freezeTimerBd = static_cast<std::int8_t>(byte); return true;

    case 0x28: entity.radius54 = toField(value); return true;
    case 0x29: entity.height58 = toField(value); return true;

    // 0x38..0x3F are eight consecutive dwords from +0x198. Only the first is
    // modelled; for type 0x3A that is the chest's event flag id.
    case 0x38: entity.eventFlagId198 = value; return true;

    default: return false;
    }
  }

  bool FUN_0025c548_read_object_register(const entity::OriginalEntity &entity,
                                         std::uint32_t index,
                                         std::uint32_t &value)
  {
    switch (index)
    {
    case 0x00: value = static_cast<std::uint16_t>(entity.typeId00); return true;
    case 0x01: value = entity.descriptorFlags02; return true;
    case 0x02: value = entity.collisionFlags0c; return true;
    case 0x03: value = entity.halfword04; return true;
    case 0x04: value = entity.halfword08; return true;
    case 0x05: value = entity.flags06; return true;

    // Signed halve, rounding toward zero, undoing the write's doubling.
    case 0x06:
      value = static_cast<std::uint32_t>(static_cast<std::int16_t>(entity.timelineCursorA8) / 2);
      return true;
    case 0x07: value = entity.flagsAa; return true;
    case 0x08: value = entity.animationA0; return true;

    case 0x09: value = entity.flagWord6c; return true;
    case 0x0A: value = entity.flagWord70; return true;
    case 0x0B: value = entity.requiredTerrainMask78; return true;
    case 0x0C: value = entity.rejectTerrainMask74; return true;

    case 0x0D: value = fromField(entity.facingRadians5c); return true;
    case 0x0E: value = fromField(entity.verticalAcceleration48); return true;
    case 0x0F: value = entity.fadeRamp62; return true;
    // Read back as a *signed* char, unlike the 0x15 case just below it.
    case 0x10: value = static_cast<std::uint32_t>(static_cast<std::int8_t>(entity.spawnParam94)); return true;
    // Read signed, unlike the 0x15 case, which reads the same width unsigned.
    case 0x11: value = static_cast<std::uint32_t>(static_cast<std::int8_t>(entity.byte95)); return true;

    case 0x13: value = fromField(entity.groundHeight4c); return true;
    case 0x19: value = entity.state60; return true;

    case 0x1A: value = fromField(entity.desiredDeltaX30); return true;
    case 0x1B: value = fromField(entity.desiredDeltaZ34); return true;
    case 0x1C: value = fromField(entity.velocityX3c); return true;
    case 0x1D: value = fromField(entity.velocityZ40); return true;
    case 0x1E: value = fromField(entity.verticalVelocity44); return true;

    case 0x1F: value = fromField(entity.rotationX154); return true;
    case 0x20: value = fromField(entity.rotationY158); return true;

    case 0x22: value = entity.fadeLevel134; return true;
    case 0x23: value = entity.fadeColor138; return true;
    case 0x26: value = static_cast<std::uint32_t>(entity.freezeTimerBd); return true;

    case 0x28: value = fromField(entity.radius54); return true;
    case 0x29: value = fromField(entity.height58); return true;

    case 0x38: value = entity.eventFlagId198; return true;

    default: return false;
    }
  }

  const char *objectRegisterFieldName(std::uint32_t index)
  {
    switch (index)
    {
    case 0x00: return "+0x00 type id";
    case 0x01: return "+0x02 descriptor flags";
    case 0x02: return "+0x0C collision flags";
    case 0x03: return "+0x04 halfword";
    case 0x04: return "+0x08 halfword";
    case 0x05: return "+0x06 animation/contact flags";
    case 0x06: return "+0xA8 substate frame";
    case 0x07: return "+0xAA flags";
    case 0x08: return "+0xA0 animation id";
    case 0x09: return "+0x6C terrain flag word";
    case 0x0A: return "+0x70 terrain flag word";
    case 0x0B: return "+0x78 required terrain mask";
    case 0x0C: return "+0x74 reject terrain mask";
    case 0x0D: return "+0x5C facing";
    case 0x0E: return "+0x48 vertical acceleration";
    case 0x0F: return "+0x62 fade ramp";
    case 0x10: return "+0x94 spawn param";
    case 0x11: return "+0x95 byte";
    case 0x13: return "+0x4C ground height";
    case 0x14: return "+0xBE halfword";
    case 0x15: return "+0xBC byte";
    case 0x16: return "+0xC2 halfword";
    case 0x17: return "+0xC4 angle";
    case 0x18: return "+0xC0 halfword";
    case 0x19: return "+0x60 state";
    case 0x1A: return "+0x30 desired delta X";
    case 0x1B: return "+0x34 desired delta Z";
    case 0x1C: return "+0x3C velocity X";
    case 0x1D: return "+0x40 velocity Z";
    case 0x1E: return "+0x44 vertical velocity";
    case 0x1F: return "+0x154 angle";
    case 0x20: return "+0x158 angle";
    case 0x21: return "+0x7C float (broadcast to the party when lead)";
    case 0x22: return "+0x134 fade level";
    case 0x23: return "+0x138 fade colour";
    case 0x26: return "+0xBD freeze timer";
    case 0x28: return "+0x54 collision radius";
    case 0x29: return "+0x58 collision height";
    case 0x2A: return "+0x11C float";
    case 0x2B: return "+0x120 float";
    case 0x2C: return "+0x12A halfword";
    case 0x2D: return "+0x128 halfword";
    case 0x2E: return "+0x12C halfword";
    case 0x2F: return "+0x12E halfword";
    case 0x30: return "+0x132 byte";
    case 0x32: return "+0x195 byte";
    case 0x33: return "+0x0A halfword";
    case 0x34: return "+0x140 float";
    case 0x35: return "+0x144 float";
    case 0x36: return "+0x148 float";
    case 0x37: return "+0x133 byte";
    case 0x38: return "+0x198 dword";
    case 0x39: return "+0x19C dword";
    case 0x3A: return "+0x1A0 dword";
    case 0x3B: return "+0x1A4 dword";
    case 0x3C: return "+0x1A8 dword";
    case 0x3D: return "+0x1AC dword";
    case 0x3E: return "+0x1B0 dword";
    case 0x3F: return "+0x1B4 dword";
    case 0x40: return "+0x96 byte";
    default: return nullptr;
    }
  }

} // namespace orphen::ported::script
