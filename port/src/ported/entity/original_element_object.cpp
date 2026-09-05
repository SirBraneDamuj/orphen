#include "ported/entity/original_element_object.h"

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_00354B74. The same 0.08 the billboard pass scales +0x133 by; see
    // original_entity.h.
    constexpr float kDAT_00354b74_depthBiasUnit = 0.07999999821186066f;

    // FUN_0030BD20, the EE's float-to-int truncation.
    int FUN_0030bd20_trunc(float value) { return static_cast<int>(value); }
  } // namespace

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
