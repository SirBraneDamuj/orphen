/*
 * Opcode 0x61 - Test Controller Button State
 * Original function: FUN_0025f4b8
 *
 * Tests if a controller/control-state bit (or combination of bits) is currently set.
 * Evaluates the bit mask expression, checks it against cached controller/control state,
 * and returns true/false based on whether any masked bits are set.
 *
 * The opcode also reads one immediate byte parameter that selects which cached
 * state word to test and how the controller-active gate is applied.
 *
 * BEHAVIOR:
 * 1. Evaluate one expression: bit mask to test
 * 2. Read one byte from instruction stream: selector_flags
 *    - Bits 0-6 (0x7F): if non-zero, require DAT_0058bebc bit 0 to be set
 *    - Bit 7 (0x80): cached state selector
 *        0 = Test controller 1 buttons (DAT_0058bf1c)
 *        1 = Test controller 2 buttons (DAT_0058bf20)
 * 3. Check if entity pool is active: (DAT_0058bebc & 0x100) == 0
 * 4. Check cached state:
 *    - If bit 7 clear: test_state = DAT_0058bf1c & mask_expr
 *    - If bit 7 set:   test_state = DAT_0058bf20 & mask_expr
 * 5. Return true if (test_state != 0), else false
 *
 * PARAMETERS (inline):
 * - mask_expr (int expression): bit mask to test against DAT_0058bf1c/bf20
 * - selector_flags (immediate byte):
 *     bits 0-6: controller-active gate selector (non-zero requires DAT_0058bebc bit 0)
 *     bit 7:    cached state selector (0=DAT_0058bf1c, 1=DAT_0058bf20)
 *
 * RETURN VALUE:
 * bool: true if any specified button is pressed, false otherwise
 *
 * GLOBAL READS:
 * - DAT_00355cd0: Instruction pointer (advanced by 1 for immediate byte)
 * - DAT_0058bebc: Entity pool state/controller state flags
 *   - Bit 0x100: Entity pool inactive flag (test always fails if set)
 *   - Bit 0x001: Controller 1 active/enabled
 * - DAT_0058bf1c: Cached controller/control state word
 * - DAT_0058bf20: Alternate cached controller/control state word
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator (result unused)
 *
 * CONTROLLER / CONTROL MASK NOTES:
 * - 0x0010, 0x0020, 0x0040, and 0x0080 appear together in SCR2 state-machine
 *   gates around cutscene triggers. Treat exact button/action names as unverified
 *   until the DAT_0058bf1c producer is fully analyzed.
 * - 0x1000, 0x2000, 0x4000, and 0x8000 are used elsewhere as digital direction
 *   bits for up/right/down/left-style input.
 *
 * USE CASES:
 * - Test if player pressed specific button in cutscene (skip dialog)
 * - Check for button combinations (L1+R1 for special moves)
 * - Conditional script branching based on player input
 * - Quick-time event (QTE) button prompts
 * - Debug/cheat code detection
 *
 * TYPICAL SCRIPT SEQUENCES:
 *
 * Example 1: Test if X button pressed
 *   push 0x08           # Mask expression
 *   0x61 0x01          # Test DAT_0058bf1c, require active controller gate
 *   jump_if_true skip_dialog
 *
 * Example 2: Test if Start button on controller 2
 *   push 0x100
 *   0x61 0x81          # Test DAT_0058bf20, require active controller gate
 *   jump_if_false wait_for_input
 *
 * Example 3: Test multiple buttons (via mask)
 *   push 0x0C
 *   0x61 0x01          # Returns true if either masked bit is set in DAT_0058bf1c
 *
 * NOTES:
 * - Expression parameter is the bit mask tested against DAT_0058bf1c/bf20
 * - Entity pool inactive flag (0x100 in DAT_0058bebc) causes test to always fail
 * - Bit 7 of immediate byte selects cached state word (0=DAT_0058bf1c, 1=DAT_0058bf20)
 * - Masks support testing multiple bits at once (bitwise OR)
 * - Returns true if ANY masked bit is set (not ALL)
 * - DAT_0058bebc bit 0 checks if controller 1 is enabled/active
 * - Function reads inverted button state (active-low typical for PS2 controllers)
 * - DAT_0058bf1c/bf20 are at entity pool base +0x60/+0x64 (controller state cached here)
 */

extern unsigned int DAT_0058bebc;   // Entity pool state + controller flags (bit 0x100, bit 0x001)
extern unsigned int DAT_0058bf1c;   // Cached controller/control state bitmask
extern unsigned int DAT_0058bf20;   // Alternate cached controller/control state bitmask
extern unsigned char *DAT_00355cd0; // Instruction pointer (bytecode stream)

extern void FUN_0025c258(unsigned int *out_result); // Expression evaluator

bool test_controller_button_state(void) // orig: FUN_0025f4b8
{
  unsigned char selector_flags;
  unsigned int mask_expr;
  unsigned int controller_state;
  unsigned int test_state;
  unsigned int test_result;
  bool is_pressed;

  // Read controller state (entity pool + controller flags)
  controller_state = DAT_0058bebc;

  // Evaluate the bit mask expression.
  FUN_0025c258(&mask_expr);

  // Read immediate byte: active gate selector (bits 0-6) + state selector (bit 7)
  selector_flags = *DAT_00355cd0;
  DAT_00355cd0 = DAT_00355cd0 + 1; // Advance instruction pointer

  // Default: no button pressed
  is_pressed = false;

  // Check if entity pool is active (test disabled if bit 0x100 set)
  if ((controller_state & 0x100) == 0)
  {
    if (((selector_flags & 0x7F) == 0) || ((controller_state & 1) != 0))
    {
      // Select cached state word based on bit 7
      if ((selector_flags & 0x80) != 0)
      {
        // Bit 7 set: test alternate cached state word.
        controller_state = DAT_0058bf20;
      }
      else
      {
        // Bit 7 clear: test primary cached state word.
        controller_state = DAT_0058bf1c;
      }

      // Test if any masked bits are set.
      test_state = controller_state & mask_expr;
      test_result = test_state;
      is_pressed = (test_result != 0);
    }
  }

  return is_pressed;
}
