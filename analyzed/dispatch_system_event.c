// Analyzed re-expression of FUN_0025d1c0
// Original signature: void FUN_0025d1c0(long param_1, ushort param_2, int param_3)
//
// Purpose (inferred):
// - Initialize one of two small control blocks (globals) and dispatch an event/command
//   via FUN_0025d0e0 with a value that may be tagged in the high byte (0xFF000000)
//   depending on which control block is selected.
//
// Behavior summary (from src/FUN_0025d1c0.c):
// - If which_buffer == 0:
//     DAT_00571dc0 = 0x1fe0; use &DAT_00571dc0 as the base control block.
//   Else:
//     DAT_00571dd0 = 0;     use &DAT_00571dd0 as the base control block.
// - Set fields relative to the chosen base (treated as a ushort*):
//     base[5] = 0x00a0;             // offset +0x0A
//     base[1] = event_code;         // offset +0x02
//     *(int*)(base + 2) = payload;  // offset +0x04
// - Compute a tagged value and call FUN_0025d0e0(tagged_payload, 1):
//     tagged_payload = payload + (((uint)(*base) << 16) >> 21) * 0x1000000
//   Notably, with *base==0x1fe0 this produces (0x1fe0 >> 5) == 0xff, thus 0xFF000000.
//   With *base==0 it produces 0.
// - Finally, base[4] = 0; // offset +0x08 cleared.
//
// Parameters (renamed):
// - which_buffer  (param_1): selects control block A (0) vs B (non-zero).
// - event_code    (param_2): stored at +0x02 within the selected block.
// - payload       (param_3): stored at +0x04 and used in the dispatch value.
//
// Side effects:
// - Writes to one of two global control words (DAT_00571dc0 or DAT_00571dd0) and adjacent fields.
// - Invokes FUN_0025d0e0(tagged_payload, 1), likely kicking a subsystem (exact role TBD).
//
// Notes:
// - The 0x1fe0 constant yields a 0xFF tag when shifted as in the code, so the dispatch value is
//   payload | 0xFF000000 when which_buffer==0, otherwise just payload.
// - We leave naming of FUN_0025d0e0 and the DAT_* globals unchanged until those are analyzed.
// - Call sites (e.g., FUN_0025d380) pass which_buffer=0, a small event_code (e.g., 0x0C), and payload=0.
//   That results in FUN_0025d0e0(0xFF000000, 1).

#include <stdint.h>

// Unresolved externs preserved
// Original callee: FUN_0025d0e0(value, mode)
void build_and_submit_view_rect_packet(unsigned int payload_word, int use_alt_mode);
extern unsigned short DAT_00571dc0; // control block A base (16-bit)
extern unsigned short DAT_00571dd0; // control block B base (16-bit)

void dispatch_system_event(long which_buffer, unsigned short event_code, int payload)
{
  unsigned short *base;

  if (which_buffer == 0)
  {
    DAT_00571dc0 = 0x1fe0;
    base = &DAT_00571dc0;
  }
  else
  {
    DAT_00571dd0 = 0;
    base = &DAT_00571dd0;
  }

  // Configure control block fields
  base[5] = 0x00a0;             // +0x0A
  base[1] = event_code;         // +0x02
  *(int *)(base + 2) = payload; // +0x04

  // Compute tag from *base: (((uint)*base << 16) >> 21) yields (*base >> 5) for non-negative values.
  // For 0x1fe0 this is 0xff; for 0 it’s 0.
  int hi_tag = (int)(((uint32_t)(*base) << 16) >> 21); // effectively (*base >> 5)
  int tagged = payload + hi_tag * 0x1000000;           // payload | (hi_tag << 24)

  // Was: FUN_0025d0e0(tagged, 1)
  build_and_submit_view_rect_packet((unsigned int)tagged, 1);

  base[4] = 0; // +0x08 clear
}
