#include "ported/entity/original_smoke_cloud.h"

#include <algorithm>
#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    // FUN_00212DB0:2-8 and FUN_00212D60:2-8, the same four lines in both.
    int roundCount(int count)
    {
      if (count > kSmokeMaxParticles)
      {
        return kSmokeMaxParticles;
      }
      if ((count & 0x1F) != 0)
      {
        return (count + 0x1F) & ~0x1F;
      }
      return count;
    }
  } // namespace

  void SmokeCloud::FUN_00212db0_arm(int count,
                                    std::uint32_t colour,
                                    float scale,
                                    const orphen::ported::psm2::Vec3 &leadPosition,
                                    const std::function<std::uint32_t()> &random)
  {
    DAT_00354c5c_count_ = roundCount(count);
    DAT_00355a40_alphaCeiling_ = colour >> 24;
    DAT_00355a44_rgb_ = colour & 0x00FFFFFFu;
    DAT_00355a48_scale_ = scale * kFGpffff813c_smokeScale;

    // :19-46. Negative and zero counts skip the seed entirely, leaving whatever
    // was there -- the original guards the loop with `0 < param_2`.
    if (DAT_00354c5c_count_ > 0)
    {
      DAT_0054f080_particles_.resize(static_cast<std::size_t>(kSmokeMaxParticles));
      for (int i = 0; i < DAT_00354c5c_count_; ++i)
      {
        SmokeParticle &particle = DAT_0054f080_particles_[static_cast<std::size_t>(i)];
        std::uint32_t nibbles = 0;
        for (int c = 0; c < 3; ++c)
        {
          // :26-35. Three of the sixteen drift values are rewritten: 7 -> 5,
          // 8 -> 13, 9 -> 11. Biased by -8 at read time, those are the three
          // nearest zero, so no axis is ever left standing still.
          std::uint32_t nibble = random() & 0xFu;
          if (nibble == 7)
          {
            nibble = 5;
          }
          else if (nibble == 8)
          {
            nibble = 13;
          }
          else if (nibble == 9)
          {
            nibble = 11;
          }
          nibbles = (nibbles << 4) | nibble;
          particle.phase[static_cast<std::size_t>(c)] = static_cast<std::uint16_t>(random());
        }
        // :39. Only twelve bits are written, so the shove counter starts at 0.
        particle.packed06 = static_cast<std::uint16_t>(nibbles);
      }
    }

    // :48-57, then :58. The lead's position is latched and the unused
    // DAT_00355A4C zeroed.
    DAT_0055f080_lastLeadPosition_ = leadPosition;
  }

  void SmokeCloud::FUN_00212d60_set(int count, std::uint32_t colour, float scale)
  {
    DAT_00354c5c_count_ = roundCount(count);
    DAT_00355a40_alphaCeiling_ = colour >> 24;
    DAT_00355a44_rgb_ = colour & 0x00FFFFFFu;
    DAT_00355a48_scale_ = scale * kFGpffff813c_smokeScale;
    if (DAT_00354c5c_count_ > 0 &&
        DAT_0054f080_particles_.size() < static_cast<std::size_t>(DAT_00354c5c_count_))
    {
      // The original has one static array and never checks: a count raised past
      // what the last arm seeded simply walks records that were left where they
      // were. The port has to have the storage to do the same thing.
      DAT_0054f080_particles_.resize(static_cast<std::size_t>(kSmokeMaxParticles));
    }
  }

  void SmokeCloud::FUN_00212f38_step(const orphen::ported::psm2::Vec3 &eye,
                                     const orphen::ported::psm2::Vec3 &forward,
                                     const orphen::ported::psm2::Vec3 &leadPosition,
                                     float leadHeight,
                                     std::int16_t leadAnimation,
                                     std::uint32_t frameCounter)
  {
    drawList_.clear();
    visibleCount_ = 0;
    // :67. The whole function is behind `DAT_00354C5C != 0`.
    if (DAT_00354c5c_count_ <= 0)
    {
      return;
    }

    // :100-104. The box centre: 1.5 units along the camera forward in x and y,
    // and in z the eye plus a fixed 0.4 plus *twice* the forward z.
    const std::array<float, 3> box{
        eye.x + forward.x * 1.5f,
        eye.y + forward.y * 1.5f,
        eye.z + (kDAT_0058c0e0_cameraHeight - kDAT_003520b0_boxZBias) + forward.z + forward.z};

    // :105-116. FUN_0030BD20 is the float-to-int convert, which truncates.
    std::array<std::int32_t, 3> cell{};
    for (int c = 0; c < 3; ++c)
    {
      cell[static_cast<std::size_t>(c)] =
          static_cast<std::int32_t>(box[static_cast<std::size_t>(c)] * kDAT_003520b4_phasePerUnit);
    }

    // :117-135. Entity +0xA0 below 3 is a stand; only then does the lead's
    // movement come from comparing this frame's position with last frame's.
    bool moved = true;
    if (leadAnimation < kSmokeIdleAnimationCeiling)
    {
      moved = false;
      const std::array<float, 3> now{leadPosition.x, leadPosition.y, leadPosition.z};
      std::array<float, 3> last{DAT_0055f080_lastLeadPosition_.x,
                                DAT_0055f080_lastLeadPosition_.y,
                                DAT_0055f080_lastLeadPosition_.z};
      for (int c = 0; c < 3; ++c)
      {
        if (last[static_cast<std::size_t>(c)] != now[static_cast<std::size_t>(c)])
        {
          last[static_cast<std::size_t>(c)] = now[static_cast<std::size_t>(c)];
          moved = true;
        }
      }
      DAT_0055f080_lastLeadPosition_ = {last[0], last[1], last[2]};
    }

    // :137-142. The re-arm box is centred on the lead's waist, not its feet.
    const std::array<float, 3> origin{leadPosition.x, leadPosition.y,
                                      leadPosition.z + leadHeight * 0.5f};

    const std::uint32_t ceiling = DAT_00355a40_alphaCeiling_;
    const int count = std::min<int>(DAT_00354c5c_count_,
                                    static_cast<int>(DAT_0054f080_particles_.size()));
    drawList_.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i)
    {
      SmokeParticle &particle = DAT_0054f080_particles_[static_cast<std::size_t>(i)];
      const std::uint16_t packed = particle.packed06;

      // :148-155. Component 0 takes nibble 1, component 1 nibble 2 and
      // component 2 nibble 0 -- the arm packed them the other way round.
      const std::array<std::int32_t, 3> drift{
          static_cast<std::int32_t>((packed >> 4) & 0xF) - 8,
          static_cast<std::int32_t>((packed >> 8) & 0xF) - 8,
          static_cast<std::int32_t>(packed & 0xF) - 8};
      std::uint32_t counter = static_cast<std::uint32_t>(packed >> 12);

      std::uint32_t alpha = ceiling;
      std::array<float, 3> position{};
      for (int c = 0; c < 3; ++c)
      {
        const auto axis = static_cast<std::size_t>(c);
        const std::int32_t advanced =
            static_cast<std::int32_t>(particle.phase[axis]) + drift[axis] * 2;
        particle.phase[axis] = static_cast<std::uint16_t>(advanced);

        const std::uint32_t wrapped =
            static_cast<std::uint32_t>(advanced - cell[axis]) & 0xFFFFu;
        // :170-176. Only the two seams narrow the alpha; between them the
        // original assigns the running minimum back to itself, so the compare
        // that follows is a no-op there.
        if (wrapped < 0x1000u)
        {
          alpha = std::min(alpha, (ceiling * wrapped) >> 12);
        }
        else if (wrapped > 0xF000u)
        {
          alpha = std::min(alpha, (ceiling * (0x10000u - wrapped)) >> 12);
        }

        position[axis] =
            static_cast<float>(wrapped) * kSmokeUnitPerPhase + box[axis] - 1.5f;
      }

      // :186-196.
      if (moved)
      {
        const float dx = position[0] - origin[0];
        const float dy = position[1] - origin[1];
        const float dz = position[2] - origin[2];
        if (dx < kDAT_003520b8_reArmXY && kDAT_003520bc_reArmXYLow < dx &&
            dy < kDAT_003520b8_reArmXY && kDAT_003520bc_reArmXYLow < dy &&
            dz < kDAT_003520c0_reArmZHigh && kSmokeReArmZLow < dz)
        {
          counter = 0xF;
        }
      }

      // :197-215. A particle that is fading loses the shove outright; one at
      // full alpha takes an extra drift step proportional to what is left of
      // the counter, and the counter itself comes down every fourth frame.
      if (counter != 0)
      {
        if (alpha == ceiling)
        {
          for (int c = 0; c < 3; ++c)
          {
            const auto axis = static_cast<std::size_t>(c);
            particle.phase[axis] = static_cast<std::uint16_t>(
                particle.phase[axis] +
                static_cast<std::int32_t>(static_cast<std::int16_t>(drift[axis])) *
                    static_cast<std::int32_t>(counter) * 2);
          }
          if ((frameCounter & 3u) == 0)
          {
            --counter;
          }
        }
        else
        {
          counter = 0;
        }
        particle.packed06 =
            static_cast<std::uint16_t>((packed & 0x0FFFu) | (counter << 12));
      }

      SmokeDrawPoint point;
      point.position = {position[0], position[1], position[2]};
      // :236-239. The size index cycles 0, 10, 20, 30 with the pool index.
      point.sizeIndex = static_cast<std::uint8_t>((i & 3) * 10);
      point.alpha = static_cast<std::uint8_t>(alpha);
      if (point.alpha != 0)
      {
        ++visibleCount_;
      }
      drawList_.push_back(point);
    }
  }

} // namespace orphen::ported::entity
