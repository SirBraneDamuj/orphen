#include "ported/entity/original_entity_sound.h"

#include <array>

namespace orphen::ported::entity
{
  namespace
  {
    // DAT_0031E028, read out of the executable's data at 0x0031E028. Eight
    // rows of four; see the header for why it stops at eight.
    constexpr std::array<std::array<std::uint8_t, 4>, kSurfaceMaterialCount> DAT_0031e028_surfaceSounds{{
        {0x00, 0x04, 0x08, 0x0C},
        {0x01, 0x05, 0x09, 0x0D},
        {0x02, 0x06, 0x0A, 0x0E},
        {0x03, 0x07, 0x0B, 0x0F},
        {0x16, 0x17, 0x18, 0x19},
        {0x1A, 0x1B, 0x1C, 0x1D},
        {0x1E, 0x1F, 0x20, 0x21},
        {0x00, 0x00, 0x00, 0x00},
    }};

    // FUN_00251c80's three bases.
    constexpr int kCueBaseClass4 = 0x6F;
    constexpr int kCueBaseClass5 = 0x0F;
    constexpr int kCueBaseDefault = 0x3F;

    // FUN_00255d88's two substitutions.
    constexpr int kMaterialNoGround = 7;
    constexpr int kMaterialCarrying = 2;
    constexpr int kMaterialFoldedFrom = 0x0D;
    constexpr int kMaterialFoldedTo = 3;

    // FUN_00256ff8's gates.
    constexpr std::uint16_t kFootPlantEvent = 0x0100;   // entity +0xAA
    constexpr std::uint16_t kAnimationStepped = 0x0008; // entity +0x06
    constexpr std::uint16_t kAudible04 = 0x1000;        // entity +0x04
  } // namespace

  int FUN_002298d0_character_class(std::int16_t typeId)
  {
    switch (typeId)
    {
    case 1: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 3;
    case 6: return 4;
    case 7: return 5;
    case 0x16: return 6;
    default: return 7;
    }
  }

  std::uint16_t FUN_00251c80_character_cue(std::int16_t typeId, int soundIndex)
  {
    const int characterClass = FUN_002298d0_character_class(typeId);
    int base = kCueBaseDefault;
    if (characterClass == 4)
    {
      base = kCueBaseClass4;
    }
    else if (characterClass == 5)
    {
      base = kCueBaseClass5;
    }
    return static_cast<std::uint16_t>(soundIndex + base);
  }

  int FUN_00255d88_material(std::optional<std::uint32_t> terrainFlags, bool carrying)
  {
    int material = kMaterialCarrying;
    if (!carrying)
    {
      material = terrainFlags.has_value() ? static_cast<int>(*terrainFlags >> 28) : kMaterialNoGround;
    }
    if (material == kMaterialFoldedFrom)
    {
      material = kMaterialFoldedTo;
    }
    return material;
  }

  std::uint16_t FUN_00255d88_surface_cue(std::int16_t typeId,
                                         std::optional<std::uint32_t> terrainFlags,
                                         bool carrying,
                                         SurfaceSoundKind kind)
  {
    int material = FUN_00255d88_material(terrainFlags, carrying);
    if (material < 0 || material >= kSurfaceMaterialCount)
    {
      // The original indexes straight into whatever follows the table. The
      // port stays inside it and takes the no-ground row instead; a caller
      // that wants to notice can ask FUN_00255d88_material.
      material = kMaterialNoGround;
    }
    const std::uint8_t soundIndex =
        DAT_0031e028_surfaceSounds[static_cast<std::size_t>(material)][static_cast<std::size_t>(kind)];
    return FUN_00251c80_character_cue(typeId, soundIndex);
  }

  void FUN_00256ff8_footstep(const OriginalEntity &entity,
                             bool running,
                             std::optional<std::uint32_t> terrainFlags,
                             const EntitySoundPlayer &play)
  {
    if ((entity.flagsAa & kFootPlantEvent) == 0 || (entity.flags06 & kAnimationStepped) == 0)
    {
      return;
    }
    if ((entity.halfword04 & kAudible04) == 0)
    {
      // Not audible. The original still runs its FUN_00257098 dust effect here,
      // which the port has no counterpart for.
      return;
    }
    if (!play)
    {
      return;
    }
    play(FUN_00255d88_surface_cue(entity.typeId00, terrainFlags, entity.interactTarget68 >= 0,
                                  running ? SurfaceSoundKind::Run : SurfaceSoundKind::Walk),
         entity);
  }

} // namespace orphen::ported::entity
