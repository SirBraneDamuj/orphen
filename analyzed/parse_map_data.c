/**
 * @file parse_map_data.c
 * @brief Parses loaded map data from MAP.BIN archive into memory structures
 * @original_name FUN_0022b5a8
 *
 * This function processes a loaded map file that was read from MAP.BIN.
 * The map data uses a custom format with "PSM2" magic header followed by
 * multiple data sections containing geometry, entities, audio, and other
 * game data needed to render and interact with the map.
 *
 * Map data format structure:
 * - Header: "PSM2" magic number (0x324d5350) at offset 0x00
 * - Offset table: Section offsets at 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x2C, 0x30, 0x34, 0x38
 * - Data sections: Variable-length sections containing different data types
 *
 * Verified section processing (from code analysis):
 * - 0x04: Processes entities with 32-byte structures (6 uint32 values + padding, ID set to 0xffff)
 * - 0x08: Processes vertex data with coordinates and indices
 * - 0x30: Processes 3D coordinate triplets (x,y,z) in 16-byte structures
 * - Other offsets: 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x2C, 0x34, 0x38 (processing not analyzed)
 *
 * The function allocates and populates various global data structures
 * with parsed map information, then calls additional setup functions.
 */

// Map data buffer and offset globals (loaded by FUN_00223268)
extern unsigned int DAT_01849a00; // Map data buffer base - magic header location
extern unsigned int DAT_01849a04; // Offset to section 1 (32-byte entity structures)
extern unsigned int DAT_01849a08; // Offset to section 2 (vertex data with coordinates/indices)
extern unsigned int DAT_01849a0c; // Offset to section 3 (unanalyzed)
extern unsigned int DAT_01849a10; // Offset to section 4 (unanalyzed)
extern unsigned int DAT_01849a14; // Offset to section 5 (unanalyzed)
extern unsigned int DAT_01849a18; // Offset to section 6 (unanalyzed)
extern unsigned int DAT_01849a1c; // Offset to section 7 (unanalyzed)
extern unsigned int DAT_01849a2c; // Offset to section 8 (unanalyzed)
extern unsigned int DAT_01849a30; // Offset to section 9 (3D coordinate triplets)
extern unsigned int DAT_01849a34; // Offset to section 10 (unanalyzed)
extern unsigned int DAT_01849a38; // Offset to section 11 (unanalyzed)

// Parsed map data globals (populated by this function)
extern unsigned int DAT_003556d8;   // Entity data base address
extern unsigned int DAT_003556d4;   // Entity count
extern unsigned int DAT_003556d0;   // Entity data secondary count
extern unsigned int DAT_003556a4;   // Vertex data base address
extern unsigned int DAT_0035568c;   // Coordinate count
extern float *DAT_0035572c;         // Current memory pointer for allocation
extern unsigned long *DAT_003556a0; // Vertex indices base
extern unsigned long *DAT_00355698; // Vertex data pointer 2
extern unsigned long *DAT_00355694; // Vertex data pointer 1
extern unsigned long *DAT_0035569c; // Vertex coordinates base
extern unsigned int DAT_00355684;   // Vertex count
extern unsigned int DAT_003556a8;   // Vertex base calculation result
extern unsigned int DAT_00355bdc;   // Computed memory pointer for lighting data

// Helper functions for reading data from map buffer
extern unsigned int *FUN_0022b4e0(unsigned int *source, unsigned int *dest);     // Read uint32
extern unsigned short *FUN_0022b520(unsigned short *source, unsigned int *dest); // Read uint16

// Post-processing setup functions
extern void FUN_0022c3d8(void); // Post-processing function 1
extern void FUN_0022c6e8(void); // Post-processing function 2
extern void FUN_0022d258(void); // Post-processing function 3
extern void FUN_00211230(void); // Final map initialization

// Error handling and memory management
extern void FUN_0026bfc0(unsigned int error_code); // Debug error handler
extern void FUN_002f3118(void);                    // Initialize memory system

void parse_map_data(void)
{
  // Local variables from original decompilation
  unsigned char temp_byte;
  short section_count, temp_short;
  unsigned short temp_ushort;
  unsigned char continue_flag;
  unsigned int temp_uint, current_offset;
  unsigned short *ushort_ptr1, *ushort_ptr2;
  unsigned char *byte_ptr1, *byte_ptr2;
  float *float_ptr1;
  unsigned long temp_ulong;
  unsigned int temp_uint2, temp_uint3;
  unsigned int *uint_ptr1;
  int i, j, k;
  unsigned short *ushort_ptr3;
  float *float_ptr2;
  int loop_index1, loop_index2;
  int entity_index;
  unsigned short *ushort_ptr4;
  short *short_ptr1;
  int vertex_index;
  float *float_ptr3, *float_ptr4;
  unsigned long *ulong_ptr1;
  unsigned int temp_calc;
  short *short_ptr2;
  float *float_ptr5;
  unsigned long *ulong_ptr2;
  long base_offset1, base_offset2, base_offset3;
  unsigned int *uint_ptr2;
  short *short_ptr3;
  unsigned short *ushort_ptr5, *ushort_ptr6;
  short *short_ptr4;
  int entity_count, mesh_count;
  int temp_calc2;
  unsigned long hi_part;
  int temp_stack[4];

  // Initialize memory management system
  FUN_002f3118();

  // Validate map data header - check for "PSM2" magic number
  if (DAT_01849a00 != 0x324d5350)
  {                         // "PSM2" magic header validation
    FUN_0026bfc0(0x34c188); // Invalid map data error
  }

  // Calculate aligned memory address for entity data (4-byte alignment)
  current_offset = ((int)DAT_0035572c + 3U) & 0xfffffffc;
  DAT_003556d8 = current_offset;

  // Process Section 1: 32-byte Entity Structures (offset 0x04)
  if (DAT_01849a04 == 0)
  {
    // No entity data section
    DAT_003556d4 = 0; // Entity count
    DAT_003556d0 = 0; // Entity data count
  }
  else
  {
    // Read entity count from section header
    section_count = *(short *)((int)&DAT_01849a00 + DAT_01849a04);
    base_offset3 = 0;
    DAT_003556d4 = (int)section_count;
    uint_ptr2 = (unsigned int *)((int)&DAT_01849a04 + DAT_01849a04);
    DAT_003556d0 = (int)*(short *)((int)&DAT_01849a00 + DAT_01849a04 + 2);

    // Process each entity (32 bytes per entity structure)
    if (0 < (long)section_count)
    {
      do
      {
        entity_count = (int)base_offset3 * 0x20; // 32 bytes per entity
        base_offset3 = (long)((int)base_offset3 + 1);
        uint_ptr1 = (unsigned int *)(entity_count + current_offset);

        // Copy 6 uint32 values (24 bytes) from source data
        j = 5;
        do
        {
          temp_uint2 = *uint_ptr2;
          j = j + -1;
          uint_ptr2 = uint_ptr2 + 1;
          *uint_ptr1 = temp_uint2;
          uint_ptr1 = uint_ptr1 + 1;
        } while (-1 < j);

        // Initialize remaining 8 bytes to zero, set entity ID to 0xffff
        j = 6;
        uint_ptr1 = (unsigned int *)(entity_count + current_offset + 0x18);
        do
        {
          *uint_ptr1 = 0;
          j = j + 1;
          uint_ptr1 = uint_ptr1 + 1;
        } while (j < 8);
        *(unsigned short *)(entity_count + current_offset + 0x1a) = 0xffff;
      } while (base_offset3 < section_count);
    }
  }

  // Calculate next section base address (16-byte aligned)
  DAT_003556a4 = current_offset + DAT_003556d4 * 0x20 + 0xf & 0xfffffff0;

  // Process Section 9: 3D Coordinate Triplets (offset 0x30)
  if (DAT_01849a30 == 0)
  {
    // No coordinate data section
    DAT_0035568c = 0;
    DAT_0035572c = (float *)DAT_003556a4;
  }
  else
  {
    DAT_0035572c = (float *)DAT_003556a4;
    // Read coordinate data using helper function
    temp_ulong = FUN_0022b4e0((int)&DAT_01849a00 + DAT_01849a30, temp_stack);
    j = 0;
    DAT_0035568c = (int)(short)temp_stack[0];

    // Process coordinate triplets (x, y, z values)
    if (0 < (short)temp_stack[0])
    {
      do
      {
        temp_calc2 = j * 0x10; // 16 bytes per coordinate set
        entity_count = 0;
        j = j + 1;
        do
        {
          temp_ulong = FUN_0022b4e0(temp_ulong, temp_stack);
          loop_index1 = entity_count * 4;
          entity_count = entity_count + 1;
          loop_index2 = temp_calc2 + DAT_003556a4;
          *(int *)(loop_index1 + loop_index2) = temp_stack[0];
        } while (entity_count < 3);
        continue_flag = j < DAT_0035568c;
        *(unsigned int *)(loop_index2 + 0xc) = 0; // Padding/reserved field
      } while (continue_flag);
    }
  }

  // Calculate addresses for vertex-related data sections
  DAT_003556a8 = (int)DAT_0035572c + DAT_0035568c * 0x10;
  DAT_0035572c = (float *)(DAT_003556a8 + DAT_0035568c * 0x10);

  // Check memory bounds to prevent buffer overflow
  if ((unsigned long *)0x18499ff < DAT_0035572c)
  {
    FUN_0026bfc0(0x34c198); // Memory overflow error
  }

  // Process Section 2: Vertex Data with Coordinates and Indices (offset 0x08)
  if (DAT_01849a08 == 0)
  {
    // No vertex data section
    DAT_00355684 = 0;                             // Vertex count
    DAT_003556a0 = (unsigned long *)DAT_0035572c; // Vertex indices base
    DAT_00355698 = (unsigned long *)DAT_0035572c; // Vertex data ptr 2
    DAT_00355694 = (unsigned long *)DAT_0035572c; // Vertex data ptr 1
    DAT_0035569c = (unsigned long *)DAT_0035572c; // Vertex coordinates base
  }
  else
  {
    // Read vertex count and allocate aligned memory for vertex data arrays
    short_ptr3 = (short *)((int)&DAT_01849a00 + DAT_01849a08);
    section_count = *short_ptr3;
    DAT_00355694 = (unsigned long *)((int)DAT_0035572c + 0xfU & 0xfffffff0);
    DAT_00355684 = (int)section_count;
    DAT_00355698 = (unsigned long *)((int)DAT_00355694 + section_count + 0xf & 0xfffffff0);
    DAT_0035569c = (unsigned long *)((int)DAT_00355698 + section_count * 2 + 0xf & 0xfffffff0);
    j = 0;
    DAT_003556a0 = DAT_0035569c + DAT_00355684 * 2;
    DAT_0035572c = (float *)(DAT_003556a0 + DAT_00355684 * 2);

    // Process vertex data entries
    if (0 < section_count)
    {
      do
      {
        // Read vertex coordinates and indices using helper functions
        temp_ulong = FUN_0022b4e0(short_ptr3 + 1, temp_stack);
        *(int *)(DAT_0035569c + j * 2) = temp_stack[0];
        temp_ulong = FUN_0022b4e0(temp_ulong, temp_stack);
        *(int *)((int)DAT_0035569c + j * 0x10 + 4) = temp_stack[0];
        ushort_ptr1 = (unsigned short *)FUN_0022b4e0(temp_ulong, temp_stack);
        ulong_ptr1 = DAT_0035569c;
        temp_ushort = *ushort_ptr1;
        ushort_ptr2 = (unsigned short *)(j * 2 + (int)DAT_00355698);
        *(unsigned int *)((int)DAT_0035569c + j * 0x10 + 0xc) = 0; // Padding

        // Store vertex index data in both arrays
        *(unsigned short *)(j * 2 + (int)DAT_00355694) = temp_ushort;
        *ushort_ptr2 = temp_ushort;

        j = j + 1;
        ushort_ptr1 = ushort_ptr1 + 1;
      } while (j < DAT_00355684);
    }
  }

  // Note: The original function continues processing additional data sections
  // at offsets 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x2C, 0x34, and 0x38.
  // Each follows similar patterns of reading counts, allocating aligned memory,
  // and parsing structured data, but the specific data formats have not been analyzed.

  // Memory bounds check before post-processing
  if (0x18499ff < DAT_0035572c)
  {
    FUN_0026bfc0(0x34c198);
  }

  // Call post-processing setup functions to finalize map data
  FUN_0022c3d8();                       // Post-processing function 1
  DAT_0035572c = (float *)DAT_00355bdc; // Update memory pointer
  FUN_0022c6e8();                       // Post-processing function 2
  FUN_0022d258();                       // Post-processing function 3
  FUN_00211230();                       // Final map initialization

  return;
}
