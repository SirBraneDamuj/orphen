#pragma once

// The area map: the orbiting 3D view of the level that Left on the D-pad puts
// up, and the game mode it runs in.
//
//   src/FUN_00224ff0.c  the field frame's gate. `uGpffffb686 & 0x8000` -- Left
//                       newly pressed -- checks event flag 0x512, calls
//                       FUN_00213EF0 and sets iGpffffadbc to 0xC
//   src/FUN_00213ef0.c  the open: push every global the view is about to
//                       overwrite, then overwrite them
//   src/FUN_00224418.c  PTR_FUN_00318A88[0xC], the whole mode-12 frame
//   src/FUN_00208ee8.c  the fork -- mode 12 runs FUN_00214300 where the field
//                       frame runs FUN_0020BEC8
//   src/FUN_00214300.c  the orbit camera: pan, zoom, tilt, turn, key light
//   src/FUN_002141d8.c  the close: pop everything FUN_00213EF0 pushed
//
// == It really is the level ==
//
// There is no second scene and no separate map asset. FUN_00208F28 walks the
// same PSM2 map, FUN_0020C5A8 draws the same entity pool, and the only thing
// mode 12 changes is the matrices they are handed and which globals are in
// force while they run. FUN_00213EF0 is a save/restore stack -- FUN_00213E68
// pushes a region to the scratch at 0x01949A10, FUN_00213EA8 pops it -- and
// what it pushes is exactly what FUN_002141D8 pops:
//
//   0x00343888 +0x140  the sixteen dynamic point lights, then all deactivated
//   0x0035566C         \ ambient and fog colour, replaced with 0x404040 and
//   0x00355670         / 0xFFFFFF
//   0x00355674         \ the global colour pair, both zeroed -- the first of
//   0x00355678         / these is what makes the void around the level black
//   0x0035567C         \ the fade radii, 500.0 and 600.0
//   0x00355680         /
//   0x003439C8 +0x0C   the key light direction
//   0x00343A08 +0x0C   the fog band, whose DAT_00343A10 is then set to -1000,
//                      which is the literal FUN_002000C0:216 tests to decide
//                      whether to run the fog pass at all
//   0x00355700         DAT_00355700, the global fade cap, set to 0
//   0x00355628         DAT_00355628, the draw distance, set to 500.0 -- this
//   0x0035562C         is what makes the whole level visible at once
//   0x005A96B0 +0x100  the per-slot status bytes, then masked (below)
//   0x0058BEB0 +0x1D8  entity slot 0, then respawned as type 0x256
//   0x003FFE00 +0x540  slot 0's pose filter state (bones 0..20), which the
//                      marker filters through while it has the slot
//
// The masking rule is FUN_00213EF0:56-72, over slots 1..255: a slot stays live
// only if its +0x08 has bit 0x2000 clear *and* its type is at or above 0x272
// (a type 0x38 reads its real type from +0x1CE first). On the entry-state scene
// that leaves exactly two slots live -- 0, the marker, and 10, a type 0x272
// treasure chest. The compass roses on screen are not entities; they are part
// of the map mesh.
//
// Slot 0 is then `FUN_00229C40(0x58BEB0, 0x256)` -- respawned in place as the
// marker actor, the arrow -- with `DAT_0058BEB4 |= 0x4119` on top of whatever
// the descriptor gave it. FUN_00214300 rescales it every frame to 2.0 / zoom,
// which is why the arrow stays the same size on screen however far the view
// pulls back.
//
// == What mode 12 leaves out ==
//
// FUN_00224418's whole body is `FUN_00225C20, FUN_00208450, FUN_00208EE8,
// FUN_00208F28, FUN_0020C5A8`, the exit test, and one caption. That is less
// than the field menu's modes 4 and 5 run: there is no FUN_0020F3E0, no
// FUN_002192C0 and no FUN_0020C290, so the effect pools stop and the fog
// backdrop is gone. FUN_002000C0 drops four more passes on `DAT_00354D2C ==
// 0xC` -- the frame feedback blur, the fog, FUN_00210CA0 and FUN_00202FF8 --
// and skips the FUN_002020A8 overlays.
//
// FUN_00225C90 is the one thing that still steps: it is the marker's own
// animation, called from inside FUN_00214300 rather than from an actor loop.
//
// == The camera ==
//
// An orbit rig about a focus point, seeded from the player's position, with the
// same projection the field camera builds. FUN_00214300 composes
//
//   translate(-focus) * rotZ(yaw + pi/2) * rotX(-pi/2 - pitch)
//     * scale(-zoom, zoom, -zoom) * translate(0, 0, 5)
//
// against FUN_0020BEC8's `translate(-eye) * rotZ(yaw + pi/2) * rotX(-pi/2 -
// pitch) * rotZ(roll) * scale(-1, 1, -1)`. So the zoom is not a projection
// change at all -- the world is scaled about the focus and then pushed five
// units down the view axis, and the projection is byte-for-byte the field's.
//
// Controls, all read straight out of FUN_0023B5D8's decode:
//
//   left stick     pan the focus. The heading is `stickAngle + yaw - pi/2`, so
//                  it is relative to the view, and the speed divides by the
//                  zoom so a close-in view pans slowly.
//   right stick    horizontal is zoom, vertical is tilt. The sectors are
//                  angular: within 35 degrees of +X zooms in by x1.04 a frame
//                  to a cap of 2.0, beyond 145 degrees zooms out by /1.04 to a
//                  floor of 0.04, and between 65 and 125 degrees either side
//                  tilts. Neither zoom step is scaled by the frame tick; the
//                  tilt and the pan both are.
//   L1 / R1        turn. R1 subtracts, L1 adds, 0.0015625 radians a tick --
//                  0.05 a frame at the nominal 32.
//   Cross or Left  exit (FUN_00224418:12, `DAT_003555F6 & 0x8040`).
//
// The key light is the odd one. DAT_00354C60 advances one degree every frame,
// unscaled by the tick, and the light direction is (cos, sin, -0.8) of it
// normalised -- so the whole level is lit by a sun that circles it once every
// six seconds. It is not a bug; it is what makes an unlit top-down view
// readable.

#include "ported/psm2/psm2_runtime.h"
#include "ported/render/original_view_projection.h"

#include <cstdint>

namespace orphen::ported::scene
{

  using orphen::ported::psm2::Vec3;

  // PTR_FUN_00318A88 slot 12, the mode FUN_00224FF0 raises.
  inline constexpr int kAreaMapMode = 0xC;

  // FUN_00224FF0:105, `FUN_00266368(0x512)`. Not story progress: FUN_0022A418
  // rewrites it on every scene load from the scene descriptor's +0x0C bit
  // 0x8000 (clear = map available), so it answers "does this scene have a
  // map", and battle scenes and the title stage say no.
  inline constexpr std::uint32_t kAreaMapEventFlag = 0x512;

  // FUN_00213EF0:84. The marker actor slot 0 is respawned as, and the bits
  // FUN_00213EF0:86 ORs into its +0x04 afterwards.
  inline constexpr std::int32_t kAreaMapMarkerType = 0x256;
  inline constexpr std::uint16_t kAreaMapMarkerFlags04 = 0x4119;

  // FUN_00213EF0:58-70. A slot below this type is hidden for the duration.
  inline constexpr std::uint16_t kAreaMapKeptTypeFloor = 0x272;
  // The type that indirects through +0x1CE for its real one.
  inline constexpr std::uint16_t kAreaMapIndirectType = 0x38;
  // +0x08 bit 0x2000 hides a slot whatever its type.
  inline constexpr std::uint16_t kAreaMapHiddenFlag08 = 0x2000;

  // FUN_00224418:20, `FUN_0025B9E8(0x30)` -- "Press <Cross> to Exit".
  inline constexpr int kAreaMapCaptionMessage = 0x30;
  // FUN_00224418:12. Cross (0x0040) or Left (0x8000).
  inline constexpr std::uint16_t kAreaMapExitMask = 0x8040;

  // 0x00213FCC-0x00214004: the ambient and the key light's colour, replaced
  // wholesale so the view is lit the same way in every scene.
  inline constexpr std::uint32_t kAreaMapAmbientRgb = 0x00404040;
  inline constexpr std::uint32_t kAreaMapLightRgb = 0x00FFFFFF;

  // The globals FUN_00213EF0 overwrites that the port models.
  inline constexpr float kDAT_00355628_areaMapDrawDistance = 500.0f;  // uGpffffb6b8
  inline constexpr float kDAT_0035562c_areaMapFogDistance = 500.0f;   // uGpffffb6bc

  // What FUN_0023B5D8 left in the pad globals, which is all FUN_00214300 reads.
  struct AreaMapPad
  {
    std::uint16_t DAT_003555f4_held = 0;    // uGpffffb684
    std::uint16_t DAT_003555f6_pressed = 0; // uGpffffb686
    // The movement stick: FUN_0023B3F0 on pad bytes 6/7.
    float DAT_003555e4_moveAngle = 0.0f;
    float DAT_003555e8_moveMagnitude = 0.0f;
    // The camera stick: FUN_0023B3F0 on pad bytes 4/5.
    float DAT_003555ec_cameraAngle = 0.0f;
    float DAT_003555f0_cameraMagnitude = 0.0f;
  };

  // Everything one FUN_00214300 produces that the frame outside it needs.
  struct AreaMapStep
  {
    orphen::ported::render::ViewProjection camera;
    // DAT_0058BFFC / DAT_0058C000, the marker's +0x14C and +0x150: 2.0 / zoom.
    float markerScale = 1.0f;
    // DAT_003439C8..D0 after FUN_00216510 normalised it.
    Vec3 DAT_003439c8_lightDirection{};
    // DAT_00355A50, consumed on the first frame: the marker's +0xA4 goes to 1.
    bool markerStateReset = false;
  };

  class AreaMap
  {
  public:
    // FUN_00213EF0's camera half. `focus` is DAT_0058BED0..D8, slot 0's
    // position, which the open copies to DAT_0055F090..98 before it respawns
    // the slot; `fieldYaw` is fGpffffb6d4, so the map opens facing the way the
    // field camera was.
    void FUN_00213ef0_open(const Vec3 &DAT_0058bed0_leadPosition, float fGpffffb6d4_fieldYaw);

    // FUN_002241D8's companion. The runtime pops the saved globals itself; this
    // only drops the mode.
    void FUN_002141d8_close();

    // FUN_00214300, minus the FUN_00225C90 call at its head -- the marker's
    // animation belongs to the entity pool, so the runtime steps it there.
    AreaMapStep FUN_00214300_step(const AreaMapPad &pad,
                                  std::uint32_t DAT_003555bc_frameTicks,
                                  bool cGpffffb66e_widescreen);

    bool open() const { return open_; }
    // DAT_0055F090..98.
    const Vec3 &DAT_0055f090_focus() const { return DAT_0055f090_focus_; }
    float DAT_00355a54_pitch() const { return DAT_00355a54_pitch_; }
    float DAT_00355a58_yaw() const { return DAT_00355a58_yaw_; }
    float DAT_00355a5c_zoom() const { return DAT_00355a5c_zoom_; }

  private:
    bool open_ = false;

    // DAT_0055F090/94/98: the point the rig orbits, in world units.
    Vec3 DAT_0055f090_focus_{};
    // uGpffffbae0 / DAT_00355A50, a signed byte. FUN_00213EF0 sets it to 1 and
    // FUN_00214300 spends it on the first frame.
    std::int8_t DAT_00355a50_markerReset_ = 0;
    // uGpffffbae4 / DAT_00355A54, seeded from 0x003520F8.
    float DAT_00355a54_pitch_ = 0.0f;
    // uGpffffbae8 / DAT_00355A58, seeded from the field camera's yaw.
    float DAT_00355a58_yaw_ = 0.0f;
    // uGpffffbaec / DAT_00355A5C, seeded from 0x003520FC.
    float DAT_00355a5c_zoom_ = 0.0f;
    // DAT_00354C60, the key light's bearing. It is *not* saved and restored by
    // FUN_00213EF0/FUN_002141D8 -- it just keeps turning from wherever the last
    // map view left it.
    float DAT_00354c60_lightBearing_ = 0.0f;
  };

} // namespace orphen::ported::scene
