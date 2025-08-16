// Analyzed version of FUN_0025bc68
// Purpose: Higher-level script block structural interpreter handling nested block delimiters.
//
// This routine walks a byte-oriented script buffer using a separate pointer/global state
// (pbGpffffbd60 / uGpffffbd68) from the main stack-based VM (`bytecode_interpreter`). It mixes:
//   * A small low-opcode table (0x00–0x0A excluding 0x04) at PTR_LAB_0031e1f8
//   * Structural block delimiters: 0x32 (block begin / push continuation) and 0x04 (block end)
//   * The main high-opcode dispatch tables PTR_LAB_0031e228 (>=0x32 standard) and PTR_LAB_0031e538 (0xFF extended)
//
// Structural semantics (inferred):
//   - Local variable `depth_counter` starts at BASE_DEPTH (0x10). Each 0x32 decrements it, each 0x04 increments it.
//   - 0x32 saves a "continuation" return address (pbGpffffbd60 + 5) onto a manual return stack (puVar4 -= 1, *puVar4 = address),
//     then advances past the opcode (pbGpffffbd60+1) and calls FUN_0025c220(), which adjusts a different global (DAT_00355cd0).
//     The +5 offset is an observation from the raw code: continuation = current_ptr + 5. The assumed meaning ("skip 4 header bytes")
//     is NOT yet verified; these 4 bytes may represent size, offset, flags, or could be consumed indirectly by VM handlers.
//     Marked as: PENDING_STRUCT_LAYOUT_VALIDATION.
//   - 0x04 pops the continuation (pbGpffffbd60 = *puVar4; puVar4++) unless the interpreter has returned to
//     baseline depth (depth_counter == BASE_DEPTH before increment). At baseline, encountering 0x04 terminates
//     the function (it consumes the 0x04 and returns). Thus 0x04 acts as a BLOCK_END; the top-level final 0x04 closes the structure.
//
//     Subproc chaining motif (observed in SCR): sequences like `9E 0C 01 1E 0B 04 <id16-le>` often appear.
//     Interpretation:
//       * 0x9E finish_process_slot(sel): clears slot table entry. Here `0C 01 1E 0B` evaluates to -1 (imm8 1, negate, return), so
//         sel < 0 => use current slot index `uGpffffbd88`. Writes 0 to `*(iGpffffbd84 + 4*sel)`.
//       * 0x04 at structural layer ends the current block and is immediately followed by a 16-bit ID. That ID is consumed by the
//         next record’s header to schedule/chain the next subproc for that (now-finished) slot. Thus 0x04 here acts as a delimiter
//         and a visual anchor for the following subproc ID16 in the raw bytes, not as a low-range arithmetic opcode.
//     Cross-refs: iGpffffbd84 (slot table base pointer), uGpffffbd88 (current slot index) from globals.json.
//   - The depth counter is used only as a termination sentinel (no bounds enforcement observed besides early exit condition).
//
// Low-opcode mini-table (PTR_LAB_0031e1f8 indices 0x00–0x0A; 0x04 is repurposed, so not dispatched):
//   0x00 -> LAB_0025bdc8
//   0x01 -> FUN_0025bdd0
//   0x02 -> FUN_0025be10
//   0x03 -> LAB_0025bea0
//   0x04 -> (structural end marker handled inline, NOT a table entry)
//   0x05 -> LAB_0025bdc8
//   0x06 -> LAB_0025bdc8
//   0x07 -> LAB_0025bea8
//   0x08 -> LAB_0025beb8
//   0x09 -> LAB_0025bec0
//   0x0A -> LAB_0025bed0
//   0x0B -> LAB_0025bed8 (note: opcodes < 0x0B are dispatched; 0x0B itself is not reached through <0x0B branch)
//
// Relationship to the main VM:
//   This interpreter shares the same handler tables for opcodes >= 0x32 and extended 0xFF-prefixed instructions,
//   implying the high-level opcode semantics are unified. The structural layer here wraps sequences into blocks.
//   The previously observed signature bytes starting with 0x04 likely correspond to these BLOCK_END markers delimiting
//   records / substructures in the raw SCR data, rather than actual executable arithmetic/logical opcodes.
//
// Unanalyzed callees kept by original FUN_/LAB_ names. Globals externed here without renaming until broader context confirmed.
// Further work:
//   * Validate what occupies the 4 bytes after 0x32 by sampling raw script segments (hexdump around 0x32 sites).
//   * Determine whether FUN_0025c220's target region overlaps those bytes or uses separate execution buffer.
//   * Map where 16-bit IDs are read relative to 0x32 / 0x04 boundaries to confirm 0x04<ID16> role (end->new-block ID?).
//   * Inspect low-opcode handlers for implicit consumption of bytes that might explain the +5.

#include <stdint.h>

typedef void (*code_fn)(void);

// External globals (names preserved from raw for traceability)
extern uint8_t *pbGpffffbd60;      // Current script pointer for this structural interpreter
extern unsigned short uGpffffbd68; // Current opcode (mirrors DAT_00355cd8 usage pattern)
extern void *PTR_LAB_0031e1f8;     // Low-opcode mini-dispatch table (0x00–0x0A excluding structural 0x04)
extern void *PTR_LAB_0031e228;     // Standard high-opcode table (shared with main VM)
extern void *PTR_LAB_0031e538;     // Extended opcode table (shared)

// External helpers
extern void FUN_0025c220(void); // Relative jump helper (used after 0x32)

// NOTE: Original signature: void FUN_0025bc68(long param_1)
void script_block_structure_interpreter(uint8_t *script_start)
{
  const int BASE_DEPTH = 0x10; // mirrors initial iVar5
  int depth_counter = BASE_DEPTH;
  uint8_t *return_stack[32]; // conceptual stack
  int sp_index = 32;         // downward-growing stack pointer index

  pbGpffffbd60 = script_start;
  if (script_start == 0)
    return;

  while (pbGpffffbd60 != 0)
  {
    uint8_t opcode = *pbGpffffbd60;
    uint8_t *next = pbGpffffbd60 + 1;

    if (opcode < 0x0B)
    {
      if (opcode == 0x04)
      {
        int at_base = (depth_counter == BASE_DEPTH);
        depth_counter++;
        if (at_base)
        {
          pbGpffffbd60 = next; // consume terminating 0x04
          return;              // exit interpreter
        }
        if (sp_index < 32)
        {
          pbGpffffbd60 = return_stack[sp_index];
          sp_index++; // pop
        }
        else
        {
          return; // underflow safeguard
        }
      }
      else
      {
        pbGpffffbd60 = next;
        ((code_fn *)&PTR_LAB_0031e1f8)[opcode]();
      }
    }
    else if (opcode == 0xFF)
    {
      uint8_t ext = *next;
      uGpffffbd68 = (unsigned short)(ext + 0x100);
      pbGpffffbd60 += 2;
      ((code_fn *)&PTR_LAB_0031e538)[ext]();
    }
    else if (opcode == 0x32)
    {
      if (sp_index == 0)
        return; // overflow safeguard
      sp_index--;
      return_stack[sp_index] = pbGpffffbd60 + 5; // continuation (+5 offset observed; semantics TBD)
      depth_counter--;                           // entering nested block
      pbGpffffbd60 = next;                       // advance past opcode
      FUN_0025c220();                            // relative jump affecting DAT_00355cd0 (execution VM)
    }
    else
    {
      uGpffffbd68 = opcode;
      pbGpffffbd60 = next;
      ((code_fn *)&PTR_LAB_0031e228)[opcode - 0x32]();
    }
  }
}

// NOTE: This is a clarified re-expression; original stack pointer arithmetic used the active stack frame.
// Future refinement: confirm 4 bytes after 0x32 content (length vs offset) and integrate actual continuation math.
