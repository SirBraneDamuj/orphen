/*
 * ANALYSIS: Scene Flag Management Function (with Global Variables)
 *
 * This function appears to be a debug/deve  // Get flag state and render current selection
  result = get_flag_state(flagOffset + g_selectedFlagIndex);              // Get flag state
  sprintf_variadic(textBuffer, 0x34d8e8, g_selectedFlagIndex, result != 0); // Render flag info
  // Display formatted flag infoent menu for managing game flags.
 * It handles 4 different types of flags:
 * - MFLG (Main Flags) - General game state
 * - BFLG (Battle Flags) - Combat-related flags
 * - TFLG (Treasure Flags) - Treasure chest collection status (user theory)
 * - SFLG (Story/System Flags) - Story progression and system state
 *
 * Original function: FUN_0026a1b8
 * Address: 0x0026a1b8
 *
 * This version uses meaningful global variable names instead of DAT_ addresses.
 * See orphen_globals.h for variable declarations and constants.
 */

#include <stdbool.h>
#include <stdint.h>
#include "orphen_globals.h"

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned int uint;

// Function prototypes for referenced functions
extern void strcpy_custom(char *dest, char *src);
extern void strcat_custom(char *dest, char *src);
extern int strlen_custom(char *text);                                           // Calculate string length
extern void render_text_string(char *text, int x, int y);                       // Position/render text
extern int get_flag_state(int flagIndex);                                       // Get flag state
extern void sprintf_variadic(char *buffer, char *format, int index, int state); // Format flag info
extern void render_simple_primitive(int x, int y, int w, int h, int color);     // Draw colored box
extern int read_controller_input(int button_mask, int enable_sticky_input);     // Read controller input
extern uint toggle_flag_state(uint flag_id);                                    // Toggle flag state
extern void refresh_display(void);                                              // Refresh display

undefined4 SceneFlagManager_WithGlobals(void)
{
  bool isOutOfBounds;
  int textWidth;
  int textHalfWidth;
  undefined4 shouldContinue;
  long result;
  int maxFlagIndex;
  int flagOffset;
  undefined1 textBuffer[80];

  flagOffset = 0;

  // Initialize text rendering system
  strcpy_custom(textBuffer, 0x34d8d8); // Base debug text

  // Append appropriate flag type string to display
  if (g_currentFlagType == FLAG_TYPE_BFLG)
  {
    strcat_custom(textBuffer, 0x355110); // "BFLG" string
  }
  else if (g_currentFlagType < FLAG_TYPE_TFLG)
  {
    if (g_currentFlagType == FLAG_TYPE_MFLG)
    {
      strcat_custom(textBuffer, 0x355108); // "MFLG" string
    }
  }
  else if (g_currentFlagType == FLAG_TYPE_TFLG)
  {
    strcat_custom(textBuffer, 0x355118); // "TFLG" string
  }
  else if (g_currentFlagType == FLAG_TYPE_SFLG)
  {
    strcat_custom(textBuffer, 0x355120); // "SFLG" string
  }

  // Calculate position to center the text horizontally
  textWidth = (300 - strlen_custom(textBuffer)) / 2;

  // Render the flag type and current selection text
  render_text_string(textBuffer, textWidth, 0);

  // Set up flag index limits and offsets based on flag type
  switch (g_currentFlagType)
  {
  case FLAG_TYPE_BFLG:
    maxFlagIndex = FLAG_MAX_BFLG;  // 224 flags max
    flagOffset = FLAG_OFFSET_BFLG; // Base offset 800
    break;

  case FLAG_TYPE_TFLG:
    maxFlagIndex = FLAG_MAX_TFLG;  // 256 flags max
    flagOffset = FLAG_OFFSET_TFLG; // Base offset 1024
    break;

  case FLAG_TYPE_SFLG:
    maxFlagIndex = FLAG_MAX_SFLG;  // 1024 flags max
    flagOffset = FLAG_OFFSET_SFLG; // Base offset 1280
    break;

  case FLAG_TYPE_MFLG:
  default:
    maxFlagIndex = FLAG_MAX_MFLG;  // 1024 flags max
    flagOffset = FLAG_OFFSET_MFLG; // No offset
    break;
  }

  // Clamp flag index to valid range
  if (g_selectedFlagIndex >= maxFlagIndex)
  {
    g_selectedFlagIndex = maxFlagIndex - 1;
  }
  if (g_selectedFlagIndex < 0)
  {
    g_selectedFlagIndex = 0;
  }

  // Get flag state and render current selection
  result = get_flag_state(flagOffset + g_selectedFlagIndex);                // Get flag state
  sprintf_variadic(textBuffer, 0x34d8e8, g_selectedFlagIndex, result != 0); // Render flag info
  // Display formatted flag info
  render_text_string(textBuffer, textWidth + 0x10, 0xffffffffffffffec); // Position text

  // Draw selection boxes/indicators
  render_simple_primitive(textWidth + -4, 0, textHalfWidth + 4, 0x14, 0x6000);   // Top box
  render_simple_primitive(textWidth + -4, 4, textHalfWidth + 8, 0x30, 0x600000); // Bottom box

  // Read controller input
  result = read_controller_input(0xf00c, 0);
  if (result == 0)
    goto EXIT_CONTINUE;

  // Handle flag type navigation (Left/Right)
  if (g_controller1Input & CTRL1_LEFT) // Left - Previous flag type
  {
    g_currentFlagType--;
    if (g_currentFlagType < FLAG_TYPE_MFLG)
    {
      g_currentFlagType = FLAG_TYPE_SFLG; // Wrap to SFLG
    }
    goto EXIT_CONTINUE;
  }
  if (g_controller1Input & CTRL1_RIGHT) // Right - Next flag type
  {
    g_currentFlagType++;
    if (g_currentFlagType > FLAG_TYPE_SFLG)
    {
      g_currentFlagType = FLAG_TYPE_MFLG; // Wrap to MFLG
    }
    goto EXIT_CONTINUE;
  }

  // Handle flag index navigation
  if (g_controller1Input & CTRL1_UP) // Up - increment by 1
  {
    g_selectedFlagIndex++;
    if (g_selectedFlagIndex >= maxFlagIndex)
    {
      g_selectedFlagIndex = 0; // Wrap to beginning
    }
  }
  else if (g_controller1Input & CTRL1_DOWN) // Down - decrement by 1
  {
    g_selectedFlagIndex--;
    if (g_selectedFlagIndex < 0)
    {
      g_selectedFlagIndex = maxFlagIndex - 1; // Wrap to end
    }
  }
  else if (g_controller1Input & CTRL1_TRIANGLE) // Triangle - increment by 10
  {
    g_selectedFlagIndex += 10;
    if (g_selectedFlagIndex >= maxFlagIndex)
    {
      g_selectedFlagIndex = maxFlagIndex - 1; // Clamp to max
    }
  }
  else if (g_controller1Input & CTRL1_X) // X - decrement by 10
  {
    g_selectedFlagIndex -= 10;
    if (g_selectedFlagIndex < 0)
    {
      g_selectedFlagIndex = 0; // Clamp to min
    }
  }

EXIT_CONTINUE:
  shouldContinue = 0;

  // Check for exit or flag toggle
  if (!(g_controller2Input & CTRL2_START)) // Not Start button
  {
    if (g_controller2Input & CTRL2_CIRCLE) // Circle button - toggle flag
    {
      // Calculate final flag index with offset
      int finalFlagIndex;
      switch (g_currentFlagType)
      {
      case FLAG_TYPE_BFLG:
        finalFlagIndex = g_selectedFlagIndex + FLAG_OFFSET_BFLG;
        break;
      case FLAG_TYPE_TFLG:
        finalFlagIndex = g_selectedFlagIndex + FLAG_OFFSET_TFLG;
        break;
      case FLAG_TYPE_SFLG:
        finalFlagIndex = g_selectedFlagIndex + FLAG_OFFSET_SFLG;
        break;
      case FLAG_TYPE_MFLG:
      default:
        finalFlagIndex = g_selectedFlagIndex + FLAG_OFFSET_MFLG;
        break;
      }
      toggle_flag_state(finalFlagIndex); // Toggle the flag
    }
    refresh_display();  // Refresh display
    shouldContinue = 1; // Continue running
  }
  // If Start pressed, shouldContinue remains 0 (exit)

  return shouldContinue;
}
