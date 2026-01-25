// Entity Animation Frame Update (analyzed)
// Original: FUN_00225c90
// Address: 0x00225c90
//
// Summary:
// - Core animation system update function called every frame for each active entity.
// - Manages animation frame progression, timer countdown, and state transitions.
// - Reads animation data from entity's animation descriptor and updates playback state.
// - This is THE function that iterates entities through their animations frame-by-frame.
//
// Inputs:
// - entity_ptr (short*): Pointer to entity structure (0xEC stride from DAT_0058beb0)
//
// Entity Structure Offsets (animation-related, as short array indices):
// - [0x50] (0xA0): animation_param - current animation parameter/substate ID
// - [0x51] (0xA2): animation_next_state - queued animation state for transitions
// - [0x52] (0xA4): animation_timer - countdown timer, decremented by uGpffffb64c per frame
// - [0x53] (0xA6): animation_frame_duration - duration of current frame from data
// - [0x54] (0xA8): animation_frame_index - current frame index, incremented by 2
// - [0x55] (0xAA): animation_data_field_1 - extracted from animation data
// - [0x56] (0xAC): animation_data_field_2 - extracted from animation data
// - [0x57] (0xAE): animation_data_field_3 - previous frame data
// - [0x9E] (0x13C): interpolation_ratio - computed as (delta / duration) for smooth blending
// - [0x4E] (0x9C): animation_descriptor_ptr - pointer to animation data structure
// - [0xAE] (0x15C): animation_base_ptr - base pointer for animation frame data
// - [1] (0x02): entity_flags - bit 0x200 disables animation updates
// - [3] (0x06): animation_control_flags - various control bits
// - [4] (0x08): entity_state_flags - bit 0x10 affects interpolation calculation
//
// Key Operations Per Frame:
// 1. Check entity flags - if bit 0x200 set, reset interpolation and exit
// 2. Validate animation descriptor exists at [0x4E]
// 3. Load animation frame data from descriptor using current frame index [0x50]
// 4. Decrement animation timer [0x52] by global frame delta (uGpffffb64c)
// 5. If timer expired:
//    - Advance frame index [0x54] by 2
//    - Load next frame data (3 shorts: data_field_1, duration, data_field_2)
//    - Call FUN_00225c60 to process frame duration
//    - Update control flags based on frame data (bit 0x8000 triggers flag 0x02)
//    - Reset timer to next frame's duration
// 6. Calculate interpolation ratio: delta / remaining_timer
// 7. Store interpolation ratio at [0x9E] for smooth animation blending
//
// Frame Data Structure (per keyframe, 3 shorts = 6 bytes):
// - Short 0: data_field_1 (stored in entity[0x56], prev in entity[0x57])
// - Short 1: frame_duration (bit 0x8000 = special flag, lower 15 bits = duration)
// - Short 2: data_field_2 (stored in entity[0x55])
//
// Side Effects:
// - Calls FUN_00225c60(duration) to process/validate frame duration
// - Updates animation control flags in entity[3] (bits 0x02, 0x04, 0x05, 0x08)
// - Modifies interpolation ratio for rendering system
// - May trigger animation loops or state transitions
//
// Global Dependencies:
// - uGpffffb64c: Global frame delta/tick counter (typically 32 per frame at 30fps)
//
// Notes:
// - Animation timer counts DOWN, not up (starts at 999 from FUN_00225bf0)
// - Frame index advances by 2 (likely because data is stored as shorts, not bytes)
// - Interpolation ratio enables smooth transitions between keyframes
// - Bit 0x8000 in duration triggers flag 0x02 (animation loop or event marker?)
// - When timer expires and frame advances, system supports both looping and one-shot animations
// - The function handles animation state transitions when [0x50] != [0x51]

#include <stdint.h>

// Global frame delta/tick counter
extern uint32_t uGpffffb64c;

// Frame duration processor (validates/converts duration value)
extern long FUN_00225c60(uint16_t duration);

// Error logger for invalid animation state
extern void FUN_002681c0(uint32_t msg_ptr, long entity_id, bool overflow_flag, int16_t frame_index);

// Original signature: void FUN_00225c90(short *param_1)
void update_entity_animation_frame(int16_t *entity_ptr)
{
  // Early exit if animation updates disabled (bit 0x200 in flags)
  if ((entity_ptr[1] & 0x200) != 0)
  {
    // Reset interpolation ratio to identity (0.0, 1.0 as IEEE-754 floats)
    entity_ptr[0x9E] = 0;
    entity_ptr[0x9F] = 0x3F80; // 1.0f
    return;
  }

  // Reset interpolation ratio
  entity_ptr[0x9E] = 0;
  entity_ptr[0x9F] = 0x3F80;

  // Check if animation descriptor exists
  if (*(int32_t *)(entity_ptr + 0x4E) == 0)
  {
    return;
  }

  uint8_t control_flags = *(uint8_t *)(entity_ptr + 3);
  uint32_t flags_modified = control_flags & 0xF7; // Clear bit 0x08

  // Skip update if bit 0x10 set
  if ((control_flags & 0x10) != 0)
  {
    return;
  }

  int16_t current_frame = entity_ptr[0x50]; // Animation param/frame index

  // Validate frame index against animation descriptor's frame count
  if (*(int16_t *)(*(int32_t *)(entity_ptr + 0xAE) + 6) <= current_frame)
  {
    // Frame index out of bounds - log error
    long entity_id = (long)*entity_ptr;
    bool overflow = entity_id > 0x271;
    if (overflow)
    {
      entity_id = (long)((*entity_ptr - 0x272) * 0x10000 >> 0x10);
    }
    FUN_002681c0(0x34BF08, entity_id, overflow, entity_ptr[0x50]);
    return;
  }

  // Load animation frame data pointer
  int32_t data_offset = *(int32_t *)(current_frame * 8 + *(int32_t *)(entity_ptr + 0x4E));
  int16_t *frame_data = (int16_t *)(*(int32_t *)(entity_ptr + 0xAE) + data_offset);

  if (data_offset == 0)
  {
    frame_data = NULL;
  }

  if (frame_data == NULL)
  {
    // Invalid frame data - log error and exit
    long entity_id = (long)*entity_ptr;
    bool overflow = entity_id > 0x271;
    if (overflow)
    {
      entity_id = (long)((*entity_ptr - 0x272) * 0x10000 >> 0x10);
    }
    FUN_002681c0(0x34BF08, entity_id, overflow, entity_ptr[0x50]);
    return;
  }

  int16_t timer = entity_ptr[0x52]; // Current animation timer

  // Check if we're continuing the same frame or transitioning
  if (current_frame == entity_ptr[0x51]) // Same state
  {
    long remaining_time = (long)timer;

    // Decrement timer by frame delta unless it's the max value (0x7FFF = pause)
    if (timer != 0x7FFF)
    {
      remaining_time = (long)((int)((timer - (uGpffffb64c & 0xFFFF)) * 0x10000) >> 0x10);

      // Handle underflow based on control flags
      if ((control_flags & 4) == 0)
      {
        if (remaining_time <= 0)
        {
          remaining_time = 1; // Clamp to minimum
        }
      }
      else if (remaining_time > 0)
      {
        remaining_time = 0;
      }
    }

    // If timer still running, update interpolation and exit
    if (remaining_time > 0)
    {
      // Calculate interpolation ratio for smooth blending
      if ((int)uGpffffb64c < remaining_time)
      {
        *(float *)(entity_ptr + 0x9E) = (float)(int)uGpffffb64c / (float)(int)remaining_time;
      }
      else
      {
        entity_ptr[0x9E] = 0;
        entity_ptr[0x9F] = 0x3F80;
      }

      entity_ptr[0x52] = (int16_t)remaining_time;
      entity_ptr[3] = (int16_t)flags_modified;
      return;
    }

    // Timer expired - advance to next frame
    flags_modified = control_flags & 0xF3; // Clear bits 0x04, 0x08

    if ((control_flags & 2) == 0)
    {
      // Normal advance: increment frame index by 2
      entity_ptr[0x54] += 2;
    }
    else
    {
      // Special mode: reset or loop
      flags_modified = control_flags & 0xF0;
      if ((control_flags & 0x80) == 0)
      {
        entity_ptr[0x54] = 0; // Reset to first frame
      }
    }

    // Load next frame data (offset by frame_index, 3 shorts per frame)
    int frame_offset = ((int)((uint16_t)entity_ptr[0x54] << 0x10) >> 0x10) -
                           ((int)((uint16_t)entity_ptr[0x54] << 0x10) >> 0x1F) >>
                       1;
    frame_data = frame_data + frame_offset * 3;

    int16_t data_field_2 = frame_data[2];
    int16_t data_field_1 = frame_data[0];
    uint16_t duration_raw = frame_data[1];

    // Store previous frame data
    entity_ptr[0x57] = entity_ptr[0x56];
    entity_ptr[0x56] = data_field_1;
    entity_ptr[0x55] = data_field_2;

    // Check for special flag in duration (bit 0x8000)
    if ((int16_t)duration_raw < 0)
    {
      duration_raw = duration_raw & 0x7FFF;
      flags_modified |= 0x02; // Set loop/event flag
    }

    // Process frame duration
    long frame_duration = FUN_00225c60(duration_raw);
    entity_ptr[0x53] = (int16_t)frame_duration;

    // Adjust remaining time
    if (frame_duration != 0x7FFF)
    {
      frame_duration = (long)(((int)frame_duration + (int)remaining_time) * 0x10000 >> 0x10);
    }

    remaining_time = frame_duration;
    if (frame_duration < 1)
    {
      remaining_time = 1;
    }

    timer = (int16_t)remaining_time;
  }
  else
  {
    // State transition - reset frame index and load new animation
    flags_modified = control_flags & 0xF0;
    entity_ptr[0x51] = entity_ptr[0x50]; // Update tracked state
    entity_ptr[0x54] = 0;                // Reset frame index
    entity_ptr[3] &= 0xFF3F;             // Clear bits 6-7

    uint16_t duration_raw = frame_data[1];
    int16_t data_field_2 = frame_data[2];
    int16_t data_field_1 = frame_data[0];

    // Check for special duration flag
    if ((int16_t)duration_raw < 0)
    {
      duration_raw &= 0x7FFF;
      flags_modified |= 0x02;
    }

    long frame_duration = FUN_00225c60(duration_raw);
    int16_t prev_data = entity_ptr[0x56];
    entity_ptr[0x56] = data_field_1;
    entity_ptr[0x57] = prev_data;
    entity_ptr[0x55] = data_field_2;
    entity_ptr[0x53] = (int16_t)frame_duration;

    timer = (int16_t)frame_duration;
  }

  flags_modified |= 0x08; // Set "animation active" flag

  // Calculate and store interpolation ratio
  uint16_t final_flags = (uint16_t)flags_modified;
  if ((int)uGpffffb64c < timer)
  {
    if ((entity_ptr[4] & 0x10) == 0)
    {
      *(float *)(entity_ptr + 0x9E) = (float)(int)uGpffffb64c / (float)(int)timer;
    }
    else
    {
      entity_ptr[0x9E] = 0;
      entity_ptr[0x9F] = 0x3F80;
    }
  }
  else
  {
    // Frame completed within this tick
    uint16_t completed_flags = final_flags | 5;
    final_flags = final_flags | 4;
    if ((flags_modified & 2) != 0)
    {
      final_flags = completed_flags;
    }

    entity_ptr[0x9E] = 0;
    entity_ptr[0x9F] = 0x3F80;
  }

  // Write back updated values
  entity_ptr[0x52] = timer;
  entity_ptr[3] = (int16_t)final_flags;
}

// Original signature wrapper
void FUN_00225c90(int16_t *param_1)
{
  update_entity_animation_frame(param_1);
}
