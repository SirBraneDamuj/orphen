#pragma once

// The type 0x58 field HP gauge -- the animated bar in the top left corner of a
// field scene.
//
//   src/FUN_0022a418.c   0x0022A418:388  builds it, once per scene load
//   src/FUN_002d0ea8.c   0x002D0EA8      its per-frame behaviour
//   src/FUN_00251ed8.c   0x00251ED8      drains the hit points it reads
//
// **It is pool slot 8, and it is an entity like any other.** FUN_0022a418
// builds it at 0x0058CD70, which is `DAT_0058BEB0 + 8 * 0x1D8`, and parks it at
// screen (48, 40) with DAT_003524E0 -- 65534.0 -- in +0x28. Its descriptor,
// 0x003196E4 + the type bias, carries +0x04 = 0x0600 and +0x16 = 0x5040:
//
//   +0x02 bit 0x200   FUN_0020C5A8 refuses it, so the skeletal pass never sees
//                     it and FUN_0020F3E0's billboard pass picks it up instead.
//   +0x08 bit 0x1000  the billboard pass's screen-space branch: +0x20/+0x24 are
//                     already pixels of the 640x448 virtual screen and +0x28 is
//                     a GS depth word, not a world height.
//
// So nothing here draws. The gauge is a sprite strip (model record 0x57 ->
// grp_398, tex_374 statically bound to texture slot 0x2C) and the ordinary
// sprite pass renders it, exactly as it renders the two type 0x68 battle bars.
//
// ------------------------------------------------------------ the animation
//
// There is no geometry behind the fill level either. FUN_002D0EA8's tail bands
// the lead player's hit points into 31 animations:
//
//   +0xA0 = 30 - min(30, (lead +0x12A * 30) / lead +0x128)
//
// so animation 0 is a full bar and animation 30 an empty one. The divide traps
// on a zero maximum in the original; the port returns instead, because a port
// scene can reach a frame with the lead's stats unloaded and the original never
// can.
//
// ------------------------------------------------- when it is on screen
//
// **In practice: always, in field mode.** The gate at the top hides it -- +0x08
// bit 0 -- for a game mode other than 0, a scene load in flight, letterbox bars
// on screen, a lead that is itself hidden, a lead type at or above 0x3A, and
// event flag 0x50B. Past that gate the function never *sets* bit 0 again: the
// state-0 branch clears it once +0x98 has counted up to 0xF00, and the two
// item-pickup callers (FUN_002D46D0, FUN_002D4CD8) slam +0x98 to 0xF00 and
// clear the bit outright. Since FUN_0022A418 builds the slot with bit 0 already
// clear, the count-up is a no-op on a fresh scene and the bar is simply up.
//
// The one caller that raises the bit is FUN_00234468, the menu, and that is
// also a game-mode change -- so the hide is the gate's, not this function's.
// The +0x62 / +0x98 pair is kept because it is what the original runs, not
// because the port has found a frame where it decides anything.
//
// The lead's states 0x16..0x18 -- the three hit reactions FUN_00251ED8 plays --
// take their own branch: +0x98 to 0xF00, +0x62 to 1, bit 0 cleared. That is the
// "you were just hit, show the bar" path, and it is the only reason the
// machinery exists at all.

#include "ported/entity/entity_descriptor_table.h"
#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"

#include <cstddef>
#include <cstdint>

namespace orphen::ported::entity
{

  // 0x0058CD70 == DAT_0058BEB0 + 8 * 0x1D8.
  inline constexpr std::size_t kDAT_0058cd70_fieldHpGaugeSlot = 8;
  inline constexpr std::int32_t kFieldHpGaugeTypeId = 0x58;
  inline constexpr std::uint32_t kFUN_002d0ea8_fieldHpGauge = 0x002D0EA8;

  // FUN_0022a418:389-390. The screen position and the GS depth word it is
  // parked at, as the immediates spell them.
  inline constexpr float kFUN_0022a418_gaugeScreenX = 48.0f;  // 0x42400000
  inline constexpr float kFUN_0022a418_gaugeScreenY = 32.0f;  // 0x42200000
  inline constexpr float kDAT_003524e0_gaugeDepth = 65534.0f;

  // FUN_0022a418:388's three-way gate.
  //
  //   DAT_003555D3  a section-14 battle scene has no field gauge
  //   DAT_003555C6  the title screen's arena mode (stages 0x15..0x17). Nothing
  //                 in the port reaches it, so the caller passes false.
  //   DAT_003551F4  stage 0x0C is the title/menu stage
  void FUN_0022a418_build_field_hp_gauge(EntityPool &pool,
                                         const EntityDescriptorTable &descriptors,
                                         bool DAT_003555d3_groupEScene,
                                         bool DAT_003555c6_arenaMode,
                                         int DAT_003551f4_stage);

  // Everything FUN_002D0EA8 reads outside the two entities.
  struct FieldHpGaugeEnvironment
  {
    std::uint32_t frameTicks = 0;      // iGpffffb64c / DAT_003555BC
    int DAT_00354d2c_gameMode = 0;     // iGpffffadbc
    int DAT_00355054_letterboxMode = 0; // iGpffffb0e4
    std::uint32_t DAT_003551ec_sceneRequest = 0; // iGpffffb27c
    // FUN_00266368(0x50B). A scene raises it to suppress the gauge.
    bool DAT_0034ab70_flag50b = false;
    // cGpffffb6e4. FUN_0022A418 clears it and nothing in src/ ever sets it, so
    // it is false; it only matters when the lead is itself hidden.
    bool DAT_00355654 = false;
  };

  // 0x002D0EA8, the type 0x58 behaviour.
  void FUN_002d0ea8_field_hp_gauge(OriginalEntity &gauge,
                                   const OriginalEntity &lead,
                                   const FieldHpGaugeEnvironment &environment);

} // namespace orphen::ported::entity
