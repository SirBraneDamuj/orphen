// Battle Logo State Manager (analyzed)
// Original: FUN_00271220
// Address: 0x00271220
//
// Summary:
// - Multi-mode state machine for battle/scene setup and logo management.
// - Mode 3 (battle init) checks flag 0x511 and initializes alternate logo (type 0x48).
// - Manages entity positioning, camera setup, and state transitions.
// - Different from opcode 0xDF which creates type 0x49 logo.
//
// Modes (param_1):
//   0: Reset/cleanup mode
//      - Saves state to DAT_00355d50
//      - Resets counters and flags
//      - Calls FUN_002294d0 if map changed
//      - Calls FUN_002294b8 for cleanup
//
//   3: Battle initialization mode
//      - Calculates entity positions and camera angles
//      - Checks flag 0x511 (logo already shown?)
//      - If flag 0x511 NOT set:
//         * Sets flag 0x511 (mark logo as shown)
//         * Creates logo entity at 0x58c7e8 with TYPE 0x48 (alternate logo!)
//         * Sets flag bits 0x100 and 0x40
//         * Handles flag 0x0e for additional state
//      - Sets DAT_00342c8e if DAT_00354ba8 enabled
//
//   4: Update/tick mode
//      - Calls function from table PTR_FUN_003256f8[DAT_00342b7d]
//      - Updates DAT_00342b7d based on return value
//
//   7: Save state mode
//      - Saves DAT_00355d50 to DAT_003555dc
//
// Logo Entity Types:
//   Type 0x49: Created by opcode 0xDF (FUN_0025d5b8)
//              - "SORCEROUS STABBER ORPHEN" battle intro logo
//              - Used in general battle setup
//
//   Type 0x48: Created by this function in mode 3
//              - Alternate logo or splash screen
//              - Only shown if flag 0x511 is NOT set
//              - Flag 0x511 prevents re-showing across sessions
//
// Flag System:
//   0x511 (1297): "Logo already shown" flag
//                 - Category: SFLG (Story/System Flags) index 17
//                 - SFLG base offset is 0x500 (1280), so 1297 - 1280 = index 17
//                 - Prevents re-displaying logo if already seen
//                 - Set by FUN_002663a0(0x511) first time through
//                 - Checked via FUN_00266368(0x511)
//                 - Debug Menu: SFLG category, index 17 (0x11)
//
//   0x0e (14): Additional logo/state flag
//              - Category: MFLG (Main Flags) index 14
//              - Set if not already enabled
//              - Sets flag bit 0x80 at offset +0x06 in entity
//
// Entity Setup (Mode 3):
//   Address: 0x58C7E8 (same as type 0x49 logo)
//   Type: 0x48 (alternate logo variant)
//   Flags set:
//     - Bit 0x100 at offset +0x04 (DAT_0058c7ec)
//     - Bit 0x40 at offset +0x08 (DAT_0058c7f0)  
//     - Bit 0x80 at offset +0x06 if flag 0x0e set (DAT_0058c7ee)
//   Additional setup:
//     - Sets DAT_0058c810 = DAT_00352ec4
//
// Position Calculations (Mode 3):
//   - Reads entity position from pool (DAT_0058bed0 + offset)
//   - Applies DAT_00354c98 scale factor to sin/cos of angles
//   - Calls FUN_00217d70 with calculated positions
//   - Calls FUN_00217e18(0) for rendering setup
//   - Uses FUN_00216690 for angle normalization
//
// Context:
//   - Called via function pointer table PTR_LAB_003252b8 at index 0x0C (12)
//   - Dispatcher FUN_0022a360 reads index from param_1+2 and calls table[index]
//   - FUN_0022a360 called from FUN_0022a418 (battle/scene system)
//   - Mode 3 specifically for battle start with logo display
//   - Flag 0x511 system ensures one-time logo display per session
//   - Type 0x48 vs 0x49 distinction suggests different logo variants
//   - Both types use same memory address (0x58c7e8) but exclusive
//
// Call Chain from Main Game Loop:
//   1. main_game_loop() [FUN_00204db8] - Line ~143
//        ↓ calls FUN_0022a418() during mode transitions
//   
//   2. FUN_0022a418() [battle/scene system] - Lines 105, 369
//        ↓ Line 105: uVar10 = FUN_0022a288(DAT_003551f4, DAT_003551f0);
//        ↓ Line 105: FUN_0022a360(uVar10);  ← Sets DAT_0032536c function pointer
//        ↓ Line 101: (*DAT_0032536c)(7);    ← Calls mode 7 (save state)
//        ↓ Line 369: (*DAT_0032536c)(3);    ← Calls mode 3 (battle init + logo)
//        ↓ Line 118: FUN_002663d8(0x511);   ← CLEARS flag 0x511 under certain conditions!
//   
//   3. FUN_0022a288(map_id, area_id) - Struct lookup
//        ↓ Returns pointer to scene descriptor struct
//        ↓ Struct contains index at offset +2 (value 0x0C for this handler)
//   
//   4. FUN_0022a360(descriptor_struct) - Dispatcher
//        ↓ Reads index from struct+2
//        ↓ Sets DAT_0032536c = PTR_LAB_003252b8[index]
//        ↓ When index == 0x0C, points to FUN_00271220 (this function)
//   
//   5. (*DAT_0032536c)(mode) - Indirect function call
//        ↓ Calls FUN_00271220 with mode parameter
//        ↓ Mode 3: Battle init, checks flag 0x511, creates type 0x48 logo
//        ↓ Mode 7: Save state
//        ↓ Mode 0: Reset/cleanup
//        ↓ Mode 4: Update/tick
//
// Flag 0x511 Lifecycle:
//   - CLEARED in FUN_0022a418:118 when entering certain map/area combos
//   - CHECKED in this function mode 3 - if SET, logo skipped
//   - SET in this function mode 3 - after first logo display
//   - Acts as "logo already shown for this session" flag
//
// Function Table PTR_LAB_003252b8 (0x003252b8):
//   [0x00] LAB_0026d148
//   [0x01] FUN_0026d468
//   [0x02] LAB_00272110
//   [0x03] FUN_0026c370
//   [0x04] FUN_0026c468
//   [0x05] FUN_0026c228
//   [0x06] FUN_0026c600
//   [0x07] FUN_00272850
//   [0x08] LAB_0026e760
//   [0x09] LAB_0026c2a0
//   [0x0A] FUN_0026c0f0
//   [0x0B] FUN_0026c568
//   [0x0C] FUN_00271220 ← This function
//   [0x0D] LAB_0026d618
//   [0x0E] FUN_0026c1a0
//   [0x0F] FUN_0026c870
//   ... (table continues)
//
// Notes:
//   - Function pointer stored in DAT_0032536c by FUN_0022a360
//   - Called with mode parameter (0, 3, 4, 7) from battle system
//   - Flag 0x511 can be cleared between battles, allowing logo to show again
//   - Type 0x48 might be JP-specific "first battle" or "chapter intro" logo
//   - US version likely removed by not setting index 0x0C in descriptor structs
//   - Complex positioning math suggests camera/viewport setup for logo display
//   - Scene descriptor structs returned by FUN_0022a288 control which handler is used
//
// Original signature: void FUN_00271220(int param_1)

#include <stdint.h>

// Mode-specific setup functions
extern void FUN_002294d0(void); // Map change handler
extern void FUN_002294b8(void); // Cleanup/reset handler
extern void FUN_00217e18(int param); // Rendering setup
extern void FUN_00217d70(float x1, float y1, float z1, float x2, float y2, float z2); // Position/camera setup
extern uint32_t FUN_00216690(float angle); // Angle normalization

// Trigonometric helpers
extern float FUN_00305130(uint32_t param); // sin calculation
extern float FUN_00305218(uint32_t param); // cos calculation

// Flag system
extern long FUN_00266368(uint32_t flag_index); // get_flag_state
extern void FUN_002663a0(uint32_t flag_index); // set_flag_state

// Entity initializer
extern void FUN_00229c40(uint64_t entity_addr, long entity_type);

// Function dispatch table
extern void (*PTR_FUN_003256f8[])(uint32_t);

// Global state variables (many not yet in orphen_globals.h)
extern uint32_t DAT_00355d50;
extern uint32_t DAT_003555dc;
extern uint16_t DAT_003555c8;
extern uint16_t DAT_003555c6;
extern uint32_t _DAT_00355054;
extern uint32_t DAT_003551f0;
extern uint32_t DAT_00354d7c;
extern uint32_t DAT_003551f4;
extern uint32_t DAT_00354d78;
extern uint32_t DAT_00355060;
extern float DAT_00354c98; // Scale factor
extern float DAT_00352ec4; // Config value
extern float DAT_00352ec0; // Angle offset
extern uint32_t DAT_00355644; // Base angle
extern uint8_t DAT_00342b7d; // State index
extern uint8_t DAT_00354ba8; // Enable flag
extern uint8_t DAT_00342c8e; // State flag

// Entity data arrays (stride 0x1d8 = 472 bytes)
extern uint32_t DAT_0058c04c[]; // Entity data array base
extern float DAT_0058bed0[]; // Entity positions X (stride 0x76 shorts = 0x1D8 bytes / 4)
extern float DAT_0058bed4[]; // Entity positions Y
extern float DAT_0058bed8[]; // Entity positions Z
extern float DAT_0058befc[]; // Entity angles/rotations
extern float DAT_0058bf08[]; // Entity angle offsets
extern uint32_t DAT_0058bf0c[]; // Entity angle results

// Logo entity at fixed address
extern uint16_t DAT_0058c7e8; // Entity ID
extern uint16_t DAT_0058c7ec; // +0x04: Flags
extern uint16_t DAT_0058c7ee; // +0x06: Flags
extern uint16_t DAT_0058c7f0; // +0x08: Flags
extern float DAT_0058c810; // +0x28: Config value

// Original signature: void FUN_00271220(int param_1)
void battle_logo_state_manager(int mode)
{
  if (mode == 0)
  {
    // Mode 0: Reset/cleanup
    DAT_00355d50 = DAT_003555dc;
    DAT_003555c8 = 0;
    DAT_003555c6 = 0;
    _DAT_00355054 = 0;
    
    // Check if map changed and handle transition
    if (((DAT_003551f0 != DAT_00354d7c) || (DAT_003551f4 != DAT_00354d78)) && 
        (DAT_00354d78 != 0xc))
    {
      FUN_002294d0();
    }
    
    FUN_002294b8();
    return;
  }
  
  if (mode == 3)
  {
    // Mode 3: Battle initialization with logo display
    
    // Get entity index from scene work data
    int entity_idx = *(int *)(DAT_00355060 + 4);
    
    // Read entity rotation angle
    uint32_t rotation = *(uint32_t *)(&DAT_0058c04c + entity_idx * 0x1d8);
    
    // Setup rendering context
    FUN_00217e18(0);
    
    // Calculate position with trigonometric adjustments
    float sin_val = (float)FUN_00305130(rotation);
    float x_pos = (float)(&DAT_0058bed0)[entity_idx * 0x76] - DAT_00354c98 * sin_val;
    
    float cos_val1 = (float)FUN_00305218(rotation);
    float y_pos = (float)(&DAT_0058bed4)[entity_idx * 0x76];
    float y_offset = DAT_00354c98 * cos_val1;
    
    float cos_val2 = (float)FUN_00305218(DAT_00354c94);
    float z_pos = (float)(&DAT_0058bed8)[entity_idx * 0x76] + DAT_00354c98 * cos_val2;
    
    // Apply position and camera setup
    FUN_00217d70(x_pos, y_pos - y_offset, z_pos,
                 (&DAT_0058bed0)[entity_idx * 0x76],
                 (&DAT_0058bed4)[entity_idx * 0x76],
                 (float)(&DAT_0058befc)[entity_idx * 0x76] + (float)(&DAT_0058bf08)[entity_idx * 0x76]);
    
    // Calculate and store normalized angle
    uint32_t norm_angle = FUN_00216690(DAT_00355644 + DAT_00352ec0);
    (&DAT_0058bf0c)[entity_idx * 0x76] = norm_angle;
    
    // Reset state flag
    DAT_00342b7d = 0;
    
    // Check if logo has been shown before (flag 0x511)
    long logo_shown = FUN_00266368(0x511);
    
    if (logo_shown != 0)
    {
      // Logo already shown, skip initialization
      FUN_002663a0(3); // Set flag 3?
      return;
    }
    
    // First time showing logo - initialize type 0x48 entity
    
    // Set flag 0x511 to prevent re-showing
    FUN_002663a0(0x511);
    
    // Initialize logo entity at 0x58c7e8 with type 0x48
    FUN_00229c40(0x58c7e8, 0x48);
    
    // Set logo flags
    DAT_0058c7ec = DAT_0058c7ec | 0x100; // +0x04: bit 0x100
    DAT_0058c7f0 = DAT_0058c7f0 | 0x40;  // +0x08: bit 0x40
    
    // Set config value
    DAT_0058c810 = DAT_00352ec4;
    
    // Check and set additional flag 0x0e
    long flag_0e = FUN_00266368(0x0e);
    if (flag_0e == 0)
    {
      FUN_002663a0(0x0e);
      DAT_0058c7ee = DAT_0058c7ee | 0x80; // +0x06: bit 0x80
    }
    
    // Additional state setup
    if (DAT_00354ba8 != '\0')
    {
      DAT_00342c8e = 1;
    }
  }
  else if (mode == 4)
  {
    // Mode 4: Update/tick
    uint32_t current_state = (uint32_t)DAT_00342b7d;
    uint32_t new_state = (*(PTR_FUN_003256f8[DAT_00342b7d]))(current_state);
    
    if (current_state != new_state)
    {
      DAT_00342b7d = (uint8_t)new_state;
    }
  }
  else if (mode == 7)
  {
    // Mode 7: Save state
    DAT_003555dc = DAT_00355d50;
  }
}

// Original signature wrapper
void FUN_00271220(int param_1)
{
  battle_logo_state_manager(param_1);
}
