// analyzed/ops/0x50_initialize_selected_entity_with_type.c
// Original: FUN_0025eaf0
// Opcode: 0x50
// Handler: Initialize selected entity with specified type

// Behavior:
// - Saves current entity pointer (uGpffffb0d4) as fallback.
// - Evaluates 1 expression (entity selector).
// - Reads 1 immediate byte from stream (entity type ID).
// - Selects entity via FUN_0025d6c0 (selector, saved pointer).
// - Initializes the selected entity via FUN_00229c40(uGpffffb0d4, type_id).
// - Returns 0 (standard opcode completion).

// Related:
// - FUN_0025d6c0: select_current_object_frame (analyzed/select_current_object_frame.c)
//   - If selector != 0x100: Sets DAT_00355044 = &DAT_0058beb0 + (selector * 0xEC)
//   - If selector == 0x100: Sets DAT_00355044 = fallback pointer
// - FUN_00229c40: Entity initialization function (takes entity pointer + type ID)
//   - Calls FUN_00229980 to validate/setup entity descriptor
//   - Calls FUN_00267e78 to clear entity memory (0x1D8 bytes)
//   - Loads type-specific configuration from descriptor table
//   - Initializes entity state based on type ID
// - FUN_0025c1d0: Read immediate byte from stream, advance pointer
// - uGpffffb0d4: Current selected entity pointer (alias for DAT_00355044)

// Usage Context:
// - Similar to opcode 0x52 (spawn_entity_by_type) but operates on already-selected entity.
// - Opcode 0x52: Allocates new slot then initializes (find free + init).
// - Opcode 0x50: Initializes existing/selected slot (select + init).
// - Type IDs range from 0x00 to ~0x100, each mapped to specific entity behavior.
// - Common types: 0x49 (battle logo), 0x4C (particles), 0x55 (special marker), etc.

// PS2 Architecture:
// - Entity pool at DAT_0058beb0, stride 0xEC (236 bytes per entity).
// - Maximum ~246 entities (0xF6 slots based on related opcodes).
// - Type ID determines entity vtable, update function, and behavior flags.
// - Initialization clears state to prevent stale data from previous use.

#include <stdint.h>

// External declarations
typedef void (*bytecode_evaluator_t)(void *);
extern bytecode_evaluator_t FUN_0025c258; // Bytecode expression evaluator
extern uint32_t FUN_0025c1d0(void);       // Read immediate byte from stream

// Entity selection: sets DAT_00355044 based on selector or fallback pointer
extern void FUN_0025d6c0(uint32_t selector, uint32_t fallback_ptr);

// Entity initialization: configures entity with type-specific behavior
extern void FUN_00229c40(uint8_t *entity_ptr, uint32_t type_id);

// Globals
extern uint32_t uGpffffb0d4; // Current selected entity pointer (alias for DAT_00355044)

// Opcode 0x50: Initialize selected entity with type
uint64_t opcode_0x50_initialize_selected_entity_with_type(void)
{
  uint32_t saved_entity_ptr;
  uint32_t selector;
  uint32_t type_id;
  uint32_t vm_result[4];

  // Save current entity pointer as fallback for selection
  saved_entity_ptr = uGpffffb0d4;

  // Evaluate entity selector expression
  FUN_0025c258(vm_result);
  selector = vm_result[0];

  // Read type ID from immediate byte in stream
  type_id = FUN_0025c1d0();

  // Select entity (by index if selector < 0x100, else use saved pointer)
  FUN_0025d6c0(selector, saved_entity_ptr);

  // Initialize selected entity with specified type
  FUN_00229c40((uint8_t *)uGpffffb0d4, type_id);

  return 0;
}

/*
 * Function Call Hierarchy:
 *
 * opcode_0x50_initialize_selected_entity_with_type()
 *   ├─> uVar1 = uGpffffb0d4                  [Save current entity pointer]
 *   ├─> FUN_0025c258(&selector)              [Eval entity selector expr]
 *   ├─> type_id = FUN_0025c1d0()             [Read immediate type ID byte]
 *   ├─> FUN_0025d6c0(selector, uVar1)        [Select entity by index or fallback]
 *   └─> FUN_00229c40(uGpffffb0d4, type_id)   [Initialize entity with type]
 *         ├─> FUN_00229980(entity, type_id)  [Validate/setup descriptor]
 *         ├─> FUN_00267e78(entity, 0x1D8)    [Clear entity memory]
 *         └─> [Type-specific initialization] [Load config, set vtable, etc.]
 *
 * Entity Selection Logic (FUN_0025d6c0):
 * - selector < 0x100:
 *     DAT_00355044 = &DAT_0058beb0 + (selector * 0xEC)
 *     Select entity by absolute pool index
 * - selector == 0x100:
 *     DAT_00355044 = fallback_ptr (saved_entity_ptr)
 *     Keep current selection unchanged
 *
 * Entity Initialization (FUN_00229c40):
 * 1. Validate type ID via descriptor lookup (FUN_00229980)
 * 2. Clear entity memory to reset state (FUN_00267e78, 0x1D8 bytes)
 * 3. Load type-specific configuration from descriptor table
 * 4. Set entity vtable/function pointers based on type
 * 5. Initialize type-specific state fields
 * 6. Mark entity as active in status array
 *
 * Comparison with Related Opcodes:
 * - 0x50: initialize_selected_entity_with_type
 *     Usage: Reinitialize existing entity or initialize pre-selected slot
 *     Steps: eval selector → read type → select entity → initialize
 *
 * - 0x52: spawn_entity_by_type
 *     Usage: Allocate new entity slot and initialize
 *     Steps: eval type → find free slot → initialize
 *     Difference: 0x52 finds free slot automatically, 0x50 uses explicit selection
 *
 * - 0x58: select_pw_slot_by_index
 *     Usage: Select entity without initialization
 *     Steps: eval selector → select entity
 *     Difference: Selection only, no initialization
 *
 * - 0x51: set_pw_all_dispatch
 *     Usage: Batch spawn multiple entities from table
 *     Steps: iterate table → allocate slot → initialize each
 *     Difference: Batch operation using lookup table from 0x4E
 *
 * Typical Script Sequences:
 * ```
 * # Sequence 1: Reinitialize existing entity
 * 0x58 [index]           # Select entity by index
 * 0x50 0x100 [type]      # Initialize with new type (0x100 = keep selection)
 *
 * # Sequence 2: Initialize specific slot
 * 0x50 [index] [type]    # Select and initialize in one step
 *
 * # Sequence 3: Conditional initialization
 * 0x5A [search_id]       # Search for entity by tag (sets DAT_00355044)
 * 0x14 <skip_if_not_found>
 * 0x50 0x100 [type]      # Initialize found entity with new type
 * ```
 *
 * Entity Type ID Examples (from FUN_00229c40 usage):
 * - 0x19: Menu/UI entity (src/FUN_00251e40.c)
 * - 0x48: Camera entity (src/FUN_00271220.c)
 * - 0x49: Battle logo entity (src/FUN_0025d5b8.c)
 * - 0x4C: Particle system (src/FUN_002338f0.c)
 * - 0x55: Special marker (used by opcode 0x52 spawn check)
 * - 0x58: Unknown type (src/FUN_00271220.c)
 * - 0x68: Unknown type (src/FUN_0022a418.c)
 * - 0x6A: Unknown type (src/FUN_0022a418.c)
 * - 0x256: Unknown type (src/FUN_00213ef0.c)
 *
 * Entity Descriptor Structure (from FUN_00229c40):
 * - Stored at aiStack_80[0] after FUN_00229980 call
 * - +0x04: Entity flags/attributes (ushort)
 * - +0x05: Active/valid flag (char, checked for '\0')
 * - Type ID indexes into descriptor table
 * - Descriptor contains vtable pointers, size, behavior flags
 *
 * Memory Layout:
 * - Entity pool base: DAT_0058beb0
 * - Entity stride: 0xEC (236 bytes)
 * - Entity structure (common fields):
 *   +0x00: Type ID / Entity ID
 *   +0x04: Status flags
 *   +0x20-0x2B: Position XYZ (3 floats)
 *   +0x4C: Tag/index (used by 0x5A for lookup)
 *   +0x192: Bone index (used by 0x64)
 *   +0x194: Bone flags (used by 0x64)
 *   ...
 *   +0x1D8: Total cleared size (from FUN_00267e78)
 *
 * Error Handling:
 * - FUN_00229c40 calls FUN_0026bfc0(0x34c050, type_id) if descriptor invalid
 * - Error message likely: "Invalid entity type" or "Descriptor not found"
 * - No bounds checking on selector index (can overflow pool)
 *
 * Performance Notes:
 * - Entity initialization is expensive (descriptor lookup + memory clear + config load)
 * - Prefer batch operations (0x51) when spawning multiple entities
 * - Reinitialization (0x50 with 0x100) faster than spawn (0x52) - no slot search
 * - Type ID lookup likely uses hash table or binary search in descriptor array
 *
 * State Transitions:
 * Before: Entity slot in any state (empty, active, stale)
 * After: Entity initialized with type-specific behavior, ready for updates
 *
 * Side Effects:
 * - Modifies DAT_00355044 (current selected entity pointer)
 * - Clears entity memory (0x1D8 bytes at entity base)
 * - Updates entity status array (marks slot as active)
 * - May allocate resources (textures, audio, animation state)
 * - Type-specific initialization may modify global state
 *
 * Thread Safety:
 * - Not thread-safe (modifies global state)
 * - Single-threaded VM execution assumed
 * - Entity pool shared across all scripts
 *
 * Debug/Development:
 * - Error message at 0x34c050 indicates debug build support
 * - Type ID validation helps catch script bugs
 * - Descriptor system allows hot-reloading entity types
 *
 * Related Global State:
 * - uGpffffb0d4 / DAT_00355044: Current selected entity pointer
 * - DAT_0058beb0: Entity pool base (246 slots * 0xEC bytes)
 * - DAT_00355cd0: Bytecode stream pointer (advanced by FUN_0025c1d0)
 * - Entity status array: Parallel array tracking active/free slots
 *
 * Cross-References:
 * - analyzed/ops/0x52_spawn_entity_by_type.c: Spawn new entity (find slot + init)
 * - analyzed/ops/0x58_select_pw_slot_by_index.c: Select entity by index
 * - analyzed/ops/0x5A_select_pw_by_index.c: Select entity by tag
 * - analyzed/ops/0x5C_destroy_entity_by_index.c: Destroy entity (inverse operation)
 * - analyzed/ops/0x51_set_pw_all_dispatch.c: Batch spawn from table
 * - analyzed/select_current_object_frame.c: Entity selection implementation
 * - src/FUN_00229c40.c: Entity initialization implementation (219 lines)
 * - src/FUN_00229980.c: Descriptor validation/lookup
 * - src/FUN_00267e78.c: Memory clear utility
 *
 * Additional Notes:
 * - The name "pw" in related opcodes suggests "pawn" or "player world" entity
 * - Entity pool is fixed-size (no dynamic allocation)
 * - Type-based polymorphism via vtable/function pointers
 * - Initialization cost amortized over entity lifetime
 * - Common pattern: select → initialize → configure → activate
 */
