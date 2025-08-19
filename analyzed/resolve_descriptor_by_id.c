/*
 * Resolve descriptor by 16-bit ID (parameter-validated)
 * Original: FUN_00229be8
 *
 * Summary
 * - Validates the ID: disallow 0x38 and any value > 0x574; on violation, calls FUN_0026bfc0
 *   with message at 0x34c038 ("ER_PARAM [get_pchr]").
 * - Calls FUN_00229980(0, id, out[4]) and returns out[0] as the resolved pointer.
 * - Caller typically treats the returned value as a descriptor pointer (or NULL if not found).
 *
 * Notes
 * - Used by load_model path (see FUN_002661a8 wrapper), error string suggests original name "get_pchr".
 * - We keep unresolved FUN_ names and global addresses for traceability.
 */

#include <stdint.h>

// Unresolved externs (original names)
extern void FUN_00229980(int, int16_t, uint32_t out4[4]);
extern void FUN_0026bfc0(uint32_t msg_addr, ...);

// strings.json: 0x0034c038 => "ER_PARAM [get_pchr]"
#define STR_ER_PARAM_GET_PCHR 0x0034c038u

// NOTE: Original signature: undefined4 FUN_00229be8(short param_1, short param_2)
void *resolve_descriptor_by_id(int16_t id)
{
  // Parameter check mirrors decompiled logic
  if (id == 0x38 || id > 0x574)
  {
    FUN_0026bfc0(STR_ER_PARAM_GET_PCHR, id);
  }

  uint32_t out[4] = {0, 0, 0, 0};
  FUN_00229980(0, id, out);
  return (void *)(uintptr_t)out[0];
}
