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
// Not modelled here: FUN_0020d188's temporal smoothing, which filters each field
// toward its target over several frames rather than snapping, and the scripted
// bone override table at DAT_004a7e00 that FUN_0020d378 checks first. Both are
// per-frame concerns; this samples one column and composes it.

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
  BonePose FUN_0020d378_sample_bone(const Psc3Model &model,
                                    std::span<const std::uint8_t> blob,
                                    std::size_t boneIndex,
                                    std::uint16_t poseColumn);

  // FUN_0020cf28's param_10 == 0 branch.
  Matrix4 FUN_0020cf28_compose_local(const BonePose &pose);

  // FUN_0020c810's bone walk plus FUN_0020d618's recursion: one world matrix per
  // submesh, in the model's own bone order. Bones the traversal never reaches
  // keep `root`.
  std::vector<Matrix4> FUN_0020d618_build_palette(const Psc3Model &model,
                                                  std::span<const std::uint8_t> blob,
                                                  std::uint16_t poseColumn,
                                                  const Matrix4 &root);

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
