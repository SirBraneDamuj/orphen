// Analysis of FUN_00260738 -> update_entity_timed_parameter (provisional name)
// Original signature (decompiled):
//   undefined8 FUN_00260738(void)
// Context: Invoked via script VM standard opcode jump table (PTR_LAB_0031e228).
// Behavior summary:
//   Reads an entity index and a time/value parameter from the script VM (via FUN_0025c258 calls), then
//   updates one of two per-entity float parameter arrays (offsets 0x3C or 0x48 within a 0x74-sized entity record)
//   scaling the raw script parameter by a global timing divisor (DAT_00352c08 or DAT_00352c0c). It also sets flag bits
//   in a status byte at offset 0x5A indicating which parameter groups have been initialized.
//
// Inferred semantics:
//   - DAT_003556e0 : base pointer to entity array (each entity struct size 0x74 bytes)
//   - DAT_003556dc : entity count or capacity (bounds checked)
//   - DAT_00352c08 / DAT_00352c0c : timing or frame-time scaling denominators (float)
//   - DAT_00355cd8 : current opcode (used to detect variant 0x7d)
//   - The second value fetched (iStack_5c) is scaled; first (iStack_60) selects entity index.
//   - If opcode == 0x7d choose parameter slot group A (offset base 0x3C) and flag bit 0x02; else group B (offset base 0x48) and flag bit 0x01.
//   - FUN_00216690 appears to convert a float ratio to a fixed or eased value (kept as-is pending its own analysis).
//   - FUN_0026bfc0 is likely an assertion / error logger for out-of-range index or unexpected parameter count.
//
// TODOs:
//   - Confirm exact meaning of FUN_00216690 (e.g., easing function, scaling, randomness?).
//   - Name entity struct fields once more functions referencing 0x3C/0x48/0x5A are analyzed.
//   - Determine precise opcode number mapping for 0x7d variant (maybe "set_param_timed_normalized" vs default variant).
//
// Original function kept below with renamed variables.

#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef float f32;

typedef u32 (*vm_func_t)(void);

// Extern (original symbols preserved):
extern short DAT_00355cd8; // current opcode for the standard handler path
extern int DAT_003556dc;   // entity capacity / count
extern int DAT_003556e0;   // base pointer to entity array
extern f32 DAT_00352c08;   // timing divisor A
extern f32 DAT_00352c0c;   // timing divisor B

// Script VM helpers (un-analyzed yet):
extern void FUN_0025c258(int *out_value);  // fetch next script value / stack interaction
extern void FUN_0026bfc0(int addr);        // error / debug
extern uint32_t FUN_00216690(float value); // conversion/scaling (returns packed? stored as u32)

// Provisional analyzed version:
uint64_t update_entity_timed_parameter(void)
{ // returns 0 in original
  short currentOpcode = DAT_00355cd8;
  int entityIndexTemp = 0; // iStack_60
  int scaledInputRaw = 0;  // iStack_5c

  // Fetch first script-supplied value (entity index)
  FUN_0025c258(&entityIndexTemp);

  // Read a small parameter count/control byte from bytecode stream (*DAT_00355cd0 previously) – done inline in original
  // In original decomp a byte bVar2 was read and bounds-checked (0..2); we keep logic implicit here.
  unsigned char paramSlot = *(unsigned char *)/*DAT_00355cd0*/ (0); // NOTE: placeholder; actual fetch happens before increment in original context
  // The real code advanced DAT_00355cd0 externally; we do not model PC here, only document behavior.

  // Fetch second script value (raw scalar or time value)
  FUN_0025c258(((int)&entityIndexTemp) | 4); // using stack offset pattern; reproducing sequence

  // Bounds & variant checks (mirroring original order):
  if (paramSlot > 2)
  {
    FUN_0026bfc0(0x34cfb8); // unexpected slot
  }
  if (entityIndexTemp >= DAT_003556dc)
  {
    FUN_0026bfc0(0x34cfd0); // entity index OOB
  }

  int entityBase = DAT_003556e0 + entityIndexTemp * 0x74;

  // Choose parameter group & scaling based on opcode variant
  u8 *statusByte = (u8 *)(entityBase + 0x5A);
  if (currentOpcode == 0x7d)
  {
    // Group A path: store converted scaled value at offset 0x3C + slot*4
    uint32_t packed = FUN_00216690((float)scaledInputRaw / DAT_00352c08);
    *(uint32_t *)(entityBase + 0x3C + paramSlot * 4) = packed;
    // Set/ensure bit 1 (0x02) in status byte
    if (*statusByte < 2)
    {
      *statusByte = 2;
    }
    else
    {
      *statusByte |= 2;
    }
  }
  else
  {
    // Group B path: store a plain float (scaled) at offset 0x48 + slot*4
    *(float *)(entityBase + 0x48 + paramSlot * 4) = (float)scaledInputRaw / DAT_00352c0c;
    if (*statusByte < 1)
    {
      *statusByte = 1;
    }
    else
    {
      *statusByte |= 1;
    }
  }
  return 0; // original returns 0
}

// NOTE: The read of the byte 'bVar2' (paramSlot) and increment of DAT_00355cd0 happen in the interpreter harness prior
// to calling this semantic body; for clarity we abstracted PC manipulation out. A future refinement can pass paramSlot
// as an explicit argument once calling conventions are better documented.
