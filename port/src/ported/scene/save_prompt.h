#pragma once

// The save prompt: what opcode 0xE1 puts up, and the two game modes it runs in.
//
//   src/FUN_00265000.c  opcode 0xE1: raise flag 0x8EE, mode 0x10, lead to state
//                       10, park the 0x37 party members
//   0x00224BA8          PTR_FUN_00318A88[0x10] (and [0xF]), the prompt
//   0x00224E68          PTR_FUN_00318A88[0x11], the wait after Yes
//   src/FUN_00234468.c  the save screen Yes hands off to, as mode 8
//
// Neither handler is in the decompiled sources -- Ghidra has no function at
// either entry -- so both were read out of SLUS_200.11.
//
// == Mode 0x10 ==
//
// Flag 0x8EE is the open latch: 0xE1 raises it, and the handler's first frame
// clears it and resets the cursor to 0 (Yes) and both answers' alpha bytes to
// 0x20. Then, every frame:
//
//   message 0x21  centred at y 0x21, cell 0x1E x 0x21
//   0x4C / 0x4D   Yes / No side by side at y 0, 0x20 apart, the pair centred
//                 as one run, each at its own ramped alpha
//   FUN_002318C0  both alphas ramped towards the cursor -- *after* the answers
//                 were drawn, so the draw shows last frame's alpha
//   message 0x28  centred at y -0xA0, cell 0x14 x 0x16
//
// Then input, one branch:
//
//   FUN_0023B9F8(0xA000, 1)  Left/Right flips the cursor, cue 1
//   Cross on No              cue 3, FUN_002241D8 (mode 0), FUN_00252D88(lead)
//   Cross on Yes             cue 2, remember the mode, 0xA0 ticks, mode 0x11
//
// There is no Triangle. The tail is FUN_0025D0E0(0x50000000, 1) -- black at
// alpha 0x50 -- and then FUN_00224218, the mode-0 handler: FUN_002261E0 and the
// draw half of the field frame plus the effect pools. No script tick, no player
// controller, no actor loop. The closing frame still takes the tail.
//
// Mode 0xF shares the handler and swaps the tail for FUN_0025D238, with a
// FUN_0025D380(8) on No; FUN_0022A418 raises it at scene load. Not ported.
//
// == Mode 0x11 ==
//
// Counts the 0xA0 down by frameTicks with the same dim-and-field tail. Once it
// is spent it waits for the lead's +0x0C bit 0 (grounded), running an extra
// FUN_002261E0 each frame it is not, and then puts the remembered mode back,
// calls FUN_00234468(1) and leaves mode 8 -- the memory-card save screen.

#include "ported/scene/field_menu.h"
#include "ported/text/original_dialogue_text.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orphen::ported::scene
{

  inline constexpr int kGameModeSavePrompt = 0x10;
  inline constexpr int kGameModeSaveWait = 0x11;
  inline constexpr int kGameModeSaveScreen = 8;

  // FUN_00265000:9 raises it; 0x00224BD0 tests it and 0x00224BE0 clears it.
  inline constexpr std::uint32_t kSavePromptOpenFlag = 0x8EE;

  inline constexpr int kSavePromptTitleMessage = 0x21;
  inline constexpr int kSavePromptYesMessage = 0x4C;
  inline constexpr int kSavePromptNoMessage = 0x4D;
  inline constexpr int kSavePromptHintMessage = 0x28;

  // 0x00224E18, `lui a0,0x5000` into FUN_0025D0E0: black at alpha 0x50.
  inline constexpr std::uint32_t kSavePromptDimColour = 0x000000;
  inline constexpr std::uint8_t kSavePromptDimAlpha = 0x50;

  struct SavePromptText
  {
    std::string title; // 0x21
    std::string yes;   // 0x4C
    std::string no;    // 0x4D
    std::string hint;  // 0x28
  };

  struct SavePromptStep
  {
    // FUN_00267D38 cue, or -1.
    int cue = -1;
    // Cross on No: FUN_002241D8 then FUN_00252D88 on the lead.
    bool closed = false;
    // Mode 0x11 finished: FUN_00234468(1), mode 8.
    bool openSaveScreen = false;
    // Mode 0x11 is waiting on the lead to land: one more FUN_002261E0.
    bool extraPhysics = false;
  };

  class SavePrompt
  {
  public:
    // 0x00224BA8 for mode 0x10. `openLatch` is flag 0x8EE, which the caller
    // clears when this returns having consumed it. `autoRepeat` is the field
    // menu's FUN_0023B9F8, whose budget and step counter the original shares.
    SavePromptStep FUN_00224ba8_prompt(bool openLatch,
                                       FieldMenu &autoRepeat,
                                       FieldMenuPad &pad,
                                       std::uint32_t frameTicks);

    // 0x00224E68 for mode 0x11. `leadGrounded` is lead +0x0C bit 0.
    SavePromptStep FUN_00224e68_wait(bool leadGrounded, std::uint32_t frameTicks);

    // The mode the handler leaves in DAT_00354D2C.
    int DAT_00354d2c_mode() const { return mode_; }
    void raise() { mode_ = kGameModeSavePrompt; }
    // Leaves drawn_ alone: the frame No closes on still drew the prompt.
    void clear() { mode_ = 0; }
    bool active() const { return mode_ == kGameModeSavePrompt || mode_ == kGameModeSaveWait; }

    // The prompt's draw, for the frame its handler last ran.
    bool drawn() const { return drawn_; }
    void clearDrawn() { drawn_ = false; }
    std::vector<orphen::ported::text::DialogueSprite> layout(
        const SavePromptText &text,
        const orphen::ported::text::DialogueFont &font) const;

  private:
    int mode_ = 0;
    bool drawn_ = false;
    // DAT_0031C458: the mode Yes was chosen from, which mode 0x11 restores.
    int DAT_0031c458_savedMode_ = 0;
    // DAT_0031C46C: the cursor in mode 0x10, the countdown in mode 0x11.
    std::int32_t DAT_0031c46c_cursorOrTimer_ = 0;
    // DAT_0031C463 / DAT_0031C47B, the alpha bytes of the Yes and No colour
    // words, and the values they held when this frame drew them.
    int DAT_0031c463_yesAlpha_ = 0x20;
    int DAT_0031c47b_noAlpha_ = 0x20;
    int drawnYesAlpha_ = 0x20;
    int drawnNoAlpha_ = 0x20;
  };

} // namespace orphen::ported::scene
