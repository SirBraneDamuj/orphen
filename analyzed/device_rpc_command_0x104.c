// Analyzed re-expression of FUN_003034e0
// Original signature: undefined4 FUN_003034e0(undefined4 p1, undefined4 p2, undefined4 p3, undefined4 p4)
//
// Observed Pattern:
//   This function is one of a tightly related cluster (FUN_003033d0, FUN_00303458, FUN_003034e0,
//   FUN_00303568, FUN_00303628, FUN_003037a8, FUN_00303828, FUN_00303958, FUN_00303a30, etc.) that:
//     1. Writes a command ID into DAT_01d4e160.
//     2. Packs up to four 32-bit parameters into DAT_01d4e164 .. DAT_01d4e170 (or a small byte array for 0x106).
//     3. Calls FUN_002f4b10 with (0x1d4e010, 1, 0, 0x1d4e160, 0x80, 0x1d4e160, 0x80, 0).
//     4. Reads a result field (commonly DAT_01d4e174 or DAT_01d4e16c depending on command) and returns it.
//     5. On negative return from FUN_002f4b10 (transport failure) logs an error via FUN_002f73b8(<string_addr>)
//        and returns 0 / 0xFFFFFFFF depending on wrapper.
//
// Inference:
//   FUN_002f4b10 implements a synchronous SIF (IOP<->EE) RPC style transaction:
//     - It acquires a work item (FUN_002f44e0), sets up a semaphore (CreateSema) when needed, copies
//       request parameters to IOP shared memory (param_4 / param_5), kicks an RPC (FUN_002f7950),
//       waits on completion (WaitSema) and returns a status (0 = success, negative = transport/setup error).
//   The constant command IDs in the 0x80000100..0x8000010B range strongly resemble module function numbers.
//   We leave the precise subsystem unnamed (could be controller/pad, peripheral, or custom game service) and
//   label these wrappers generically as device RPC commands.
//
// This specific wrapper sets command ID 0x80000104 and writes four 32-bit arguments.
// Return value: DAT_01d4e174 on success (transport status >= 0), else 0 after logging error.
//   (DAT_01d4e174 is assumed to be populated by the IOP side into the 0x1d4e160 reply buffer.)
//
// Side Effects:
//   - Mutates the shared request/reply buffer at 0x1d4e160.. (command + args + result).
//   - Issues an inter-processor transaction.
//
// Naming:
//   Analyzed name chosen: device_rpc_command_0x104. Original FUN_* symbol preserved for cross reference.
//   Future refinement: Once the subsystem is definitively identified, rename the group with semantic verbs
//   (e.g., pad_port_action_set, mc_slot_op, etc.).
//
// Error Path:
//   On FUN_002f4b10 < 0, calls FUN_002f73b8(0x350f98) — address likely points to an error string. Returns 0.
//
// Potential Next Steps:
//   - Inspect the buffer layout at 0x1d4e160 in memory dumps to map field semantics.
//   - Correlate command IDs with values written/returned to guess operations (e.g., open, read, status).
//   - Analyze FUN_002f73b8 to catalog error messages.
//
// NOTE: We do not alter global names; we only wrap.

#include <stdint.h>

// ===== Extern globals (raw addresses / symbols) =====
extern uint32_t DAT_01d4e160; // command ID / leading word
extern uint32_t DAT_01d4e164; // arg0
extern uint32_t DAT_01d4e168; // arg1
extern uint32_t DAT_01d4e16c; // arg2 (or result for some commands)
extern uint32_t DAT_01d4e170; // arg3
extern uint32_t DAT_01d4e174; // primary result field for this command

// ===== Extern functions (un-analyzed) =====
extern long FUN_002f4b10(int *work, uint32_t mode, unsigned long flags,
                         long reqBuf, long reqSize, long replyBuf, long replySize, long cbArg);
extern void FUN_002f73b8(int string_addr); // error logger / panic print

// Analyzed wrapper
uint32_t device_rpc_command_0x104(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
  DAT_01d4e160 = 0x80000104; // command ID
  DAT_01d4e164 = a0;
  DAT_01d4e168 = a1;
  DAT_01d4e16c = a2;
  DAT_01d4e170 = a3;

  long transport = FUN_002f4b10((int *)0x1d4e010, 1, 0, 0x1d4e160, 0x80, 0x1d4e160, 0x80, 0);
  uint32_t result = DAT_01d4e174;
  if (transport < 0)
  {
    FUN_002f73b8(0x350f98); // log error string
    result = 0;             // override result on transport failure
  }
  return result;
}

// Original symbol preserved
uint32_t FUN_003034e0(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4)
{
  return device_rpc_command_0x104(p1, p2, p3, p4);
}
