// Opcode 0x3D–0x40 — modify_flag_state
// Original handler: FUN_0025e560 (src/FUN_0025e560.c)
//
// Summary
//   Reads one VM argument (a flag index) and performs a flag operation depending
//   on the opcode byte that invoked this handler. All four opcodes (0x3D, 0x3E, 0x3F, 0x40)
//   map to the same function but choose among: set, clear, toggle, or query via
//   the preceding opcode byte ('>', '@', '?'). Returns non-zero if the queried flag is set.
//
// Details
//   - The handler peeks the previous byte at (IP-1) to distinguish variants:
//       '>' (0x3E): set_flag_state(index)
//       '?' (0x3F): clear_flag_state(index)
//       '@' (0x40): toggle_flag_state(index)
//       default (0x3D): pure query
//   - It calls get_flag_state(index) first and returns whether it was non-zero.
//   - When debug is enabled (DAT_003555d3 != 0) and opcode in { '>', '?', '@' } and
//     index < 800, it prints a formatted debug string at 0x34ce70 (not yet named).
//
// Original signature
//   bool FUN_0025e560(void)
//
// Notes
//   - We keep the debug print as an extern.
//   - Bounds: the flag helpers already clamp at 0x900 bytes (18432 bits).

#include <stdint.h>
#include <stdbool.h>

// VM core
extern uint8_t *DAT_00355cd0;                // interpreter IP
extern void bytecode_interpreter(void *out); // analyzed helper

// Flag helpers (analyzed wrappers)
extern unsigned int get_flag_state(unsigned int flag_index);  // FUN_00266368
extern void set_global_event_flag(uint32_t bit_index);        // FUN_002663a0
extern void clear_flag_state(uint32_t bit_index);             // FUN_002663d8 (wrapper exists)
extern uint32_t toggle_global_event_flag(uint32_t bit_index); // FUN_00266418

// Debug globals
extern unsigned char DAT_003555d3; // debug toggle
extern void FUN_0026bfc0(uint32_t fmt_addr);

bool opcode_0x3E_modify_flag_state(void)
{
  // Read the previous opcode byte to discriminate variant
  uint8_t prev = *(uint8_t *)(DAT_00355cd0 - 1);

  // Fetch one VM argument: flag index
  uint32_t arg;
  bytecode_interpreter(&arg);

  // Query current state first; original returns (state != 0)
  bool was_set = get_flag_state(arg) != 0;

  // Optional debug spew for small indices
  if (DAT_003555d3 != 0 && (prev == '>' || prev == '@' || prev == '?') && arg < 800)
  {
    FUN_0026bfc0(0x34ce70);
  }

  // Perform side effect depending on opcode variant
  if (prev == '>')
  {
    set_global_event_flag(arg);
  }
  else if (prev == '?')
  {
    clear_flag_state(arg);
  }
  else if (prev == '@')
  {
    (void)toggle_global_event_flag(arg);
  }

  return was_set;
}
