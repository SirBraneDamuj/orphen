#include "ported/render/original_weapon_trail.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace orphen::ported::render
{
  namespace
  {
    using orphen::ported::model::Psc3Model;

    // FUN_00266460 keeps its two working arrays in the PS2 scratchpad at
    // DAT_70000000 without reserving any: the intervals at [0] and the divided
    // differences at [17]. With at most sixteen points that reaches index 31.
    constexpr std::size_t kSolveScratch = 34;

    float valueOf(const Vec3 &point, int axis)
    {
      return axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
    }

    void setAxis(Vec3 &point, int axis, float value)
    {
      if (axis == 0)
      {
        point.x = value;
      }
      else if (axis == 1)
      {
        point.y = value;
      }
      else
      {
        point.z = value;
      }
    }
  } // namespace

  // --------------------------------------------------------------------------
  // FUN_00266460. A natural cubic spline's tridiagonal solve, run in place over
  // the coefficient array. Both ends are pinned to zero before anything else,
  // which is the natural boundary condition and the reason the ribbon's tip
  // straightens out rather than curling.
  //
  // Ported as the original walks it, pointer for pointer. Two things about it
  // look wrong read quickly and are not: the divided differences at scratch[17]
  // are **overwritten** with the eliminated diagonal as the forward sweep goes,
  // so the array means two different things at two different times; and the
  // last row is reduced once before the back substitution and then again by the
  // substitution's first step, which is harmless only because the term it
  // subtracts is `coefficient[n-1]` and that was set to zero.
  void FUN_00266460_solve(int pointCount,
                          const float *knot,
                          const float *value,
                          float *coefficient)
  {
    if (pointCount < 3)
    {
      return;
    }
    std::array<float, kSolveScratch> scratch{};

    coefficient[0] = 0.0f;
    coefficient[pointCount - 1] = 0.0f;

    for (int index = 0; index < pointCount - 1; ++index)
    {
      const float interval = knot[index + 1] - knot[index];
      scratch[static_cast<std::size_t>(index)] = interval;
      scratch[static_cast<std::size_t>(index) + 17] = (value[index + 1] - value[index]) / interval;
    }

    const int last = pointCount - 2;
    coefficient[1] = (scratch[18] - scratch[17]) - scratch[0] * coefficient[0];
    scratch[17] = (knot[2] - knot[0]) + (knot[2] - knot[0]);

    for (int index = 1; index < last; ++index)
    {
      const float diagonal = scratch[static_cast<std::size_t>(index) + 16];
      const float interval = scratch[static_cast<std::size_t>(index)];
      coefficient[index + 1] = (scratch[static_cast<std::size_t>(index) + 18] -
                                scratch[static_cast<std::size_t>(index) + 17]) -
                               coefficient[index] * (interval / diagonal);
      scratch[static_cast<std::size_t>(index) + 17] =
          ((knot[index + 2] - knot[index]) + (knot[index + 2] - knot[index])) -
          interval * (interval / diagonal);
    }

    coefficient[pointCount - 2] -=
        scratch[static_cast<std::size_t>(last)] * coefficient[pointCount - 1];

    for (int index = last; index > 0; --index)
    {
      coefficient[index] = (coefficient[index] -
                            scratch[static_cast<std::size_t>(index)] * coefficient[index + 1]) /
                           scratch[static_cast<std::size_t>(index) + 16];
    }
  }

  // FUN_00266610. The last knot at or below `parameter`, floored at zero.
  int FUN_00266610_segmentFor(float parameter, int pointCount, const float *knot)
  {
    int high = pointCount - 1;
    int low = 0;
    int cursor = high;
    if (high > 0)
    {
      do
      {
        cursor = cursor / 2;
        if (knot[cursor] < parameter)
        {
          low = cursor + 1;
          cursor = high;
        }
        high = cursor;
        cursor = low + high;
      } while (low < high);
    }
    return low - (low > 0 ? 1 : 0);
  }

  // FUN_00266668. Kept in the original's grouping rather than expanded: its
  // stored coefficient is a third of the textbook second-derivative term, and
  // the cubic below is written to match, so the two are only right together.
  float FUN_00266668_evaluate(float parameter,
                              int segment,
                              const float *knot,
                              const float *value,
                              const float *coefficient)
  {
    const float here = coefficient[segment];
    const float offset = parameter - knot[segment];
    const float span = knot[segment + 1] - knot[segment];
    return ((((coefficient[segment + 1] - here) * offset) / span + here * 3.0f) * offset +
            ((value[segment + 1] - value[segment]) / span -
             (here + here + coefficient[segment + 1]) * span)) *
               offset +
           value[segment];
  }

  // FUN_00266a78 with its last argument zero: uniform knots over 0..1 rather
  // than the chord-length parameterisation the other branch builds. The
  // coefficients are only solved for three points or more; below that
  // FUN_00266ce8 answers without them.
  void FUN_00266a78_load(CubicSpline &spline, const Vec3 *points, int pointCount)
  {
    spline.pointCount = pointCount;
    for (int index = 0; index < pointCount; ++index)
    {
      for (int axis = 0; axis < 3; ++axis)
      {
        spline.value[static_cast<std::size_t>(axis)][static_cast<std::size_t>(index)] =
            valueOf(points[index], axis);
      }
    }
    if (pointCount <= 2)
    {
      return;
    }
    for (int index = 0; index < pointCount; ++index)
    {
      spline.knot[static_cast<std::size_t>(index)] =
          static_cast<float>(index) / static_cast<float>(pointCount - 1);
    }
    for (int axis = 0; axis < 3; ++axis)
    {
      FUN_00266460_solve(pointCount,
                         spline.knot.data(),
                         spline.value[static_cast<std::size_t>(axis)].data(),
                         spline.coefficient[static_cast<std::size_t>(axis)].data());
    }
  }

  // FUN_00266ce8.
  Vec3 FUN_00266ce8_sample(CubicSpline &spline, float parameter)
  {
    spline.segment = FUN_00266610_segmentFor(parameter, spline.pointCount, spline.knot.data());
    Vec3 out{};
    for (int axis = 0; axis < 3; ++axis)
    {
      const float *value = spline.value[static_cast<std::size_t>(axis)].data();
      if (spline.pointCount < 2)
      {
        setAxis(out, axis, value[0]);
      }
      else if (spline.pointCount == 2)
      {
        setAxis(out, axis, value[0] + (value[1] - value[0]) * parameter);
      }
      else
      {
        setAxis(out,
                axis,
                FUN_00266668_evaluate(parameter,
                                      spline.segment,
                                      spline.knot.data(),
                                      value,
                                      spline.coefficient[static_cast<std::size_t>(axis)].data()));
      }
    }
    return out;
  }

  // --------------------------------------------------------------------------
  // FUN_0020e760. The claim clears the sample count, not the release -- so a
  // slot handed straight back out starts empty however it was given up.
  int WeaponTrailPool::FUN_0020e760_claim()
  {
    for (std::size_t index = 0; index < weaponTrail::kSlotCount; ++index)
    {
      const std::uint32_t bit = 1u << index;
      if ((allocationMask_ & bit) == 0)
      {
        slot_[index].sampleCount = 0;
        allocationMask_ |= bit;
        return static_cast<int>(index) + 1;
      }
    }
    return 0;
  }

  // FUN_0020e7b0.
  void WeaponTrailPool::FUN_0020e7b0_release(int handle)
  {
    const auto index = static_cast<std::uint32_t>(handle - 1);
    if (index < weaponTrail::kSlotCount)
    {
      allocationMask_ &= ~(1u << index);
    }
  }

  void WeaponTrailPool::reset()
  {
    allocationMask_ = 0;
    for (auto &entry : slot_)
    {
      entry = TrailSlot{};
    }
  }

  std::size_t WeaponTrailPool::liveSlotCount() const
  {
    std::size_t live = 0;
    for (std::size_t index = 0; index < weaponTrail::kSlotCount; ++index)
    {
      if ((allocationMask_ & (1u << index)) != 0)
      {
        ++live;
      }
    }
    return live;
  }

  // --------------------------------------------------------------------------
  // FUN_0020e840.
  void WeaponTrailPool::FUN_0020e840_step(const Psc3Model &model,
                                          const std::vector<Matrix4> &bonePalette,
                                          std::uint16_t flagsAa,
                                          std::uint8_t &previousMask,
                                          std::array<std::uint8_t, 8> &handle,
                                          std::vector<TrailQuad> &out)
  {
    // `iVar9 == 0`: the model has no trail table, and nothing below runs --
    // including the release of any slot still held, which is why a model swap
    // mid-trail would leak one. Nothing in the game does that.
    if (model.trails.empty())
    {
      return;
    }
    const std::uint8_t previous = previousMask;
    if (flagsAa == 0 && previous == 0)
    {
      return;
    }

    for (std::size_t index = 0; index < 8; ++index)
    {
      const std::uint32_t bit = 1u << index;

      if ((flagsAa & bit) == 0)
      {
        // The bit is clear this frame: hand the slot back if we hold one. The
        // original leaves the byte at zero and the samples where they are.
        if (handle[index] != 0)
        {
          FUN_0020e7b0_release(handle[index]);
          handle[index] = 0;
        }
        continue;
      }

      int claimed = handle[index];
      if (claimed == 0)
      {
        claimed = FUN_0020e760_claim();
        if (claimed == 0)
        {
          // All 32 slots busy. The original draws nothing and tries again next
          // frame rather than dropping the bit.
          continue;
        }
        handle[index] = static_cast<std::uint8_t>(claimed);
      }
      else if (static_cast<std::uint32_t>(claimed - 1) >= weaponTrail::kSlotCount)
      {
        handle[index] = 0;
        continue;
      }

      TrailSlot &entry = slot_[static_cast<std::size_t>(claimed - 1)];

      // The bit went from clear to set this frame: start the history over, so a
      // second swing does not draw a ribbon back to where the first one ended.
      if ((previous & bit) == 0)
      {
        entry.sampleCount = 0;
      }

      if (index >= model.trails.size())
      {
        continue;
      }
      const auto &descriptor = model.trails[index];

      // The two edge vertices, skinned by their own bones. FUN_0020e840 reads
      // the same vertex stream and the same bone byte the draw does, and the
      // same matrix buffer at ctx+0x158.
      Vec3 sample[2]{};
      bool resolved = true;
      const std::int16_t edgeVertex[2] = {descriptor.vertexA, descriptor.vertexB};
      for (int edge = 0; edge < 2; ++edge)
      {
        const std::int16_t vertexIndex = edgeVertex[edge];
        if (vertexIndex < 0 ||
            static_cast<std::size_t>(vertexIndex) >= model.vertices.size())
        {
          // A descriptor no animation enables can hold -1 here, and the
          // original would read off the front of the vertex stream. Refusing is
          // the only sane answer and no live descriptor reaches it.
          resolved = false;
          break;
        }
        const auto &vertex = model.vertices[static_cast<std::size_t>(vertexIndex)];
        if (static_cast<std::size_t>(vertex.boneIndex) >= bonePalette.size())
        {
          resolved = false;
          break;
        }
        sample[edge] = orphen::ported::model::transformPoint(
            vertex.position, bonePalette[static_cast<std::size_t>(vertex.boneIndex)]);
      }
      if (!resolved)
      {
        continue;
      }

      // Shift the history down by one and put the new pair at the front. The
      // original stops the shift at fifteen so the drop off the end costs
      // nothing, and only then grows the count.
      for (std::size_t age = entry.sampleCount; age > 0; --age)
      {
        if (age < weaponTrail::kHistoryDepth)
        {
          entry.edgeA[age] = entry.edgeA[age - 1];
          entry.edgeB[age] = entry.edgeB[age - 1];
        }
      }
      if (entry.sampleCount < weaponTrail::kHistoryDepth)
      {
        ++entry.sampleCount;
      }
      entry.edgeA[0] = sample[0];
      entry.edgeB[0] = sample[1];

      // One sample is a point, not a ribbon.
      if (entry.sampleCount <= 1)
      {
        continue;
      }

      int length = descriptor.sampleCount;
      length = std::max(length, weaponTrail::kMinimumSampleCount);
      length = std::min(length, static_cast<int>(weaponTrail::kHistoryDepth));
      length = std::min(length, static_cast<int>(entry.sampleCount));

      std::array<std::array<Vec3, weaponTrail::kResampleCount>, 2> resampled{};
      for (int edge = 0; edge < 2; ++edge)
      {
        CubicSpline spline;
        FUN_00266a78_load(spline, edge == 0 ? entry.edgeA.data() : entry.edgeB.data(), length);
        for (std::size_t step = 0; step < weaponTrail::kResampleCount; ++step)
        {
          resampled[static_cast<std::size_t>(edge)][step] = FUN_00266ce8_sample(
              spline, static_cast<float>(step) / weaponTrail::kResampleDivisor);
        }
      }

      // FUN_00207de8's untextured colour fixup: the alpha loses its low bit and
      // is halved, and the rgb reaches the GS as authored.
      const std::uint32_t alpha = descriptor.colour >> 24;
      const float red = static_cast<float>(descriptor.colour & 0xFFu) / weaponTrail::kGsUnit;
      const float green = static_cast<float>((descriptor.colour >> 8) & 0xFFu) / weaponTrail::kGsUnit;
      const float blue = static_cast<float>((descriptor.colour >> 16) & 0xFFu) / weaponTrail::kGsUnit;
      const auto rampedAlpha = [&](std::uint32_t numerator) {
        const std::uint32_t ramped = (alpha * numerator) / weaponTrail::kResampleCount;
        return static_cast<float>((ramped & 0xFEu) >> 1) / weaponTrail::kGsUnit;
      };

      for (std::size_t quad = 0; quad < weaponTrail::kQuadCount; ++quad)
      {
        // The newer end of the strip, at `quad`, and the older one at
        // `quad + 1`. Corner order is the packet's: A, B, B', A'.
        TrailQuad emitted;
        emitted.corner[0] = resampled[0][quad];
        emitted.corner[1] = resampled[1][quad];
        emitted.corner[2] = resampled[1][quad + 1];
        emitted.corner[3] = resampled[0][quad + 1];

        const float nearAlpha =
            rampedAlpha(static_cast<std::uint32_t>(weaponTrail::kResampleCount - quad));
        const float farAlpha =
            rampedAlpha(static_cast<std::uint32_t>(weaponTrail::kResampleCount - 1 - quad));
        const std::array<float, 4> nearColour{red, green, blue, nearAlpha};
        const std::array<float, 4> farColour{red, green, blue, farAlpha};
        emitted.colour[0] = nearColour;
        emitted.colour[1] = nearColour;
        emitted.colour[2] = farColour;
        emitted.colour[3] = farColour;
        out.push_back(emitted);
      }
    }

    previousMask = static_cast<std::uint8_t>(flagsAa);
  }

} // namespace orphen::ported::render
