#pragma once

// --scenario <file>: a timed test script for the battle system.
//
// A scenario is harness input, not game code. It may only touch what a player
// or the game's own scripts could touch -- the pad, and a starting condition
// such as hit points -- and then lets the ported code run. It must never fake
// an outcome: no writing +0xBE, no forcing a state. `--damage` does exactly
// that (it stamps +0xBE and skips the guard arc), which is why it could not
// test a block; a scenario that did the same would be testing nothing.
//
// One directive per line, `#` to end of line is a comment:
//
//   hold <button> <first>-<last>    a held pad button, pressed on <first>
//   set <frame> <who> <field> <n>   write a starting condition on <frame>
//   expect <frames> <who> <field> <op> <n>
//                                   true on at least one frame of the range
//   expect-all <frames> <who> <field> <op> <n>
//                                   true on every frame of the range
//
// With type:N, `expect` needs one matching entity to hold and `expect-all`
// needs every one of them to (and passes on a frame where there are none).
//
// <button>  triangle circle cross square up right down left start
// <frames>  `N` or `N-M`, the same 1-based numbering as --hold-*
// <who>     `lead`, `slot:N` (entity pool slot), or `type:N` (any live entity
//           of that type id -- "some spark of type 0x1C7")
// <field>   hp, maxhp (settable); state (+0x60 without the 0x4000 entry bit),
//           anim (+0xA0), action (the party control block's current action)
// <op>      == != < <= > >=
// Numbers take a 0x prefix for hex.
//
// `set` runs at the start of the frame, beside the debug damage keys. An
// expectation is read after the frame has run, so `expect 1751 ...` sees the
// state frame 1751 left behind.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace orphen::harness
{

  struct ScenarioTarget
  {
    enum class Kind
    {
      Lead,
      Slot,
      AnyOfType,
    };
    Kind kind = Kind::Lead;
    int value = 0;
  };

  enum class ScenarioField
  {
    Hp,
    MaxHp,
    State,
    Anim,
    Action,
  };

  enum class ScenarioOp
  {
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
  };

  struct ScenarioHold
  {
    int button = 0; // the index --hold-* uses: triangle, circle, cross, square, up, right, down, left, start
    std::uint32_t first = 0;
    std::uint32_t last = 0;
  };

  struct ScenarioSet
  {
    int line = 0;
    std::uint32_t frame = 0;
    ScenarioTarget target;
    ScenarioField field = ScenarioField::Hp;
    int value = 0;
  };

  struct ScenarioExpect
  {
    int line = 0;
    std::string text;
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    bool everyFrame = false;
    ScenarioTarget target;
    ScenarioField field = ScenarioField::Hp;
    ScenarioOp op = ScenarioOp::Eq;
    int value = 0;

    // Filled in as the run goes.
    bool held = false;            // expect: some frame matched
    bool broken = false;          // expect-all: some frame did not
    std::uint32_t decidingFrame = 0;
    bool sawValue = false;        // a value was readable on the deciding/last frame
    int seenValue = 0;
    std::uint32_t framesChecked = 0;
  };

  struct BattleScenario
  {
    std::string path;
    std::vector<ScenarioHold> holds;
    std::vector<ScenarioSet> sets;
    std::vector<ScenarioExpect> expects;
  };

  // Throws std::runtime_error naming the file and line on a bad directive.
  BattleScenario loadBattleScenario(const std::filesystem::path &path);

  bool scenarioCompare(int lhs, ScenarioOp op, int rhs);
  bool scenarioFieldSettable(ScenarioField field);
  const char *scenarioFieldName(ScenarioField field);

} // namespace orphen::harness
