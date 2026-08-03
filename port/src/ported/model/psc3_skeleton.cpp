#include "ported/model/psc3_skeleton.h"

#include <cmath>

namespace orphen::ported::model
{
  namespace
  {
    std::uint16_t u16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint16_t>(bytes[offset]) |
             static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }

    std::int16_t s16At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int16_t>(u16At(bytes, offset));
    }

    std::uint32_t u32At(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    bool fits(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t needed)
    {
      return offset <= bytes.size() && needed <= bytes.size() - offset;
    }

    // FUN_0020ba30: rows 1 and 2.
    Matrix4 rotationX(float angle)
    {
      Matrix4 m = identityMatrix();
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      m[5] = c;
      m[6] = -s;
      m[9] = s;
      m[10] = c;
      return m;
    }

    // FUN_0020ba88: rows 0 and 2.
    Matrix4 rotationY(float angle)
    {
      Matrix4 m = identityMatrix();
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      m[0] = c;
      m[2] = s;
      m[8] = -s;
      m[10] = c;
      return m;
    }

    // FUN_0020bae0: rows 0 and 1.
    Matrix4 rotationZ(float angle)
    {
      Matrix4 m = identityMatrix();
      const float c = std::cos(angle);
      const float s = std::sin(angle);
      m[0] = c;
      m[1] = -s;
      m[4] = s;
      m[5] = c;
      return m;
    }

    // The bone's track: submesh +0x10, an offset from the model base to an array
    // of u32 keys, one per pose column.
    std::size_t boneTrackOffset(const Psc3Model &model, std::size_t boneIndex)
    {
      return boneIndex < model.submeshes.size() ? model.submeshes[boneIndex].sectionAOffset : 0;
    }
  } // namespace

  Matrix4 identityMatrix()
  {
    return Matrix4{1.0f, 0.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f,
                   0.0f, 0.0f, 0.0f, 1.0f};
  }

  Matrix4 multiply(const Matrix4 &left, const Matrix4 &right)
  {
    Matrix4 out{};
    for (std::size_t row = 0; row < 4; ++row)
    {
      for (std::size_t column = 0; column < 4; ++column)
      {
        float sum = 0.0f;
        for (std::size_t k = 0; k < 4; ++k)
        {
          sum += left[row * 4 + k] * right[k * 4 + column];
        }
        out[row * 4 + column] = sum;
      }
    }
    return out;
  }

  Vec3 transformPoint(const Vec3 &point, const Matrix4 &matrix)
  {
    return Vec3{point.x * matrix[0] + point.y * matrix[4] + point.z * matrix[8] + matrix[12],
                point.x * matrix[1] + point.y * matrix[5] + point.z * matrix[9] + matrix[13],
                point.x * matrix[2] + point.y * matrix[6] + point.z * matrix[10] + matrix[14]};
  }

  BonePose FUN_0020d378_sample_bone(const Psc3Model &model,
                                    std::span<const std::uint8_t> blob,
                                    std::size_t boneIndex,
                                    std::uint16_t poseColumn)
  {
    BonePose pose;

    const std::size_t track = boneTrackOffset(model, boneIndex);
    if (track == 0 || !fits(blob, track + static_cast<std::size_t>(poseColumn) * 4, 4))
    {
      return pose;
    }

    const std::uint32_t packed = u32At(blob, track + static_cast<std::size_t>(poseColumn) * 4);
    const std::uint16_t placementKey = static_cast<std::uint16_t>(packed & 0xFFFF);
    const std::uint16_t rotationKey = static_cast<std::uint16_t>(packed >> 16);

    const std::size_t pool = model.keyframePoolOffset;
    if (pool == 0)
    {
      return pose;
    }

    // Translation and scale. Four halfwords; the first being 0x7FFF marks the
    // whole bone as unposed, and FUN_0020d378 returns before reading rotation.
    if (placementKey != kNoKey)
    {
      const std::size_t at = pool + static_cast<std::size_t>(placementKey) * 2;
      if (!fits(blob, at, 8))
      {
        return pose;
      }
      if (u16At(blob, at) == kKeySentinel)
      {
        return pose;
      }
      pose.translation = Vec3{static_cast<float>(s16At(blob, at + 0)) * kTranslationScale,
                              static_cast<float>(s16At(blob, at + 2)) * kTranslationScale,
                              static_cast<float>(s16At(blob, at + 4)) * kTranslationScale};
      pose.scale = static_cast<float>(s16At(blob, at + 6)) * kScaleScale;
    }

    // Rotation. Three halfwords, absent means no rotation rather than no pose.
    if (rotationKey != kNoKey)
    {
      const std::size_t at = pool + static_cast<std::size_t>(rotationKey) * 2;
      if (fits(blob, at, 6))
      {
        pose.rotationRadians = Vec3{static_cast<float>(s16At(blob, at + 0)) / kDAT_00352060_angleScale,
                                    static_cast<float>(s16At(blob, at + 2)) / kDAT_00352060_angleScale,
                                    static_cast<float>(s16At(blob, at + 4)) / kDAT_00352060_angleScale};
      }
    }

    pose.posed = true;
    return pose;
  }

  Matrix4 FUN_0020cf28_compose(const Vec3 &translation,
                               float scaleXY,
                               float scaleZ,
                               const Vec3 &rotationRadians,
                               ComposeOrder order)
  {
    // FUN_0020bb38(param_1, param_1, param_2): x and y take the first scale,
    // z the second. Every rotation is applied negated.
    Matrix4 result = identityMatrix();
    result[0] = scaleXY;
    result[5] = scaleXY;
    result[10] = scaleZ;

    if (order == ComposeOrder::ZXY)
    {
      result = multiply(result, rotationZ(-rotationRadians.z));
      result = multiply(result, rotationX(-rotationRadians.x));
      result = multiply(result, rotationY(-rotationRadians.y));
    }
    else
    {
      result = multiply(result, rotationX(-rotationRadians.x));
      result = multiply(result, rotationY(-rotationRadians.y));
      result = multiply(result, rotationZ(-rotationRadians.z));
    }

    result[12] = translation.x;
    result[13] = translation.y;
    result[14] = translation.z;
    return result;
  }

  Matrix4 FUN_0020cdc0_entity_root(const Vec3 &position,
                                   float facingRadians,
                                   float rotationX154,
                                   float rotationY158,
                                   float scaleXY14c,
                                   float scaleZ150)
  {
    return FUN_0020cf28_compose(position, scaleXY14c, scaleZ150,
                                Vec3{rotationX154, rotationY158,
                                     facingRadians + kfGpffff80c8_modelFacingBias},
                                ComposeOrder::XYZ);
  }

  std::vector<Matrix4> FUN_0020d618_build_palette(const Psc3Model &model,
                                                  std::span<const std::uint8_t> blob,
                                                  std::uint16_t poseColumn,
                                                  const Matrix4 &root)
  {
    std::vector<Matrix4> palette(model.submeshes.size(), root);
    // boneOrder is the depth-first order the parse derived from the child lists,
    // so a parent is always composed before any child reads it.
    for (const std::uint8_t bone : model.boneOrder)
    {
      if (bone >= palette.size())
      {
        continue;
      }
      const int parent = model.submeshes[bone].parentIndex;
      const Matrix4 &parentMatrix =
          (parent >= 0 && static_cast<std::size_t>(parent) < palette.size())
              ? palette[static_cast<std::size_t>(parent)]
              : root;

      const BonePose pose = FUN_0020d378_sample_bone(model, blob, bone, poseColumn);
      if (!pose.posed)
      {
        // FUN_0020d618's "copy the parent" path.
        palette[bone] = parentMatrix;
        continue;
      }
      palette[bone] = multiply(FUN_0020cf28_compose_local(pose), parentMatrix);
    }
    return palette;
  }

  std::size_t poseColumnCount(const Psc3Model &model, std::span<const std::uint8_t> blob)
  {
    // Tracks are packed one after another, so the shortest distance from any
    // track to the next section start bounds every track's length. Taking the
    // minimum across bones is conservative and keeps a column index in range.
    std::size_t smallest = 0;
    for (std::size_t bone = 0; bone < model.submeshes.size(); ++bone)
    {
      const std::size_t track = boneTrackOffset(model, bone);
      if (track == 0 || track >= blob.size())
      {
        continue;
      }
      const std::size_t available = (blob.size() - track) / 4;
      if (smallest == 0 || available < smallest)
      {
        smallest = available;
      }
    }
    return smallest;
  }

  std::uint16_t firstPoseColumnForAnimation(const Psc3Model &model,
                                            std::span<const std::uint8_t> blob,
                                            std::uint16_t animationId)
  {
    const std::size_t table = model.animationTableOffset;
    if (table == 0)
    {
      return 0;
    }
    const std::size_t record = table + static_cast<std::size_t>(animationId) * 8;
    if (!fits(blob, record, 8))
    {
      return 0;
    }
    const std::uint32_t timeline = u32At(blob, record);
    if (timeline == 0 || !fits(blob, timeline, 2))
    {
      return 0;
    }
    return u16At(blob, timeline);
  }

} // namespace orphen::ported::model
