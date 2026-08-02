#include "ported/script/scene_command_interpreter.h"

namespace orphen::ported::script
{

  bool SceneCommandInterpreter::canRead(std::size_t byteCount) const
  {
    return static_cast<std::size_t>(streamOffset_) + byteCount <= blob_.size();
  }

  std::uint8_t SceneCommandInterpreter::peekU8() const
  {
    return canRead(1) ? blob_[streamOffset_] : 0u;
  }

  std::uint8_t SceneCommandInterpreter::readU8()
  {
    if (!canRead(1))
    {
      halted_ = true;
      overran_ = true;
      return 0;
    }
    return blob_[streamOffset_++];
  }

  // FUN_0025c1d0. The alignment test in the original is an lwl/lwr idiom for an
  // unaligned load; both of its branches do the same thing. It advances 4, not 1
  // -- see analyzed/ops/0x4E_push_to_lookup_table.c for why that matters.
  std::uint32_t SceneCommandInterpreter::FUN_0025c1d0_readStreamU32()
  {
    if (!canRead(4))
    {
      halted_ = true;
      overran_ = true;
      streamOffset_ = static_cast<std::uint32_t>(blob_.size());
      return 0;
    }
    const std::uint32_t value = static_cast<std::uint32_t>(blob_[streamOffset_]) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(blob_[streamOffset_ + 3]) << 24);
    streamOffset_ += 4;
    return value;
  }

  // FUN_0025c220: DAT_00355cd0 += *DAT_00355cd0. The offset is relative to its
  // own position, and it is signed.
  void SceneCommandInterpreter::FUN_0025c220_relativeJump()
  {
    if (!canRead(4))
    {
      halted_ = true;
      overran_ = true;
      return;
    }
    const std::int32_t relative = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(blob_[streamOffset_]) |
        (static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) << 8) |
        (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 16) |
        (static_cast<std::uint32_t>(blob_[streamOffset_ + 3]) << 24));

    const std::int64_t target = static_cast<std::int64_t>(streamOffset_) + relative;
    if (target < 0 || static_cast<std::uint64_t>(target) >= blob_.size())
    {
      halted_ = true;
      overran_ = true;
      return;
    }
    streamOffset_ = static_cast<std::uint32_t>(target);
  }

  void SceneCommandInterpreter::alignStreamTo4()
  {
    const std::uint32_t remainder = streamOffset_ & 3u;
    if (remainder != 0)
    {
      streamOffset_ += 4u - remainder;
    }
  }

  bool SceneCommandInterpreter::FUN_0025bc68_run(std::uint32_t entryOffset)
  {
    if (entryOffset >= blob_.size())
    {
      return false;
    }

    streamOffset_ = entryOffset;
    halted_ = false;
    overran_ = false;

    std::uint32_t callStack[kCallStackDepth]{};
    // iVar5 in the original: counts down from 0x10 on a call and back up on a
    // block end. A block end while it is still 0x10 ends the routine.
    std::size_t depth = kCallStackDepth;
    std::size_t stackTop = 0;

    while (!halted_)
    {
      if (!canRead(1))
      {
        halted_ = true;
        overran_ = true;
        break;
      }

      const std::uint32_t opcodeOffset = streamOffset_;
      const std::uint8_t opcode = blob_[streamOffset_];

      if (opcode < 0x0B)
      {
        if (opcode == 0x04)
        {
          const bool atTopLevel = (depth == kCallStackDepth);
          ++depth;
          if (atTopLevel)
          {
            ++streamOffset_;
            trace_.recordOpcode(opcode, opcodeOffset, true);
            return true;
          }
          trace_.recordOpcode(opcode, opcodeOffset, true);
          streamOffset_ = callStack[--stackTop];
          continue;
        }

        ++streamOffset_;
        dispatchLow(opcode);
        trace_.recordOpcode(opcode, opcodeOffset, true);
        continue;
      }

      if (opcode == 0xFF)
      {
        if (!canRead(2))
        {
          halted_ = true;
          overran_ = true;
          break;
        }
        const std::uint8_t extension = blob_[streamOffset_ + 1];
        currentOpcode_ = static_cast<std::uint16_t>(extension + 0x100);
        streamOffset_ += 2;
        dispatchExtended(extension);
        continue;
      }

      if (opcode == 0x32)
      {
        // Push the continuation, which is five bytes on: the opcode plus the
        // rel32 the jump reads.
        if (stackTop >= kCallStackDepth)
        {
          halted_ = true;
          break;
        }
        callStack[stackTop++] = streamOffset_ + 5;
        --depth;
        ++streamOffset_;
        trace_.recordOpcode(opcode, opcodeOffset, true);
        FUN_0025c220_relativeJump();
        continue;
      }

      currentOpcode_ = opcode;
      ++streamOffset_;
      dispatchStandard(opcode);
    }

    return !overran_;
  }

  // FUN_0025bf70. Returns false when the next token is not a literal, leaving
  // the stream untouched so the caller falls through to the operator switch.
  bool SceneCommandInterpreter::FUN_0025bf70_decodeLiteral(std::uint32_t &value)
  {
    const std::uint8_t token = peekU8();
    literalToken_ = token;

    switch (token)
    {
    case 0x0C: // u8
      if (!canRead(2))
      {
        break;
      }
      value = blob_[streamOffset_ + 1];
      streamOffset_ += 2;
      return true;

    case 0x0D: // u16
      if (!canRead(3))
      {
        break;
      }
      value = static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) |
              (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 8);
      streamOffset_ += 3;
      return true;

    case 0x0E: // u32
      if (!canRead(5))
      {
        break;
      }
      value = static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) |
              (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 8) |
              (static_cast<std::uint32_t>(blob_[streamOffset_ + 3]) << 16) |
              (static_cast<std::uint32_t>(blob_[streamOffset_ + 4]) << 24);
      streamOffset_ += 5;
      return true;

    case 0x0F: // s32 * 100 -- the world-coordinate literal
      if (!canRead(5))
      {
        break;
      }
      {
        const std::int32_t raw = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(blob_[streamOffset_ + 1]) |
            (static_cast<std::uint32_t>(blob_[streamOffset_ + 2]) << 8) |
            (static_cast<std::uint32_t>(blob_[streamOffset_ + 3]) << 16) |
            (static_cast<std::uint32_t>(blob_[streamOffset_ + 4]) << 24));
        value = static_cast<std::uint32_t>(raw * 100);
      }
      streamOffset_ += 5;
      return true;

    case 0x10: // s16 * 1000
      if (!canRead(3))
      {
        break;
      }
      {
        const std::int16_t raw = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(blob_[streamOffset_ + 1]) |
            (static_cast<std::uint16_t>(blob_[streamOffset_ + 2]) << 8));
        value = static_cast<std::uint32_t>(raw * 1000);
      }
      streamOffset_ += 3;
      return true;

    case 0x11: // s16 degrees -> engine angle units: * 0xF570 / 0x168
      if (!canRead(3))
      {
        break;
      }
      {
        const std::int16_t raw = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(blob_[streamOffset_ + 1]) |
            (static_cast<std::uint16_t>(blob_[streamOffset_ + 2]) << 8));
        value = static_cast<std::uint32_t>((raw * 0xF570) / 0x168);
      }
      streamOffset_ += 3;
      return true;

    case 0x30: // rgb from three sub-expressions
    case 0x31: // rgba from four
    {
      ++streamOffset_;
      const std::uint32_t blue = FUN_0025c258_evaluate();
      const std::uint32_t green = FUN_0025c258_evaluate();
      const std::uint32_t red = FUN_0025c258_evaluate();
      const std::uint32_t alpha = (token == 0x31) ? FUN_0025c258_evaluate() : 0u;
      value = blue | (green << 8) | (red << 16) | (alpha << 24);
      return true;
    }

    default:
      return false;
    }

    halted_ = true;
    overran_ = true;
    return false;
  }

  // FUN_0025c258. Postfix evaluation over a small operand stack; 0x0B pops the
  // result and returns.
  std::uint32_t SceneCommandInterpreter::FUN_0025c258_evaluate()
  {
    constexpr std::size_t kOperandCapacity = 8;
    std::uint32_t operands[kOperandCapacity]{};
    std::size_t count = 0;

    const auto push = [&](std::uint32_t value)
    {
      if (count < kOperandCapacity)
      {
        operands[count++] = value;
      }
    };
    // top() is operands[count-1]; second() is operands[count-2]. The original's
    // puVar6[0] and puVar6[1] respectively, since its stack grows downward.
    const auto top = [&]() -> std::uint32_t & { return operands[count ? count - 1 : 0]; };
    const auto second = [&]() -> std::uint32_t & { return operands[count > 1 ? count - 2 : 0]; };

    while (!halted_)
    {
      // Statement opcodes inside an expression: dispatch and push the result.
      while (!halted_ && peekU8() > 0x31)
      {
        const std::uint8_t opcode = peekU8();
        if (opcode == 0xFF)
        {
          if (!canRead(2))
          {
            halted_ = true;
            overran_ = true;
            break;
          }
          const std::uint8_t extension = blob_[streamOffset_ + 1];
          currentOpcode_ = static_cast<std::uint16_t>(extension + 0x100);
          streamOffset_ += 2;
          push(dispatchExtended(extension));
        }
        else
        {
          currentOpcode_ = opcode;
          ++streamOffset_;
          push(dispatchStandard(opcode));
        }
      }
      if (halted_)
      {
        break;
      }

      std::uint32_t literal = 0;
      if (FUN_0025bf70_decodeLiteral(literal))
      {
        push(literal);
        continue;
      }
      if (halted_)
      {
        break;
      }

      const std::uint8_t token = peekU8();
      switch (token)
      {
      case 0x0B: // end of expression
        ++streamOffset_;
        return count ? operands[count - 1] : 0u;

      case 0x12: second() = (second() == top()) ? 1u : 0u; break;
      case 0x13: second() = (second() != top()) ? 1u : 0u; break;
      case 0x14: second() = (static_cast<std::int32_t>(second()) < static_cast<std::int32_t>(top())) ? 1u : 0u; break;
      case 0x15: second() = (static_cast<std::int32_t>(top()) < static_cast<std::int32_t>(second())) ? 1u : 0u; break;
      case 0x16: second() = (static_cast<std::int32_t>(top()) < static_cast<std::int32_t>(second())) ? 0u : 1u; break;
      case 0x17: second() = (static_cast<std::int32_t>(second()) < static_cast<std::int32_t>(top())) ? 0u : 1u; break;

      // Unary: rewrite the top without popping.
      case 0x18: top() = (top() == 0) ? 1u : 0u; ++streamOffset_; continue;
      case 0x19: top() = ~top(); ++streamOffset_; continue;
      case 0x1E: top() = static_cast<std::uint32_t>(-static_cast<std::int32_t>(top())); ++streamOffset_; continue;

      case 0x1A: second() = (second() == 0) ? 0u : ((top() != 0) ? 1u : 0u); break;
      case 0x1B:
      case 0x21: second() = second() | top(); break;
      case 0x1C: second() = second() + top(); break;
      case 0x1D: second() = second() - top(); break;
      case 0x1F: second() = second() ^ top(); break;
      case 0x20: second() = second() & top(); break;

      // The original traps on a zero divisor (the MIPS break). The port cannot
      // usefully trap, so it yields zero and halts -- a script that divides by
      // zero is a decode failure, not a game event.
      case 0x22:
        if (top() == 0)
        {
          halted_ = true;
          overran_ = true;
          return 0;
        }
        second() = static_cast<std::uint32_t>(static_cast<std::int32_t>(second()) / static_cast<std::int32_t>(top()));
        break;

      case 0x23: second() = second() * top(); break;

      case 0x24:
        if (top() == 0)
        {
          halted_ = true;
          overran_ = true;
          return 0;
        }
        second() = static_cast<std::uint32_t>(static_cast<std::int32_t>(second()) % static_cast<std::int32_t>(top()));
        break;

      default:
        // Not a literal, not an operator, not a statement: the stream is not
        // where the decoder thinks it is.
        halted_ = true;
        overran_ = true;
        return count ? operands[count - 1] : 0u;
      }

      if (count > 1)
      {
        --count; // pop one operand
      }
      ++streamOffset_;
    }

    return count ? operands[count - 1] : 0u;
  }

  // PTR_LAB_0031e1f8. See analyzed/structural_ops/structural_ops_dispatch_table.md;
  // several entries are aliases of one another in the original table.
  void SceneCommandInterpreter::dispatchLow(std::uint8_t opcode)
  {
    switch (opcode)
    {
    case 0x00:
    case 0x05:
    case 0x06:
      break; // LAB_0025bdc8, no-op

    case 0x01:
      FUN_0025bdd0_conditional_jump();
      break;

    case 0x02:
      FUN_0025be10_switch_dispatch();
      break;

    case 0x03:
    case 0x08:
    case 0x0A:
      FUN_0025c220_relativeJump();
      break;

    case 0x07:
    case 0x09:
      streamOffset_ += 4;
      if (streamOffset_ > blob_.size())
      {
        halted_ = true;
        overran_ = true;
      }
      break;

    default:
      break;
    }
  }

} // namespace orphen::ported::script
