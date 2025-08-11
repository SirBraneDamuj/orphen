// Analysis of FUN_00237b38
// Original decompiled signature: void FUN_00237b38(long param_1)
//
// Inferred Role:
//   Initialize (or terminate) the active dialogue/cutscene text & control stream.
//   Sets the global pointer pcGpffffaec0 to the supplied stream base and, when starting a fresh stream,
//   seeds a large set of rendering / timing / style globals and clears previously active glyph entries.
//
// High-Level Behavior:
//   param_1 == 0:
//     * Terminates current stream (if any) and flips resource flags (enables 0x8ff / 0x8fe assets?).
//   param_1 != 0 and first byte at param_1 != 0x02:
//     * Treat as NEW stream start.
//       - Positions: uGpffffbcc8 (Y origin), uGpffffbccc (X origin)
//       - Timing: uGpffffbce0 set to 2, iGpffffbce4 = 600 (wrap/line budget), uGpffffbce8 = 0 (elapsed),
//         uGpffffaec8 = 0 (aux counter), uGpffffbd00 = -1 (timer sentinel).
//       - Color/style: uGpffffbcd4 = 0x80808080 (ARGB neutral gray), glyph slot color fields cleared.
//       - Clears ~300 glyph slots at iGpffffaed4 (stride 0x3C, markers at +0x04 / +0x2C set to constants).
//       - Resource toggling: disables 0x8ff / 0x8fe (opposite of termination path) to show dialogue layer.
//       - Resets list / scroll state: DAT_005716c0 = 0; sets uGpffffbcf4 = 8 (mode/state code).
//   param_1 != 0 and first byte == 0x02:
//     * Instead of initializing, it clears pcGpffffaec0 (acts like a guard / special script header causing abort).
//
// Observations:
//   * First stream byte acting as 0x02 sentinel may denote an alternative format or a control header; the routine
//     refuses to start if it encounters it on first activation, possibly meaning "not a dialogue script" or a
//     continuation chunk requiring different entry point.
//   * iGpffffaed4 base contains 300 entries * 0x3C bytes ≈ 0x2CA0 bytes. Loop writes sentinel pattern 0xFFFFEFF7
//     at +4 and 1 at +0x2C—likely invalid bounding box and active flag/time scaler reset.
//   * Resource function pairs FUN_002663d8 / FUN_002663a0 appear to enable/disable assets (maybe overlay textures
//     or sprite sheets). IDs: 0x509 (dialogue pane?), 0x8ff / 0x8fe (subtitle layers?).
//
// Future Work:
//   * Formalize globals into a struct to clarify relationships (position, timing, color, mode flags).
//   * Determine meaning of first-byte == 0x02 to decide whether to split to a separate loader.
//
// Naming: Provide dialogue_start_stream wrapper while preserving original symbol.
// NOTE: Raw function remains unmodified in src/.

#include "orphen_globals.h"

// External globals (types provisional)
extern char *pcGpffffaec0;        // active stream pointer
extern unsigned int uGpffffbcc8;  // Y origin (signed stored in unsigned?)
extern unsigned int uGpffffbccc;  // X origin
extern unsigned int uGpffffbce0;  // line(?) mode / spacing
extern int iGpffffbce4;           // wrap threshold (600)
extern unsigned int uGpffffaec8;  // auxiliary counter
extern unsigned int uGpffffbcd4;  // default color/style
extern int iGpffffaed4;           // glyph slot array base
extern unsigned int uGpffffbd00;  // timing sentinel
extern unsigned int uGpffffb0f4;  // global flags bitfield (UI / overlay)
extern unsigned int DAT_005716c0; // list size / scroll count
extern unsigned int uGpffffbcf4;  // state mode
extern unsigned int uGpffffbce8;  // elapsed counter

// External funcs
extern void FUN_002663d8(int); // enable resource
extern void FUN_002663a0(int); // disable resource
extern void FUN_00238f18(int); // likely clears glyph list (argument 0)

static void dialogue_start_stream_impl(long streamPtr)
{
  bool wasNull = (pcGpffffaec0 == 0);
  pcGpffffaec0 = (char *)streamPtr;
  if (streamPtr == 0 || *pcGpffffaec0 != 0x02)
  {
    // Resource: ensure 0x509 disabled while (re)starting unless terminating case sets opposite
    FUN_002663d8(0x509);
    if (wasNull)
    {
      uGpffffbcc8 = 0xFFFFFED0; // -0x130
      uGpffffbccc = 0xFFFFFF88; // -0x78
      uGpffffbce0 = 2;
      iGpffffbce4 = 600;
      uGpffffaec8 = 0;
      FUN_00238f18(0); // clear glyph records
      uGpffffbcd4 = 0x80808080;
      // Clear 300 glyph slots
      int count = 299;
      int p = iGpffffaed4;
      while (count-- >= 0)
      {
        *(unsigned int *)(p + 0x04) = 0xFFFFEFF7; // sentinel
        *(unsigned int *)(p + 0x2C) = 1;          // flag / init value
        p += 0x3C;
      }
      uGpffffbd00 = 0xFFFFFFFF;
    }
    if (streamPtr == 0)
    {
      // Terminate: disable dialogue assets, set flags
      FUN_002663a0(0x8FF);
      FUN_002663a0(0x8FE);
      uGpffffb0f4 |= 0x6000;
    }
    else
    {
      // Starting: enable assets
      FUN_002663d8(0x8FF);
      FUN_002663d8(0x8FE);
      uGpffffb0f4 &= 0x9FFF; // clear bits 0x6000
    }
    DAT_005716c0 = 0;
    uGpffffbcf4 = 8;
    uGpffffbce8 = 0;
  }
  else
  {
    // First byte 0x02: abort start (clears pointer)
    pcGpffffaec0 = 0;
  }
}

void dialogue_start_stream(long ptr) { dialogue_start_stream_impl(ptr); }

// Wrapper preserving original name
void FUN_00237b38(long param_1) { dialogue_start_stream_impl(param_1); }
