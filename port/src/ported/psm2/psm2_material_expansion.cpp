#include "ported/psm2/psm2_material_expansion.h"

#include <cstring>

namespace orphen::ported::psm2
{
  namespace
  {

    // Flag 0x4 on 0x80-record +0x70: the colour index names four consecutive
    // palette entries rather than one shared entry.
    constexpr std::uint32_t kPerVertexColourBit = 0x4;

    // Set on +0x70 when slot 0 asks for a blend, alongside the +0x78 term.
    constexpr std::uint32_t kBlendFlag = 0x20000;

    // FUN_00211230:146-164 sets this while building the static packet, from
    // the same `slot flags & 0x70` test -- but for *any* slot, not just slot 0
    // -- at the point it also turns on the PRIM word's ABE bit. It is applied
    // here because the port has no packet builder, and FUN_00209140 reads it
    // as the "already blended, never fade" gate.
    constexpr std::uint32_t kAlphaBlendFlag = 0x40;

    // FUN_0022c3d8:29-34 and :57-62 both accumulate the palette entry as
    // (byte2 << 16) | (byte1 << 8) | byte0, which puts red in the low byte --
    // the order the GS RGBAQ register wants.
    std::uint32_t paletteColour(const Psm2RuntimeState &state, std::size_t index)
    {
      if (index >= state.DAT_00355bdc_palette.size())
      {
        return 0;
      }
      return state.DAT_00355bdc_palette[index].packed();
    }

    void expandVertexColours(const Psm2RuntimeState &state, DRecord80 &record)
    {
      if ((record.primitiveFlags & kPerVertexColourBit) == 0)
      {
        const std::uint32_t colour = paletteColour(state, record.colourIndex);
        record.vertexColours = {colour, colour, colour, colour};
        return;
      }

      for (std::size_t corner = 0; corner < record.vertexColours.size(); ++corner)
      {
        record.vertexColours[corner] = paletteColour(state, static_cast<std::size_t>(record.colourIndex) + corner);
      }
    }

    // FUN_0022c3d8:69-72. Selector -1 means a fixed mid grey; the original
    // literally stores the address of DAT_00404040, whose little-endian bytes
    // are 40 40 40 00.
    void applyAbsentSlot(MaterialSlot &slot)
    {
      slot.type = 0xfe;
      slot.textureCoordinates[0] = 0x40;
      slot.textureCoordinates[1] = 0x40;
      slot.textureCoordinates[2] = 0x40;
      slot.textureCoordinates[3] = 0x00;
    }

    // FUN_0022c3d8:73-78. Any other negative selector indexes the palette with
    // its low 15 bits and expands to (b0 << 16) | (b1 << 8) | b1 -- note that
    // is a different channel order from the vertex-colour path, and that the
    // blue byte of the entry is unused.
    void applyPaletteSlot(const Psm2RuntimeState &state, MaterialSlot &slot, std::int16_t selector)
    {
      const std::size_t index = static_cast<std::size_t>(selector) & 0x7fffu;
      std::uint8_t first = 0;
      std::uint8_t second = 0;
      if (index < state.DAT_00355bdc_palette.size())
      {
        first = state.DAT_00355bdc_palette[index].byte0;
        second = state.DAT_00355bdc_palette[index].byte1;
      }

      slot.type = 0xff;
      slot.textureCoordinates[0] = second;
      slot.textureCoordinates[1] = second;
      slot.textureCoordinates[2] = first;
      slot.textureCoordinates[3] = 0x00;
    }

    // FUN_0022c3d8:79-98: copy the section E record's 12 bytes in, then two
    // fixed remaps.
    void applySectionESlot(const Psm2RuntimeState &state, MaterialSlot &slot, std::int16_t selector)
    {
      const std::size_t index = static_cast<std::size_t>(selector);
      if (index >= state.DAT_003556b4_sectionERecords.size())
      {
        applyAbsentSlot(slot);
        return;
      }

      const auto &record = state.DAT_003556b4_sectionERecords[index];
      std::memcpy(slot.textureCoordinates.data(), record.bytes.data(), slot.textureCoordinates.size());
      slot.type = record.bytes[8];
      slot.byte9 = record.bytes[9];
      slot.alpha = record.bytes[10];
      slot.flags = record.bytes[11];

      if (slot.type == 0x0f)
      {
        slot.type = 0x09;
      }

      if (slot.alpha == 0xff)
      {
        slot.alpha = 0x80;
      }
      else
      {
        slot.alpha = static_cast<std::uint8_t>(slot.alpha >> 1);
      }
    }

  } // namespace

  void expandPsm2Materials(Psm2RuntimeState &state)
  {
    for (auto &record : state.DAT_003556ac_dRecords80)
    {
      record.blendTerm = 0.0f;
      expandVertexColours(state, record);

      for (std::size_t slotIndex = 0; slotIndex < record.materialSlots.size(); ++slotIndex)
      {
        MaterialSlot &slot = record.materialSlots[slotIndex];
        const std::int16_t selector = record.slotSelectors[slotIndex];

        if (selector < 0)
        {
          if (selector == -1)
          {
            applyAbsentSlot(slot);
          }
          else
          {
            applyPaletteSlot(state, slot, selector);
          }
          continue;
        }

        applySectionESlot(state, slot, selector);

        if ((slot.flags & 0x70) != 0)
        {
          record.primitiveFlags |= kAlphaBlendFlag;
        }

        // FUN_0022c3d8:99-110. Only slot 0 can put the primitive into the
        // blended path, and the term it picks depends on whether the slot
        // asked for the fully opaque alpha.
        if (slotIndex == 0 && (slot.flags & 0x70) != 0)
        {
          if ((slot.flags & 0x40) == 0 || slot.alpha == 0x80)
          {
            record.blendTerm = -2.0f;
          }
          else
          {
            record.blendTerm = -0.5f;
          }
          record.primitiveFlags |= kBlendFlag;
        }
      }

      record.dynamicFade = kFadeLoadValue;
    }
  }

} // namespace orphen::ported::psm2
