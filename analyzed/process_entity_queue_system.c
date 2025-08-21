// Original function: FUN_0025ce30
// Purpose: Per-frame scheduler for up to 4 timed script events (dialogue/subproc triggers)
//
// What it actually does (grounded in src/FUN_0025ce30.c):
// - Treats four "channels" laid out in three parallel arrays with 12-byte stride per channel:
//     - DAT_00571e40 + 0x0.. : pointer cursor into a compact event stream (advanced by 8 on consume)
//     - DAT_00571e44 + 0x0.. : per-channel timer accumulator (u32)
//     - DAT_00571e48 + 0x0.. : per-channel consumed-entry counter (u32)
// - For each channel, reads event header at pointer[-1] (two u16 values + one u32):
//     - u16 limit = *(u16*)ptr           (timer limit, compared against (timer >> 5))
//     - u16 flags = *(u16*)(ptr + 2)     (gate/condition bits)
//     - u32 offset = *(u32*)(ptr + 4)    (script-relative target offset)
// - Gating rules:
//     - flags == 0            → proceed normally
//     - (flags & 0x8000) == 0 → require FUN_00266368(flags) != 0 to proceed
//     - else if (flags & (uGpffffb0f4 | 0x8000)) == flags → proceed (mask subset satisfied)
// - Timer handling: increment timer by iGpffffb64c until (timer >> 5) < limit fails, then consume entry.
// - On consume:
//     - If offset ∈ [uGpffffbd70, uGpffffbd74): call FUN_00237b38(iGpffffb0e8 + offset) directly
//       (starts the target immediately; used for items in an allowed offset window, e.g., dialogue pages).
//     - Else: allocate a runtime slot via FUN_00261de0() and enqueue the pointer into the subproc slot table:
//         *(iGpffffbd84 + 4*slot) = iGpffffb0e8 + offset.
// - Then advance channel cursor (DAT_00571e40 += 8), increment count (DAT_00571e48 += 1),
//   clear timer, and null the cursor if next entry is terminal (cursor[+1] == 0).
//
// Notes:
// - This is a timed event scheduler for script/subproc work. Earlier wording about "audio/graphics"
//   was misleading; there are no audio/graphics side-effects here beyond kicking script/dialogue work.
// - iGpffffb0e8 is the base for turning offsets into absolute pointers; iGpffffbd84 is the runtime
//   subproc slot table (0x40 entries). The [uGpffffbd70,uGpffffbd74) window selects which targets are
//   executed immediately vs pushed to the slot queue.

#include "orphen_globals.h"

// External globals (DAT_* variables not yet analyzed)
extern uint32_t DAT_00571e44;
extern uint32_t DAT_00571e40;
extern uint32_t DAT_00571e48;
extern int32_t iGpffffb64c;
extern uint32_t uGpffffbd70;
extern uint32_t uGpffffbd74;
extern int32_t iGpffffbd84;
extern int32_t iGpffffb0e8;
extern uint16_t uGpffffb0f4;

// External functions (FUN_* functions not yet analyzed)
extern int FUN_00261de0(void);                 // Allocate a free runtime slot index (subproc queue)
extern void FUN_00237b38(long param_1);        // Start/dispatch a script/dialogue target at pointer
extern long FUN_00266368(uint16_t flag_value); // Flag/condition query used to gate scheduler entries

void process_entity_queue_system(void)
{
  uint16_t entity_flags;
  uint16_t *entity_ptr;
  uint32_t timer_value;
  int allocation_index;
  long flag_check_result;
  int *entity_data_ptr;
  int struct_offset;
  int entity_index;
  uint32_t *timer_ptr;

  // Start with first channel's timer at DAT_00571e44
  timer_ptr = (uint32_t *)&DAT_00571e44;
  entity_index = 0;
  struct_offset = 0;

  do
  {
    // Fetch event header pointer from previous word (puVar9[-1])
    // Event layout at that pointer: [u16 limit][u16 flags][u32 offset]
    entity_ptr = (uint16_t *)timer_ptr[-1];

    if (entity_ptr == (uint16_t *)0x0)
      goto advance_to_next_entity;

    entity_flags = entity_ptr[1]; // Flags at +2 bytes

    if (entity_flags == 0)
    {
    entity_timer_update:
      timer_value = *timer_ptr;

    entity_state_check:
      // Check timer gate: (timer >> 5) < limit
      if ((timer_value >> 5 & 0xffff) < (uint32_t)*entity_ptr)
      {
        // Increment timer by per-frame tick
        *timer_ptr = timer_value + iGpffffb64c;
      }
      else
      {
        // Timer expired — consume entry
        timer_value = *(uint32_t *)(entity_ptr + 2); // offset

        // Direct-dispatch window vs queued subproc
        if ((timer_value < uGpffffbd70) || (uGpffffbd74 <= timer_value))
        {
          // Outside window → enqueue into runtime subproc slot table
          allocation_index = FUN_00261de0(); // Get free slot index
          *(int *)(allocation_index * 4 + iGpffffbd84) = *(int *)(entity_ptr + 2) + iGpffffb0e8;
        }
        else
        {
          // Inside window → direct start/dispatch
          FUN_00237b38(timer_value + iGpffffb0e8);
        }

        // Advance channel cursor and counters for DAT_00571e40/44/48
        entity_data_ptr = (int *)((int)&DAT_00571e40 + struct_offset);
        *entity_data_ptr = *entity_data_ptr + 8; // Advance pointer by 8 bytes

        // Increment counter at DAT_00571e48 + offset
        *(int *)(&DAT_00571e48 + struct_offset) = *(int *)(&DAT_00571e48 + struct_offset) + 1;

        // If next entry indicates end (cursor[+1] == 0), clear cursor
        if (*(int *)(*entity_data_ptr + 4) == 0)
        {
          *entity_data_ptr = 0; // Reset pointer
        }

        // Clear timer value for this channel
        *(uint32_t *)(&DAT_00571e44 + struct_offset) = 0;
      }
    }
    else if ((entity_flags & 0x8000) == 0)
    {
      // flags without 0x8000: require external flag/condition to be true
      flag_check_result = FUN_00266368(entity_ptr[1]);
      if (flag_check_result != 0)
        goto entity_timer_update;
    }
    else if ((entity_flags & (uGpffffb0f4 | 0x8000)) == entity_flags)
    {
      // 0x8000 with subset of mask uGpffffb0f4: allow
      timer_value = *timer_ptr;
      goto entity_state_check;
    }

  advance_to_next_entity:
    entity_index = entity_index + 1;
    struct_offset = struct_offset + 0xc; // 12-byte stride between channels
    timer_ptr = timer_ptr + 3;           // 3 uint entries per channel

    if (3 < entity_index)
    { // Process maximum of 4 entities (0-3)
      return;
    }
  } while (true);
}
