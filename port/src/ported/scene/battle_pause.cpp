#include "ported/scene/battle_pause.h"

namespace orphen::ported::scene
{

  namespace text = orphen::ported::text;

  // 0x00224268:7-37. The twelve bytes are cleared first, so the cursor and
  // both alphas start fresh every time the panel opens.
  void BattlePause::FUN_00224268_open(bool DAT_003555d3_battle, int DAT_003551f8_groupEntry,
                                      bool DAT_003555c7_debug)
  {
    // :12-29. Outside a battle with the debug byte down the message stays 0;
    // FUN_0026BFC0 complains about it, and the gate never gets here like that.
    int line0 = 0;
    if (DAT_003555d3_battle && battlePauseTrainingEntry(DAT_003551f8_groupEntry))
    {
      line0 = kBattlePauseMessageTraining;
    }
    else if (DAT_003555d3_battle)
    {
      line0 = DAT_003555c7_debug ? kBattlePauseMessageDebug : kBattlePauseMessageBattle;
    }
    else if (DAT_003555c7_debug)
    {
      line0 = kBattlePauseMessageDebug;
    }
    DAT_005609c0_line0Message_ = line0;
    DAT_005609c2_line1Message_ = line0 + 1;
    DAT_005609c8_line0Alpha_ = kBattlePauseAlphaStart;
    DAT_005609c9_line1Alpha_ = kBattlePauseAlphaStart;
    DAT_005609ca_cursor_ = 0;
    drawn_ = false;
  }

  BattlePauseStep BattlePause::FUN_00225340_step(FieldMenuPad pad, std::uint32_t frameTicks, FieldMenu &repeat)
  {
    BattlePauseStep result;
    drawn_ = false;

    // :12-15. Either direction flips it.
    if (repeat.FUN_0023b9f8_autoRepeat(kBattlePauseNavigateMask, pad, frameTicks))
    {
      DAT_005609ca_cursor_ = (DAT_005609ca_cursor_ + 1) & 1;
      result.cue = kBattlePauseCueMove;
    }

    // :16-24, the draw half, taken only on a frame with no act press.
    if ((pad.uGpffffb686_pressed & kBattlePauseActMask) == 0)
    {
      FieldMenu::FUN_002318c0_ramp(DAT_005609ca_cursor_, 0, DAT_005609c8_line0Alpha_, frameTicks);
      FieldMenu::FUN_002318c0_ramp(DAT_005609ca_cursor_, 1, DAT_005609c9_line1Alpha_, frameTicks);
      drawn_ = true;
      return result;
    }

    // :26. Over the move cue, if both landed on one frame: FUN_00267D38 is
    // called for each, and the port plays only one cue per step.
    result.cue = kBattlePauseCueAct;
    if (DAT_005609ca_cursor_ == 0 || (pad.uGpffffb686_pressed & kBattlePausePadTriangle) != 0)
    {
      result.action = BattlePauseStep::Action::Resume;
      return result;
    }
    // :38-63, line 1. Which branch it takes follows from the message
    // 0x00224268 picked, since both test the same three things.
    switch (DAT_005609c0_line0Message_)
    {
    case kBattlePauseMessageBattle:
      result.action = BattlePauseStep::Action::ChangeEquipment;
      break;
    case kBattlePauseMessageTraining:
      result.action = BattlePauseStep::Action::QuitToTitle;
      break;
    case kBattlePauseMessageDebug:
      result.action = BattlePauseStep::Action::QuitToTitle;
      result.clearReturnedFlag = true;
      break;
    default:
      result.action = BattlePauseStep::Action::Close;
      break;
    }
    return result;
  }

  std::vector<text::DialogueSprite> BattlePause::layout(const BattlePauseText &strings,
                                                        const text::DialogueFont &font) const
  {
    constexpr std::uint32_t kRgb = 0x00808080;
    std::vector<std::vector<text::DialogueSprite>> groups;
    const auto centred = [&](const std::string &line, int y, std::uint32_t colour) {
      if (line.empty())
      {
        return;
      }
      const int width = text::FUN_00238e68_measure(line, font, kBattlePauseCellWidth);
      groups.push_back(text::FUN_00238608_layout(-width / 2, y, line, colour, kBattlePauseCellWidth,
                                                 kBattlePauseCellHeight, font));
    };
    centred(strings.line0, 0, kRgb | (static_cast<std::uint32_t>(DAT_005609c8_line0Alpha_) << 24));
    centred(strings.line1, kBattlePauseLine1Y,
            kRgb | (static_cast<std::uint32_t>(DAT_005609c9_line1Alpha_) << 24));
    centred(strings.caption, kBattlePauseCaptionY, text::kColorDefault);

    // Submitted in that order and drawn last-in first, as FUN_00231C50's are.
    std::vector<text::DialogueSprite> sprites;
    for (auto group = groups.rbegin(); group != groups.rend(); ++group)
    {
      sprites.insert(sprites.end(), group->begin(), group->end());
    }
    return sprites;
  }

} // namespace orphen::ported::scene
