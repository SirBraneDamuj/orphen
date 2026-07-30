#include "runtime/scene_script_interpreter.h"

#include "ported/psm2/psm2_runtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace orphen::port
{
  namespace
  {

    bool canRead(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t byteCount)
    {
      return offset <= bytes.size() && byteCount <= bytes.size() - offset;
    }

    std::uint32_t readLeU32Unchecked(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::uint32_t>(bytes[offset]) |
             (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
             (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
             (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    }

    std::int16_t readLeS16Unchecked(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                       (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
    }

    std::int32_t readLeS32Unchecked(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
      return static_cast<std::int32_t>(readLeU32Unchecked(bytes, offset));
    }

    std::int64_t signedDeltaFromLeU32(std::uint32_t rawDelta)
    {
      if (rawDelta <= 0x7fffffffu)
      {
        return static_cast<std::int64_t>(rawDelta);
      }

      return -static_cast<std::int64_t>((~rawDelta) + 1u);
    }

    bool addRelativeOffset(std::size_t baseOffset, std::int64_t delta, std::size_t &targetOffset)
    {
      const std::int64_t base = static_cast<std::int64_t>(baseOffset);
      const std::int64_t target = base + delta;
      if (target < 0)
      {
        return false;
      }

      targetOffset = static_cast<std::size_t>(target);
      return true;
    }

    void appendEvent(SceneScriptTraceSummary &trace, const SceneScriptTraceEvent &event, const SceneScriptTraceOptions &options)
    {
      if (trace.events.size() < options.maxEvents)
      {
        trace.events.push_back(event);
      }
    }

    bool isNoopStructuralOpcode(std::uint8_t opcode)
    {
      return opcode == 0x00 || opcode == 0x05 || opcode == 0x06;
    }

    bool isSkipInlineWordOpcode(std::uint8_t opcode)
    {
      return opcode == 0x07 || opcode == 0x09;
    }

    bool isRelativeAdvanceOpcode(std::uint8_t opcode)
    {
      return opcode == 0x03 || opcode == 0x08 || opcode == 0x0a;
    }

    enum class VmExpressionStatus
    {
      Success,
      OutOfBoundsRead,
      StepLimitReached,
      RecursionLimitReached,
      StackUnderflow,
      DivisionByZero,
      UnsupportedOpcode,
    };

    struct VmExpressionResult
    {
      VmExpressionStatus status = VmExpressionStatus::UnsupportedOpcode;
      std::int32_t value = 0;
      std::size_t nextOffset = 0;
      std::size_t stopOffset = 0;
      std::uint16_t opcode = 0;
    };

    struct ScriptVmState
    {
      std::array<std::uint32_t, 128> work{};
      std::array<std::uint8_t, 2304> flags{};
      std::array<std::uint32_t, 256> objectRegisters{};
      std::array<std::uint8_t, 0x41> scriptSlots{};
    };

    SceneScriptTraceStop traceStopFromVmExpressionStatus(VmExpressionStatus status)
    {
      switch (status)
      {
      case VmExpressionStatus::Success:
        return SceneScriptTraceStop::Completed;
      case VmExpressionStatus::OutOfBoundsRead:
        return SceneScriptTraceStop::OutOfBoundsRead;
      case VmExpressionStatus::StepLimitReached:
        return SceneScriptTraceStop::StepLimitReached;
      case VmExpressionStatus::RecursionLimitReached:
      case VmExpressionStatus::StackUnderflow:
      case VmExpressionStatus::DivisionByZero:
      case VmExpressionStatus::UnsupportedOpcode:
        return SceneScriptTraceStop::RequiresVmEvaluation;
      }

      return SceneScriptTraceStop::RequiresVmEvaluation;
    }

    bool readFlagBit(const ScriptVmState &state, std::uint32_t flagId)
    {
      const std::size_t byteIndex = flagId >> 3;
      if (byteIndex >= state.flags.size())
      {
        return false;
      }

      return (state.flags[byteIndex] & (1u << (flagId & 7u))) != 0;
    }

    void writeFlagBit(ScriptVmState &state, std::uint32_t flagId, bool value)
    {
      const std::size_t byteIndex = flagId >> 3;
      if (byteIndex >= state.flags.size())
      {
        return;
      }

      const std::uint8_t mask = static_cast<std::uint8_t>(1u << (flagId & 7u));
      if (value)
      {
        state.flags[byteIndex] = static_cast<std::uint8_t>(state.flags[byteIndex] | mask);
      }
      else
      {
        state.flags[byteIndex] = static_cast<std::uint8_t>(state.flags[byteIndex] & ~mask);
      }
    }

    std::uint32_t readObjectRegister(const ScriptVmState &state, std::uint32_t registerId)
    {
      return registerId < state.objectRegisters.size() ? state.objectRegisters[registerId] : 0;
    }

    void writeObjectRegister(ScriptVmState &state, std::uint32_t registerId, std::uint32_t value)
    {
      if (registerId < state.objectRegisters.size())
      {
        state.objectRegisters[registerId] = value;
      }
    }

    std::optional<std::uint32_t> applyScriptAluSelector(std::uint32_t currentValue,
                                                        std::uint32_t operandValue,
                                                        std::uint8_t selector)
    {
      switch (selector)
      {
      case 0x25:
        return operandValue;
      case 0x26:
        return currentValue * operandValue;
      case 0x27:
        return operandValue != 0 ? static_cast<std::uint32_t>(static_cast<std::int32_t>(currentValue) / static_cast<std::int32_t>(operandValue)) : currentValue;
      case 0x28:
        return operandValue != 0 ? static_cast<std::uint32_t>(static_cast<std::int32_t>(currentValue) % static_cast<std::int32_t>(operandValue)) : currentValue;
      case 0x29:
        return currentValue + operandValue;
      case 0x2a:
        return currentValue - operandValue;
      case 0x2b:
        return currentValue & operandValue;
      case 0x2c:
        return currentValue ^ operandValue;
      case 0x2d:
        return currentValue | operandValue;
      case 0x2e:
        return currentValue + 1;
      case 0x2f:
        return currentValue - 1;
      default:
        return std::nullopt;
      }
    }

    VmExpressionResult evaluateExpression(std::span<const std::uint8_t> scriptBytes,
                                          std::size_t startOffset,
                                          const SceneScriptTraceOptions &options,
                                          std::size_t depth,
                                          ScriptVmState &state);

    VmExpressionResult failExpression(VmExpressionStatus status, std::size_t offset, std::uint16_t opcode)
    {
      VmExpressionResult result;
      result.status = status;
      result.nextOffset = offset;
      result.stopOffset = offset;
      result.opcode = opcode;
      return result;
    }

    bool pushExpressionValue(std::vector<std::int32_t> &stack, std::int32_t value)
    {
      stack.push_back(value);
      return true;
    }

    std::optional<std::int32_t> popExpressionValue(std::vector<std::int32_t> &stack)
    {
      if (stack.empty())
      {
        return std::nullopt;
      }

      const std::int32_t value = stack.back();
      stack.pop_back();
      return value;
    }

    VmExpressionResult evaluateNestedExpression(std::span<const std::uint8_t> scriptBytes,
                                                std::size_t startOffset,
                                                const SceneScriptTraceOptions &options,
                                                std::size_t depth,
                                                ScriptVmState &state)
    {
      return evaluateExpression(scriptBytes, startOffset, options, depth + 1, state);
    }

    VmExpressionResult evaluateHighExpressionOpcode(std::span<const std::uint8_t> scriptBytes,
                                                    std::size_t opcodeOffset,
                                                    std::uint8_t opcode,
                                                    const SceneScriptTraceOptions &options,
                                                    std::size_t depth,
                                                    ScriptVmState &state)
    {
      switch (opcode)
      {
      case 0x32:
        return VmExpressionResult{VmExpressionStatus::Success, 0, opcodeOffset + 1, opcodeOffset + 1, opcode};
      case 0x36:
      case 0x38:
      case 0x3d:
      case 0x3e:
      case 0x3f:
      case 0x40:
      {
        VmExpressionResult idExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (idExpression.status != VmExpressionStatus::Success)
        {
          return idExpression;
        }

        const std::uint32_t id = static_cast<std::uint32_t>(idExpression.value);
        std::int32_t value = 0;
        if (opcode == 0x36)
        {
          value = id < state.work.size() ? static_cast<std::int32_t>(state.work[id]) : 0;
        }
        else if (opcode == 0x38)
        {
          const std::size_t byteIndex = id >> 3;
          value = byteIndex < state.flags.size() ? state.flags[byteIndex] : 0;
        }
        else
        {
          const bool previousValue = readFlagBit(state, id);
          value = previousValue ? 1 : 0;
          if (opcode == 0x3e)
          {
            writeFlagBit(state, id, true);
          }
          else if (opcode == 0x3f)
          {
            writeFlagBit(state, id, false);
          }
          else if (opcode == 0x40)
          {
            writeFlagBit(state, id, !previousValue);
          }
        }

        return VmExpressionResult{VmExpressionStatus::Success, value, idExpression.nextOffset, idExpression.nextOffset, opcode};
      }
      case 0x37:
      case 0x39:
      {
        VmExpressionResult indexExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (indexExpression.status != VmExpressionStatus::Success)
        {
          return indexExpression;
        }

        VmExpressionResult valueExpression = evaluateNestedExpression(scriptBytes, indexExpression.nextOffset, options, depth, state);
        if (valueExpression.status != VmExpressionStatus::Success)
        {
          return valueExpression;
        }
        if (!canRead(scriptBytes, valueExpression.nextOffset, 1))
        {
          return failExpression(VmExpressionStatus::OutOfBoundsRead, valueExpression.nextOffset, opcode);
        }

        const std::uint32_t index = static_cast<std::uint32_t>(indexExpression.value);
        const std::uint32_t operandValue = static_cast<std::uint32_t>(valueExpression.value);
        const std::uint8_t selector = scriptBytes[valueExpression.nextOffset];
        std::uint32_t currentValue = 0;
        if (opcode == 0x37)
        {
          currentValue = index < state.work.size() ? state.work[index] : 0;
        }
        else
        {
          const std::size_t byteIndex = index >> 3;
          currentValue = byteIndex < state.flags.size() ? state.flags[byteIndex] : 0;
        }

        const std::optional<std::uint32_t> updatedValue = applyScriptAluSelector(currentValue, operandValue, selector);
        if (!updatedValue.has_value())
        {
          return failExpression(VmExpressionStatus::UnsupportedOpcode, valueExpression.nextOffset, selector);
        }

        if (opcode == 0x37)
        {
          if (index < state.work.size())
          {
            state.work[index] = *updatedValue;
          }
        }
        else
        {
          const std::size_t byteIndex = index >> 3;
          if (byteIndex < state.flags.size())
          {
            state.flags[byteIndex] = static_cast<std::uint8_t>(*updatedValue);
          }
        }

        return VmExpressionResult{VmExpressionStatus::Success, static_cast<std::int32_t>(*updatedValue), valueExpression.nextOffset + 1, valueExpression.nextOffset + 1, opcode};
      }
      case 0x52:
      {
        VmExpressionResult typeExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (typeExpression.status != VmExpressionStatus::Success)
        {
          return typeExpression;
        }

        const bool spawned = typeExpression.value != 0x55;
        return VmExpressionResult{VmExpressionStatus::Success, spawned ? 1 : 0, typeExpression.nextOffset, typeExpression.nextOffset, opcode};
      }
      case 0x61:
      {
        VmExpressionResult maskExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (maskExpression.status != VmExpressionStatus::Success)
        {
          return maskExpression;
        }
        if (!canRead(scriptBytes, maskExpression.nextOffset, 1))
        {
          return failExpression(VmExpressionStatus::OutOfBoundsRead, maskExpression.nextOffset, opcode);
        }

        return VmExpressionResult{VmExpressionStatus::Success, 0, maskExpression.nextOffset + 1, maskExpression.nextOffset + 1, opcode};
      }
      case 0x59:
        return VmExpressionResult{VmExpressionStatus::Success, 0x100, opcodeOffset + 1, opcodeOffset + 1, opcode};
      case 0x5a:
      {
        VmExpressionResult tagExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (tagExpression.status != VmExpressionStatus::Success)
        {
          return tagExpression;
        }

        return VmExpressionResult{VmExpressionStatus::Success, 0, tagExpression.nextOffset, tagExpression.nextOffset, opcode};
      }
      case 0x76:
      {
        VmExpressionResult selectorExpression = evaluateNestedExpression(scriptBytes, opcodeOffset + 1, options, depth, state);
        if (selectorExpression.status != VmExpressionStatus::Success)
        {
          return selectorExpression;
        }
        VmExpressionResult registerExpression = evaluateNestedExpression(scriptBytes, selectorExpression.nextOffset, options, depth, state);
        if (registerExpression.status != VmExpressionStatus::Success)
        {
          return registerExpression;
        }

        const std::uint32_t registerId = static_cast<std::uint32_t>(registerExpression.value);
        return VmExpressionResult{VmExpressionStatus::Success, static_cast<std::int32_t>(readObjectRegister(state, registerId)), registerExpression.nextOffset, registerExpression.nextOffset, opcode};
      }
      case 0x9f:
      {
        if (!canRead(scriptBytes, opcodeOffset + 1, 1))
        {
          return failExpression(VmExpressionStatus::OutOfBoundsRead, opcodeOffset + 1, opcode);
        }

        const std::uint8_t slotIndex = scriptBytes[opcodeOffset + 1];
        const bool occupied = slotIndex < state.scriptSlots.size() && state.scriptSlots[slotIndex] != 0;
        return VmExpressionResult{VmExpressionStatus::Success, occupied ? 1 : 0, opcodeOffset + 2, opcodeOffset + 2, opcode};
      }
      case 0xa0:
      {
        for (std::size_t slotIndex = 0; slotIndex < 0x3e && slotIndex < state.scriptSlots.size(); ++slotIndex)
        {
          if (state.scriptSlots[slotIndex] == 0)
          {
            return VmExpressionResult{VmExpressionStatus::Success, static_cast<std::int32_t>(slotIndex), opcodeOffset + 1, opcodeOffset + 1, opcode};
          }
        }

        return VmExpressionResult{VmExpressionStatus::Success, -1, opcodeOffset + 1, opcodeOffset + 1, opcode};
      }
      default:
        return failExpression(VmExpressionStatus::UnsupportedOpcode, opcodeOffset, opcode);
      }
    }

    VmExpressionResult evaluateExpression(std::span<const std::uint8_t> scriptBytes,
                                          std::size_t startOffset,
                                          const SceneScriptTraceOptions &options,
                                          std::size_t depth,
                                          ScriptVmState &state)
    {
      if (depth > options.maxExpressionDepth)
      {
        return failExpression(VmExpressionStatus::RecursionLimitReached, startOffset, 0);
      }

      std::size_t pc = startOffset;
      std::size_t expressionSteps = 0;
      std::vector<std::int32_t> stack;

      while (expressionSteps < options.maxExpressionSteps)
      {
        if (pc >= scriptBytes.size())
        {
          return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, 0);
        }

        const std::uint8_t opcode = scriptBytes[pc];
        ++expressionSteps;

        if (opcode > 0x31)
        {
          if (opcode == 0xff)
          {
            const std::uint16_t extendedOpcode = canRead(scriptBytes, pc, 2)
                                                     ? static_cast<std::uint16_t>(scriptBytes[pc + 1] + 0x100u)
                                                     : 0xff;
            return failExpression(VmExpressionStatus::UnsupportedOpcode, pc, extendedOpcode);
          }

          VmExpressionResult highOpcode = evaluateHighExpressionOpcode(scriptBytes, pc, opcode, options, depth, state);
          if (highOpcode.status != VmExpressionStatus::Success)
          {
            return highOpcode;
          }

          pushExpressionValue(stack, highOpcode.value);
          pc = highOpcode.nextOffset;
          continue;
        }

        switch (opcode)
        {
        case 0x0b:
        {
          const auto value = popExpressionValue(stack);
          return VmExpressionResult{VmExpressionStatus::Success, value.value_or(0), pc + 1, pc + 1, opcode};
        }
        case 0x0c:
          if (!canRead(scriptBytes, pc, 2))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, scriptBytes[pc + 1]);
          pc += 2;
          continue;
        case 0x0d:
          if (!canRead(scriptBytes, pc, 3))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, static_cast<std::int32_t>(static_cast<std::uint16_t>(scriptBytes[pc + 1]) |
                                                               (static_cast<std::uint16_t>(scriptBytes[pc + 2]) << 8)));
          pc += 3;
          continue;
        case 0x0e:
          if (!canRead(scriptBytes, pc, 5))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, readLeS32Unchecked(scriptBytes, pc + 1));
          pc += 5;
          continue;
        case 0x0f:
          if (!canRead(scriptBytes, pc, 5))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, readLeS32Unchecked(scriptBytes, pc + 1) * 100);
          pc += 5;
          continue;
        case 0x10:
          if (!canRead(scriptBytes, pc, 3))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, static_cast<std::int32_t>(readLeS16Unchecked(scriptBytes, pc + 1)) * 1000);
          pc += 3;
          continue;
        case 0x11:
          if (!canRead(scriptBytes, pc, 3))
          {
            return failExpression(VmExpressionStatus::OutOfBoundsRead, pc, opcode);
          }
          pushExpressionValue(stack, static_cast<std::int32_t>(readLeS16Unchecked(scriptBytes, pc + 1)) * 0xf570 / 0x168);
          pc += 3;
          continue;
        case 0x30:
        case 0x31:
        {
          pc += 1;
          std::array<std::int32_t, 4> components{};
          const std::size_t componentCount = opcode == 0x31 ? 4 : 3;
          for (std::size_t componentIndex = 0; componentIndex < componentCount; ++componentIndex)
          {
            VmExpressionResult component = evaluateNestedExpression(scriptBytes, pc, options, depth, state);
            if (component.status != VmExpressionStatus::Success)
            {
              return component;
            }
            components[componentIndex] = component.value;
            pc = component.nextOffset;
          }
          const std::uint32_t packed = (static_cast<std::uint32_t>(components[3]) & 0xffu) << 24 |
                                       (static_cast<std::uint32_t>(components[2]) & 0xffu) << 16 |
                                       (static_cast<std::uint32_t>(components[1]) & 0xffu) << 8 |
                                       (static_cast<std::uint32_t>(components[0]) & 0xffu);
          pushExpressionValue(stack, static_cast<std::int32_t>(packed));
          continue;
        }
        default:
          break;
        }

        if (opcode == 0x18 || opcode == 0x19 || opcode == 0x1e)
        {
          auto value = popExpressionValue(stack);
          if (!value.has_value())
          {
            return failExpression(VmExpressionStatus::StackUnderflow, pc, opcode);
          }
          if (opcode == 0x18)
          {
            pushExpressionValue(stack, *value == 0 ? 1 : 0);
          }
          else if (opcode == 0x19)
          {
            pushExpressionValue(stack, ~*value);
          }
          else
          {
            pushExpressionValue(stack, -*value);
          }
          pc += 1;
          continue;
        }

        auto right = popExpressionValue(stack);
        auto left = popExpressionValue(stack);
        if (!left.has_value() || !right.has_value())
        {
          return failExpression(VmExpressionStatus::StackUnderflow, pc, opcode);
        }

        switch (opcode)
        {
        case 0x12:
          pushExpressionValue(stack, *left == *right ? 1 : 0);
          break;
        case 0x13:
          pushExpressionValue(stack, *left != *right ? 1 : 0);
          break;
        case 0x14:
          pushExpressionValue(stack, *left < *right ? 1 : 0);
          break;
        case 0x15:
          pushExpressionValue(stack, *right < *left ? 1 : 0);
          break;
        case 0x16:
          pushExpressionValue(stack, *right >= *left ? 1 : 0);
          break;
        case 0x17:
          pushExpressionValue(stack, *left >= *right ? 1 : 0);
          break;
        case 0x1a:
          pushExpressionValue(stack, (*left != 0 && *right != 0) ? 1 : 0);
          break;
        case 0x1b:
        case 0x21:
          pushExpressionValue(stack, *left | *right);
          break;
        case 0x1c:
          pushExpressionValue(stack, *left + *right);
          break;
        case 0x1d:
          pushExpressionValue(stack, *left - *right);
          break;
        case 0x1f:
          pushExpressionValue(stack, *left ^ *right);
          break;
        case 0x20:
          pushExpressionValue(stack, *left & *right);
          break;
        case 0x22:
          if (*right == 0)
          {
            return failExpression(VmExpressionStatus::DivisionByZero, pc, opcode);
          }
          pushExpressionValue(stack, *left / *right);
          break;
        case 0x23:
          pushExpressionValue(stack, *left * *right);
          break;
        case 0x24:
          if (*right == 0)
          {
            return failExpression(VmExpressionStatus::DivisionByZero, pc, opcode);
          }
          pushExpressionValue(stack, *left % *right);
          break;
        default:
          return failExpression(VmExpressionStatus::UnsupportedOpcode, pc, opcode);
        }
        pc += 1;
      }

      return failExpression(VmExpressionStatus::StepLimitReached, pc, 0);
    }

    bool skipExpression(std::span<const std::uint8_t> scriptBytes,
                        std::size_t startOffset,
                        const SceneScriptTraceOptions &options,
                        std::size_t depth,
                        std::size_t &nextOffset,
                        SceneScriptTraceStop &stopReason)
    {
      nextOffset = startOffset;
      if (depth > options.maxExpressionDepth)
      {
        stopReason = SceneScriptTraceStop::RequiresVmEvaluation;
        return false;
      }

      std::size_t pc = startOffset;
      std::size_t expressionSteps = 0;
      while (expressionSteps < options.maxExpressionSteps)
      {
        if (pc >= scriptBytes.size())
        {
          nextOffset = pc;
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        const std::uint8_t opcode = scriptBytes[pc];
        ++expressionSteps;
        switch (opcode)
        {
        case 0x0b:
          nextOffset = pc + 1;
          return true;
        case 0x0c:
          if (!canRead(scriptBytes, pc, 2))
          {
            nextOffset = pc;
            stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return false;
          }
          pc += 2;
          continue;
        case 0x0d:
        case 0x10:
        case 0x11:
          if (!canRead(scriptBytes, pc, 3))
          {
            nextOffset = pc;
            stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return false;
          }
          pc += 3;
          continue;
        case 0x0e:
        case 0x0f:
          if (!canRead(scriptBytes, pc, 5))
          {
            nextOffset = pc;
            stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return false;
          }
          pc += 5;
          continue;
        case 0x30:
        case 0x31:
        {
          pc += 1;
          const std::size_t nestedExpressionCount = opcode == 0x31 ? 4 : 3;
          for (std::size_t nestedIndex = 0; nestedIndex < nestedExpressionCount; ++nestedIndex)
          {
            if (!skipExpression(scriptBytes, pc, options, depth + 1, pc, stopReason))
            {
              return false;
            }
          }
          continue;
        }
        default:
          if (opcode >= 0x12 && opcode <= 0x24)
          {
            pc += 1;
            continue;
          }

          nextOffset = pc;
          stopReason = SceneScriptTraceStop::RequiresVmEvaluation;
          return false;
        }
      }

      nextOffset = pc;
      stopReason = SceneScriptTraceStop::StepLimitReached;
      return false;
    }

    bool consumeExpression(std::span<const std::uint8_t> scriptBytes,
                           std::size_t startOffset,
                           const SceneScriptTraceOptions &options,
                           ScriptVmState &state,
                           std::size_t &nextOffset,
                           SceneScriptTraceStop &stopReason)
    {
      VmExpressionResult result = evaluateExpression(scriptBytes, startOffset, options, 0, state);
      if (result.status != VmExpressionStatus::Success)
      {
        nextOffset = result.stopOffset;
        stopReason = traceStopFromVmExpressionStatus(result.status);
        return false;
      }

      nextOffset = result.nextOffset;
      return true;
    }

    std::size_t applyRecord80TerrainFlag(orphen::ported::psm2::Psm2RuntimeState &terrainState,
                                         std::uint32_t matchMask,
                                         std::uint8_t mode,
                                         std::uint32_t flagMask)
    {
      const std::size_t recordCount = std::min(terrainState.DAT_003556b0_dRecords78.size(),
                                               terrainState.DAT_003556ac_dRecords80.size());
      std::size_t writeCount = 0;
      for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex)
      {
        const auto &record78 = terrainState.DAT_003556b0_dRecords78[recordIndex];
        if ((record78.terrainFlags & matchMask) == 0)
        {
          continue;
        }

        auto &record80 = terrainState.DAT_003556ac_dRecords80[recordIndex];
        const std::uint32_t previous = record80.terrainFlags;
        if (mode == 0)
        {
          record80.terrainFlags |= flagMask;
        }
        else
        {
          record80.terrainFlags &= ~flagMask;
        }
        if (record80.terrainFlags != previous)
        {
          ++writeCount;
        }
      }
      return writeCount;
    }

    std::size_t applyRecord78TerrainFlags(orphen::ported::psm2::Psm2RuntimeState &terrainState,
                                          std::uint32_t matchMask,
                                          std::uint8_t mode,
                                          std::uint32_t flagMask)
    {
      std::size_t writeCount = 0;
      for (auto &record78 : terrainState.DAT_003556b0_dRecords78)
      {
        if ((record78.terrainFlags & matchMask) == 0)
        {
          continue;
        }

        const std::uint32_t previous = record78.terrainFlags;
        if (mode == 0)
        {
          record78.terrainFlags &= ~flagMask;
        }
        else
        {
          record78.terrainFlags |= flagMask;
        }
        if (record78.terrainFlags != previous)
        {
          ++writeCount;
        }
      }
      return writeCount;
    }

    std::size_t applyRecord78LeadingWordFlag(orphen::ported::psm2::Psm2RuntimeState &terrainState,
                                             std::uint32_t matchMask,
                                             std::uint8_t mode,
                                             std::uint32_t flagMask)
    {
      std::size_t writeCount = 0;
      for (auto &record78 : terrainState.DAT_003556b0_dRecords78)
      {
        if ((record78.terrainFlags & matchMask) == 0)
        {
          continue;
        }

        const std::uint32_t previous = record78.leadingWord;
        if (mode == 0)
        {
          record78.leadingWord &= ~flagMask;
        }
        else
        {
          record78.leadingWord |= flagMask;
        }
        if (record78.leadingWord != previous)
        {
          ++writeCount;
        }
      }
      return writeCount;
    }

    bool skipKnownStandardOpcode(std::span<const std::uint8_t> scriptBytes,
                                 std::size_t opcodeOffset,
                                 std::uint8_t opcode,
                                 const SceneScriptTraceOptions &options,
                                 ScriptVmState &state,
                                 std::size_t &nextOffset,
                                 SceneScriptTraceStop &stopReason)
    {
      if (opcode == 0x4d)
      {
        if (!canRead(scriptBytes, opcodeOffset + 1, 1))
        {
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        const std::uint8_t resourceIdCount = scriptBytes[opcodeOffset + 1];
        const std::size_t payloadBytes = static_cast<std::size_t>(resourceIdCount) * sizeof(std::uint32_t);
        if (!canRead(scriptBytes, opcodeOffset + 2, payloadBytes))
        {
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        nextOffset = opcodeOffset + 2 + payloadBytes;
        return true;
      }

      if (opcode == 0x4f)
      {
        nextOffset = opcodeOffset + 1;
        return true;
      }

      if (opcode == 0x51)
      {
        if (!canRead(scriptBytes, opcodeOffset + 1, 1))
        {
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        nextOffset = opcodeOffset + 2;
        return true;
      }

      if (opcode == 0x54 || opcode == 0x55)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 4; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0x37 || opcode == 0x39)
      {
        VmExpressionResult indexExpression = evaluateExpression(scriptBytes, opcodeOffset + 1, options, 0, state);
        if (indexExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = indexExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(indexExpression.status);
          return false;
        }

        VmExpressionResult valueExpression = evaluateExpression(scriptBytes, indexExpression.nextOffset, options, 0, state);
        if (valueExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = valueExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(valueExpression.status);
          return false;
        }
        if (!canRead(scriptBytes, valueExpression.nextOffset, 1))
        {
          nextOffset = valueExpression.nextOffset;
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        const std::uint32_t index = static_cast<std::uint32_t>(indexExpression.value);
        const std::uint32_t operandValue = static_cast<std::uint32_t>(valueExpression.value);
        const std::uint8_t selector = scriptBytes[valueExpression.nextOffset];
        std::uint32_t currentValue = 0;
        if (opcode == 0x37)
        {
          currentValue = index < state.work.size() ? state.work[index] : 0;
        }
        else
        {
          const std::size_t byteIndex = index >> 3;
          currentValue = byteIndex < state.flags.size() ? state.flags[byteIndex] : 0;
        }

        const std::optional<std::uint32_t> updatedValue = applyScriptAluSelector(currentValue, operandValue, selector);
        if (!updatedValue.has_value())
        {
          nextOffset = valueExpression.nextOffset;
          stopReason = SceneScriptTraceStop::RequiresVmEvaluation;
          return false;
        }

        if (opcode == 0x37)
        {
          if (index < state.work.size())
          {
            state.work[index] = *updatedValue;
          }
        }
        else
        {
          const std::size_t byteIndex = index >> 3;
          if (byteIndex < state.flags.size())
          {
            state.flags[byteIndex] = static_cast<std::uint8_t>(*updatedValue);
          }
        }

        nextOffset = valueExpression.nextOffset + 1;
        return true;
      }

      if (opcode >= 0x77 && opcode <= 0x7c)
      {
        std::size_t pc = opcodeOffset + 1;
        VmExpressionResult selectorExpression = evaluateExpression(scriptBytes, pc, options, 0, state);
        if (selectorExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = selectorExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(selectorExpression.status);
          return false;
        }

        VmExpressionResult registerExpression = evaluateExpression(scriptBytes, selectorExpression.nextOffset, options, 0, state);
        if (registerExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = registerExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(registerExpression.status);
          return false;
        }

        VmExpressionResult valueExpression = evaluateExpression(scriptBytes, registerExpression.nextOffset, options, 0, state);
        if (valueExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = valueExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(valueExpression.status);
          return false;
        }

        const std::uint32_t registerId = static_cast<std::uint32_t>(registerExpression.value);
        const std::uint32_t currentValue = readObjectRegister(state, registerId);
        const std::uint32_t operandValue = static_cast<std::uint32_t>(valueExpression.value);
        std::uint32_t writtenValue = operandValue;
        switch (opcode)
        {
        case 0x78:
          writtenValue = currentValue & operandValue;
          break;
        case 0x79:
          writtenValue = currentValue | operandValue;
          break;
        case 0x7a:
          writtenValue = currentValue ^ operandValue;
          break;
        case 0x7b:
          writtenValue = currentValue + operandValue;
          break;
        case 0x7c:
          writtenValue = currentValue - operandValue;
          break;
        default:
          break;
        }

        writeObjectRegister(state, registerId, writtenValue);
        nextOffset = valueExpression.nextOffset;
        return true;
      }

      if (opcode == 0xb8)
      {
        return consumeExpression(scriptBytes, opcodeOffset + 1, options, state, nextOffset, stopReason);
      }

      if (opcode == 0x96 || opcode == 0xb9 || opcode == 0xba)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 3; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0x97 || opcode == 0x98)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 6; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0x99)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 3; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0x9a)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 8; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0x9d)
      {
        VmExpressionResult slotExpression = evaluateExpression(scriptBytes, opcodeOffset + 1, options, 0, state);
        if (slotExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = slotExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(slotExpression.status);
          return false;
        }
        if (!canRead(scriptBytes, slotExpression.nextOffset, sizeof(std::uint32_t)))
        {
          nextOffset = slotExpression.nextOffset;
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        const std::uint32_t slotIndex = static_cast<std::uint32_t>(slotExpression.value);
        if (slotIndex < 0x40 && slotIndex < state.scriptSlots.size())
        {
          state.scriptSlots[slotIndex] = 1;
        }

        nextOffset = slotExpression.nextOffset + sizeof(std::uint32_t);
        return true;
      }

      if (opcode == 0x9e)
      {
        VmExpressionResult slotExpression = evaluateExpression(scriptBytes, opcodeOffset + 1, options, 0, state);
        if (slotExpression.status != VmExpressionStatus::Success)
        {
          nextOffset = slotExpression.stopOffset;
          stopReason = traceStopFromVmExpressionStatus(slotExpression.status);
          return false;
        }

        const std::uint32_t slotIndex = static_cast<std::uint32_t>(slotExpression.value);
        if (slotIndex < state.scriptSlots.size())
        {
          state.scriptSlots[slotIndex] = 0;
        }

        nextOffset = slotExpression.nextOffset;
        return true;
      }

      if (opcode == 0xa1)
      {
        std::size_t pc = opcodeOffset + 1;
        if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
        {
          return false;
        }
        if (!canRead(scriptBytes, pc, sizeof(std::uint32_t)))
        {
          nextOffset = pc;
          stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return false;
        }

        nextOffset = pc + sizeof(std::uint32_t);
        return true;
      }

      if (opcode == 0xa2)
      {
        return consumeExpression(scriptBytes, opcodeOffset + 1, options, state, nextOffset, stopReason);
      }

      if (opcode == 0xbb)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 2; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      if (opcode == 0xbd)
      {
        std::size_t pc = opcodeOffset + 1;
        for (std::size_t componentIndex = 0; componentIndex < 4; ++componentIndex)
        {
          if (!consumeExpression(scriptBytes, pc, options, state, pc, stopReason))
          {
            return false;
          }
        }

        nextOffset = pc;
        return true;
      }

      stopReason = SceneScriptTraceStop::StandardOpcodeDispatch;
      return false;
    }

    SceneScriptTraceSummary traceSceneScriptEntrypoint(std::span<const std::uint8_t> scriptBytes,
                                                       std::size_t entryIndex,
                                                       std::uint32_t entryOffset,
                                                       const SceneScriptTraceOptions &options,
                                                       ScriptVmState &vmState,
                                                       orphen::ported::psm2::Psm2RuntimeState *terrainState)
    {
      SceneScriptTraceSummary trace;
      trace.entryIndex = entryIndex;
      trace.entryOffset = entryOffset;
      trace.stopOffset = entryOffset;

      if (entryOffset >= scriptBytes.size())
      {
        trace.stopReason = SceneScriptTraceStop::InvalidEntryOffset;
        return trace;
      }

      std::size_t pc = entryOffset;
      std::vector<std::size_t> returnStack;

      while (trace.steps < options.maxSteps)
      {
        if (pc >= scriptBytes.size())
        {
          trace.stopOffset = pc;
          trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
          return trace;
        }

        const std::uint8_t opcode = scriptBytes[pc];
        ++trace.steps;

        if (opcode < 0x0b)
        {
          if (opcode == 0x04)
          {
            if (returnStack.empty())
            {
              appendEvent(trace, {SceneScriptTraceEventKind::BlockEnd, pc, opcode, pc + 1, 0, returnStack.size()}, options);
              trace.stopOffset = pc + 1;
              trace.stopReason = SceneScriptTraceStop::Completed;
              return trace;
            }

            const std::size_t nextOffset = returnStack.back();
            returnStack.pop_back();
            appendEvent(trace, {SceneScriptTraceEventKind::BlockEnd, pc, opcode, nextOffset, 0, returnStack.size()}, options);
            pc = nextOffset;
            continue;
          }

          if (isNoopStructuralOpcode(opcode))
          {
            appendEvent(trace, {SceneScriptTraceEventKind::Noop, pc, opcode, pc + 1, 0, returnStack.size()}, options);
            pc += 1;
            continue;
          }

          if (isSkipInlineWordOpcode(opcode))
          {
            if (!canRead(scriptBytes, pc + 1, sizeof(std::uint32_t)))
            {
              trace.stopOffset = pc;
              trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
              return trace;
            }

            appendEvent(trace, {SceneScriptTraceEventKind::SkipInlineWord, pc, opcode, pc + 5, 0, returnStack.size()}, options);
            pc += 5;
            continue;
          }

          if (isRelativeAdvanceOpcode(opcode))
          {
            if (!canRead(scriptBytes, pc + 1, sizeof(std::uint32_t)))
            {
              trace.stopOffset = pc;
              trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
              return trace;
            }

            const std::int64_t delta = signedDeltaFromLeU32(readLeU32Unchecked(scriptBytes, pc + 1));
            std::size_t targetOffset = 0;
            if (!addRelativeOffset(pc + 1, delta, targetOffset) || targetOffset >= scriptBytes.size())
            {
              appendEvent(trace, {SceneScriptTraceEventKind::RelativeAdvance, pc, opcode, targetOffset, delta, returnStack.size()}, options);
              trace.stopOffset = pc;
              trace.stopReason = SceneScriptTraceStop::InvalidRelativeTarget;
              return trace;
            }

            appendEvent(trace, {SceneScriptTraceEventKind::RelativeAdvance, pc, opcode, targetOffset, delta, returnStack.size()}, options);
            pc = targetOffset;
            continue;
          }

          if (opcode == 0x01)
          {
            VmExpressionResult condition = evaluateExpression(scriptBytes, pc + 1, options, 0, vmState);
            if (condition.status != VmExpressionStatus::Success)
            {
              appendEvent(trace, {SceneScriptTraceEventKind::LowOpcodeRequiresVm, condition.stopOffset, condition.opcode, condition.nextOffset, 0, returnStack.size()}, options);
              trace.stopOffset = condition.stopOffset;
              trace.stopReason = traceStopFromVmExpressionStatus(condition.status);
              return trace;
            }

            if (condition.value == 0)
            {
              if (!canRead(scriptBytes, condition.nextOffset, sizeof(std::uint32_t)))
              {
                appendEvent(trace, {SceneScriptTraceEventKind::ConditionalBranch, pc, opcode, condition.nextOffset, condition.value, returnStack.size()}, options);
                trace.stopOffset = condition.nextOffset;
                trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
                return trace;
              }

              const std::int64_t delta = signedDeltaFromLeU32(readLeU32Unchecked(scriptBytes, condition.nextOffset));
              std::size_t targetOffset = 0;
              if (!addRelativeOffset(condition.nextOffset, delta, targetOffset) || targetOffset >= scriptBytes.size())
              {
                appendEvent(trace, {SceneScriptTraceEventKind::ConditionalBranch, pc, opcode, targetOffset, delta, returnStack.size()}, options);
                trace.stopOffset = condition.nextOffset;
                trace.stopReason = SceneScriptTraceStop::InvalidRelativeTarget;
                return trace;
              }

              appendEvent(trace, {SceneScriptTraceEventKind::ConditionalBranch, pc, opcode, targetOffset, delta, returnStack.size()}, options);
              pc = targetOffset;
              continue;
            }

            const std::size_t targetOffset = condition.nextOffset + sizeof(std::uint32_t);
            if (targetOffset > scriptBytes.size())
            {
              appendEvent(trace, {SceneScriptTraceEventKind::ConditionalBranch, pc, opcode, targetOffset, condition.value, returnStack.size()}, options);
              trace.stopOffset = condition.nextOffset;
              trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
              return trace;
            }

            appendEvent(trace, {SceneScriptTraceEventKind::ConditionalBranch, pc, opcode, targetOffset, condition.value, returnStack.size()}, options);
            pc = targetOffset;
            continue;
          }

          appendEvent(trace, {SceneScriptTraceEventKind::LowOpcodeRequiresVm, pc, opcode, pc + 1, 0, returnStack.size()}, options);
          trace.stopOffset = pc;
          trace.stopReason = SceneScriptTraceStop::RequiresVmEvaluation;
          return trace;
        }

        if (opcode == 0xff)
        {
          if (!canRead(scriptBytes, pc, 2))
          {
            trace.stopOffset = pc;
            trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return trace;
          }

          const std::uint16_t extendedOpcode = static_cast<std::uint16_t>(scriptBytes[pc + 1] + 0x100u);
          if (extendedOpcode == 0x149)
          {
            VmExpressionResult valueExpression = evaluateExpression(scriptBytes, pc + 2, options, 0, vmState);
            if (valueExpression.status != VmExpressionStatus::Success)
            {
              appendEvent(trace, {SceneScriptTraceEventKind::ExtendedOpcode, pc, extendedOpcode, valueExpression.nextOffset, 0, returnStack.size()}, options);
              trace.stopOffset = valueExpression.stopOffset;
              trace.stopReason = traceStopFromVmExpressionStatus(valueExpression.status);
              return trace;
            }

            appendEvent(trace, {SceneScriptTraceEventKind::ExtendedOpcode, pc, extendedOpcode, valueExpression.nextOffset, 0, returnStack.size()}, options);
            pc = valueExpression.nextOffset;
            continue;
          }

          appendEvent(trace, {SceneScriptTraceEventKind::ExtendedOpcode, pc, extendedOpcode, pc + 2, 0, returnStack.size()}, options);
          trace.stopOffset = pc;
          trace.stopReason = SceneScriptTraceStop::ExtendedOpcodeDispatch;
          return trace;
        }

        if (opcode == 0x32)
        {
          if (returnStack.size() >= options.maxReturnDepth)
          {
            trace.stopOffset = pc;
            trace.stopReason = SceneScriptTraceStop::ReturnStackLimitReached;
            return trace;
          }
          if (!canRead(scriptBytes, pc + 1, sizeof(std::uint32_t)))
          {
            trace.stopOffset = pc;
            trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return trace;
          }

          const std::int64_t delta = signedDeltaFromLeU32(readLeU32Unchecked(scriptBytes, pc + 1));
          std::size_t targetOffset = 0;
          const std::size_t continuationOffset = pc + 5;
          returnStack.push_back(continuationOffset);
          if (!addRelativeOffset(pc + 1, delta, targetOffset) || targetOffset >= scriptBytes.size())
          {
            appendEvent(trace, {SceneScriptTraceEventKind::BlockBegin, pc, opcode, targetOffset, delta, returnStack.size()}, options);
            trace.stopOffset = pc;
            trace.stopReason = SceneScriptTraceStop::InvalidRelativeTarget;
            return trace;
          }

          appendEvent(trace, {SceneScriptTraceEventKind::BlockBegin, pc, opcode, targetOffset, delta, returnStack.size()}, options);
          pc = targetOffset;
          continue;
        }

        if (opcode == 0xa4 || opcode == 0xa6)
        {
          VmExpressionResult matchExpression = evaluateExpression(scriptBytes, pc + 1, options, 0, vmState);
          if (matchExpression.status != VmExpressionStatus::Success)
          {
            appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, matchExpression.nextOffset, 0, returnStack.size()}, options);
            trace.stopOffset = matchExpression.stopOffset;
            trace.stopReason = traceStopFromVmExpressionStatus(matchExpression.status);
            return trace;
          }
          if (!canRead(scriptBytes, matchExpression.nextOffset, 1))
          {
            appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, matchExpression.nextOffset, 0, returnStack.size()}, options);
            trace.stopOffset = matchExpression.nextOffset;
            trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return trace;
          }

          const std::uint32_t matchMask = static_cast<std::uint32_t>(matchExpression.value);
          const std::uint8_t mode = scriptBytes[matchExpression.nextOffset];
          if (opcode == 0xa4)
          {
            ++trace.terrainMutations.opcodeA4Count;
            if (terrainState != nullptr)
            {
              trace.terrainMutations.record80FlagWrites += applyRecord80TerrainFlag(*terrainState, matchMask, mode, 0x20);
            }
          }
          else
          {
            ++trace.terrainMutations.opcodeA6Count;
            if (terrainState != nullptr)
            {
              trace.terrainMutations.record78LeadingWordWrites += applyRecord78LeadingWordFlag(*terrainState, matchMask, mode, 0x800);
            }
          }

          const std::size_t nextOffset = matchExpression.nextOffset + 1;
          appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, nextOffset, 0, returnStack.size()}, options);
          pc = nextOffset;
          continue;
        }

        if (opcode == 0xa5)
        {
          VmExpressionResult matchExpression = evaluateExpression(scriptBytes, pc + 1, options, 0, vmState);
          if (matchExpression.status != VmExpressionStatus::Success)
          {
            appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, matchExpression.nextOffset, 0, returnStack.size()}, options);
            trace.stopOffset = matchExpression.stopOffset;
            trace.stopReason = traceStopFromVmExpressionStatus(matchExpression.status);
            return trace;
          }
          if (!canRead(scriptBytes, matchExpression.nextOffset, 1))
          {
            appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, matchExpression.nextOffset, 0, returnStack.size()}, options);
            trace.stopOffset = matchExpression.nextOffset;
            trace.stopReason = SceneScriptTraceStop::OutOfBoundsRead;
            return trace;
          }

          const std::uint8_t mode = scriptBytes[matchExpression.nextOffset];
          VmExpressionResult flagExpression = evaluateExpression(scriptBytes, matchExpression.nextOffset + 1, options, 0, vmState);
          if (flagExpression.status != VmExpressionStatus::Success)
          {
            appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, flagExpression.nextOffset, 0, returnStack.size()}, options);
            trace.stopOffset = flagExpression.stopOffset;
            trace.stopReason = traceStopFromVmExpressionStatus(flagExpression.status);
            return trace;
          }

          ++trace.terrainMutations.opcodeA5Count;
          if (terrainState != nullptr)
          {
            trace.terrainMutations.record78FlagWrites += applyRecord78TerrainFlags(*terrainState,
                                                                                   static_cast<std::uint32_t>(matchExpression.value),
                                                                                   mode,
                                                                                   static_cast<std::uint32_t>(flagExpression.value));
          }

          appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, flagExpression.nextOffset, 0, returnStack.size()}, options);
          pc = flagExpression.nextOffset;
          continue;
        }

        std::size_t nextOffset = pc + 1;
        SceneScriptTraceStop stopReason = SceneScriptTraceStop::StandardOpcodeDispatch;
        if (skipKnownStandardOpcode(scriptBytes, pc, opcode, options, vmState, nextOffset, stopReason))
        {
          appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, nextOffset, 0, returnStack.size()}, options);
          pc = nextOffset;
          continue;
        }

        appendEvent(trace, {SceneScriptTraceEventKind::StandardOpcode, pc, opcode, nextOffset, 0, returnStack.size()}, options);
        trace.stopOffset = pc;
        trace.stopReason = stopReason;
        return trace;
      }

      trace.stopOffset = pc;
      trace.stopReason = SceneScriptTraceStop::StepLimitReached;
      return trace;
    }

  } // namespace

  std::vector<SceneScriptTraceSummary> traceSceneScriptEntrypoints(std::span<const std::uint8_t> scriptBytes,
                                                                   std::span<const std::uint32_t> entryOffsets,
                                                                   SceneScriptTraceOptions options)
  {
    std::vector<SceneScriptTraceSummary> traces;
    traces.reserve(entryOffsets.size());
    ScriptVmState vmState;
    for (std::size_t entryIndex = 0; entryIndex < entryOffsets.size(); ++entryIndex)
    {
      traces.push_back(traceSceneScriptEntrypoint(scriptBytes, entryIndex, entryOffsets[entryIndex], options, vmState, nullptr));
    }
    return traces;
  }

  std::vector<SceneScriptTraceSummary> traceSceneScriptEntrypoints(std::span<const std::uint8_t> scriptBytes,
                                                                   std::span<const std::uint32_t> entryOffsets,
                                                                   orphen::ported::psm2::Psm2RuntimeState &terrainState,
                                                                   SceneScriptTraceOptions options)
  {
    std::vector<SceneScriptTraceSummary> traces;
    traces.reserve(entryOffsets.size());
    ScriptVmState vmState;
    for (std::size_t entryIndex = 0; entryIndex < entryOffsets.size(); ++entryIndex)
    {
      traces.push_back(traceSceneScriptEntrypoint(scriptBytes, entryIndex, entryOffsets[entryIndex], options, vmState, &terrainState));
    }
    return traces;
  }

  std::string_view sceneScriptTraceStopName(SceneScriptTraceStop stopReason)
  {
    switch (stopReason)
    {
    case SceneScriptTraceStop::Completed:
      return "completed";
    case SceneScriptTraceStop::InvalidEntryOffset:
      return "invalid-entry";
    case SceneScriptTraceStop::OutOfBoundsRead:
      return "out-of-bounds";
    case SceneScriptTraceStop::StepLimitReached:
      return "step-limit";
    case SceneScriptTraceStop::ReturnStackLimitReached:
      return "return-stack-limit";
    case SceneScriptTraceStop::RequiresVmEvaluation:
      return "requires-vm-eval";
    case SceneScriptTraceStop::StandardOpcodeDispatch:
      return "standard-dispatch";
    case SceneScriptTraceStop::ExtendedOpcodeDispatch:
      return "extended-dispatch";
    case SceneScriptTraceStop::InvalidRelativeTarget:
      return "invalid-relative-target";
    }

    return "unknown";
  }

  std::string_view sceneScriptTraceEventName(SceneScriptTraceEventKind eventKind)
  {
    switch (eventKind)
    {
    case SceneScriptTraceEventKind::Noop:
      return "noop";
    case SceneScriptTraceEventKind::BlockBegin:
      return "block-begin";
    case SceneScriptTraceEventKind::BlockEnd:
      return "block-end";
    case SceneScriptTraceEventKind::RelativeAdvance:
      return "relative-advance";
    case SceneScriptTraceEventKind::SkipInlineWord:
      return "skip-inline-word";
    case SceneScriptTraceEventKind::LowOpcodeRequiresVm:
      return "low-op-requires-vm";
    case SceneScriptTraceEventKind::ConditionalBranch:
      return "conditional-branch";
    case SceneScriptTraceEventKind::StandardOpcode:
      return "standard-opcode";
    case SceneScriptTraceEventKind::ExtendedOpcode:
      return "extended-opcode";
    }

    return "unknown";
  }

} // namespace orphen::port
