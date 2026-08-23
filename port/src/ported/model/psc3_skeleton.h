#pragma once

// Bone sampling and matrix composition for PSC3 models.
//
//   src/FUN_0020c810.c  per-entity setup; walks the root bone list
//   src/FUN_0020d378.c  samples one bone at the entity's current pose column
//   src/FUN_0020da68.c  the same sampler standalone, easier to read
//   src/FUN_0020d618.c  composes the bone's matrix and recurses into children
//   src/FUN_0020cf28.c  builds a local matrix from the sampled fields
//   src/FUN_0020d188.c  reads one field, with interpolation
//
// **The seven sampled fields are not a quaternion.** An earlier reading of this
// path called them one; FUN_0020cf28 settles it by what it does with them:
//
//   +0x174..+0x17C  translation, s16 / 2048   -> FUN_0020bb48, matrix row 3
//   +0x180          uniform scale, s16 / 4096 -> FUN_0020bb38, the diagonal
//   +0x184          rotation about X, s16 / DAT_00352060 -> FUN_0020ba30
//   +0x188          rotation about Y                     -> FUN_0020ba88
//   +0x18C          rotation about Z                     -> FUN_0020bae0
//
// Two independent things confirm it. FUN_0020d188 takes a mode flag per field
// and only fields 4, 5 and 6 get mode 1, which routes through FUN_002166e8 --
// the signed *angle* difference -- and wraps at +-pi; the others interpolate
// linearly. And the data reads as round numbers: the scale field is 4096 (=1.0)
// on almost every key, and the rotation fields land on 21845, -10922 and -32768,
// which over DAT_00352060 are exactly 120, -60 and -180 degrees. That constant
// is 10430.380859 in both available EE dumps, i.e. s16 full scale maps to +-pi.
//
// Composition order, from FUN_0020cf28's param_10 == 0 branch, which is the one
// FUN_0020d618 uses. Each step multiplies on the right (row-vector convention,
// matching the port's view matrices), and the angles are negated:
//
//   local = Scale * RotZ(-rz) * RotX(-rx) * RotY(-ry) * Translate(t)
//   world = local * parentWorld
//
// FUN_0020d378 checks the scripted bone override table at DAT_004a7e00 before
// it looks at the keyframe track at all, so a bone can be driven directly by a
// behavior or a script opcode instead of by its animation. That path is here
// too; see EntityBoneOverrides below.
//
// FUN_0020d188 is the other half of the animation, and it explains why the EE
// dump's bone matrices never reconciled with a straight compose of the sampled
// keys -- the drawn pose is filtered, not raw. It is *two* filters per field,
// both stateful per entity, per bone, per field:
//
//   stage 1, keyframe blend
//     target = stored_target + (sampled - stored_target) * entity+0x13C
//     ...skipped entirely when +0x13C is 1.0, which snaps
//     store target back
//
//   stage 2, temporal smoothing
//     out = stored_out + (target - stored_out) * ctx+0x1CC
//     iterated once per 0x20 of DAT_003555bc, starting at 0x10, and skipped
//     when DAT_003555bc <= 0x10
//     store out back
//
// Fields 4, 5 and 6 (the rotations) take the angle path in both stages, going
// through FUN_002166e8 for the signed difference and wrapping at +-pi; fields
// 0..3 interpolate linearly. That per-field mode flag is what identified the
// rotations in the first place.
//
// **The state lives beside the matrix palette**, and FUN_0020c5a8 lines 74-75
// give both banks at once:
//
//   ctx+0x154 = 0x003FFE00 + slot * 0xA80   filter state
//   ctx+0x158 = 0x00357E00 + slot * 0xA80   the matrix palette
//
// so both are indexed by pool slot, 256 slots each, packed end to end
// (0x357E00 + 256*0xA80 == 0x3FFE00, and 0x3FFE00 + 256*0xA80 == 0x4A7E00,
// where the scripted override table begins). 0xA80 / 0x40 = 42 bones, and
// FUN_0020d378's ctx+0x164 = ctx+0x154 + bone*0x40 splits each bone's block
// into seven smoothed values at +0x00 and seven targets at +0x20.
//
// **The previous pose column, entity +0xAE, is not part of this.** An earlier
// reading had the sampler blending between +0xAE and +0xAC; FUN_0020d378
// line 63 indexes the track by +0xAC alone, and the stored target is what
// stands in for where the pose came from. +0x13C is not elapsed/total either --
// FUN_00225c90 line 132 makes it frameTicks/remaining, so the blend rate rises
// as the keyframe's deadline approaches and reaches exactly 1.0 on the last
// frame. It is a reach-the-target-by-the-deadline filter, not a lerp.

#include "ported/model/psc3_model.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace orphen::ported::model
{

  // Row-major, row-vector convention (v' = v * M), translation in row 3 --
  // the same layout ported/render/original_view_projection.h uses.
  using Matrix4 = std::array<float, 16>;

  Matrix4 identityMatrix();
  Matrix4 multiply(const Matrix4 &left, const Matrix4 &right);
  Vec3 transformPoint(const Vec3 &point, const Matrix4 &matrix);

  // s16 / DAT_00352060. Read out of s01_e24.bin and eeMemory.bin, identical in
  // both, so it is a constant rather than per-scene state.
  inline constexpr float kDAT_00352060_angleScale = 10430.380859375f;
  inline constexpr float kTranslationScale = 1.0f / 2048.0f;
  inline constexpr float kScaleScale = 1.0f / 4096.0f;
  // The first halfword of a key being 0x7FFF means "this bone has no transform
  // at this column"; FUN_0020d378 stores the 999.0 sentinel and FUN_0020d618
  // falls back to the parent.
  inline constexpr std::uint16_t kKeySentinel = 0x7FFF;
  inline constexpr std::uint16_t kNoKey = 0xFFFF;

  struct BonePose
  {
    Vec3 translation{};
    float scale = 1.0f;
    Vec3 rotationRadians{};
    // False when the bone has no track, no key, or the 0x7FFF sentinel. The
    // original inherits the parent's matrix unchanged in that case.
    bool posed = false;
  };

  // FUN_0020d378 without the scripted-override branch or the smoothing.
  // The raw key pair FUN_0020d378 reads for one bone at one column, for
  // diagnostics: 0xFFFF is "no key", 0x7FFF at the placement target is the
  // unposed marker, and a track that runs off the blob reads as neither.
  struct BoneKeyProbe
  {
    std::uint32_t trackOffset = 0;
    std::uint16_t placementKey = 0;
    std::uint16_t rotationKey = 0;
    bool trackInRange = false;
    bool sentinel = false;
    Vec3 rotationRadians{};
    Vec3 translation{};
    float scale = 1.0f;
  };
  BoneKeyProbe FUN_0020d378_probe_bone(const Psc3Model &model,
                                       std::span<const std::uint8_t> blob,
                                       std::size_t boneIndex,
                                       std::uint16_t poseColumn);

  BonePose FUN_0020d378_sample_bone(const Psc3Model &model,
                                    std::span<const std::uint8_t> blob,
                                    std::size_t boneIndex,
                                    std::uint16_t poseColumn);

  // FUN_0020cf28 has two rotation orders, selected by its param_10. The bone
  // path (FUN_0020d618) passes 0; the entity root (FUN_0020cdc0) passes 1.
  enum class ComposeOrder
  {
    ZXY, // param_10 == 0
    XYZ, // param_10 == 1
  };

  Matrix4 FUN_0020cf28_compose(const Vec3 &translation,
                               float scaleXY,
                               float scaleZ,
                               const Vec3 &rotationRadians,
                               ComposeOrder order);

  inline Matrix4 FUN_0020cf28_compose_local(const BonePose &pose)
  {
    return FUN_0020cf28_compose(pose.translation, pose.scale, pose.scale, pose.rotationRadians,
                                ComposeOrder::ZXY);
  }

  // fGpffff80c8, at 0x00352038. FUN_0020cdc0 adds it to entity +0x5C before
  // building the root matrix, so a model stands a quarter turn off its facing
  // angle -- entity facing is measured from +X, the mesh is authored along +Y.
  //
  // This was a guessed constant until the gp base was pinned down: fGpffff80fc
  // is 0x0035206c, not the 0x00352060 that holds an identical copy of the same
  // 10430.380859, and being 12 bytes out put fGpffff80c8 on an unrelated 0.9.
  inline constexpr float kfGpffff80c8_modelFacingBias = 1.570796013f;

  // fGpffff80cc, at 0x0035203C. The same quarter turn as fGpffff80c8, applied
  // by FUN_0020cdc0's *attached* branch instead. Two separate globals holding
  // the same 1.5707960 in the shipped executable.
  inline constexpr float kfGpffff80cc_attachedFacingBias = 1.570796013f;

  // FUN_0020cdc0's first branch: the entity's own world matrix.
  Matrix4 FUN_0020cdc0_entity_root(const Vec3 &position,
                                   float facingRadians,
                                   float rotationX154,
                                   float rotationY158,
                                   float scaleXY14c,
                                   float scaleZ150);

  // FUN_0020dc88: a point in a parent bone's space, in world space. `bone` is
  // already the positive bone index -- FUN_0020cdc0 negates the signed byte at
  // entity +0x194 before it gets here. An empty palette stands for the original's
  // "entity +0x0C has no 0x2000 bit" case and falls back to the parent's own
  // world position.
  Vec3 FUN_0020dc88_bone_point(std::span<const Matrix4> parentPalette,
                               std::size_t bone,
                               const Vec3 &localOffset,
                               const Vec3 &parentFallbackPosition);

  // FUN_0020cdc0's second branch, taken when entity +0x192 names a parent slot
  // and the bone byte at +0x194 is *negative*. The attached entity rides the
  // named bone's position and keeps its own facing.
  //
  // For the player's bandana, `boneLocalOffset` is the entity's own +0x20..+0x28
  // -- which for a parented entity is a bone-local offset, not a world position.
  Matrix4 FUN_0020cdc0_attached_root(std::span<const Matrix4> parentPalette,
                                     std::size_t parentBone,
                                     const Vec3 &boneLocalOffset,
                                     const Vec3 &parentFallbackPosition,
                                     float facingRadians,
                                     float rotationX154,
                                     float rotationY158,
                                     float scaleXY14c,
                                     float scaleZ150);

  // FUN_0020cdc0's third branch: +0x192 names a parent and the bone byte at
  // +0x194 is *non-negative*. Rigid attachment -- the entity's own local matrix
  // is concatenated with the parent's bone matrix, so it inherits the bone's
  // orientation as well as its position.
  //
  // This is how a character's head is a separate entity. Opcode 0x140/0x141
  // parents a head model to the neck bone and hides the body's own head bones,
  // and the head then carries the face animation (mouth, eyes) on its own
  // animation channel. In s01_e012 the real game runs four of these -- Orphen's
  // head on his bone 32, a second layer on the head's bone 1, and one each for
  // two party members.
  //
  // Two differences from the second branch, both the original's:
  //   - no facing bias. Branches one and two add fGpffff80c8 / fGpffff80cc to
  //     +0x5C; this one passes +0x5C straight through, because the quarter turn
  //     is already in the bone matrix it multiplies by.
  //   - the offset is used as the local translation before the concatenation,
  //     not resolved through FUN_0020dc88 first.
  //
  // The original reads 0x357E00 + parentSlot * 0xA80 + bone * 0x40 with no
  // liveness test; when the parent has no palette this frame it is reading
  // whatever the slot held last. The port has nothing to read there, so an
  // absent or out-of-range bone yields the local matrix alone.
  Matrix4 FUN_0020cdc0_rigid_attached_root(std::span<const Matrix4> parentPalette,
                                           std::size_t parentBone,
                                           const Vec3 &boneLocalOffset,
                                           float facingRadians,
                                           float rotationX154,
                                           float rotationY158,
                                           float scaleXY14c,
                                           float scaleZ150);

  // DAT_00352188 / DAT_0035218c, read out of s01_e24.bin. The game's pi is
  // 3.141592025756836, a hair short of the real one, and every wrap in the pose
  // path uses this pair rather than a computed constant.
  inline constexpr float kDAT_00352188_pi = 3.141592025756836f;
  inline constexpr float kDAT_0035218c_twoPi = 6.283184051513672f;
  // DAT_0035205c / DAT_00352064 / DAT_00352068: three copies of 999.0, the
  // "this bone has no transform" marker.
  inline constexpr float kDAT_00352068_unposed = 999.0f;

  // FUN_00216690: fold an angle back into [-pi, pi], at most 16 times. The
  // decompiler types both this and FUN_002166e8 as void; the value comes back
  // in $f0 and every caller uses it as a float.
  float FUN_00216690_wrap_angle(float angle);

  // FUN_002166e8: the signed difference from `from` to `to`, wrapped.
  inline float FUN_002166e8_angle_delta(float from, float to)
  {
    return FUN_00216690_wrap_angle(to - from);
  }

  // 0xA80 / 0x40. A model with more bones than this cannot be posed by the
  // original at all -- it would run into the next slot's block.
  inline constexpr std::size_t kMaxFilteredBones = 42;
  inline constexpr std::size_t kPoseFieldCount = 7;

  // One bone's 0x40 block. Field order is the ctx+0x174 order, which is what
  // FUN_0020d188's field index means: 0..2 translation, 3 scale, 4..6 rotation.
  struct BoneFilterState
  {
    std::array<float, kPoseFieldCount> smoothed{}; // +0x00
    std::array<float, kPoseFieldCount> target{};   // +0x20
    // Not in the original. Its bank is 688 KB of BSS that no map load clears,
    // so a freshly drawn entity filters out of whatever the previous occupant
    // of its slot left behind -- stale, but never far from a plausible pose.
    // Starting from zeroes instead would collapse a model to a point and grow
    // it back over the first keyframe, so the port snaps the first sample.
    bool seeded = false;
  };

  // One entity's 0xA80 block, the port's stand-in for 0x003FFE00 + slot*0xA80.
  struct EntityPoseFilter
  {
    std::array<BoneFilterState, kMaxFilteredBones> bones{};
    void reset();
  };

  // The scripted bone override, DAT_004a7e00 + slot*0x540 + bone*0x20. Seven
  // pose fields and a countdown at +0x1C, and 0x540/0x20 is the same 42 bones.
  //
  // FUN_0020d378 checks this *before* the keyframe track: a bone with an active
  // override ignores its animation entirely. Six type 0x62 enemies in s01_e024
  // drive four bones each through it every frame, so it is not a corner case.
  struct BoneOverride
  {
    std::array<float, kPoseFieldCount> fields{};
    std::int32_t remainingTicks1c = 0;
  };

  // entity +0x168 and the DAT_004a7e00 block for one slot, kept together
  // because nothing in the port needs them apart. FUN_0020dc48's clear-all
  // walks 0x168..0x191, which is where the 42 comes from a third time.
  struct EntityBoneOverrides
  {
    // <0 hides the bone, 0 is normal, >0 takes the override.
    std::array<std::int8_t, kMaxFilteredBones> mode168{};
    std::array<BoneOverride, kMaxFilteredBones> overrides{};
    void reset();
  };

  // FUN_0020d8c0. `pose` is in the *caller's* order -- rotation xyz, then
  // translation xyz, then scale -- which is not the ctx+0x174 field order the
  // table stores. The shuffle is the original's, not a convenience.
  void FUN_0020d8c0_set_bone_override(EntityBoneOverrides &state,
                                      std::size_t bone,
                                      const std::array<float, kPoseFieldCount> &pose,
                                      int durationFrames);

  // FUN_0020d9c8 / FUN_0020dc38 / FUN_0020dc48. A negative bone clears all 42.
  void FUN_0020d9c8_clear_bone_override(EntityBoneOverrides &state, std::size_t bone);

  // FUN_0020eec0's matrix upload loop, which walks entity +0x168 one byte per
  // bone and writes sixteen zeros instead of the matrix when the byte is
  // negative -- every vertex on that bone lands on the same point, so nothing
  // it belongs to has any area.
  //
  // This is deliberately not folded into the palette build. The palette at
  // 0x00357E00 keeps the true matrix for a hidden bone, and it has to: opcode
  // 0x140/0x141 hides a character's head bones *and* attaches a replacement
  // head to one of them, so the attachment reads a bone that is hidden.
  void FUN_0020eec0_apply_hidden_bones(std::vector<Matrix4> &palette,
                                       const EntityBoneOverrides *overrides);
  void FUN_0020dc38_hide_bone(EntityBoneOverrides &state, std::size_t bone);
  void FUN_0020dc48_clear_bone(EntityBoneOverrides &state, int bone);

  // FUN_0020d968: 1 when no override, 0 while one is still running, 2 once its
  // countdown has expired.
  int FUN_0020d968_bone_override_status(const EntityBoneOverrides &state, std::size_t bone);

  // FUN_0020d9d8: read a bone's current smoothed pose back out, in the caller's
  // order. The read-modify-write partner of FUN_0020d8c0.
  std::array<float, kPoseFieldCount> FUN_0020d9d8_read_bone_pose(const EntityPoseFilter &filter,
                                                                 std::size_t bone);

  // What FUN_0020c810 and FUN_0020d378 hand the filter, gathered up because the
  // port has no render context struct to hang them off.
  struct PoseFilterInputs
  {
    float blendRatio1c8 = 1.0f;  // entity +0x13C, via FUN_0020d378
    float smoothRate1cc = 1.0f;  // FUN_0020c810 lines 129-139
    std::uint32_t frameTicks = 0x20; // DAT_003555bc
    bool skipSmoothing1fe = false;   // entity +0x08 bit 0x200, or bit 0x10
    // ctx+0x1FD, set with +0x1FE when the entity was culled last frame. It only
    // reaches the override path, where it forces the countdown to zero so a
    // reappearing entity snaps instead of easing in from a stale pose.
    bool wasCulled1fd = false;
  };

  // FUN_0020d188 for one field. Reads and writes `state`.
  float FUN_0020d188_filter_field(BoneFilterState &state,
                                  std::size_t field,
                                  float sampled,
                                  bool isAngle,
                                  const PoseFilterInputs &inputs);

  // FUN_0020c810 lines 129-139: byte +4 of the animation's 8-byte record is a
  // smoothing time constant in ticks. At or below 10 there is no smoothing;
  // above it the per-frame rate is 10/b, clamped to 1.0.
  float FUN_0020c810_smoothing_rate(const Psc3Model &model,
                                    std::span<const std::uint8_t> blob,
                                    std::uint16_t animationId);

  // FUN_0020c810's bone walk plus FUN_0020d618's recursion: one world matrix per
  // submesh, in the model's own bone order. Bones the traversal never reaches
  // keep `root`.
  //
  // The filtered overload is the real path. The one without a filter composes
  // the sampled keys directly, which is what the model report wants -- it asks
  // what a column looks like, not what an entity that has been standing there
  // for six frames looks like.
  std::vector<Matrix4> FUN_0020d618_build_palette(const Psc3Model &model,
                                                  std::span<const std::uint8_t> blob,
                                                  std::uint16_t poseColumn,
                                                  const Matrix4 &root);

  //
  // `overrides` may be null. When it is not, a bone whose +0x168 byte is
  // positive takes its pose from the override table rather than the keyframe
  // track, and one whose byte is negative gets the zero matrix FUN_0020eec0
  // writes for a hidden submesh -- every vertex collapses to a point, so
  // nothing rasterises.
  std::vector<Matrix4> FUN_0020d618_build_palette(const Psc3Model &model,
                                                  std::span<const std::uint8_t> blob,
                                                  std::uint16_t poseColumn,
                                                  const Matrix4 &root,
                                                  EntityPoseFilter &filter,
                                                  const PoseFilterInputs &inputs,
                                                  EntityBoneOverrides *overrides = nullptr);

  // How many pose columns a bone's track has, bounded by the blob. Used to keep
  // a column index in range while the timeline walk is not ported.
  std::size_t poseColumnCount(const Psc3Model &model, std::span<const std::uint8_t> blob);

  // The animation table at header +0x0C: 8-byte records, the first dword an
  // offset to a 6-byte-entry timeline. Returns the first entry's column, which
  // is the pose an entity holds until FUN_00225c90 starts advancing it.
  //
  // Verified against the EE dump: the chest's animation 4 timeline reads
  // [12, 0x8001, 0] and the chest's +0xAC is 12; the lead player's animation 1
  // reads [1, 6, 0] then [3, 60, 0] and its +0xAE / +0xAC are 1 and 3.
  std::uint16_t firstPoseColumnForAnimation(const Psc3Model &model,
                                            std::span<const std::uint8_t> blob,
                                            std::uint16_t animationId);

} // namespace orphen::ported::model
