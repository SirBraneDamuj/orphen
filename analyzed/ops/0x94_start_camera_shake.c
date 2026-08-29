// Opcode 0x94 — start_camera_shake
// Original: FUN_002612e0 (0x002612e0)
//
// Previously filed as "set_audio_position_normalized". It is not audio. The
// name came from the one visible callee, FUN_0023baf8, which sits in the sound
// code -- and which is `jr $ra; nop` in the retail build.
//
// Summary:
// - Evaluates two VM expressions.
// - magnitude = (float)(int)expr0 / DAT_00352c34   (100000.0, the same scale
//   every other world-space operand uses).
// - duration  = (short)expr1, read back as a halfword at 0x00261304
//   (`lhu 4($sp)`) and sign-extended, so only the low 16 bits survive.
// - Calls FUN_0022dcf0(magnitude, duration).
//
// FUN_0022dcf0 (0x0022dcf0):
//
//   if (sGpffffb6f8 != 0 && magnitude < (float)sGpffffb6f8) return;
//   fGpffffb6f4 = magnitude;      // 0x00355664
//   sGpffffb6f8 = duration;       // 0x00355668, in frame ticks
//   FUN_0023baf8(-1, ticks >> 5, strength);   // empty in retail
//
// The guard compares a float magnitude against the *remaining tick count*.
// That is what the disassembly does (`c.olt.s $f12, $f0` at 0x0022dd08), not a
// decompiler artefact; since magnitudes are fractions and durations are
// hundreds, it means a running shake cannot be restarted until it expires.
//
// The only consumer is FUN_0020bec8, the view-matrix builder, at
// 0x0020bf70-0x0020bfd8:
//
//   eye_z = DAT_0058c0b0 + fGpffff808c;            // + 0.4
//   if (uGpffffb6f8 != 0) {
//     a = min((short)uGpffffb6f8 * fGpffff8090,    // 0.0003125
//             fGpffffb6f4);
//     eye_z += a * sinf((short)uGpffffb6f8 / 40.0);
//     uGpffffb6f8 -= uGpffffb64c;                  // DAT_003555bc, frame ticks
//     if ((short)uGpffffb6f8 < 0) uGpffffb6f8 = 0;
//   }
//
// So the shake is a single vertical displacement of the eye, decaying on the
// ramp term rather than on the requested magnitude -- at the 200-tick duration
// s01_e012 uses, the ramp caps at 0.0625 and the scripted 0.3 never applies.
// FUN_0022a418:287 clears uGpffffb6f8 on scene load.
//
// Callers in s01_e012: subproc 4174 (body 0x9FDF) issues `0x94 300 200`, armed
// four times off the scheduler stream at 0xD860 during the closing exchange.

#include <stdint.h>

// VM evaluator (analyzed name for FUN_0025c258)
extern void bytecode_interpreter(void *result_out);

// Arm the camera shake (raw name kept; the function itself is not analyzed
// beyond what is written above).
extern void FUN_0022dcf0(float magnitude, int16_t duration_ticks);

// 100000.0, the shared world-coordinate scale.
extern float DAT_00352c34;

// Original signature: undefined8 FUN_002612e0(void)
uint64_t opcode_0x94_start_camera_shake(void)
{
  int32_t magnitude_raw;
  uint16_t duration_raw; // written by the second evaluate, at &magnitude_raw + 4

  bytecode_interpreter(&magnitude_raw);
  bytecode_interpreter((void *)((uint32_t)&magnitude_raw | 4));

  FUN_0022dcf0((float)magnitude_raw / DAT_00352c34, (int16_t)duration_raw);

  return 0;
}
