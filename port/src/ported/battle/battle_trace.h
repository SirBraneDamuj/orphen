#pragma once

// What `--battle-report` prints.
//
// The battle module has no HUD in this slice, so this is the only way to see
// what it is doing. It answers the three questions the mask binding raises --
// which button fires which spell, what action byte the press produced, and what
// state the character went into -- and it names every state the run reached
// whose handler is not ported, the way `--actor-report` names an unported actor
// behaviour rather than silently doing nothing.
//
// It is a sampler, not a hook: PortRuntime hands it the control block and the
// entity once per frame and it records the frames on which something changed.
// Nothing here feeds back into the simulation, so `--battle-report` cannot move
// a `--frames N` run off its deterministic path.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orphen::ported::battle
{

  class BattleTrace
  {
  public:
    struct Sample
    {
      std::uint32_t frame = 0;
      std::uint8_t pendingAction = 0;
      std::uint8_t currentAction = 0;
      std::uint16_t state = 0;
      std::uint16_t charge = 0;
      std::int16_t target = -1;
      std::uint8_t selectedSlot = 0;
      // Entity +0xA0. For the kind-0 chain this is the whole story: 0x33, 0x30
      // and 0x31 are the three slashes, in the order a press walks them.
      std::uint16_t animation = 0;
      // The attack effect's +0x150 in thousandths -- the blade's length, which
      // is what a kind-0 charge grows.
      std::uint16_t bladeLength = 0;
      // The ground ring's +0x14C in thousandths (type 0x18F, DAT_0031DA8C): the
      // charge gauge under the caster's feet, which FUN_002d9b78 resizes.
      std::uint16_t ringScale = 0;
      // The shared hit effect's +0x14C in thousandths (type 0x1E3,
      // DAT_0031DAD0): **the blue targeting circle** an elemental spell grows
      // at its landing spot, placed every frame by FUN_002f1380. Without this
      // column the growth is invisible in a headless run.
      std::uint16_t hitEffectScale = 0;
      std::uint32_t heldPad = 0;
      // Entity +0x5C and the bearing to the control block's target, both in
      // whole degrees. FUN_00249610's face-the-target block is the only thing
      // that closes the gap between them, so a run where they stay apart while
      // a target is held is that block failing.
      std::int16_t facingDegrees = 0;
      std::int16_t targetBearingDegrees = 0;
      std::array<std::uint16_t, 5> cooldowns{};
    };

    void reset()
    {
      samples_.clear();
      stateHits_.clear();
      unportedStates_.clear();
      lastSample_.reset();
      battleStartedFrame_ = 0;
      partyBuiltFrame_ = 0;
    }

    void recordBattleBuilt(std::uint32_t frame)
    {
      if (partyBuiltFrame_ == 0)
      {
        partyBuiltFrame_ = frame;
      }
    }
    // A group-2 placement whose actor id is not in the encounter table. The
    // original has no such case -- the two tables ship together -- so this
    // firing means the port got one of them wrong.
    void recordUnboundBattleActor(std::uint8_t id) { unboundBattleActors_.push_back(id); }
    const std::vector<std::uint8_t> &unboundBattleActors() const { return unboundBattleActors_; }

    void recordBattleStarted(std::uint32_t frame)
    {
      if (battleStartedFrame_ == 0)
      {
        battleStartedFrame_ = frame;
      }
    }

    // One state dispatch. `ported` is false for a table entry this slice does
    // not implement, which is what turns into the "states reached but not
    // ported" list.
    void recordState(std::uint16_t state, bool ported)
    {
      ++stateHits_[state];
      if (!ported)
      {
        ++unportedStates_[state];
      }
    }

    // Called once per frame with the player's control block contents. Only a
    // change is kept, so a run that stands still costs one line.
    void sample(const Sample &sample)
    {
      if (!lastSample_.has_value() || changed(*lastSample_, sample))
      {
        samples_.push_back(sample);
        if (samples_.size() > kMaxSamples)
        {
          samples_.erase(samples_.begin());
        }
      }
      lastSample_ = sample;
    }

    const std::vector<Sample> &samples() const { return samples_; }
    const std::map<std::uint16_t, std::uint32_t> &stateHits() const { return stateHits_; }
    const std::map<std::uint16_t, std::uint32_t> &unportedStates() const { return unportedStates_; }
    std::uint32_t partyBuiltFrame() const { return partyBuiltFrame_; }
    std::uint32_t battleStartedFrame() const { return battleStartedFrame_; }

  private:
    static constexpr std::size_t kMaxSamples = 400;

    static bool changed(const Sample &a, const Sample &b)
    {
      return a.pendingAction != b.pendingAction || a.currentAction != b.currentAction ||
             a.state != b.state || a.charge != b.charge || a.target != b.target ||
             a.selectedSlot != b.selectedSlot || a.animation != b.animation ||
             a.bladeLength != b.bladeLength || a.cooldowns != b.cooldowns ||
             a.facingDegrees != b.facingDegrees ||
             a.targetBearingDegrees != b.targetBearingDegrees;
    }

    std::vector<Sample> samples_;
    std::map<std::uint16_t, std::uint32_t> stateHits_;
    std::map<std::uint16_t, std::uint32_t> unportedStates_;
    std::optional<Sample> lastSample_;
    std::uint32_t partyBuiltFrame_ = 0;
    std::uint32_t battleStartedFrame_ = 0;
    std::vector<std::uint8_t> unboundBattleActors_;
  };

} // namespace orphen::ported::battle
