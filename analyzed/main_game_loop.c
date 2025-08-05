/*
 * Main Game Loop - FUN_002239c8
 *
 * This is the core game loop function that handles the main update cycle for the game.
 * It processes input, updates game systems, handles debug output, and manages frame timing.
 * This function is called every frame and orchestrates all major game subsystems.
 *
 * Key responsibilities:
 * - Controller input processing and state management
 * - Debug menu and output handling
 * - Game state transitions and mode changes
 * - Entity processing and scene updates
 * - Graphics rendering coordination
 * - Frame timing and performance monitoring
 * - Memory management (scratchpad checks)
 *
 * Original function: FUN_002239c8
 * Address: 0x002239c8
 */

#include "orphen_globals.h"

// Forward declarations for referenced functions not yet analyzed
extern void FUN_0022b300(int param);                                     // Audio/sound system function
extern long FUN_0025d238(int param);                                     // System state check
extern void FUN_002f9308(int param1, int param2);                        // Graphics system function
extern void FUN_00305110(void);                                          // GPU/graphics wait function
extern void FUN_0022a418(void);                                          // Menu/UI system handler
extern void FUN_0022dd60(int param);                                     // Debug system function
extern void FUN_003050d8(long param1, int param2, int param3);           // Graphics command function
extern void process_controller_input(long enable_history_logging);       // process_controller_input (FUN_0023b5d8)
extern void debug_output_formatter(void *format_string, ...);            // debug_output_formatter (FUN_002681c0)
extern void FUN_002241e0(void);                                          // State transition handler
extern void FUN_00224f78(void);                                          // Menu state handler
extern void process_scene_with_work_flags(void);                         // Scene processing with work flags (FUN_0025b778)
extern void FUN_00251ed8(int param1, uint param2, uint param3);          // Graphics/rendering function
extern void FUN_00249610(int param);                                     // Alternate rendering function
extern void FUN_00224ff0(void);                                          // Frame update function
extern void FUN_00239ce0(void);                                          // Graphics processing function
extern void FUN_002d3218(void);                                          // System processing function
extern void FUN_0023bf28(void);                                          // Debug/menu processing function
extern void FUN_0023fd30(void);                                          // System processing function
extern void FUN_00208450(void);                                          // System function
extern void FUN_002261e0(void);                                          // System function
extern void FUN_00224060(void);                                          // System function
extern void FUN_0025b918(void);                                          // System function
extern void FUN_00216aa0(void);                                          // System function
extern void debug_output_coordinates_from_struct(int coordinate_struct); // Debug output function (FUN_00269fa8)
extern void FUN_0026a048(int param1, int param2, int param3);            // Debug output function
extern int FUN_0022a238(int param);                                      // Menu/data lookup function
extern long FUN_0030bd20(double param);                                  // Float conversion function
extern void FUN_002255b8(void);                                          // System function
extern void FUN_00237fc0(void);                                          // System function
extern void FUN_00208ee8(void);                                          // System function
extern void FUN_00208f28(void);                                          // System function
extern void FUN_0020c5a8(void);                                          // System function
extern void FUN_0020f3e0(void);                                          // System function
extern void FUN_002192c0(void);                                          // System function
extern void FUN_0020c290(void);                                          // System function
extern void FUN_00203aa0(int param);                                     // System function

// Global variables used by main game loop (not yet in orphen_globals.h)
extern uint uGpffffb0ec;                // Frame/system state flag
extern uint uGpffffb668;                // Input/system control flags
extern uint uGpffffb27c;                // System mode flags
extern int iGpffffadbc;                 // Game state/mode variable
extern int iGpffffb64c;                 // Frame counter/timer
extern uint uGpffffb655;                // System parameter
extern byte bGpffffb66d;                // Debug/system flags
extern int iGpffffb284;                 // Current map/scene ID
extern char DAT_003437a0[21];           // Action/command array (7 items * 3 bytes each)
extern uint uGpffffb68c;                // Input state change flags
extern char cGpffffb66a;                // Master enable/disable flag
extern uint uGpffffb684;                // Controller input state flags
extern char cGpffffadc0;                // State transition flag
extern int iGpffffb640;                 // System state parameter
extern char cGpffffb663;                // Debug/alternate mode flag
extern short sGpffffb052;               // System state value
extern uint uGpffffb688;                // Graphics parameter 1
extern uint uGpffffb68a;                // Graphics parameter 2
extern void (*DAT_0032536c)(int state); // Function pointer for system dispatch
extern long iGpffffb7bc;                // Performance timer
extern char cGpffffb128;                // Debug output enable flag
extern char cGpffffb66c;                // Debug output state flag
extern int iGpffffb288;                 // Menu/data index
extern float DAT_0058bed0;              // Debug position X
extern float DAT_0058bed4;              // Debug position Y
extern float DAT_0058bed8;              // Debug position Z
extern char cGpffffb66b;                // System processing flag
extern uint uGpffffb644;                // Frame counter
extern int iGpffffb648;                 // Frame accumulator
extern uint uGpffffb6c8;                // System timer (capped at 0x2932d880)
extern void *DAT_70000000;              // Scratchpad memory pointer 1
extern void *DAT_70000100;              // Scratchpad memory pointer 2 (expected value)
extern uint uGpffffb280;                // Map/scene parameter
extern int DAT_0058bebc;                // Debug value 1
extern int DAT_0058beb4;                // Debug value 2
extern int DAT_0058beb8;                // Debug value 3
extern short DAT_0058beb6;              // Debug value 4
extern int DAT_0058be90;                // Debug coordinates 1
extern int DAT_0058be94;                // Debug coordinates 2
extern int DAT_0058be98;                // Debug coordinates 3
extern int DAT_0058bc80;                // System data 1
extern int DAT_0058bc88;                // System data 2
extern short uGpffffb686;               // System state flags

/*
 * Main game loop function - called every frame
 *
 * This function coordinates all major game systems and handles the core update cycle.
 * It processes input, manages game state, handles debug output, and ensures proper
 * frame timing and system coordination.
 */
void main_game_loop(void)
{
  int iVar1;
  uint uVar2;
  long lVar3;
  uint uVar4;
  uint uVar5;
  uint uVar6;
  char *pcVar7;
  char *pcVar8;
  int iVar9;
  int iVar10;

  // Reset per-frame state
  uGpffffb0ec = 0;

  // Handle audio system control
  if ((uGpffffb668 & 0x40) != 0)
  {
    FUN_0022b300(0);
  }

  // Check for mode transition conditions
  if (((uGpffffb27c != 0) && (1 < iGpffffadbc - 9U)) &&
      (((uGpffffb27c & 2) == 0 || (lVar3 = FUN_0025d238(0), lVar3 != 0))))
  {
    // Initialize graphics/rendering systems
    FUN_002f9308(0, 0);
    FUN_00305110();
    FUN_0022a418();

    // Set frame timing parameters
    iGpffffb64c = 0x20;
    uGpffffb655 = 0xff;

    // Handle debug system activation
    if ((bGpffffb66d & 4) != 0)
    {
      FUN_0022dd60(0);
    }

    // Execute graphics command
    FUN_003050d8(0xffffffff8008403e, 0, 0);
  }

  // Process controller input for current frame
  process_controller_input(1);

  // Handle action/command array processing (skip certain indices)
  if ((iGpffffb284 != 0xc) && (iGpffffb284 != 0))
  {
    iVar1 = 0;
    do
    {
      iVar10 = iVar1 + 1;
      // Skip indices 1, 2, and 6
      if (((iVar1 != 6) && (iVar1 != 2)) && (iVar1 != 1))
      {
        pcVar8 = &DAT_003437a0 + iVar1 * 3; // 3-byte stride
        iVar9 = 2;
        pcVar7 = &DAT_003437a0;

        do
        {
          if (*pcVar8 == '\0')
          {
            // Initialize action array values based on index
            if (iVar1 == 3)
            {
              pcVar7[9] = 0x14; // Set value at offset 9
            }
            else if (iVar1 < 4)
            {
              if (iVar1 == 0)
              {
                *pcVar7 = 1; // Set first value
              }
            }
            else if (iVar1 == 4)
            {
              pcVar7[0xc] = 0x21; // Set value at offset 12
            }
            else if (iVar1 == 5)
            {
              pcVar7[0xf] = 0x2d; // Set value at offset 15
            }
          }
          pcVar7 = pcVar7 + 1;
          iVar9 = iVar9 - 1;
          pcVar8 = pcVar8 + 1;
        } while (-1 < iVar9);
      }
      iVar1 = iVar10;
    } while (iVar10 < 7);
  }

  // Handle input state change flags
  if ((uGpffffb68c & 1) != 0)
  {
    bGpffffb66d = bGpffffb66d ^ 1; // Toggle bit 0
  }
  if ((uGpffffb68c & 2) != 0)
  {
    bGpffffb66d = bGpffffb66d ^ 2; // Toggle bit 1
  }
  if (((uGpffffb68c & 4) != 0) && (bGpffffb66d = bGpffffb66d ^ 4, (bGpffffb66d & 4) != 0))
  {
    FUN_0022dd60(0); // Activate debug system
  }
  if ((uGpffffb68c & 0x10) != 0)
  {
    bGpffffb66d = bGpffffb66d ^ 0x10; // Toggle bit 4
  }

  // Handle stepping/debug mode processing
  if (cGpffffb66a != '\0')
  {
    if (((uGpffffb684 & 1) != 0) && ((uGpffffb684 & 0x100) != 0))
    {
      cGpffffadc0 = '\x01';
    }

    if (cGpffffadc0 != '\0')
    {
      debug_output_formatter(0x34be40); // Debug output: "Stepping\n"
      process_controller_input(1);

      if ((uGpffffb684 & 1) == 0)
      {
        cGpffffadc0 = '\0';
      }
      else
      {
        FUN_00305110();

        // Wait for input state change
        while (((uGpffffb684 & 1) != 0 && ((uGpffffb686 & 0x100) == 0)))
        {
          FUN_00203aa0(1);
          process_controller_input(1);
        }

        process_controller_input(0);
        if (iGpffffb640 != 0)
        {
          FUN_00203aa0(1);
        }
        FUN_003050d8(0xffffffff8008403e, 0, 0);
      }
      iGpffffb64c = 0x20;
    }
  }

  // Handle special game state processing
  if (iGpffffadbc != 0)
  {
    FUN_002241e0();
    goto LAB_00223fc8;
  }

  // Process menu state
  if (cGpffffb66a != '\0')
  {
    FUN_00224f78();
  }

  // Early exit for specific state
  if (iGpffffadbc == 7)
  {
    return;
  }

  // Execute system function dispatch (state 4)
  DAT_0032536c(4);

  // Core system updates
  process_scene_with_work_flags();

  // Graphics/rendering system selection
  if ((cGpffffb663 == '\0') || (sGpffffb052 == 0))
  {
    FUN_00251ed8(0x58beb0, uGpffffb688, uGpffffb68a);
  }
  else
  {
    FUN_00249610(0x58beb0);
  }

  // Frame processing functions
  FUN_00224ff0();
  FUN_00239ce0();
  FUN_002d3218();

  // Debug system processing
  if (cGpffffb663 != '\0')
  {
    FUN_0023bf28();
  }

  FUN_0023fd30();

  // Performance monitoring and debug output
  if ((bGpffffb66d & 4) != 0)
  {
    // Calculate and display performance percentage
    uVar2 = (uint)(((iGpffffb7bc + -0xdc9a00) / 100) * 10000) / 0x1ae14;
    debug_output_formatter(0x34be50, uVar2 / 100, uVar2 % 100); // Debug output: "GWORK:%d.%d%%\n"
  }

  // System processing functions
  FUN_00208450();
  FUN_002261e0();
  FUN_00224060();

  // Execute system function dispatch (state 5)
  DAT_0032536c(5);

  FUN_0025b918();
  FUN_00216aa0();

  // Debug information output
  if (cGpffffb128 != '\0')
  {
    if ((cGpffffb66a == '\0') || (cGpffffb66c != '\0'))
    {
      // Detailed debug output mode
      debug_output_coordinates_from_struct(0x58beb0);
      debug_output_formatter(0x34be80, DAT_0058bebc, DAT_0058beb4, DAT_0058beb8, DAT_0058beb6);
      debug_output_formatter(0x354d40);
      FUN_0026a048(DAT_0058be90, DAT_0058be94, DAT_0058be98);
      debug_output_formatter(0x354d48);
      debug_output_coordinates_from_struct(0x58c088);
      debug_output_formatter(0x34bea8, iGpffffb284, uGpffffb280); // Debug output: "MAP>(MP%02d%02d)\n"
    }
    else
    {
      // Simple debug output mode
      cGpffffb66c = 1;

      if (cGpffffb663 == '\0')
      {
        debug_output_formatter(0x34be60, iGpffffb284, uGpffffb280); // Debug output: "~MP%02d%02d"
      }
      else
      {
        iVar1 = FUN_0022a238(0xd);
        debug_output_formatter(0x354d38, *(short *)(iVar1 + iGpffffb288 * 0x10));
      }

      // Position debug output
      uVar4 = FUN_0030bd20(DAT_0058bed0 * 1000.0);
      uVar5 = FUN_0030bd20(DAT_0058bed4 * 1000.0);
      uVar6 = FUN_0030bd20(DAT_0058bed8 * 1000.0);
      debug_output_formatter(0x34be70, uVar4, uVar5, uVar6); // Debug output: "(%d,%d,%d)\n"

      cGpffffb66c = '\0';
    }
  }

  // Additional system processing
  FUN_002255b8();
  FUN_00237fc0();
  FUN_00208ee8();

  // Conditional system processing based on flags
  if ((cGpffffb66b == '\0') || ((uGpffffb684 & 2) == 0))
  {
  LAB_00223f8c:
    FUN_00208f28();
    FUN_0020c5a8();
    FUN_0020f3e0();
    FUN_002192c0();
    FUN_0020c290();
  }
  else
  {
    // Frame skipping logic based on input state
    if ((uGpffffb684 & 1) == 0)
    {
      uVar2 = uGpffffb644 & 0xf; // Every 16 frames
    }
    else
    {
      uVar2 = uGpffffb644 & 0x7f; // Every 128 frames
    }
    if (uVar2 == 0)
      goto LAB_00223f8c;
  }

  // Execute system function dispatch (state 6)
  DAT_0032536c(6);

LAB_00223fc8:
  // Frame counter and timing management
  uGpffffb644 = uGpffffb644 + 1;
  iGpffffb648 = iGpffffb648 + iGpffffb64c;

  // System timer management (with overflow protection)
  if ((iGpffffadbc != 2) && (uGpffffb6c8 = uGpffffb6c8 + iGpffffb64c, 0x2932d880 < uGpffffb6c8))
  {
    uGpffffb6c8 = 0x2932d880; // Cap timer at maximum value
  }

  // Scratchpad memory overflow detection and error reporting
  if (DAT_70000000 != &DAT_70000100)
  {
    debug_output_formatter(0x34bec0); // Debug output: "Spad not free!\n"
    DAT_70000000 = &DAT_70000100;     // Reset to expected value
  }

  return;
}

// String constants referenced in this function:
// 0x34be40: "Stepping\n" - Debug stepping mode message
// 0x34be50: "GWORK:%d.%d%%\n" - Performance monitoring format string
// 0x34be80: Format string for detailed debug output
// 0x34bea8: "MAP>(MP%02d%02d)\n" - Map display debug format
// 0x34be60: "~MP%02d%02d" - Simple map debug format
// 0x34be70: "(%d,%d,%d)\n" - Position debug format
// 0x34bec0: "Spad not free!\n" - Scratchpad memory error message
