// Analyzed from: src/FUN_00221f60.c
// Original symbol: FUN_00221f60
//
// Purpose
//   Relocate PSC3-internal section pointers after copying/decoding to a new base.
//   Copies the resource bytes from oldBase to newBase, then adds the base delta to
//   four u32 header fields at offsets +0x1C, +0x20, +0x24, +0x28.
//
// Contract
// - Input param_1: PSC3 base pointer (int) of the destination header
// - Input param_2: newBase (destination bytes)
// - Input param_3: oldBase (source bytes)
// - Input param_4: byteCount to copy
// - Effect: memcpy + patch header dwords as above

#include <stdint.h>

extern void FUN_00267da0(void *dst, const void *src, int size); // memcpy variant

// void FUN_00221f60(int param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)
void relocate_psc3_header_pointers(int psc3Base, void *newBase, void *oldBase, int size)
{
  // Copy the bytes
  FUN_00267da0(newBase, oldBase, size);

  // Compute base delta and patch header pointer-like fields
  int delta = (int)((uintptr_t)newBase - (uintptr_t)oldBase);
  *(int *)(psc3Base + 0x1C) += delta;
  *(int *)(psc3Base + 0x20) += delta;
  *(int *)(psc3Base + 0x24) += delta;
  *(int *)(psc3Base + 0x28) += delta;
}
