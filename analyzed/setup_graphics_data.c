#include <stdint.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned char undefined1;
typedef unsigned short undefined2;
typedef unsigned int undefined4;
typedef unsigned short ushort;
typedef unsigned int uint;

// Forward declaration for referenced function
extern void FUN_00207de8(int param); // Unknown function - likely graphics command

/**
 * Graphics data setup function - configures coordinate/vertex data structures
 *
 * This function appears to set up graphics-related data structures with coordinate
 * values and vertex information. It manipulates floating-point constants and
 * coordinate data, suggesting this is part of a 3D graphics or rendering pipeline.
 *
 * The function sets up what appears to be vertex coordinates or transformation
 * matrices with specific floating-point values, then calls another function
 * with a graphics-related parameter.
 *
 * Original function: FUN_0025d0e0
 * Address: 0x0025d0e0
 *
 * @param coordinate_data Coordinate or transformation data
 * @param graphics_mode Graphics mode flag (affects final configuration)
 */
void setup_graphics_data(undefined4 coordinate_data, char graphics_mode)
{
  undefined4 texture_value;
  int data_base;
  undefined1 coord_byte1;
  undefined1 coord_byte2;
  int loop_counter;
  undefined4 *vertex_ptr;
  undefined4 *texture_ptr;
  undefined4 packed_coords;

  // Get base address for graphics data structure
  data_base = graphics_data_base;
  texture_ptr = (undefined4 *)(graphics_data_base + 0x28); // Texture data offset
  vertex_ptr = (undefined4 *)(graphics_data_base + 0x10);  // Vertex data offset
  loop_counter = 3;                                        // Process 4 elements (0, 1, 2, 3)

  // Set up floating-point coordinate constants:
  *(undefined4 *)(graphics_data_base + 0x34) = 0xc3600000; // -224.0f
  *(undefined4 *)(data_base + 0x44) = 0xc3600000;          // -224.0f
  *(undefined4 *)(data_base + 0x20) = 0xc3a00000;          // -320.0f
  *(undefined4 *)(data_base + 0x40) = 0x43a00000;          //  320.0f
  *(undefined4 *)(data_base + 0x24) = 0x43600000;          //  224.0f
  *(undefined4 *)(data_base + 0x30) = 0xc3a00000;          // -320.0f
  *(undefined4 *)(data_base + 0x50) = 0x43a00000;          //  320.0f
  *(undefined4 *)(data_base + 0x54) = 0x43600000;          //  224.0f

  // Get texture value
  texture_value = texture_constant;

  // Pack coordinate data into bytes:
  packed_coords._0_1_ = (undefined1)coordinate_data; // Byte 0
  coord_byte1 = (undefined1)packed_coords;
  packed_coords._2_1_ = (undefined1)((uint)coordinate_data >> 0x10); // Byte 2
  coord_byte2 = packed_coords._2_1_;
  packed_coords._3_1_ = (undefined1)((uint)coordinate_data >> 0x18);   // Byte 3
  packed_coords._0_3_ = CONCAT12(coord_byte1, (short)coordinate_data); // Combine bytes 0-1
  packed_coords = CONCAT31(packed_coords._1_3_, coord_byte2);          // Final packed value

  // Fill arrays with coordinate and texture data:
  do
  {
    loop_counter = loop_counter + -1;
    *texture_ptr = texture_value;  // Set texture data
    *vertex_ptr = packed_coords;   // Set coordinate data
    texture_ptr = texture_ptr + 4; // Advance by 16 bytes
    vertex_ptr = vertex_ptr + 1;   // Advance by 4 bytes
  } while (-1 < loop_counter);

  // Configure graphics parameters:
  *(undefined2 *)(data_base + 4) = 4;         // Graphics parameter 1
  *(undefined2 *)(data_base + 6) = 0xffff;    // Graphics parameter 2
  *(undefined4 *)(data_base + 0xc) = 0x40180; // Base graphics mode

  // Modify graphics mode based on parameter:
  if (graphics_mode != '\0')
  {
    *(undefined4 *)(data_base + 0xc) = 0x44180; // Enhanced graphics mode
  }

  // Execute graphics command:
  FUN_00207de8(0x1007); // Graphics operation with command 0x1007
  return;
}

// Global variables for graphics system:

/**
 * Graphics data structure base address
 * Original: DAT_00355724
 */
extern int graphics_data_base;

/**
 * Texture constant value
 * Original: DAT_00352b68
 */
extern undefined4 texture_constant;
