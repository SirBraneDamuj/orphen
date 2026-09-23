#include "ported/scene/equip_screen.h"

#include "ported/model/psc3_skeleton.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace orphen::ported::scene
{
  namespace
  {
    namespace text = orphen::ported::text;
    using orphen::ported::psm2::Vec3;

    // FUN_002340E0:32/:43 -- the fade-out handing over to the relight below
    // this level, and the relight's ceiling.
    constexpr std::int16_t kFadeOutHandOver = 0x61;
    constexpr std::int16_t kFadeOutFloor = 6;
    constexpr std::int16_t kFadeInCeiling = 0x1FE0;
    // FUN_002340E0:32, the map fade cap that stops FUN_00209140 drawing it.
    constexpr std::uint8_t kFadeCapMapOff = 3;

    // FUN_00230128's gp words, DAT_00354DC0..DC8+0x18: the reach along the
    // lead's facing, then a sideways radius and a height per slot.
    constexpr float kDAT_00354dc0_reach = 1.2f;
    constexpr float kSlotRadius[kEquipSlotCount] = {0.2f, 0.5f, 0.8f};     // DC4, DCC, DD4
    constexpr float kSlotHeight[kEquipSlotCount] = {0.025f, -0.15f, -0.3f}; // DC8, DD0, DD8
    // DAT_00352544, the quarter turn the sideways axis is taken at.
    constexpr float kDAT_00352544_quarterTurn = 1.570796012878418f;

    // FUN_00230F08's gp constants: fGpffff85E0, fGpffff85E8, fGpffff85E4 and
    // fGpffff85EC, then uGpffff8544 -- slot 1's +0x58, written once at boot by
    // FUN_00228E28:206 and never again.
    constexpr float kFGpffff85e0 = 1.570796012878418f;
    constexpr float kFGpffff85e8 = -1.570796012878418f;
    constexpr float kFGpffff85e4_depthScale = -1.2f;
    constexpr float kFGpffff85ec = 0.2f;
    constexpr float kDAT_0058c0e0_cameraHeight = 0.6f;
    constexpr float kProjectionScale = 480.0f;
    // FUN_002311E8:29.
    constexpr int kRowTextOffset = 0x4B;

    // FUN_00230CE0's separator, control code 7.
    constexpr char kLineSeparator = '\a';

    // FUN_002311A8: the character's name and bar 0.
    constexpr int kHeaderX = -0x12C;
    constexpr int kHeaderY = 0xC0;
    constexpr int kHeaderCell = 0x18;
    constexpr int kHeaderBarY = 0xBC;
    // FUN_00230E50: the spell's name right-aligned to 0x130 at y 0xA0, and bar 1.
    constexpr int kRightEdge = 0x130;
    constexpr int kSpellNameY = 0xA0;
    constexpr int kSpellNameCellWidth = 0x14;
    constexpr int kSpellNameCellHeight = 0x16;
    constexpr int kSpellBarY = 0x9C;
    // FUN_00230DB0: the four description lines, DAT_0031C328.
    constexpr int kDescriptionY[4] = {0x80, 0x6E, -0x34, -0x46};
    constexpr int kDescriptionCell = 0x12;
    // FUN_002311E8: row i's name at y `0xE4 - DAT_00354DE0[i]`, its bar at
    // `0xE0 - DAT_00354DE0[i]`, and the button glyph 0x1E to the left.
    constexpr int kDAT_00354de0_rowDrop[kEquipSlotCount] = {330, 363, 395};
    constexpr int kRowTextY = 0xE4;
    constexpr int kRowBarY = 0xE0;
    constexpr int kRowCellWidth = 0x16;
    constexpr int kRowCellHeight = 0x18;
    constexpr int kGlyphGap = 0x1E;
    // FUN_0022E910:47-51: message 0x29, right-aligned to 0x140 at y 0xBE.
    constexpr int kCaptionRightEdge = 0x140;
    constexpr int kCaptionY = 0xBE;
    constexpr int kCaptionCell = 0x14;

    // The FUN_00239020 entry at 0x0031C2B8, read out of SLUS_200.11. FUN_0022EF10
    // *overwrites* its texture word with `index * 0x100 + 0x2C` -- the 0x22C in
    // the ELF is just bar 2's, left there by the last call before the image
    // was written -- so bars 0, 1 and 2 read slot 0x2C through CLUT banks 0, 1
    // and 2. Full width, 16 high, one 16x16 texel block at (0, 0xB0), blend off.
    constexpr int kBarTextureWord = 0x02C;
    constexpr int kBarX = -0x140;
    constexpr int kBarWidth = 0x280;
    constexpr int kBarHeight = 0x10;
    constexpr int kBarU = 0x00;
    constexpr int kBarV = 0xB0;
    constexpr int kBarSource = 0x10;
    // The entry at 0x0031C338: slot 0x2C, CLUT bank 8, 0x16 x 0x18, a 32x32
    // block whose corner FUN_002311E8 copies out of DAT_0031C230 per row --
    // Triangle, Circle, Cross.
    constexpr int kGlyphTextureWord = 0x82C;
    constexpr int kGlyphWidth = 0x16;
    constexpr int kGlyphHeight = 0x18;
    constexpr int kGlyphSource = 0x20;
    constexpr int kDAT_0031c230_glyphUv[kEquipSlotCount][2] = {{192, 120}, {192, 152}, {224, 152}};
    // Both entries' +0x2C.
    constexpr int kEntryBlendMode = 0;
    constexpr std::uint32_t kEntryColour = 0x80808080;
    // The entry at 0x0031C388, FUN_00231C30's box: slot 0x2C through CLUT
    // bank 6, a 0x80 x 0x14 block at (0x80, 0xEC), bucket 0x1009. x, y, width
    // and height are the call's.
    constexpr int kBoxTextureWord = 0x62C;
    constexpr int kBoxU = 0x80;
    constexpr int kBoxV = 0xEC;
    constexpr int kBoxSourceWidth = 0x80;
    constexpr int kBoxSourceHeight = 0x14;
    constexpr int kBoxHeight = 0x24;
    // FUN_0022FBD0's message cell.
    constexpr int kNoSpellsCell = 0x20;
    // Their +0x04, the sort buckets FUN_00207938 negates: bars under the
    // glyphs, and both under every glyph of text (0xFFFFEFF7).
    constexpr int kBucketBars = 0x1004;
    constexpr int kBucketGlyphs = 0x1005;
    constexpr int kBucketText = 0x1009;

    constexpr int kScreenHalfWidth = 320;
    constexpr int kScreenHalfHeight = 224;
    int screenX(int entryX) { return entryX + kScreenHalfWidth; }
    int screenY(int entryY) { return kScreenHalfHeight - entryY; }

    // `(v + 0x1F) >> 5` for a negative v, `v >> 5` otherwise.
    int shiftToward(int value) { return (value < 0 ? value + 0x1F : value) >> 5; }

    text::DialogueSprite bar(int index, int entryY)
    {
      const int word = kBarTextureWord + index * 0x100;
      text::DialogueSprite sprite;
      sprite.textureSlot = text::textureWordSlot(word);
      sprite.clutBank = text::textureWordBank(word);
      sprite.u = kBarU;
      sprite.v = kBarV;
      sprite.sourceWidth = kBarSource;
      sprite.sourceHeight = kBarSource;
      sprite.x = screenX(kBarX);
      sprite.y = screenY(entryY);
      sprite.width = kBarWidth;
      sprite.height = kBarHeight;
      sprite.color = kEntryColour;
      sprite.blendMode = kEntryBlendMode;
      sprite.sortBucket = kBucketBars;
      return sprite;
    }
  } // namespace

  EquipFadeStep FUN_002340e0_step(EquipFade &fade,
                                  bool titleSceneSpecial,
                                  bool titleSceneEntry2a,
                                  std::uint32_t frameTicks)
  {
    EquipFadeStep step;
    if (titleSceneSpecial)
    {
      // :19-23. Nothing is drawn and nothing ramps.
      if (titleSceneEntry2a)
      {
        fade.done1da8c = 1;
      }
      return step;
    }
    if (fade.done1da8c != 0)
    {
      // :25-28.
      step.underlayAlpha = 0xFF;
      return step;
    }

    bool finished = false;
    const int ticks4 = static_cast<int>(frameTicks) * 4;
    if (fade.fadeOut1da8e < kFadeOutHandOver)
    {
      // :31-44. The room is gone; relight toward the screen's own light.
      step.fadeCapWritten = true;
      step.fadeCap = kFadeCapMapOff;
      step.colourMixLevel = shiftToward(fade.fadeIn1da90);
      fade.fadeIn1da90 = static_cast<std::int16_t>(fade.fadeIn1da90 + ticks4);
      if (fade.fadeIn1da90 > kFadeInCeiling)
      {
        finished = true;
        fade.fadeIn1da90 = kFadeInCeiling;
      }
    }
    else
    {
      // :46-58. The smear and the map cap take the same level, then the level
      // steps down -- so the cap written is the level before the step.
      step.smearWritten = true;
      step.smear = static_cast<std::uint8_t>(shiftToward(fade.fadeOut1da8e));
      step.fadeCapWritten = true;
      step.fadeCap = step.smear;
      fade.fadeOut1da8e = static_cast<std::int16_t>(fade.fadeOut1da8e - ticks4);
      if (fade.fadeOut1da8e < kFadeOutFloor)
      {
        fade.fadeOut1da8e = 0;
        step.smear = 0;
      }
    }
    // :60-65.
    step.underlayAlpha =
        static_cast<std::uint8_t>(~(shiftToward(fade.fadeOut1da8e) << 1) & 0xFF);
    if (finished)
    {
      fade.done1da8c = 1;
    }
    return step;
  }

  std::uint32_t FUN_0022ef30_mix(std::uint32_t savedRgb, int level, int target)
  {
    // Three bytes, each `(saved * (0xFF - level) + level * target) >> 8`; the
    // fourth is left alone. Every channel takes the same formula, so the
    // byte order of the packing does not matter.
    std::uint32_t result = savedRgb & 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8)
    {
      const int channel = static_cast<int>((savedRgb >> shift) & 0xFFu);
      const int mixed = (channel * (0xFF - level) + level * target) >> 8;
      result |= (static_cast<std::uint32_t>(mixed) & 0xFFu) << shift;
    }
    return result;
  }

  Vec3 FUN_00230128_slot_position(int slot, const Vec3 &lead, float leadFacing)
  {
    if (slot < 0 || slot >= kEquipSlotCount)
    {
      return lead;
    }
    const float sideways =
        orphen::ported::model::FUN_00216690_wrap_angle(leadFacing - kDAT_00352544_quarterTurn);
    const float radius = kSlotRadius[slot];
    return {lead.x + radius * std::cos(sideways) + kDAT_00354dc0_reach * std::cos(leadFacing),
            lead.y + radius * std::sin(sideways) + kDAT_00354dc0_reach * std::sin(leadFacing),
            lead.z + kSlotHeight[slot]};
  }

  int FUN_002311e8_row_x(const Vec3 &point, const Vec3 &eye, float cameraYaw, float cameraPitch)
  {
    using Matrix = std::array<std::array<float, 4>, 4>;
    const auto identity = []
    {
      Matrix m{};
      for (int i = 0; i < 4; ++i)
      {
        m[i][i] = 1.0f;
      }
      return m;
    };

    // FUN_0020BAE0 into 0x00342828 and FUN_0020BA30 into 0x003427A8. Both
    // write only their four rotation words; the rest of each matrix reads as
    // identity on hardware.
    Matrix yaw = identity();
    const float a = kFGpffff85e0 - cameraYaw;
    yaw[0][0] = std::cos(a);
    yaw[0][1] = -std::sin(a);
    yaw[1][0] = std::sin(a);
    yaw[1][1] = std::cos(a);
    Matrix pitch = identity();
    const float b = kFGpffff85e8 - cameraPitch;
    pitch[1][1] = std::cos(b);
    pitch[1][2] = -std::sin(b);
    pitch[2][1] = std::sin(b);
    pitch[2][2] = std::cos(b);

    // FUN_0020BB98(out, a, b) is `out = a * b`, rows by columns.
    const auto multiply = [](const Matrix &l, const Matrix &r)
    {
      Matrix out{};
      for (int i = 0; i < 4; ++i)
      {
        for (int j = 0; j < 4; ++j)
        {
          out[i][j] = l[i][0] * r[0][j] + l[i][1] * r[1][j] + l[i][2] * r[2][j] +
                      l[i][3] * r[3][j];
        }
      }
      return out;
    };
    Matrix view = multiply(pitch, yaw);
    // :20-29, the translation column: the eye lifted by slot 1's +0x58 less
    // fGpffff85EC.
    const float tx = -eye.x;
    const float ty = -eye.y;
    const float tz = -(eye.z + (kDAT_0058c0e0_cameraHeight - kFGpffff85ec));
    for (int row = 0; row < 3; ++row)
    {
      view[row][3] = tx * view[row][0] + ty * view[row][1] + tz * view[row][2];
    }

    // FUN_0020BB38(480, 480, fGpffff85E4) into 0x00342968, then the product,
    // then :34-36 rewrite the translation column scaled the same way.
    Matrix scale = identity();
    scale[0][0] = kProjectionScale;
    scale[1][1] = kProjectionScale;
    scale[2][2] = kFGpffff85e4_depthScale;
    Matrix out = multiply(scale, view);
    out[0][3] = view[0][3] * kProjectionScale;
    out[2][3] = view[2][3] * kFGpffff85e4_depthScale;

    // FUN_002310F8 for rows 0 and 2, and FUN_0030BD20's truncating convert.
    const float x = point.x * out[0][0] + point.y * out[0][1] + point.z * out[0][2] + out[0][3];
    const float z = point.x * out[2][0] + point.y * out[2][1] + point.z * out[2][2] + out[2][3];
    return static_cast<int>(x / z) + kRowTextOffset;
  }

  EquipRingPath FUN_00230450_ring_path(const std::function<Vec3(int bone)> &bonePoint)
  {
    EquipRingPath path{};
    for (int bone = 0; bone < kEquipRingBones; ++bone)
    {
      // :17-20. The last bone's midpoint is taken toward bone 0.
      const Vec3 here = bonePoint(bone);
      const Vec3 next = bonePoint(bone + 1 < kEquipRingBones ? bone + 1 : 0);
      const std::size_t at = static_cast<std::size_t>(bone) * 2;
      path[at] = here;
      path[at + 1] = {here.x + (next.x - here.x) * 0.5f, here.y + (next.y - here.y) * 0.5f,
                      here.z + (next.z - here.z) * 0.5f};
    }
    return path;
  }

  camera::Curve3 FUN_00230608_path_curve(const EquipRingPath &path, int from, int to, int step)
  {
    camera::Curve3 curve;
    if (from == to)
    {
      return curve;
    }
    const auto wrap = [](int index)
    {
      return index < 0 ? kEquipRingPathPoints - 1 : (index >= kEquipRingPathPoints ? 0 : index);
    };
    if (to >= kEquipRingPathPoints)
    {
      to = 0;
    }
    // :24-35. Point 0 is where the icon is, point 1 halfway to the next step.
    std::array<Vec3, camera::kMaxSplinePoints> points{};
    points[0] = path[static_cast<std::size_t>(from)];
    int cursor = wrap(from + step);
    const Vec3 &first = path[static_cast<std::size_t>(cursor)];
    points[1] = {points[0].x + (first.x - points[0].x) * 0.5f,
                 points[0].y + (first.y - points[0].y) * 0.5f,
                 points[0].z + (first.z - points[0].z) * 0.5f};

    // :36-78, DAT_005709D0: the loop indices still to visit, ending on `to`.
    std::array<int, kEquipRingPathPoints> visit{};
    visit[0] = to;
    if (cursor != to)
    {
      int steps = 0;
      std::size_t at = 0;
      int walk = cursor;
      visit[0] = cursor;
      while (true)
      {
        ++steps;
        walk = wrap(walk + step);
        ++at;
        if (walk == to)
        {
          break;
        }
        visit[at] = walk;
      }
      if (steps < 0xE)
      {
        visit[at] = to;
      }
      else
      {
        // :56-76. Thin to thirteen: a point is kept each time the running
        // share crosses 1, and the share keeps its fraction -- FUN_0030BD20
        // and `% 100000` over DAT_0035254C.
        constexpr float kDAT_0035254c = 100000.0f;
        const float share = 13.0f / static_cast<float>(steps);
        std::size_t kept = 0;
        int remaining = 0xD;
        float running = share + 0.0f;
        int index = cursor;
        while (true)
        {
          if (1.0f <= running)
          {
            --remaining;
            const int scaled = static_cast<int>(running * kDAT_0035254c);
            visit[kept++] = index;
            running = static_cast<float>(scaled % 100000) / kDAT_0035254c;
          }
          index = wrap(index + step);
          if (remaining == 0)
          {
            break;
          }
          running += share;
        }
        visit[kept] = to;
      }
    }

    // :79-87. Copied in until `to` is in, or the curve is full.
    std::size_t count = 2;
    std::size_t at = 0;
    while (true)
    {
      ++count;
      points[count - 1] = path[static_cast<std::size_t>(visit[at])];
      if (count > 0xF)
      {
        break;
      }
      const int copied = visit[at];
      ++at;
      if (copied == to)
      {
        break;
      }
    }
    curve.FUN_00266a78_build({points.data(), count}, true);
    return curve;
  }

  bool FUN_00230e50_spell_shown(int state, const std::string &spellName)
  {
    return !spellName.empty() && state != 3 && state != 8 && state != 9 && state != 10 &&
           state != 11;
  }

  std::array<std::string, 4> FUN_00230ce0_split(const std::string &description)
  {
    std::array<std::string, 4> lines{};
    std::size_t at = 0;
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
      while (at < description.size() && description[at] != kLineSeparator)
      {
        lines[line].push_back(description[at]);
        ++at;
      }
      if (at >= description.size())
      {
        break;
      }
      // Past the separator; a separator straight after another is an empty
      // line, which FUN_00230DB0 skips.
      ++at;
    }
    return lines;
  }

  std::vector<text::DialogueSprite> FUN_0022e910_layout(const EquipScreenDraw &draw,
                                                        const text::DialogueFont &font)
  {
    // Collected as (bucket, group) in submission order. FUN_00207938 draws
    // bucket by bucket, lowest first, and pushes each entry onto its bucket's
    // head -- so within one bucket the last submitted is drawn first.
    std::vector<std::pair<int, std::vector<text::DialogueSprite>>> submitted;
    // FUN_0022EEF0: FUN_00238608, except in state 0xC.
    const bool drawText = draw.state != kEquipStateLeaveByReload;
    const auto say = [&](int x, int y, const std::string &line, int cellWidth, int cellHeight)
    {
      if (drawText && !line.empty())
      {
        submitted.push_back({kBucketText, text::FUN_00238608_layout(x, y, line, kEntryColour,
                                                                    cellWidth, cellHeight, font)});
      }
    };

    // FUN_00230DB0 -- the four description lines, right-aligned to 0x130.
    const auto describe = [&]
    {
      for (std::size_t line = 0; line < draw.descriptionLines.size(); ++line)
      {
        const std::string &lineText = draw.descriptionLines[line];
        const int lineWidth = text::FUN_00238e68_measure(lineText, font, kDescriptionCell);
        say(kRightEdge - lineWidth, kDescriptionY[line], lineText, kDescriptionCell,
            kDescriptionCell);
      }
    };
    // FUN_0022F408:50, inside the state handler -- so ahead of everything the
    // dispatcher draws after it.
    if (draw.descriptionTwice)
    {
      describe();
    }

    // FUN_0022FBD0, state 8's handler: the message centred on y 0 with the
    // box FUN_00231C30 lays behind it, then message 0x27 where the spell's
    // name goes. All three go to bucket 0x1009, box after text, so the box is
    // drawn first.
    if (draw.noSpellsBox)
    {
      const int width = text::FUN_00238e68_measure(draw.noSpellsMessage, font, kNoSpellsCell);
      say(-width / 2, 0, draw.noSpellsMessage, kNoSpellsCell, kNoSpellsCell);
      text::DialogueSprite box;
      box.textureSlot = text::textureWordSlot(kBoxTextureWord);
      box.clutBank = text::textureWordBank(kBoxTextureWord);
      box.u = kBoxU;
      box.v = kBoxV;
      box.sourceWidth = kBoxSourceWidth;
      box.sourceHeight = kBoxSourceHeight;
      box.x = screenX(-width / 2 - 0x10);
      box.y = screenY(2);
      box.width = width + 0x20;
      box.height = kBoxHeight;
      box.color = kEntryColour;
      box.blendMode = kEntryBlendMode;
      box.sortBucket = kBucketText;
      submitted.push_back({kBucketText, {box}});
      if (!draw.noSpellsCaption.empty())
      {
        const int captionWidth =
            text::FUN_00238e68_measure(draw.noSpellsCaption, font, kSpellNameCellWidth);
        submitted.push_back(
            {kBucketText, text::FUN_00238608_layout(kRightEdge - captionWidth, kSpellNameY,
                                                    draw.noSpellsCaption, kEntryColour,
                                                    kSpellNameCellWidth, kSpellNameCellHeight, font)});
      }
    }

    // FUN_002311A8.
    say(kHeaderX, kHeaderY, draw.characterName, kHeaderCell, kHeaderCell);
    submitted.push_back({kBucketBars, {bar(0, kHeaderBarY)}});

    // FUN_00230E50. The state list is its own; the pentagon (FUN_0022EC30) is
    // not ported yet.
    const bool spellShown = FUN_00230e50_spell_shown(draw.state, draw.spellName);
    if (spellShown)
    {
      const int width = text::FUN_00238e68_measure(draw.spellName, font, kSpellNameCellWidth);
      say(kRightEdge - width, kSpellNameY, draw.spellName, kSpellNameCellWidth,
          kSpellNameCellHeight);
      describe();
    }
    submitted.push_back({kBucketBars, {bar(1, kSpellBarY)}});

    // FUN_002311E8, the Equip screen only.
    if (draw.equip)
    {
      for (int row = 0; row < kEquipSlotCount; ++row)
      {
        const std::size_t r = static_cast<std::size_t>(row);
        const int drop = kDAT_00354de0_rowDrop[row];
        if (draw.slotIcon[r])
        {
          say(draw.rowX[r], kRowTextY - drop, draw.slotNames[r], kRowCellWidth, kRowCellHeight);
        }
        submitted.push_back({kBucketBars, {bar(2, kRowBarY - drop)}});

        text::DialogueSprite glyph;
        glyph.textureSlot = text::textureWordSlot(kGlyphTextureWord);
        glyph.clutBank = text::textureWordBank(kGlyphTextureWord);
        glyph.u = kDAT_0031c230_glyphUv[row][0];
        glyph.v = kDAT_0031c230_glyphUv[row][1];
        glyph.sourceWidth = kGlyphSource;
        glyph.sourceHeight = kGlyphSource;
        glyph.x = screenX(draw.rowX[r] - kGlyphGap);
        glyph.y = screenY(kRowTextY - drop);
        glyph.width = kGlyphWidth;
        glyph.height = kGlyphHeight;
        glyph.color = kEntryColour;
        glyph.blendMode = kEntryBlendMode;
        glyph.sortBucket = kBucketGlyphs;
        submitted.push_back({kBucketGlyphs, {glyph}});
      }
    }

    // :46-51. `1 < state - 1U` keeps it off in states 1 and 2.
    const bool captionShown = static_cast<unsigned>(draw.state - 1) > 1u &&
                              draw.state != kEquipStateLeaveByReload &&
                              draw.state != kEquipStateNoSpells &&
                              draw.state != kEquipStateItemList && draw.state != 0xB;
    if (captionShown && !draw.caption.empty())
    {
      const int width = text::FUN_00238e68_measure(draw.caption, font, kCaptionCell);
      submitted.push_back(
          {kBucketText, text::FUN_00238608_layout(kCaptionRightEdge - width, kCaptionY,
                                                  draw.caption, kEntryColour, kCaptionCell,
                                                  kCaptionCell, font)});
    }

    std::vector<text::DialogueSprite> sprites;
    for (const int bucket : {kBucketBars, kBucketGlyphs, kBucketText})
    {
      for (auto group = submitted.rbegin(); group != submitted.rend(); ++group)
      {
        if (group->first == bucket)
        {
          sprites.insert(sprites.end(), group->second.begin(), group->second.end());
        }
      }
    }
    return sprites;
  }

} // namespace orphen::ported::scene
