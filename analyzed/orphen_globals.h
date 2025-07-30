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
  FLAG_TYPE_MFLG = 0, // Main flags (offset: 0, max: 1024)
  FLAG_TYPE_BFLG = 1, // Battle flags (offset: 800, max: 224)
  FLAG_TYPE_TFLG = 2, // Town/Trigger flags (offset: 1024, max: 256)
  FLAG_TYPE_SFLG = 3  // Story/System flags (offset: 1280, max: 1024)
} FlagType;

// ===== FLAG SYSTEM CONSTANTS =====

#define FLAG_OFFSET_MFLG 0     // Main flags base offset
#define FLAG_OFFSET_BFLG 800   // Battle flags base offset
#define FLAG_OFFSET_TFLG 0x400 // Town/Trigger flags base offset (1024)
#define FLAG_OFFSET_SFLG 0x500 // Story/System flags base offset (1280)

#define FLAG_MAX_MFLG 0x400 // 1024 main flags
#define FLAG_MAX_BFLG 0xe0  // 224 battle flags
#define FLAG_MAX_TFLG 0x100 // 256 town/trigger flags
#define FLAG_MAX_SFLG 0x400 // 1024 story/system flags

// ===== CONTROLLER INPUT CONSTANTS =====

// Controller 1 (DAT_003555f4) button masks
#define CTRL1_LEFT 0x8000
#define CTRL1_DOWN 0x4000
#define CTRL1_RIGHT 0x2000
#define CTRL1_UP 0x1000
#define CTRL1_X 0x0008
#define CTRL1_TRIANGLE 0x0004

// Controller 2 (DAT_003555f6) button masks
#define CTRL2_START 0x0100
#define CTRL2_CIRCLE 0x0020

// ===== GAME FLAG ARRAY =====

/*
 * Game flags bit array - stores all game state flags
 * Capacity: ~18,424 flags (2,303 bytes * 8 bits per byte)
 * Used by get_flag_state() and flag management system
 * Original address: DAT_00342b70
 */
extern unsigned char game_flags_array[2303];

// ===== GRAPHICS/GPU SYSTEM GLOBALS =====

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

#endif // ORPHEN_GLOBALS_H
