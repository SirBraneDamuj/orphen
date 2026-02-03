/*
 * Opcode 0x61 - Test Controller Button State
 * Original function: FUN_0025f4b8
 *
 * Tests if a controller button (or combination of buttons) is currently pressed.
 * Reads a button mask parameter, checks against current controller state, and
 * returns true/false based on whether the specified buttons are pressed.
 *
 * The opcode reads one immediate byte parameter that controls which buttons
 * are tested and from which controller port.
 *
 * BEHAVIOR:
 * 1. Evaluate one expression (typically just returns a selector or flags)
 * 2. Read one byte from instruction stream: bitmask_and_flags
 *    - Bits 0-6 (0x7F): Button mask to test
 *    - Bit 7 (0x80): Controller port selector
 *        0 = Test controller 1 buttons (DAT_0058bf1c)
 *        1 = Test controller 2 buttons (DAT_0058bf20)
 * 3. Check if entity pool is active: (DAT_0058bebc & 0x100) == 0
 * 4. Check button state:
 *    - If bit 7 clear: test_buttons = DAT_0058bf1c & button_mask
 *    - If bit 7 set:   test_buttons = DAT_0058bf20 & button_mask
 * 5. Return true if (test_buttons != 0), else false
 *
 * PARAMETERS (inline):
 * - expr (int expression) - Evaluated but result unused (may be selector for future use)
 * - button_flags (immediate byte):
 *     bits 0-6: Button mask (which buttons to test)
 *     bit 7:    Controller port (0=port1, 1=port2)
 *
 * RETURN VALUE:
 * bool: true if any specified button is pressed, false otherwise
 *
 * GLOBAL READS:
 * - DAT_00355cd0: Instruction pointer (advanced by 1 for immediate byte)
 * - DAT_0058bebc: Entity pool state/controller state flags
 *   - Bit 0x100: Entity pool inactive flag (test always fails if set)
 *   - Bit 0x001: Controller 1 active/enabled
 * - DAT_0058bf1c: Controller 1 button state (various bits for each button)
 * - DAT_0058bf20: Controller 2 button state (various bits for each button)
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator (result unused)
 *
 * CONTROLLER BUTTON MAPPINGS (typical PS2 layout):
 * Based on surrounding code patterns:
 * - 0x0001: Controller 1 enabled/active
 * - 0x0004: Triangle button
 * - 0x0008: X button
 * - 0x0020: Circle button
 * - 0x0080: Square button (likely)
 * - 0x0100: Start button
 * - 0x0280: L1/L2 triggers
 * - 0x1000: Up D-pad
 * - 0x2000: Right D-pad
 * - 0x4000: Down D-pad
 * - 0x8000: Left D-pad
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
 *   push 0              # Selector (unused)
 *   0x61 0x08          # Test button 0x08 (X), port 0
 *   jump_if_true skip_dialog
 *
 * Example 2: Test if Start button on controller 2
 *   push 0
 *   0x61 0x81          # Test button 0x01 (shifted), port 1 (bit 7 set)
 *   jump_if_false wait_for_input
 *
 * Example 3: Test multiple buttons (via mask)
 *   push 0
 *   0x61 0x0C          # Test buttons 0x0C (0x04|0x08 = Triangle|X)
 *   # Returns true if either Triangle OR X is pressed
 *
 * NOTES:
 * - Expression parameter is evaluated but never used (may be for future expansion)
 * - Entity pool inactive flag (0x100 in DAT_0058bebc) causes test to always fail
 * - Bit 7 of immediate byte selects controller port (0=port1, 1=port2)
 * - Button masks support testing multiple buttons at once (bitwise OR)
 * - Returns true if ANY of the masked buttons are pressed (not ALL)
 * - DAT_0058bebc bit 0 checks if controller 1 is enabled/active
 * - Function reads inverted button state (active-low typical for PS2 controllers)
 * - DAT_0058bf1c/bf20 are at entity pool base +0x60/+0x64 (controller state cached here)
 */

extern unsigned int DAT_0058bebc;   // Entity pool state + controller flags (bit 0x100, bit 0x001)
extern unsigned int DAT_0058bf1c;   // Controller 1 button state bitmask
extern unsigned int DAT_0058bf20;   // Controller 2 button state bitmask
extern unsigned char *DAT_00355cd0; // Instruction pointer (bytecode stream)

extern void FUN_0025c258(unsigned int *out_result); // Expression evaluator

bool test_controller_button_state(void) // orig: FUN_0025f4b8
{
  unsigned char button_flags;
  unsigned int expr_result;
  unsigned int controller_state;
  unsigned int button_mask;
  unsigned int test_result;
  bool is_pressed;

  // Read controller state (entity pool + controller flags)
  controller_state = DAT_0058bebc;

  // Evaluate expression (result unused - may be for future selector support)
  FUN_0025c258(&expr_result);

  // Read immediate byte: button mask (bits 0-6) + port selector (bit 7)
  button_flags = *DAT_00355cd0;
  DAT_00355cd0 = DAT_00355cd0 + 1; // Advance instruction pointer

  // Default: no button pressed
  is_pressed = false;

  // Check if entity pool is active (test disabled if bit 0x100 set)
  if ((controller_state & 0x100) == 0)
  {
    // Extract button mask (clear high bit used for port selection)
    button_mask = button_flags & 0x7F;

    // Check if we should test any buttons
    // (button_mask == 0 means "test if controller active")
    if ((button_mask == 0) ||
        ((controller_state & 1) == 0)) // Controller 1 inactive
    {
      is_pressed = false;
    }
    else
    {
      // Select controller port based on bit 7
      if ((button_flags & 0x80) != 0)
      {
        // Bit 7 set: Test controller 2 (port 1)
        controller_state = DAT_0058bf20;
      }
      else
      {
        // Bit 7 clear: Test controller 1 (port 0)
        controller_state = DAT_0058bf1c;
      }

      // Test if any of the masked buttons are pressed
      // Returns true if (button_state & mask) != 0
      test_result = controller_state & button_mask;
      is_pressed = (test_result != 0);
    }
  }

  return is_pressed;
}
