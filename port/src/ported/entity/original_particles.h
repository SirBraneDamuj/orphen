#pragma once

// Native counterpart of the global particle pool at DAT_00355620:
//
//   src/FUN_002d3290.c  0x002d3290  clear the pool, drop the behaviour
//   src/FUN_002d3218.c  0x002d3218  step and draw every particle
//   src/FUN_002d2348.c  0x002d2348  the spark behaviour
//   src/FUN_002d3058.c  0x002d3058  one particle's screen quad
//   src/FUN_002d2470.c  lines 120-165, the impact burst that fills the pool
//
// There is exactly one pool -- 0x18000 bytes at 0x01C69A00, so 1536 entries of
// 0x40 -- and exactly one behaviour installed at a time, through the function
// pointer DAT_00355e0c. FUN_002d3218 walks all 1536 every frame and calls the
// behaviour on each; the behaviour owns the particle's whole life, including
// deciding when it is free. A particle with +0x1C clear is free; that is the
// only thing the spawn loop looks at.
//
// Two behaviours exist. FUN_002d2348 is the spark shower the magic projectile
// installs when it detonates, and it is the one ported here. FUN_002d3320 is
// the ambient dust that FUN_002d36f8 installs, which re-seeds its own particles
// off the player's bones and is not ported.
//
// ---- verified against a save state ---------------------------------------
//
// A PCSX2 save state taken with the sparks live reads exactly 100 particles
// alive, all tagged with the projectile's pool slot, headings stepping by
// 0.0349066 rad, colours with R in {0, 0xFF}, G and B in [0xC0, 0xFF] and alpha
// 0xE0. Every field below was confirmed against it.

#include "ported/entity/original_entity.h"
#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstdint>
#include <functional>

namespace orphen::ported::entity
{

  // One entry of DAT_00355620. Offsets are the entry's own; the fields the two
  // shipped behaviours never touch are left out, and +0x0C is genuinely unused.
  struct OriginalParticle
  {
    float x00 = 0.0f; // +0x00, world position -- the same axes an entity's +0x20 uses
    float y04 = 0.0f; // +0x04
    float z08 = 0.0f; // +0x08

    // +0x10. Vertical velocity, integrated against +0x24 as a ballistic arc.
    float velocityZ10 = 0.0f;
    // +0x14. Horizontal speed along +0x28, bled off by DAT_00354678 a frame
    // until it reaches that value.
    float speed14 = 0.0f;
    // +0x18. GS RGBA, so byte 0 is red and byte 3 is alpha where 0x80 is 1.0.
    // The spark behaviour fades the alpha byte to kill the particle.
    std::uint32_t colour18 = 0;
    // +0x1C. Non-zero means alive. The spawn loop tests this and nothing else.
    std::int16_t alive1c = 0;
    // +0x1E / +0x20. The quad's width and height in units of 40 and 20 GS
    // 12.4 units at unit depth -- so a particle is one to three pixels across
    // and never scales with anything but distance. Both get the same value.
    std::int16_t widthUnits1e = 0;
    std::int16_t heightUnits20 = 0;
    // +0x22. Ticks left before the alpha starts falling. 32 ticks to a frame,
    // and it is allowed to go negative.
    std::int16_t timer22 = 0;
    float gravity24 = 0.0f; // +0x24
    float heading28 = 0.0f; // +0x28, radians
    // +0x2C. The pool slot of the entity that spawned it. Written but never
    // read -- the original computes it with a division idiom off the entity
    // pointer, and it is only useful for exactly the kind of save-state check
    // this port was verified with.
    std::int16_t ownerSlot2c = 0;
  };

  // DAT_00355e0c. A function pointer in the original; an enum here because only
  // one of the two behaviours is ported and a pointer would suggest otherwise.
  enum class ParticleBehaviour
  {
    // The pointer is null. FUN_002d3218 returns immediately and nothing in the
    // pool moves or draws, however much of it is marked alive.
    None,
    // FUN_002d2348, installed by FUN_002d2470 when the magic projectile ends.
    FUN_002d2348_sparks,
  };

  class ParticlePool
  {
  public:
    // 0x18000 bytes of 0x40. The loop bound is a byte count in the original,
    // which is why it is written this way.
    static constexpr std::size_t kPoolBytes = 0x18000;
    static constexpr std::size_t kStride = 0x40;
    static constexpr std::size_t kCount = kPoolBytes / kStride; // 1536

    // FUN_002d3290. Zeroes every entry and gives each a small stagger in +0x22,
    // between -160 and +448 ticks. That stagger only matters to FUN_002d3320,
    // which uses it to spread its ambient dust out in time; the spark behaviour
    // overwrites +0x22 on spawn. Also drops the behaviour.
    void FUN_002d3290_reset(const std::function<std::uint32_t()> &random);

    // FUN_002d3218. Steps every particle through the installed behaviour. The
    // original draws here too; this port collects the quads separately at
    // publish time, which puts the same particles in the same order into the
    // same display-list bucket.
    void FUN_002d3218_step(std::uint32_t frameTicks);

    // FUN_002d2470:0x002d2818-0x002d29b0. The detonation burst: up to a hundred
    // particles seeded from the entity's position, fanning out from its facing
    // (+0x5C) plus DAT_00354690 and stepping DAT_0035469c per particle.
    //
    // The loop walks the pool from the start and only fills entries whose +0x1C
    // is clear, so a second burst while the first is still alive gets fewer than
    // a hundred. It stops at the end of the pool either way. Installs the spark
    // behaviour. Returns how many it actually seeded.
    std::size_t FUN_002d2470_spawn_impact_burst(const OriginalEntity &source,
                                                std::size_t sourceSlot,
                                                const std::function<std::uint32_t()> &random);

    ParticleBehaviour DAT_00355e0c_behaviour() const { return behaviour_; }
    const std::array<OriginalParticle, kCount> &particles() const { return particles_; }

    // Diagnostics: how many entries currently read alive.
    std::size_t aliveCount() const;

  private:
    std::array<OriginalParticle, kCount> particles_{};
    ParticleBehaviour behaviour_ = ParticleBehaviour::None;
  };

  // FUN_002d2348. One particle, one frame. Public because it is the whole of the
  // behaviour and worth being able to test on its own.
  void FUN_002d2348_spark_step(OriginalParticle &particle, std::uint32_t frameTicks);

} // namespace orphen::ported::entity
