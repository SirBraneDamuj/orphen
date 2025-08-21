// Object/System Method Dispatcher (analyzed)
// Original: FUN_00242a18
// Invoked by VM opcode 0xBD (object_method_dispatch) via FUN_00263e80
//
// Summary:
// - Dispatches a variety of object/system methods selected by an 8-bit method code.
// - Some methods operate on global UI/selection subsystems, some on per-object UI slots
//   referenced by the object’s slot index at offset +0x95, others control timed interpolation
//   and state machines.
// - The parameter conventions vary by method case: codes {1,2,3,0x64..0x6A} consume the
//   third/fourth arguments; codes {0x6F..0x7D} usually act on the object/context (first arg).
//
// Parameters (inferred):
// - obj: pointer to current object/context (from uGpffffb0d4 via opcode 0xBD caller)
// - method: 8-bit method code (see mapping below)
// - a0, a1: 64-bit slots used as integers/pointers depending on method
// Returns:
// - u64: method-defined; many return 0/1 status or small integers; some return -1/err codes.
//
// Notable globals used by callees:
// - uGpffffb052: subsystem flags; bits checked/modified by several methods
// - iGpffffb0f0: base of a small global array used by timed updates (see 0x64..0x6A cluster)
// - DAT_0031d7xx / DAT_0031daxx / DAT_00354fxx: large banks used for UI/selection state
// - DAT_003437a0/b8: small mapping tables manipulated by 0x78
// - iGpffffb64c: global tick delta (Q5) used by timed interpolation paths
//
// Method map (case → callee → behavior):
// - 0x01 → FUN_00242de0(a0,a1): Reset subsystem flags (uGpffffb052 = 0). Returns 0.
// - 0x02 → FUN_00243f80(a0,a1): Ensure subsystem initialized; snapshot/seed several per-slot
//   UI values from current object(s) (position*10, etc.), run setup across active slots.
//   Sets uGpffffb052 |= 1. Returns 0.
// - 0x03 → FUN_002432d8(a0,a1): Heavy initialization/refresh for the UI/selection subsystem.
//   Allocates/clears banks, seeds per-slot structures, attaches pool objects, configures
//   per-slot resources, and populates mapping arrays. Returns 0.
//
// Timed interpolation / control cluster (decimal 100..106):
// - 0x64 (100) → FUN_00242c20(a0,a1): Reset timing control words (uGpffffb056=0, clear queues/counters). Ret 1.
// - 0x65 (101) → FUN_00242c40(a0,a1): If system idle, set state word at (iGpffffb0f0+100)=a0, set
//   uGpffffb056=3; schedule duration iGpffffb058 = (a1<<5); clear pending write (uGpffffb064/60).
//   Ret 1 (or -1 if busy, -2 if a0<1).
// - 0x66 (102) → FUN_00242c90(a0,a1): Queue a deferred write: set index (uGpffffb064=a0) and value (uGpffffb060=a1).
//   The updater (FUN_00242cf0) later writes value into *(iGpffffb0f0 + index*4). Ret 0.
// - 0x67 (103) → FUN_00242ca0(): Clear running/completion bits in uGpffffb056; call FUN_00217e18(0) if needed.
//   Returns whether the “in-progress” bit was set before clearing (bool as u64).
// - 0x68 (104) → FUN_00242cf0(): Per-frame updater. If certain state words (500/501/600) are set, call
//   mode-specific handlers. Else, if a duration (iGpffffb058) is active, call FUN_00217f38() each tick and
//   advance iGpffffb05c by iGpffffb64c until reaching iGpffffb058. Then process any deferred index write
//   (cGpffffb064>=0) into iGpffffb0f0[cGpffffb064]. Ret 0.
// - 0x69 (105) → FUN_00242dd0(a0,a1): Stub, returns 0.
// - 0x6A (106) → FUN_00242dd8(a0,a1): Stub, returns 0.
//
// Per-object/slot methods (0x6F..0x7D):
// - 0x6F → FUN_00244248(obj, byte value=a0, long force=a1): Set per-slot state byte at offset 2, with optional
//   transition checks if force==0 (requires flags allow and current state not '\n'/'\x06' unless exceptions).
//   Selects the slot either from object->+0x95 (<11) or via FUN_00247d80. Ret 1 on write, 0/err otherwise.
// - 0x70 → FUN_002443f8(param_1=a0, baseOffset=param_2, flags=param_3): Build/start a timed interpolation track:
//   copies N keyframes from a temp bank (DAT_01849a00/08/0C), scales to seconds, sets durations (<<4), seeds
//   control struct with base pointer (DAT_00355058+baseOffset), and calls FUN_00266a78(..., streaming=flags&0x40000000).
//   Ret 1 on success else 0xFFFFFFFF.
// - 0x71 → FUN_00244650(id, fieldIndex, value): Find active track by id and set one of four small fields
//   (0: half of value into +9, 1: value into +8, 2: short at +0xC, 3: value into +0xB). Ret 0.
// - 0x72 → progress_or_cancel_timed_track (orig FUN_002445c8): If bit0 of flags==0, return progress fraction
//   (1..1001) from fields (u16 total at [2], current at [3]); if bit0 set, cancel by zeroing first field.
//   See analyzed/object_methods/progress_or_cancel_timed_track.c. Ret -1 if system off.
// - 0x73 → FUN_00244210(_, baseOffset, force): If force==0 and not busy (DAT_003555d3==0), call FUN_0023f318(baseOffset+DAT_00355058).
//   Ret 0.
// - 0x74 → FUN_00244a18(obj, tableOffset, index): Write per-slot 2D/3D position fields. If tableOffset==0, take
//   object->(+0x20,+0x24,+0x28) scaled by 10 (Z offset adds fGpffff87b0); else read coord triplet from
//   (iGpffffb0e8 + tableOffset + index*12) and divide each by 100. Updates both “current” and “target” shorts.
//   Ret 0 on success else 0xFFFFFFFF.
// - 0x75 → FUN_00244b40(short *field, mode): Toggle sign of a short field and flip a per-entity flag in DAT_005a96b0
//   based on mode (1=set positive if needed; 2=set negative if needed). No-op if already matching state. Ret 0.
// - 0x76 → FUN_00244bf0(_, mode): If mode==1: enter a mode (set uGpffffb6f1=0, flags|=4, clear counters);
//   if mode==2: exit mode (clear bit 4), possibly set slot state 6 for current selection, and call FUN_00249108 on slot.
//   Ret 0.
// - 0x77 → FUN_00244ca0(_, a0, a1): Pass-through to FUN_00206f08(a0,a1). Likely trigger/effect helper. Ret void.
// - 0x78 → FUN_00244cc0(_, packedXY, spec): Manipulate a 3-wide per-row mapping table DAT_003437a0 and
//   reference counts DAT_003437b8. packedXY encodes x (low nibble) and row (bits 8..11). spec encodes a source index
//   or an operation; supports swapping, moving, and count bookkeeping. Ret 0/1 or 0xFFFFFFFF on invalid.
// - 0x79 → FUN_00244fe8(obj, byte): If obj->+0x95 < 4, write byte into DAT_0031da65[idx]. Ret 0.
// - 0x7A → FUN_00245010(): Advance a small state machine (uGpffffafaa), calling a function from a table at 0x31d358
//   and priming a render packet via FUN_002d7038(...,0x1007). Returns new state (u16).
// - 0x7B → FUN_002454b0(_, mode): Compute selection/weighting across 12 channels, clamp, derive a decision code
//   DAT_0031dc10 in {0,1,2}; update resource pointers and optionally signal via FUN_00245860(0, (&DAT_0031dbee)[code]).
//   Returns code (or 2 if only one mode available).
// - 0x7C → FUN_002457d0(obj): Map global mode (FUN_0023ef10) to one of {1,0x48,0x6E,0x73,0x7C}, call FUN_00248e98(obj,code),
//   and set a status bit in obj (+6)|=0x80. Ret 1.
// - 0x7D → FUN_00245860(_, id): Increment a counter bucket indexed by FUN_00237830(id). Ret 0.
//
// Notes:
// - Many of these are UI/cutscene/selection related rather than core gameplay; naming is tentative and based on
//   observed globals and data flow. As more callees are analyzed, refine names accordingly.

#include <stdint.h>

// Extern raw callees (leave unanalyzed names)
extern uint64_t FUN_00242de0(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00243f80(uint64_t a0, uint64_t a1);
extern uint64_t FUN_002432d8(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00242c20(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00242c40(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00242c90(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00242ca0(void);
extern uint64_t FUN_00242cf0(void);
extern uint64_t FUN_00242dd0(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00242dd8(uint64_t a0, uint64_t a1);
extern uint64_t FUN_00244248(uint64_t obj, uint8_t value, uint64_t force);
extern uint64_t FUN_002443f8(uint32_t p1, int baseOffset, uint64_t flags);
extern uint64_t FUN_00244650(int id, int field, int value);
extern int progress_or_cancel_timed_track(int id, unsigned long flags);
extern uint64_t FUN_00244210(uint64_t unused, int baseOffset, uint64_t force);
extern uint64_t FUN_00244a18(uint64_t obj, uint64_t tableOffset, int index);
extern uint64_t FUN_00244b40(int16_t *field, int mode);
extern uint64_t FUN_00244bf0(uint64_t unused, int mode);
extern void FUN_00244ca0(uint64_t unused, uint64_t a0, uint64_t a1);
extern uint32_t FUN_00244cc0(uint64_t unused, uint32_t packedXY, uint64_t spec);
extern uint64_t FUN_00244fe8(uint64_t obj, uint8_t value);
extern uint16_t FUN_00245010(void);
extern uint16_t FUN_002454b0(uint64_t unused, int mode);
extern uint32_t FUN_002457d0(uint64_t obj);
extern uint64_t FUN_00245860(uint64_t unused, uint16_t id);

// Original signature: undefined8 FUN_00242a18(undefined8 obj, undefined1 method, undefined8 a0, undefined8 a1)
uint64_t object_system_method_dispatcher(uint64_t obj, uint8_t method, uint64_t a0, uint64_t a1)
{
  switch (method)
  {
  case 0x01:
    return FUN_00242de0(a0, a1);
  case 0x02:
    return FUN_00243f80(a0, a1);
  case 0x03:
    return FUN_002432d8(a0, a1);
  case 0x64:
    return FUN_00242c20(a0, a1);
  case 0x65:
    return FUN_00242c40(a0, a1);
  case 0x66:
    return FUN_00242c90(a0, a1);
  case 0x67:
    return FUN_00242ca0();
  case 0x68:
    return FUN_00242cf0();
  case 0x69:
    return FUN_00242dd0(a0, a1);
  case 0x6A:
    return FUN_00242dd8(a0, a1);
  case 0x6F:
    return FUN_00244248(obj, (uint8_t)(a0 & 0xFF), a1);
  case 0x70:
    return FUN_002443f8((uint32_t)a0, (int)a1, a1); // note: raw takes (p1, baseOffset, flags)
  case 0x71:
    return FUN_00244650((int)a0, (int)(a1 & 0xFFFFFFFF), (int)(a1 >> 32));
  case 0x72:
    return (uint64_t)progress_or_cancel_timed_track((int)a0, a1);
  case 0x73:
    return FUN_00244210(0, (int)a0, a1);
  case 0x74:
    return FUN_00244a18(obj, a0, (int)a1);
  case 0x75:
    return FUN_00244b40((int16_t *)obj, (int)a0);
  case 0x76:
    return FUN_00244bf0(0, (int)a0);
  case 0x77:
    FUN_00244ca0(0, a0, a1);
    return 0;
  case 0x78:
    return FUN_00244cc0(0, (uint32_t)a0, a1);
  case 0x79:
    return FUN_00244fe8(obj, (uint8_t)(a0 & 0xFF));
  case 0x7A:
    return FUN_00245010();
  case 0x7B:
    return FUN_002454b0(0, (int)a0);
  case 0x7C:
    return FUN_002457d0(obj);
  case 0x7D:
    return FUN_00245860(0, (uint16_t)(a0 & 0xFFFF));
  default:
    return 0;
  }
}
