#pragma once

// Which cue an entity's own actions make.
//
//   src/FUN_002298d0.c  type id -> character class
//   src/FUN_00251c80.c  class + sound index -> cue number
//   src/FUN_00255d88.c  the surface under an entity picks the sound index
//   src/FUN_00256ff8.c  the foot-plant keyframe
//
// This is the layer between a behaviour and FUN_00267d38. Nothing here knows
// which entity it is working on beyond its type id: a footstep is "whatever
// cue this character class makes on this material", and the same three
// functions serve the player, a party member and anything else that walks.
//
// == Cue numbering ==
//
// FUN_00251c80 offsets a sound index by the character's class:
//
//     class 4 -> index + 0x6F
//     class 5 -> index + 0x0F
//     else    -> index + 0x3F
//
// and FUN_002298d0 maps the type id to the class. Type 1, the lead player, is
// class 0, so its cues start at 0x3F.
//
// == Surfaces ==
//
// FUN_00255d88 reads the settled surface's D-record word 1 -- the same
// `terrainFlags` the ground query already returns -- and takes its **top
// nibble** as the material. DAT_0031E028 is then a [material][kind] table of
// sound indices, four dwords per material:
//
//     material 0   0x00 0x04 0x08 0x0C
//     material 1   0x01 0x05 0x09 0x0D
//     material 2   0x02 0x06 0x0A 0x0E
//     material 3   0x03 0x07 0x0B 0x0F
//     material 4   0x16 0x17 0x18 0x19
//     material 5   0x1A 0x1B 0x1C 0x1D
//     material 6   0x1E 0x1F 0x20 0x21
//     material 7   0    0    0    0
//
// The table ends there: 0x0031E0E8, twelve rows in, is the player state table,
// and the two rows before it hold unrelated floats. Material 7 is the value
// FUN_00255d88 substitutes when there is no ground at all, so the zero row is
// deliberate; materials 8 and up would read past the table, which is a real
// out-of-range read the original never seems to make.
//
// The kinds are the columns, and the callers name them: FUN_00256ff8 passes
// the run flag, so 0 is a walking step and 1 a running one, and FUN_00256bb8's
// jump branch passes 2.
//
// == The foot-plant ==
//
// FUN_00256ff8 is the whole footstep mechanism, and it is driven by the
// *animation*, not by a timer: the walk and run timelines carry 0x100 in the
// trailing word of the keyframes where a foot lands, and 0x200 alongside it on
// the second of the two. In grp_0001 that is
//
//     animation 0x0B (walk)  keyframe 1 -> 0x300, keyframe 3 -> 0x100
//     animation 0x0E (run)   keyframe 4 -> 0x100, keyframe 9 -> 0x300
//
// so a cycle plants twice and 0x200 distinguishes the two feet -- which is
// what picks the dust effect (FUN_00257098 with 8/1 or 9/2), not the sound.
//
// `+0x04 bit 0x1000` gates the sound. In the s01_e024 dump the lead player's
// +0x04 is 0x3024 and the party members' is 0x00A4, so only the player is
// audible -- which is also why FUN_00256ff8 is only ever called from player
// states.

#include "ported/entity/original_entity.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace orphen::ported::entity
{

  // The columns of DAT_0031E028.
  enum class SurfaceSoundKind
  {
    Walk = 0,
    Run = 1,
    Jump = 2,
    Extra = 3,
  };

  // FUN_002298d0.
  int FUN_002298d0_character_class(std::int16_t typeId);

  // FUN_00251c80.
  std::uint16_t FUN_00251c80_character_cue(std::int16_t typeId, int soundIndex);

  // FUN_00255d88. `terrainFlags` is the settled surface's D-record word 1;
  // nullopt is the original's FUN_00227798-failed path, material 7. `carrying`
  // is entity +0x68 being non-zero, which forces material 2.
  std::uint16_t FUN_00255d88_surface_cue(std::int16_t typeId,
                                         std::optional<std::uint32_t> terrainFlags,
                                         bool carrying,
                                         SurfaceSoundKind kind);

  // The material FUN_00255d88 derives, exposed so a caller can report one that
  // falls outside the table.
  int FUN_00255d88_material(std::optional<std::uint32_t> terrainFlags, bool carrying);
  inline constexpr int kSurfaceMaterialCount = 8;

  // FUN_00267d38 with an entity: how a behaviour reaches the sound engine.
  using EntitySoundPlayer = std::function<void(std::uint16_t cue, const OriginalEntity &at)>;

  // FUN_00256ff8. A no-op unless the animation stepped onto a keyframe
  // carrying 0x100 this frame and the entity is audible.
  void FUN_00256ff8_footstep(const OriginalEntity &entity,
                             bool running,
                             std::optional<std::uint32_t> terrainFlags,
                             const EntitySoundPlayer &play);

} // namespace orphen::ported::entity
