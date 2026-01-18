/*
 * Text opcode 0x13 — FUN_00239760
 *
 * Summary:
 *   Processes a nested dialogue segment with a temporary color override (0x80606000 - cyan/teal),
 *   continuing until the stream reaches a null byte (0x00) and the text parameter index
 *   (DAT_00355c64) has not changed. After completing the nested processing, restores the original
 *   color and sets a timing adjustment flag (DAT_00355c40 = 1).
 *
 * Behavior:
 *   1. Save current state: index (DAT_00355c64) and color (DAT_00355c44)
 *   2. Advance cursor (DAT_00354e30) by 1 byte to skip opcode itself
 *   3. Call FUN_00238f18(0) to clear/filter glyph slots for layer 0
 *   4. Set temporary color: DAT_00355c44 = 0x80606000 (cyan/teal tone)
 *   5. Loop:
 *      - Call FUN_00237de8() to process one dialogue tick/glyph
 *      - Check current stream byte (*DAT_00354e30)
 *      - Continue while: byte != 0x00 OR index (DAT_00355c64) changed
 *   6. After loop: advance cursor by 1 more (skip null terminator)
 *   7. Set DAT_00355c40 = 1 (timing adjustment flag; affects text_op_01 behavior)
 *   8. Restore saved color to DAT_00355c44
 *   9. Call FUN_00238f98() to advance active glyph/overlay timers
 *
 * Globals Accessed:
 *   - DAT_00354e30 (char*): dialogue stream cursor pointer
 *   - DAT_00355c64 (int): text parameter load index (from text_op_00)
 *   - DAT_00355c44 (u32): current rendering color (RGBA/ABGR format)
 *   - DAT_00355c40 (int): timing adjustment flag
 *
 * External Calls:
 *   - FUN_00238f18(0): Clear or filter glyph slots (analyzed as dialogue_clear_or_filter_glyph_slots)
 *   - FUN_00237de8(): Process one dialogue tick (analyzed as dialogue_text_advance_tick)
 *   - FUN_00238f98(): Advance active overlay timers (decrements countdown fields in active slots)
 *
 * Loop Termination Conditions:
 *   The loop continues UNTIL BOTH:
 *   - Current stream byte is 0x00 (null terminator)
 *   - Text parameter index (DAT_00355c64) matches the saved value (no palette loads occurred)
 *
 *   This means the nested segment can trigger text_op_00 (palette loads) which increment
 *   DAT_00355c64, and the loop will continue even after hitting 0x00 until index stabilizes.
 *
 * Use Case:
 *   Primarily used to render speaker name headers on subtitles (e.g., "Orphen:", "Magnus:").
 *   The temporary color (0x80606000 - cyan/teal with 50% alpha) visually distinguishes the
 *   speaker label from the main dialogue text. The timing adjustment flag (DAT_00355c40) affects
 *   subsequent text_op_01 banner spawn positioning, likely to properly align the main dialogue
 *   below the speaker header
 *
 * PS2-specific Notes:
 *   - Color 0x80606000 in RGBA/ABGR format: R=0x00, G=0x60, B=0x60, A=0x80 (cyan/teal, 50% alpha)
 *   - Loop condition using saved index prevents premature termination if nested ops load parameters
 *   - FUN_00238f18(0) with param 0 likely clears all slots (layer -1 in modulo logic)
 *
 * Original signature:
 *   void FUN_00239760(void)
 */

#include <stdint.h>

// Globals (original names from globals.json)
extern char *DAT_00354e30;    // dialogue stream cursor
extern int DAT_00355c64;      // text parameter index
extern uint32_t DAT_00355c44; // rendering color (ARGB)
extern int DAT_00355c40;      // timing adjustment flag

// External functions
extern void FUN_00238f18(long param); // dialogue_clear_or_filter_glyph_slots
extern void FUN_00237de8(void);       // dialogue_text_advance_tick
extern void FUN_00238f98(void);       // advance_active_overlay_timers

// Analyzed implementation
void text_op_13_process_nested_dialogue_with_temp_color(void)
{
  // Save current state
  int savedIndex = DAT_00355c64;
  uint32_t savedColor = DAT_00355c44;

  // Advance past opcode byte
  DAT_00354e30 = (char *)((int)DAT_00354e30 + 1);

  // Clear/filter glyph slots for layer 0
  FUN_00238f18(0);

  // Set temporary color (cyan/teal with 50% alpha)
  DAT_00355c44 = 0x80606000;

  // Process nested dialogue until null terminator AND index unchanged
  char currentByte = *DAT_00354e30;
  while ((currentByte != '\0') || (savedIndex != DAT_00355c64))
  {
    FUN_00237de8(); // Process one dialogue tick
    currentByte = *DAT_00354e30;
  }

  // Skip null terminator
  DAT_00354e30 = DAT_00354e30 + 1;

  // Set timing adjustment flag (affects text_op_01 spawn behavior)
  DAT_00355c40 = 1;

  // Restore saved color
  DAT_00355c44 = savedColor;

  // Advance active glyph/overlay timers
  FUN_00238f98();

  return;
}

// Original FUN_00239760 exposed as:
void FUN_00239760(void)
{
  text_op_13_process_nested_dialogue_with_temp_color();
}
