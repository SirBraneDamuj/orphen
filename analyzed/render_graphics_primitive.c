#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned long ulong;
typedef unsigned int uint;

/**
 * Graphics primitive rendering function - renders 2D graphics primitives to PS2 GPU
 *
 * This function appears to be a low-level graphics rendering function that builds
 * GPU command packets for the PlayStation 2's Graphics Processing Unit. It handles
 * rendering of 2D primitives like sprites, characters, and UI elements.
 *
 * Original function: FUN_00207938
 * Address: 0x00207938
 *
 * @param render_flags Rendering flags and options
 * @param texture_info Texture information (negative values for special handling)
 * @param x_pos X coordinate for rendering
 * @param y_pos Y coordinate for rendering
 * @param width Width of the primitive
 * @param height Height of the primitive
 * @param texture_u Texture U coordinate
 * @param texture_v Texture V coordinate
 * @param texture_width Texture width
 * @param texture_height Texture height
 * @param color_data Color/shading information
 * @param gpu_command GPU command parameter
 */
void render_graphics_primitive(ulong render_flags, long texture_info, int x_pos, int y_pos,
                               short width, short height, int texture_u, int texture_v,
                               int texture_width, int texture_height, int color_data, undefined4 gpu_command)
{
  undefined4 *gpu_ptr;
  long *command_buffer;
  undefined2 gpu_mode;
  undefined4 temp_data;
  ulong packed_data;
  long temp_long;
  long *buffer_ptr;
  long buffer_value;
  short vertex_x1, vertex_y1, vertex_x2, vertex_y2;
  short tex_u, tex_v;
  char *command_ptr;
  undefined4 *gpu_data_ptr;
  int loop_counter;
  short tex_u2, tex_v2;
  uint texture_mode;

  // Get current command buffer pointer
  command_buffer = gpu_command_buffer_start;

  // Check if we have enough space in the command buffer (1024 bytes needed)
  if (gpu_command_buffer_end - (int)gpu_command_buffer_current < 0x400)
  {
    return; // Not enough space, abort
  }

  // Advance buffer pointer and check bounds
  gpu_command_buffer_start = gpu_command_buffer_start + 0xc;
  if ((long *)0x70003fff < gpu_command_buffer_start)
  {
    // Buffer overflow, call error handler
    graphics_buffer_overflow_handler(0);
  }

  // Handle texture information
  if (texture_info < 0)
  {
    // Special texture handling for negative values
    *(int *)(command_buffer + 10) = -(int)texture_info;
    texture_info = 0xffff;
  }
  else
  {
    // Normal texture handling - divide by 16 for texture page calculation
    loop_counter = (int)texture_info >> 4;
    *(int *)(command_buffer + 10) = loop_counter;
    if (loop_counter < 2)
    {
      temp_data = 2;
    }
    else
    {
      if (loop_counter < 0x1000)
        goto CONTINUE_SETUP;
      temp_data = 0xfff;
    }
    *(undefined4 *)(command_buffer + 10) = temp_data;
  }

CONTINUE_SETUP:
  // Calculate vertex coordinates (convert to PS2 screen coordinates)
  vertex_x1 = (short)(x_pos << 4) + -0x8000; // X1 = (x * 16) - 32768
  tex_u = (short)(texture_u << 4);
  *(short *)((int)command_buffer + 0x1c) = tex_u;
  vertex_x2 = ((short)x_pos + width) * 0x10 + -0x8000; // X2 = ((x + width) * 16) - 32768
  *(short *)(command_buffer + 3) = tex_u;
  tex_v = (short)(texture_v << 4);
  *(short *)((int)command_buffer + 0x26) = tex_v;
  tex_u = tex_u + (short)(texture_width << 4);
  *(short *)((int)command_buffer + 0x1a) = tex_v;
  vertex_y1 = (short)(y_pos << 3) + -0x8000; // Y1 = (y * 8) - 32768
  tex_v = tex_v + (short)(texture_height << 4);
  vertex_y2 = ((short)y_pos + height) * 8 + -0x8000; // Y2 = ((y + height) * 8) - 32768

  // Set up vertex data for quad (4 vertices)
  *(short *)(command_buffer + 5) = vertex_x1; // Top-left X
  texture_mode = color_data + 1;
  *(short *)(command_buffer + 7) = vertex_x2;         // Top-right X
  *(short *)((int)command_buffer + 0x2a) = vertex_y1; // Top-left Y
  *(short *)((int)command_buffer + 0x32) = vertex_y2; // Bottom-left Y
  *(short *)(command_buffer + 4) = tex_u;
  *(short *)((int)command_buffer + 0x1e) = tex_v;

  // Set up remaining vertices
  loop_counter = 3;
  *(short *)(command_buffer + 6) = vertex_x1;         // Bottom-left X
  *(short *)(command_buffer + 8) = vertex_x2;         // Bottom-right X
  *(short *)((int)command_buffer + 0x42) = vertex_y1; // Top-right Y
  *(short *)((int)command_buffer + 0x3a) = vertex_y2; // Bottom-right Y
  *(short *)((int)command_buffer + 0x24) = tex_u;
  *(short *)((int)command_buffer + 0x22) = tex_v;

  // Set texture coordinates for all vertices
  buffer_ptr = command_buffer;
  do
  {
    *(short *)((int)buffer_ptr + 0x2c) = (short)texture_info;
    loop_counter = loop_counter + -1;
    *(undefined2 *)((int)buffer_ptr + 0x2e) = 0xff; // Alpha value
    buffer_ptr = buffer_ptr + 1;
  } while (-1 < loop_counter);

  // Set up GPU command packet header
  *(undefined4 **)(command_buffer + 9) = gpu_command_buffer_current;
  command_buffer[1] = 0xd; // Base packet type
  *command_buffer = 2;     // 2 vertices for line/triangle

  if (texture_mode != 0)
  {
    *command_buffer = 3;      // 3 vertices for textured primitive
    command_buffer[1] = 0x1d; // Textured packet type
  }

  *(undefined4 *)((int)command_buffer + 0x4c) = 0;

  // Handle rendering flags for blending/transparency
  if ((render_flags & 1) == 0)
  {
    if ((render_flags & 2) == 0)
    {
      temp_data = 3;
      if ((render_flags & 4) == 0)
        goto CONTINUE_PACKET_SETUP;
      packed_data = command_buffer[1];
    }
    else
    {
      packed_data = command_buffer[1];
      temp_data = 2;
    }
  }
  else
  {
    packed_data = command_buffer[1];
    temp_data = 1;
  }
  *(undefined4 *)((int)command_buffer + 0x4c) = temp_data;
  command_buffer[1] = packed_data | 0x40; // Set transparency bit

CONTINUE_PACKET_SETUP:
  // Build GPU command packet
  *(undefined4 *)((int)gpu_command_buffer_current + 8) = 0x6e03c000;
  *(undefined1 *)((int)gpu_command_buffer_current + 0xc) = 4;
  command_ptr = (char *)((int)gpu_command_buffer_current + 0xd);

  if (texture_mode == 0)
  {
    // Non-textured primitive
    *command_ptr = '\0';
    *(undefined1 *)((int)gpu_command_buffer_current + 0xe) = 0;
  }
  else
  {
    // Textured primitive
    if ((texture_mode & 0xff) < 0x18)
    {
      *command_ptr = '\0';
    }
    else
    {
      *command_ptr = (char)(texture_mode >> 8) + '\x01';
    }
    *(char *)((int)gpu_command_buffer_current + 0xe) = (char)texture_mode;
  }

  *(undefined1 *)((int)gpu_command_buffer_current + 0xf) = 0;
  *(char *)((int)gpu_command_buffer_current + 0x10) =
      (char)(*(int *)((int)command_buffer + 0x4c) << 1) + (char)*(int *)((int)command_buffer + 0x4c);
  *(undefined1 *)((int)gpu_command_buffer_current + 0x11) = 0;
  *(undefined2 *)((int)gpu_command_buffer_current + 0x12) = 0;

  buffer_value = *command_buffer;
  *(undefined4 *)((int)gpu_command_buffer_current + 0x14) = 0;
  temp_long = command_buffer[1];
  *(undefined4 *)((int)gpu_command_buffer_current + 0x18) = 0x65018003;
  *(uint *)((int)gpu_command_buffer_current + 0x1c) = texture_mode;
  packed_data = temp_long << 0x2f | buffer_value << 0x3c | 0x400000008004U;
  *(undefined4 *)((int)gpu_command_buffer_current + 0x20) = 0x68018004;
  command_buffer[2] = packed_data;
  *(int *)((int)gpu_command_buffer_current + 0x24) = (int)command_buffer[2];
  *(int *)((int)gpu_command_buffer_current + 0x28) = (int)(packed_data >> 0x20);

  if (texture_mode == 0)
  {
    gpu_mode = 0x41; // Non-textured mode
  }
  else
  {
    gpu_mode = 0x412; // Textured mode
  }
  *(undefined2 *)((int)gpu_command_buffer_current + 0x2c) = gpu_mode;
  *(undefined4 *)((int)gpu_command_buffer_current + 0x30) = 0x6e01c024;
  *(undefined4 *)((int)gpu_command_buffer_current + 0x34) = gpu_command;
  gpu_data_ptr = (undefined4 *)((int)gpu_command_buffer_current + 0x38);

  if (texture_mode != 0)
  {
    // Add texture data for textured primitives
    buffer_ptr = command_buffer + 3;
    loop_counter = 3;
    *gpu_data_ptr = 0x65048039;
    gpu_data_ptr = (undefined4 *)((int)gpu_command_buffer_current + 0x3c);
    do
    {
      temp_long = *buffer_ptr;
      loop_counter = loop_counter + -1;
      buffer_ptr = (long *)((int)buffer_ptr + 4);
      *gpu_data_ptr = (int)temp_long;
      gpu_data_ptr = gpu_data_ptr + 1;
    } while (-1 < loop_counter);
  }

  // Add vertex data
  loop_counter = 3;
  *gpu_data_ptr = 0x6d04c006;
  gpu_command_buffer_current = gpu_data_ptr + 1;
  buffer_ptr = command_buffer;
  do
  {
    gpu_command_buffer_current = gpu_command_buffer_current + 2;
    loop_counter = loop_counter + -1;
    gpu_data_ptr[1] = (int)buffer_ptr[5];
    gpu_ptr = (undefined4 *)((int)buffer_ptr + 0x2c);
    buffer_ptr = buffer_ptr + 1;
    gpu_data_ptr[2] = *gpu_ptr;
    gpu_data_ptr = gpu_data_ptr + 2;
  } while (-1 < loop_counter);

  // Finalize packet
  *gpu_command_buffer_current = 0x14000084;
  while (loop_counter = gpu_interrupt_counter, gpu_command_buffer_current = gpu_command_buffer_current + 1,
         ((uint)gpu_command_buffer_current & 0xf) != 0)
  {
    *gpu_command_buffer_current = 0; // Pad to 16-byte alignment
  }

  // Set DMA packet size and link
  **(uint **)(command_buffer + 9) =
      ((uint)((int)gpu_command_buffer_current - (int)*(uint **)(command_buffer + 9)) >> 4) - 1 | 0x20000000;
  *(undefined4 *)((int)command_buffer[9] + 4) = *(undefined4 *)((int)command_buffer[10] * 0x10 + loop_counter + 4);

  // Synchronize and update buffer pointers
  __sync(); // PS2 cache sync instruction
  gpu_command_buffer_start = gpu_command_buffer_start + -0xc;
  *(int *)((int)command_buffer[10] * 0x10 + loop_counter + 4) = (int)command_buffer[9];
  return;
}

// Global variables for PS2 GPU command buffer management:

/**
 * GPU command buffer start pointer
 * Original: DAT_70000000
 */
extern long *gpu_command_buffer_start;

/**
 * GPU command buffer current write position
 * Original: DAT_70000004
 */
extern undefined4 *gpu_command_buffer_current;

/**
 * GPU command buffer end pointer
 * Original: DAT_70000008
 */
extern int gpu_command_buffer_end;

/**
 * GPU interrupt counter
 * Original: DAT_7000000c
 */
extern int gpu_interrupt_counter;

// Function prototype for referenced function:

/**
 * Graphics buffer overflow handler
 * Original: FUN_0026bf90
 */
extern void graphics_buffer_overflow_handler(int error_code);
