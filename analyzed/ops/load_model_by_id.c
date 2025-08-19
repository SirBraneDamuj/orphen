/*
 * Load/ensure model by 16-bit ID
 * Original: FUN_002661a8
 *
 * Summary
 * - Resolves a model/resource descriptor pointer by a 16-bit ID using FUN_00229be8.
 * - If resolution fails (returns NULL), triggers an error with message at 0x34d470 ("ER_BADNO [load_model]") via FUN_0026bfc0.
 * - If resolution succeeds, calls FUN_00266118(descriptor) to register/initialize or ensure it is loaded.
 * - Returns the resolved descriptor pointer.
 *
 * Notes
 * - Keep original FUN_/DAT_ names to preserve traceability; callees remain by their raw labels until analyzed.
 * - See also: opcode 0x4D list processing which feeds IDs into this path via FUN_002661f8 -> FUN_002661a8.
 * - strings.json @ 0x0034d470: "ER_BADNO [load_model]" (used for the failure path).
 */

#include <stdint.h>
#include <stddef.h>

// Externs (use analyzed wrappers where available)
extern void *resolve_descriptor_by_id(int16_t id); // analyzed wrapper for FUN_00229be8
extern void FUN_0026bfc0(uint32_t msg_addr, ...);
extern void ensure_descriptor_loaded(void *descriptor); // analyzed wrapper for FUN_00266118

// Address of error string used on failure (from strings.json)
#define STR_ER_BADNO_LOAD_MODEL 0x0034d470u

// NOTE: Original signature: long FUN_002661a8(undefined2 param_1)
void *load_model_by_id(int16_t model_id)
{
  void *descriptor = resolve_descriptor_by_id(model_id);
  if (descriptor == NULL)
  {
    // Error: bad/unknown model ID
    FUN_0026bfc0(STR_ER_BADNO_LOAD_MODEL);
    return NULL; // Unreachable if FUN_0026bfc0 does not return
  }

  // Ensure model is registered/loaded
  ensure_descriptor_loaded(descriptor);
  return descriptor;
}
