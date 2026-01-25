// Extended opcodes 0x125/0x126 — audio_select_and_trigger (analyzed)
// Original: FUN_00261330
//
// Summary:
// - Reads u16 immediate (sound/command ID) from bytecode stream.
// - Evaluates 1 expression (object selector).
// - Calls FUN_0025d6c0 to select object based on expression result.
// - Branches based on opcode:
//     0x125: Calls FUN_00267d38(command_id) - single-arg audio command
//     0x126: Evaluates 2nd expression, calls FUN_00267d88(command_id, current_obj, param2)
// - Commonly paired with opcode 0x103 in scripts.
// - Returns 0.
//
// Typical usage:
//   FF 25 <u16_id> <obj_selector_expr>             # 0x125: Select object, trigger audio
//   FF 26 <u16_id> <obj_selector_expr> <param_expr> # 0x126: Select object, trigger with param
//
// Context:
// - Part of audio system that can bind sounds to specific entities.
// - FUN_0025d6c0 is object selector (sets DAT_00355044 based on expression).
// - FUN_00267d38/FUN_00267d88 are audio command dispatchers.
// - Often used for positional/3D audio (sound attached to entity location).
//
// PS2 notes:
// - u16 ID likely indexes into sound bank or command table.
// - Object binding enables spatial audio calculations (see FUN_0023b6f0 for 3D positional audio).
// - Opcode 0x126 variant allows additional parameter (volume/pitch/distance override).
//
// Keep unresolved externs by their original labels for traceability.

#include <stdint.h>

typedef unsigned int uint;
typedef unsigned short ushort;

// VM entry (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig FUN_0025c258

// Bytecode instruction pointer
extern unsigned char *DAT_00355cd0;

// Current opcode value (set by interpreter)
extern ushort DAT_00355cd8;

// Current selected object pointer
extern void *DAT_00355044;

// Object selector (sets DAT_00355044 based on expression result)
// Original: FUN_0025d6c0
extern void FUN_0025d6c0(uint selector_result, void *fallback_obj);

// Audio command dispatchers (unanalyzed)
extern void FUN_00267d38(int command_id);
extern void FUN_00267d88(int command_id, void *obj, uint param);

// Original signature: undefined8 FUN_00261330(void)
uint64_t opcode_0x125_0x126_audio_select_and_trigger(void)
{
  ushort opcode = DAT_00355cd8; // 0x125 or 0x126
  void *saved_obj = DAT_00355044;

  // Read u16 command/sound ID from bytecode stream (little-endian)
  int command_id = (int)(ushort)(DAT_00355cd0[0] | (DAT_00355cd0[1] << 8));
  DAT_00355cd0 += 2; // Advance instruction pointer

  uint params[2];

  // Evaluate object selector expression
  bytecode_interpreter(&params[0]);

  // Select object based on expression result
  FUN_0025d6c0(params[0], saved_obj);

  if (opcode == 0x125)
  {
    // Simple variant: trigger audio command with selected object
    FUN_00267d38(command_id);
  }
  else // opcode == 0x126
  {
    // Extended variant: evaluate 2nd parameter and pass to audio command
    bytecode_interpreter(&params[1]);
    FUN_00267d88(command_id, DAT_00355044, params[1]);
  }

  return 0;
}
