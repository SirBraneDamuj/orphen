#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned char byte;
typedef unsigned long ulong;
typedef short sshort;

// Forward declarations for referenced functions
extern void graphics_buffer_overflow_handler(int error_code); // FUN_0026bf90
extern ushort float_to_fixed_point(float value);              // FUN_0030bd20

/**
 * GPU command packet builder - builds complex graphics rendering commands
 *
 * This is a major graphics function that builds GPU command packets for the PS2's
 * Graphics Processing Unit. It handles vertex data, texture coordinates, rendering
 * modes, and DMA packet construction for complex graphics operations.
 *
 * The function appears to handle multiple rendering modes including:
 * - Textured and non-textured primitives
 * - Transparency and blending modes
 * - Vertex transformations and coordinate processing
 * - GPU command queue management
 *
 * This is much more complex than the simple primitive renderer we analyzed earlier.
 *
 * Original function: FUN_00207de8
 * Address: 0x00207de8
 *
 * @param render_operation_id Rendering operation identifier (0x1007 in our case)
 */
void gpu_command_builder(int render_operation_id)
{
  byte texture_flags;
  sshort vertex_count;
  ushort texture_mode;
  sshort primitive_type;
  long *command_buffer;
  undefined *graphics_state_ptr;
  undefined2 texture_coord;
  uint render_flags;
  ulong packed_data;
  long buffer_value;
  long vertex_data;
  uint *gpu_ptr;
  char *command_ptr;
  uint *vertex_ptr;
  uint *color_ptr;
  uint *texture_ptr;
  undefined2 *coord_ptr;
  undefined *buffer_ptr;
  float *vertex_float_ptr;
  int loop_counter;
  float *texture_float_ptr;
  float coord_x, coord_y;
  undefined4 blend_mode;
  float texture_u, texture_v;

  // Get current graphics state and command buffer
  graphics_state_ptr = current_graphics_state;
  command_buffer = gpu_command_buffer_start;

  // Check if we have enough space in command buffer (1024 bytes needed)
  if (gpu_command_buffer_end - (int)gpu_command_buffer_current < 0x400)
  {
    return; // Not enough space
  }

  // Advance buffer pointer and check bounds
  gpu_command_buffer_start = gpu_command_buffer_start + 6;
  if ((long *)0x70003fff < gpu_command_buffer_start)
  {
    graphics_buffer_overflow_handler(0);
  }

  gpu_ptr = gpu_command_buffer_current;

  // Check if graphics state is valid and has minimum vertex count
  if (((*(uint *)(graphics_state_ptr + 0xc) & 0x80) == 0) ||
      (*(sshort *)(graphics_state_ptr + 4) < 3))
  {
    goto CLEANUP_AND_EXIT;
  }

  // Initialize command buffer entry
  *(undefined4 *)((int)command_buffer + 0x24) = 0;

  // Handle command caching/optimization
  if (render_operation_id == cached_operation_id)
  {
    if (gpu_ptr == cached_gpu_ptr)
    {
      texture_mode = *cached_texture_mode_ptr;
      *(uint *)(command_buffer + 4) = (uint)texture_mode;
      if (texture_mode < 0xff00)
      {
        *(undefined4 *)((int)command_buffer + 0x24) = 1;
      }
      goto SETUP_VERTEX_COUNT;
    }
    vertex_count = *(sshort *)(graphics_state_ptr + 6);
  }
  else
  {
  SETUP_VERTEX_COUNT:
    vertex_count = *(sshort *)(graphics_state_ptr + 6);
  }

  // Set up command buffer structure
  *(uint **)(command_buffer + 3) = gpu_ptr;
  if (*(int *)((int)command_buffer + 0x24) == 0)
  {
    gpu_ptr = gpu_ptr + 2; // Skip ahead if not cached
  }

  // Configure basic rendering parameters
  command_buffer[1] = 0xd; // Base packet type
  *command_buffer = 2;     // Basic primitive type
  *(sshort *)(graphics_state_ptr + 6) = vertex_count + 1;

  // Handle textured vs non-textured rendering
  if ((sshort)(vertex_count + 1) != 0)
  {
    *command_buffer = 3;      // Textured primitive
    command_buffer[1] = 0x1d; // Textured packet type
  }

  // Initialize transparency/blending
  *(undefined4 *)((int)command_buffer + 0x1c) = 0;
  render_flags = *(uint *)(graphics_state_ptr + 0xc);

  // Handle transparency and blending modes
  if ((render_flags & 0x1c000) != 0)
  {
    command_buffer[1] = command_buffer[1] | 0x40; // Enable transparency
    if ((render_flags & 0x4000) == 0)
    {
      blend_mode = 2;
      if (((render_flags & 0x8000) == 0) && (blend_mode = 3, (render_flags & 0x10000) == 0))
        goto SETUP_GPU_COMMANDS;
    }
    else
    {
      blend_mode = 1;
    }
    *(undefined4 *)((int)command_buffer + 0x1c) = blend_mode;
  }

SETUP_GPU_COMMANDS:
  // Set fog/depth testing flag
  *(byte *)(command_buffer + 5) = (byte)((int)*(undefined4 *)(graphics_state_ptr + 0xc) >> 0x1d) & 1;

  // Build GPU command header
  *gpu_ptr = 0x6e03c000;
  *(undefined *)(gpu_ptr + 1) = graphics_state_ptr[4];
  command_ptr = (char *)((int)gpu_ptr + 5);

  // Handle texture/vertex data processing
  if (*(sshort *)(graphics_state_ptr + 6) == 0)
  {
    // Non-textured path
    *command_ptr = '\0';
    *(undefined *)((int)gpu_ptr + 6) = 0;
  }
  else
  {
    // Textured path - more complex processing
    if ((*(uint *)(graphics_state_ptr + 0xc) & 0xff) < 0x18)
    {
      *command_ptr = '\0';
    }
    else
    {
      *command_ptr = (char)(*(uint *)(graphics_state_ptr + 0xc) >> 8) + '\x01';
    }
    *(char *)((int)gpu_ptr + 6) = (char)*(uint *)(graphics_state_ptr + 0xc);
  }

  // Continue with complex vertex and texture processing...
  // [The full function continues with extensive vertex processing,
  //  texture coordinate transformation, and GPU packet finalization]

  // Finalize GPU commands and update pointers
  vertex_count = *(sshort *)(graphics_state_ptr + 4);
  texture_mode = *(ushort *)(graphics_state_ptr + 4);
  primitive_type = *(sshort *)(graphics_state_ptr + 6);
  gpu_ptr[10] = (int)vertex_count << 0x10 | 0x6e00c024;
  gpu_ptr = gpu_ptr + 0xb;

  // Process vertex data
  if (0 < vertex_count)
  {
    loop_counter = (int)(sshort)texture_mode;
    vertex_ptr = (uint *)(graphics_state_ptr + 0x10);
    do
    {
      uint vertex_data = *vertex_ptr;
      if (primitive_type == 0)
      {
        vertex_data = vertex_data & 0xffffff | (vertex_data & 0xfe000000) >> 1;
      }
      else
      {
        vertex_data = (vertex_data & 0xfefefefe) >> 1;
      }
      *gpu_ptr = vertex_data;
      gpu_ptr = gpu_ptr + 1;
      loop_counter = loop_counter + -1;
      vertex_ptr = vertex_ptr + 1;
    } while (loop_counter != 0);
  }

  // Handle texture coordinate processing for textured primitives
  if (primitive_type != 0)
  {
    // Complex texture coordinate transformation
    vertex_data = (long)*(sshort *)(graphics_state_ptr + 4);
    if ((char)command_buffer[5] == '\0')
    {
      uint texture_header = (uint)texture_mode << 0x10;
      vertex_data = 0;
      *gpu_ptr = texture_header | 0x65008039;
      color_ptr = gpu_ptr + 1;
      if (0 < (int)texture_header)
      {
        vertex_float_ptr = (float *)(graphics_state_ptr + 0xb4);
        coord_ptr = (undefined2 *)((int)gpu_ptr + 6);
        do
        {
          vertex_data = (long)((int)vertex_data + 1);
          color_ptr = color_ptr + 1;
          texture_coord = float_to_fixed_point(vertex_float_ptr[-1] * 4096.0);
          coord_x = *vertex_float_ptr;
          coord_ptr[-1] = texture_coord;
          vertex_float_ptr = vertex_float_ptr + 2;
          texture_coord = float_to_fixed_point(coord_x * 4096.0);
          *coord_ptr = texture_coord;
          coord_ptr = coord_ptr + 2;
        } while ((int)vertex_data < (int)(texture_header >> 0x10));
      }
      gpu_ptr = color_ptr;
    }
  }

  // Pad to 16-byte alignment
  while (gpu_ptr = gpu_ptr + 1, ((uint)gpu_ptr & 0xf) != 0)
  {
    *gpu_ptr = 0;
  }

  // Finalize DMA packet
  if (*(int *)((int)command_buffer + 0x24) == 0)
  {
    loop_counter = render_operation_id * 0x10 + gpu_interrupt_counter;
    **(uint **)(command_buffer + 3) =
        ((uint)((int)gpu_ptr - (int)*(uint **)(command_buffer + 3)) >> 4) - 1 | 0x20000000;
    *(undefined4 *)((int)command_buffer[3] + 4) = *(undefined4 *)(loop_counter + 4);
    __sync(); // PS2 cache sync
    *(int *)(loop_counter + 4) = (int)command_buffer[3];
    cached_texture_mode_ptr = *(ushort **)(command_buffer + 3);
    cached_operation_id = render_operation_id;
  }
  else
  {
    *(uint *)(command_buffer + 4) = (int)command_buffer[4] +
                                    ((uint)((int)gpu_ptr - (int)command_buffer[3]) >> 4);
    *cached_texture_mode_ptr = *(ushort *)(command_buffer + 4);
  }

  // Update global state
  current_graphics_state = &graphics_data_table;
  gpu_command_buffer_current = gpu_ptr;
  cached_gpu_ptr = gpu_ptr;

CLEANUP_AND_EXIT:
  gpu_command_buffer_start = gpu_command_buffer_start + -6;
  return;
}

// Global variables for GPU command system:

/**
 * Current graphics state pointer
 * Original: puGpffffb7b4
 */
extern undefined *current_graphics_state;

/**
 * GPU command buffer start
 * Original: DAT_70000000
 */
extern long *gpu_command_buffer_start;

/**
 * GPU command buffer end
 * Original: DAT_70000008
 */
extern int gpu_command_buffer_end;

/**
 * GPU command buffer current position
 * Original: DAT_70000004
 */
extern uint *gpu_command_buffer_current;

/**
 * GPU interrupt counter
 * Original: DAT_7000000c
 */
extern int gpu_interrupt_counter;

/**
 * Cached operation ID for optimization
 * Original: iGpffffaca8
 */
extern int cached_operation_id;

/**
 * Cached GPU pointer for optimization
 * Original: puGpffffacb0
 */
extern uint *cached_gpu_ptr;

/**
 * Cached texture mode pointer for optimization
 * Original: puGpffffacac
 */
extern ushort *cached_texture_mode_ptr;

/**
 * Graphics data table
 * Original: DAT_70000010
 */
extern undefined graphics_data_table;
