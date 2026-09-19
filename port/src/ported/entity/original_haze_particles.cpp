#include "ported/entity/original_haze_particles.h"

#include "ported/render/original_view_projection.h"

#include <cmath>

namespace orphen::ported::entity
{
  namespace
  {
    using orphen::ported::psm2::Vec3;
    using orphen::ported::render::Matrix4;

    // fGpffff83d4 and fGpffff83d8 at 0x00352344 / 0x00352348, the two quarter
    // turns the camera-locked branch folds into the camera's pitch and yaw.
    // Byte for byte the pair FUN_0021EBE8 uses for a camera-relative fountain.
    inline constexpr float kFGpffff83d4_negHalfPi = -1.570796012878418f;
    inline constexpr float kFGpffff83d8_halfPi = 1.570796012878418f;
    // fGpffff83dc at 0x0035234C. The half turn added to the field's own angle.
    inline constexpr float kFGpffff83dc_pi = 3.1415920257568359f;
    // fGpffff83d0 at 0x00352340. How far under the entity's ground the field
    // is laid.
    inline constexpr float kFGpffff83d0_groundDrop = 0.69999998807907104f;
    // DAT_0035236C at 0x0035236C, a third copy of the authored full turn.
    inline constexpr float kDAT_0035236c_turn = 6.2831840515136719f;
    // DAT_0035235C / 60 / 64 / 68. Two limits written twice: the compare reads
    // one copy and the clamp writes the other, and both pairs hold the same
    // number.
    inline constexpr float kDAT_0035235c_cornerLimitX = 24000.0f;
    inline constexpr float kDAT_00352360_cornerLimitY = 48000.0f;
    // 32 ticks to a frame, the divisor every pool folds its per-tick rates by.
    inline constexpr float kTickScale = 0.03125f;

    // DAT_00315658's four ST pairs, as texels on the 256-wide sheet. Stored
    // normalised in the original and scaled by 4096 on the way into the packet,
    // which is 256 texels in GS 1/16 units.
    inline constexpr float kHazeTexels[4][2] = {
        {0.040625000372529f * 256.0f, 0.040625000372529f * 256.0f},
        {0.040625000372529f * 256.0f, 0.431250005960464f * 256.0f},
        {0.462500005960464f * 256.0f, 0.431250005960464f * 256.0f},
        {0.462500005960464f * 256.0f, 0.040625000372529f * 256.0f}};

    inline constexpr int kHazeTextureSlot = 0x21;
    // Packet halfword 0x0021. FUN_00207DE8 increments it on the way past -- an
    // untextured packet is 0xFFFF and becomes zero there -- so the GS sees
    // 0x22, which is slot 0x21 in the one-based resource numbering. The high
    // byte is the CLUT window, and it is zero: this field reads the sheet's low
    // nibbles through palette window 0, which for texture 0x19B is the same
    // image as the plain page.
    inline constexpr int kHazeClutBank = 0;
    // The RGB every record shares. The packet's alpha is the only thing that
    // differs between them.
    inline constexpr std::uint32_t kHazeColour = 0xF0F0F0u;
    // 0x10004080 into the packet's +0x0C. FUN_00207DE8 reads bit 0x4000 first,
    // so this is mode 1 -- the alpha blend, not the additive one.
    inline constexpr int kHazeBlendMode = 1;
    inline constexpr int kHazeDisplayListBucket = 0x1000;
    inline constexpr int kHazeFrontDisplayListBucket = 0x1005;

    std::uint32_t roll(const std::function<std::uint32_t()> &random)
    {
      return random ? random() : 0u;
    }

    // FUN_00218EB0, one point at a time. Row-vector, with the matrix's row 3
    // added as the translation.
    Vec3 FUN_00218eb0_transform(const Vec3 &point, const Matrix4 &matrix)
    {
      return {point.x * matrix.at(0, 0) + point.y * matrix.at(1, 0) +
                  point.z * matrix.at(2, 0) + matrix.at(3, 0),
              point.x * matrix.at(0, 1) + point.y * matrix.at(1, 1) +
                  point.z * matrix.at(2, 1) + matrix.at(3, 1),
              point.x * matrix.at(0, 2) + point.y * matrix.at(1, 2) +
                  point.z * matrix.at(2, 2) + matrix.at(3, 2)};
    }
  } // namespace

  void HazeParticlePool::FUN_0021be58_reset()
  {
    for (HazeParticle &particle : particles_)
    {
      particle = HazeParticle{};
    }
    draws_.clear();
    live_ = 0;
    DAT_00354cb8_gate_ = false;
    // FUN_0021BE58 leaves every other global of this pool alone -- including
    // DAT_00315638, which is why the corner table is not touched here.
  }

  void HazeParticlePool::FUN_0021bd30_arm(float size, float speed, float angle,
                                          int count, std::int32_t magnitude, int entityIndex)
  {
    entityIndex_ = (entityIndex < 0 || entityIndex >= 0x100) ? -1 : entityIndex;
    cornerScale_ = size * 16.0f;
    DAT_00354cb8_gate_ = true;
    angle_ = angle;
    speed_ = speed;
    lifeSpread_ = 200;

    // The magnitude is kept as its absolute value and its sign is kept
    // separately, on each record that this call makes live.
    const bool front = magnitude < 0;
    magnitude_ = front ? -magnitude : magnitude;

    if (live_ < count)
    {
      // The growth walk, 0x0021BD98-0x0021BDE8.
      //
      // **It only ever gets halfway to the target.** The loop test reloads the
      // live count from gp and adds one before comparing, so it is
      // `added < count - (live + 1)` with `added` and `live` both rising a pass
      // -- and asking for seven from empty makes four live. A field that is
      // armed once a frame converges; one armed once does not.
      //
      // The scan cursor also does **not** restart at the base of the pool for
      // each record: it carries on from the slot it last filled, and the count
      // it guards with is occupied slots seen since this pass began rather than
      // slots visited overall.
      std::size_t cursor = 0;
      int added = 0;
      while (added < count - live_)
      {
        int scanned = 0;
        bool found = true;
        while (cursor < kCount && particles_[cursor].alive())
        {
          ++scanned;
          if (scanned > 99)
          {
            // A hundred occupied slots in a row. The original advances the
            // cursor once more on the way out and reports nothing free, however
            // the slot it lands on reads.
            ++cursor;
            found = false;
            break;
          }
          ++cursor;
        }
        if (cursor >= kCount)
        {
          // The original has no bound here at all -- it walks off the end of
          // the 0x960 block and reads whatever follows. Stopping is the one
          // deviation, and it needs a full pool to be reachable.
          found = false;
        }
        if (!found)
        {
          return;
        }
        particles_[cursor].alive14 = 1;
        particles_[cursor].front15 = front ? 1 : 0;
        ++live_;
        ++added;
      }
      return;
    }

    if (count < live_)
    {
      // The shrink walk frees `excess + 1` records, not `excess`: the test is
      // `excess < freed` and it is made **after** the free, so the pass that
      // reaches the excess still frees one more before it stops.
      const int excess = live_ - count;
      int freed = 0;
      for (std::size_t cursor = 0; cursor < kCount; ++cursor)
      {
        if (particles_[cursor].alive14 > 0)
        {
          particles_[cursor].alive14 = -1;
          ++freed;
          --live_;
        }
        if (excess < freed)
        {
          break;
        }
      }
    }
  }

  void HazeParticlePool::FUN_0021bef0_step(std::uint32_t frameTicks,
                                           const HazeCameraFrame &camera,
                                           const std::optional<HazeEntityAnchor> &anchor,
                                           const std::function<std::uint32_t()> &random)
  {
    namespace render = orphen::ported::render;

    draws_.clear();

    // FUN_0021BEF0:2. Nothing live, or the gate down, and not a single record
    // is stepped -- the countdowns stop where they are.
    if (live_ == 0 || !DAT_00354cb8_gate_)
    {
      return;
    }

    Vec3 origin{0.0f, 0.0f, 0.0f};
    if (!anchor.has_value())
    {
      // The camera-locked branch, and the one place DAT_00315638 is written.
      // The four y components become a symmetric pair so the sprite is centred
      // on its origin instead of standing on it, and they stay that way.
      DAT_00315638_corners_[1] = -5.0f;
      DAT_00315638_corners_[3] = 5.0f;
      DAT_00315638_corners_[5] = 5.0f;
      DAT_00315638_corners_[7] = -5.0f;

      Matrix4 pitch = render::FUN_0020bc38_identity();
      render::FUN_0020ba30_setRotationX(pitch,
                                        camera.fGpffffb6d8_pitch + kFGpffff83d4_negHalfPi);
      Matrix4 yaw = render::FUN_0020bc38_identity();
      render::FUN_0020bae0_setRotationZ(yaw, -camera.fGpffffb6d4_yaw - kFGpffff83d8_halfPi);
      Matrix4 place = render::FUN_0020bc38_identity();
      render::FUN_0020bb48_setTranslation(place, camera.DAT_0058c0a8_eyeX,
                                          camera.DAT_0058c0ac_eyeY, camera.DAT_0058c0b0_eyeZ);
      Matrix4 toWorld = render::FUN_0020bb58_multiply(pitch, yaw);
      toWorld = render::FUN_0020bb58_multiply(toWorld, place);

      // The magnitude is a distance straight down the camera's line of sight.
      origin = FUN_00218eb0_transform(
          Vec3{0.0f, 0.0f, static_cast<float>(magnitude_)}, toWorld);
    }
    else
    {
      origin = Vec3{anchor->positionX20, anchor->positionY24,
                    anchor->FUN_00227798_groundHeight - kFGpffff83d0_groundDrop};
    }

    // The field's own frame: a pitch of exactly zero, the angle plus a half
    // turn about Z, and the origin. The x rotation is a literal zero in the
    // original and is kept here because it is what makes the plane horizontal.
    Matrix4 fieldPitch = render::FUN_0020bc38_identity();
    render::FUN_0020ba30_setRotationX(fieldPitch, 0.0f);
    Matrix4 fieldYaw = render::FUN_0020bc38_identity();
    render::FUN_0020bae0_setRotationZ(fieldYaw, angle_ + kFGpffff83dc_pi);
    Matrix4 fieldPlace = render::FUN_0020bc38_identity();
    render::FUN_0020bb48_setTranslation(fieldPlace, origin.x, origin.y, origin.z);
    Matrix4 field = render::FUN_0020bb58_multiply(fieldPitch, fieldYaw);
    field = render::FUN_0020bb58_multiply(field, fieldPlace);

    // FUN_0021BEF0's one per-frame value, shared by every record: the drift in
    // world units for this frame.
    const float frameSpeed = speed_ * static_cast<float>(frameTicks) * kTickScale;
    const float driftX = std::cos(angle_);
    const float driftY = std::sin(angle_);

    for (HazeParticle &particle : particles_)
    {
      // FUN_0021C288:1. The walk covers all 100 records either way.
      if (!particle.alive())
      {
        continue;
      }

      // The countdown is a halfword subtract stored back as a halfword, and the
      // test is on the sign-extended result, so it expires on the frame it
      // reaches zero rather than the frame after.
      const auto stepped = static_cast<std::uint16_t>(
          particle.remaining10 - static_cast<std::uint16_t>(frameTicks));
      particle.remaining10 = stepped;

      if (static_cast<std::int16_t>(stepped) <= 0)
      {
        // Four rolls, in this order. Every divide is a `divu`, so each
        // remainder is already in range and none of them can come back
        // negative however the generator's word is signed.
        const float spawnAngle =
            (static_cast<float>(roll(random) % 360u) * kDAT_0035236c_turn) / 360.0f;
        float radius = static_cast<float>(roll(random) % 100u) / 100.0f;
        radius += static_cast<float>(
            roll(random) % static_cast<std::uint32_t>(magnitude_ != 0 ? magnitude_ : 1));
        const auto life = static_cast<std::int16_t>(
            (static_cast<int>(roll(random) %
                              static_cast<std::uint32_t>(lifeSpread_ != 0 ? lifeSpread_ : 1)) +
             10) *
            32);
        particle.total12 = static_cast<std::uint16_t>(life);
        particle.remaining10 = static_cast<std::uint16_t>(life);

        particle.x00 = radius * std::cos(spawnAngle);
        particle.y04 = radius * std::sin(spawnAngle);
        particle.z08 = 0.0f;
        // A respawn draws nothing on the frame it happens.
        continue;
      }

      particle.x00 += frameSpeed * driftX;
      particle.y04 += frameSpeed * driftY;

      const float ratio = static_cast<float>(static_cast<std::int16_t>(particle.remaining10)) /
                          static_cast<float>(static_cast<std::int16_t>(particle.total12));
      const float alpha = ratio <= 0.5f ? ratio * 255.0f : 255.0f - ratio * 255.0f;

      draws_.push_back(HazeParticleDraw{
          FUN_00218eb0_transform(Vec3{particle.x00, particle.y04, particle.z08}, field),
          static_cast<int>(alpha),
          particle.front15 != 0});
    }
  }

  orphen::ported::render::SpriteQuad FUN_0021c288_build_haze_quad(const HazeQuadInputs &inputs)
  {
    orphen::ported::render::SpriteQuad quad;

    const float viewZ = inputs.viewZ > orphen::ported::render::kDAT_0035209c_spriteNearClip
                            ? inputs.viewZ
                            : orphen::ported::render::kDAT_0035209c_spriteNearClip;
    const float q = 1.0f / viewZ;

    // The four corners are an axis-aligned rectangle, so corner 0 and corner 2
    // carry both extremes -- including the clamp, which only bites on the
    // positive side and so can make the rectangle asymmetric without making it
    // anything other than a rectangle.
    const auto cornerOffset = [&](int corner, int axis) {
      const float limit = axis == 0 ? kDAT_0035235c_cornerLimitX : kDAT_00352360_cornerLimitY;
      float scaled = inputs.corners[corner * 2 + axis] * inputs.cornerScale;
      if (limit < scaled)
      {
        scaled = limit;
      }
      return static_cast<int>(scaled * q);
    };

    const int gsX0 = inputs.gsOriginX + cornerOffset(0, 0);
    const int gsY0 = inputs.gsOriginY + cornerOffset(0, 1);
    const int gsX1 = inputs.gsOriginX + cornerOffset(2, 0);
    const int gsY1 = inputs.gsOriginY + cornerOffset(2, 1);

    const float perX = inputs.projectionScaleX != 0.0f ? viewZ / inputs.projectionScaleX : 0.0f;
    const float perY = inputs.projectionScaleY != 0.0f ? viewZ / inputs.projectionScaleY : 0.0f;

    quad.x0 = (static_cast<float>(gsX0) - inputs.screenCentreX) * perX;
    quad.x1 = (static_cast<float>(gsX1) - inputs.screenCentreX) * perX;
    quad.y0 = (static_cast<float>(gsY0) - inputs.screenCentreY) * perY;
    quad.y1 = (static_cast<float>(gsY1) - inputs.screenCentreY) * perY;
    quad.viewZ = viewZ;

    quad.u0 = kHazeTexels[0][0];
    quad.v0 = kHazeTexels[0][1];
    quad.u1 = kHazeTexels[2][0];
    quad.v1 = kHazeTexels[2][1];

    // The packet's texture halfword is non-zero, so FUN_00207DE8 halves all
    // four channels on the way past. A record is therefore never brighter than
    // 0x78 grey at half alpha, which is the whole reason a screen-sized sprite
    // reads as haze.
    const auto folded = [](std::uint32_t component)
    { return static_cast<float>((component & 0xFEu) >> 1) / 128.0f; };
    quad.colour[0] = folded(kHazeColour & 0xFFu);
    quad.colour[1] = folded((kHazeColour >> 8) & 0xFFu);
    quad.colour[2] = folded((kHazeColour >> 16) & 0xFFu);
    quad.colour[3] = folded(static_cast<std::uint32_t>(inputs.alpha) & 0xFFu);

    quad.blendMode = kHazeBlendMode;
    quad.textureSlot = kHazeTextureSlot;
    quad.clutBank = kHazeClutBank;
    quad.displayListBucket =
        inputs.front ? kHazeFrontDisplayListBucket : kHazeDisplayListBucket;
    // **The 0x1005 bucket does not mean "no depth test" here.** FUN_0020F510
    // pairs that bucket with a GS z of 0xFFFF, which is what puts a sprite in
    // front of the world; FUN_0021C288 writes the record's own projected z into
    // the packet either way (+0x0C bit 0x10000000 passes it through raw), so
    // the bucket only decides submission order and the quad still tests against
    // the depth buffer.
    quad.depthTest = true;
    return quad;
  }

} // namespace orphen::ported::entity
