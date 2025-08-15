// vm_fetch_immediate_or_pack — analyzed version of FUN_0025bf70
// Original signature: undefined4 FUN_0025bf70(uint *out)
// Role: Low-range immediate/packing helper used by the main bytecode interpreter for opcodes 0x00–0x31.
// Behavior:
//  - Reads a mini-opcode at pbGpffffbd60, stores it in uGpffffbd64, and advances pbGpffffbd60.
//  - For 0x0C/0x0D/0x0E: returns 8/16/32-bit immediates (little-endian) in *out.
//  - For 0x0F/0x10/0x11: returns scaled immediates (x100, x1000, deg->tau*10000/360).
//  - For 0x30/0x31: packs 3 or 4 bytes returned by nested VM evaluations into a 32-bit word (little-endian order).
// Return:
//  - 1 on handled opcode (and *out set), 0 if the leading byte is not recognized here (caller handles it).
// Globals:
//  - pbGpffffbd60: cursor into the script stream for this immediate helper.
//  - uGpffffbd64: last mini-opcode consumed (for diagnostics/instrumentation).
// Notes:
//  - Nested evaluations call the main VM (`bytecode_interpreter`) to fill consecutive 32-bit slots.
//  - 0x11 uses integer math: out = (short)*0xF570 / 0x168, matching the decompile; this equals round(deg * (2π*10000/360)).

#include <stdint.h>

typedef unsigned int uint;

// Globals (addresses in globals.json)
extern unsigned char *pbGpffffbd60; // input cursor
extern uint uGpffffbd64;            // last mini-opcode

// Main VM (analyzed name)
extern void bytecode_interpreter(void *result_out); // orig: FUN_0025c258

unsigned int vm_fetch_immediate_or_pack(uint *out)
{
  uint op = (uint)*pbGpffffbd60;
  uGpffffbd64 = op;

  switch (op)
  {
  case 0x0C:
  { // u8 immediate
    unsigned char v = pbGpffffbd60[1];
    pbGpffffbd60 += 2;
    *out = (uint)v;
    return 1;
  }
  case 0x0D:
  { // u16 immediate (LE)
    uint v = (uint)pbGpffffbd60[1] | ((uint)pbGpffffbd60[2] << 8);
    pbGpffffbd60 += 3;
    *out = v;
    return 1;
  }
  case 0x0E:
  { // u32 immediate (LE)
    uint v = (uint)pbGpffffbd60[1] | ((uint)pbGpffffbd60[2] << 8) | ((uint)pbGpffffbd60[3] << 16) | ((uint)pbGpffffbd60[4] << 24);
    pbGpffffbd60 += 5;
    *out = v;
    return 1;
  }
  case 0x0F:
  { // s32 * 100
    int v = *(int *)(pbGpffffbd60 + 1);
    pbGpffffbd60 += 5;
    *out = (uint)(v * 100);
    return 1;
  }
  case 0x10:
  { // s16 * 1000
    int16_t v = *(int16_t *)(pbGpffffbd60 + 1);
    pbGpffffbd60 += 3;
    *out = (uint)(v * 1000);
    return 1;
  }
  case 0x11:
  { // degrees -> tau*10000 scaling: v * 0xF570 / 0x168
    int16_t v = *(int16_t *)(pbGpffffbd60 + 1);
    pbGpffffbd60 += 3;
    *out = (uint)((v * 0xF570) / 0x168);
    return 1;
  }

  case 0x30: // pack 3 bytes (A omitted -> 0)
  case 0x31:
  { // pack 4 bytes
    // Consume the opcode byte
    pbGpffffbd60 += 1;

    // Fill 3 or 4 32-bit slots via nested VM evaluations
    uint slots[4] = {0, 0, 0, 0};
    bytecode_interpreter(&slots[0]);                          // B
    bytecode_interpreter((void *)((uintptr_t)&slots[0] | 4)); // G
    bytecode_interpreter((void *)((uintptr_t)&slots[0] | 8)); // R
    if (op == 0x31)
      bytecode_interpreter((void *)((uintptr_t)&slots[0] | 12)); // A

    // Pack into BGRA order per original: [B|G|R|A] little-endian
    *out = ((slots[3] & 0xFF) << 24) | ((slots[2] & 0xFF) << 16) | ((slots[1] & 0xFF) << 8) | (slots[0] & 0xFF);
    return 1;
  }

  default:
    return 0; // not handled here; caller will process
  }
}
