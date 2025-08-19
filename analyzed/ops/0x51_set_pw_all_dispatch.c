// Opcode 0x51 — "set_pw_all" group dispatcher (inferred)
// Original: FUN_0025eb48 (src/FUN_0025eb48.c)
//
// Summary
// - Reads one mode byte from a secondary parameter stream (pbGpffffbd60), then walks a table of
//   precomputed entries at iGpffffb778 (count iGpffffb774, stride 0x10). For each entry whose
//   type byte (entry+0xD) matches the mode, it spawns/configures an object/effect using the
//   first three 32-bit words of the entry and a time/scale derived from the 4th word's low byte.
// - Two subpaths:
//   - mode == 3: Enforces entry[0xE] <= 0x7F (else trap with "tbox param error [set_pw_all]").
//     Allocates via FUN_00265e28(0x3A), seeds fields (index into +0x4C, low ID into +0x98,
//     and a code at +0xCC = entry[0x0F] + 0x400), and calls FUN_00266240(..., lastArg=0).
//   - mode != 3: Scans a small table at DAT_00571d00 (iGpffffb0dc entries, stride 0xC) for an
//     entry whose first int equals entry[0x0E] and whose second int != 0x55. If found, allocates
//     (FUN_00265e28(0)), calls FUN_00266240 with lastArg = *(byte*)(0x571d08 + idx*0xC), sets
//     index (+0x4C), then computes a per-mode parameter block via FUN_0025bae8 and applies it
//     with FUN_0023a518. For mode == 2 with cGpffffb663 != 0, also writes a selection byte at
//     ((uint8_t*)puGpffffb0d4)[0x95], using entry[0x0F] if within [0x1E,0x31] else a rolling
//     counter at (iGpffffb0f0 + 0x68).
// - Returns 0.
//
// Notes
// - The error string used on the mode==3 range check is at 0x0034CEB8: "tbox param error [set_pw_all]".
//   This strongly hints the original internal name for this behavior was "set_pw_all" in the
//   "tbox" system; we adopt that as the semantic alias while preserving FUN_0025eb48 in comments.
// - The entry layout (per 0x10 stride):
//   [0x00] int32 x, [0x04] int32 y, [0x08] int32 z, [0x0C] int32 packed (low byte used),
//   [0x0D] uint8 type/mode, [0x0E] int8 id/code, [0x0F] uint8 attr/code.
// - Time/scale computation: t_fixed = FUN_00216690((int8)entry[0x0C] * fGpffff8c2c + fGpffff8c30).
// - Allocation helper FUN_00265e28 is called with 0x3A for mode 3 and with no argument for others
//   in the raw code; here we pass 0 for the latter to keep a single prototype.
// - Keep original FUN_/DAT_ names in externs; rename only this analyzed wrapper.

#include <stdint.h>

// ===== Extern globals (names preserved as exported) =====
extern float fGpffff8c30, fGpffff8c2c; // timing/scale globals
extern unsigned char *pbGpffffbd60;    // secondary parameter byte stream
extern int iGpffffb774;                // entry count
extern int iGpffffb778;                // entry base pointer (struct array)
extern int iGpffffb0dc;                // aux table count
extern char cGpffffb663;               // mode-2 extra behavior flag
extern int iGpffffb0f0;                // struct holding rolling counter at +0x68
extern uint16_t *puGpffffb0d4;         // global pointer to last allocated object/effect

// ===== Extern functions (un-analyzed) =====
extern void FUN_0026bfc0(int str_addr);           // error/abort with string
extern long FUN_00265e28(int type_maybe);         // allocator (type 0x3A for mode 3; 0 otherwise)
extern uint32_t FUN_00216690(float f);            // float->fixed/packed convert
extern void FUN_00266240(int, int, int, uint32_t, // spawner/configurer
                         uint16_t *, int, int, uint8_t);
extern void FUN_0025bae8(uint8_t mode, uint16_t firstWord, // param block builder
                         uint8_t outBlock[48]);
extern void FUN_0023a518(uint16_t *obj, const uint8_t block[48]); // apply param block

// ===== Extern data tables =====
extern int DAT_00571d00; // table base (first int per entry)
extern int DAT_00571d04; // table base +4 (second int per entry)
extern int DAT_00571d08; // table base +8 (byte used as last arg)

// Original signature: undefined8 FUN_0025eb48(void)
void opcode_0x51_set_pw_all_dispatch(void)
{
  // Read mode from secondary byte stream
  uint8_t mode = *pbGpffffbd60;
  pbGpffffbd60++;

  if (iGpffffb774 <= 0)
    return; // nothing to do

  // Cache timing globals
  const float base = fGpffff8c30;
  const float scale = fGpffff8c2c;

  // Iterate entries (stride 0x10)
  for (int idx = 0; idx < iGpffffb774; ++idx)
  {
    int entry = iGpffffb778 + idx * 0x10;

    if (mode == *(uint8_t *)(entry + 0x0D))
    {
      int8_t id = *(int8_t *)(entry + 0x0E);

      if (mode == 3)
      {
        // Range check for set_pw_all (tbox) parameter
        if ((int)id > 0x7F)
        {
          // "tbox param error [set_pw_all]"
          FUN_0026bfc0(0x0034ceb8);
        }

        long p = FUN_00265e28(0x3A);
        puGpffffb0d4 = (uint16_t *)p;
        if (p != 0)
        {
          // Build time/scale param
          uint32_t t_fixed = FUN_00216690((float)(int)*(int8_t *)(entry + 0x0C) * scale + base);

          // Spawn/configure with lastArg=0
          FUN_00266240(*(int *)(entry + 0x00), *(int *)(entry + 0x04), *(int *)(entry + 0x08),
                       t_fixed, puGpffffb0d4, 0, 0, 0);

          // Bookkeeping fields on the spawned object
          *(int *)(puGpffffb0d4 + 0x4C) = idx; // store list index
          puGpffffb0d4[0x98] = (uint16_t)id;   // store ID (sign-extended in src)
          *(uint32_t *)(puGpffffb0d4 + 0xCC) = // code = attr + 0x400
              (uint32_t)(*(uint8_t *)(entry + 0x0F)) + 0x400;
        }
      }
      else
      {
        // Search aux table for matching id and valid state
        for (int j = 0; j < iGpffffb0dc; ++j)
        {
          int off = j * 0x0C;
          if (id == *(int *)&((uint8_t *)&DAT_00571d00)[off] &&
              *(int *)&((uint8_t *)&DAT_00571d04)[off] != 0x55)
          {
            long p = FUN_00265e28(0);
            puGpffffb0d4 = (uint16_t *)p;
            if (p != 0)
            {
              uint32_t t_fixed = FUN_00216690((float)(int)*(int8_t *)(entry + 0x0C) * scale + base);

              // lastArg pulled from table +8 (one byte)
              uint8_t lastArg = *(uint8_t *)&((uint8_t *)&DAT_00571d08)[off];
              FUN_00266240(*(int *)(entry + 0x00), *(int *)(entry + 0x04), *(int *)(entry + 0x08),
                           t_fixed, puGpffffb0d4, 0, 0, lastArg);

              *(int *)(puGpffffb0d4 + 0x4C) = idx;

              // Build and apply per-mode parameter block
              uint8_t block[48];
              FUN_0025bae8(mode, *puGpffffb0d4, block);
              FUN_0023a518(puGpffffb0d4, block);

              // Extra tagging for mode==2 when enabled
              if (mode == 2 && cGpffffb663)
              {
                uint8_t tag = *(uint8_t *)(entry + 0x0F);
                if ((uint8_t)(tag - 0x1E) < 0x14)
                {
                  *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x95) = tag;
                }
                else
                {
                  *(uint8_t *)((uint8_t *)puGpffffb0d4 + 0x95) = *(uint8_t *)(iGpffffb0f0 + 0x68);
                  *(int *)(iGpffffb0f0 + 0x68) = *(int *)(iGpffffb0f0 + 0x68) + 1;
                }
              }
            }

            break; // match handled for this entry
          }
        }
      }
    }
  }
}
