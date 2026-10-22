#include "harness/battle_scenario.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace orphen::harness
{
  namespace
  {
    [[noreturn]] void fail(const std::filesystem::path &path, int line, const std::string &why)
    {
      throw std::runtime_error("--scenario " + path.string() + ":" + std::to_string(line) + ": " + why);
    }

    int parseNumber(const std::filesystem::path &path, int line, const std::string &text)
    {
      try
      {
        std::size_t used = 0;
        const long value = std::stol(text, &used, 0);
        if (used != text.size())
        {
          fail(path, line, "not a number: " + text);
        }
        return static_cast<int>(value);
      }
      catch (const std::logic_error &)
      {
        fail(path, line, "not a number: " + text);
      }
    }

    void parseFrames(const std::filesystem::path &path, int line, const std::string &text,
                     std::uint32_t &first, std::uint32_t &last)
    {
      const std::size_t dash = text.find('-');
      const int from = parseNumber(path, line, text.substr(0, dash));
      const int to = dash == std::string::npos ? from : parseNumber(path, line, text.substr(dash + 1));
      if (from < 1 || to < from)
      {
        fail(path, line, "bad frame range: " + text);
      }
      first = static_cast<std::uint32_t>(from);
      last = static_cast<std::uint32_t>(to);
    }

    ScenarioTarget parseTarget(const std::filesystem::path &path, int line, const std::string &text)
    {
      ScenarioTarget target;
      if (text == "lead")
      {
        return target;
      }
      const std::size_t colon = text.find(':');
      const std::string kind = text.substr(0, colon);
      if (colon == std::string::npos || (kind != "slot" && kind != "type"))
      {
        fail(path, line, "expected lead, slot:N or type:N, got " + text);
      }
      target.kind = kind == "slot" ? ScenarioTarget::Kind::Slot : ScenarioTarget::Kind::AnyOfType;
      target.value = parseNumber(path, line, text.substr(colon + 1));
      return target;
    }

    ScenarioField parseField(const std::filesystem::path &path, int line, const std::string &text)
    {
      if (text == "hp") return ScenarioField::Hp;
      if (text == "maxhp") return ScenarioField::MaxHp;
      if (text == "state") return ScenarioField::State;
      if (text == "anim") return ScenarioField::Anim;
      if (text == "action") return ScenarioField::Action;
      fail(path, line, "unknown field " + text);
    }

    ScenarioOp parseOp(const std::filesystem::path &path, int line, const std::string &text)
    {
      if (text == "==") return ScenarioOp::Eq;
      if (text == "!=") return ScenarioOp::Ne;
      if (text == "<") return ScenarioOp::Lt;
      if (text == "<=") return ScenarioOp::Le;
      if (text == ">") return ScenarioOp::Gt;
      if (text == ">=") return ScenarioOp::Ge;
      fail(path, line, "unknown comparison " + text);
    }

    int parseButton(const std::filesystem::path &path, int line, const std::string &text)
    {
      static const char *const kButtons[9] = {"triangle", "circle", "cross", "square", "up",
                                              "right",    "down",   "left",  "start"};
      for (int index = 0; index < 9; ++index)
      {
        if (text == kButtons[index])
        {
          return index;
        }
      }
      fail(path, line, "unknown button " + text);
    }
  } // namespace

  BattleScenario loadBattleScenario(const std::filesystem::path &path)
  {
    std::ifstream in(path);
    if (!in)
    {
      throw std::runtime_error("--scenario: cannot open " + path.string());
    }
    BattleScenario scenario;
    scenario.path = path.string();
    std::string raw;
    int line = 0;
    while (std::getline(in, raw))
    {
      ++line;
      const std::size_t hash = raw.find('#');
      std::string text = raw.substr(0, hash);
      std::istringstream words(text);
      std::vector<std::string> word;
      for (std::string one; words >> one;)
      {
        word.push_back(one);
      }
      if (word.empty())
      {
        continue;
      }
      const std::string &verb = word[0];
      if (verb == "hold")
      {
        if (word.size() != 3)
        {
          fail(path, line, "hold <button> <first>-<last>");
        }
        ScenarioHold hold;
        hold.button = parseButton(path, line, word[1]);
        parseFrames(path, line, word[2], hold.first, hold.last);
        scenario.holds.push_back(hold);
      }
      else if (verb == "set")
      {
        if (word.size() != 5)
        {
          fail(path, line, "set <frame> <who> <field> <value>");
        }
        ScenarioSet set;
        set.line = line;
        std::uint32_t last = 0;
        parseFrames(path, line, word[1], set.frame, last);
        if (last != set.frame)
        {
          fail(path, line, "set takes one frame");
        }
        set.target = parseTarget(path, line, word[2]);
        set.field = parseField(path, line, word[3]);
        if (!scenarioFieldSettable(set.field))
        {
          // Only starting conditions. Writing a state or an action would be
          // the harness deciding an outcome the ported code is meant to reach.
          fail(path, line, std::string(scenarioFieldName(set.field)) + " is read-only in a scenario");
        }
        if (set.target.kind == ScenarioTarget::Kind::AnyOfType)
        {
          fail(path, line, "set needs lead or slot:N");
        }
        set.value = parseNumber(path, line, word[4]);
        scenario.sets.push_back(set);
      }
      else if (verb == "expect" || verb == "expect-all")
      {
        if (word.size() != 6)
        {
          fail(path, line, verb + " <frames> <who> <field> <op> <value>");
        }
        ScenarioExpect expect;
        expect.line = line;
        expect.everyFrame = verb == "expect-all";
        parseFrames(path, line, word[1], expect.first, expect.last);
        expect.target = parseTarget(path, line, word[2]);
        expect.field = parseField(path, line, word[3]);
        expect.op = parseOp(path, line, word[4]);
        expect.value = parseNumber(path, line, word[5]);
        expect.text = verb + " " + word[1] + " " + word[2] + " " + word[3] + " " + word[4] + " " + word[5];
        scenario.expects.push_back(expect);
      }
      else
      {
        fail(path, line, "unknown directive " + verb);
      }
    }
    return scenario;
  }

  bool scenarioCompare(int lhs, ScenarioOp op, int rhs)
  {
    switch (op)
    {
    case ScenarioOp::Eq: return lhs == rhs;
    case ScenarioOp::Ne: return lhs != rhs;
    case ScenarioOp::Lt: return lhs < rhs;
    case ScenarioOp::Le: return lhs <= rhs;
    case ScenarioOp::Gt: return lhs > rhs;
    case ScenarioOp::Ge: return lhs >= rhs;
    }
    return false;
  }

  bool scenarioFieldSettable(ScenarioField field)
  {
    return field == ScenarioField::Hp || field == ScenarioField::MaxHp;
  }

  const char *scenarioFieldName(ScenarioField field)
  {
    switch (field)
    {
    case ScenarioField::Hp: return "hp";
    case ScenarioField::MaxHp: return "maxhp";
    case ScenarioField::State: return "state";
    case ScenarioField::Anim: return "anim";
    case ScenarioField::Action: return "action";
    }
    return "?";
  }

} // namespace orphen::harness
