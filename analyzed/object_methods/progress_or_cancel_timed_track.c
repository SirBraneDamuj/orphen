// Object method 0x72 — progress_or_cancel_timed_track (analyzed)
// Original: FUN_002445c8
// Invoked via object_system_method_dispatcher case 0x72
//
// Summary:
// - Operates on the global timed track list (DAT_00354fa8). Looks up a track by id and either:
//   - Return progress percent (1..1001) when (flags & 1) == 0, computed from [total,current] duration fields.
//   - Cancel/stop the track when (flags & 1) != 0 by zeroing its first field (*puVar4 = 0).
// - Returns -1 if the track system is not initialized; returns 0 when the id is not found.
//
// Parameters:
// - id (int): Track identifier; compared to *(int*)(track+0x20) in the active list.
// - flags (u64): If bit0 clear, query progress; if bit0 set, cancel.
//
// Data layout (from raw):
// - DAT_00354fa8 points to a struct with count at +0x54 and base at +0x58.
// - Each track appears to be 0x2D8 bytes with key fields:
//   - +0x00: u16 state (zeroing this cancels)
//   - +0x04: u16 total (puVar4[2])
//   - +0x06: u16 current (puVar4[3])
//   - +0x20: int id
//
// Return:
// - If querying: int progress = ((total - current) * 1000) / total + 1.
// - If canceling: 0 (after attempting cancel).
// - -1 if system not available; 0 if id not found.

#include <stdint.h>

// Globals
extern int DAT_00354fa8; // track system root (0 if off)

// Original signature: int FUN_002445c8(int id, undefined8 unused, ulong flags)
int progress_or_cancel_timed_track(int id, unsigned long flags)
{
  if (DAT_00354fa8 == 0)
    return -1;

  uint32_t count = *(uint32_t *)(DAT_00354fa8 + 0x54);
  uint32_t idx = 0;
  uint16_t *track = *(uint16_t **)(DAT_00354fa8 + 0x58);

  while (idx < count)
  {
    if (*(int *)(track + 8) == id)
    {
      if ((flags & 1ULL) == 0)
      {
        uint16_t total = track[2];
        if (total == 0)
        {
          __builtin_trap();
        }
        // ((total - current) * 1000) / total + 1
        int current = (int)(uint16_t)track[3];
        return (int)(((int)total - current) * 1000) / (int)total + 1;
      }
      // cancel
      *track = 0;
      return 0;
    }
    idx = (idx + 1) & 0xFFFF;
    track += 0x16c; // 0x2D8 bytes / 2
  }
  return 0;
}
