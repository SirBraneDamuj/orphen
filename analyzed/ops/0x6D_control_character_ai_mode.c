// Opcode 0x6D — control_character_ai_mode
// Original handler: FUN_0025fd10 (src/FUN_0025fd10.c)
//
// Summary
//   Reads one byte parameter from the secondary stream (pcGpffffbd60) and controls
//   main character AI/animation state based on the value. Different ranges trigger
//   different behavior modes:
//     - value < -2: Enable attack/battle mode, set animation state
//     - value == -2 or -1: Raise the cinematic letterbox bars
//     - value == 1: Disable battle mode, reset AI state
//
// Behavior by Parameter Value
//   - < 1 (including -2, -1, and 0):
//       • Clear bit 0 of DAT_0058beb8 (character flags)
//       • Call FUN_00225bf0(0x58beb0, 10, 1) - set animation state 10, parameter 1
//
//   - < -2 (e.g., -3, -4, etc.):
//       • Set bit 0 of DAT_0058beb8 (enable battle/attack mode)
//       • Check character type via FUN_002298d0(DAT_0058beb0)
//       • If player character (result == 0) and in valid context (DAT_0058c610 > 0):
//           - Set bit 0x4000 of DAT_0058c614 (battle mode flag)
//           - Set bit 0 of DAT_0058c618 (attack enabled flag)
//
//   - == -2 or -1 (when iGpffffb0e4 < 1):
//       • Set iGpffffb0e4 = 1 -- raise the CINEMATIC LETTERBOX BARS
//       • If value == -2: uGpffffbd8c = 0x780, the bars start already closed
//       • If value == -1: uGpffffbd8c = 0, the bars slide in
//       • If cGpffffb6e4 != 0: clear it and clear bit 0 of DAT_0058beb8
//     See analyzed/cinematic_letterbox_bars.c. This pair used to be documented
//     here as an "alignment/positioning mode" and a "timing parameter"; both
//     names were guesses, and FUN_0025cfb8 is what actually reads them.
//
//   - == 1:
//       • If DAT_0058bf10 == 10:
//           - Call FUN_00252d88(0x58beb0) - reset animation state to (0, 1)
//           - Check character type via FUN_002298d0
//           - If player character and in valid context:
//               • Clear bit 0x4000 of DAT_0058c614 (disable battle mode)
//               • Clear bit 0 of DAT_0058c618 (disable attack)
//       • If iGpffffb0e4 != 0: set it to -1, so the bars slide back out
//
//   - == 0: Set state 10/substate 1, with no extra battle/alignment flags
//   - > 1: Clear bit 0 of DAT_0058beb8 only
//
// Key Globals
//   pcGpffffbd60         - Secondary parameter stream pointer (advanced by 1)
//   DAT_0058beb0         - Main character entity base (0xEC stride structure)
//   DAT_0058beb8         - Character state flags (bit 0 = battle/attack mode)
//   DAT_0058bf10         - Character animation state ID
//   DAT_0058c610         - Character context/validity counter
//   DAT_0058c614         - Battle mode flags (bit 0x4000 = battle active)
//   DAT_0058c618         - Attack control flags (bit 0 = attack enabled)
//   iGpffffb0e4          - DAT_00355054, letterbox mode (0 off, 1 open, -1 closing)
//   uGpffffbd8c          - DAT_00355CFC, letterbox ramp, 0..0x780
//   cGpffffb6e4          - Special mode flag
//
// Related Functions
//   FUN_00225bf0(entity, state, param) - Set character animation state
//   FUN_00252d88(entity) - Reset character animation to default (state 0, param 1)
//   FUN_002298d0(char_id) - Get character type (0=player, others=NPC types)
//
// Usage Context
//   This opcode takes player control away and gives it back, and the -1 / -2 arm
//   is also how a cutscene raises the letterbox bars -- FUN_0025cfb8 draws them
//   from these two globals at the end of every script tick, and FUN_00238a08
//   reads the mode to move the subtitles clear of them.
//
// PS2-specific Notes
//   - Bit manipulation of character flags suggests hardware-level state tracking
//   - 0x780 is the letterbox ramp at full extension; FUN_0025cfb8 draws
//     `ramp >> 5` scanlines-of-448 per bar, so 60 units
//   - The entity base at 0x58beb0 is the main player character slot
//
// Returns: 0 (always)
//
// Original signature: undefined8 FUN_0025fd10(void)

#include <stdint.h>

// Globals (character system)
extern unsigned char *pcGpffffbd60; // secondary parameter stream
extern uint8_t DAT_0058beb0;        // main character entity base
extern uint16_t DAT_0058beb8;       // character state flags
extern uint16_t DAT_0058bf10;       // character animation state
extern int DAT_0058c610;            // character context counter
extern uint16_t DAT_0058c614;       // battle mode flags
extern uint16_t DAT_0058c618;       // attack control flags
extern int iGpffffb0e4;             // DAT_00355054, letterbox mode
extern unsigned int uGpffffbd8c;    // DAT_00355CFC, letterbox ramp
extern char cGpffffb6e4;            // special mode flag

// External functions
extern void FUN_00225bf0(int entity, unsigned short state, unsigned short param); // set animation state
extern void FUN_00252d88(uint8_t *entity);                                        // reset animation
extern long FUN_002298d0(short char_id);                                          // get character type

unsigned long opcode_0x6D_control_character_ai_mode(void)
{
  char mode_param = *pcGpffffbd60;
  pcGpffffbd60++;

  // Always clear battle mode flag initially
  DAT_0058beb8 &= 0xFFFE;

  if (mode_param < 1)
  {
    FUN_00225bf0((int)&DAT_0058beb0, 10, 1); // set animation state 10

    // === Enable/Configure AI/Battle Modes ===
    if (mode_param < -2)
    {
      // Enable battle/attack mode (-3, -4, etc.)
      DAT_0058beb8 |= 1;                       // enable battle mode flag

      long char_type = FUN_002298d0(DAT_0058beb0);
      if (char_type == 0 && DAT_0058c610 > 0) // player character in valid context
      {
        DAT_0058c614 |= 0x4000; // set battle active flag
        DAT_0058c618 |= 1;      // enable attack
      }
    }
    else if (mode_param < 0 && iGpffffb0e4 < 1) // -2 or -1
    {
      // Raise the letterbox bars
      iGpffffb0e4 = 1;

      if (mode_param == -2)
      {
        uGpffffbd8c = 0x780; // already closed
      }
      else if (mode_param == -1)
      {
        uGpffffbd8c = 0; // slide them in
      }

      if (cGpffffb6e4 != 0)
      {
        cGpffffb6e4 = 0;
        DAT_0058beb8 &= 0xFFFE; // ensure battle mode cleared
      }
    }
    // else mode_param == 0: just clear flag (already done above)
  }
  else if (mode_param == 1)
  {
    // === Disable Battle Mode ===
    if (DAT_0058bf10 == 10) // if in battle animation state
    {
      FUN_00252d88(&DAT_0058beb0); // reset to default animation

      long char_type = FUN_002298d0(DAT_0058beb0);
      if (char_type == 0 && DAT_0058c610 > 0) // player character
      {
        DAT_0058c614 &= 0xBFFF; // clear battle active flag (bit 0x4000)
        DAT_0058c618 &= 0xFFFE; // disable attack (bit 0)
      }
    }

    if (iGpffffb0e4 != 0)
    {
      iGpffffb0e4 = -1; // slide the letterbox bars back out
    }
  }
  // else mode_param > 1: flag already cleared, nothing more to do

  return 0;
}
