#pragma once

// The battle pause: Start in a section-14 scene, game modes 1 then 2.
//
//   0x00224268          PTR_FUN_00318A88[1], not in src/: FUN_00206CE0, then
//                       the two-line panel at 0x005609C0 is set up, the mode
//                       goes to 2, and it jumps to FUN_00224218
//   src/FUN_00224320.c  mode 2, which calls FUN_00225340 in place of the
//                       field pause's caption when DAT_003555D3 is set
//   src/FUN_00225340.c  the panel: navigate, draw, act
//   src/FUN_0022ead0.c  "Change Equipment": straight into the Equip screen
//
// == The panel ==
//
// 0x005609C0 is twelve bytes, cleared by 0x00224268 and then written:
//
//   +0x00  u16  message for line 0
//   +0x02  u16  message for line 1, always line 0's plus one
//   +0x08  u8   line 0's alpha, starting 0x20
//   +0x09  u8   line 1's alpha, starting 0x20
//   +0x0A  u16  the cursor, 0 or 1
//
// Line 0's message is 0x5B in battle entries 0x39..0x3F ("Return to Battle
// Training" / "Quit Battle Training"), 0x57 with the debug byte up ("Return to
// Game" / "Return to Title Screen") and otherwise 0x59 ("Return to Battle" /
// "Change Equipment and Return to Battle").
//
// FUN_00225340 each frame: Up or Down through FUN_0023B9F8's auto-repeat flips
// the cursor with cue 1. Then, if none of Start, Triangle and Cross is newly
// pressed, it ramps the two alphas with FUN_002318C0 (0x80 selected, 0x20 not)
// and draws the two lines centred at y 0 and -0x16 and message 0x29 at -0xA0,
// all in 0x14 x 0x16 cells. Otherwise cue 2 and **nothing is drawn**, and:
//
//   line 0, or Triangle   resume: uGpffffb686 is forced to 0x800 and
//                         FUN_00224FF0 runs -- which, guards permitting, is
//                         the Start resume's music restore -- then
//                         FUN_002241D8 and DAT_00354E96 = 0
//   line 1                FUN_002241D8 and the bars' mode to 0; then in
//                         0x39..0x3F a title load, otherwise FUN_0022EAD0
//
// The resume here does not run FUN_00224218 the way the Start resume does:
// FUN_00225340 returns into FUN_00224320's draw tail.

#include "ported/scene/field_menu.h"
#include "ported/text/original_dialogue_text.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orphen::ported::scene
{

  // 0x00224268:12-29.
  inline constexpr int kBattlePauseMessageBattle = 0x59;
  inline constexpr int kBattlePauseMessageTraining = 0x5B;
  inline constexpr int kBattlePauseMessageDebug = 0x57;
  inline constexpr int kBattlePauseTrainingFirstEntry = 0x39;
  inline constexpr int kBattlePauseTrainingEntryCount = 7;
  inline constexpr int kBattlePauseAlphaStart = 0x20;

  // FUN_00225340.
  inline constexpr std::uint16_t kBattlePauseNavigateMask = 0x5000;
  inline constexpr std::uint16_t kBattlePauseActMask = 0x0850;
  inline constexpr std::uint16_t kBattlePausePadTriangle = 0x0010;
  inline constexpr int kBattlePauseCueMove = 1; // FUN_002256C0
  inline constexpr int kBattlePauseCueAct = 2;  // FUN_002256B0
  inline constexpr int kBattlePauseCaptionMessage = 0x29;
  inline constexpr int kBattlePauseLine1Y = -0x16;
  inline constexpr int kBattlePauseCaptionY = -0xA0;
  inline constexpr int kBattlePauseCellWidth = 0x14;
  inline constexpr int kBattlePauseCellHeight = 0x16;

  // LAB_00225414: the title scene, reached with the doorway bits set.
  inline constexpr std::uint32_t kBattlePauseQuitRequest = 0x2003;

  inline bool battlePauseTrainingEntry(int groupEntry)
  {
    return static_cast<unsigned>(groupEntry - kBattlePauseTrainingFirstEntry) <
           static_cast<unsigned>(kBattlePauseTrainingEntryCount);
  }

  struct BattlePauseStep
  {
    int cue = -1;
    enum class Action
    {
      None,
      Resume,          // line 0 or Triangle
      ChangeEquipment, // line 1 in an ordinary battle: FUN_0022EAD0
      QuitToTitle,     // line 1 in battle training, or with the debug byte up
      Close,           // line 1 outside a battle with the debug byte down
    };
    Action action = Action::None;
    // QuitToTitle by way of the debug byte also clears event flag 0x511.
    bool clearReturnedFlag = false;
  };

  struct BattlePauseText
  {
    std::string line0;
    std::string line1;
    std::string caption; // message 0x29
  };

  class BattlePause
  {
  public:
    // 0x00224268:7-37, the panel's setup.
    void FUN_00224268_open(bool DAT_003555d3_battle, int DAT_003551f8_groupEntry, bool DAT_003555c7_debug);

    // FUN_00225340. `repeat` owns FUN_0023B9F8's budget, which the original
    // keeps in globals shared with the field menu.
    BattlePauseStep FUN_00225340_step(FieldMenuPad pad, std::uint32_t frameTicks, FieldMenu &repeat);

    std::vector<orphen::ported::text::DialogueSprite> layout(const BattlePauseText &text,
                                                             const orphen::ported::text::DialogueFont &font) const;

    int DAT_005609c0_line0Message() const { return DAT_005609c0_line0Message_; }
    int DAT_005609c2_line1Message() const { return DAT_005609c2_line1Message_; }
    int DAT_005609ca_cursor() const { return DAT_005609ca_cursor_; }
    // Whether the last step drew; a step that acts does not.
    bool drawn() const { return drawn_; }
    void clearDrawn() { drawn_ = false; }

  private:
    int DAT_005609c0_line0Message_ = 0;
    int DAT_005609c2_line1Message_ = 0;
    int DAT_005609c8_line0Alpha_ = kBattlePauseAlphaStart;
    int DAT_005609c9_line1Alpha_ = kBattlePauseAlphaStart;
    int DAT_005609ca_cursor_ = 0;
    bool drawn_ = false;
  };

} // namespace orphen::ported::scene
