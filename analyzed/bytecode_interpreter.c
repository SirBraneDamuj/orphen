// Originally FUN_0025c258
// Bytecode interpreter/virtual machine that executes script instructions
// Uses a stack-based execution model with instruction pointer and opcode dispatch

#include <stdint.h>

typedef unsigned char byte;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef uint undefined4;
typedef uint (*code)(void);

// External global variables (script execution state)
extern byte *DAT_00355cd0;     // Instruction pointer
extern ushort DAT_00355cd8;    // Current instruction value
extern void *PTR_LAB_0031e228; // Standard instruction jump table
extern void *PTR_LAB_0031e538; // Extended instruction jump table

// External function for handling basic opcodes
extern long FUN_0025bf70(uint *param);

void bytecode_interpreter(uint *result_param)
{
  byte instruction_byte;
  uint stack_value1;
  uint stack_value2;
  long call_result;
  uint *stack_ptr;
  undefined4 local_stack[8]; // Local execution stack
  uint temp_array[4];
  uint working_buffer[4];
  code *instruction_handler;

  stack_ptr = temp_array;
  stack_value2 = 0;

  // Initialize local stack with sentinel values
  do
  {
    working_buffer[0] = stack_value2 + 1;
    local_stack[stack_value2] = 0xffffffff;
    stack_value2 = working_buffer[0];
  } while ((int)working_buffer[0] < 8);

  temp_array[0] = 0;

LAB_0025c2b8:
  while (1)
  {
    // Process instructions with opcodes > 0x31 (high-level instructions)
    while (0x31 < *DAT_00355cd0)
    {
      instruction_byte = *DAT_00355cd0;

      if (instruction_byte == 0xff)
      {
        // Extended instruction set (0x100 + next byte)
        DAT_00355cd8 = DAT_00355cd0[1] + 0x100;
        instruction_handler = (code)(&PTR_LAB_0031e538)[DAT_00355cd0[1]];
        DAT_00355cd0 = DAT_00355cd0 + 2; // Advance past opcode and parameter
      }
      else
      {
        // Standard instruction set (opcodes 0x32-0xFE)
        DAT_00355cd8 = (ushort)instruction_byte;
        instruction_handler = (code)(&PTR_LAB_0031e228)[instruction_byte - 0x32];
        DAT_00355cd0 = DAT_00355cd0 + 1; // Advance past opcode
      }

      // Push result onto stack and execute instruction
      stack_ptr = stack_ptr + -1;
      stack_value2 = (*instruction_handler)();
      *stack_ptr = stack_value2;
    }

    // Call function to handle lower-level operations (opcodes 0x00-0x31)
    call_result = FUN_0025bf70(working_buffer);
    if (call_result == 0)
      break; // Exit if function returns 0

    // Push result onto stack
    stack_ptr = stack_ptr + -1;
    *stack_ptr = working_buffer[0];
  }

  // Process final opcode (0x00-0x31 range)
  switch (*DAT_00355cd0)
  {
  case 0xb: // Return/exit instruction
    *result_param = *stack_ptr;
    DAT_00355cd0 = DAT_00355cd0 + 1;
    return;

  case 0x12: // Equality comparison
    stack_ptr[1] = (uint)(stack_ptr[1] == *stack_ptr);
    break;

  case 0x13: // Inequality comparison
    stack_ptr[1] = (uint)(stack_ptr[1] != *stack_ptr);
    break;

  case 0x14: // Less than (signed) - argument order: stack[1] < stack[0]
    stack_value1 = stack_ptr[1];
    stack_value2 = *stack_ptr;
    goto LAB_0025c43c;

  case 0x15: // Less than (signed) - argument order: stack[0] < stack[1]
    stack_value2 = stack_ptr[1];
    stack_value1 = *stack_ptr;
  LAB_0025c43c:
    stack_ptr[1] = (uint)((int)stack_value1 < (int)stack_value2);
    break;

  case 0x16: // Greater than or equal (signed) - argument order: stack[1] >= stack[0]
    stack_value1 = stack_ptr[1];
    stack_value2 = *stack_ptr;
    goto LAB_0025c45c;

  case 0x17: // Greater than or equal (signed) - argument order: stack[0] >= stack[1]
    stack_value2 = stack_ptr[1];
    stack_value1 = *stack_ptr;
  LAB_0025c45c:
    stack_ptr[1] = (int)stack_value2 < (int)stack_value1 ^ 1; // !(val2 < val1) == (val2 >= val1)
    break;

  case 0x18: // Logical NOT (zero test)
    stack_value2 = (uint)(*stack_ptr == 0);
    goto LAB_0025c3e8;

  case 0x19: // Bitwise NOT
    stack_value2 = ~*stack_ptr;
    goto LAB_0025c3e8;

  case 0x1a: // Logical AND
    stack_value2 = (uint)(*stack_ptr != 0);
    if (stack_ptr[1] == 0)
    {
      stack_value2 = 0;
    }
    stack_ptr[1] = stack_value2;
    break;

  case 0x1b: // Bitwise OR
  case 0x21: // Bitwise OR (duplicate opcode)
    stack_ptr[1] = stack_ptr[1] | *stack_ptr;
    break;

  case 0x1c: // Addition
    stack_ptr[1] = stack_ptr[1] + *stack_ptr;
    break;

  case 0x1d: // Subtraction
    stack_ptr[1] = stack_ptr[1] - *stack_ptr;
    break;

  case 0x1e: // Unary negation
    stack_value2 = -*stack_ptr;
  LAB_0025c3e8:
    DAT_00355cd0 = DAT_00355cd0 + 1;
    *stack_ptr = stack_value2;
    goto LAB_0025c2b8;

  case 0x1f: // Bitwise XOR
    stack_ptr[1] = stack_ptr[1] ^ *stack_ptr;
    break;

  case 0x20: // Bitwise AND
    stack_ptr[1] = stack_ptr[1] & *stack_ptr;
    break;

  case 0x22: // Division (signed, with divide-by-zero check)
    if (*stack_ptr == 0)
    {
      trap(7); // Division by zero trap
    }
    stack_ptr[1] = (int)stack_ptr[1] / (int)*stack_ptr;
    break;

  case 0x23: // Multiplication
    stack_ptr[1] = stack_ptr[1] * *stack_ptr;
    break;

  case 0x24: // Modulo (signed, with divide-by-zero check)
    if (*stack_ptr == 0)
    {
      trap(7); // Division by zero trap
    }
    stack_ptr[1] = (int)stack_ptr[1] % (int)*stack_ptr;
  }

  // Pop one item from stack and continue execution
  stack_ptr = stack_ptr + 1;
  DAT_00355cd0 = DAT_00355cd0 + 1;
  goto LAB_0025c2b8;
}
