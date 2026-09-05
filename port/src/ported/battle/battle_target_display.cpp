#include "ported/battle/battle_target_display.h"

#include <cmath>
#include <cstdio>
#include <optional>

namespace orphen::ported::battle
{
  namespace
  {
    namespace text = orphen::ported::text;
    namespace render = orphen::ported::render;

    // FUN_0030BD20, the EE's float-to-int truncation. Every pentagon corner
    // goes through it, in GS units, before it is an integer.
    int FUN_0030bd20_trunc(float value) { return static_cast<int>(value); }

    // The two GS origins FUN_0022EC30 adds, undone. See original_hud_quads.h:
    // x is 1/16 of a pixel and y 1/8 of a 448-space line.
    constexpr float kGsXPerPixel = 16.0f;
    constexpr float kGsYPerLine = 8.0f;

    // FUN_002334E8:19 and :36. A type 0x38 is an entity opcode 0x66 converted;
    // the class it actually is sits at +0x1CE.
    std::int32_t effectiveType(const orphen::ported::entity::OriginalEntity &entity)
    {
      const std::int32_t typeId = entity.typeId00;
      return typeId == 0x38 ? static_cast<std::int32_t>(entity.originalType1ce) : typeId;
    }
  } // namespace

  void FUN_0022ec30_pentagon(int centreX,
                             int centreY,
                             const std::array<std::uint8_t, 0x10> &effectiveness,
                             bool enemyBanding,
                             std::vector<render::HudQuad> &quads)
  {
    for (std::size_t arm = 0; arm < kDAT_0031c240_elements.size(); ++arm)
    {
      // :70-89. The table byte is *signed*, and a negative one lands on the
      // one-pip arm of the enemy banding rather than reading as huge.
      const auto value =
          static_cast<std::int32_t>(static_cast<std::int8_t>(
              effectiveness[static_cast<std::size_t>(kDAT_0031c240_elements[arm])]));
      std::int32_t litCount = value;
      if (value != 0)
      {
        if (!enemyBanding)
        {
          litCount = static_cast<std::int16_t>(value) / 10;
          if (litCount > 2)
          {
            litCount = 3;
          }
        }
        else
        {
          litCount = 1;
          if (value > kEffectivenessTwoPips)
          {
            litCount = 3;
            if (value < kEffectivenessThreePips)
            {
              litCount = 2;
            }
          }
        }
      }

      const float angle = kDAT_0031c2a0_armAngles[arm];
      // FUN_00305130 and FUN_00305218, cosf and sinf.
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);

      for (std::size_t pip = 0; pip < kDAT_0031c260_pipCorners.size(); ++pip)
      {
        const auto &corners = kDAT_0031c260_pipCorners[pip];
        const auto &pivot = kDAT_0031c290_pipPivots[pip];

        render::HudQuad quad;
        quad.textureSlot = kPipTextureSlot;
        quad.clutBank = kDAT_0031c250_clutBanks[arm];
        quad.color = static_cast<std::int32_t>(pip) < litCount ? kPipColorLit : kPipColorDim;
        for (std::size_t k = 0; k < 4; ++k)
        {
          const int dx = corners[k][0] - pivot[0];
          const int dy = corners[k][1] - pivot[1];
          // :95-108. The rotation happens in GS units, x sixteen to the pixel
          // and y eight to the line, and both are truncated before they are
          // added to the centre.
          const int gsX = FUN_0030bd20_trunc(static_cast<float>(dx * 16) * cosine -
                                             static_cast<float>(dy * 16) * sine);
          const int gsY = FUN_0030bd20_trunc(static_cast<float>(dx * 8) * sine +
                                             static_cast<float>(dy * 8) * cosine);
          quad.x[k] = static_cast<float>(centreX) + static_cast<float>(gsX) / kGsXPerPixel;
          quad.y[k] = static_cast<float>(centreY) + static_cast<float>(gsY) / kGsYPerLine;
          quad.u[k] = kPipTexels[k][0];
          quad.v[k] = kPipTexels[k][1];
        }
        quads.push_back(quad);
      }
    }
  }

  void FUN_002334e8_build(TargetDisplayRecord &record,
                          const TargetDisplayEnvironment &environment,
                          std::size_t slot)
  {
    // :14. The clear covers both captions as well as the record.
    record.FUN_00267e78_clear();
    if (environment.pool == nullptr || slot >= orphen::ported::entity::kEntitySlotCount)
    {
      return;
    }
    const auto &pool = *environment.pool;
    const auto &entity = pool.slot(slot);
    const std::int32_t typeId = effectiveType(entity);

    // :22-52. How many *other* live entities share this type, and where this
    // one comes in the list. The walk is pool slots 10..255 -- the script range
    // -- against DAT_005A96B0, the per-slot status byte.
    std::vector<std::size_t> sameType;
    for (std::size_t other = orphen::ported::entity::kFirstScriptSlot;
         other < orphen::ported::entity::kEntitySlotCount; ++other)
    {
      if (static_cast<std::int8_t>(pool.status(other)) <= 0)
      {
        continue;
      }
      if (effectiveType(pool.slot(other)) == typeId)
      {
        sameType.push_back(other);
      }
    }
    int duplicateIndex = 0;
    if (sameType.size() > 1)
    {
      for (std::size_t at = 0; at < sameType.size(); ++at)
      {
        if (sameType[at] == slot)
        {
          duplicateIndex = static_cast<int>(at) + 1;
          break;
        }
      }
    }

    // :54-105. Four tables, picked by the type id.
    std::optional<orphen::ported::resource::StatRecord> row;
    std::string name;
    if (typeId < 0x7C)
    {
      // The enemy table: a linear scan of group 2 for a row whose own +0x02 is
      // this type. A miss leaves the record cleared and nothing is drawn.
      if (environment.stats == nullptr)
      {
        return;
      }
      const std::uint32_t count = environment.stats->groupCount(2);
      for (std::uint32_t index = 0; index < count; ++index)
      {
        const auto candidate = environment.stats->FUN_00229688_record(2, static_cast<std::int32_t>(index));
        if (!candidate.has_value())
        {
          break;
        }
        if (candidate->halfword02 == typeId)
        {
          row = candidate;
          name = environment.stats->FUN_00229688_name(2, static_cast<std::int32_t>(index));
          break;
        }
      }
      if (!row.has_value())
      {
        return;
      }
    }
    else
    {
      const orphen::ported::resource::CharacterStats *archive = nullptr;
      std::size_t group = 0;
      std::int32_t index = 0;
      if (static_cast<std::uint32_t>((typeId - 0x7C) & 0xFFFF) < 0x7F)
      {
        archive = environment.stats;
        group = 0;
        index = typeId - 0x7C;
      }
      else if (static_cast<std::uint32_t>(typeId - 0x272) <= 0xFF)
      {
        archive = environment.objectStats;
        group = static_cast<std::size_t>(environment.DAT_00355208_objectGroup);
        index = typeId - 0x272;
      }
      else if (static_cast<std::uint32_t>(typeId - 0x373) <= 0xFF)
      {
        archive = environment.objectStats;
        group = 15;
        index = typeId - 0x373;
      }
      else if (static_cast<std::uint32_t>(typeId - 0x474) <= 0xFF)
      {
        archive = environment.objectStats;
        group = 16;
        index = typeId - 0x474;
      }
      else
      {
        return;
      }
      if (archive == nullptr)
      {
        return;
      }
      row = archive->FUN_00229688_record(group, index);
      if (!row.has_value())
      {
        return;
      }
      name = archive->FUN_00229688_name(group, index);
    }

    record.DAT_005715d0_effectiveness = row->tail18;

    // :107-113. FUN_00268558 is a plain copy; FUN_0030C1D8 is sprintf, and the
    // two formats are "%s-%d" at 0x00354DF0 and "HP:%3d" at 0x00354DF8.
    if (duplicateIndex == 0)
    {
      record.DAT_005715e0_name = name;
    }
    else
    {
      char buffer[0x80] = {};
      std::snprintf(buffer, sizeof(buffer), "%s-%d", name.c_str(), duplicateIndex);
      record.DAT_005715e0_name = buffer;
    }

    // psVar13[0x95] is entity +0x12A, the hit points the +0xBE drain eats into.
    char hitPoints[0x40] = {};
    std::snprintf(hitPoints, sizeof(hitPoints), "HP:%3d",
                  static_cast<int>(entity.staggerTimer12a));
    record.DAT_00571660_hitPoints = hitPoints;
  }

  void FUN_00233818_draw(const TargetDisplayRecord &record,
                         const TargetDisplayEnvironment &environment,
                         std::vector<text::DialogueSprite> &sprites,
                         std::vector<render::HudQuad> &quads)
  {
    // :7. The name is the first byte of the block FUN_002334E8 cleared.
    if (record.DAT_005715e0_name.empty())
    {
      return;
    }

    if (environment.font != nullptr && environment.font->measured())
    {
      const auto caption = [&](const std::string &line, int y) {
        const int width = text::FUN_00238e68_measure(line, *environment.font, kCaptionCellWidth);
        const std::vector<text::DialogueSprite> laid =
            text::FUN_00238608_layout(kCaptionRightEdge - width, y, line, kCaptionColor,
                                      kCaptionCellWidth, kCaptionCellHeight, *environment.font);
        sprites.insert(sprites.end(), laid.begin(), laid.end());
      };
      caption(record.DAT_005715e0_name, kNameCaptionY);
      caption(record.DAT_00571660_hitPoints, kHitPointsCaptionY);
    }

    FUN_0022ec30_pentagon(kPentagonCentreX, kPentagonCentreY,
                          record.DAT_005715d0_effectiveness, true, quads);
  }

} // namespace orphen::ported::battle
