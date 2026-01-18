// Opcode 0x66 — convert_entity_to_party_member (set_pw_tman2)
// Original: FUN_0025f950
//
// Summary:
// - Reads an entity selector value and team/party slot index from the script VM
// - Validates the index (must be <= 0xFFFF, error: "address error [set_pw_tman2]")
// - Selects the target entity via select_current_object_frame
// - Sets entity flag bit 0x4000 at offset +0x002 (psGpffffb0d4[1])
// - If entity type is 0x37, formats and outputs debug message with party formation parameters
// - Stores the party slot index at entity offset +0x130 (psGpffffb0d4[0x98])
// - Preserves original entity type in a backup field if type != 0x38
// - Converts entity to type 0x38 (party member/team character)
// - Clears multiple entity state fields (positions, counters, flags)
// - Returns 0
//
// Inferred semantics:
// - "set_pw_tman2" = "set party with team manager 2" (from error strings)
// - Opcode used to transform an existing entity (typically type 0x37) into a controllable party member (type 0x38)
// - Type 0x37: Pre-party entity (follower/companion not yet in active party)
// - Type 0x38: Active party member entity (fully integrated into formation)
// - Debug output params at 0x34cef8: "CAN'T SET PARTY [set_pw_tman2(%d/%d/%d/%d)]"
//   likely relate to formation position (uGpffffb280, uGpffffb284, uGpffffb0cc)
//
// Side effects:
// - Modifies current entity pointer (DAT_00355044) via select_current_object_frame
// - Outputs debug message if entity type is 0x37
// - Writes to entity structure at multiple offsets:
//   +0x002 (flags), +0x000 (type), +0x130 (slot), +0x1CE (type backup),
//   +0x1BC-0x1CC (cleared state fields)
//
// PS2-specific notes:
// - The 0xEC stride for entity pool is consistent across all analyzed entity opcodes
// - Flag 0x4000 at offset +2 likely indicates "party management active" or "team member slot assigned"
// - Entity stride from DAT_0058beb0: Each entity is 0xEC bytes (236 bytes)
// - Type conversion 0x37→0x38 mirrors similar pattern in spawn request opcode (0x4F)

#include <stdint.h>
#include <stdbool.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Read immediate value from script stream (aligned 32-bit read)
// Returns value at DAT_00355cd0 (with alignment handling)
extern uint32_t FUN_0025c1d0(void);

// Select current entity frame by index or direct pointer
// selector==0x100: use fallbackPtr directly; else: (&DAT_0058beb0) + selector * 0xEC
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);

// Format string to buffer using sprintf-style formatter
// Analyzed as sprintf_variadic in several files
extern void FUN_0030c1d8(char *buffer, uint32_t format_addr, ...);

// Error/abort handler with string address
extern void FUN_0026bfc0(uint32_t string_addr);

// Globals
extern uint16_t *psGpffffb0d4; // Current entity pointer (short* view)
extern uint32_t uGpffffb280;   // Formation/party parameter 1
extern uint32_t uGpffffb284;   // Formation/party parameter 2
extern uint32_t uGpffffb0cc;   // Formation/party parameter 3

// Analyzed implementation
unsigned long opcode_0x66_convert_entity_to_party_member(void)
{
  uint16_t *entity;
  uint32_t partySlotIndex;
  uint8_t tempBuffer[256];
  uint32_t vmResult[4];

  // Save current entity pointer before selection
  entity = psGpffffb0d4;

  // Read entity selector from VM expression
  bytecode_interpreter(vmResult);

  // Read party slot index from script stream (aligned read)
  partySlotIndex = FUN_0025c1d0();

  // Validate party slot index (must fit in 16-bit range)
  if (partySlotIndex > 0xFFFF)
  {
    // "address error [set_pw_tman2]"
    FUN_0026bfc0(0x34ced8);
  }

  // Select target entity (vmResult[0] as selector, entity as fallback ptr)
  select_current_object_frame(vmResult[0], entity);

  // Set party management flag bit 0x4000 in entity flags at +0x002
  psGpffffb0d4[1] = psGpffffb0d4[1] | 0x4000;

  // If entity is type 0x37 (pre-party companion), output debug diagnostic
  if (*psGpffffb0d4 == 0x37)
  {
    // Format: "CAN'T SET PARTY [set_pw_tman2(%d/%d/%d/%d)]"
    // Parameters: entity slot (+0x1A0), and three formation globals
    FUN_0030c1d8(
        tempBuffer,
        0x34cef8,
        psGpffffb0d4[0xd0], // entity field at short offset 0xD0 (+0x1A0 bytes)
        uGpffffb280,        // formation param 1
        uGpffffb284,        // formation param 2
        uGpffffb0cc);       // formation param 3

    // Abort with formatted error message
    FUN_0026bfc0((uint32_t)tempBuffer);
  }

  // Store party slot index at entity offset +0x130 (short index 0x98)
  psGpffffb0d4[0x98] = (int16_t)partySlotIndex;

  // Preserve original entity type in backup field if not already type 0x38
  if (*psGpffffb0d4 != 0x38)
  {
    psGpffffb0d4[0xe7] = *psGpffffb0d4; // Backup at short offset 0xE7 (+0x1CE bytes)
  }

  // Clear state fields (position/counter block at +0x1BC through +0x1CC)
  *(uint8_t *)(psGpffffb0d4 + 0xde) = 0; // byte at +0x1BC
  *(uint8_t *)(psGpffffb0d4 + 0xe6) = 0; // byte at +0x1CC
  psGpffffb0d4[0xe0] = 0;                // short at +0x1C0
  psGpffffb0d4[0xdf] = 0;                // short at +0x1BE
  psGpffffb0d4[0xe2] = 0;                // short at +0x1C4
  psGpffffb0d4[0xe3] = 0;                // short at +0x1C6

  // Convert entity to type 0x38 (active party member)
  *psGpffffb0d4 = 0x38;

  // Clear additional state fields
  psGpffffb0d4[0xe4] = 0; // short at +0x1C8
  psGpffffb0d4[0xe5] = 0; // short at +0x1CA

  return 0;
}

// Original signature preserved for cross-reference
// undefined8 FUN_0025f950(void)
