#pragma once

#include <cstdint>

namespace orphen::ported
{

  // DAT_003555bc (also read as iGpffffb64c): the engine's per-frame elapsed-time
  // tick count. FUN_002000c0 recomputes it every frame from the EE performance
  // counter:
  //
  //   DAT_003555bc = (PCR0 << 5) / 0x4b125c + 0x10 & 0xffffffe0;
  //   if ((int)DAT_003555bc < 0x20) DAT_003555bc = 0x20;
  //   if (0x80 < (int)DAT_003555bc) DAT_003555bc = 0x80;
  //
  // The `+ 0x10 & 0xffffffe0` rounds to the nearest multiple of 0x20, so the
  // value is elapsed time expressed in whole 60 Hz frames at 0x20 ticks per
  // frame, clamped to between one and four frames. It is 0x20 whenever the game
  // is holding 60 fps, and only grows when the original dropped frames.
  //
  // This stays a runtime parameter instead of being folded into the movement
  // constants because the original scales both axes by it:
  //
  //   horizontal  FUN_00256bb8:  FUN_00256ab0(iGpffffb64c * fGpffff8a4c * 0.03125, e)
  //   airborne    FUN_00253488:  FUN_00256ab0(DAT_003555bc * DAT_003555e8 * DAT_00352878)
  //   vertical    FUN_002262c0:  dt = (float)DAT_003555bc * 0.125
  //
  // The port drives its simulation from a fixed 60 Hz accumulator, so in
  // practice this is always kNominalFrameTicks. Keeping the parameter means a
  // later slice can reproduce the original's dropped-frame behavior exactly
  // rather than approximating it with extra sub-steps, which is not what the
  // original does -- it runs one longer update, and per-frame state counters
  // such as the entity +0xA8 substate frame still advance only once.
  constexpr std::uint32_t kNominalFrameTicks = 0x20;
  constexpr std::uint32_t kMinFrameTicks = 0x20;
  constexpr std::uint32_t kMaxFrameTicks = 0x80;

  // Seconds of simulated time one nominal frame represents.
  constexpr float kNominalFrameSeconds = 1.0f / 60.0f;

  // FUN_002262c0: the vertical integrator's timestep is the tick count scaled by
  // 0.125, giving 4.0 at the nominal 0x20 ticks.
  constexpr float kFrameTicksToPhysicsStep = 0.125f;

  // FUN_00256bb8: the horizontal impulse divides the tick count by 0x20, so a
  // nominal frame applies exactly one unscaled speed constant.
  constexpr float kFrameTicksToMovementScale = 0.03125f;

  inline float physicsStepForFrameTicks(std::uint32_t frameTicks)
  {
    return static_cast<float>(frameTicks) * kFrameTicksToPhysicsStep;
  }

  inline float movementScaleForFrameTicks(std::uint32_t frameTicks)
  {
    return static_cast<float>(frameTicks) * kFrameTicksToMovementScale;
  }

} // namespace orphen::ported
