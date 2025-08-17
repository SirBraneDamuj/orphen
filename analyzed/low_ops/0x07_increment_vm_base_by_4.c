// Low-opcode 0x07 — increment_vm_base_by_4 (analyzed)
// Original label: LAB_0025bea8
//
// Ghidra label body:
//   lw   v0, -0x42a0(gp)
//   addiu v0, v0, 4
//   jr   ra
//   sw   v0, -0x42a0(gp)
//
// Semantics:
// - Unconditionally advances the VM scratch/base pointer by 4 bytes.
// - Matches the behavior seen as the non-zero branch of 0x01 (iGpffffbd60 += 4), but without a guard.
//
// Notes:
// - We alias the gp-relative slot at -0x42a0(gp) as iGpffffbd60 per broader usage in the VM helpers.

extern int iGpffffbd60; // VM scratch/base pointer used by FUN_0025c258

unsigned int low_opcode_0x07_increment_vm_base_by_4(void)
{
  iGpffffbd60 += 4;
  return 0;
}
