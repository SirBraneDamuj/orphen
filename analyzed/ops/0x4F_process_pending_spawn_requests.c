// Opcode 0x4F — process_pending_spawn_requests (analyzed)
// Original: FUN_0025e7c0
//
// Summary:
// - Iterates a pending spawn/config list at DAT_003556e8 with count DAT_003556e4 (stride 16 bytes per entry).
// - For each entry, selects a descriptor/index based on type bytes at +0x0D/+0x0E:
//     type==0:    base = *(int*)(DAT_00355208*8 + DAT_003551e8); id = (entry[+0x0E]-1) + 0x272
//     type==0x04: base = *(int*)(DAT_003551e8 + 0x78);           id = (entry[+0x0E]-1) + 0x373
//     type==0x05: base = *(int*)(DAT_003551e8 + 0x80);           id = (entry[+0x0E]-1) + 0x474
//   Uses per-entry index (0-based) with 0x2C stride into selected base to check/load and then resolve an entity pointer.
// - Ensures descriptor is loaded (FUN_002661a8) if not flagged, resolves instance via FUN_00265e28(id), and initializes:
//     * FUN_002662e0(entry[0], entry[1], entry[2], entity) — likely sets position/params
//     * entity[+0x2E*2] = FUN_00216690((int8)entry[3] * DAT_00352b90 + DAT_00352b94) — timing/phase
//     * entity[+0x4C*2] = loop index (i)
//     * If entry[+0x0F] is negative: set facing/flip and a cluster of flags; in debug mode (DAT_003555d3) perform an immediate update
// - Calls FUN_0025ba98(entity->something, outFlags) to derive auStack_b0/uStack_a4/uStack_a0/fStack_9c and applies:
//     * entity[+0x2A*2], [+0x8E*2] = uStack_a4; entity[+0x2C*2], [+0x90*2] = uStack_a0
//     * entity[+0x133] = FUN_0030bd20(fStack_9c / DAT_00352b98) — likely volume/attenuation
//     * entity[+0x95..+0x97], [+0x99] from cStack_aa/a9/a8/a7; set bit 0x100 if cStack_a7 != 0
//     * From auStack_b0[0] bits: 0x2000 -> set bit 0x10; 0x4000 -> either copy [0x50] to [0x57] or zero [3];
//       0x8000 -> clear a bit and write derived value via FUN_00227798 to [0x26]; 0x1000 -> set a bit in [2].
// - No VM operands consumed; returns 0.
//
// Notes:
// - This appears to be a batched spawner/initializer for effects or small entities sourced from a prebuilt list.
// - Keep raw FUN_/DAT_ names; we document addresses and behaviors but do not rename unresolved externals.

#include <stdint.h>
#include <stdbool.h>

// Globals
extern int DAT_00355208;       // Index selector for type 0 base lookup
extern int DAT_003551e8;       // Base address for descriptor tables
extern int DAT_003556e4;       // Spawn request count
extern uint32_t *DAT_003556e8; // Spawn request list (stride 0x10 bytes per entry)
extern uint16_t *DAT_00355044; // Current entity pointer
extern char DAT_003555d3;      // Debug mode flag
extern float DAT_00352b90;     // Timing scale factor 1
extern float DAT_00352b94;     // Timing base offset
extern float DAT_00352b98;     // Volume/attenuation divisor

// External functions
extern void FUN_002661a8(int descriptorId);                       // Ensure descriptor loaded
extern long FUN_00265e28(int entityId);                           // Resolve entity instance by ID
extern void FUN_002662e0(int x, int y, int z, long entity);       // Set entity position/params
extern uint32_t FUN_00216690(float value);                        // Convert/scale timing value
extern void FUN_0023fb20(uint16_t *entity);                       // Update entity (debug mode)
extern long FUN_002f0608(uint16_t *entity);                       // Check entity state?
extern void FUN_0025ba98(uint16_t firstWord, uint16_t *outFlags); // Derive entity configuration flags
extern uint8_t FUN_0030bd20(float ratio);                         // Calculate volume/attenuation
extern uint32_t FUN_00227798(uint32_t x, uint32_t y, uint32_t z); // Compute derived value from position

// Analyzed implementation
unsigned int opcode_0x4F_process_pending_spawn_requests(void)
{
  int descriptorBase = *(int *)(DAT_00355208 * 8 + DAT_003551e8);
  uint32_t *spawnEntry = DAT_003556e8;
  int spawnCount = DAT_003556e4;

  if (spawnCount <= 0)
    return 0;

  for (int i = 0; i < spawnCount; i++, spawnEntry += 4) // stride 0x10 bytes (4 uint32s)
  {
    // Read entry fields
    char entryType = *(char *)((uintptr_t)spawnEntry + 0x0D);
    char entryIndex = *(char *)((uintptr_t)spawnEntry + 0x0E);

    // Skip invalid entries
    if (entryType != 0 && entryType != 4 && entryType != 5)
      continue;

    // Calculate descriptor base and entity ID based on type
    int descriptorTableBase;
    int entityId;
    int indexOffset = (entryIndex - 1);

    if (indexOffset < 0)
      continue; // Invalid index

    if (entryType == 4)
    {
      entityId = indexOffset + 0x373;
      descriptorTableBase = *(int *)(DAT_003551e8 + 0x78);
    }
    else if (entryType == 5)
    {
      entityId = indexOffset + 0x474;
      descriptorTableBase = *(int *)(DAT_003551e8 + 0x80);
    }
    else // type == 0
    {
      entityId = indexOffset + 0x272;
      descriptorTableBase = descriptorBase;
    }

    // Locate descriptor entry (stride 0x2C)
    int descriptorEntry = descriptorTableBase + indexOffset * 0x2C;

    // Ensure descriptor is loaded if needed
    if (*(char *)(descriptorEntry + 5) == 0)
    {
      FUN_002661a8(entityId);
    }

    // Resolve entity instance
    long entityPtr = FUN_00265e28(entityId);
    DAT_00355044 = (uint16_t *)entityPtr;

    if (entityPtr == 0)
      continue; // Failed to resolve entity

    uint16_t *entity = DAT_00355044;

    // Set entity position/parameters from entry words 0-2
    FUN_002662e0(spawnEntry[0], spawnEntry[1], spawnEntry[2], entityPtr);

    // Set timing/phase parameter at offset +0x2E (word index)
    char timingParam = *(char *)((uintptr_t)spawnEntry + 6); // entry[3] as int8
    float timingValue = (float)timingParam * DAT_00352b90 + DAT_00352b94;
    *(uint32_t *)(entity + 0x2E) = FUN_00216690(timingValue);

    // Store loop index at offset +0x4C
    *(int *)(entity + 0x4C) = i;

    // Handle facing/flip flag if entry byte +0x0F is negative
    char facingFlag = *(char *)((uintptr_t)spawnEntry + 0x0F);
    if (facingFlag < 0)
    {
      // Set facing direction (negated)
      *(char *)((uintptr_t)entity + 0x95) = -facingFlag;

      // Set flag bits
      entity[4] |= 1;      // Set bit 0 in flags[4]
      entity[2] |= 1;      // Set bit 0 in flags[2]
      entity[1] |= 0x4000; // Set bit 0x4000 in flags[1]

      // In debug mode, perform immediate update
      if (DAT_003555d3 != 0)
      {
        entity[0x95] = 1;
        FUN_0023fb20(entity);
        entity[4] &= 0xFFFE; // Clear bit 0

        long stateCheck = FUN_002f0608(entity);
        if (stateCheck != 0)
          continue; // Skip further processing if state check fails
      }
    }

    // Derive configuration flags and parameters
    uint16_t configFlags[3];
    char configByte1, configByte2, configByte3, configByte4;
    uint32_t param1, param2;
    float floatParam;

    FUN_0025ba98(*entity, configFlags);

    // Apply derived parameters
    *(uint32_t *)(entity + 0x2A) = param1;
    *(uint32_t *)(entity + 0x8E) = param1;
    *(uint32_t *)(entity + 0x2C) = param2;
    *(uint32_t *)(entity + 0x90) = param2;

    // Calculate and store volume/attenuation
    uint8_t volume = FUN_0030bd20(floatParam / DAT_00352b98);
    *(uint8_t *)((uintptr_t)entity + 0x133) = volume;

    // Store configuration bytes
    entity[0x95] = (short)configByte1;
    entity[0x94] = (short)configByte1;
    entity[0x96] = (short)configByte2;
    entity[0x97] = (short)configByte3;
    *(char *)(entity + 0x99) = configByte4;

    if (configByte4 != 0)
    {
      entity[1] |= 0x100; // Set bit 0x100 if configByte4 is non-zero
    }

    // Process configuration flags bits
    if ((configFlags[0] & 0x2000) != 0)
    {
      entity[1] |= 0x10; // Set bit 0x10
    }

    if ((configFlags[0] & 0x4000) == 0)
    {
      entity[0x57] = entity[0x50]; // Copy field 0x50 to 0x57
    }
    else
    {
      entity[3] = 0; // Clear field 3
    }

    if ((configFlags[0] & 0x8000) != 0)
    {
      entity[2] &= 0xFFF7; // Clear bit 0x08

      // Compute and store derived position value
      uint32_t derivedValue = FUN_00227798(
          *(uint32_t *)(entity + 0x10),
          *(uint32_t *)(entity + 0x12),
          *(uint32_t *)(entity + 0x14));
      *(uint32_t *)(entity + 0x26) = derivedValue;
    }

    if ((configFlags[0] & 0x1000) != 0)
    {
      entity[2] |= 1; // Set bit 0
    }
  }

  return 0;
}
