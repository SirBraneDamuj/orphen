#include "ported/render/original_letterbox.h"

namespace orphen::ported::render
{

  void Letterbox::FUN_0025fd10_open(bool alreadyOpen)
  {
    // FUN_0025fd10:21-28. The guard is on the *mode*, so a second -1 while the
    // bars are already open is ignored rather than restarting the slide.
    if (DAT_00355054_mode_ >= 1)
    {
      return;
    }
    DAT_00355054_mode_ = 1;
    DAT_00355cfc_level_ = alreadyOpen ? kFullLevel : 0;
  }

  void Letterbox::FUN_0025fd10_close()
  {
    // FUN_0025fd10:44-46.
    if (DAT_00355054_mode_ != 0)
    {
      DAT_00355054_mode_ = -1;
    }
  }

  void Letterbox::FUN_0025cfb8_step(std::uint32_t frameTicks)
  {
    if (DAT_00355054_mode_ == 0)
    {
      barHeight_ = 0;
      return;
    }

    // FUN_0025cfb8:9-13. The bias before the shift rounds toward zero for a
    // negative level; nothing can drive the level below zero, but the original
    // computes it unconditionally and the height it feeds the sprites is this
    // one, not the advanced one.
    int biased = DAT_00355cfc_level_;
    if (biased < 0)
    {
      biased += (1 << kLevelShift) - 1;
    }
    barHeight_ = biased >> kLevelShift;

    // :30-41. Note the retract arm only clears the mode when the level was
    // above zero to begin with -- a mode of -1 sitting on a level of 0 stays
    // -1, which is the original's behaviour and is unreachable from 0x6D.
    const int step = static_cast<int>(frameTicks) * kLevelStepPerTick;
    if (DAT_00355054_mode_ < 1)
    {
      if (DAT_00355cfc_level_ > 0)
      {
        DAT_00355cfc_level_ -= step;
        if (DAT_00355cfc_level_ < 1)
        {
          DAT_00355cfc_level_ = 0;
          DAT_00355054_mode_ = 0;
        }
      }
    }
    else if (DAT_00355cfc_level_ < kFullLevel)
    {
      DAT_00355cfc_level_ += step;
    }
    else
    {
      DAT_00355cfc_level_ = kFullLevel;
    }
  }

  void Letterbox::reset()
  {
    DAT_00355054_mode_ = 0;
    DAT_00355cfc_level_ = 0;
    barHeight_ = 0;
  }

} // namespace orphen::ported::render
