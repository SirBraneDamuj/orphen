/*
 * Global Variables for Orphen: Scion of Sorcery
 *
 * This file contains global variable declarations with meaningful names
 * derived from reverse engineering analysis of the game's code.
 *
 * Original DAT addresses are preserved in comments for reference.
 */

#ifndef ORPHEN_GLOBALS_H
#define ORPHEN_GLOBALS_H

#include <stdint.h>
#include <stdbool.h>

// ===== PS2 TYPE DEFINITIONS =====
// Common type definitions from Ghidra decompilation output

typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef char cchar;
typedef unsigned char byte;
typedef short sshort;
typedef long long long64;

// ===== DEBUG/FLAG SYSTEM GLOBALS =====

/*
 * Current flag type being viewed/edited in the debug menu
 * Values: 0=MFLG (Main), 1=BFLG (Battle), 2=TFLG (Town/Trigger), 3=SFLG (Story/System)
 * Original address: DAT_003550fc
 */
extern int g_currentFlagType;

/*
 * Currently selected flag index within the current flag type
 * Range varies by flag type (0-1023 for MFLG/SFLG, 0-223 for BFLG, 0-255 for TFLG)
 * Original address: DAT_00355100
 */
extern int g_selectedFlagIndex;

// ===== SYSTEM FUNCTION DISPATCH GLOBALS =====

/**
 * System function index/state
 * Used to select which function to call from system_function_table
 * Original: DAT_00354d2c
 */
extern int system_function_index;

/**
 * System function pointer array
 * Array of function pointers for different system states/modes
 * Original: PTR_FUN_00318a88
 */
extern void (**system_function_table)(void);

// ===== CONTROLLER INPUT GLOBALS =====

/*
 * Controller 1 input state (bitfield)
 * Bit flags:
 *   0x8000 - Left D-pad
 *   0x4000 - Down D-pad
 *   0x2000 - Right D-pad
 *   0x1000 - Up D-pad
 *   0x0008 - X button
 *   0x0004 - Triangle button
 * Original address: DAT_003555f4
 */
extern uint16_t g_controller1Input;

/*
 * Controller 2 input state (bitfield)
 * Bit flags:
 *   0x0100 - Start button
 *   0x0020 - Circle button
 * Original address: DAT_003555f6
 */
extern uint16_t g_controller2Input;

// ===== FLAG TYPE CONSTANTS =====

typedef enum
{
  FLAG_TYPE_MFLG = 0, // Map flags (offset: 0, max: 1024) - puzzles, interactions, cutscene progress
  FLAG_TYPE_BFLG = 1, // Battle flags (offset: 800, max: 224)
  FLAG_TYPE_TFLG = 2, // Treasure flags (offset: 1024, max: 256) - user theory
  FLAG_TYPE_SFLG = 3  // Story/System flags (offset: 1280, max: 1024)
} FlagType;

// ===== FLAG SYSTEM CONSTANTS =====

#define FLAG_OFFSET_MFLG 0     // Map flags base offset
#define FLAG_OFFSET_BFLG 800   // Battle flags base offset
#define FLAG_OFFSET_TFLG 0x400 // Treasure flags base offset (1024) - user theory
#define FLAG_OFFSET_SFLG 0x500 // Story/System flags base offset (1280)

#define FLAG_MAX_MFLG 0x400 // 1024 map flags - map-specific puzzle/interaction state
#define FLAG_MAX_BFLG 0xe0  // 224 battle flags
#define FLAG_MAX_TFLG 0x100 // 256 treasure flags - user theory
#define FLAG_MAX_SFLG 0x400 // 1024 story/system flags

// ===== CONTROLLER INPUT CONSTANTS =====

// Controller 1 (DAT_003555f4) button masks
#define CTRL1_LEFT 0x8000
#define CTRL1_DOWN 0x4000
#define CTRL1_RIGHT 0x2000
#define CTRL1_UP 0x1000
#define CTRL1_X 0x0008        // (TODO: this is actually L1)
#define CTRL1_TRIANGLE 0x0004 // (TODO: this is actually R1)

// Controller 2 (DAT_003555f6) button masks
#define CTRL2_START 0x0100
#define CTRL2_CIRCLE 0x0020

// ===== GAME FLAG ARRAY =====

/*
 * Game flags bit array - stores all game state flags
 * Capacity: ~18,424 flags (2,303 bytes * 8 bits per byte)
 * Used by get_flag_state() and flag management system
 *
 * Original address: DAT_00342b70
 */
extern unsigned char game_flags_array[2303];

/*
 * Game mode state indicator byte
 *
 * This single byte tracks the current game interaction mode and must survive
 * complete game state resets. Known values:
 * - 0x0C (12) = Normal gameplay/field exploration mode
 * - 0x00 (0)  = Dialog/conversation mode with NPCs
 * - Other values likely represent battle, menu, cutscene modes
 *
 * Value is preserved across flag system resets and major state transitions
 * to ensure the game can return to the correct mode after initialization.
 * Changes dynamically during gameplay based on player interactions.
 *
 * Original address: DAT_00342c8f
 */
extern unsigned char g_game_mode_state;

// ===== GAME MODE CONSTANTS =====

#define GAME_MODE_DIALOG 0x00 // Dialog/conversation with NPCs
#define GAME_MODE_FIELD 0x0C  // Normal field exploration/gameplay// ===== GRAPHICS/GPU SYSTEM GLOBALS =====

/*
 * GPU command buffer start pointer
 * Used by graphics primitive rendering system
 * Original address: DAT_70000000
 */
extern long *gpu_command_buffer_start;

/*
 * GPU command buffer current write position
 * Points to next available position in command buffer
 * Original address: DAT_70000004
 */
extern void *gpu_command_buffer_current;

/*
 * GPU command buffer end pointer
 * Marks the end of available buffer space
 * Original address: DAT_70000008
 */
extern int gpu_command_buffer_end;

/*
 * GPU interrupt counter
 * Used for synchronization and timing
 * Original address: DAT_7000000c
 */
extern int gpu_interrupt_counter;

// ===== MENU SYSTEM GLOBALS =====

/*
 * Menu availability check function pointer array
 *
 * This array contains 7 function pointers used by the menu system to determine
 * which menu items should be available to the player. Each function performs
 * specific availability checks based on game state, progression, flags, etc.
 *
 * Array layout (PTR_FUN_0031c3c0):
 * [0] = FUN_002320a8  - Button Configuration menu handler ✅ CONFIRMED
 * [1] = FUN_002324e0  - Screen Ratio menu handler (unknown criteria)
 * [2] = FUN_00232870  - Analog Controller Vibration menu handler (unknown criteria)
 * [3] = FUN_00232c08  - World Map Display menu handler (unknown criteria)
 * [4] = FUN_00232fa8  - Return to Title Screen menu handler (unknown criteria)
 * [5] = LAB_00233240  - Item menu handler (unknown criteria)
 * [6] = LAB_00233250  - Equip menu handler (unknown criteria)
 *
 * Usage:
 * - initialize_menu_system() iterates through this array to check availability
 * - Results are stored in menu_availability_mask as a bitfield
 * - NULL function pointers automatically mark items as unavailable
 * - Menu item 6 has special handling based on game_mode_state
 *
 * Memory address: 0x0031c3c0 - 0x0031c3d8 (7 pointers * 4 bytes each)
 * Original: PTR_FUN_0031c3c0
 */
extern undefined *menu_availability_functions[7];

// ===== DEBUG MENU SYSTEM GLOBALS =====

/*
 * Debug menu display text pointers
 * These point to strings that show "ON " or "OFF" status for debug options
 */
extern char *PTR_s_ON__POSITION_DISP_0031e7ac;    // Position display toggle text
extern char *PTR_s_ON__MINI_MAP_DISP_0031e7b0;    // Mini-map display toggle text
extern char *PTR_s_ON__SCR_SUBPROC_DISP_0031e7a8; // Screen subprocess display toggle text

/*
 * Debug menu color settings
 */
extern int DAT_0031e84c; // Menu item color
extern int DAT_0031e858; // Disabled/highlight color

/*
 * Debug menu state variables
 */
extern unsigned int uGpffffb128;  // Position display enable flag
extern unsigned char bGpffffb66d; // Debug display flags (bit 2=minimap, bit 7=subproc)
extern char cGpffffb663;          // Debug mode state flag
extern unsigned int uGpffffb124;  // Current menu selection
extern unsigned int uGpffffb11c;  // Selection processed flag
extern unsigned int uGpffffbdd8;  // Menu active flag
extern unsigned int uGpffffbdd0;  // Previous menu state
extern unsigned int uGpffffbdd4;  // Menu action value
extern unsigned int uGpffffb284;  // Saved action value
extern unsigned int uGpffffb12c;  // Action counter

// ===== BYTECODE EXECUTION SYSTEM =====

/*
 * Bytecode interpreter for script execution
 * Executes stack-based virtual machine instructions from script data
 * Supports arithmetic, logical, and control flow operations
 * Original function: FUN_0025c258
 */
extern void bytecode_interpreter(uint *result_param);

/*
 * Script instruction for reading game state flags or work memory
 * Two modes: array lookup (opcode 0x36) vs bitfield access (other opcodes)
 * Used by bytecode interpreter to access persistent game state data
 * Original function: FUN_0025d768
 */
extern uint script_read_flag_or_work_memory(void);

#endif // ORPHEN_GLOBALS_H
