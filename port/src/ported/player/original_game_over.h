#pragma once

// The two states the player's death actually ends in.
//
//   src/FUN_00251ed8.c:122-141   the death branch of the damage drain
//   src/FUN_002555d8.c           state 0x18, which is where the death is *played*
//   src/FUN_00255820.c           state 0x1A, PTR_FUN_0031E0E8[0x1A]
//   0x002559E8                   state 0x1B, PTR_FUN_0031E0E8[0x1B] (no src/ file)
//
// == Death is state 0x18, not state 0x19 ==
//
// `FUN_00251ED8`'s death branch ends with `FUN_00225bf0(entity, 0x18, 0x20)` --
// the ordinary **knockback**, with `uGpffff88c0` of horizontal speed and
// `uGpffff88c8` of pop-up. So dying looks like being hit very hard: the body
// tumbles backwards through the air, lands, and stays down.
//
// State 0x19 is a different thing entirely. Its one writer is
// `FUN_00251ED8:60-71`, the terrain check -- `+0x6C & 0x1000000`, a lava or pit
// surface -- and `FUN_002557A0` under it sinks the body and hands it to
// `FUN_00255E40`, the respawn that puts the player back on the lead trail with
// one hit point. It is the *hazard* state, and the port used to run the death
// through it, which is why dying faded the body out and then stopped.
//
// == What happens instead ==
//
// `FUN_002555D8` (state 0x18) sees animation 0x21 -- knocked down -- run out of
// its `+0x62` frames with `+0x12A` at zero, and writes state **0x1A**.
//
// `FUN_00255820` is a one-shot: it never runs twice, because the last thing it
// does before returning is set state 0x1B. It is the whole staging of the game
// over, and it touches far more than the player.
//
// `FUN_002559E8` is then the frame loop, and it runs three ramps at once over
// about a second:
//
//   +0x62   0 -> 0x2000 at 4x the tick rate. `+0x62 / 32` is both the white
//           light's brightness on the body and the alpha of a full-screen black
//           quad, so the body lights up as the picture goes out.
//   fog     DAT_00355674 multiplied toward zero by `(255 - level) / 256`.
//   +0x1B6  0xFE0 down at 2x the tick rate. `+0x1B6 / 32` is written to
//           DAT_00355700 *and* to every entity's +0x134 from slot 10 up.
//
// The last of those is what empties the room, and the mechanism is worth
// stating plainly because it is not a fade: **DAT_00355700 at 3 stops the map
// being drawn at all.** `FUN_00209140:127` guards the whole primitive walk with
// `cap == 0 || cap > 3`, and `FUN_002559E8` parks the cap at exactly 3 once the
// ramp bottoms out. Confirmed on hardware -- writing 0x7F back into
// DAT_00355700 mid-game-over brings the room straight back.
//
// Everything else fades the honest way: the other fifteen dynamic lights lose
// four off each channel per frame and free themselves at zero, and the entities
// from slot 10 up take the same `+0x134` the cap does and get hidden outright
// when it reaches zero.
//
// Once both ramps are done the state holds. `+0x1A4` counts 19200 ticks -- ten
// seconds -- down by the frame tick, and either that or a press of Circle or
// Cross (`uGpffffb686 & 0x60`) hands off to `FUN_00237A08`.
//
// == What the port does not do ==
//
// `FUN_00237A08` itself: it arms a fade, sets game mode 0xC and calls
// `FUN_002241D8`, which is the return to the title screen. The port has no mode
// 0xC, so the hand-off is a hook -- see `onHandOff` -- and the runtime spends it
// on the fade alone. The lead is left in state 10 either way, which is what the
// original does first.
//
// `DAT_00343A10 = -1000.0` (the gate on `FUN_002025E0`, which the port does not
// have), `FUN_00212DB0(0, 0, 0)` (the star field, likewise) and
// `cGpffffb664` (a sound-suppression latch nothing in the port reads) are the
// three writes with no counterpart here.

#include "ported/entity/entity_pool.h"
#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_light_table.h"

#include <cstdint>
#include <functional>

namespace orphen::ported::player
{

  // Everything FUN_00255820 and FUN_002559E8 reach outside pool slot 0. The
  // plain pointers are globals the port already models under the same names;
  // the callbacks are the ones that need a layer the player does not see.
  struct GameOverHooks
  {
    orphen::ported::entity::EntityPool *DAT_0058beb0_pool = nullptr;
    orphen::ported::render::LightTable *DAT_00343888_lights = nullptr;

    // DAT_00355700. FUN_00209140 reads it as the map's draw gate as well as its
    // fade, so this is the field that empties the room.
    std::uint8_t *DAT_00355700_globalFadeCap = nullptr;

    // The scene environment block: ambient, light 0 and the fog colour, all
    // packed 0x00RRGGBB, plus light 0's direction. FUN_00255820 replaces the
    // first three outright and FUN_002559E8 decays the fog toward black.
    std::uint32_t *DAT_0035566c_ambient = nullptr;
    std::uint32_t *DAT_00355670_lightColour = nullptr;
    std::uint32_t *DAT_00355674_fogColour = nullptr;
    // Three floats, the way the scene state stores them.
    float *DAT_003439c8_lightDirection = nullptr;
    // Whatever has to happen for a write to the four above to reach the
    // renderer. Called once per FUN_00255820 and once per FUN_002559E8 frame.
    std::function<void()> applySceneEnvironment;

    // FUN_00265EC0(0x58C610) and FUN_00265EC0(0x58CB98): pool slots 4 and 7,
    // the bandana and whatever is riding slot 7, released with their children.
    std::function<void(std::size_t slot)> FUN_00265ec0_release;

    // FUN_002D36F8. Clears DAT_00355620 and installs FUN_002D3320 on it.
    std::function<void()> FUN_002d36f8_install_dust;

    // FUN_002218F0(-1) plus the six uGpffffad38..ad4c gates FUN_00255820 zeroes
    // by hand: every weather and effect pool stops.
    std::function<void()> stopEffectPools;

    // FUN_00217E18(0), FUN_00216968(3.2) and the two camera bytes:
    // cGpffffb6e2 |= 4 (the cutscene gate) and bGpffffb6e1 = 0 (the sub-mode).
    std::function<void()> FUN_00255820_stage_camera;
    // bGpffffb6e0 = 5 and cGpffffb6e6 = 1, written *every frame* by
    // FUN_002559E8's tail. Mode 5 is FieldCameraMode::FixedStep, the constant
    // yaw step -- that, and nothing else, is the slow orbit around the body.
    std::function<void()> FUN_002559e8_hold_camera;

    // FUN_00255820:46-49. cGpffffb6e4 is the free-look latch; if the player was
    // holding the camera when the blow landed, it is dropped and the body is
    // un-hidden, because free-look hides it. Returns whether it was set.
    std::function<bool()> clearFreeLook;

    // FUN_00206260(slot, 0x32, 0) for slots 0..6, then FUN_002063C8(7, 0xF,
    // 1000). The ramp up is on the slot FUN_00251ED8's unported death sting
    // would have loaded, so on its own it moves a fader with nothing behind it.
    std::function<void()> FUN_00255820_stage_sound;

    // FUN_00255CE8(alpha): a full-screen black quad, GS sort bucket **2**. It
    // is under the map, not over it -- a hardware capture with the alpha at
    // 0xFF still shows the room when the fade cap is forced back up -- so all
    // it covers is the backdrop.
    std::function<void(std::uint8_t alpha)> FUN_00255ce8_black_quad;

    // uGpffffb686 & 0x60: Circle or Cross, newly pressed.
    std::function<bool()> skipRequested;

    // FUN_00237A08, and the four DAT_00345A18 background bytes beside it.
    std::function<void()> onHandOff;
  };

  // PTR_FUN_0031E0E8[0x1A]. One shot: it leaves the entity in state 0x1B.
  void FUN_00255820_enter_game_over(orphen::ported::entity::OriginalEntity &lead,
                                    const GameOverHooks &hooks);

  // PTR_FUN_0031E0E8[0x1B]. Every frame until the hand-off.
  void FUN_002559e8_update_game_over(orphen::ported::entity::OriginalEntity &lead,
                                     std::uint32_t frameTicks,
                                     const GameOverHooks &hooks);

} // namespace orphen::ported::player
