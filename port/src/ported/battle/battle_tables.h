#pragma once

// The battle module's static tables, kept as one raw window of PS2 memory.
//
//   src/FUN_002432d8.c:47-51  clears all five of them, and its five sizes are
//                             what pins their extents:
//
//     FUN_00267e78(0x31d3c8, 1000);   // party records, stride 100  -> 10
//     FUN_00267e78(0x31d7b0,  600);   // control blocks, stride 0x3C -> 10
//     FUN_00267e78(0x31da08, 0xFC);   // per-member spell aux
//     FUN_00267e78(0x31dc18, 0xF0);   // button masks, stride 0x28   -> 6
//     FUN_00267e78(0x31dd08, 0x3C);   // cooldowns, stride 10        -> 6 x 5
//
// **These are modelled byte-accurately rather than as C++ structs**, which is
// not how the rest of the port handles state. Three reasons, all specific to
// this module:
//
//   - The fields alias. DAT_0031da3a is written both as `+ slot*2` (the player
//     pass, FUN_002432d8:124) and as `+ member*6 + slot*2` (the loop pass,
//     :248), and with six members the second overruns DAT_0031da54, which the
//     player pass also writes. Splitting them into named members would silently
//     *fix* an overlap the original has, and the first thing to read the wrong
//     value would look like a port bug somewhere else entirely.
//   - Ghidra spells one field at several labels depending on which function
//     touched it. Control block +0x0E is DAT_0031d7be indexed by member and
//     also DAT_00355cb8[2] through the cached pointer; +0x2C is
//     DAT_0031d7a0 + slot*0x3C in FUN_002493b8 and DAT_00355cb8[0x20] in
//     FUN_00249610. Addressing by PS2 address makes those the same place by
//     construction rather than by a naming decision that has to be got right.
//   - battle_logo_loaded.p2s is a save state taken inside a real battle, so the
//     whole window can be diffed against hardware in one memcmp.
//
// The window is 0x0031D300..0x0031DE00: every battle table plus the slack
// between them. Nothing outside it is battle-mutable.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace orphen::ported::battle
{

  // ---------------------------------------------------------------- addresses

  inline constexpr std::uint32_t kWindowBase = 0x0031D300;
  inline constexpr std::uint32_t kWindowSize = 0x00000B00;

  // FUN_00249610:28-31 caches four pointers into the first two arrays, all four
  // indexed by the entity 1-based party slot (+0x95), which is why the
  // decompilation bases sit one stride below the arrays proper:
  //
  //   DAT_00355cac = 0x31d364 + slot*100   == partyRecord(member) + 0x00
  //   DAT_00355cb0 = 0x31d38c + slot*100   == partyRecord(member) + 0x28
  //   DAT_00355cb4 = 0x31d774 + slot*0x3c  == controlBlock(member) + 0x00
  //   DAT_00355cb8 = 0x31d780 + slot*0x3c  == controlBlock(member) + 0x0C
  //
  // member == slot - 1 throughout.
  inline constexpr std::uint32_t kDAT_0031d3c8_partyRecords = 0x0031D3C8;
  inline constexpr std::uint32_t kPartyRecordStride = 100;
  inline constexpr std::uint32_t kPartyRecordCount = 10;

  inline constexpr std::uint32_t kDAT_0031d7b0_controlBlocks = 0x0031D7B0;
  inline constexpr std::uint32_t kControlBlockStride = 0x3C;
  inline constexpr std::uint32_t kControlBlockCount = 10;

  inline constexpr std::uint32_t kDAT_0031da08_spellAux = 0x0031DA08;
  inline constexpr std::uint32_t kSpellAuxSize = 0xFC;

  inline constexpr std::uint32_t kDAT_0031dc18_buttonMasks = 0x0031DC18;
  inline constexpr std::uint32_t kButtonMaskStride = 0x28;
  inline constexpr std::uint32_t kButtonMaskCount = 6;

  inline constexpr std::uint32_t kDAT_0031dd08_cooldowns = 0x0031DD08;
  inline constexpr std::uint32_t kCooldownStride = 10; // five u16 per member
  inline constexpr std::uint32_t kCooldownCount = 6;
  inline constexpr std::uint32_t kCooldownGuardIndex = 4; // DAT_0031dd10

  // ------------------------------------------- party record field offsets
  //
  // FUN_002458a8 fills +0x04..+0x10 from the entity; FUN_002432d8 writes the
  // class at +0x00 and the per-slot spell block at +0x14/+0x18.
  namespace record
  {
    inline constexpr std::uint32_t kClass00 = 0x00;      // short, FUN_002298d0 input
    inline constexpr std::uint32_t kTypeId04 = 0x04;     // abs(entity +0x00)
    inline constexpr std::uint32_t kFlags06 = 0x06;      // entity +0x02
    inline constexpr std::uint32_t kFlags08 = 0x08;      // entity +0x04
    inline constexpr std::uint32_t kState0a = 0x0A;      // entity +0x60
    inline constexpr std::uint32_t kSubState0c = 0x0C;   // entity +0x62
    inline constexpr std::uint32_t kAnim0e = 0x0E;       // entity +0x94 low byte
    inline constexpr std::uint32_t kPartySlot0f = 0x0F;  // entity +0x95
    inline constexpr std::uint32_t kEntity10 = 0x10;     // entity pointer -> slot index here
    inline constexpr std::uint32_t kSpellByte14 = 0x14;  // + slot, item record +0x07
    inline constexpr std::uint32_t kSpellBlock18 = 0x18; // + slot*4: u16 mask, u8 power, u8
    // DAT_00355cb0 is this record + 0x28; FUN_00249610 reads +0x0C and +0x10
    // off it, so those are the record fields below.
    // Everything from here down is spelled through DAT_00355cb0, which is this
    // record + 0x28 -- so a `DAT_00355cb0 + N` in the decompilation is
    // record + 0x28 + N, not record + 0x38 + N. Getting that wrong puts three
    // separate timers on top of the charge accumulator.
    inline constexpr std::uint32_t kFacing34 = 0x34;    // DAT_00355cb0 + 0x0C, float
    inline constexpr std::uint32_t kTurnFlags38 = 0x38; // DAT_00355cb0 + 0x10, u16 bit 0 = turning
    // DAT_00355cb0 + 0x14. FUN_00249108 clears it and FUN_00249128 accumulates
    // DAT_003555bc into it, capped at 0x2D00: **the charge**. FUN_00249270
    // divides it by 0x780, capped at 0x2580, into the level 0..4 that becomes
    // the effect entity's +0x94.
    inline constexpr std::uint32_t kChargeTimer3c = 0x3C;
    inline constexpr std::uint32_t kSwingTimer3e = 0x3E;  // DAT_00355cb0 + 0x16
    inline constexpr std::uint32_t kHitCounter40 = 0x40;  // DAT_00355cb0 + 0x18
    inline constexpr std::uint32_t kReturnTimer42 = 0x42; // DAT_00355cb0 + 0x1A
    inline constexpr std::uint32_t kAimMarker45 = 0x45;   // DAT_00355cb0 + 0x1D, signed byte
  } // namespace record

  // ----------------------------------------- control block field offsets
  namespace control
  {
    inline constexpr std::uint32_t kPartySlot00 = 0x00; // entity +0x95
    inline constexpr std::uint32_t kEntity08 = 0x08;    // DAT_0031d7b8
    // The two bytes everything turns on. FUN_002462c8 writes the pending one
    // and FUN_0024a360 spends it; +0x0F is the action the character is *in*.
    inline constexpr std::uint32_t kPendingAction0e = 0x0E; // DAT_0031d7be
    inline constexpr std::uint32_t kCurrentAction0f = 0x0F; // DAT_0031d7bf
    inline constexpr std::uint32_t kFlags10 = 0x10;         // DAT_0031d7c0, bits 0x10 / 0x20
    inline constexpr std::uint32_t kPosX14 = 0x14;          // s16, position * 10
    inline constexpr std::uint32_t kPosY16 = 0x16;
    inline constexpr std::uint32_t kPosZ18 = 0x18;
    inline constexpr std::uint32_t kAiHistory1d = 0x1D; // three bytes, FUN_0023fd30 target ring
    inline constexpr std::uint32_t kPosX26 = 0x26;
    inline constexpr std::uint32_t kPosY28 = 0x28;
    inline constexpr std::uint32_t kPosZ2a = 0x2A;
    // s16 target index into the entity pool, -1 = no target. FUN_00249610:170
    // takes the no-target branch on anything below 3.
    inline constexpr std::uint32_t kTarget2c = 0x2C; // DAT_0031d7a0 + slot*0x3C
    inline constexpr std::uint32_t kScriptYield2e = 0x2E;
    inline constexpr std::uint32_t kScriptPc30 = 0x30;
    inline constexpr std::uint32_t kScriptSub34 = 0x34;
    inline constexpr std::uint32_t kFlags38 = 0x38; // DAT_0031d7e8
  } // namespace control

  // ------------------------------------------- button mask field offsets
  //
  // FUN_002432d8 ORs DAT_0031d168[slot] into one of the four odd words by the
  // item kind byte; FUN_002462c8 reads the even word back as the mask the
  // release edge is tested against, so a press latches trigger -> held.
  namespace mask
  {
    inline constexpr std::uint32_t kHeldAttack00 = 0x00;    // 0x31dc18: kind == 0, latched
    inline constexpr std::uint32_t kTriggerAttack04 = 0x04; // 0x31dc1c
    inline constexpr std::uint32_t kHeldSpellA08 = 0x08;    // 0x31dc20: kind > 0, latched
    inline constexpr std::uint32_t kTriggerSpellA0c = 0x0C; // 0x31dc24
    inline constexpr std::uint32_t kHeldSpellB10 = 0x10;    // 0x31dc28: kind < 0 not -1, latched
    inline constexpr std::uint32_t kTriggerSpellB14 = 0x14; // 0x31dc2c
    inline constexpr std::uint32_t kHeldSpellC18 = 0x18;    // 0x31dc30: kind == -1, latched
    inline constexpr std::uint32_t kTriggerSpellC1c = 0x1C; // 0x31dc34
    inline constexpr std::uint32_t kHeldGuard20 = 0x20;     // 0x31dc38: Square, latched
    inline constexpr std::uint32_t kTriggerGuard24 = 0x24;  // 0x31dc3c, always DAT_0031d174
  } // namespace mask

  // ------------------------------------------------ spell aux, 0x0031DA08
  //
  // FUN_002432d8 writes four things per member here and FUN_00242df0 reads them
  // back to spawn the effect entities. Absolute addresses because the strides
  // disagree -- see the aliasing note at the top of the file.
  inline constexpr std::uint32_t kDAT_0031da22_itemIds = 0x0031DA22;     // + member*3 + slot
  inline constexpr std::uint32_t kDAT_0031da2e_kinds = 0x0031DA2E;       // + member*3 + slot, signed
  inline constexpr std::uint32_t kDAT_0031da3a_effectTypes = 0x0031DA3A; // + member*6 + slot*2, u16
  inline constexpr std::uint32_t kDAT_0031da54_family = 0x0031DA54;      // + slot*4, member 0 only
  // FUN_002462c8 stores which of the three spell buttons fired here, indexed by
  // the *1-based* party slot rather than by member.
  inline constexpr std::uint32_t kDAT_0031da65_selectedSlot = 0x0031DA65; // + partySlot
  inline constexpr std::uint32_t kDAT_0031da6c_memberFlags = 0x0031DA6C;  // + member*4, u32
  inline constexpr std::uint32_t kDAT_0031da0c_memberFlags2 = 0x0031DA0C; // + member*4, u32
  inline constexpr std::uint32_t kDAT_0031da7c_auraEntity = 0x0031DA7C;   // + member*4, type 0x118
  inline constexpr std::uint32_t kDAT_0031da9c_attackEntity = 0x0031DA9C; // + member*4
  inline constexpr std::uint32_t kDAT_0031daac_shieldEntity = 0x0031DAAC; // + member*4
  inline constexpr std::uint32_t kDAT_0031dabc_guardEntity = 0x0031DABC;  // + member*4, type 0x1C7
  inline constexpr std::uint32_t kDAT_0031da8c_slotEntity = 0x0031DA8C;   // + member*4, FUN_002d9ae8

  // Pool slots are stored where the original stored a pointer. -1 is "none",
  // which is what a null pointer meant.
  inline constexpr std::int32_t kNoEntity = -1;

  // ----------------------------------------------------------------- constants

  // DAT_0031d168[0..2] and DAT_0031d174, read out of the executable: {0x10,
  // 0x20, 0x40} then 0x80 -- Triangle, Circle, Cross, Square. Post-CONCAT11
  // layout, the same one platform/sdl_gl_window.cpp builds rawHeldPad in, so
  // these are pad bits directly and need no remap.
  inline constexpr std::uint32_t kDAT_0031d168_slotButtons[3] = {0x10, 0x20, 0x40};
  inline constexpr std::uint32_t kDAT_0031d174_guardButton = 0x80;

  // FUN_00248e48: `(n << 0x15) >> 0x10`, i.e. n * 32 sign-extended through a
  // short. Ticks, at the nominal 0x20 per frame.
  constexpr std::int16_t FUN_00248e48_arm_timer(std::int32_t frames)
  {
    const std::uint32_t shifted = static_cast<std::uint32_t>(frames) << 21;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(shifted) >> 16);
  }

  // FUN_00248e58: subtract this frame ticks, clamped at zero through an
  // unsigned wrap test rather than a comparison against zero.
  constexpr std::uint16_t FUN_00248e58_step_timer(std::uint16_t value, std::uint16_t frameTicks)
  {
    if (value == 0)
    {
      return 0;
    }
    const std::uint16_t stepped = static_cast<std::uint16_t>(value - frameTicks);
    return (value < stepped) ? std::uint16_t{0} : stepped;
  }

  // ------------------------------------------------------------------- storage

  class BattleTables
  {
  public:
    // FUN_00267e78 is the engine memset. Named for it so the five calls in
    // FUN_002432d8 transcribe one for one.
    void FUN_00267e78_clear(std::uint32_t address, std::size_t byteCount)
    {
      if (!contains(address, byteCount))
      {
        return;
      }
      std::memset(bytes_.data() + (address - kWindowBase), 0, byteCount);
    }

    void clearAll() { bytes_.fill(0); }

    template <typename T>
    T read(std::uint32_t address) const
    {
      if (!contains(address, sizeof(T)))
      {
        return T{};
      }
      T value{};
      std::memcpy(&value, bytes_.data() + (address - kWindowBase), sizeof(T));
      return value;
    }

    template <typename T>
    void write(std::uint32_t address, T value)
    {
      if (!contains(address, sizeof(T)))
      {
        return;
      }
      std::memcpy(bytes_.data() + (address - kWindowBase), &value, sizeof(T));
    }

    // For the save-state diff: the whole window, and the address it starts at.
    std::span<const std::uint8_t> window() const { return bytes_; }

    static constexpr std::uint32_t partyRecord(std::uint32_t member)
    {
      return kDAT_0031d3c8_partyRecords + member * kPartyRecordStride;
    }
    static constexpr std::uint32_t controlBlock(std::uint32_t member)
    {
      return kDAT_0031d7b0_controlBlocks + member * kControlBlockStride;
    }
    static constexpr std::uint32_t buttonMask(std::uint32_t member)
    {
      return kDAT_0031dc18_buttonMasks + member * kButtonMaskStride;
    }
    static constexpr std::uint32_t cooldown(std::uint32_t member, std::uint32_t index)
    {
      return kDAT_0031dd08_cooldowns + member * kCooldownStride + index * 2;
    }

  private:
    static constexpr bool contains(std::uint32_t address, std::size_t byteCount)
    {
      if (address < kWindowBase)
      {
        return false;
      }
      const std::uint32_t offset = address - kWindowBase;
      return offset <= kWindowSize && byteCount <= kWindowSize - offset;
    }

    std::array<std::uint8_t, kWindowSize> bytes_{};
  };

} // namespace orphen::ported::battle
