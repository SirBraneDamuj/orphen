/*
 * Player Character Entity Update Function - Field/Exploration Mode
 * Original: FUN_00251ed8
 *
 * This is the field/exploration mode update function for the player character entity.
 * It's called with entity 0 (at 0x58beb0) when the game is in field/exploration mode,
 * while FUN_00249610 handles the same entity during battle mode.
 *
 * The game has two distinct player update systems:
 * - Field Mode (this function): Handles movement, exploration, basic interactions
 * - Battle Mode (FUN_00249610): Handles combat, abilities, turn-based actions
 *
 * Mode Selection Logic:
 * - if (cGpffffb663 == 0 || sGpffffb052 == 0): Use this function (field mode)
 * - else: Use FUN_00249610 (battle mode)
 *
 * Called from:
 * - FUN_002239c8 (main game loop) when in field mode
 * - FUN_002245d8 (alternate game loop)
 */ \
#include "orphen_globals.h"

// Forward declarations for unanalyzed functions
extern void FUN_00225bf0(undefined8 entity, uint state, uint substate);                          // Set entity state
extern void calculate_sound_envelope_fade(int sound_id, long distance, undefined8 target_level); // Sound envelope fade calculation
extern void FUN_00267da0(byte *buffer, void *source, int size);                                  // Memory copy/buffer operation
extern void FUN_00257b00(void);                                                                  // Unknown subsystem call
extern void FUN_00265ec0(uint param);                                                            // Audio/sound related function
extern void FUN_00205938(int param1, int param2, int param3);                                    // System trigger
extern void FUN_002536a8(undefined8 entity);                                                     // Entity state handler
extern void FUN_00257c10(undefined8 entity);                                                     // Entity finalization
extern void FUN_00257c40(undefined8 entity);                                                     // Entity state handler alt
extern byte FUN_00266008(void);                                                                  // Get next available slot/index
extern void FUN_00252658(undefined8 entity);                                                     // Entity subsystem update
extern void FUN_00253080(undefined8 entity);                                                     // Entity processing
extern long FUN_002298d0(short value);                                                           // Value lookup/validation
extern int FUN_00305130(undefined4 param);                                                       // Math/trigonometric function
extern int FUN_00305218(undefined4 param);                                                       // Math/trigonometric function

// Global variables (original names preserved until analyzed)
extern uint uGpffffbd54;   // State flag related to param_2 & 0x20
extern char cGpffffb66a;   // Master enable/disable flag
extern char cGpffffb0ac;   // State control flag
extern char cGpffffb6e4;   // Special mode flag
extern float fGpffffb678;  // Timer/counter value
extern ushort uGpffffb686; // State flags
extern ushort uGpffffb68a; // Additional state data
extern ushort uGpffffb6a8; // State parameter
extern byte bGpffffb6e1;   // Counter/index value
extern uint uGpffffb6e0;   // State reset flag
extern uint uGpffffbd50;   // State preservation
extern uint uGpffffb098;   // Parameter storage
extern float fGpffffb0a0;  // Comparison/threshold value
extern float fGpffffb674;  // Target/reference value
extern float fGpffff88b8;  // Threshold constant
extern uint uGpffffb0a4;   // Associated state data
extern uint uGpffffb6d4;   // Reference data
extern uint uGpffffb09c;   // Parameter storage
extern ushort uGpffffbd5e; // Timer countdown
extern ushort uGpffffb64c; // Frame/tick counter
extern char bGpffffbd58;   // Array index/slot
extern ushort uGpffffbd5a; // Timer for array operation
extern char cGpffffb6d0;   // Death/destruction state flag
extern uint uGpffffb0b4;   // System state flag

// Constants (original addresses preserved)
extern float fGpffff88bc;                     // Threshold constant
extern float fGpffffb78c;                     // Limit constant
extern float fGpffff88c4;                     // Offset constant
extern uint uGpffff88c0;                      // Default value
extern uint uGpffff88c8;                      // Default value
extern float fGpffff88cc;                     // Offset constant
extern uint uGpffff88d0;                      // State value
extern uint uGpffff88d4;                      // State value
extern float fGpffff88d8;                     // Time multiplier
extern uint DAT_0058beb8;                     // System flags
extern void *DAT_00343888;                    // Data array base
extern void *DAT_00343898;                    // Float array base
extern void *DAT_00343894;                    // Data array offset
extern void *DAT_00343895;                    // Data array offset
extern void *DAT_00343896;                    // Data array offset
extern void (**PTR_FUN_0031e0e8)(undefined8); // Function pointer table for states < 0x1c
extern void (**PTR_FUN_0031e160)(undefined8); // Function pointer table for states >= 0x1c

void update_player_field_mode(undefined8 player_entity, uint input_flags, undefined4 param_3)
{
  char countdown_timer;
  short *data_pointer;
  byte slot_index;
  short temp_value;
  ushort flags;
  int result;
  long validation_result;
  short *entity;
  float movement_value;
  undefined4 rotation_param;
  float target_position;

  entity = (short *)player_entity;

  // Early exit if entity is invalid/inactive
  if (*entity == 0)
  {
    return;
  }

  // Process input flags - extract bit 5 (0x20)
  uGpffffbd54 = input_flags & 0x20;
  if (cGpffffb66a == '\0')
  {
    uGpffffbd54 = 0; // Master disable overrides input
  }

  // Handle state transition flags
  if (cGpffffb0ac != '\0')
  {
    if ((input_flags & 0xf0) == 0)
    {
      cGpffffb0ac = '\0'; // Clear flag if no upper nibble set
    }
    else
    {
      input_flags = input_flags & 0xffffff0f; // Clear upper nibble, preserve flag
    }
  }

  // Special mode handling - clears most state when active
  if (cGpffffb6e4 != '\0')
  {
    input_flags = 0;
    fGpffffb678 = 0.0;
    param_3 = 0;
    uGpffffb686 = uGpffffb686 & 0x400; // Preserve only bit 10
    uGpffffb68a = 0;
    uGpffffb6a8 = 0;
  }

  // Handle counter-based state clearing
  if (bGpffffb6e1 - 1 < 9)
  { // Counter in range 1-9
    input_flags = 0;
    param_3 = 0;
  }

  // Preserve and reset state
  uGpffffbd50 = uGpffffb098; // Save previous state
  uGpffffb6e0 = 0;

  // Handle position/target tracking
  if (fGpffffb678 == 0.0)
  {
    fGpffffb0a0 = -1.0; // Reset to invalid position
  }
  else
  {
    // Calculate distance from target
    movement_value = fGpffffb674 - fGpffffb0a0;
    if (movement_value < 0.0)
    {
      movement_value = -movement_value; // Absolute value
    }
    // Update position if movement exceeds threshold
    if (fGpffff88b8 < movement_value)
    {
      fGpffffb0a0 = fGpffffb674;
      uGpffffb0a4 = uGpffffb6d4;
    }
  }

  // Store current parameters
  uGpffffb098 = input_flags;
  uGpffffb09c = param_3;

  // Death/destruction condition check
  // Complex condition checking entity flags, state, and position thresholds
  if (((((*(uint *)(entity + 0x36) & 0x1000000) != 0) && (entity[0x30] != 0x19)) &&
       (((*(uint *)(entity + 6) & 1) != 0 || (*(float *)(entity + 0x22) < fGpffff88bc)))) &&
      (((*(uint *)(entity + 0x36) & 0xf0000000) != 0x10000000 ||
        (*(float *)(entity + 0x14) <= fGpffffb78c))))
  {

    // Trigger death state transition
    FUN_00225bf0(player_entity, 0x19, 0xd);  // Set state to death (0x19)
    *(undefined1 *)(entity + 0x9a) = 0x7c;   // Set death marker
    entity[2] = entity[2] | 0x110;           // Set death flags
    entity[4] = entity[4] & 0xfffb;          // Clear flag bit 2
    entity[0x31] = 0;                        // Clear timer
    entity[0xda] = (short)(char)bGpffffb6e1; // Store counter value
    bGpffffb6e1 = 0xff;                      // Reset counter
  }

  // Handle countdown timer
  if ((0 < (short)uGpffffbd5e) &&
      (result = (uint)uGpffffbd5e - (uGpffffb64c & 0xffff), uGpffffbd5e = (ushort)result,
       result * 0x10000 < 1))
  {
    uGpffffbd5e = 0;
    calculate_sound_envelope_fade(7, 0x19, 0); // Trigger system event
  }

  // Handle entity-specific countdown
  countdown_timer = *(char *)((int)entity + 0xbd);
  if (*(char *)((int)entity + 0xbd) != '\0')
  {
    *(char *)((int)entity + 0xbd) = countdown_timer + -1; // Decrement timer
    entity[0x52] = entity[0x52] + (short)uGpffffb64c;     // Update position counter
    if (countdown_timer != '\x01')
    {
      return; // Exit early if timer not expired
    }
  }

  // Handle data array operations
  if (-1 < (char)bGpffffbd58)
  { // Valid slot index
    // Copy 12 bytes from data array to entity offset 0x10
    FUN_00267da0(&DAT_00343888 + (char)bGpffffbd58 * 5, entity + 0x10, 0xc);
    slot_index = bGpffffbd58;
    uGpffffbd5a = uGpffffbd5a - 1; // Decrement timer
    if ((int)((uint)uGpffffbd5a << 0x10) < 1)
    {
      bGpffffbd58 = 0xff;                        // Mark slot as free
      (&DAT_00343898)[(char)slot_index * 5] = 0; // Clear float array entry
    }
  }

  // Health/damage processing
  if (entity[0x5f] != 0)
  { // Health change pending
    if (cGpffffb6e4 == '\0')
    {
      entity[4] = entity[4] & 0xfffe; // Clear active flag
    }
    else
    {
      cGpffffb6e4 = '\x02'; // Set special mode
    }

    // Special handling for state 9
    if (entity[0x30] == 9)
    {
      result = *(int *)(entity + 0xcc);
      *(undefined1 *)(result + 0x1b8) = 0;                        // Clear linked entity flag
      *(ushort *)(result + 4) = *(ushort *)(result + 4) & 0xfff6; // Clear flags
      entity[2] = entity[2] & 0xfff6;                             // Clear entity flags
      flags = entity[0x61];
    }
    else
    {
      flags = entity[0x61];
    }

    // Check for invulnerability flag
    if ((flags & 0x2000) == 0)
    {
      FUN_00257b00(); // Process damage/health change
      flags = entity[0x95];
      entity[0x95] = (short)((uint)flags - (uint)(ushort)entity[0x5f]); // Apply damage

      if (0 < (int)(((uint)flags - (uint)(ushort)entity[0x5f]) * 0x10000))
      {
        // Still alive, handle state-specific logic
        temp_value = entity[0x30];
        goto LAB_002522b0;
      }

      // Death sequence
      entity[2] = entity[2] | 0x11;                               // Set death flags
      *(uint *)(entity + 6) = *(uint *)(entity + 6) & 0xfffffffe; // Clear active flag
      entity[0x31] = 0x3c0;                                       // Set death timer
      entity[0x95] = 0;                                           // Clear health
      *(undefined1 *)(entity + 0x9a) = 0;                         // Clear status
      cGpffffb6d0 = '\x01';                                       // Set global death flag

      // Trigger death effects and cleanup
      FUN_00265ec0(0x58cd70);                    // Play death sound
      FUN_00205938(7, 0x2f, 0);                  // System trigger
      calculate_sound_envelope_fade(0, 0x19, 0); // System event
      calculate_sound_envelope_fade(1, 0x19, 0); // System event

      if (entity[0x30] == 0x19)
      { // If already in death state
        entity[0xd8] = 0;
        entity[0xd9] = 0;
      }
      else
      {
        *(undefined4 *)(entity + 0xd8) = uGpffff88c0; // Set death state data
      }

      *(undefined4 *)(entity + 0x22) = uGpffff88c8;                        // Set death parameters
      *(float *)(entity + 0x2e) = *(float *)(entity + 0x62) + fGpffff88c4; // Position adjustment

      FUN_00225bf0(player_entity, 0x18, 0x20); // Set death state
      FUN_00257c10(player_entity);             // Finalize death
    }
    else
    {
      // Invulnerable, handle state-specific logic
      temp_value = entity[0x30];

    LAB_002522b0:
      if (temp_value < 3)
      {
        // Low states: use base position
        movement_value = *(float *)(entity + 0x62);
      }
      else
      {
        if (temp_value < 7)
        {
          // Mid states: special handling
          FUN_002536a8(player_entity);
          FUN_00225bf0(player_entity, 0x18, 0x20);
          entity[0x1e] = 0;
          entity[0x1f] = 0;
          entity[0x31] = entity[0x60]; // Copy timer
          entity[0x20] = 0;
          entity[0x21] = 0;
          FUN_00257c10(player_entity);
          goto LAB_00252438;
        }

        if (temp_value == 0x1c)
        {
          // Special state 0x1c handling
          if (*(short **)(entity + 0xcc) != (short *)0x0)
          {
            if (**(short **)(entity + 0xcc) != 0x42)
            {
              movement_value = *(float *)(entity + 0x62);
              goto LAB_00252334;
            }
            FUN_00265ec0(0); // Audio call with default parameter
          }
          movement_value = *(float *)(entity + 0x62);
        }
        else
        {
          movement_value = *(float *)(entity + 0x62);
        }
      }

    LAB_00252334:
      *(float *)(entity + 0x2e) = movement_value + fGpffff88cc; // Apply position offset

      // Handle specific attack/action states
      if ((char)entity[0x5e] == '\x13')
      {
        // Attack state 0x13
        FUN_00225bf0(player_entity, 0x17, 0x22);
        *(undefined4 *)(entity + 0xd2) = *(undefined4 *)(entity + 0xa8); // Backup data
        *(undefined4 *)(entity + 0xd4) = *(undefined4 *)(entity + 0x2c);
        entity[0xa8] = 0; // Clear working data
        entity[0xa9] = 0;
        entity[0x2c] = 0;
        entity[0x2d] = 0;
        if (entity[0x60] == 0)
        {
          entity[0x60] = 0x1e; // Set default timer
        }
        FUN_00257c40(player_entity);
      }
      else if (((char)entity[0x5e] == '\x12') || (((*(uint *)(entity + 6) ^ 1) & 1) != 0))
      {
        // Attack state 0x12 or flag condition
        FUN_00225bf0(player_entity, 0x18, 0x20);
        *(uint *)(entity + 6) = *(uint *)(entity + 6) & 0xfffffffe; // Clear flag
        entity[0x31] = entity[0x60];                                // Set timer
        if (entity[0x60] == 0)
        {
          entity[0x31] = 0x100; // Default timer
        }
        if ((char)entity[0x5e] == '\x12')
        {
          *(undefined4 *)(entity + 0xd8) = uGpffff88d0;
        }
        else
        {
          entity[0xd8] = 0;
          entity[0xd9] = 0;
        }
        *(undefined4 *)(entity + 0x22) = uGpffff88d4;
        FUN_00257c10(player_entity);
      }
      else
      {
        // Default damage state
        FUN_00225bf0(player_entity, 0x16, 0x1f);
        FUN_00257c40(player_entity);
      }

      entity[0xdc] = 0x1680; // Set recovery timer
    }

  LAB_00252438:
    // Handle data array slot allocation
    if ((-1 < (char)bGpffffbd58) ||
        (bGpffffbd58 = FUN_00266008(), -1 < (int)((uint)bGpffffbd58 << 0x18)))
    {
      result = (char)bGpffffbd58 * 0x14;                   // 20-byte stride
      uGpffffbd5a = 0xf;                                   // Set timer
      (&DAT_00343898)[(char)bGpffffbd58 * 5] = 0x3f800000; // Set float to 1.0
      (&DAT_00343894)[result] = 0xff;                      // Set data values
      (&DAT_00343895)[result] = 0;
      (&DAT_00343896)[result] = 0;
    }

    // Clear damage/health change flags
    entity[0x61] = 0;
    entity[0x5f] = 0;
    entity[0x60] = (short)((int)entity[0x60] << 5); // Scale timer
  }

  // Update general timer
  if ((entity[0x60] != 0) &&
      (result = (uint)(ushort)entity[0x60] - (uGpffffb64c & 0xffff), entity[0x60] = (short)result,
       result * 0x10000 < 0))
  {
    entity[0x60] = 0; // Timer expired
  }

  // Core entity subsystem update
  FUN_00252658(player_entity);

  // State-based function dispatch
  if ((entity[2] & 0x4000U) == 0)
  {                            // Not in special state
    temp_value = entity[0x30]; // Current state
    if (temp_value < 0x1c)
    {
      // States 0-27: use primary function table
      PTR_FUN_0031e0e8[temp_value](player_entity);
      if (entity[0x30] == 0)
        goto LAB_00252564; // Early exit for state 0
    }
    else
    {
      // States 28+: use secondary function table
      PTR_FUN_0031e160[temp_value + -0x1c](player_entity);
      if (cGpffffb6e4 != '\0')
      {
        DAT_0058beb8 = DAT_0058beb8 & 0xfffe; // Clear system flag
      }
      cGpffffb6e4 = '\0'; // Clear special mode
    }
    uGpffffb0b4 = 0; // Clear system state
  }

LAB_00252564:
  // Additional processing when not in death state
  if (cGpffffb6d0 == '\0')
  {
    FUN_00253080(player_entity); // Standard entity processing
    temp_value = entity[0xdc];
  }
  else
  {
    temp_value = entity[0xdc]; // Death state - skip processing
  }

  // Handle recovery/invulnerability timer
  if (temp_value != 0)
  {
    result = (uint)(ushort)entity[0xdc] - (uGpffffb64c & 0xffff);
    entity[0xdc] = (short)result;
    if (result * 0x10000 < 1)
    {
      entity[0xdc] = 0;           // Timer expired
      flags = entity[2] & 0xffef; // Clear invulnerability flag
    }
    else
    {
      flags = entity[2] | 0x10; // Set invulnerability flag
    }
    entity[2] = flags;
  }

  // Handle linked entity movement/physics
  data_pointer = *(short **)(entity + 0x34);
  if ((data_pointer != (short *)0x0) && ((*(uint *)(entity + 6) & 1) != 0))
  {
    temp_value = *data_pointer;
    if (temp_value == 0x38)
    {
      temp_value = data_pointer[0xe7]; // Use alternate value
    }
    validation_result = FUN_002298d0(temp_value); // Validate/lookup value
    if (validation_result < 7)
    { // Valid range
      rotation_param = *(undefined4 *)(*(int *)(entity + 0x34) + 0x5c);
      target_position = (float)(int)uGpffffb64c * fGpffff88d8; // Time-based movement

      // Apply trigonometric movement (sin/cos based)
      movement_value = (float)FUN_00305130(rotation_param); // Sine function
      *(float *)(entity + 0x18) = *(float *)(entity + 0x18) + target_position * movement_value;

      movement_value = (float)FUN_00305218(rotation_param); // Cosine function
      *(float *)(entity + 0x1a) = *(float *)(entity + 0x1a) + target_position * movement_value;
    }
  }

  return;
}
