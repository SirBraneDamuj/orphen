#pragma once

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{
  struct OriginalEntity;
}

namespace orphen::ported::script
{

  // Native counterparts of src/FUN_0025c8f8.c (write) and src/FUN_0025c548.c
  // (read): the "object register" file that opcodes 0x76..0x7C address.
  //
  // These are not registers in any storage sense. Both functions are a switch on
  // the register index whose cases write straight through DAT_00355044 -- the
  // entity FUN_0025d6c0 last selected. So an object register *is* an entity
  // field, and the index is just a stable name for an offset that the script
  // compiler could emit without knowing the struct layout.
  //
  // This matters more than it looks. s01_e024's init issues twelve 0x77 writes
  // and five 0x79 writes, and index 13 is the facing angle at +0x5C -- so these
  // opcodes are what point the room's NPCs in their authored directions. A port
  // that keeps the values in a side array loads a scene full of characters all
  // facing angle zero.
  //
  // Every divisor in both functions (fGpffff8bac..fGpffff8bf4, DAT_00352adc..
  // DAT_00352b18) reads 100000.0 out of the retail executable -- the same
  // kScriptCoordinateScale the position opcodes use. They are separate globals,
  // presumably so they could have been tuned independently, but they never were.

  // The register file is indexed 0x00..0x40; FUN_0025c8f8's last case is 0x40.
  inline constexpr std::size_t kObjectRegisterCount = 0x41;

  // fGpffff8bac and friends, all 100000.0.
  inline constexpr float kObjectRegisterScale = 100000.0f;

  // FUN_00216690 (0x00216690): wrap an angle into [-pi, pi] by repeated
  // addition, capped at 16 iterations so a wild input cannot spin forever. The
  // constants are DAT_00352188 / DAT_0035218c / DAT_00352190 read out of the
  // retail executable; note they are a slightly short pi, not M_PI, and that
  // 2*pi is stored separately rather than derived, so this is not exactly fmod.
  float FUN_00216690_wrapAngle(float radians);

  // Returns false when the index names an entity field this port does not model
  // yet. The caller keeps its side array for those and counts them, so an
  // unmodelled field is reported rather than silently dropped -- the same
  // discipline the opcode dispatch uses for unimplemented opcodes.
  //
  // An index with no case at all in the original (0x12, 0x24, 0x25, 0x27, 0x31)
  // also returns false; those fall into FUN_0025c8f8's empty default, which
  // writes nothing, and FUN_0025c548's default, which reads 0.
  bool FUN_0025c8f8_write_object_register(entity::OriginalEntity &entity,
                                          std::uint32_t index,
                                          std::uint32_t value);

  bool FUN_0025c548_read_object_register(const entity::OriginalEntity &entity,
                                         std::uint32_t index,
                                         std::uint32_t &value);

  // The entity field an index names, as "+0xNN name", or nullptr when the index
  // has no case in the original at all. Used by the report to distinguish "this
  // port does not model that field" from "that index means nothing".
  const char *objectRegisterFieldName(std::uint32_t index);

} // namespace orphen::ported::script
