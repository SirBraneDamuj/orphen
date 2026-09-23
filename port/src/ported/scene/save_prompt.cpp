#include "ported/scene/save_prompt.h"

namespace orphen::ported::scene
{
  namespace
  {
    namespace text = orphen::ported::text;

    constexpr std::uint16_t kPadCross = 0x0040;
    // 0x00224D5C, `ori a0,zero,0xa000`: Left or Right.
    constexpr std::uint16_t kToggleMask = 0xA000;

    constexpr int kCursorYes = 0;
    constexpr int kCursorNo = 1;

    // 0x00224BE8-0x00224C08: every colour word reset to 0x20808080.
    constexpr int kAlphaIdle = 0x20;
    constexpr std::uint32_t kAnswerRgb = 0x00808080;

    // 0x00224DFC-0x00224E04: Yes arms the mode-0x11 wait with 0xA0 ticks.
    constexpr std::int32_t kSaveWaitTicks = 0xA0;

    // FUN_002256C0 / FUN_002256B0 / FUN_002256A0, each `FUN_00267D38(n, 0)`.
    constexpr int kCueMove = 1;
    constexpr int kCueYes = 2;
    constexpr int kCueNo = 3;

    // The title and the answers: `t0 = 0x1E, t1 = 0x21`.
    constexpr int kLargeCellWidth = 0x1E;
    constexpr int kLargeCellHeight = 0x21;
    constexpr int kTitleY = 0x21;
    constexpr int kAnswerY = 0;
    // 0x00224C90 / 0x00224CD0: the gap between Yes and No.
    constexpr int kAnswerGap = 0x20;
    // The hint: `a1 = -0xA0, t0 = 0x14, t1 = 0x16`.
    constexpr int kSmallCellWidth = 0x14;
    constexpr int kSmallCellHeight = 0x16;
    constexpr int kHintY = -0xA0;
  } // namespace

  SavePromptStep SavePrompt::FUN_00224ba8_prompt(bool openLatch,
                                                 FieldMenu &autoRepeat,
                                                 FieldMenuPad &pad,
                                                 std::uint32_t frameTicks)
  {
    SavePromptStep result;

    // 0x00224BD0-0x00224C08. The latch is the open: cursor on Yes, both
    // answers dim. DAT_0031C458 is zeroed too, and only Yes writes it again.
    if (openLatch)
    {
      DAT_0031c46c_cursorOrTimer_ = kCursorYes;
      DAT_0031c458_savedMode_ = 0;
      DAT_0031c463_yesAlpha_ = kAlphaIdle;
      DAT_0031c47b_noAlpha_ = kAlphaIdle;
    }

    // 0x00224C4C / 0x00224CBC / 0x00224CE8 draw the answers at the alphas they
    // hold now; 0x00224CFC / 0x00224D10 ramp them afterwards.
    drawnYesAlpha_ = DAT_0031c463_yesAlpha_;
    drawnNoAlpha_ = DAT_0031c47b_noAlpha_;
    drawn_ = true;
    FieldMenu::FUN_002318c0_ramp(DAT_0031c46c_cursorOrTimer_, kCursorYes, DAT_0031c463_yesAlpha_,
                                 frameTicks);
    FieldMenu::FUN_002318c0_ramp(DAT_0031c46c_cursorOrTimer_, kCursorNo, DAT_0031c47b_noAlpha_,
                                 frameTicks);

    int nextMode = mode_;
    if (autoRepeat.FUN_0023b9f8_autoRepeat(kToggleMask, pad, frameTicks))
    {
      // 0x00224D6C-0x00224D7C, `1 - cursor`.
      DAT_0031c46c_cursorOrTimer_ = 1 - DAT_0031c46c_cursorOrTimer_;
      result.cue = kCueMove;
    }
    else if ((pad.uGpffffb686_pressed & kPadCross) != 0)
    {
      if (DAT_0031c46c_cursorOrTimer_ != kCursorYes)
      {
        // 0x00224DA4-0x00224DE8.
        result.cue = kCueNo;
        result.closed = true;
        nextMode = 0;
      }
      else
      {
        // 0x00224DF4-0x00224E08.
        result.cue = kCueYes;
        nextMode = kGameModeSaveWait;
        DAT_0031c46c_cursorOrTimer_ = kSaveWaitTicks;
        DAT_0031c458_savedMode_ = mode_;
      }
    }
    mode_ = nextMode;
    return result;
  }

  SavePromptStep SavePrompt::FUN_00224e68_wait(bool leadGrounded, std::uint32_t frameTicks)
  {
    SavePromptStep result;
    // The wait draws nothing of its own; only the dim and the field.
    drawn_ = false;

    DAT_0031c46c_cursorOrTimer_ -= static_cast<std::int32_t>(frameTicks);
    if (DAT_0031c46c_cursorOrTimer_ > 0)
    {
      return result;
    }
    if (leadGrounded)
    {
      // 0x00224EA8-0x00224EBC: the remembered mode goes back for the length of
      // FUN_00234468(1), and then the frame leaves in mode 8.
      result.openSaveScreen = true;
      mode_ = kGameModeSaveScreen;
    }
    else
    {
      // 0x00224EC0.
      result.extraPhysics = true;
    }
    return result;
  }

  std::vector<text::DialogueSprite> SavePrompt::layout(const SavePromptText &strings,
                                                       const text::DialogueFont &font) const
  {
    std::vector<std::vector<text::DialogueSprite>> groups;

    // 0x00224C10-0x00224C4C. `negu; srl 31; addu; sra 1` is -w / 2 rounded
    // toward zero, which is what C division gives.
    const int titleWidth = text::FUN_00238e68_measure(strings.title, font, kLargeCellWidth);
    groups.push_back(text::FUN_00238608_layout(-titleWidth / 2, kTitleY, strings.title,
                                               text::kColorDefault, kLargeCellWidth,
                                               kLargeCellHeight, font));

    // 0x00224C54-0x00224CEC. The run is truncated to 16 bits before it is
    // negated and halved.
    const int yesWidth = text::FUN_00238e68_measure(strings.yes, font, kLargeCellWidth);
    const int noWidth = text::FUN_00238e68_measure(strings.no, font, kLargeCellWidth);
    const int run = static_cast<std::int16_t>(yesWidth + noWidth + kAnswerGap);
    const int left = -run / 2;
    groups.push_back(text::FUN_00238608_layout(
        left, kAnswerY, strings.yes, static_cast<std::uint32_t>(drawnYesAlpha_) << 24 | kAnswerRgb,
        kLargeCellWidth, kLargeCellHeight, font));
    groups.push_back(text::FUN_00238608_layout(
        left + kAnswerGap + yesWidth, kAnswerY, strings.no,
        static_cast<std::uint32_t>(drawnNoAlpha_) << 24 | kAnswerRgb, kLargeCellWidth,
        kLargeCellHeight, font));

    // 0x00224D18-0x00224D54.
    const int hintWidth = text::FUN_00238e68_measure(strings.hint, font, kSmallCellWidth);
    groups.push_back(text::FUN_00238608_layout(-hintWidth / 2, kHintY, strings.hint,
                                               text::kColorDefault, kSmallCellWidth,
                                               kSmallCellHeight, font));

    // FUN_00207938's list is head-first, as the field menu's is.
    std::vector<text::DialogueSprite> sprites;
    for (auto group = groups.rbegin(); group != groups.rend(); ++group)
    {
      sprites.insert(sprites.end(), group->begin(), group->end());
    }
    return sprites;
  }

} // namespace orphen::ported::scene
