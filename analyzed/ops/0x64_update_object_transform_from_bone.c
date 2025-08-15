// Opcode 0x64 — update_object_transform_from_bone (tentative)
// Original: FUN_0025f700 (src/FUN_0025f700.c)
//
// Summary
//   Reads one argument from the VM, applies a character/bone-relative transform to the current
//   object (DAT_00355044) if a pending bone index is present, then clears the pending index.
//
// Evidence
//   - Reads one VM arg into auStack_40 and calls select_current_object_frame(auStack_40[0], currentObj)
//     (original: FUN_0025d6c0).
//     Likely binds/updates transform based on a parameter (e.g., bone id or resource).
//   - If *(short *)(current+0x192) >= 0 and *(char *)(current+0x194) >= 0, then:
//       * Copies current->pos (12 bytes at +0x20) to a temp
//       * Calls FUN_0020dc88(base=DAT_0058beb0 + idx*0xec, subIndex, inPos, outPos)
//       * Writes back transformed pos to current->pos
//       * Recomputes current->worldMatrix? at +0x4c via FUN_00227798(x,y,z)
//     Then clears current->pendingBoneIndex (short at +0x192) to -1.
//
// Original signature
//   undefined8 FUN_0025f700(void);
//
// Globals
//   DAT_00355044: current object pointer
//   DAT_0058beb0: base to actor/bone table (stride 0xEC)
//
// Functions
//   FUN_0025c258(out)        – VM fetch arg
//   select_current_object_frame(arg, obj) — choose active entity/frame (orig: FUN_0025d6c0)
//   FUN_00267da0(dst, src, n) – memcpy
//   FUN_0020dc88(base, sub, inPos, outPos) – bone transform apply
//   FUN_00227798(x,y,z)     – pack vector / compute hash
//
// Side effects
//   - Mutates current object position (0x20..0x2B) and a derived field at 0x4C.
//   - Clears pending bone index.

#include <stdint.h>

extern void *DAT_00355044;         // current object
extern unsigned char DAT_0058beb0; // base table

extern void bytecode_interpreter(void *out); // analyzed (orig: FUN_0025c258)
extern void select_current_object_frame(uint32_t selector, void *fallbackPtr);
extern void FUN_00267da0(void *dst, const void *src, int n);
extern void FUN_0020dc88(void *base, unsigned char subIndex, void *inPos, void *outPos);
extern uint32_t FUN_00227798(uint32_t x, uint32_t y, uint32_t z);

unsigned long opcode_0x64_update_object_transform_from_bone(void)
{
  int obj = (int)(intptr_t)DAT_00355044;

  uint32_t arg4[4];
  bytecode_interpreter(arg4);
  select_current_object_frame(arg4[0], (void *)(intptr_t)obj);

  short pending = *(short *)(obj + 0x192);
  if (pending >= 0)
  {
    signed char sub = *(signed char *)(obj + 0x194);
    if (sub >= 0)
    {
      unsigned char tmpIn[16], tmpOut[16];
      FUN_00267da0(tmpIn, (void *)(obj + 0x20), 12);
      FUN_0020dc88((void *)((intptr_t)&DAT_0058beb0 + pending * 0xEC), (unsigned char)sub,
                   (void *)(obj + 0x20), tmpOut);
      FUN_00267da0((void *)(obj + 0x20), tmpOut, 12);
      uint32_t packed = FUN_00227798(*(uint32_t *)(obj + 0x20), *(uint32_t *)(obj + 0x24),
                                     *(uint32_t *)(obj + 0x28));
      *(uint32_t *)(obj + 0x4C) = packed;
    }
    *(short *)(obj + 0x192) = (short)0xFFFF; // clear
  }

  return 0;
}
