#pragma once

// The field menu: the seven-item panel Up or Down on the D-pad puts up, and the
// two game modes it runs in.
//
//   src/FUN_00224ff0.c  the field frame's gate. `uGpffffb686 & 0x5000` -- Up or
//                       Down newly pressed -- sets iGpffffadbc to 4 and calls
//                       FUN_00231A98
//   src/FUN_002241e0.c  the mode dispatch, `PTR_FUN_00318A88[DAT_00354D2C]`
//   src/FUN_00231a98.c  the open: availability, the widest label, the layout
//   src/FUN_00231958.c  mode 5, the navigation
//   src/FUN_00231c50.c  the draw, run by both modes
//   src/FUN_002318c0.c  the per-item colour ramp
//   src/FUN_00231c30.c  the bar behind one item, a FUN_00239020 entry at
//                       0x0031C388
//   src/FUN_0023b9f8.c  the auto-repeat
//   src/FUN_002241d8.c  the close
//
// == Two modes, not one ==
//
// The mode table at 0x00318A88 is not in the decompiled sources -- Ghidra has no
// function at either entry -- so both were read out of SLUS_200.11. Slot 4 is
// 0x00224570 and slot 5 is 0x00224518, and the only difference between them is
// which of FUN_00231958 / FUN_00231C50 they call:
//
//   mode 4   `lhu v0,-0x497c(gp); andi v0,v0,0xf060; bne v0,zero,+3; li v0,5;
//             sw v0,-0x5244(gp)` -- while any D-pad direction, Circle or Cross
//             is still *held*, stay in 4; otherwise drop to 5. Then FUN_00231C50
//             (draw only) and the render tail.
//   mode 5   FUN_00231958 (navigate, then draw) and the same tail.
//
// So mode 4 is the swallow of the press that opened the panel. Without it the
// Up that opened the menu would immediately walk the selection.
//
// == What the modes leave out ==
//
// Both tails are `FUN_00225C20, FUN_00208450, FUN_00208EE8, FUN_00208F28,
// FUN_0020C5A8, FUN_0020F3E0, FUN_002192C0, FUN_0020C290` -- the draw half of
// the field frame plus the effect-pool step, and nothing else. No script tick,
// no player controller, no actor loop, no FUN_002261E0 physics, no FUN_00216AA0
// camera, no FUN_00237FC0. The game is frozen, not slowed.
//
// Confirmed on hardware: with the panel up on the entry-state scene, ninety
// stepped frames produced a byte-identical screenshot -- the idle animation is
// stopped too.
//
// == The panel ==
//
// Seven labels, SCR.BIN resource 1 messages 0x3F..0x45, which are plain
// NUL-terminated ASCII rather than a dialogue stream:
//
//   0  Button Configuration          4  Return to Title Screen
//   1  Screen Ratio                  5  Item
//   2  Analog Controller Vibration   6  Equip
//   3  World Map Display
//
// Item 6 is the only one with a condition on it: FUN_00231A98:20 clears its bit
// when `FUN_002298D0(DAT_0058BEB0) - 1` is below 2, i.e. for two of the playable
// leads. Availability lives in uGpffffbcc0 as one bit per item; a cleared bit
// dims the label to 0x40 grey and makes Cross do nothing.
//
// Each item is centred at entry y `0x70 - index * 0x1E`, with an opaque bar two
// units below it. The bar is the same width for every item -- the widest label
// plus 0x20 -- and is one 128x20 texel block of the button-icon sheet, slot 0x2C
// read through CLUT bank 6. **Its entry at 0x0031C388 has blend mode 0**, where
// every glyph entry FUN_00238608 builds carries 1, which is why the bars are
// solid and the text over them is not.
//
// Draw order is the display list's, which FUN_00207938 builds head-first: the
// last entry submitted is drawn first. FUN_00231C50 submits text then bar per
// item, top item first, so the panel paints caption, bar 6, text 6, bar 5, ...
// -- each bar behind its own label.

#include "ported/text/original_dialogue_text.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orphen::ported::scene
{

  // PTR_FUN_00318A88 slots 4 and 5, and the field mode the panel leaves behind.
  inline constexpr int kFieldMenuModeSwallow = 4;
  inline constexpr int kFieldMenuModeActive = 5;

  inline constexpr int kFieldMenuItemCount = 7;
  // FUN_00231A98:31 and FUN_00231C50:23 -- `FUN_0025B9E8(index + 0x3F)`.
  inline constexpr int kFieldMenuFirstLabelMessage = 0x3F;
  // FUN_00231C50:41, the caption in the corner: "Press <Cross> to Select
  // <Triangle> to Cancel".
  inline constexpr int kFieldMenuCaptionMessage = 0x29;

  // FUN_00267D38 cue numbers, through the four one-line wrappers FUN_00237AD8,
  // FUN_00237AC8, FUN_002256C0 and FUN_002256B0.
  inline constexpr int kFieldMenuCueMove = 1;
  inline constexpr int kFieldMenuCueConfirm = 2;
  inline constexpr int kFieldMenuCueOpen = 4;
  inline constexpr int kFieldMenuCueBack = 5;

  struct FieldMenuPad
  {
    std::uint16_t uGpffffb684_held = 0;
    std::uint16_t uGpffffb686_pressed = 0;
    std::uint16_t uGpffffb68e_stickDirection = 0;
  };

  // What one step of the panel did. `cue` is -1 when it made no sound, and at
  // most one of the three outcomes is set: the original's FUN_00231958 returns
  // after the Triangle branch and takes the navigate and confirm branches
  // exclusively.
  struct FieldMenuStep
  {
    int cue = -1;
    // The item Cross confirmed, or -1. Only an available item ever lands here.
    int confirmed = -1;
    // Triangle: FUN_002241D8 put the frame mode back to 0.
    bool closed = false;
  };

  class FieldMenu
  {
  public:
    // FUN_00231A98. `labels` are the seven messages already read out of the item
    // database, and `mask` is uGpffffbcc0 -- bit per item, set for available.
    // Measuring the labels needs the font, so the bar width is computed here
    // exactly as the original computes it at open time.
    void FUN_00231a98_open(const std::array<std::string, kFieldMenuItemCount> &labels,
                           std::uint16_t uGpffffbcc0_availability,
                           const orphen::ported::text::DialogueFont &font);

    // FUN_00224570 (mode 4) then FUN_00231958 (mode 5), whichever the frame is
    // in. Returns what the step did; the caller plays the cue and acts on the
    // selection, which is where the original's PTR_FUN_0031C3C0 handler would
    // have run.
    FieldMenuStep step(const FieldMenuPad &pad, std::uint32_t frameTicks);

    // FUN_00231C50. Both modes draw, so this runs on every frame the panel is
    // up. `caption` is message 0x29; an empty one is simply not drawn.
    std::vector<orphen::ported::text::DialogueSprite> FUN_00231c50_layout(
        const std::string &caption,
        const orphen::ported::text::DialogueFont &font) const;

    bool open() const { return DAT_00354d2c_mode_ != 0; }
    // iGpffffadbc while the panel owns the frame: 4 or 5.
    int DAT_00354d2c_mode() const { return DAT_00354d2c_mode_; }
    int uGpffffae34_selected() const { return uGpffffae34_selected_; }
    const std::string &label(int index) const { return labels_[static_cast<std::size_t>(index)]; }
    bool available(int index) const
    {
      return (uGpffffbcc0_availability_ >> index & 1) != 0;
    }

  private:
    // FUN_002318C0(selected, index, &colourByte, 0x2080): ramp the alpha byte of
    // one item's colour word towards 0x80 when it is the selected one and 0x20
    // when it is not, by frameTicks/8 a step.
    void FUN_002318c0_ramp(int index, std::uint32_t frameTicks);
    // FUN_0023B9F8(mask, 1). The original's budget and step counter are globals
    // shared with the dialogue window's copy of this helper; nothing reads them
    // across the two, because only one of the two can be up at a time.
    bool FUN_0023b9f8_autoRepeat(std::uint16_t mask, FieldMenuPad &pad, std::uint32_t frameTicks);

    int DAT_00354d2c_mode_ = 0;
    int uGpffffae34_selected_ = 0;
    std::uint16_t uGpffffbcc0_availability_ = 0;
    std::array<std::string, kFieldMenuItemCount> labels_{};
    // DAT_0031C45C after FUN_00231A98:48: the widest label plus 0x20.
    int DAT_0031c45c_barWidth_ = 0;
    // The alpha byte of each item's colour word, the thing FUN_002318C0 walks.
    std::array<int, kFieldMenuItemCount> itemAlpha_{};

    std::int32_t iGpffffaf00_repeatBudget_ = 0;
    std::int16_t sGpffffaefe_repeatSteps_ = 0;
  };

} // namespace orphen::ported::scene
