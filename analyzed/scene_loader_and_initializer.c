/*
 * Scene Loader and Initializer - FUN_0025b390
 *
 * Main scene loading and initialization function that handles loading scene data
 * from files and setting up the scene processing system. This function manages
 * memory allocation, buffer setup, and initialization of scene objects arrays.
 *
 * The function handles three main scenarios:
 * 1. Normal scene loading (param_1 = scene data, param_2 = 0)
 * 2. Special scene restoration (param_2 < 0) - restores from existing buffer
 * 3. Invalid/empty scene data (param_1 = 0 or no scene size) - creates empty scene
 *
 * Error handling includes parameter validation and memory overflow detection.
 * Debug strings indicate this is the "load_script" function from developer logs.
 *
 * Original function: FUN_0025b390
 */

#include "orphen_globals.h"

// Forward declarations for functions not yet analyzed
extern void FUN_0026bfc0(unsigned int error_format_addr);                    // Debug error handler/crash function
extern void FUN_0025b288(void);                                              // Scene initialization helper 1
extern void FUN_0025b2f0(void);                                              // Scene initialization helper 2
extern long FUN_00223268(int archive_type, short file_id, void *buffer);     // File loader (already identified)
extern void FUN_002f3118(void *buffer1, void *buffer2);                      // Memory system initialization
extern void FUN_00267da0(void *dest, void *src, unsigned int size);          // Memory copy function
extern void FUN_0030c1d8(char *buffer, unsigned int format_addr, int value); // sprintf_formatted

// Scene loading globals (not yet in orphen_globals.h)
extern unsigned int *DAT_0035572c; // Current memory allocation pointer
extern unsigned int *DAT_0035561c; // Scene data buffer base address
extern unsigned int DAT_00355040;  // Scene buffer offset/size tracker
extern unsigned int DAT_00355720;  // Scene data size from file
extern unsigned char DAT_003555d3; // Scene loading mode flag
extern unsigned int DAT_003551ec;  // System flags bitfield

// Scene processing globals (from process_scene_with_work_flags.c)
extern unsigned int *DAT_00355058;  // Scene data pointer
extern int DAT_00355cf4;            // Scene objects array pointer
extern unsigned char *DAT_00355060; // Scene work data array pointer

// Static scene work data fallback
extern unsigned int DAT_00343470; // Default/static scene work data array

/*
 * Loads and initializes scene data with memory management
 *
 * Handles three loading modes:
 * 1. Normal loading: Loads scene file, allocates buffers, sets up arrays
 * 2. Restoration mode (param_2 < 0): Restores from existing buffer without file I/O
 * 3. Empty scene: Creates minimal scene with default values when no data available
 *
 * Memory layout after successful load:
 * - DAT_00355058: Points to main scene data
 * - DAT_00355cf4: Points to scene objects array (at scene_data + aligned_size)
 * - DAT_00355060: Points to scene work data (default or at objects_array + 0x104)
 *
 * The function includes bounds checking to prevent buffer overflows beyond 0x20000 bytes.
 */
void scene_loader_and_initializer(long scene_data_ptr, long load_mode)
{
  unsigned int *allocation_ptr;
  short scene_file_id;
  unsigned int *scene_buffer_ptr;
  long file_load_result;
  int object_index;
  int objects_array_addr;
  unsigned int aligned_scene_size;
  unsigned int total_required_size;
  char error_buffer[256];

  // Validate parameters - error if load_mode > 1
  if (1 < load_mode)
  {
    FUN_0026bfc0(0x34ca00); // "parameter error argc2 [load_script]"
  }

  scene_buffer_ptr = DAT_0035572c;

  // Handle scene restoration mode (load_mode < 0)
  if (load_mode < 0)
  {
    // Restore scene from existing buffer without file loading
    DAT_00355060 = (unsigned char *)&DAT_00343470;             // Use default scene work data
    DAT_00355cf4 = (int)DAT_0035561c + (DAT_00355040 - 0x304); // Restore objects array pointer
    DAT_00355058 = DAT_0035561c;                               // Restore main scene data pointer
    FUN_0025b288();                                            // Initialize scene system 1
    FUN_0025b2f0();                                            // Initialize scene system 2
    return;
  }

  // Handle empty/invalid scene data
  if ((scene_data_ptr == 0) || (scene_file_id = *(short *)((int)scene_data_ptr + 4), scene_file_id == 0))
  {
    // Create minimal empty scene
    DAT_00355058 = DAT_0035572c;
    allocation_ptr = DAT_0035572c + 0xb;
    DAT_0035572c = DAT_0035572c + 0xc;
    *(unsigned char *)allocation_ptr = 4;          // Set scene type/version
    scene_buffer_ptr[0] = 0x2c;                    // Initialize scene header values
    DAT_00355060 = (unsigned char *)&DAT_00343470; // Use default scene work data
    scene_buffer_ptr[7] = 0x2c;
    DAT_0035572c = (unsigned int *)((int)DAT_0035572c + 0x37U & 0xfffffffc); // Align to 4-byte boundary
    scene_buffer_ptr[5] = 0x2c;
    scene_buffer_ptr[4] = 0x2c;
    scene_buffer_ptr[3] = 0x2c;
    scene_buffer_ptr[2] = 0x2c;
    scene_buffer_ptr[1] = 0x2c;
    scene_buffer_ptr[9] = 0;
    scene_buffer_ptr[8] = 0;
    scene_buffer_ptr[6] = 0;
    DAT_00355cf4 = 0; // No scene objects array
    goto FINALIZE_SCENE_SETUP;
  }

  // Load scene file from archive
  file_load_result = FUN_00223268(1, scene_file_id, 0x1849a00); // Load from archive type 1
  if (file_load_result < 0)
  {
    FUN_0026bfc0(0x34ca28); // "file scen open error"
  }

  // Initialize memory system for scene data
  FUN_002f3118(0x1849a00, 0x1859a00);

  // Calculate memory requirements with 4-byte alignment
  aligned_scene_size = DAT_00355720 + 3U & 0xfffffffc; // Align scene data size to 4 bytes
  total_required_size = aligned_scene_size + 0x304;    // Add space for objects array (62*4 + header)

  // Check memory bounds to prevent overflow
  if (total_required_size < 0x20001)
  {
    if (DAT_003555d3 == '\0')
    {
      // First-time loading mode
      DAT_00355058 = DAT_0035561c;        // Set scene data to buffer base
      DAT_00355040 = total_required_size; // Track total allocation size
    }
    else
    {
      // Incremental loading mode
      DAT_00355058 = (unsigned int *)((int)DAT_0035561c + DAT_00355040); // Append to existing buffer
      if (0x20000 < DAT_00355040 + total_required_size)
        goto MEMORY_OVERFLOW_ERROR;
    }
  }
  else
  {
  MEMORY_OVERFLOW_ERROR:
    // Buffer overflow - format error message with overflow amount
    FUN_0030c1d8(error_buffer, 0x34ca40, DAT_00355040 + total_required_size + -0x20000);
    FUN_0026bfc0(error_buffer); // "ER_SIZEOVER(0x%X) [load_script]"
  }

  // Copy scene data from file buffer to allocated memory
  FUN_00267da0(DAT_00355058, 0x1859a00, aligned_scene_size);

  // Set up scene objects array pointer (placed after aligned scene data)
  DAT_00355cf4 = (int)DAT_00355058 + aligned_scene_size;

  // Set up scene work data array pointer
  if (DAT_003555d3 == '\0')
  {
    DAT_00355060 = (unsigned char *)&DAT_00343470; // Use default static array
  }
  else
  {
    DAT_00355060 = (unsigned char *)(DAT_00355cf4 + 0x104); // Use dynamic array after objects
  }

  // Initialize scene processing systems
  FUN_0025b2f0(); // Scene initialization helper 2
  FUN_0025b288(); // Scene initialization helper 1

FINALIZE_SCENE_SETUP:
  objects_array_addr = DAT_00355cf4;

  // Clear scene work data array if system flag not set
  if ((DAT_003551ec & 0x80000) == 0)
  {
    object_index = 0x7f;                                     // 128 entries (0-127)
    allocation_ptr = (unsigned int *)(DAT_00355060 + 0x1fc); // Start from end (128*4-4 = 0x1fc)
    do
    {
      *allocation_ptr = 0; // Clear work data entry
      object_index = object_index + -1;
      allocation_ptr = allocation_ptr - 1; // Move to previous entry (4 bytes back)
    } while (-1 < object_index);
  }

  // Clear scene objects array if it exists
  if (objects_array_addr != 0)
  {
    allocation_ptr = (unsigned int *)(objects_array_addr + 0x100); // 64 entries (62 objects + 2 extra)
    object_index = 0x40;                                           // 64 entries total
    do
    {
      *allocation_ptr = 0; // Clear objects array entry
      object_index = object_index + -1;
      allocation_ptr = allocation_ptr - 1; // Move to previous entry (4 bytes back)
    } while (-1 < object_index);
  }

  return;
}
