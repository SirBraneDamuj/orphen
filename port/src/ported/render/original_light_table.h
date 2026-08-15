#pragma once

// The dynamic light table, `DAT_00343888`: sixteen slots of 0x14 bytes.
//
//   src/FUN_00266050.c  allocate, scanning from slot 0   (opcode 0xC0)
//   src/FUN_00266008.c  allocate, scanning from slot 3   (opcode 0xBF)
//   src/FUN_00263f28.c  the shared 0xBF/0xC0 handler
//   src/FUN_00264148.c  0xC2 alpha
//   src/FUN_00264190.c  0xC3 colour
//   src/FUN_00264218.c  0xC4 radius
//   src/FUN_00264298.c  0xC5 position
//   src/FUN_00264360.c  0xC6 position from an entity
//   src/FUN_002643f0.c  0xC7 release
//
// Slot layout, all five words of it:
//
//   +0x00 float x        \
//   +0x04 float y         > world position
//   +0x08 float z        /
//   +0x0C byte  r  +0x0D byte g  +0x0E byte b  +0x0F byte alpha
//   +0x10 float radius   0.0 means the slot is free
//
// **Radius is the allocator.** Both allocators scan for the first slot whose
// radius is 0.0 and 0xC7 releases a slot by writing 0.0 back, so the field is
// simultaneously the free-list marker and the light's extent. Everything else
// in the slot is left behind when a light is released, which is why a dump
// shows plausible positions and colours in slots nothing is using.
//
// The two allocators differ only in where they start: 0xC0 from slot 0 and 0xBF
// from slot 3. The dispatch-table notes call them "point" and "directional",
// but neither handler branches on anything afterwards -- the shared body writes
// the same fields either way. The only real difference is that 0xC0 can hand
// out 0..2 and 0xBF cannot.
//
// Every scale involved is the ordinary `kScriptCoordinateScale` of 100000:
// `fGpffff8d4c`, `fGpffff8d50`, `DAT_00352cc4` and `DAT_00352cc8` all hold
// 100000.0 (gp = 0x00359F70; resolving them against a guessed gp gives 0.4 and
// 0.2, which is how to tell the base is wrong).
//
// ---- What consumes it, and why the port does not yet -----------------------
//
// `FUN_0020b430` walks all sixteen slots and, for each with a non-zero radius,
// uploads position, `1/r^2` and `colour/255` into the per-draw VU scratchpad;
// `FUN_0020eec0` resolves the three nearest to per-entity directional lights in
// VU1 slots 1..3. See scene_lighting.h, which documents that path in full.
//
// The port models the table but not the falloff. That is not laziness about the
// visible result: `eeMemory.bin`, captured during s01_e012's opening, has all
// sixteen radii at exactly 0.0, and so does `s01_e24.bin`. The real game is
// running with every dynamic light disabled in both scenes examined, so there
// is nothing to reproduce yet -- and the table is here so the port can *say*
// that rather than assume it. `--scr-report` prints any slot a scene ever makes
// live.

#include <array>
#include <cstdint>
#include <cstddef>

namespace orphen::ported::render
{

  class LightTable
  {
  public:
    static constexpr std::size_t kSlotCount = 16;
    // FUN_00266008's scan base. Slots below it are reachable only through 0xC0.
    static constexpr std::size_t kHighAllocationBase = 3;

    struct Slot
    {
      float x = 0.0f;
      float y = 0.0f;
      float z = 0.0f;
      std::uint8_t red = 0;
      std::uint8_t green = 0;
      std::uint8_t blue = 0;
      std::uint8_t alpha = 0;
      float radius = 0.0f; // 0 == free
    };

    // FUN_00266050 / FUN_00266008. -1 when the table is full.
    std::int32_t FUN_00266050_allocateFromZero() const { return allocateFrom(0); }
    std::int32_t FUN_00266008_allocateFromThree() const { return allocateFrom(kHighAllocationBase); }

    bool valid(std::uint32_t slot) const { return slot < kSlotCount; }
    Slot &slot(std::uint32_t index) { return slots_[index < kSlotCount ? index : 0]; }
    const Slot &slot(std::uint32_t index) const { return slots_[index < kSlotCount ? index : 0]; }

    // The high-water mark of what a scene actually lit, for the report. A slot
    // that is written but never given a radius contributes nothing on hardware
    // either, so this is the number that says whether the falloff is worth
    // porting.
    void noteRadius(std::uint32_t index, float radius);
    std::uint32_t everLiveMask() const { return everLive_; }
    float peakRadius() const { return peakRadius_; }

    void reset();

  private:
    std::int32_t allocateFrom(std::size_t first) const;

    std::array<Slot, kSlotCount> slots_{};
    std::uint32_t everLive_ = 0;
    float peakRadius_ = 0.0f;
  };

} // namespace orphen::ported::render
