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

    // ctx+0x174 .. ctx+0x18C, which is the order FUN_0020d188's field index and
    // FUN_0020cf28's argument list both use.
    std::array<float, kPoseFieldCount> poseToFields(const BonePose &pose)
    {
      return {pose.translation.x, pose.translation.y, pose.translation.z, pose.scale,
              pose.rotationRadians.x, pose.rotationRadians.y, pose.rotationRadians.z};
    }

    BonePose fieldsToPose(const std::array<float, kPoseFieldCount> &fields)
    {
      BonePose pose;
      pose.translation = Vec3{fields[0], fields[1], fields[2]};
      pose.scale = fields[3];
      pose.rotationRadians = Vec3{fields[4], fields[5], fields[6]};
      pose.posed = true;
      return pose;
    }

    // FUN_0020d618 lines 31-37. A bone whose sample is the 999.0 marker does not
    // pop to its parent: the fields are reset to an identity transform and the
    // filter walks the bone back to it. Composing that identity and multiplying
    // by the parent is the same matrix the original's copy-the-parent fast path
    // produces, so only the transition differs, and only for the better.
    constexpr std::array<float, kPoseFieldCount> kIdentityFields{
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

    // The same transform in the caller's order: rotation, translation, scale.
    constexpr std::array<float, kPoseFieldCount> kIdentityCallerPose{
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    // FUN_0020d618 line 40-42: fields 4, 5 and 6 take the angle path.
    constexpr bool fieldIsAngle(std::size_t field) { return field >= 4; }
  } // namespace

  void EntityPoseFilter::reset()
  {
    for (auto &bone : bones)
    {
      bone = BoneFilterState{};
    }
  }

  float FUN_00216690_wrap_angle(float angle)
  {
    for (int iteration = 0; iteration < 16; ++iteration)
    {
      if (angle > kDAT_00352188_pi)
      {
        angle -= kDAT_0035218c_twoPi;
      }
      else if (angle >= -kDAT_00352188_pi)
      {
        return angle;
      }
      else
      {
        angle += kDAT_0035218c_twoPi;
      }
    }
    return angle;
  }

  float FUN_0020d188_filter_field(BoneFilterState &state,
                                  std::size_t field,
                                  float sampled,
                                  bool isAngle,
                                  const PoseFilterInputs &inputs)
  {
    if (!state.seeded)
    {
      state.target[field] = sampled;
      state.smoothed[field] = sampled;
      return sampled;
    }

    float target = state.target[field];
    const float previousOutput = state.smoothed[field];

    // Stage 1: blend the freshly sampled key into the stored target. Both 1.0
    // and 0.0 snap -- the original tests them separately but neither branch
    // touches the sampled value, so it is stored as-is. A ratio of 0 freezing
    // the pose forever is presumably what that guards against.
    if (target != sampled)
    {
      const float ratio = inputs.blendRatio1c8;
      if (ratio == 1.0f || ratio == 0.0f)
      {
        target = sampled;
      }
      else
      {
        if (!isAngle)
        {
          target = target + (sampled - target) * ratio;
        }
        else
        {
          target = target + FUN_002166e8_angle_delta(target, sampled) * ratio;
          // A single unrolled wrap, not FUN_00216690's loop: the step cannot
          // leave (-3pi, 3pi) so one correction is always enough.
          if (target > kDAT_00352188_pi)
          {
            target -= kDAT_0035218c_twoPi;
          }
          else if (target < -kDAT_00352188_pi)
          {
            target += kDAT_0035218c_twoPi;
          }
        }
      }
    }
    state.target[field] = target;

    // Stage 2: chase the target. Entities flagged +0x1FE skip it entirely and
    // take the target as the output; a short frame (DAT_003555bc at or below
    // 0x10) runs no iterations and holds the previous output instead. Those are
    // different outcomes and the original distinguishes them.
    float output = target;
    if (!inputs.skipSmoothing1fe)
    {
      if (inputs.frameTicks < 0x11u)
      {
        output = previousOutput;
      }
      else
      {
        float current = previousOutput;
        for (std::uint32_t step = 0x10; step < inputs.frameTicks; step += 0x20)
        {
          if (!isAngle)
          {
            current = current + (target - current) * inputs.smoothRate1cc;
          }
          else
          {
            current = current + FUN_002166e8_angle_delta(current, target) * inputs.smoothRate1cc;
            if (current > kDAT_00352188_pi)
            {
              current -= kDAT_0035218c_twoPi;
            }
            else if (current < -kDAT_00352188_pi)
            {
              current += kDAT_0035218c_twoPi;
            }
          }
        }
        output = current;
      }
    }

    state.smoothed[field] = output;
    return output;
  }

  void EntityBoneOverrides::reset()
  {
    mode168.fill(0);
    for (auto &entry : overrides)
    {
      entry = BoneOverride{};
    }
  }

  void FUN_0020d8c0_set_bone_override(EntityBoneOverrides &state,
                                      std::size_t bone,
                                      const std::array<float, kPoseFieldCount> &pose,
                                      int durationFrames)
  {
    if (bone >= kMaxFilteredBones)
    {
      return;
    }
    state.mode168[bone] = 1;
    BoneOverride &entry = state.overrides[bone];
    // The caller passes rotation first; the table stores translation first.
    entry.fields[0] = pose[3];
    entry.fields[1] = pose[4];
    entry.fields[2] = pose[5];
    entry.fields[3] = pose[6];
    entry.fields[4] = pose[0];
    entry.fields[5] = pose[1];
    entry.fields[6] = pose[2];
    entry.remainingTicks1c = durationFrames << 5;
  }

  void FUN_0020d9c8_clear_bone_override(EntityBoneOverrides &state, std::size_t bone)
  {
    if (bone < kMaxFilteredBones)
    {
      state.mode168[bone] = 0;
    }
  }

  void FUN_0020dc38_hide_bone(EntityBoneOverrides &state, std::size_t bone)
  {
    if (bone < kMaxFilteredBones)
    {
      state.mode168[bone] = -1; // 0xFF
    }
  }

  void FUN_0020eec0_apply_hidden_bones(std::vector<Matrix4> &palette,
                                       const EntityBoneOverrides *overrides)
  {
    if (overrides == nullptr)
    {
      return;
    }
    const std::size_t count = std::min(palette.size(), kMaxFilteredBones);
    for (std::size_t bone = 0; bone < count; ++bone)
    {
      if (overrides->mode168[bone] < 0)
      {
        palette[bone] = Matrix4{};
      }
    }
  }

  void FUN_0020dc48_clear_bone(EntityBoneOverrides &state, int bone)
  {
    if (bone >= 0)
    {
      FUN_0020d9c8_clear_bone_override(state, static_cast<std::size_t>(bone));
      return;
    }
    state.mode168.fill(0);
  }

  int FUN_0020d968_bone_override_status(const EntityBoneOverrides &state, std::size_t bone)
  {
    if (bone >= kMaxFilteredBones || state.mode168[bone] < 1)
    {
      return 1;
    }
    return state.overrides[bone].remainingTicks1c > 0 ? 0 : 2;
  }

  std::array<float, kPoseFieldCount> FUN_0020d9d8_read_bone_pose(const EntityPoseFilter &filter,
                                                                 std::size_t bone)
  {
    if (bone >= kMaxFilteredBones)
    {
      return kIdentityCallerPose;
    }
    const auto &smoothed = filter.bones[bone].smoothed;
    // Back into the caller's order, the inverse of FUN_0020d8c0's shuffle.
    return {smoothed[4], smoothed[5], smoothed[6],
            smoothed[0], smoothed[1], smoothed[2], smoothed[3]};
  }

  float FUN_0020c810_smoothing_rate(const Psc3Model &model,
                                    std::span<const std::uint8_t> blob,
                                    std::uint16_t animationId)
  {
    const std::size_t table = model.animationTableOffset;
    if (table == 0)
    {
      return 1.0f;
    }
    const std::size_t record = table + static_cast<std::size_t>(animationId) * 8;
    if (!fits(blob, record, 8))
    {
      return 1.0f;
    }
    const float ticks = static_cast<float>(blob[record + 4]);
    if (ticks <= 10.0f)
    {
      return 1.0f;
    }
    const float rate = 10.0f / ticks;
    return rate > 1.0f ? 1.0f : rate;
  }

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

  Vec3 FUN_0020dc88_bone_point(std::span<const Matrix4> parentPalette,
                               std::size_t bone,
                               const Vec3 &localOffset,
                               const Vec3 &parentFallbackPosition)
  {
    // FUN_0020dc88's `param_2 < 0 || param_2 == 0x29` clamp: a negative bone or
    // the 42nd asks for the last one.
    if (!parentPalette.empty() && (bone >= parentPalette.size() || bone == kMaxFilteredBones))
    {
      bone = parentPalette.size() - 1;
    }
    // The entity +0x0C bit 0x2000 test. In the original that bit means "this
    // entity has a matrix palette this frame"; the port has no palette when the
    // entity has no model, which is the same condition, so the fallback is the
    // parent's own world position.
    if (parentPalette.empty() || bone >= parentPalette.size())
    {
      return parentFallbackPosition;
    }
    return transformPoint(localOffset, parentPalette[bone]);
  }

  Matrix4 FUN_0020cdc0_attached_root(std::span<const Matrix4> parentPalette,
                                     std::size_t parentBone,
                                     const Vec3 &boneLocalOffset,
                                     const Vec3 &parentFallbackPosition,
                                     float facingRadians,
                                     float rotationX154,
                                     float rotationY158,
                                     float scaleXY14c,
                                     float scaleZ150)
  {
    const Vec3 anchor =
        FUN_0020dc88_bone_point(parentPalette, parentBone, boneLocalOffset, parentFallbackPosition);
    // The middle branch takes the bone's *position* and the entity's own facing.
    // It never picks up the bone's orientation -- that is the third branch, the
    // rigid one, which concatenates the parent's bone matrix instead.
    return FUN_0020cf28_compose(anchor, scaleXY14c, scaleZ150,
                                Vec3{rotationX154, rotationY158,
                                     facingRadians + kfGpffff80cc_attachedFacingBias},
                                ComposeOrder::XYZ);
  }

  Matrix4 FUN_0020cdc0_rigid_attached_root(std::span<const Matrix4> parentPalette,
                                           std::size_t parentBone,
                                           const Vec3 &boneLocalOffset,
                                           float facingRadians,
                                           float rotationX154,
                                           float rotationY158,
                                           float scaleXY14c,
                                           float scaleZ150)
  {
    // No facing bias here: the original's third branch passes +0x5C unmodified.
    const Matrix4 local =
        FUN_0020cf28_compose(boneLocalOffset, scaleXY14c, scaleZ150,
                             Vec3{rotationX154, rotationY158, facingRadians}, ComposeOrder::XYZ);
    if (parentPalette.empty() || parentBone >= parentPalette.size())
    {
      return local;
    }
    // FUN_0020bca8 then FUN_0020bb58: dest = local * the parent's bone matrix,
    // the same order FUN_0020d618 uses to hang a child bone off its parent.
    return multiply(local, parentPalette[parentBone]);
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

  std::vector<Matrix4> FUN_0020d618_build_palette(const Psc3Model &model,
                                                  std::span<const std::uint8_t> blob,
                                                  std::uint16_t poseColumn,
                                                  const Matrix4 &root,
                                                  EntityPoseFilter &filter,
                                                  const PoseFilterInputs &inputs,
                                                  EntityBoneOverrides *overrides)
  {
    std::vector<Matrix4> palette(model.submeshes.size(), root);
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

      // Past the bank's 42 blocks there is nowhere to keep the state, which is
      // also true of the original -- it would be writing into the next slot.
      // Falling back to the unfiltered compose at least draws the right pose.
      if (bone >= kMaxFilteredBones)
      {
        const BonePose pose = FUN_0020d378_sample_bone(model, blob, bone, poseColumn);
        palette[bone] = pose.posed ? multiply(FUN_0020cf28_compose_local(pose), parentMatrix)
                                   : parentMatrix;
        continue;
      }

      // A negative +0x168 is *not* handled here. FUN_0020c810 runs FUN_0020d378
      // and FUN_0020d618 over every bone in the root list without consulting
      // +0x168 at all; the hide is applied later, by FUN_0020eec0, on the way
      // from the palette to VU1. Keeping the composed matrix matters because
      // 0x00357E00 is what an attached entity reads to find the bone it rides --
      // and the bones a head is attached to are exactly the ones the same
      // opcode hides. See FUN_0020eec0_apply_hidden_bones.
      const std::int8_t mode = overrides != nullptr ? overrides->mode168[bone] : 0;

      BoneFilterState &state = filter.bones[bone];
      PoseFilterInputs boneInputs = inputs;
      std::array<float, kPoseFieldCount> fields{};

      if (mode > 0)
      {
        // FUN_0020d378's first branch. The override replaces the sampled key
        // outright, and its own countdown replaces entity +0x13C: 32 ticks over
        // whatever is left, so it converges on its deadline the same way a
        // keyframe does. A duration of 0 leaves the countdown negative, which
        // reads as a ratio of 1.0 -- an override rewritten every frame snaps,
        // which is what a procedural drive wants.
        BoneOverride &entry = overrides->overrides[bone];
        fields = entry.fields;
        std::int32_t remaining =
            entry.remainingTicks1c - static_cast<std::int32_t>(inputs.frameTicks);
        if (inputs.wasCulled1fd)
        {
          remaining = 0;
        }
        if (remaining < 1)
        {
          boneInputs.blendRatio1c8 = 1.0f;
        }
        else
        {
          const float ratio = 32.0f / static_cast<float>(remaining);
          boneInputs.blendRatio1c8 = ratio > 1.0f ? 1.0f : ratio;
        }
        entry.remainingTicks1c = remaining;
      }
      else
      {
        const BonePose sampled = FUN_0020d378_sample_bone(model, blob, bone, poseColumn);
        fields = sampled.posed ? poseToFields(sampled) : kIdentityFields;
      }

      // FUN_0020d618 lines 39-45 calls the filter in this order, scale first.
      // Order matters only in that it is the original's; the fields do not
      // interact.
      std::array<float, kPoseFieldCount> filtered{};
      for (const std::size_t field : {std::size_t{3}, std::size_t{4}, std::size_t{5},
                                      std::size_t{6}, std::size_t{0}, std::size_t{1},
                                      std::size_t{2}})
      {
        filtered[field] = FUN_0020d188_filter_field(state, field, fields[field],
                                                    fieldIsAngle(field), boneInputs);
      }
      state.seeded = true;

      palette[bone] = multiply(FUN_0020cf28_compose_local(fieldsToPose(filtered)), parentMatrix);
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
