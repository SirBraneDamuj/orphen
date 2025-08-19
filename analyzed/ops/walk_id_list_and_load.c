/*
 * Walk 0-terminated 16-bit ID list and load models/resources
 * Original: FUN_002661f8
 *
 * Summary
 * - Iterates through a list of int16 entries until a 0 terminator.
 * - For each entry:
 *   - If negative, skip.
 *   - If non-negative, dispatch to FUN_002661a8(id) which resolves and ensures the model/resource is loaded.
 */

#include <stdint.h>

extern void *FUN_002661a8(int16_t id);

// NOTE: Original signature: void FUN_002661f8(short *param_1)
void walk_id_list_and_load(int16_t *list)
{
  while (1)
  {
    int16_t v = *list++;
    if (v == 0)
    {
      return;
    }
    if (v >= 0)
    {
      (void)FUN_002661a8(v);
    }
  }
}
