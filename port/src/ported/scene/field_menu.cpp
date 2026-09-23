#include "ported/scene/field_menu.h"

#include "ported/scene/equip_screen.h"

#include <algorithm>

namespace orphen::ported::scene
{
  namespace
  {
    namespace text = orphen::ported::text;

    // The pad bits FUN_00231958 and 0x00224570 test, in the post-CONCAT11 order
    // the port keeps everywhere.
    constexpr std::uint16_t kPadTriangle = 0x0010;
    constexpr std::uint16_t kPadCross = 0x0040;
    constexpr std::uint16_t kPadUp = 0x1000;
    constexpr std::uint16_t kPadDown = 0x4000;
    // FUN_00231958:19, the mask handed to the auto-repeat: all four directions,
    // so Left and Right make the move sound without moving anything.
    constexpr std::uint16_t kNavigationMask = 0xF000;
    // 0x00224570:3, `andi v0,v0,0xf060`: the four directions plus Circle and
    // Cross. Mode 4 holds until every one of them is up.
    constexpr std::uint16_t kSwallowMask = 0xF060;

    // FUN_0023B9F8:8 and :12.
    constexpr std::int32_t kRepeatBudgetCap = 0x200;
    constexpr std::int32_t kRepeatStepTicks = 0x20;

    // FUN_00231A98:35, and the 0x1E it steps by at :43.
    constexpr int kFirstItemY = 0x70;
    constexpr int kItemStep = 0x1E;
    // FUN_00231A98:48, the pad either side of the widest label.
    constexpr int kBarPadding = 0x20;
    // FUN_00231C50:30's `FUN_00231C30(-w/2, y + 2, w, 0x1A)`.
    constexpr int kBarYBias = 2;
    constexpr int kBarHeight = 0x1A;
    // The cell FUN_00231C50 measures and draws every label at.
    constexpr int kCellWidth = 0x14;
    constexpr int kCellHeight = 0x16;
    // FUN_00231C50:37-44, the caption's offset from item 0 and its right edge.
    constexpr int kCaptionYBias = 0x26;
    constexpr int kCaptionRightEdge = 0x120;

    // FUN_00232FA8's two states, and the geometry of :21-56.
    constexpr int kConfirmYes = 1;
    constexpr int kConfirmNo = 2;
    // DAT_0031C510, written 0x14 on open and never changed: the question's y.
    constexpr int kConfirmQuestionY = 0x14;
    // :26, the caption's y.
    constexpr int kConfirmCaptionY = -0x40;
    // :49, the answers sit 0x16 under the question.
    constexpr int kConfirmAnswerDrop = 0x16;
    // :53-56, the gap between Yes and No.
    constexpr int kConfirmAnswerGap = 0x10;
    // :30, `FUN_0023B9F8(0xA000, 1)`: Left or Right toggles.
    constexpr std::uint16_t kConfirmToggleMask = 0xA000;

    // FUN_00231A98:38-41. The alpha byte is what FUN_002318C0 walks; these are
    // the other three channels it leaves alone.
    constexpr std::uint32_t kAvailableRgb = 0x00808080;
    constexpr std::uint32_t kUnavailableRgb = 0x00404040;
    // FUN_00231C50:25's `0x2080`: low byte is the selected item's target alpha,
    // high byte every other item's.
    constexpr int kAlphaSelected = 0x80;
    constexpr int kAlphaIdle = 0x20;

    // The FUN_00239020 entry at 0x0031C388, read out of SLUS_200.11. Texture
    // word 0x62C is slot 0x2C -- the button-icon sheet FUN_00221FD8 binds at
    // boot -- through CLUT bank 6, and +0x2C is 0 where every glyph entry
    // carries 1, so the bar draws with blending off.
    constexpr int kBarTextureWord = 0x62C;
    constexpr int kBarU = 0x80;
    constexpr int kBarV = 0xEC;
    constexpr int kBarSourceWidth = 0x80;
    constexpr int kBarSourceHeight = 0x14;
    constexpr std::uint32_t kBarColour = 0x80808080;
    constexpr int kBarBlendMode = 0;

    // FUN_00207938's screen mapping, the same one original_dialogue_text.cpp
    // keeps privately.
    constexpr int kScreenHalfWidth = 320;
    constexpr int kScreenHalfHeight = 224;
    int screenX(int entryX) { return entryX + kScreenHalfWidth; }
    int screenY(int entryY) { return kScreenHalfHeight - entryY; }
  } // namespace

  void FieldMenu::FUN_00231a98_open(const std::array<std::string, kFieldMenuItemCount> &labels,
                                    std::uint16_t uGpffffbcc0_availability,
                                    const text::DialogueFont &font)
  {
    labels_ = labels;
    uGpffffbcc0_availability_ = uGpffffbcc0_availability;
    // FUN_00231A98:16. The selection is reset by DAT_0031C458 = 0 at the top of
    // the open, not carried over from the last time the panel was up.
    uGpffffae34_selected_ = 0;
    // :50.
    iGpffffbcbc_submenuState_ = 0;

    // :32-47. One pass over the seven labels: the widest measured at the 0x14
    // cell sets the bar width, and every item's colour word starts at the idle
    // alpha whether or not it is the selected one.
    int widest = 0;
    for (std::size_t index = 0; index < labels_.size(); ++index)
    {
      widest = std::max(widest, text::FUN_00238e68_measure(labels_[index], font, kCellWidth));
      itemAlpha_[index] = kAlphaIdle;
    }
    DAT_0031c45c_barWidth_ = widest + kBarPadding;

    iGpffffaf00_repeatBudget_ = 0;
    sGpffffaefe_repeatSteps_ = 0;
    // FUN_00224FF0:96 sets the mode before it calls FUN_00231A98; the panel
    // owns the word from here.
    DAT_00354d2c_mode_ = kFieldMenuModeSwallow;
  }

  // FUN_002318C0(param_1 = selected, param_2 = index, param_3 = &alpha byte,
  // param_4 = 0x2080), at 0x002318c0. The step is `frameTicks / 8` -- the
  // original's `(iGpffffb64c << 13) >> 16` with the usual round-toward-zero
  // fixup ahead of it -- so a nominal 0x20 tick walks the byte by 4 and the
  // 0x20 -> 0x80 fade in takes 24 frames.
  void FieldMenu::FUN_002318c0_ramp(int selected, int index, int &alpha, std::uint32_t frameTicks)
  {
    const int step = static_cast<int>(frameTicks) / 8;
    const int target = index == selected ? kAlphaSelected : kAlphaIdle;
    if (alpha < target)
    {
      alpha = std::min(target, alpha + step);
    }
    else if (alpha > target)
    {
      alpha = std::max(target, alpha - step);
    }
  }

  // FUN_0023B9F8(param_1 = mask, param_2 = 1), at 0x0023b9f8 -- the same helper
  // DialogueWindow carries its own copy of. The mid-frame `FUN_0023B5D8(0)`
  // re-read is dropped because the port has already sampled this frame's pad,
  // and the OR of the stick's direction bits into the held word is the
  // original's own write into the global, made here on the caller's copy.
  bool FieldMenu::FUN_0023b9f8_autoRepeat(std::uint16_t mask,
                                          FieldMenuPad &pad,
                                          std::uint32_t frameTicks)
  {
    iGpffffaf00_repeatBudget_ += static_cast<std::int32_t>(frameTicks);
    if (iGpffffaf00_repeatBudget_ > kRepeatBudgetCap)
    {
      iGpffffaf00_repeatBudget_ = kRepeatBudgetCap;
    }
    if (iGpffffaf00_repeatBudget_ < kRepeatStepTicks)
    {
      return false;
    }

    pad.uGpffffb684_held =
        static_cast<std::uint16_t>(pad.uGpffffb684_held | (pad.uGpffffb68e_stickDirection & mask));

    if ((pad.uGpffffb684_held & mask) == 0)
    {
      sGpffffaefe_repeatSteps_ = 0;
      return false;
    }

    // :24-36. Step 1 fires, then nothing until step 13, and every fourth after
    // that.
    while (iGpffffaf00_repeatBudget_ > 0)
    {
      ++sGpffffaefe_repeatSteps_;
      const std::int32_t stepIndex = sGpffffaefe_repeatSteps_;
      if (stepIndex == 1 || (stepIndex > 0x0C && ((stepIndex - 0x0D) & 3) == 0))
      {
        iGpffffaf00_repeatBudget_ = 0;
        return true;
      }
      iGpffffaf00_repeatBudget_ -= kRepeatStepTicks;
    }
    return false;
  }

  // FUN_00232FA8(param_1 = state), at 0x00232fa8 -- PTR_FUN_0031C3C0[4].
  int FieldMenu::FUN_00232fa8_returnToTitle(int state,
                                            FieldMenuPad &pad,
                                            std::uint32_t frameTicks,
                                            FieldMenuStep &result)
  {
    // :12-17. The open: both answers start dim, the cursor starts on No, and
    // nothing is drawn this frame.
    if (state == 0)
    {
      DAT_0031c508_yesAlpha_ = kAlphaIdle;
      DAT_0031c509_noAlpha_ = kAlphaIdle;
      return kConfirmNo;
    }
    // `param_1 - 1U < 2`. Nothing else is ever handed in.
    if (state != kConfirmYes && state != kConfirmNo)
    {
      return state;
    }

    if ((pad.uGpffffb686_pressed & kPadCross) != 0)
    {
      // :43-50. Either answer closes the menu; only Yes goes on.
      result.cue = kFieldMenuCueConfirm;
      DAT_00354d2c_mode_ = 0;
      result.closed = true;
      if (state == kConfirmYes)
      {
        // `uGpffffae34 = 0; FUN_00237A08();`
        uGpffffae34_selected_ = 0;
        result.returnToTitle = true;
      }
    }
    else if ((pad.uGpffffb686_pressed & kPadTriangle) != 0)
    {
      // :39-41.
      result.cue = kFieldMenuCueCancel;
      DAT_00354d2c_mode_ = 0;
      result.closed = true;
    }
    else if (FUN_0023b9f8_autoRepeat(kConfirmToggleMask, pad, frameTicks))
    {
      // :30-36. Either direction flips it.
      state = state == kConfirmYes ? kConfirmNo : kConfirmYes;
      result.cue = kFieldMenuCueMove;
    }

    // :52-53, then the draw -- which happens on the closing frame too.
    FUN_002318c0_ramp(state, kConfirmYes, DAT_0031c508_yesAlpha_, frameTicks);
    FUN_002318c0_ramp(state, kConfirmNo, DAT_0031c509_noAlpha_, frameTicks);
    returnToTitleDrawn_ = true;
    result.dimScreen = true;
    return state;
  }

  FieldMenuStep FieldMenu::step(const FieldMenuPad &padIn, std::uint32_t frameTicks)
  {
    FieldMenuStep result;
    panelDrawn_ = false;
    returnToTitleDrawn_ = false;
    if (DAT_00354d2c_mode_ == kFieldMenuModeSwallow)
    {
      // 0x00224570:1-6. The press that opened the panel is still down, so this
      // is what stops it walking the selection on the first frame.
      if ((padIn.uGpffffb684_held & kSwallowMask) == 0)
      {
        DAT_00354d2c_mode_ = kFieldMenuModeActive;
      }
    }
    else if (DAT_00354d2c_mode_ == kFieldMenuModeActive)
    {
      FieldMenuPad pad = padIn;
      if (iGpffffbcbc_submenuState_ != 0)
      {
        // FUN_00231958:40, `iGpffffbcbc = PTR_FUN_0031C3C0[selected]()`. Only
        // item 4 can get here: the unported six leave the word at zero.
        iGpffffbcbc_submenuState_ =
            FUN_00232fa8_returnToTitle(iGpffffbcbc_submenuState_, pad, frameTicks, result);
      }
      // FUN_00231958:5-10. Triangle closes, and the function **returns before
      // the draw** -- the panel's last frame is the one before this.
      else if ((pad.uGpffffb686_pressed & kPadTriangle) != 0)
      {
        DAT_00354d2c_mode_ = 0;
        result.closed = true;
        result.cue = kFieldMenuCueBack;
        return result;
      }

      else if ((pad.uGpffffb686_pressed & kPadCross) == 0)
      {
        if (FUN_0023b9f8_autoRepeat(kNavigationMask, pad, frameTicks))
        {
          // :22-31. Up wins over Down when both are somehow down, and the wrap
          // is at both ends. The cue plays for any of the four directions,
          // which is why Left and Right click without moving.
          if ((pad.uGpffffb684_held & kPadUp) != 0)
          {
            if (--uGpffffae34_selected_ < 0)
            {
              uGpffffae34_selected_ = kFieldMenuItemCount - 1;
            }
          }
          else if ((pad.uGpffffb684_held & kPadDown) != 0)
          {
            if (++uGpffffae34_selected_ > kFieldMenuItemCount - 1)
            {
              uGpffffae34_selected_ = 0;
            }
          }
          result.cue = kFieldMenuCueMove;
        }
      }
      else if (available(uGpffffae34_selected_))
      {
        // :35-37, `iGpffffbcbc = PTR_FUN_0031C3C0[selected](0)`, then the
        // confirm cue. An unavailable item makes no sound at all -- the whole
        // branch is inside the availability test.
        if (uGpffffae34_selected_ == kFieldMenuReturnToTitleItem)
        {
          iGpffffbcbc_submenuState_ = FUN_00232fa8_returnToTitle(0, pad, frameTicks, result);
        }
        else if (uGpffffae34_selected_ == kFieldMenuEquipItem ||
                 uGpffffae34_selected_ == kFieldMenuItemItem)
        {
          // 0x00233240 / 0x00233250, PTR_FUN_0031C3C0[5] and [6]: both are
          // `iGpffffadbc = 3; return 0` -- the Item and Equip screens are one
          // game mode, told apart later by uGpffffae34. The submenu word stays
          // 0, so the panel still draws this frame.
          DAT_00354d2c_mode_ = kGameModeEquipScreen;
          result.equipScreen = true;
        }
        else
        {
          // The other five submenus are not ported, so the selection is handed
          // back to the caller and the panel stays where it is.
          result.confirmed = uGpffffae34_selected_;
        }
        result.cue = kFieldMenuCueConfirm;
      }
    }

    // FUN_00231958:43, `(iGpffffbcbc == 0 || selected != 4) && iGpffffb27c ==
    // 0`, and mode 4 draws unconditionally. The scene-request half never
    // decides anything here: the gate will not open the menu with a request
    // standing, and the one submenu that raises one closes the menu first.
    const bool drawPanel = DAT_00354d2c_mode_ == kFieldMenuModeSwallow ||
                           ((DAT_00354d2c_mode_ == kFieldMenuModeActive || result.equipScreen) &&
                            (iGpffffbcbc_submenuState_ == 0 ||
                             uGpffffae34_selected_ != kFieldMenuReturnToTitleItem));
    if (drawPanel)
    {
      // FUN_00231C50:25, once per item on every frame the panel draws.
      for (int index = 0; index < kFieldMenuItemCount; ++index)
      {
        FUN_002318c0_ramp(uGpffffae34_selected_, index,
                          itemAlpha_[static_cast<std::size_t>(index)], frameTicks);
      }
      panelDrawn_ = true;
    }
    return result;
  }

  std::vector<text::DialogueSprite> FieldMenu::FUN_00231c50_layout(
      const std::string &caption, const text::DialogueFont &font) const
  {
    // Built in the order FUN_00231C50 submits -- label then bar, top item
    // first, caption last -- and then reversed, because FUN_00207938 pushes
    // each entry onto the head of its bucket's list and every entry here shares
    // the one bucket (0xFFFFEFF7). The glyphs inside one label keep their order:
    // they never overlap, so only the group order is visible.
    std::vector<std::vector<text::DialogueSprite>> groups;
    groups.reserve(kFieldMenuItemCount * 2 + 1);

    for (int index = 0; index < kFieldMenuItemCount; ++index)
    {
      const int itemY = kFirstItemY - index * kItemStep;
      const std::string &label = labels_[static_cast<std::size_t>(index)];
      const std::uint32_t colour =
          (available(index) ? kAvailableRgb : kUnavailableRgb) |
          (static_cast<std::uint32_t>(itemAlpha_[static_cast<std::size_t>(index)]) << 24);
      const int width = text::FUN_00238e68_measure(label, font, kCellWidth);
      groups.push_back(text::FUN_00238608_layout(-width / 2, itemY, label, colour, kCellWidth,
                                                 kCellHeight, font));

      text::DialogueSprite bar;
      bar.textureSlot = text::textureWordSlot(kBarTextureWord);
      bar.clutBank = text::textureWordBank(kBarTextureWord);
      bar.u = kBarU;
      bar.v = kBarV;
      bar.sourceWidth = kBarSourceWidth;
      bar.sourceHeight = kBarSourceHeight;
      bar.x = screenX(-DAT_0031c45c_barWidth_ / 2);
      bar.y = screenY(itemY + kBarYBias);
      bar.width = DAT_0031c45c_barWidth_;
      bar.height = kBarHeight;
      bar.color = kBarColour;
      bar.blendMode = kBarBlendMode;
      groups.push_back({bar});
    }

    // FUN_00231C50:26-28. Items 1..3 keep the caption while their submenu runs;
    // the others draw one of their own.
    const bool submenuHidesCaption = iGpffffbcbc_submenuState_ != 0 && uGpffffae34_selected_ != 1 &&
                                     uGpffffae34_selected_ != 2 && uGpffffae34_selected_ != 3;
    if (!caption.empty() && !submenuHidesCaption)
    {
      const int width = text::FUN_00238e68_measure(caption, font, kCellWidth);
      groups.push_back(text::FUN_00238608_layout(kCaptionRightEdge - width,
                                                 kFirstItemY + kCaptionYBias, caption,
                                                 text::kColorDefault, kCellWidth, kCellHeight,
                                                 font));
    }

    std::vector<text::DialogueSprite> sprites;
    for (auto group = groups.rbegin(); group != groups.rend(); ++group)
    {
      sprites.insert(sprites.end(), group->begin(), group->end());
    }
    return sprites;
  }

  std::vector<text::DialogueSprite> FieldMenu::FUN_00232fa8_layout(const ReturnToTitleText &strings,
                                                                   const text::DialogueFont &font) const
  {
    // Submitted question, caption, Yes, No, and reversed for FUN_00207938's
    // head-first list as the panel is. None of the four overlaps.
    std::vector<std::vector<text::DialogueSprite>> groups;

    // :21-26. Both centred, both at the default colour.
    const auto centred = [&](const std::string &line, int y)
    {
      const int width = text::FUN_00238e68_measure(line, font, kCellWidth);
      groups.push_back(text::FUN_00238608_layout(-width / 2, y, line, text::kColorDefault,
                                                 kCellWidth, kCellHeight, font));
    };
    centred(strings.question, kConfirmQuestionY);
    centred(strings.caption, kConfirmCaptionY);

    // :48-55. The pair is centred as one run -- Yes, a 0x10 gap, No -- with
    // the sum truncated to 16 bits before it is halved.
    const int answerY = kConfirmQuestionY - kConfirmAnswerDrop;
    const int yesWidth = text::FUN_00238e68_measure(strings.yes, font, kCellWidth);
    const int noWidth = text::FUN_00238e68_measure(strings.no, font, kCellWidth);
    const int left = -static_cast<std::int16_t>(yesWidth + noWidth + kConfirmAnswerGap) / 2;
    groups.push_back(text::FUN_00238608_layout(
        left, answerY, strings.yes,
        static_cast<std::uint32_t>(DAT_0031c508_yesAlpha_) << 24 | kAvailableRgb, kCellWidth,
        kCellHeight, font));
    groups.push_back(text::FUN_00238608_layout(
        left + kConfirmAnswerGap + yesWidth, answerY, strings.no,
        static_cast<std::uint32_t>(DAT_0031c509_noAlpha_) << 24 | kAvailableRgb, kCellWidth,
        kCellHeight, font));

    std::vector<text::DialogueSprite> sprites;
    for (auto group = groups.rbegin(); group != groups.rend(); ++group)
    {
      sprites.insert(sprites.end(), group->begin(), group->end());
    }
    return sprites;
  }

} // namespace orphen::ported::scene
