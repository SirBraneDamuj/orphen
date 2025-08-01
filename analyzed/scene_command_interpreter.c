/*
 * Scene Command Interpreter - FUN_0025bc68
 *
 * Core scene processing function that interprets bytecode commands from scene data.
 * This is a virtual machine that processes scene command sequences stored in the
 * scene data files. The commands control rendering, graphics operations, and
 * scene object behavior.
 *
 * The interpreter maintains:
 * - Current command pointer (pbGpffffbd60)
 * - Command parameter (uGpffffbd68)
 * - Call stack for nested command sequences
 * - Stack pointer management for subroutine calls
 *
 * Command Categories:
 * - Commands 0x00-0x0A: Basic operations via jump table PTR_LAB_0031e1f8
 * - Command 0xFF: Extended commands (0x100-0x1FF) via jump table PTR_LAB_0031e538
 * - Command 0x32: Subroutine call with stack management
 * - Commands 0x33+: Extended operations via jump table PTR_LAB_0031e228
 * - Command 0x04: Return from subroutine
 *
 * This is essentially a scene scripting virtual machine for the PS2 game engine.
 *
 * Original function: FUN_0025bc68
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern void FUN_0025c220(void); // scene_data_offset_operation

// Scene command interpreter globals (not yet in orphen_globals.h)
extern unsigned char *pbGpffffbd60; // Current command pointer in scene data
extern unsigned short uGpffffbd68;  // Current command parameter value

// Command jump tables
extern void **PTR_LAB_0031e1f8; // Basic command jump table (commands 0x00-0x0A)
extern void **PTR_LAB_0031e538; // Extended command jump table (commands 0x100-0x1FF)
extern void **PTR_LAB_0031e228; // High command jump table (commands 0x33+)

/*
 * Scene command interpreter and virtual machine
 *
 * Processes scene bytecode commands in sequence:
 * 1. Reads command byte from current position
 * 2. Dispatches to appropriate handler based on command value
 * 3. Advances command pointer and continues until termination
 *
 * Command format varies by type:
 * - Basic commands: [cmd_byte] (dispatched via jump table)
 * - Extended commands: [0xFF] [cmd_byte] (cmd_byte + 0x100 dispatched)
 * - Subroutine calls: [0x32] [4-byte-offset] (pushes return address)
 * - Returns: [0x04] (pops return address from stack)
 *
 * The interpreter uses a call stack to handle nested command sequences,
 * allowing for subroutines and complex scene scripting.
 *
 * @param scene_data_ptr Pointer to scene command sequence to execute
 */
void scene_command_interpreter(long scene_data_ptr)
{
  bool is_stack_full;
  unsigned char command_byte;
  unsigned char *next_command_ptr;
  void **call_stack_ptr;
  int stack_depth;

  // Initialize command pointer to scene data
  pbGpffffbd60 = (unsigned char *)scene_data_ptr;

  // Initialize call stack (16 levels deep)
  stack_depth = 0x10;

  if (scene_data_ptr != 0)
  {
    void *local_call_stack[16]; // Local call stack array
    call_stack_ptr = (void **)local_call_stack;

    do
    {
      // Read current command byte
      command_byte = *pbGpffffbd60;
      next_command_ptr = pbGpffffbd60 + 1;

      if (command_byte < 0x0B)
      {
        // Basic commands (0x00-0x0A)
        if (command_byte == 0x04)
        {
          // Return from subroutine
          is_stack_full = stack_depth == 0x10;
          stack_depth = stack_depth + 1;

          if (is_stack_full)
          {
            // Stack is full, terminate execution
            pbGpffffbd60 = next_command_ptr;
            return;
          }

          // Pop return address from call stack
          pbGpffffbd60 = (unsigned char *)*call_stack_ptr;
          call_stack_ptr = call_stack_ptr + 1;
        }
        else
        {
          // Execute basic command via jump table
          pbGpffffbd60 = next_command_ptr;
          (*(void (*)())PTR_LAB_0031e1f8[command_byte])();
        }
      }
      else if (command_byte == 0xFF)
      {
        // Extended command (0x100-0x1FF range)
        uGpffffbd68 = *next_command_ptr + 0x100;
        pbGpffffbd60 = pbGpffffbd60 + 2;
        (*(void (*)())PTR_LAB_0031e538[*next_command_ptr])();
      }
      else if (command_byte == 0x32)
      {
        // Subroutine call
        call_stack_ptr = call_stack_ptr - 1;
        *call_stack_ptr = pbGpffffbd60 + 5; // Push return address (after 4-byte offset)
        stack_depth = stack_depth - 1;
        pbGpffffbd60 = next_command_ptr;
        FUN_0025c220(); // Process 4-byte offset for subroutine jump
      }
      else
      {
        // High commands (0x33+ range)
        uGpffffbd68 = (unsigned short)command_byte;
        pbGpffffbd60 = next_command_ptr;
        (*(void (*)())PTR_LAB_0031e228[command_byte - 0x32])();
      }
    } while (pbGpffffbd60 != (unsigned char *)0x0);
  }

  return;
}
