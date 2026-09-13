#pragma once

// The stage a level-5 summon sets, and nothing about any particular summon.
//
// When a spell is released at full charge with a live target, the elemental
// launches hand over to a creature -- types 0x13F..0x142, and 0x13E for Bite of
// Lightning -- whose behaviour takes the whole scene over for its run. Five
// hundred lines of that are the creature; this is the part all five share:
//
//   src/FUN_002de4a8.c  freeze every live entity but the target cursors
//   src/FUN_002de500.c  release all of them again
//   src/FUN_002de548.c  release the ones that were just hit, and type 0x74
//   src/FUN_002de640.c  release one
//   src/FUN_002d6e20.c  build the set of entities to dim
//   src/FUN_002d6f38.c  take one entity back out of that set
//   src/FUN_002d6fa0.c  write one fade level across the whole set
//   src/FUN_002d7038.c  the flat veil the dimmed field sits behind
//
// == The freeze ==
//
// Bit 0x800 of +0x02 is the gate FUN_00239CE0 and FUN_002261E0 already skip on,
// so raising it on every slot stops every behaviour and every physics step at
// once. The battle's own pause (FUN_002DE5B8) is the same bit with three
// exemptions; the summon's has one -- type 0x192, the target cursors -- and the
// creature then hands the bit back to the handful of entities that have to keep
// running: itself, the player, the caster's ground ring and shield effect, and
// the shared hit effect.
//
// == The dim ==
//
// DAT_0058BB00 is a 256-bit mask, one bit per pool slot, of "entities that fade
// with the stage". FUN_002D6E20 fills it with every live slot from 2 up whose
// +0x96 has neither of its low two bits and whose +0x134 is already zero -- so
// anything mid-fade of its own is left alone -- and slots 0 and 1 are never in
// it. FUN_002D6F38 then takes the few that must stay lit back out. Every frame
// after that FUN_002D6FA0 stamps one level into +0x134 across the whole set,
// which is the same byte the model draw fades an entity out with.
//
// The level is DAT_00355700 / 100 counted off the same ramp that drives the
// map's global fade cap, so the map and everything standing on it darken
// together and the creature, the caster and the player do not.
//
// == The veil ==
//
// FUN_002D7038 draws one flat quad over the whole 640x448 virtual screen in
// bucket 2 -- under the world, not over it -- in a colour each summon picks and
// at `DAT_00355550 * 2` alpha. It is what the dimmed field recedes *into*.
// **Not ported**: the port has no path that submits a raw packet into a bucket
// that low, and the two halves that carry the effect -- the map's fade cap and
// the per-entity +0x134 -- are both here. Named at its call sites.

#include "ported/camera/original_camera_path.h"
#include "ported/entity/entity_pool.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <span>

namespace orphen::ported::entity
{

  using orphen::ported::psm2::Vec3;

  class SummonStage
  {
  public:
    // ---------------------------------------------------------- the freeze
    // FUN_002DE4A8: bit 0x800 on every live slot except type 0x192.
    static void FUN_002de4a8_freeze_field(EntityPool &pool);
    // FUN_002DE500: bit 0x800 off every live slot, no exemptions.
    static void FUN_002de500_release_field(EntityPool &pool);
    // FUN_002DE548: off every live slot carrying pending damage (+0xBE >= 1),
    // and off type 0x74 whether it was hit or not. The creature calls this on
    // the frame its damage pass goes off, so the victims can play their
    // reactions while the rest of the field is still held.
    static void FUN_002de548_release_hurt(EntityPool &pool);
    // FUN_002DE640: off one entity.
    static void FUN_002de640_release_one(OriginalEntity &entity);
    static void FUN_002de640_release_one(EntityPool &pool, std::int32_t slot);

    // ------------------------------------------------------------- the dim
    void FUN_002d6e20_build_dim_set(const EntityPool &pool);
    void FUN_002d6f38_exclude(std::int32_t slot);
    void FUN_002d6fa0_apply(EntityPool &pool, std::uint8_t level) const;

    // ---------------------------------------------------------- the camera
    // FUN_00266A78(0x326AC0, ...) and FUN_00266A78(0x326CC8, ...): the two
    // curves every summon but Bite of Lightning puts the camera on. Both are
    // in the creature's own frame -- FUN_00266CE8's sample is turned by the
    // creature's facing and added to its position -- so the same handful of
    // points reads the same way whichever way the caster happens to face.
    void FUN_00266a78_build_eye(std::span<const Vec3> points);
    void FUN_00266a78_build_look_at(std::span<const Vec3> points);
    Vec3 FUN_00266ce8_sample_eye(float t) const;
    Vec3 FUN_00266ce8_sample_look_at(float t) const;

    // ------------------------------------------------------------ the ramps
    // DAT_00355574/78 and DAT_00355576/7A: how long each curve takes and how
    // far along it is, both in the same units DAT_003555BC counts in. The
    // steppers return the sample parameter and clamp at the end, which is
    // where the camera parks.
    void armCurves(std::int16_t eyeDuration, std::int16_t lookAtDuration);
    bool eyeRunning() const { return DAT_00355578_eyeElapsed_ < DAT_00355574_eyeDuration_; }
    bool lookAtRunning() const
    {
      return DAT_0035557a_lookAtElapsed_ < DAT_00355576_lookAtDuration_;
    }
    float stepEye(std::uint32_t frameTicks);
    float stepLookAt(std::uint32_t frameTicks);

    // DAT_00355554, the creature's own fade, and DAT_0035554C, the stage's.
    // Both start at 0x319C and count in hundredths of a fade level.
    std::int32_t &DAT_00355554_creatureFade() { return DAT_00355554_creatureFade_; }
    std::int32_t &DAT_0035554c_stageFade() { return DAT_0035554c_stageFade_; }
    std::uint8_t &DAT_00355550_veilAlpha() { return DAT_00355550_veilAlpha_; }

  private:
    // DAT_0058BB00, eight words of it.
    std::array<std::uint32_t, 8> DAT_0058bb00_dimSet_{};

    std::array<orphen::ported::camera::CubicSpline, 3> eye_{};
    std::array<orphen::ported::camera::CubicSpline, 3> lookAt_{};

    std::int16_t DAT_00355574_eyeDuration_ = 0;
    std::int16_t DAT_00355578_eyeElapsed_ = 0;
    std::int16_t DAT_00355576_lookAtDuration_ = 0;
    std::int16_t DAT_0035557a_lookAtElapsed_ = 0;

    std::int32_t DAT_00355554_creatureFade_ = 0;
    std::int32_t DAT_0035554c_stageFade_ = 0;
    std::uint8_t DAT_00355550_veilAlpha_ = 0;
  };

  // The original's copy is a run of globals, so there is exactly one of these
  // and it outlives the creature that set it up. Two summons never overlap:
  // DAT_00354ECC is raised for the whole of one and every path that could start
  // another is frozen behind it.
  SummonStage &DAT_0058bb00_summonStage();

} // namespace orphen::ported::entity
