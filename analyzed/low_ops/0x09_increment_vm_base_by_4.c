// Low-opcode 0x09 — increment structural VM base pointer by 4 (same as 0x07)
// Original label: LAB_0025bec0
// Raw body:
//   lw v0, -0x42a0(gp)
//   addiu v0, v0, 0x4
//   jr ra
//   sw v0, -0x42a0(gp)
//
// Semantics:
//   iGpffffbd60 += 4 (the structural interpreter's scratch/base pointer). Identical to 0x07.
//
// Notes:
//   We express this in analyzed form by advancing the exported `pbGpffffbd60` pointer by 4 bytes.

#include <stdint.h>

// Structural interpreter pointer (extern from analyzed/script_block_structure_interpreter.c)
extern unsigned char *pbGpffffbd60;

void lowop_0x09_increment_vm_base_by_4(void)
{
  pbGpffffbd60 += 4;
}
