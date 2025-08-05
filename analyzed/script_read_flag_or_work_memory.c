// Originally FUN_0025d768
// Script instruction that reads/accesses game state flags or work memory
// Appears in bytecode interpreter jump table for opcode 0x36 and potentially others
// Two modes: array lookup (opcode 0x36) vs bitfield access (other opcodes)

#include <stdint.h>

typedef unsigned char byte;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef short sshort;

// External globals
extern ushort DAT_00355cd8; // Current instruction opcode value
extern uint DAT_00355060;   // Pointer to work/flag array data
extern byte DAT_00342b70[]; // Flag bitfield data array

// External functions
extern uint bytecode_interpreter(uint *result);       // FUN_0025c258
extern void debug_output_formatter(uint string_addr); // FUN_0026bfc0

uint script_read_flag_or_work_memory(void)
{
  sshort current_opcode;
  uint result_value;
  uint stack_result[4];

  // Get current instruction opcode for mode determination
  current_opcode = DAT_00355cd8;

  // Execute bytecode interpreter to get parameter value from stack
  bytecode_interpreter(stack_result);

  if (current_opcode == 0x36)
  {
    // Mode 1: Work memory array access (opcode 0x36)
    // Parameter is index into work array (max 128 entries)
    if (0x7f < (int)stack_result[0])
    {
      debug_output_formatter(0x34cdf0); // "script work over"
    }
    // Access work memory array: base_ptr[index]
    result_value = *(uint *)(stack_result[0] * 4 + DAT_00355060);
  }
  else
  {
    // Mode 2: Flag bitfield access (other opcodes)
    // Parameter is bit index into flag array (max 18424 bits = 2303 bytes)
    if ((0x47f8 < (int)stack_result[0]) || ((stack_result[0] & 7) != 0))
    {
      debug_output_formatter(0x34ce08); // "scenario flag work error"
    }

    // Calculate byte index and extract bit value
    // Bit manipulation: get bit at position (index % 8) in byte (index / 8)
    uint byte_index = stack_result[0];
    if (-1 < (int)stack_result[0])
    {
      byte_index = stack_result[0];
    }
    else
    {
      // Handle negative indices by adding 7 (ceiling division)
      byte_index = stack_result[0] + 7;
    }

    // Right shift by 3 = divide by 8 to get byte index
    // Cast result to byte to extract single bit value (0 or 1)
    result_value = (uint)(byte)(&DAT_00342b70)[(int)byte_index >> 3];
  }

  return result_value;
}
